#ifndef TRACE_STDERR_H
#define TRACE_STDERR_H

typedef uint64_t TraceEventID;

typedef struct {
    const char *tp_name;
    bool state;
} TraceEvent;

#endif /* ! TRACE_STDERR_H */
