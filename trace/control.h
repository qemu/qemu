/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2011 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef TRACE_CONTROL_H
#define TRACE_CONTROL_H

#include <stdbool.h>


/** Initialize the tracing backend.
 *
 * @file Name of trace output file; may be NULL.
 *       Corresponds to commandline option "-trace file=...".
 * @return Whether the backend could be successfully initialized.
 */
bool trace_backend_init(const char *file);

#endif  /* TRACE_CONTROL_H */
