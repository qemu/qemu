/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2014 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__CONTROL_INTERNAL_H
#define TRACE__CONTROL_INTERNAL_H



extern TraceEvent trace_events[];
extern bool trace_events_dstate[];
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

static inline bool trace_event_get_state_dynamic_by_id(int id)
{
    return unlikely(trace_events_enabled_count) && trace_events_dstate[id];
}

static inline bool trace_event_get_state_dynamic(TraceEvent *ev)
{
    int id = trace_event_get_id(ev);
    return trace_event_get_state_dynamic_by_id(id);
}

static inline void trace_event_set_state_dynamic(TraceEvent *ev, bool state)
{
    int id = trace_event_get_id(ev);
    assert(ev != NULL);
    assert(trace_event_get_state_static(ev));
    trace_events_enabled_count += state - trace_events_dstate[id];
    trace_events_dstate[id] = state;
}

#endif  /* TRACE__CONTROL_INTERNAL_H */
