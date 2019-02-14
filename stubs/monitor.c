#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-emit-events.h"
#include "qemu-common.h"
#include "monitor/monitor.h"

__thread Monitor *cur_mon;

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init(Chardev *chr, int flags)
{
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
}
