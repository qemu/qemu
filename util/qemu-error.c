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

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "qemu/error-report.h"

void error_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
}

void error_printf_unless_qmp(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf_unless_qmp(fmt, ap);
    va_end(ap);
}

static Location std_loc = {
    .kind = LOC_NONE
};
static Location *cur_loc = &std_loc;

/*
 * Push location saved in LOC onto the location stack, return it.
 * The top of that stack is the current location.
 * Needs a matching loc_pop().
 */
Location *loc_push_restore(Location *loc)
{
    assert(!loc->prev);
    loc->prev = cur_loc;
    cur_loc = loc;
    return loc;
}

/*
 * Initialize *LOC to "nowhere", push it onto the location stack.
 * The top of that stack is the current location.
 * Needs a matching loc_pop().
 * Return LOC.
 */
Location *loc_push_none(Location *loc)
{
    loc->kind = LOC_NONE;
    loc->prev = NULL;
    return loc_push_restore(loc);
}

/*
 * Pop the location stack.
 * LOC must be the current location, i.e. the top of the stack.
 */
Location *loc_pop(Location *loc)
{
    assert(cur_loc == loc && loc->prev);
    cur_loc = loc->prev;
    loc->prev = NULL;
    return loc;
}

/*
 * Save the current location in LOC, return LOC.
 */
Location *loc_save(Location *loc)
{
    *loc = *cur_loc;
    loc->prev = NULL;
    return loc;
}

/*
 * Change the current location to the one saved in LOC.
 */
void loc_restore(Location *loc)
{
    Location *prev = cur_loc->prev;
    assert(!loc->prev);
    *cur_loc = *loc;
    cur_loc->prev = prev;
}

/*
 * Change the current location to "nowhere in particular".
 */
void loc_set_none(void)
{
    cur_loc->kind = LOC_NONE;
}

/*
 * Change the current location to argument ARGV[IDX..IDX+CNT-1].
 */
void loc_set_cmdline(char **argv, int idx, int cnt)
{
    cur_loc->kind = LOC_CMDLINE;
    cur_loc->num = cnt;
    cur_loc->ptr = argv + idx;
}

/*
 * Change the current location to file FNAME, line LNO.
 */
void loc_set_file(const char *fname, int lno)
{
    assert (fname || cur_loc->kind == LOC_FILE);
    cur_loc->kind = LOC_FILE;
    cur_loc->num = lno;
    if (fname) {
        cur_loc->ptr = fname;
    }
}

static const char *progname;

/*
 * Set the program name for error_print_loc().
 */
void error_set_progname(const char *argv0)
{
    const char *p = strrchr(argv0, '/');
    progname = p ? p + 1 : argv0;
}

const char *error_get_progname(void)
{
    return progname;
}

/*
 * Print current location to current monitor if we have one, else to stderr.
 */
static void error_print_loc(void)
{
    const char *sep = "";
    int i;
    const char *const *argp;

    if (!cur_mon && progname) {
        fprintf(stderr, "%s:", progname);
        sep = " ";
    }
    switch (cur_loc->kind) {
    case LOC_CMDLINE:
        argp = cur_loc->ptr;
        for (i = 0; i < cur_loc->num; i++) {
            error_printf("%s%s", sep, argp[i]);
            sep = " ";
        }
        error_printf(": ");
        break;
    case LOC_FILE:
        error_printf("%s:", (const char *)cur_loc->ptr);
        if (cur_loc->num) {
            error_printf("%d:", cur_loc->num);
        }
        error_printf(" ");
        break;
    default:
        error_printf("%s", sep);
    }
}

bool enable_timestamp_msg;
/*
 * Print an error message to current monitor if we have one, else to stderr.
 * Format arguments like vsprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 * It's wrong to call this in a QMP monitor.  Use error_setg() there.
 */
void error_vreport(const char *fmt, va_list ap)
{
    GTimeVal tv;
    gchar *timestr;

    if (enable_timestamp_msg && !cur_mon) {
        g_get_current_time(&tv);
        timestr = g_time_val_to_iso8601(&tv);
        error_printf("%s ", timestr);
        g_free(timestr);
    }

    error_print_loc();
    error_vprintf(fmt, ap);
    error_printf("\n");
}

/*
 * Print an error message to current monitor if we have one, else to stderr.
 * Format arguments like sprintf().  The resulting message should be a
 * single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 * It's wrong to call this in a QMP monitor.  Use error_setg() there.
 */
void error_report(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);
}
