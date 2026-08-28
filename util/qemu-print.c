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
#include "monitor/hmp.h"
#include "qom/object.h"
#include "qemu/qemu-print.h"

/*
 * Print like vprintf().
 * Print to current monitor if we have one, else to stdout.
 * (if the monitor is QMP, fail without printing anything)
 */
int qemu_vprintf(const char *fmt, va_list ap)
{
    Monitor *cur_mon = monitor_cur();

    /* for all monitors: QMP & HMP */
    if (cur_mon) {
        /* don't use monitor_cur_hmp(), to avoid a second lookup */
        MonitorHMP *hmp = (MonitorHMP *)
            object_dynamic_cast(OBJECT(cur_mon), TYPE_MONITOR_HMP);
        if (!hmp) {
            return -1;
        }
        return monitor_hmp_vprintf(hmp, fmt, ap);
    }
    return vprintf(fmt, ap);
}

/*
 * Print like printf().
 * Print to current monitor if we have one, else to stdout.
 * (if the monitor is QMP, fail without printing anything)
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
 * Print to @stream if non-null, else to current HMP monitor if we
 * have one, else fail without printing anything.
 * Return number of characters printed on success, negative value on
 * error.
 */
int qemu_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    if (!stream) {
        MonitorHMP *hmp = monitor_cur_hmp();
        if (!hmp) {
            return -1;
        }
        return monitor_hmp_vprintf(hmp, fmt, ap);
    }
    return vfprintf(stream, fmt, ap);
}

/*
 * Print like fprintf().
 * Print to @stream if non-null, else to current HMP monitor if we
 * have one, else fail without printing anything.
 * Return number of characters printed on success, negative value on
 * error.
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
