/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2014-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"

void trace_event_set_state_dynamic(TraceEvent *ev, bool state)
{
    assert(trace_event_get_state_static(ev));

    bool state_pre = *ev->dstate;
    if (state_pre != state) {
        if (state) {
            trace_events_enabled_count++;
            *ev->dstate = 1;
        } else {
            trace_events_enabled_count--;
            *ev->dstate = 0;
        }
    }
}
