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
    /*
     * Pretend 'g_test_message' is our monitor console to
     * stop the caller sending messages to stderr
     */
    if (g_test_initialized() && !g_test_subprocess() &&
        getenv("QTEST_SILENT_ERRORS")) {
        char *msg = g_strdup_vprintf(fmt, ap);
        g_test_message("%s", msg);
        size_t ret = strlen(msg);
        g_free(msg);
        return ret;
    }
    return -1;
}
