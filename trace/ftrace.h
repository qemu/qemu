#ifndef TRACE_FTRACE_H
#define TRACE_FTRACE_H

#include <stdbool.h>


#define MAX_TRACE_STRLEN 512
#define _STR(x) #x
#define STR(x) _STR(x)

extern int trace_marker_fd;

bool ftrace_init(void);

#endif /* ! TRACE_FTRACE_H */
