#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "monitor/monitor.h"

Monitor *cur_mon = NULL;

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init(Chardev *chr, int flags)
{
}
