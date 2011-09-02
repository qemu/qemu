/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "trace/control.h"


void trace_backend_init_events(const char *fname)
{
    if (fname == NULL) {
        return;
    }

    FILE *fp = fopen(fname, "r");
    if (!fp) {
        fprintf(stderr, "error: could not open trace events file '%s': %s\n",
                fname, strerror(errno));
        exit(1);
    }
    char line_buf[1024];
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        size_t len = strlen(line_buf);
        if (len > 1) {              /* skip empty lines */
            line_buf[len - 1] = '\0';
            if (!trace_event_set_state(line_buf, true)) {
                fprintf(stderr,
                        "error: trace event '%s' does not exist\n", line_buf);
                exit(1);
            }
        }
    }
    if (fclose(fp) != 0) {
        fprintf(stderr, "error: closing file '%s': %s\n",
                fname, strerror(errno));
        exit(1);
    }
}
