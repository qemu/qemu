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

#include "qapi/qapi-commands-machine-target.h"
#include "cpu-qom.h"

static void riscv_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info = g_malloc0(sizeof(*info));
    const char *typename = object_class_get_name(oc);
    ObjectClass *dyn_class;

    info->name = g_strndup(typename,
                           strlen(typename) - strlen("-" TYPE_RISCV_CPU));
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
