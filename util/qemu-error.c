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

/*
 * @report_type is the type of message: error, warning or
 * informational.
 */
typedef enum {
    REPORT_TYPE_ERROR,
    REPORT_TYPE_WARNING,
    REPORT_TYPE_INFO,
} report_type;

/* Prepend timestamp to messages */
bool message_with_timestamp;
bool error_with_guestname;
const char *error_guest_name;

int error_printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = error_vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int error_printf_unless_qmp(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = error_vprintf_unless_qmp(fmt, ap);
    va_end(ap);
    return ret;
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

/*
 * Print current location to current monitor if we have one, else to stderr.
 */
static void print_loc(void)
{
    const char *sep = "";
    int i;
    const char *const *argp;

    if (!monitor_cur() && g_get_prgname()) {
        fprintf(stderr, "%s:", g_get_prgname());
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

/*
 * Print a message to current monitor if we have one, else to stderr.
 * @report_type is the type of message: error, warning or informational.
 * Format arguments like vsprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 */
static void vreport(report_type type, const char *fmt, va_list ap)
{
    GTimeVal tv;
    gchar *timestr;

    if (message_with_timestamp && !monitor_cur()) {
        g_get_current_time(&tv);
        timestr = g_time_val_to_iso8601(&tv);
        error_printf("%s ", timestr);
        g_free(timestr);
    }

    /* Only prepend guest name if -msg guest-name and -name guest=... are set */
    if (error_with_guestname && error_guest_name && !monitor_cur()) {
        error_printf("%s ", error_guest_name);
    }

    print_loc();

    switch (type) {
    case REPORT_TYPE_ERROR:
        break;
    case REPORT_TYPE_WARNING:
        error_printf("warning: ");
        break;
    case REPORT_TYPE_INFO:
        error_printf("info: ");
        break;
    }

    error_vprintf(fmt, ap);
    error_printf("\n");
}

/*
 * Print an error message to current monitor if we have one, else to stderr.
 * Format arguments like vsprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 * It's wrong to call this in a QMP monitor.  Use error_setg() there.
 */
void error_vreport(const char *fmt, va_list ap)
{
    vreport(REPORT_TYPE_ERROR, fmt, ap);
}

/*
 * Print a warning message to current monitor if we have one, else to stderr.
 * Format arguments like vsprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 */
void warn_vreport(const char *fmt, va_list ap)
{
    vreport(REPORT_TYPE_WARNING, fmt, ap);
}

/*
 * Print an information message to current monitor if we have one, else to
 * stderr.
 * Format arguments like vsprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 */
void info_vreport(const char *fmt, va_list ap)
{
    vreport(REPORT_TYPE_INFO, fmt, ap);
}

/*
 * Print an error message to current monitor if we have one, else to stderr.
 * Format arguments like sprintf().  The resulting message should be
 * a single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 * It's wrong to call this in a QMP monitor.  Use error_setg() there.
 */
void error_report(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vreport(REPORT_TYPE_ERROR, fmt, ap);
    va_end(ap);
}

/*
 * Print a warning message to current monitor if we have one, else to stderr.
 * Format arguments like sprintf(). The resulting message should be a
 * single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 */
void warn_report(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vreport(REPORT_TYPE_WARNING, fmt, ap);
    va_end(ap);
}

/*
 * Print an information message to current monitor if we have one, else to
 * stderr.
 * Format arguments like sprintf(). The resulting message should be a
 * single phrase, with no newline or trailing punctuation.
 * Prepend the current location and append a newline.
 */
void info_report(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vreport(REPORT_TYPE_INFO, fmt, ap);
    va_end(ap);
}

/*
 * Like error_report(), except print just once.
 * If *printed is false, print the message, and flip *printed to true.
 * Return whether the message was printed.
 */
bool error_report_once_cond(bool *printed, const char *fmt, ...)
{
    va_list ap;

    assert(printed);
    if (*printed) {
        return false;
    }
    *printed = true;
    va_start(ap, fmt);
    vreport(REPORT_TYPE_ERROR, fmt, ap);
    va_end(ap);
    return true;
}

/*
 * Like warn_report(), except print just once.
 * If *printed is false, print the message, and flip *printed to true.
 * Return whether the message was printed.
 */
bool warn_report_once_cond(bool *printed, const char *fmt, ...)
{
    va_list ap;

    assert(printed);
    if (*printed) {
        return false;
    }
    *printed = true;
    va_start(ap, fmt);
    vreport(REPORT_TYPE_WARNING, fmt, ap);
    va_end(ap);
    return true;
}

static char *qemu_glog_domains;

static void qemu_log_func(const gchar *log_domain,
                          GLogLevelFlags log_level,
                          const gchar *message,
                          gpointer user_data)
{
    switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_DEBUG:
    case G_LOG_LEVEL_INFO:
        /*
         * Use same G_MESSAGES_DEBUG logic as glib to enable/disable debug
         * messages
         */
        if (qemu_glog_domains == NULL) {
            break;
        }
        if (strcmp(qemu_glog_domains, "all") != 0 &&
          (log_domain == NULL || !strstr(qemu_glog_domains, log_domain))) {
            break;
        }
        /* Fall through */
    case G_LOG_LEVEL_MESSAGE:
        info_report("%s%s%s",
                    log_domain ?: "", log_domain ? ": " : "", message);

        break;
    case G_LOG_LEVEL_WARNING:
        warn_report("%s%s%s",
                    log_domain ?: "", log_domain ? ": " : "", message);
        break;
    case G_LOG_LEVEL_CRITICAL:
    case G_LOG_LEVEL_ERROR:
        error_report("%s%s%s",
                     log_domain ?: "", log_domain ? ": " : "", message);
        break;
    }
}

void error_init(const char *argv0)
{
    const char *p = strrchr(argv0, '/');

    /* Set the program name for error_print_loc(). */
    g_set_prgname(p ? p + 1 : argv0);

    /*
     * This sets up glib logging so libraries using it also print their logs
     * through error_report(), warn_report(), info_report().
     */
    g_log_set_default_handler(qemu_log_func, NULL);
    g_warn_if_fail(qemu_glog_domains == NULL);
    qemu_glog_domains = g_strdup(g_getenv("G_MESSAGES_DEBUG"));
}
