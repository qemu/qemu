/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"
#include "qemu/help_option.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#ifdef CONFIG_TRACE_FTRACE
#include "trace/ftrace.h"
#endif
#ifdef CONFIG_TRACE_LOG
#include "qemu/log.h"
#endif
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"

int trace_events_enabled_count;
/*
 * Interpretation depends on wether the event has the 'vcpu' property:
 * - false: Boolean value indicating whether the event is active.
 * - true : Integral counting the number of vCPUs that have this event enabled.
 */
uint16_t trace_events_dstate[TRACE_EVENT_COUNT];
/* Marks events for late vCPU state init */
static bool trace_events_dstate_init[TRACE_EVENT_COUNT];

QemuOptsList qemu_trace_opts = {
    .name = "trace",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_trace_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "events",
            .type = QEMU_OPT_STRING,
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};


TraceEvent *trace_event_name(const char *name)
{
    assert(name != NULL);

    TraceEventID i;
    for (i = 0; i < trace_event_count(); i++) {
        TraceEvent *ev = trace_event_id(i);
        if (strcmp(trace_event_get_name(ev), name) == 0) {
            return ev;
        }
    }
    return NULL;
}

static bool pattern_glob(const char *pat, const char *ev)
{
    while (*pat != '\0' && *ev != '\0') {
        if (*pat == *ev) {
            pat++;
            ev++;
        }
        else if (*pat == '*') {
            if (pattern_glob(pat, ev+1)) {
                return true;
            } else if (pattern_glob(pat+1, ev)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    while (*pat == '*') {
        pat++;
    }

    if (*pat == '\0' && *ev == '\0') {
        return true;
    } else {
        return false;
    }
}

TraceEvent *trace_event_pattern(const char *pat, TraceEvent *ev)
{
    assert(pat != NULL);

    TraceEventID i;

    if (ev == NULL) {
        i = -1;
    } else {
        i = trace_event_get_id(ev);
    }
    i++;

    while (i < trace_event_count()) {
        TraceEvent *res = trace_event_id(i);
        if (pattern_glob(pat, trace_event_get_name(res))) {
            return res;
        }
        i++;
    }

    return NULL;
}

void trace_list_events(void)
{
    int i;
    for (i = 0; i < trace_event_count(); i++) {
        TraceEvent *res = trace_event_id(i);
        fprintf(stderr, "%s\n", trace_event_get_name(res));
    }
}

static void do_trace_enable_events(const char *line_buf)
{
    const bool enable = ('-' != line_buf[0]);
    const char *line_ptr = enable ? line_buf : line_buf + 1;

    if (trace_event_is_pattern(line_ptr)) {
        TraceEvent *ev = NULL;
        while ((ev = trace_event_pattern(line_ptr, ev)) != NULL) {
            if (trace_event_get_state_static(ev)) {
                /* start tracing */
                trace_event_set_state_dynamic(ev, enable);
                /* mark for late vCPU init */
                trace_events_dstate_init[ev->id] = true;
            }
        }
    } else {
        TraceEvent *ev = trace_event_name(line_ptr);
        if (ev == NULL) {
            error_report("WARNING: trace event '%s' does not exist",
                         line_ptr);
        } else if (!trace_event_get_state_static(ev)) {
            error_report("WARNING: trace event '%s' is not traceable",
                         line_ptr);
        } else {
            /* start tracing */
            trace_event_set_state_dynamic(ev, enable);
            /* mark for late vCPU init */
            trace_events_dstate_init[ev->id] = true;
        }
    }
}

void trace_enable_events(const char *line_buf)
{
    if (is_help_option(line_buf)) {
        trace_list_events();
        if (cur_mon == NULL) {
            exit(0);
        }
    } else {
        do_trace_enable_events(line_buf);
    }
}

static void trace_init_events(const char *fname)
{
    Location loc;
    FILE *fp;
    char line_buf[1024];
    size_t line_idx = 0;

    if (fname == NULL) {
        return;
    }

    loc_push_none(&loc);
    loc_set_file(fname, 0);
    fp = fopen(fname, "r");
    if (!fp) {
        error_report("%s", strerror(errno));
        exit(1);
    }
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        loc_set_file(fname, ++line_idx);
        size_t len = strlen(line_buf);
        if (len > 1) {              /* skip empty lines */
            line_buf[len - 1] = '\0';
            if ('#' == line_buf[0]) { /* skip commented lines */
                continue;
            }
            trace_enable_events(line_buf);
        }
    }
    if (fclose(fp) != 0) {
        loc_set_file(fname, 0);
        error_report("%s", strerror(errno));
        exit(1);
    }
    loc_pop(&loc);
}

void trace_init_file(const char *file)
{
#ifdef CONFIG_TRACE_SIMPLE
    st_set_trace_file(file);
#elif defined CONFIG_TRACE_LOG
    /* If both the simple and the log backends are enabled, "-trace file"
     * only applies to the simple backend; use "-D" for the log backend.
     */
    if (file) {
        qemu_set_log_filename(file, &error_fatal);
    }
#else
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backends\n");
        exit(1);
    }
#endif
}

bool trace_init_backends(void)
{
#ifdef CONFIG_TRACE_SIMPLE
    if (!st_init()) {
        fprintf(stderr, "failed to initialize simple tracing backend.\n");
        return false;
    }
#endif

#ifdef CONFIG_TRACE_FTRACE
    if (!ftrace_init()) {
        fprintf(stderr, "failed to initialize ftrace backend.\n");
        return false;
    }
#endif

    return true;
}

char *trace_opt_parse(const char *optarg)
{
    char *trace_file;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("trace"),
                                             optarg, true);
    if (!opts) {
        exit(1);
    }
    if (qemu_opt_get(opts, "enable")) {
        trace_enable_events(qemu_opt_get(opts, "enable"));
    }
    trace_init_events(qemu_opt_get(opts, "events"));
    trace_file = g_strdup(qemu_opt_get(opts, "file"));
    qemu_opts_del(opts);

    return trace_file;
}

void trace_init_vcpu_events(void)
{
    TraceEvent *ev = NULL;
    while ((ev = trace_event_pattern("*", ev)) != NULL) {
        if (trace_event_is_vcpu(ev) &&
            trace_event_get_state_static(ev) &&
            trace_events_dstate_init[ev->id]) {
            trace_event_set_state_dynamic(ev, true);
        }
    }
}
