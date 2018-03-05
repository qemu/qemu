#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
