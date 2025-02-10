/*
 * QEMU MIPS CPU (monitor definitions)
 *
 * SPDX-FileCopyrightText: 2012 SUSE LINUX Products GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-machine-target.h"
#include "cpu.h"

static void mips_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = cpu_model_from_type(typename);
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(TYPE_MIPS_CPU, false);
    g_slist_foreach(list, mips_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}
