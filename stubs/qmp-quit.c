#include "qemu/osdep.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qmp-registry.h"

void qmp_quit(Error **errp)
{
    g_assert_not_reached();
}
