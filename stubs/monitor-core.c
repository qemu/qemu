#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qemu-common.h"
#include "qapi/qapi-emit-events.h"

__thread Monitor *cur_mon;

void monitor_init_qmp(Chardev *chr, bool pretty, Error **errp)
{
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
}

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    abort();
}


