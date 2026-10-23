/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "system/qmp-cpu.h"

CpuDefinitionInfoList *qmp_query_loongarch_cpu_definitions(Error **errp)
{
    g_assert_not_reached();
}

CpuModelExpansionInfo *
qmp_query_loongarch_cpu_model_expansion(CpuModelExpansionType type,
                                        CpuModelInfo *model,
                                        Error **errp)
{
    g_assert_not_reached();
}
