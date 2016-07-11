/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__CONTROL_INTERNAL_H
#define TRACE__CONTROL_INTERNAL_H

#include <stddef.h>                     /* size_t */

#include "qom/cpu.h"


extern TraceEvent trace_events[];
extern uint16_t trace_events_dstate[];
extern int trace_events_enabled_count;


static inline TraceEventID trace_event_count(void)
{
    return TRACE_EVENT_COUNT;
}

static inline TraceEvent *trace_event_id(TraceEventID id)
{
    assert(id < trace_event_count());
    return &trace_events[id];
}

static inline bool trace_event_is_pattern(const char *str)
{
    assert(str != NULL);
    return strchr(str, '*') != NULL;
}

static inline TraceEventID trace_event_get_id(TraceEvent *ev)
{
    assert(ev != NULL);
    return ev->id;
}

static inline TraceEventVCPUID trace_event_get_vcpu_id(TraceEvent *ev)
{
    return ev->vcpu_id;
}

static inline bool trace_event_is_vcpu(TraceEvent *ev)
{
    return ev->vcpu_id != TRACE_VCPU_EVENT_COUNT;
}

static inline const char * trace_event_get_name(TraceEvent *ev)
{
    assert(ev != NULL);
    return ev->name;
}

static inline bool trace_event_get_state_static(TraceEvent *ev)
{
    assert(ev != NULL);
    return ev->sstate;
}

static inline bool trace_event_get_state_dynamic_by_id(TraceEventID id)
{
    /* it's on fast path, avoid consistency checks (asserts) */
    return unlikely(trace_events_enabled_count) && trace_events_dstate[id];
}

static inline bool trace_event_get_state_dynamic(TraceEvent *ev)
{
    TraceEventID id;
    assert(trace_event_get_state_static(ev));
    id = trace_event_get_id(ev);
    return trace_event_get_state_dynamic_by_id(id);
}

static inline bool trace_event_get_vcpu_state_dynamic_by_vcpu_id(CPUState *vcpu,
                                                                 TraceEventVCPUID id)
{
    /* it's on fast path, avoid consistency checks (asserts) */
    if (unlikely(trace_events_enabled_count)) {
        return test_bit(id, vcpu->trace_dstate);
    } else {
        return false;
    }
}

static inline bool trace_event_get_vcpu_state_dynamic(CPUState *vcpu,
                                                      TraceEvent *ev)
{
    TraceEventVCPUID id;
    assert(trace_event_is_vcpu(ev));
    id = trace_event_get_vcpu_id(ev);
    return trace_event_get_vcpu_state_dynamic_by_vcpu_id(vcpu, id);
}

#endif /* TRACE__CONTROL_INTERNAL_H */
