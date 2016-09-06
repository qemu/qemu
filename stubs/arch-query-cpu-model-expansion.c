#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/arch_init.h"
#include "qapi/qmp/qerror.h"

CpuModelExpansionInfo *arch_query_cpu_model_expansion(CpuModelExpansionType type,
                                                      CpuModelInfo *mode,
                                                      Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
