/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "system/qmp-cpu.h"

CpuDefinitionInfoList *qmp_query_s390x_cpu_definitions(Error **errp)
{
    g_assert_not_reached();
}

CpuModelExpansionInfo *
qmp_query_s390x_cpu_model_expansion(CpuModelExpansionType type,
                                    CpuModelInfo *model,
                                    Error **errp)
{
    g_assert_not_reached();
}

CpuModelCompareInfo *
qmp_query_cpu_model_comparison(CpuModelInfo *infoa,
                               CpuModelInfo *infob,
                               Error **errp)
{
    error_setg(errp, "CPU model comparison is not supported on this target");
    return NULL;
}

CpuModelBaselineInfo *
qmp_query_cpu_model_baseline(CpuModelInfo *infoa,
                             CpuModelInfo *infob,
                             Error **errp)
{
    error_setg(errp, "CPU model baseline is not supported on this target");
    return NULL;
}
