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
#include "qemu/option.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#ifdef CONFIG_TRACE_FTRACE
#include "trace/ftrace.h"
#endif
#ifdef CONFIG_TRACE_LOG
#include "qemu/log.h"
#endif
#ifdef CONFIG_TRACE_SYSLOG
#include <syslog.h>
#endif
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"

int trace_events_enabled_count;

typedef struct TraceEventGroup {
    TraceEvent **events;
} TraceEventGroup;

static TraceEventGroup *event_groups;
static size_t nevent_groups;
static uint32_t next_id;
static uint32_t next_vcpu_id;
static bool init_trace_on_startup;
static char *trace_opts_file;

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


void trace_event_register_group(TraceEvent **events)
{
    size_t i;
    for (i = 0; events[i] != NULL; i++) {
        events[i]->id = next_id++;
    }
    event_groups = g_renew(TraceEventGroup, event_groups, nevent_groups + 1);
    event_groups[nevent_groups].events = events;
    nevent_groups++;

#ifdef CONFIG_TRACE_SIMPLE
    st_init_group(nevent_groups - 1);
#endif
}


TraceEvent *trace_event_name(const char *name)
{
    assert(name != NULL);

    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init_all(&iter);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (strcmp(trace_event_get_name(ev), name) == 0) {
            return ev;
        }
    }
    return NULL;
}

void trace_event_iter_init_all(TraceEventIter *iter)
{
    iter->event = 0;
    iter->group = 0;
    iter->group_id = -1;
    iter->pattern = NULL;
}

void trace_event_iter_init_pattern(TraceEventIter *iter, const char *pattern)
{
    trace_event_iter_init_all(iter);
    iter->pattern = pattern;
}

void trace_event_iter_init_group(TraceEventIter *iter, size_t group_id)
{
    trace_event_iter_init_all(iter);
    iter->group_id = group_id;
}

TraceEvent *trace_event_iter_next(TraceEventIter *iter)
{
    while (iter->group < nevent_groups &&
           event_groups[iter->group].events[iter->event] != NULL) {
        TraceEvent *ev = event_groups[iter->group].events[iter->event];
        size_t group = iter->group;
        iter->event++;
        if (event_groups[iter->group].events[iter->event] == NULL) {
            iter->event = 0;
            iter->group++;
        }
        if (iter->pattern &&
            !g_pattern_match_simple(iter->pattern, trace_event_get_name(ev))) {
            continue;
        }
        if (iter->group_id != -1 &&
            iter->group_id != group) {
            continue;
        }
        return ev;
    }

    return NULL;
}

void trace_list_events(FILE *f)
{
    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init_all(&iter);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        fprintf(f, "%s\n", trace_event_get_name(ev));
    }
#ifdef CONFIG_TRACE_DTRACE
    fprintf(f, "This list of names of trace points may be incomplete "
               "when using the DTrace/SystemTap backends.\n"
               "Run 'qemu-trace-stap list %s' to print the full list.\n",
            g_get_prgname());
#endif
}

static void do_trace_enable_events(const char *line_buf)
{
    const bool enable = ('-' != line_buf[0]);
    const char *line_ptr = enable ? line_buf : line_buf + 1;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(line_ptr);

    trace_event_iter_init_pattern(&iter, line_ptr);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (!trace_event_get_state_static(ev)) {
            if (!is_pattern) {
                warn_report("trace event '%s' is not traceable",
                            line_ptr);
                return;
            }
            continue;
        }

        /* start tracing */
        trace_event_set_state_dynamic(ev, enable);
        if (!is_pattern) {
            return;
        }
    }

    if (!is_pattern) {
        warn_report("trace event '%s' does not exist",
                    line_ptr);
    }
}

void trace_enable_events(const char *line_buf)
{
    if (is_help_option(line_buf)) {
        trace_list_events(stdout);
        if (monitor_cur() == NULL) {
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

void trace_init_file(void)
{
#ifdef CONFIG_TRACE_SIMPLE
    st_set_trace_file(trace_opts_file);
    if (init_trace_on_startup) {
        st_set_trace_file_enabled(true);
    }
#elif defined CONFIG_TRACE_LOG
    /*
     * If both the simple and the log backends are enabled, "--trace file"
     * only applies to the simple backend; use "-D" for the log
     * backend. However we should only override -D if we actually have
     * something to override it with.
     */
    if (trace_opts_file) {
        qemu_set_log_filename(trace_opts_file, &error_fatal);
    }
#else
    if (trace_opts_file) {
        fprintf(stderr, "error: --trace file=...: "
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

#ifdef CONFIG_TRACE_SYSLOG
    openlog(NULL, LOG_PID, LOG_DAEMON);
#endif

    return true;
}

void trace_opt_parse(const char *optstr)
{
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("trace"),
                                             optstr, true);
    if (!opts) {
        exit(1);
    }
    if (qemu_opt_get(opts, "enable")) {
        trace_enable_events(qemu_opt_get(opts, "enable"));
    }
    trace_init_events(qemu_opt_get(opts, "events"));
    init_trace_on_startup = true;
    g_free(trace_opts_file);
    trace_opts_file = g_strdup(qemu_opt_get(opts, "file"));
    qemu_opts_del(opts);
}

uint32_t trace_get_vcpu_event_count(void)
{
    return next_vcpu_id;
}
