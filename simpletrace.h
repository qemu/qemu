/*
 * Simple trace backend
 *
 * Copyright IBM, Corp. 2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef SIMPLETRACE_H
#define SIMPLETRACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef uint64_t TraceEventID;

typedef struct {
    const char *tp_name;
    bool state;
} TraceEvent;

void trace0(TraceEventID event);
void trace1(TraceEventID event, uint64_t x1);
void trace2(TraceEventID event, uint64_t x1, uint64_t x2);
void trace3(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3);
void trace4(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4);
void trace5(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5);
void trace6(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6);
void st_print_trace(FILE *stream, fprintf_function stream_printf);
void st_print_trace_events(FILE *stream, fprintf_function stream_printf);
bool st_change_trace_event_state(const char *tname, bool tstate);
void st_print_trace_file_status(FILE *stream, fprintf_function stream_printf);
void st_set_trace_file_enabled(bool enable);
bool st_set_trace_file(const char *file);
void st_flush_trace_buffer(void);

#endif /* SIMPLETRACE_H */
