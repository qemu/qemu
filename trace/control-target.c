/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2014-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "trace/control.h"
#include "translate-all.h"


void trace_event_set_state_dynamic_init(TraceEvent *ev, bool state)
{
    TraceEventID id = trace_event_get_id(ev);
    bool state_pre;
    assert(trace_event_get_state_static(ev));
    /*
     * We ignore the "vcpu" property here, since no vCPUs have been created
     * yet. Then dstate can only be 1 or 0.
     */
    state_pre = trace_events_dstate[id];
    if (state_pre != state) {
        if (state) {
            trace_events_enabled_count++;
            trace_events_dstate[id] = 1;
        } else {
            trace_events_enabled_count--;
            trace_events_dstate[id] = 0;
        }
    }
}

void trace_event_set_state_dynamic(TraceEvent *ev, bool state)
{
    CPUState *vcpu;
    assert(trace_event_get_state_static(ev));
    if (trace_event_is_vcpu(ev)) {
        CPU_FOREACH(vcpu) {
            trace_event_set_vcpu_state_dynamic(vcpu, ev, state);
        }
    } else {
        /* Without the "vcpu" property, dstate can only be 1 or 0 */
        TraceEventID id = trace_event_get_id(ev);
        bool state_pre = trace_events_dstate[id];
        if (state_pre != state) {
            if (state) {
                trace_events_enabled_count++;
                trace_events_dstate[id] = 1;
            } else {
                trace_events_enabled_count--;
                trace_events_dstate[id] = 0;
            }
        }
    }
}

void trace_event_set_vcpu_state_dynamic(CPUState *vcpu,
                                        TraceEvent *ev, bool state)
{
    TraceEventID id;
    TraceEventVCPUID vcpu_id;
    bool state_pre;
    assert(trace_event_get_state_static(ev));
    assert(trace_event_is_vcpu(ev));
    id = trace_event_get_id(ev);
    vcpu_id = trace_event_get_vcpu_id(ev);
    state_pre = test_bit(vcpu_id, vcpu->trace_dstate);
    if (state_pre != state) {
        if (state) {
            trace_events_enabled_count++;
            set_bit(vcpu_id, vcpu->trace_dstate);
            trace_events_dstate[id]++;
        } else {
            trace_events_enabled_count--;
            clear_bit(vcpu_id, vcpu->trace_dstate);
            trace_events_dstate[id]--;
        }
    }
}
