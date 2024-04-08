#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/monitor.h"

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
}
