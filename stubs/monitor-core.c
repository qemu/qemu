#include "qemu/osdep.h"
#include "monitor/hmp.h"

#ifdef CONFIG_HMP
int monitor_hmp_vprintf(MonitorHMP *mon, const char *fmt, va_list ap)
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
#endif
