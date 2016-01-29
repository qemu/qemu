#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/arch_init.h"
#include "qapi/qmp/qerror.h"

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
