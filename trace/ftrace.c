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

#include "qemu/osdep.h"
#include "trace/control.h"
#include "trace/ftrace.h"

int trace_marker_fd;

static int find_mount(char *mount_point, const char *fstype)
{
    char type[100];
    FILE *fp;
    int ret = 0;

    fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        return 0;
    }

    while (fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
                  mount_point, type) == 2) {
        if (strcmp(type, fstype) == 0) {
            ret = 1;
            break;
        }
    }
    fclose(fp);

    return ret;
}

void ftrace_write(const char *fmt, ...)
{
    char ftrace_buf[MAX_TRACE_STRLEN];
    int unused __attribute__ ((unused));
    int trlen;
    va_list ap;

    va_start(ap, fmt);
    trlen = vsnprintf(ftrace_buf, MAX_TRACE_STRLEN, fmt, ap);
    va_end(ap);

    trlen = MIN(trlen, MAX_TRACE_STRLEN - 1);
    unused = write(trace_marker_fd, ftrace_buf, trlen);
}

bool ftrace_init(void)
{
    char mount_point[PATH_MAX];
    char path[PATH_MAX];
    int tracefs_found;
    int trace_fd = -1;
    const char *subdir = "";

    tracefs_found = find_mount(mount_point, "tracefs");
    if (!tracefs_found) {
        tracefs_found = find_mount(mount_point, "debugfs");
        subdir = "/tracing";
    }

    if (tracefs_found) {
        if (snprintf(path, PATH_MAX, "%s%s/tracing_on", mount_point, subdir)
                >= sizeof(path)) {
            fprintf(stderr, "Using tracefs mountpoint would exceed PATH_MAX\n");
            return false;
        }
        trace_fd = open(path, O_WRONLY);
        if (trace_fd < 0) {
            if (errno == EACCES) {
                trace_marker_fd = open("/dev/null", O_WRONLY);
                if (trace_marker_fd != -1) {
                    return true;
                }
            }
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
        if (snprintf(path, PATH_MAX, "%s%s/trace_marker", mount_point, subdir)
                >= sizeof(path)) {
            fprintf(stderr, "Using tracefs mountpoint would exceed PATH_MAX\n");
            return false;
        }
        trace_marker_fd = open(path, O_WRONLY);
        if (trace_marker_fd < 0) {
            perror("Could not open ftrace 'trace_marker' file");
            return false;
        }
    } else {
        fprintf(stderr, "tracefs is not mounted\n");
        return false;
    }

    return true;
}
