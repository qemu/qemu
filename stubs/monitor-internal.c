#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "../monitor/monitor-internal.h"

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
}

void monitor_fdsets_cleanup(void)
{
}
