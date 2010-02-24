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

typedef struct Location {
    /* all members are private to qemu-error.c */
    enum { LOC_NONE } kind;
    int num;
    const void *ptr;
    struct Location *prev;
} Location;

Location *loc_push_restore(Location *loc);
Location *loc_push_none(Location *loc);
Location *loc_pop(Location *loc);
Location *loc_save(Location *loc);
void loc_restore(Location *loc);
void loc_set_none(void);

void error_vprintf(const char *fmt, va_list ap);
void error_printf(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void error_print_loc(void);
void error_set_progname(const char *argv0);
void error_report(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
void qerror_report_internal(const char *file, int linenr, const char *func,
                            const char *fmt, ...)
    __attribute__ ((format(printf, 4, 5)));

#define qerror_report(fmt, ...) \
    qerror_report_internal(__FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)

#endif
