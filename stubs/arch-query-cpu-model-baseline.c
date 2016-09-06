#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/arch_init.h"
#include "qapi/qmp/qerror.h"

CpuModelBaselineInfo *arch_query_cpu_model_baseline(CpuModelInfo *modela,
                                                    CpuModelInfo *modelb,
                                                    Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
