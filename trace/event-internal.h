/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2012-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__EVENT_INTERNAL_H
#define TRACE__EVENT_INTERNAL_H

/*
 * Special value for TraceEvent.vcpu_id field to indicate
 * that the event is not VCPU specific
 */
#define TRACE_VCPU_EVENT_NONE ((uint32_t)-1)

/**
 * TraceEvent:
 * @id: Unique event identifier.
 * @vcpu_id: Unique per-vCPU event identifier.
 * @name: Event name.
 * @sstate: Static tracing state.
 * @dstate: Dynamic tracing state
 *
 * Interpretation of @dstate depends on whether the event has the 'vcpu'
 *  property:
 * - false: Boolean value indicating whether the event is active.
 * - true : Integral counting the number of vCPUs that have this event enabled.
 *
 * Opaque generic description of a tracing event.
 */
typedef struct TraceEvent {
    uint32_t id;
    uint32_t vcpu_id;
    const char * name;
    const bool sstate;
    uint16_t *dstate;
} TraceEvent;

void trace_event_set_state_dynamic_init(TraceEvent *ev, bool state);

#endif /* TRACE__EVENT_INTERNAL_H */
