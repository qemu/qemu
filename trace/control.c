/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2014 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "trace/control.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#ifdef CONFIG_TRACE_FTRACE
#include "trace/ftrace.h"
#endif


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

void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    TraceEventID i;

    for (i = 0; i < trace_event_count(); i++) {
        TraceEvent *ev = trace_event_id(i);
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_event_get_name(ev), i,
                      trace_event_get_state_static(ev) &&
                      trace_event_get_state_dynamic(ev));
    }
}

static void trace_init_events(const char *fname)
{
    if (fname == NULL) {
        return;
    }

    FILE *fp = fopen(fname, "r");
    if (!fp) {
        fprintf(stderr, "error: could not open trace events file '%s': %s\n",
                fname, strerror(errno));
        exit(1);
    }
    char line_buf[1024];
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        size_t len = strlen(line_buf);
        if (len > 1) {              /* skip empty lines */
            line_buf[len - 1] = '\0';
            if ('#' == line_buf[0]) { /* skip commented lines */
                continue;
            }
            const bool enable = ('-' != line_buf[0]);
            char *line_ptr = enable ? line_buf : line_buf + 1;
            if (trace_event_is_pattern(line_ptr)) {
                TraceEvent *ev = NULL;
                while ((ev = trace_event_pattern(line_ptr, ev)) != NULL) {
                    if (trace_event_get_state_static(ev)) {
                        trace_event_set_state_dynamic(ev, enable);
                    }
                }
            } else {
                TraceEvent *ev = trace_event_name(line_ptr);
                if (ev == NULL) {
                    fprintf(stderr,
                            "WARNING: trace event '%s' does not exist\n",
                            line_ptr);
                } else if (!trace_event_get_state_static(ev)) {
                    fprintf(stderr,
                            "WARNING: trace event '%s' is not traceable\n",
                            line_ptr);
                } else {
                    trace_event_set_state_dynamic(ev, enable);
                }
            }
        }
    }
    if (fclose(fp) != 0) {
        fprintf(stderr, "error: closing file '%s': %s\n",
                fname, strerror(errno));
        exit(1);
    }
}

bool trace_init_backends(const char *events, const char *file)
{
#ifdef CONFIG_TRACE_SIMPLE
    if (!st_init(file)) {
        fprintf(stderr, "failed to initialize simple tracing backend.\n");
        return false;
    }
#else
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backends\n");
        return false;
    }
#endif

#ifdef CONFIG_TRACE_FTRACE
    if (!ftrace_init()) {
        fprintf(stderr, "failed to initialize ftrace backend.\n");
        return false;
    }
#endif

    trace_init_events(events);
    return true;
}
