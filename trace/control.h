/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__CONTROL_H
#define TRACE__CONTROL_H

#include "qemu-common.h"
#include "trace/generated-events.h"


/**
 * TraceEventID:
 *
 * Unique tracing event identifier.
 *
 * These are named as 'TRACE_${EVENT_NAME}'.
 *
 * See also: "trace/generated-events.h"
 */
enum TraceEventID;

/**
 * trace_event_id:
 * @id: Event identifier.
 *
 * Get an event by its identifier.
 *
 * This routine has a constant cost, as opposed to trace_event_name and
 * trace_event_pattern.
 *
 * Pre-conditions: The identifier is valid.
 *
 * Returns: pointer to #TraceEvent.
 *
 */
static TraceEvent *trace_event_id(TraceEventID id);

/**
 * trace_event_name:
 * @id: Event name.
 *
 * Search an event by its name.
 *
 * Returns: pointer to #TraceEvent or NULL if not found.
 */
TraceEvent *trace_event_name(const char *name);

/**
 * trace_event_pattern:
 * @pat: Event name pattern.
 * @ev: Event to start searching from (not included).
 *
 * Get all events with a given name pattern.
 *
 * Returns: pointer to #TraceEvent or NULL if not found.
 */
TraceEvent *trace_event_pattern(const char *pat, TraceEvent *ev);

/**
 * trace_event_is_pattern:
 *
 * Whether the given string is an event name pattern.
 */
static bool trace_event_is_pattern(const char *str);

/**
 * trace_event_count:
 *
 * Return the number of events.
 */
static TraceEventID trace_event_count(void);



/**
 * trace_event_get_id:
 *
 * Get the identifier of an event.
 */
static TraceEventID trace_event_get_id(TraceEvent *ev);

/**
 * trace_event_get_vcpu_id:
 *
 * Get the per-vCPU identifier of an event.
 *
 * Special value #TRACE_VCPU_EVENT_COUNT means the event is not vCPU-specific
 * (does not have the "vcpu" property).
 */
static TraceEventVCPUID trace_event_get_vcpu_id(TraceEvent *ev);

/**
 * trace_event_is_vcpu:
 *
 * Whether this is a per-vCPU event.
 */
static bool trace_event_is_vcpu(TraceEvent *ev);

/**
 * trace_event_get_name:
 *
 * Get the name of an event.
 */
static const char * trace_event_get_name(TraceEvent *ev);

/**
 * trace_event_get_state:
 * @id: Event identifier.
 *
 * Get the tracing state of an event (both static and dynamic).
 *
 * If the event has the disabled property, the check will have no performance
 * impact.
 *
 * As a down side, you must always use an immediate #TraceEventID value.
 */
#define trace_event_get_state(id)                       \
    ((id ##_ENABLED) && trace_event_get_state_dynamic_by_id(id))

/**
 * trace_event_get_vcpu_state:
 * @vcpu: Target vCPU.
 * @id: Event identifier (TraceEventID).
 * @vcpu_id: Per-vCPU event identifier (TraceEventVCPUID).
 *
 * Get the tracing state of an event (both static and dynamic) for the given
 * vCPU.
 *
 * If the event has the disabled property, the check will have no performance
 * impact.
 *
 * As a down side, you must always use an immediate #TraceEventID value.
 */
#define trace_event_get_vcpu_state(vcpu, id, vcpu_id)                   \
    ((id ##_ENABLED) && trace_event_get_vcpu_state_dynamic_by_vcpu_id(vcpu, vcpu_id))

/**
 * trace_event_get_state_static:
 * @id: Event identifier.
 *
 * Get the static tracing state of an event.
 *
 * Use the define 'TRACE_${EVENT_NAME}_ENABLED' for compile-time checks (it will
 * be set to 1 or 0 according to the presence of the disabled property).
 */
static bool trace_event_get_state_static(TraceEvent *ev);

/**
 * trace_event_get_state_dynamic:
 *
 * Get the dynamic tracing state of an event.
 *
 * If the event has the 'vcpu' property, gets the OR'ed state of all vCPUs.
 */
static bool trace_event_get_state_dynamic(TraceEvent *ev);

/**
 * trace_event_get_vcpu_state_dynamic:
 *
 * Get the dynamic tracing state of an event for the given vCPU.
 */
static bool trace_event_get_vcpu_state_dynamic(CPUState *vcpu, TraceEvent *ev);

/**
 * trace_event_set_state:
 *
 * Set the tracing state of an event (only if possible).
 */
#define trace_event_set_state(id, state)                \
    do {                                                \
        if ((id ##_ENABLED)) {                          \
            TraceEvent *_e = trace_event_id(id);        \
            trace_event_set_state_dynamic(_e, state);   \
        }                                               \
    } while (0)

/**
 * trace_event_set_vcpu_state:
 *
 * Set the tracing state of an event for the given vCPU (only if not disabled).
 */
#define trace_event_set_vcpu_state(vcpu, id, state)                     \
    do {                                                                \
        if ((id ##_ENABLED)) {                                          \
            TraceEvent *_e = trace_event_id(id);                        \
            trace_event_set_vcpu_state_dynamic(vcpu, _e, state);        \
        }                                                               \
    } while (0)

/**
 * trace_event_set_state_dynamic:
 *
 * Set the dynamic tracing state of an event.
 *
 * If the event has the 'vcpu' property, sets the state on all vCPUs.
 *
 * Pre-condition: trace_event_get_state_static(ev) == true
 */
void trace_event_set_state_dynamic(TraceEvent *ev, bool state);

/**
 * trace_event_set_vcpu_state_dynamic:
 *
 * Set the dynamic tracing state of an event for the given vCPU.
 *
 * Pre-condition: trace_event_get_vcpu_state_static(ev) == true
 */
void trace_event_set_vcpu_state_dynamic(CPUState *vcpu,
                                        TraceEvent *ev, bool state);



/**
 * trace_init_backends:
 * @file:   Name of trace output file; may be NULL.
 *          Corresponds to commandline option "-trace file=...".
 *
 * Initialize the tracing backend.
 *
 * Returns: Whether the backends could be successfully initialized.
 */
bool trace_init_backends(void);

/**
 * trace_init_file:
 * @file:   Name of trace output file; may be NULL.
 *          Corresponds to commandline option "-trace file=...".
 *
 * Record the name of the output file for the tracing backend.
 * Exits if no selected backend does not support specifying the
 * output file, and a non-NULL file was passed.
 */
void trace_init_file(const char *file);

/**
 * trace_list_events:
 *
 * List all available events.
 */
void trace_list_events(void);

/**
 * trace_enable_events:
 * @line_buf: A string with a glob pattern of events to be enabled or,
 *            if the string starts with '-', disabled.
 *
 * Enable or disable matching events.
 */
void trace_enable_events(const char *line_buf);

/**
 * Definition of QEMU options describing trace subsystem configuration
 */
extern QemuOptsList qemu_trace_opts;

/**
 * trace_opt_parse:
 * @optarg: A string argument of --trace command line argument
 *
 * Initialize tracing subsystem.
 *
 * Returns the filename to save trace to.  It must be freed with g_free().
 */
char *trace_opt_parse(const char *optarg);

/**
 * trace_init_vcpu_events:
 *
 * Re-synchronize initial event state with vCPUs (which can be created after
 * trace_init_events()).
 */
void trace_init_vcpu_events(void);


#include "trace/control-internal.h"

#endif /* TRACE__CONTROL_H */
