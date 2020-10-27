#include "qemu/osdep.h"
#include "qapi/qmp/dispatch.h"

bool qmp_command_available(const QmpCommand *cmd, Error **errp)
{
    return true;
}
