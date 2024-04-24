#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qapi/qapi-emit-events.h"

Monitor *monitor_cur(void)
{
    return NULL;
}

Monitor *monitor_set_cur(Coroutine *co, Monitor *mon)
{
    return NULL;
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
}

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    abort();
}
