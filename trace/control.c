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
#include "trace-root.h"

int trace_events_enabled_count;

typedef struct TraceEventGroup {
    TraceEvent **events;
} TraceEventGroup;

static TraceEventGroup *event_groups;
static size_t nevent_groups;
static uint32_t next_id;
static uint32_t next_vcpu_id;

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
        if (events[i]->vcpu_id == TRACE_VCPU_EVENT_NONE) {
            continue;
        }

        if (likely(next_vcpu_id < CPU_TRACE_DSTATE_MAX_EVENTS)) {
            events[i]->vcpu_id = next_vcpu_id++;
        } else {
            warn_report("too many vcpu trace events; dropping '%s'",
                        events[i]->name);
        }
    }
    event_groups = g_renew(TraceEventGroup, event_groups, nevent_groups + 1);
    event_groups[nevent_groups].events = events;
    nevent_groups++;
}


TraceEvent *trace_event_name(const char *name)
{
    assert(name != NULL);

    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (strcmp(trace_event_get_name(ev), name) == 0) {
            return ev;
        }
    }
    return NULL;
}

void trace_event_iter_init(TraceEventIter *iter, const char *pattern)
{
    iter->event = 0;
    iter->group = 0;
    iter->pattern = pattern;
}

TraceEvent *trace_event_iter_next(TraceEventIter *iter)
{
    while (iter->group < nevent_groups &&
           event_groups[iter->group].events[iter->event] != NULL) {
        TraceEvent *ev = event_groups[iter->group].events[iter->event];
        iter->event++;
        if (event_groups[iter->group].events[iter->event] == NULL) {
            iter->event = 0;
            iter->group++;
        }
        if (!iter->pattern ||
            g_pattern_match_simple(iter->pattern, trace_event_get_name(ev))) {
            return ev;
        }
    }

    return NULL;
}

void trace_list_events(void)
{
    TraceEventIter iter;
    TraceEvent *ev;
    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        fprintf(stderr, "%s\n", trace_event_get_name(ev));
    }
#ifdef CONFIG_TRACE_DTRACE
    fprintf(stderr, "This list of names of trace points may be incomplete "
                    "when using the DTrace/SystemTap backends.\n"
                    "Run 'qemu-trace-stap list %s' to print the full list.\n",
            error_get_progname());
#endif
}

static void do_trace_enable_events(const char *line_buf)
{
    const bool enable = ('-' != line_buf[0]);
    const char *line_ptr = enable ? line_buf : line_buf + 1;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(line_ptr);

    trace_event_iter_init(&iter, line_ptr);
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
    /*
     * If both the simple and the log backends are enabled, "--trace file"
     * only applies to the simple backend; use "-D" for the log
     * backend. However we should only override -D if we actually have
     * something to override it with.
     */
    if (file) {
        qemu_set_log_filename(file, &error_fatal);
    }
#else
    if (file) {
        fprintf(stderr, "error: --trace file=...: "
                "option not supported by the selected tracing backends\n");
        exit(1);
    }
#endif
}

void trace_fini_vcpu(CPUState *vcpu)
{
    TraceEventIter iter;
    TraceEvent *ev;

    trace_guest_cpu_exit(vcpu);

    trace_event_iter_init(&iter, NULL);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (trace_event_is_vcpu(ev) &&
            trace_event_get_state_static(ev) &&
            trace_event_get_vcpu_state_dynamic(vcpu, ev)) {
            /* must disable to affect the global counter */
            trace_event_set_vcpu_state_dynamic(vcpu, ev, false);
        }
    }
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

uint32_t trace_get_vcpu_event_count(void)
{
    return next_vcpu_id;
}
