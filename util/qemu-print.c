/*
 * Print to stream or current monitor
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qemu/qemu-print.h"

/*
 * Print like vprintf().
 * Print to current monitor if we have one, else to stdout.
 */
int qemu_vprintf(const char *fmt, va_list ap)
{
    Monitor *cur_mon = monitor_cur();
    if (cur_mon) {
        return monitor_vprintf(cur_mon, fmt, ap);
    }
    return vprintf(fmt, ap);
}

/*
 * Print like printf().
 * Print to current monitor if we have one, else to stdout.
 */
int qemu_printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = qemu_vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/*
 * Print like vfprintf()
 * Print to @stream if non-null, else to current monitor.
 */
int qemu_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    if (!stream) {
        return monitor_vprintf(monitor_cur(), fmt, ap);
    }
    return vfprintf(stream, fmt, ap);
}

/*
 * Print like fprintf().
 * Print to @stream if non-null, else to current monitor.
 */
int qemu_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = qemu_vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}
