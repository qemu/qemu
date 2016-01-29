#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"

UuidInfo *qmp_query_uuid(Error **errp)
{
    UuidInfo *info = g_malloc0(sizeof(*info));

    info->UUID = g_strdup(UUID_NONE);
    return info;
}

