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

typedef uint64_t TraceEventID;

void trace0(TraceEventID event);
void trace1(TraceEventID event, uint64_t x1);
void trace2(TraceEventID event, uint64_t x1, uint64_t x2);
void trace3(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3);
void trace4(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4);
void trace5(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5);
void trace6(TraceEventID event, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6);

#endif /* SIMPLETRACE_H */
