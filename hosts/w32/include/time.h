/*
 * QEMU w32 support
 *
 * Copyright (C) 2011 Stefan Weil
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#if !defined(W32_TIME_H)
#define W32_TIME_H

#include_next <time.h>

#if !defined(WIN64)

#if !defined(HAVE_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DEFINED)
#define HAVE_STRUCT_TIMESPEC 1  /* MinGW */
#define _TIMESPEC_DEFINED       /* MinGW-64 */
struct timespec {
        long tv_sec;
        long tv_nsec;
};
#endif /* !HAVE_STRUCT_TIMESPEC && !_TIMESPEC_DEFINED */

typedef enum {
  CLOCK_REALTIME = 0
} clockid_t;

int clock_getres (clockid_t clock_id, struct timespec *res);
int clock_gettime(clockid_t clock_id, struct timespec *pTimespec);

#endif /* W64 */

#endif /* W32_TIME_H */
