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

/**
 * TraceEvent:
 * @id: Unique event identifier.
 * @name: Event name.
 * @sstate: Static tracing state.
 * @dstate: Dynamic tracing state
 *
 * Opaque generic description of a tracing event.
 */
typedef struct TraceEvent {
#ifdef CONFIG_TRACE_SIMPLE
    uint32_t id;
#endif
    const char * name;
    const bool sstate;
    uint16_t *dstate;
} TraceEvent;

#endif /* TRACE__EVENT_INTERNAL_H */
