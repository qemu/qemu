#include "trace.h"
#include "trace/control.h"


void trace_print_events(FILE *stream, fprintf_function stream_printf)
{
    unsigned int i;

    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        stream_printf(stream, "%s [Event ID %u] : state %u\n",
                      trace_list[i].tp_name, i, trace_list[i].state);
    }
}

bool trace_event_set_state(const char *name, bool state)
{
    unsigned int i;

    for (i = 0; i < NR_TRACE_EVENTS; i++) {
        if (!strcmp(trace_list[i].tp_name, name)) {
            trace_list[i].state = state;
            return true;
        }
    }
    return false;
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
