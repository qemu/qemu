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

#ifndef QEMU_ERROR_REPORT_H
#define QEMU_ERROR_REPORT_H

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

int error_vprintf(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
int error_printf(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
int error_vprintf_unless_qmp(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
int error_printf_unless_qmp(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

void error_vreport(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
void warn_vreport(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);
void info_vreport(const char *fmt, va_list ap) GCC_FMT_ATTR(1, 0);

void error_report(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
void warn_report(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
void info_report(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

bool error_report_once_cond(bool *printed, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);
bool warn_report_once_cond(bool *printed, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

void error_init(const char *argv0);

/*
 * Similar to error_report(), except it prints the message just once.
 * Return true when it prints, false otherwise.
 */
#define error_report_once(fmt, ...)                     \
    ({                                                  \
        static bool print_once_;                        \
        error_report_once_cond(&print_once_,            \
                               fmt, ##__VA_ARGS__);     \
    })

/*
 * Similar to warn_report(), except it prints the message just once.
 * Return true when it prints, false otherwise.
 */
#define warn_report_once(fmt, ...)                      \
    ({                                                  \
        static bool print_once_;                        \
        warn_report_once_cond(&print_once_,             \
                              fmt, ##__VA_ARGS__);      \
    })

const char *error_get_progname(void);

extern bool error_with_timestamp;

#endif
