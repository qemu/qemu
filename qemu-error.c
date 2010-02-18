#include <stdio.h>
#include "monitor.h"
#include "sysemu.h"

void qemu_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    if (cur_mon) {
        monitor_vprintf(cur_mon, fmt, args);
    } else {
        vfprintf(stderr, fmt, args);
    }
    va_end(args);
}

void qemu_error_internal(const char *file, int linenr, const char *func,
                         const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    va_start(va, fmt);
    qerror = qerror_from_info(file, linenr, func, fmt, &va);
    va_end(va);

    if (cur_mon) {
        monitor_set_error(cur_mon, qerror);
    } else {
        qerror_print(qerror);
        QDECREF(qerror);
    }
}
