#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

void error_vprintf(const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
}

void error_vprintf_unless_qmp(const char *fmt, va_list ap)
{
    error_vprintf(fmt, ap);
}
