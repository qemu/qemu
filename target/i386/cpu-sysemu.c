/*
 *  i386 CPUID, CPU class, definitions, models: sysemu-only code
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/xen.h"
#include "sysemu/whpx.h"
#include "kvm/kvm_i386.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-run-state.h"
#include "qapi/qmp/qdict.h"
#include "qom/qom-qobject.h"
#include "qapi/qapi-commands-machine-target.h"
#include "hw/qdev-properties.h"

#include "exec/address-spaces.h"
#include "hw/i386/apic_internal.h"

#include "cpu-internal.h"

/* Return a QDict containing keys for all properties that can be included
 * in static expansion of CPU models. All properties set by x86_cpu_load_model()
 * must be included in the dictionary.
 */
static QDict *x86_cpu_static_props(void)
{
    FeatureWord w;
    int i;
    static const char *props[] = {
        "min-level",
        "min-xlevel",
        "family",
        "model",
        "stepping",
        "model-id",
        "vendor",
        "lmce",
        NULL,
    };
    static QDict *d;

    if (d) {
        return d;
    }

    d = qdict_new();
    for (i = 0; props[i]; i++) {
        qdict_put_null(d, props[i]);
    }

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *fi = &feature_word_info[w];
        int bit;
        for (bit = 0; bit < 64; bit++) {
            if (!fi->feat_names[bit]) {
                continue;
            }
            qdict_put_null(d, fi->feat_names[bit]);
        }
    }

    return d;
}

/* Add an entry to @props dict, with the value for property. */
static void x86_cpu_expand_prop(X86CPU *cpu, QDict *props, const char *prop)
{
    QObject *value = object_property_get_qobject(OBJECT(cpu), prop,
                                                 &error_abort);

    qdict_put_obj(props, prop, value);
}

/* Convert CPU model data from X86CPU object to a property dictionary
 * that can recreate exactly the same CPU model.
 */
static void x86_cpu_to_dict(X86CPU *cpu, QDict *props)
{
    QDict *sprops = x86_cpu_static_props();
    const QDictEntry *e;

    for (e = qdict_first(sprops); e; e = qdict_next(sprops, e)) {
        const char *prop = qdict_entry_key(e);
        x86_cpu_expand_prop(cpu, props, prop);
    }
}

/* Convert CPU model data from X86CPU object to a property dictionary
 * that can recreate exactly the same CPU model, including every
 * writeable QOM property.
 */
static void x86_cpu_to_dict_full(X86CPU *cpu, QDict *props)
{
    ObjectPropertyIterator iter;
    ObjectProperty *prop;

    object_property_iter_init(&iter, OBJECT(cpu));
    while ((prop = object_property_iter_next(&iter))) {
        /* skip read-only or write-only properties */
        if (!prop->get || !prop->set) {
            continue;
        }

        /* "hotplugged" is the only property that is configurable
         * on the command-line but will be set differently on CPUs
         * created using "-cpu ... -smp ..." and by CPUs created
         * on the fly by x86_cpu_from_model() for querying. Skip it.
         */
        if (!strcmp(prop->name, "hotplugged")) {
            continue;
        }
        x86_cpu_expand_prop(cpu, props, prop->name);
    }
}

static void object_apply_props(Object *obj, QDict *props, Error **errp)
{
    const QDictEntry *prop;

    for (prop = qdict_first(props); prop; prop = qdict_next(props, prop)) {
        if (!object_property_set_qobject(obj, qdict_entry_key(prop),
                                         qdict_entry_value(prop), errp)) {
            break;
        }
    }
}

/* Create X86CPU object according to model+props specification */
static X86CPU *x86_cpu_from_model(const char *model, QDict *props, Error **errp)
{
    X86CPU *xc = NULL;
    X86CPUClass *xcc;
    Error *err = NULL;

    xcc = X86_CPU_CLASS(cpu_class_by_name(TYPE_X86_CPU, model));
    if (xcc == NULL) {
        error_setg(&err, "CPU model '%s' not found", model);
        goto out;
    }

    xc = X86_CPU(object_new_with_class(OBJECT_CLASS(xcc)));
    if (props) {
        object_apply_props(OBJECT(xc), props, &err);
        if (err) {
            goto out;
        }
    }

    x86_cpu_expand_features(xc, &err);
    if (err) {
        goto out;
    }

out:
    if (err) {
        error_propagate(errp, err);
        object_unref(OBJECT(xc));
        xc = NULL;
    }
    return xc;
}

