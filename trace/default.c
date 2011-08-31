/*
 * Default implementation for backend initialization from commandline.
 *
 * Copyright (C) 2011 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "trace/control.h"


void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    fprintf(stderr, "warning: "
            "cannot print the trace events with the current backend\n");
    stream_printf(stream, "error: "
                  "operation not supported with the current backend\n");
}

bool trace_event_set_state(const char *name, bool state)
{
    fprintf(stderr, "warning: "
            "cannot set the state of a trace event with the current backend\n");
    return false;
}

bool trace_backend_init(const char *file)
{
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backend\n");
        return false;
    }
    return true;
}
