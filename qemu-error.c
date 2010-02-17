#include <stdio.h>
#include "monitor.h"
#include "sysemu.h"

typedef struct QemuErrorSink QemuErrorSink;
struct QemuErrorSink {
    enum {
        ERR_SINK_FILE,
        ERR_SINK_MONITOR,
    } dest;
    union {
        FILE    *fp;
        Monitor *mon;
    };
    QemuErrorSink *previous;
};

static QemuErrorSink *qemu_error_sink;

void qemu_errors_to_file(FILE *fp)
{
    QemuErrorSink *sink;

    sink = qemu_mallocz(sizeof(*sink));
    sink->dest = ERR_SINK_FILE;
    sink->fp = fp;
    sink->previous = qemu_error_sink;
    qemu_error_sink = sink;
}

void qemu_errors_to_mon(Monitor *mon)
{
    QemuErrorSink *sink;

    sink = qemu_mallocz(sizeof(*sink));
    sink->dest = ERR_SINK_MONITOR;
    sink->mon = mon;
    sink->previous = qemu_error_sink;
    qemu_error_sink = sink;
}

void qemu_errors_to_previous(void)
{
    QemuErrorSink *sink;

    assert(qemu_error_sink != NULL);
    sink = qemu_error_sink;
    qemu_error_sink = sink->previous;
    qemu_free(sink);
}

void qemu_error(const char *fmt, ...)
{
    va_list args;

    assert(qemu_error_sink != NULL);
    switch (qemu_error_sink->dest) {
    case ERR_SINK_FILE:
        va_start(args, fmt);
        vfprintf(qemu_error_sink->fp, fmt, args);
        va_end(args);
        break;
    case ERR_SINK_MONITOR:
        va_start(args, fmt);
        monitor_vprintf(qemu_error_sink->mon, fmt, args);
        va_end(args);
        break;
    }
}

void qemu_error_internal(const char *file, int linenr, const char *func,
                         const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    assert(qemu_error_sink != NULL);

    va_start(va, fmt);
    qerror = qerror_from_info(file, linenr, func, fmt, &va);
    va_end(va);

    switch (qemu_error_sink->dest) {
    case ERR_SINK_FILE:
        qerror_print(qerror);
        QDECREF(qerror);
        break;
    case ERR_SINK_MONITOR:
        monitor_set_error(qemu_error_sink->mon, qerror);
        break;
    }
}
