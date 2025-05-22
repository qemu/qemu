/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"

CpuModelExpansionInfo *
qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                              CpuModelInfo *model,
                              Error **errp)
{
    error_setg(errp, "CPU model expansion is not supported on this target");
    return NULL;
}

CpuDefinitionInfoList *
qmp_query_cpu_definitions(Error **errp)
{
    error_setg(errp, "CPU model definitions are not supported on this target");
    return NULL;
}
