/*
 * Ftrace trace backend
 *
 * Copyright (C) 2013 Hitachi, Ltd.
 * Created by Eiichi Tsukata <eiichi.tsukata.xh@hitachi.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include "trace.h"
#include "trace/control.h"

int trace_marker_fd;

static int find_debugfs(char *debugfs)
{
    char type[100];
    FILE *fp;

    fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        return 0;
    }

    while (fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
                  debugfs, type) == 2) {
        if (strcmp(type, "debugfs") == 0) {
            break;
        }
    }
    fclose(fp);

    if (strcmp(type, "debugfs") != 0) {
        return 0;
    }
    return 1;
}

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
    char debugfs[PATH_MAX];
    char path[PATH_MAX];
    int debugfs_found;
    int trace_fd = -1;

    if (file) {
        fprintf(stderr, "error: -trace file=...: "
                "option not supported by the selected tracing backend\n");
        return false;
    }

    debugfs_found = find_debugfs(debugfs);
    if (debugfs_found) {
        snprintf(path, PATH_MAX, "%s/tracing/tracing_on", debugfs);
        trace_fd = open(path, O_WRONLY);
        if (trace_fd < 0) {
            perror("Could not open ftrace 'tracing_on' file");
            return false;
        } else {
            if (write(trace_fd, "1", 1) < 0) {
                perror("Could not write to 'tracing_on' file");
                close(trace_fd);
                return false;
            }
            close(trace_fd);
        }
        snprintf(path, PATH_MAX, "%s/tracing/trace_marker", debugfs);
        trace_marker_fd = open(path, O_WRONLY);
        if (trace_marker_fd < 0) {
            perror("Could not open ftrace 'trace_marker' file");
            return false;
        }
    } else {
        fprintf(stderr, "debugfs is not mounted\n");
        return false;
    }

    trace_backend_init_events(events);
    return true;
}
