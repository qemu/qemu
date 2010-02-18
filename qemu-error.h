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

#ifndef QEMU_ERROR_H
#define QEMU_ERROR_H

void error_vprintf(const char *fmt, va_list ap);
void error_printf(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void error_report(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void qemu_error_internal(const char *file, int linenr, const char *func,
                         const char *fmt, ...)
                         __attribute__ ((format(printf, 4, 5)));

#define qemu_error_new(fmt, ...) \
    qemu_error_internal(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)

#endif
