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
#include "control-vcpu.h"


static CPUState *get_cpu(bool has_vcpu, int vcpu, Error **errp)
{
    if (has_vcpu) {
        CPUState *cpu = qemu_get_cpu(vcpu);
        if (cpu == NULL) {
            error_setg(errp, "invalid vCPU index %u", vcpu);
        }
        return cpu;
    } else {
        return NULL;
    }
}

static bool check_events(bool has_vcpu, bool ignore_unavailable, bool is_pattern,
                         const char *name, Error **errp)
{
    if (!is_pattern) {
        TraceEvent *ev = trace_event_name(name);

        /* error for non-existing event */
        if (ev == NULL) {
            error_setg(errp, "unknown event \"%s\"", name);
            return false;
        }

        /* error for non-vcpu event */
        if (has_vcpu && !trace_event_is_vcpu(ev)) {
            error_setg(errp, "event \"%s\" is not vCPU-specific", name);
            return false;
        }

        /* error for unavailable event */
        if (!ignore_unavailable && !trace_event_get_state_static(ev)) {
            error_setg(errp, "event \"%s\" is disabled", name);
            return false;
        }

        return true;
    } else {
        /* error for unavailable events */
        TraceEventIter iter;
        TraceEvent *ev;
        trace_event_iter_init(&iter, name);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            if (!ignore_unavailable && !trace_event_get_state_static(ev)) {
                error_setg(errp, "event \"%s\" is disabled", trace_event_get_name(ev));
                return false;
            }
        }
        return true;
    }
}

TraceEventInfoList *qmp_trace_event_get_state(const char *name,
                                              bool has_vcpu, int64_t vcpu,
                                              Error **errp)
{
    Error *err = NULL;
    TraceEventInfoList *events = NULL;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(name);
    CPUState *cpu;

    /* Check provided vcpu */
    cpu = get_cpu(has_vcpu, vcpu, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    /* Check events */
    if (!check_events(has_vcpu, true, is_pattern, name, errp)) {
        return NULL;
    }

    /* Get states (all errors checked above) */
    trace_event_iter_init(&iter, name);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        TraceEventInfo *value;
        bool is_vcpu = trace_event_is_vcpu(ev);
        if (has_vcpu && !is_vcpu) {
            continue;
        }

        value = g_new(TraceEventInfo, 1);
        value->vcpu = is_vcpu;
        value->name = g_strdup(trace_event_get_name(ev));

        if (!trace_event_get_state_static(ev)) {
            value->state = TRACE_EVENT_STATE_UNAVAILABLE;
        } else {
            if (has_vcpu) {
                if (is_vcpu) {
                    if (trace_event_get_vcpu_state_dynamic(cpu, ev)) {
                        value->state = TRACE_EVENT_STATE_ENABLED;
                    } else {
                        value->state = TRACE_EVENT_STATE_DISABLED;
                    }
                }
                /* else: already skipped above */
            } else {
                if (trace_event_get_state_dynamic(ev)) {
                    value->state = TRACE_EVENT_STATE_ENABLED;
                } else {
                    value->state = TRACE_EVENT_STATE_DISABLED;
                }
            }
        }
        QAPI_LIST_PREPEND(events, value);
    }

    return events;
}

void qmp_trace_event_set_state(const char *name, bool enable,
                               bool has_ignore_unavailable, bool ignore_unavailable,
                               bool has_vcpu, int64_t vcpu,
                               Error **errp)
{
    Error *err = NULL;
    TraceEventIter iter;
    TraceEvent *ev;
    bool is_pattern = trace_event_is_pattern(name);
    CPUState *cpu;

    /* Check provided vcpu */
    cpu = get_cpu(has_vcpu, vcpu, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Check events */
    if (!check_events(has_vcpu, has_ignore_unavailable && ignore_unavailable,
                      is_pattern, name, errp)) {
        return;
    }

    /* Apply changes (all errors checked above) */
    trace_event_iter_init(&iter, name);
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        if (!trace_event_get_state_static(ev) ||
            (has_vcpu && !trace_event_is_vcpu(ev))) {
            continue;
        }
        if (has_vcpu) {
            trace_event_set_vcpu_state_dynamic(cpu, ev, enable);
        } else {
            trace_event_set_state_dynamic(ev, enable);
        }
    }
}
