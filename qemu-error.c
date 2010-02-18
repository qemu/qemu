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
 * Print current location to current monitor if we have one, else to stderr.
 */
void error_print_loc(void)
{
    switch (cur_loc->kind) {
    default: ;
    }
}

/*
 * Print an error message to current monitor if we have one, else to stderr.
 * Prepend the current location and append a newline.
 * It's wrong to call this in a QMP monitor.  Use qerror_report() there.
 */
void error_report(const char *fmt, ...)
{
    va_list ap;

    error_print_loc();
    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
    error_printf("\n");
}

void qerror_report_internal(const char *file, int linenr, const char *func,
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
