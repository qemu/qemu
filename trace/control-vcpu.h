/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__CONTROL_VCPU_H
#define TRACE__CONTROL_VCPU_H

#include "control.h"
#include "event-internal.h"
#include "hw/core/cpu.h"

/**
 * trace_event_get_vcpu_state:
 * @vcpu: Target vCPU.
 * @id: Event identifier name.
 *
 * Get the tracing state of an event (both static and dynamic) for the given
 * vCPU.
 *
 * If the event has the disabled property, the check will have no performance
 * impact.
 */
#define trace_event_get_vcpu_state(vcpu, id)                            \
    ((id ##_ENABLED) &&                                                 \
     trace_event_get_vcpu_state_dynamic_by_vcpu_id(                     \
         vcpu, _ ## id ## _EVENT.vcpu_id))

/**
 * trace_event_get_vcpu_state_dynamic:
 *
 * Get the dynamic tracing state of an event for the given vCPU.
 */
static bool trace_event_get_vcpu_state_dynamic(CPUState *vcpu, TraceEvent *ev);

#include "control-internal.h"

static inline bool
trace_event_get_vcpu_state_dynamic_by_vcpu_id(CPUState *vcpu,
                                              uint32_t vcpu_id)
{
    /* it's on fast path, avoid consistency checks (asserts) */
    if (unlikely(trace_events_enabled_count)) {
        return test_bit(vcpu_id, vcpu->trace_dstate);
    } else {
        return false;
    }
}

static inline bool trace_event_get_vcpu_state_dynamic(CPUState *vcpu,
                                                      TraceEvent *ev)
{
    uint32_t vcpu_id;
    assert(trace_event_is_vcpu(ev));
    vcpu_id = trace_event_get_vcpu_id(ev);
    return trace_event_get_vcpu_state_dynamic_by_vcpu_id(vcpu, vcpu_id);
}

#endif
