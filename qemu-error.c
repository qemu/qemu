/*
 * Error reporting
 *
 * Copyright (C) 2010 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include "monitor.h"
#include "sysemu.h"

/*
 * Print to current monitor if we have one, else to stderr.
 * TODO should return int, so callers can calculate width, but that
 * requires surgery to monitor_vprintf().  Left for another day.
 */
void error_vprintf(const char *fmt, va_list ap)
{
    if (cur_mon) {
        monitor_vprintf(cur_mon, fmt, ap);
    } else {
        vfprintf(stderr, fmt, ap);
    }
}

/*
 * Print to current monitor if we have one, else to stderr.
 * TODO just like error_vprintf()
 */
void error_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
}

void qemu_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
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
