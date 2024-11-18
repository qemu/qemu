/*
 * QEMU CPU QMP commands for RISC-V
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
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

#include "qapi/error.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor.h"
#include "qom/qom-qobject.h"
#include "system/kvm.h"
#include "system/tcg.h"
#include "cpu-qom.h"
#include "cpu.h"

static void riscv_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info = g_malloc0(sizeof(*info));
    const char *typename = object_class_get_name(oc);
    ObjectClass *dyn_class;

    info->name = cpu_model_from_type(typename);
    info->q_typename = g_strdup(typename);

    dyn_class = object_class_dynamic_cast(oc, TYPE_RISCV_DYNAMIC_CPU);
    info->q_static = dyn_class == NULL;

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list = object_class_get_list(TYPE_RISCV_CPU, false);

    g_slist_foreach(list, riscv_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}

static void riscv_check_if_cpu_available(RISCVCPU *cpu, Error **errp)
{
    if (!riscv_cpu_accelerator_compatible(cpu)) {
        g_autofree char *name = riscv_cpu_get_name(cpu);
        const char *accel = kvm_enabled() ? "kvm" : "tcg";

        error_setg(errp, "'%s' CPU not available with %s", name, accel);
        return;
    }
}

static void riscv_obj_add_qdict_prop(Object *obj, QDict *qdict_out,
                                     const char *name)
{
    ObjectProperty *prop = object_property_find(obj, name);

    if (prop) {
        QObject *value;

        assert(prop->get);
        value = object_property_get_qobject(obj, name, &error_abort);

        qdict_put_obj(qdict_out, name, value);
    }
}

static void riscv_obj_add_multiext_props(Object *obj, QDict *qdict_out,
                                         const RISCVCPUMultiExtConfig *arr)
{
    for (int i = 0; arr[i].name != NULL; i++) {
        riscv_obj_add_qdict_prop(obj, qdict_out, arr[i].name);
    }
}

static void riscv_obj_add_named_feats_qdict(Object *obj, QDict *qdict_out)
{
    const RISCVCPUMultiExtConfig *named_cfg;
    RISCVCPU *cpu = RISCV_CPU(obj);
    QObject *value;
    bool flag_val;

    for (int i = 0; riscv_cpu_named_features[i].name != NULL; i++) {
        named_cfg = &riscv_cpu_named_features[i];
        flag_val = isa_ext_is_enabled(cpu, named_cfg->offset);
        value = QOBJECT(qbool_from_bool(flag_val));

        qdict_put_obj(qdict_out, named_cfg->name, value);
    }
}

static void riscv_obj_add_profiles_qdict(Object *obj, QDict *qdict_out)
{
    RISCVCPUProfile *profile;
    QObject *value;

    for (int i = 0; riscv_profiles[i] != NULL; i++) {
        profile = riscv_profiles[i];
        value = QOBJECT(qbool_from_bool(profile->enabled));

        qdict_put_obj(qdict_out, profile->name, value);
    }
}

static void riscv_cpuobj_validate_qdict_in(Object *obj, QObject *props,
                                           const char *props_arg_name,
                                           Error **errp)
{
    const QDict *qdict_in;
    const QDictEntry *qe;
    Visitor *visitor;
    Error *local_err = NULL;

    visitor = qobject_input_visitor_new(props);
    if (!visit_start_struct(visitor, props_arg_name, NULL, 0, &local_err)) {
        goto err;
    }

    qdict_in = qobject_to(QDict, props);
    for (qe = qdict_first(qdict_in); qe; qe = qdict_next(qdict_in, qe)) {
        object_property_find_err(obj, qe->key, &local_err);
        if (local_err) {
            goto err;
        }

        object_property_set(obj, qe->key, visitor, &local_err);
        if (local_err) {
            goto err;
        }
    }

    visit_check_struct(visitor, &local_err);
    if (local_err) {
        goto err;
    }

    visit_end_struct(visitor, NULL);

err:
    error_propagate(errp, local_err);
    visit_free(visitor);
}

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    CpuModelExpansionInfo *expansion_info;
    QDict *qdict_out;
    ObjectClass *oc;
    Object *obj;
    Error *local_err = NULL;

    if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }

    oc = cpu_class_by_name(TYPE_RISCV_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a known RISC-V CPU type",
                   model->name);
        return NULL;
    }

    obj = object_new(object_class_get_name(oc));

    riscv_check_if_cpu_available(RISCV_CPU(obj), &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }

    if (model->props) {
        riscv_cpuobj_validate_qdict_in(obj, model->props, "model.props",
                                       &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            object_unref(obj);
            return NULL;
        }
    }

    riscv_cpu_finalize_features(RISCV_CPU(obj), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }

    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    expansion_info->model->name = g_strdup(model->name);

    qdict_out = qdict_new();

    riscv_obj_add_multiext_props(obj, qdict_out, riscv_cpu_extensions);
    riscv_obj_add_multiext_props(obj, qdict_out, riscv_cpu_experimental_exts);
    riscv_obj_add_multiext_props(obj, qdict_out, riscv_cpu_vendor_exts);
    riscv_obj_add_named_feats_qdict(obj, qdict_out);
    riscv_obj_add_profiles_qdict(obj, qdict_out);

    /* Add our CPU boolean options too */
    riscv_obj_add_qdict_prop(obj, qdict_out, "mmu");
    riscv_obj_add_qdict_prop(obj, qdict_out, "pmp");

    if (!qdict_size(qdict_out)) {
        qobject_unref(qdict_out);
    } else {
        expansion_info->model->props = QOBJECT(qdict_out);
    }

    object_unref(obj);

    return expansion_info;
}
