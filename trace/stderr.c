#include "trace.h"
#include "trace/control.h"


void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    TraceEventID i;

    for (i = 0; i < trace_event_count(); i++) {
        TraceEvent *ev = trace_event_id(i);
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_event_get_name(ev), i, trace_event_get_state_dynamic(ev));
    }
}

void trace_event_set_state_dynamic_backend(TraceEvent *ev, bool state)
{
    ev->dstate = state;
}

bool trace_backend_init(const char *events, const char *file)
{
    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backend\n");
        return false;
    }
    trace_backend_init_events(events);
    return true;
}
