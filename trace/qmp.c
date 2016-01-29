/*
 * QMP commands for tracing events.
 *
 * Copyright (C) 2014 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "qmp-commands.h"
#include "trace/control.h"


TraceEventInfoList *qmp_trace_event_get_state(const char *name, Error **errp)
{
    TraceEventInfoList *events = NULL;
    bool found = false;
    TraceEvent *ev;

    ev = NULL;
    while ((ev = trace_event_pattern(name, ev)) != NULL) {
        TraceEventInfoList *elem = g_new(TraceEventInfoList, 1);
        elem->value = g_new(TraceEventInfo, 1);
        elem->value->name = g_strdup(trace_event_get_name(ev));
        if (!trace_event_get_state_static(ev)) {
            elem->value->state = TRACE_EVENT_STATE_UNAVAILABLE;
        } else if (!trace_event_get_state_dynamic(ev)) {
            elem->value->state = TRACE_EVENT_STATE_DISABLED;
        } else {
            elem->value->state = TRACE_EVENT_STATE_ENABLED;
        }
        elem->next = events;
        events = elem;
        found = true;
    }

    if (!found && !trace_event_is_pattern(name)) {
        error_setg(errp, "unknown event \"%s\"", name);
    }

    return events;
}

void qmp_trace_event_set_state(const char *name, bool enable,
                               bool has_ignore_unavailable,
                               bool ignore_unavailable, Error **errp)
{
    bool found = false;
    TraceEvent *ev;

    /* Check all selected events are dynamic */
    ev = NULL;
    while ((ev = trace_event_pattern(name, ev)) != NULL) {
        found = true;
        if (!(has_ignore_unavailable && ignore_unavailable) &&
            !trace_event_get_state_static(ev)) {
            error_setg(errp, "cannot set dynamic tracing state for \"%s\"",
                       trace_event_get_name(ev));
            return;
        }
    }
    if (!found && !trace_event_is_pattern(name)) {
        error_setg(errp, "unknown event \"%s\"", name);
        return;
    }

    /* Apply changes */
    ev = NULL;
    while ((ev = trace_event_pattern(name, ev)) != NULL) {
        if (trace_event_get_state_static(ev)) {
            trace_event_set_state_dynamic(ev, enable);
        }
    }
}
