/*
 * QEMU LoongArch CPU (monitor definitions)
 *
 * SPDX-FileCopyrightText: 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "cpu.h"
#include "qobject/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qom/qom-qobject.h"

static void loongarch_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info = g_new0(CpuDefinitionInfo, 1);
    const char *typename = object_class_get_name(oc);

    info->name = cpu_model_from_type(typename);
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(target_cpu_type(), false);
    g_slist_foreach(list, loongarch_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}

static const char *cpu_model_advertised_features[] = {
    "lsx", "lasx", "lbt", "pmu", "kvm-pv-ipi", "kvm-steal-time", NULL
};

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    Visitor *visitor;
    bool ok;
    CpuModelExpansionInfo *expansion_info;
    QDict *qdict_out;
    ObjectClass *oc;
    Object *obj;
    const char *name;
    int i;

    if (type != CPU_MODEL_EXPANSION_TYPE_STATIC) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }

    if (model->props) {
        visitor = qobject_input_visitor_new(model->props);
        if (!visit_start_struct(visitor, "model.props", NULL, 0, errp)) {
            visit_free(visitor);
            return NULL;
        }

        ok = visit_check_struct(visitor, errp);
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (!ok) {
            return NULL;
        }
    }

    oc = cpu_class_by_name(TYPE_LOONGARCH_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a recognized LoongArch CPU type",
                   model->name);
        return NULL;
    }

    obj = object_new(object_class_get_name(oc));

    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    expansion_info->model->name = g_strdup(model->name);

    qdict_out = qdict_new();

    i = 0;
    while ((name = cpu_model_advertised_features[i++]) != NULL) {
        ObjectProperty *prop = object_property_find(obj, name);
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
    }

    object_unref(obj);

    return expansion_info;
}
