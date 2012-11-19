#include "qemu-common.h"
#include "arch_init.h"
#include "qerror.h"

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    error_set(errp, QERR_NOT_SUPPORTED);
    return NULL;
}
