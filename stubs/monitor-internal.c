#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/hmp.h"

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_new_hmp(const char *id, const char *chardev_id,
                     bool use_readline, Error **errp)
{
    g_assert_not_reached();
}
