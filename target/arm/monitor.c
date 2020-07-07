/*
 * QEMU monitor.c for ARM.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "kvm_arm.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "qom/qom-qobject.h"

static GICCapability *gic_cap_new(int version)
{
    GICCapability *cap = g_new0(GICCapability, 1);
    cap->version = version;
    /* by default, support none */
    cap->emulated = false;
    cap->kernel = false;
    return cap;
}

static GICCapabilityList *gic_cap_list_add(GICCapabilityList *head,
                                           GICCapability *cap)
{
    GICCapabilityList *item = g_new0(GICCapabilityList, 1);
    item->value = cap;
    item->next = head;
    return item;
}

static inline void gic_cap_kvm_probe(GICCapability *v2, GICCapability *v3)
{
#ifdef CONFIG_KVM
    int fdarray[3];

    if (!kvm_arm_create_scratch_host_vcpu(NULL, fdarray, NULL)) {
        return;
    }

    /* Test KVM GICv2 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V2)) {
        v2->kernel = true;
    }

    /* Test KVM GICv3 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V3)) {
        v3->kernel = true;
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);
#endif
}

GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    GICCapabilityList *head = NULL;
    GICCapability *v2 = gic_cap_new(2), *v3 = gic_cap_new(3);

    v2->emulated = true;
    v3->emulated = true;

    gic_cap_kvm_probe(v2, v3);

    head = gic_cap_list_add(head, v2);
    head = gic_cap_list_add(head, v3);

    return head;
}

QEMU_BUILD_BUG_ON(ARM_MAX_VQ > 16);

/*
 * These are cpu model features we want to advertise. The order here
 * matters as this is the order in which qmp_query_cpu_model_expansion
 * will attempt to set them. If there are dependencies between features,
 * then the order that considers those dependencies must be used.
 */
static const char *cpu_model_advertised_features[] = {
    "aarch64", "pmu", "sve",
    "sve128", "sve256", "sve384", "sve512",
    "sve640", "sve768", "sve896", "sve1024", "sve1152", "sve1280",
    "sve1408", "sve1536", "sve1664", "sve1792", "sve1920", "sve2048",
    "kvm-no-adjvtime",
    NULL
};

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    CpuModelExpansionInfo *expansion_info;
    const QDict *qdict_in = NULL;
    QDict *qdict_out;
    ObjectClass *oc;
    Object *obj;
    const char *name;
    int i;

    if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }

    if (!kvm_enabled() && !strcmp(model->name, "host")) {
        error_setg(errp, "The CPU type '%s' requires KVM", model->name);
        return NULL;
    }

    oc = cpu_class_by_name(TYPE_ARM_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a recognized ARM CPU type",
                   model->name);
        return NULL;
    }

    if (kvm_enabled()) {
        bool supported = false;

        if (!strcmp(model->name, "host") || !strcmp(model->name, "max")) {
            /* These are kvmarm's recommended cpu types */
            supported = true;
        } else if (current_machine->cpu_type) {
            const char *cpu_type = current_machine->cpu_type;
            int len = strlen(cpu_type) - strlen(ARM_CPU_TYPE_SUFFIX);

            if (strlen(model->name) == len &&
                !strncmp(model->name, cpu_type, len)) {
                /* KVM is enabled and we're using this type, so it works. */
                supported = true;
            }
        }
        if (!supported) {
            error_setg(errp, "We cannot guarantee the CPU type '%s' works "
                             "with KVM on this host", model->name);
            return NULL;
        }
    }

    if (model->props) {
        qdict_in = qobject_to(QDict, model->props);
        if (!qdict_in) {
            error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            return NULL;
        }
    }

    obj = object_new(object_class_get_name(oc));

    if (qdict_in) {
        Visitor *visitor;
        Error *err = NULL;

        visitor = qobject_input_visitor_new(model->props);
        if (!visit_start_struct(visitor, NULL, NULL, 0, errp)) {
            visit_free(visitor);
            object_unref(obj);
            return NULL;
        }

        i = 0;
        while ((name = cpu_model_advertised_features[i++]) != NULL) {
            if (qdict_get(qdict_in, name)) {
                if (!object_property_set(obj, name, visitor, &err)) {
                    break;
                }
            }
        }

        if (!err) {
            visit_check_struct(visitor, &err);
        }
        if (!err) {
            arm_cpu_finalize_features(ARM_CPU(obj), &err);
        }
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (err) {
            object_unref(obj);
            error_propagate(errp, err);
            return NULL;
        }
    } else {
        arm_cpu_finalize_features(ARM_CPU(obj), &error_abort);
    }

    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    expansion_info->model->name = g_strdup(model->name);

    qdict_out = qdict_new();

    i = 0;
    while ((name = cpu_model_advertised_features[i++]) != NULL) {
        ObjectProperty *prop = object_property_find(obj, name, NULL);
        if (prop) {
            QObject *value;

            assert(prop->get);
            value = object_property_get_qobject(obj, name, &error_abort);

            qdict_put_obj(qdict_out, name, value);
        }
    }

    if (!qdict_size(qdict_out)) {
        qobject_unref(qdict_out);
    } else {
        expansion_info->model->props = QOBJECT(qdict_out);
        expansion_info->model->has_props = true;
    }

    object_unref(obj);

    return expansion_info;
}
