/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2014-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"


void trace_event_set_state_dynamic(TraceEvent *ev, bool state)
{
    TraceEventID id;
    assert(trace_event_get_state_static(ev));
    id = trace_event_get_id(ev);
    trace_events_enabled_count += state - trace_events_dstate[id];
    trace_events_dstate[id] = state;
}

void trace_event_set_vcpu_state_dynamic(CPUState *vcpu,
                                        TraceEvent *ev, bool state)
{
    /* should never be called on non-target binaries */
    abort();
}
