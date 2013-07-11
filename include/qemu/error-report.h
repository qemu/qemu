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

#include <stdarg.h>
#include <stdbool.h>
#include "qemu/compiler.h"

typedef struct Location {
    /* all members are private to qemu-error.c */
    enum { LOC_NONE, LOC_CMDLINE, LOC_FILE } kind;
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
void loc_set_cmdline(char **argv, int idx, int cnt);
void loc_set_file(const char *fname, int lno);

void error_vprintf(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
void error_printf(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
void error_printf_unless_qmp(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
void error_print_loc(void);
void error_set_progname(const char *argv0);
void error_report(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
const char *error_get_progname(void);
extern bool enable_timestamp_msg;

#endif
