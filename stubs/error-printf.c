#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

void error_vprintf(const char *fmt, va_list ap)
{
    if (g_test_initialized() && !g_test_subprocess()) {
        char *msg = g_strdup_vprintf(fmt, ap);
        g_test_message("%s", msg);
        g_free(msg);
    } else {
        vfprintf(stderr, fmt, ap);
    }
}

void error_vprintf_unless_qmp(const char *fmt, va_list ap)
{
    error_vprintf(fmt, ap);
}