CpuModelExpansionInfo *
qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                      CpuModelInfo *model,
                                                      Error **errp)
{
    X86CPU *xc = NULL;
    Error *err = NULL;
    CpuModelExpansionInfo *ret = g_new0(CpuModelExpansionInfo, 1);
    QDict *props = NULL;
    const char *base_name;

    xc = x86_cpu_from_model(model->name,
                            model->has_props ?
                                qobject_to(QDict, model->props) :
                                NULL, &err);
    if (err) {
        goto out;
    }

    props = qdict_new();
    ret->model = g_new0(CpuModelInfo, 1);
    ret->model->props = QOBJECT(props);
    ret->model->has_props = true;

    switch (type) {
    case CPU_MODEL_EXPANSION_TYPE_STATIC:
        /* Static expansion will be based on "base" only */
        base_name = "base";
        x86_cpu_to_dict(xc, props);
    break;
    case CPU_MODEL_EXPANSION_TYPE_FULL:
        /* As we don't return every single property, full expansion needs
         * to keep the original model name+props, and add extra
         * properties on top of that.
         */
        base_name = model->name;
        x86_cpu_to_dict_full(xc, props);
    break;
    default:
        error_setg(&err, "Unsupported expansion type");
        goto out;
    }

    x86_cpu_to_dict(xc, props);

    ret->model->name = g_strdup(base_name);

out:
    object_unref(OBJECT(xc));
    if (err) {
        error_propagate(errp, err);
        qapi_free_CpuModelExpansionInfo(ret);
        ret = NULL;
    }
    return ret;
}

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

bool cpu_is_bsp(X86CPU *cpu)
{
    return cpu_get_apic_base(cpu->apic_state) & MSR_IA32_APICBASE_BSP;
}

/* TODO: remove me, when reset over QOM tree is implemented */
void x86_cpu_machine_reset_cb(void *opaque)
{
    X86CPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}

APICCommonClass *apic_get_class(void)
{
    const char *apic_type = "apic";

    /* TODO: in-kernel irqchip for hvf */
    if (kvm_apic_in_kernel()) {
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    } else if (whpx_apic_in_platform()) {
        apic_type = "whpx-apic";
    }

    return APIC_COMMON_CLASS(object_class_by_name(apic_type));
}

void x86_cpu_apic_create(X86CPU *cpu, Error **errp)
{
    APICCommonState *apic;
    ObjectClass *apic_class = OBJECT_CLASS(apic_get_class());

    cpu->apic_state = DEVICE(object_new_with_class(apic_class));

    object_property_add_child(OBJECT(cpu), "lapic",
                              OBJECT(cpu->apic_state));
    object_unref(OBJECT(cpu->apic_state));

    qdev_prop_set_uint32(cpu->apic_state, "id", cpu->apic_id);
    /* TODO: convert to link<> */
    apic = APIC_COMMON(cpu->apic_state);
    apic->cpu = cpu;
    apic->apicbase = APIC_DEFAULT_ADDRESS | MSR_IA32_APICBASE_ENABLE;
}

void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
    APICCommonState *apic;
    static bool apic_mmio_map_once;

    if (cpu->apic_state == NULL) {
        return;
    }
    qdev_realize(DEVICE(cpu->apic_state), NULL, errp);

    /* Map APIC MMIO area */
    apic = APIC_COMMON(cpu->apic_state);
    if (!apic_mmio_map_once) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            apic->apicbase &
                                            MSR_IA32_APICBASE_BASE,
                                            &apic->io_memory,
                                            0x1000);
        apic_mmio_map_once = true;
     }
}

GuestPanicInformation *x86_cpu_get_crash_info(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    GuestPanicInformation *panic_info = NULL;

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_CRASH)) {
        panic_info = g_new0(GuestPanicInformation, 1);

        panic_info->type = GUEST_PANIC_INFORMATION_TYPE_HYPER_V;

        assert(HV_CRASH_PARAMS >= 5);
        panic_info->u.hyper_v.arg1 = env->msr_hv_crash_params[0];
        panic_info->u.hyper_v.arg2 = env->msr_hv_crash_params[1];
        panic_info->u.hyper_v.arg3 = env->msr_hv_crash_params[2];
        panic_info->u.hyper_v.arg4 = env->msr_hv_crash_params[3];
        panic_info->u.hyper_v.arg5 = env->msr_hv_crash_params[4];
    }

    return panic_info;
}
void x86_cpu_get_crash_info_qom(Object *obj, Visitor *v,
                                const char *name, void *opaque,
                                Error **errp)
{
    CPUState *cs = CPU(obj);
    GuestPanicInformation *panic_info;

    if (!cs->crash_occurred) {
        error_setg(errp, "No crash occurred");
        return;
    }

    panic_info = x86_cpu_get_crash_info(cs);
    if (panic_info == NULL) {
        error_setg(errp, "No crash information");
        return;
    }

    visit_type_GuestPanicInformation(v, "crash-information", &panic_info,
                                     errp);
    qapi_free_GuestPanicInformation(panic_info);
}

