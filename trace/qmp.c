/*
 * QMP commands for tracing events.
 *
 * Copyright (C) 2014-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-trace.h"
#include "control.h"
#include "qemu/error-report.h"


static bool check_events(bool is_pattern,
                         const char *name, Error **errp)
{
    if (!is_pattern) {
        TraceEvent *ev = trace_event_name(name);

        /* error for non-existing event */
        if (ev == NULL) {
            error_setg(errp, "unknown event \"%s\"", name);
            return false;
        }
    }
    return true;
}

TraceEventInfoList *qmp_trace_event_get_state(const char *name,
                                              Error **errp)
{
    TraceEventInfoList *events = NULL;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(name);

    /* Check events */
    if (!check_events(is_pattern, name, errp)) {
        return NULL;
    }

    /* Get states (all errors checked above) */
    trace_event_iter_init_pattern(&iter, name);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        TraceEventInfo *value;

        value = g_new(TraceEventInfo, 1);
        value->name = g_strdup(trace_event_get_name(ev));

        if (trace_event_get_state_dynamic(ev)) {
            value->state = TRACE_EVENT_STATE_ENABLED;
        } else {
            value->state = TRACE_EVENT_STATE_DISABLED;
        }
        QAPI_LIST_PREPEND(events, value);
    }

    return events;
}

void qmp_trace_event_set_state(const char *name, bool enable,
                               bool has_ignore_unavailable, bool ignore_unavailable,
                               Error **errp)
{
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(name);

    if (has_ignore_unavailable && ignore_unavailable) {
        warn_report_once("the 'ignore-unavailable' property is deprecated "
                         "and has no effect");
    }

    /* Check events */
    if (!check_events(is_pattern, name, errp)) {
        return;
    }

    /* Apply changes (all errors checked above) */
    trace_event_iter_init_pattern(&iter, name);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        trace_event_set_state_dynamic(ev, enable);
    }
}
