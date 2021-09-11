/*
 *  System call related declarations
 *
 *  Copyright (c) 2013-15 Stacey D. Son (sson at FreeBSD)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SYSCALL_DEFS_H_
#define _SYSCALL_DEFS_H_

#include <sys/syscall.h>

#include "errno_defs.h"

#include "freebsd/syscall_nr.h"
#include "netbsd/syscall_nr.h"
#include "openbsd/syscall_nr.h"

/*
 * machine/_types.h
 * or x86/_types.h
 */

/*
 * time_t seems to be very inconsistly defined for the different *BSD's...
 *
 * FreeBSD uses a 64bits time_t except on i386
 * so we have to add a special case here.
 *
 * On NetBSD time_t is always defined as an int64_t.  On OpenBSD time_t
 * is always defined as an int.
 *
 */
#if (!defined(TARGET_I386))
typedef int64_t target_freebsd_time_t;
#else
typedef int32_t target_freebsd_time_t;
#endif

struct target_iovec {
    abi_long iov_base;   /* Starting address */
    abi_long iov_len;   /* Number of bytes */
};

/*
 *  sys/mman.h
 */
#define TARGET_FREEBSD_MAP_RESERVED0080 0x0080  /* previously misimplemented */
                                                /* MAP_INHERIT */
#define TARGET_FREEBSD_MAP_RESERVED0100 0x0100  /* previously unimplemented */
                                                /* MAP_NOEXTEND */
#define TARGET_FREEBSD_MAP_STACK        0x0400  /* region grows down, like a */
                                                /* stack */
#define TARGET_FREEBSD_MAP_NOSYNC       0x0800  /* page to but do not sync */
                                                /* underlying file */

#define TARGET_FREEBSD_MAP_FLAGMASK     0x1ff7

#define TARGET_NETBSD_MAP_INHERIT       0x0080  /* region is retained after */
                                                /* exec */
#define TARGET_NETBSD_MAP_TRYFIXED      0x0400  /* attempt hint address, even */
                                                /* within break */
#define TARGET_NETBSD_MAP_WIRED         0x0800  /* mlock() mapping when it is */
                                                /* established */

#define TARGET_NETBSD_MAP_STACK         0x2000  /* allocated from memory, */
                                                /* swap space (stack) */

#define TARGET_NETBSD_MAP_FLAGMASK      0x3ff7

#define TARGET_OPENBSD_MAP_INHERIT      0x0080  /* region is retained after */
                                                /* exec */
#define TARGET_OPENBSD_MAP_NOEXTEND     0x0100  /* for MAP_FILE, don't change */
                                                /* file size */
#define TARGET_OPENBSD_MAP_TRYFIXED     0x0400  /* attempt hint address, */
                                                /* even within heap */

#define TARGET_OPENBSD_MAP_FLAGMASK     0x17f7

/* XXX */
#define TARGET_BSD_MAP_FLAGMASK         0x3ff7

/*
 * sys/time.h
 * sys/timex.h
 */

typedef abi_long target_freebsd_suseconds_t;

/* compare to sys/timespec.h */
struct target_freebsd_timespec {
    target_freebsd_time_t   tv_sec;     /* seconds */
    abi_long                tv_nsec;    /* and nanoseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

#define TARGET_CPUCLOCK_WHICH_PID   0
#define TARGET_CPUCLOCK_WHICH_TID   1

/* sys/umtx.h */
struct target_freebsd__umtx_time {
    struct target_freebsd_timespec  _timeout;
    uint32_t    _flags;
    uint32_t    _clockid;
};

struct target_freebsd_timeval {
    target_freebsd_time_t       tv_sec; /* seconds */
    target_freebsd_suseconds_t  tv_usec;/* and microseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

/*
 *  sys/resource.h
 */
#if defined(__FreeBSD__)
#define TARGET_RLIM_INFINITY    RLIM_INFINITY
#else
#define TARGET_RLIM_INFINITY    ((abi_ulong)-1)
#endif

#define TARGET_RLIMIT_CPU       0
#define TARGET_RLIMIT_FSIZE     1
#define TARGET_RLIMIT_DATA      2
#define TARGET_RLIMIT_STACK     3
#define TARGET_RLIMIT_CORE      4
#define TARGET_RLIMIT_RSS       5
#define TARGET_RLIMIT_MEMLOCK   6
#define TARGET_RLIMIT_NPROC     7
#define TARGET_RLIMIT_NOFILE    8
#define TARGET_RLIMIT_SBSIZE    9
#define TARGET_RLIMIT_AS        10
#define TARGET_RLIMIT_NPTS      11
#define TARGET_RLIMIT_SWAP      12

struct target_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

struct target_freebsd_rusage {
    struct target_freebsd_timeval ru_utime; /* user time used */
    struct target_freebsd_timeval ru_stime; /* system time used */
    abi_long    ru_maxrss;      /* maximum resident set size */
    abi_long    ru_ixrss;       /* integral shared memory size */
    abi_long    ru_idrss;       /* integral unshared data size */
    abi_long    ru_isrss;       /* integral unshared stack size */
    abi_long    ru_minflt;      /* page reclaims */
    abi_long    ru_majflt;      /* page faults */
    abi_long    ru_nswap;       /* swaps */
    abi_long    ru_inblock;     /* block input operations */
    abi_long    ru_oublock;     /* block output operations */
    abi_long    ru_msgsnd;      /* messages sent */
    abi_long    ru_msgrcv;      /* messages received */
    abi_long    ru_nsignals;    /* signals received */
    abi_long    ru_nvcsw;       /* voluntary context switches */
    abi_long    ru_nivcsw;      /* involuntary context switches */
};

struct target_freebsd__wrusage {
    struct target_freebsd_rusage wru_self;
    struct target_freebsd_rusage wru_children;
};

#endif /* ! _SYSCALL_DEFS_H_ */
