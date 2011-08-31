/*
 * Default implementation for backend initialization from commandline.
 *
 * Copyright (C) 2011 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "trace/control.h"


bool trace_backend_init(const char *file)
{
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backend\n");
        return false;
    }
    return true;
}
