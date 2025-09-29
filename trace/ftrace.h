#ifndef TRACE_FTRACE_H
#define TRACE_FTRACE_H

#define MAX_TRACE_STRLEN 512
#define _STR(x) #x
#define STR(x) _STR(x)

extern int trace_marker_fd;

bool ftrace_init(void);
G_GNUC_PRINTF(1, 2) void ftrace_write(const char *fmt, ...);

#endif /* TRACE_FTRACE_H */
