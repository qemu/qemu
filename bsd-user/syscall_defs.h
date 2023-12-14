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

#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include <sys/syscall.h>
#include <sys/resource.h>

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
typedef int64_t target_time_t;
#else
typedef int32_t target_time_t;
#endif

struct target_iovec {
    abi_long iov_base;   /* Starting address */
    abi_long iov_len;   /* Number of bytes */
};

/*
 * sys/ipc.h
 */
struct target_ipc_perm {
    uint32_t    cuid;       /* creator user id */
    uint32_t    cgid;       /* creator group id */
    uint32_t    uid;        /* user id */
    uint32_t    gid;        /* group id */
    uint16_t    mode;       /* r/w permission */
    uint16_t    seq;        /* sequence # */
    abi_long    key;        /* user specified msg/sem/shm key */
};

#define TARGET_IPC_RMID 0   /* remove identifier */
#define TARGET_IPC_SET  1   /* set options */
#define TARGET_IPC_STAT 2   /* get options */

/*
 * sys/shm.h
 */
struct target_shmid_ds {
    struct  target_ipc_perm shm_perm; /* peration permission structure */
    abi_ulong   shm_segsz;  /* size of segment in bytes */
    int32_t     shm_lpid;   /* process ID of last shared memory op */
    int32_t     shm_cpid;   /* process ID of creator */
    int32_t     shm_nattch; /* number of current attaches */
    target_time_t shm_atime;  /* time of last shmat() */
    target_time_t shm_dtime;  /* time of last shmdt() */
    target_time_t shm_ctime;  /* time of last change by shmctl() */
};

#define N_BSD_SHM_REGIONS   32
struct bsd_shm_regions {
    abi_long start;
    abi_long size;
};

/*
 *  sys/mman.h
 */
#define TARGET_MADV_DONTNEED            4       /* dont need these pages */

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
    target_time_t   tv_sec;     /* seconds */
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
    target_time_t       tv_sec; /* seconds */
    target_freebsd_suseconds_t  tv_usec;/* and microseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

/*
 *  sys/resource.h
 */
#define TARGET_RLIM_INFINITY    RLIM_INFINITY

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

/*
 * sys/stat.h
 */
struct target_freebsd11_stat {
    uint32_t  st_dev;       /* inode's device */
    uint32_t  st_ino;       /* inode's number */
    int16_t   st_mode;      /* inode protection mode */
    int16_t   st_nlink;     /* number of hard links */
    uint32_t  st_uid;       /* user ID of the file's owner */
    uint32_t  st_gid;       /* group ID of the file's group */
    uint32_t  st_rdev;      /* device type */
    struct  target_freebsd_timespec st_atim; /* time last accessed */
    struct  target_freebsd_timespec st_mtim; /* time last data modification */
    struct  target_freebsd_timespec st_ctim; /* time last file status change */
    int64_t    st_size;     /* file size, in bytes */
    int64_t    st_blocks;   /* blocks allocated for file */
    uint32_t   st_blksize;  /* optimal blocksize for I/O */
    uint32_t   st_flags;    /* user defined flags for file */
    uint32_t   st_gen;      /* file generation number */
    int32_t    st_lspare;
    struct target_freebsd_timespec st_birthtim; /* time of file creation */
    /*
     * Explicitly pad st_birthtim to 16 bytes so that the size of
     * struct stat is backwards compatible.  We use bitfields instead
     * of an array of chars so that this doesn't require a C99 compiler
     * to compile if the size of the padding is 0.  We use 2 bitfields
     * to cover up to 64 bits on 32-bit machines.  We assume that
     * CHAR_BIT is 8...
     */
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
} __packed;

#if defined(__i386__)
#define TARGET_HAS_STAT_TIME_T_EXT       1
#endif

struct target_stat {
    uint64_t  st_dev;               /* inode's device */
    uint64_t  st_ino;               /* inode's number */
    uint64_t  st_nlink;             /* number of hard links */
    int16_t   st_mode;              /* inode protection mode */
    int16_t   st_padding0;
    uint32_t  st_uid;               /* user ID of the file's owner */
    uint32_t  st_gid;               /* group ID of the file's group */
    int32_t   st_padding1;
    uint64_t  st_rdev;              /* device type */
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t   st_atim_ext;
#endif
    struct  target_freebsd_timespec st_atim; /* time of last access */
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t   st_mtim_ext;
#endif
    struct  target_freebsd_timespec st_mtim; /* time of last data modification */
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t st_ctim_ext;
#endif
    struct  target_freebsd_timespec st_ctim;/* time of last file status change */
#ifdef TARGET_HAS_STAT_TIME_T_EXT
    int32_t st_btim_ext;
#endif
    struct  target_freebsd_timespec st_birthtim;   /* time of file creation */
    int64_t   st_size;              /* file size, in bytes */
    int64_t   st_blocks;            /* blocks allocated for file */
    uint32_t  st_blksize;           /* optimal blocksize for I/O */
    uint32_t  st_flags;             /* user defined flags for file */
    uint64_t  st_gen;               /* file generation number */
    uint64_t  st_spare[10];
};


/* struct nstat is the same as stat above but without the st_lspare field */
struct target_freebsd11_nstat {
    uint32_t  st_dev;       /* inode's device */
    uint32_t  st_ino;       /* inode's number */
    int16_t   st_mode;      /* inode protection mode */
    int16_t   st_nlink;     /* number of hard links */
    uint32_t  st_uid;       /* user ID of the file's owner */
    uint32_t  st_gid;       /* group ID of the file's group */
    uint32_t  st_rdev;      /* device type */
    struct  target_freebsd_timespec st_atim; /* time last accessed */
    struct  target_freebsd_timespec st_mtim; /* time last data modification */
    struct  target_freebsd_timespec st_ctim; /* time last file status change */
    int64_t    st_size;     /* file size, in bytes */
    int64_t    st_blocks;   /* blocks allocated for file */
    uint32_t   st_blksize;  /* optimal blocksize for I/O */
    uint32_t   st_flags;    /* user defined flags for file */
    uint32_t   st_gen;      /* file generation number */
    struct target_freebsd_timespec st_birthtim; /* time of file creation */
    /*
     * Explicitly pad st_birthtim to 16 bytes so that the size of
     * struct stat is backwards compatible.  We use bitfields instead
     * of an array of chars so that this doesn't require a C99 compiler
     * to compile if the size of the padding is 0.  We use 2 bitfields
     * to cover up to 64 bits on 32-bit machines.  We assume that
     * CHAR_BIT is 8...
     */
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
    unsigned int:(8 / 2) * (16 - (int)sizeof(struct target_freebsd_timespec));
} __packed;

/*
 * sys/mount.h
 */

/* filesystem id type */
typedef struct target_freebsd_fsid { int32_t val[2]; } target_freebsd_fsid_t;

/* filesystem statistics */
struct target_freebsd11_statfs {
    uint32_t f_version; /* structure version number */
    uint32_t f_type;    /* type of filesystem */
    uint64_t f_flags;   /* copy of mount exported flags */
    uint64_t f_bsize;   /* filesystem fragment size */
    uint64_t f_iosize;  /* optimal transfer block size */
    uint64_t f_blocks;  /* total data blocks in filesystem */
    uint64_t f_bfree;   /* free blocks in filesystem */
    int64_t  f_bavail;  /* free blocks avail to non-superuser */
    uint64_t f_files;   /* total file nodes in filesystem */
    int64_t  f_ffree;   /* free nodes avail to non-superuser */
    uint64_t f_syncwrites;  /* count of sync writes since mount */
    uint64_t f_asyncwrites; /* count of async writes since mount */
    uint64_t f_syncreads;   /* count of sync reads since mount */
    uint64_t f_asyncreads;  /* count of async reads since mount */
    uint64_t f_spare[10];   /* unused spare */
    uint32_t f_namemax; /* maximum filename length */
    uint32_t f_owner;   /* user that mounted the filesystem */
    target_freebsd_fsid_t   f_fsid; /* filesystem id */
    char     f_charspare[80];           /* spare string space */
    char     f_fstypename[16];   /* filesys type name */
    char     f_mntfromname[88];    /* mount filesystem */
    char     f_mntonname[88];      /* dir on which mounted*/
};

struct target_statfs {
    uint32_t f_version;             /* structure version number */
    uint32_t f_type;                /* type of filesystem */
    uint64_t f_flags;               /* copy of mount exported flags */
    uint64_t f_bsize;               /* filesystem fragment size */
    uint64_t f_iosize;              /* optimal transfer block size */
    uint64_t f_blocks;              /* total data blocks in filesystem */
    uint64_t f_bfree;               /* free blocks in filesystem */
    int64_t  f_bavail;              /* free blocks avail to non-superuser */
    uint64_t f_files;               /* total file nodes in filesystem */
    int64_t  f_ffree;               /* free nodes avail to non-superuser */
    uint64_t f_syncwrites;          /* count of sync writes since mount */
    uint64_t f_asyncwrites;         /* count of async writes since mount */
    uint64_t f_syncreads;           /* count of sync reads since mount */
    uint64_t f_asyncreads;          /* count of async reads since mount */
    uint64_t f_spare[10];           /* unused spare */
    uint32_t f_namemax;             /* maximum filename length */
    uint32_t f_owner;               /* user that mounted the filesystem */
    target_freebsd_fsid_t f_fsid;   /* filesystem id */
    char      f_charspare[80];      /* spare string space */
    char      f_fstypename[16];     /* filesystem type name */
    char      f_mntfromname[1024];  /* mounted filesystem */
    char      f_mntonname[1024];    /* directory on which mounted */
};

/* File identifier. These are unique per filesystem on a single machine. */
#define TARGET_MAXFIDSZ     16

struct target_freebsd_fid {
    uint16_t    fid_len;            /* len of data in bytes */
    uint16_t    fid_data0;          /* force longword align */
    char        fid_data[TARGET_MAXFIDSZ];  /* data (variable len) */
};

/* Generic file handle */
struct target_freebsd_fhandle {
    target_freebsd_fsid_t   fh_fsid;    /* Filesystem id of mount point */
    struct target_freebsd_fid fh_fid;   /* Filesys specific id */
};
typedef struct target_freebsd_fhandle target_freebsd_fhandle_t;

/*
 * sys/fcntl.h
 */
#define TARGET_F_DUPFD              0
#define TARGET_F_GETFD              1
#define TARGET_F_SETFD              2
#define TARGET_F_GETFL              3
#define TARGET_F_SETFL              4
#define TARGET_F_GETOWN             5
#define TARGET_F_SETOWN             6
#define TARGET_F_OGETLK             7
#define TARGET_F_OSETLK             8
#define TARGET_F_OSETLKW            9
#define TARGET_F_DUP2FD             10
#define TARGET_F_GETLK              11
#define TARGET_F_SETLK              12
#define TARGET_F_SETLKW             13
#define TARGET_F_SETLK_REMOTE       14
#define TARGET_F_READAHEAD          15
#define TARGET_F_RDAHEAD            16
#define TARGET_F_DUPFD_CLOEXEC     17
#define TARGET_F_DUP2FD_CLOEXEC    18
/* FreeBSD-specific */
#define TARGET_F_ADD_SEALS          19
#define TARGET_F_GET_SEALS          20

struct target_freebsd_flock {
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int16_t l_type;
    int16_t l_whence;
    int32_t l_sysid;
} QEMU_PACKED;

/* sys/unistd.h */
/* user: vfork(2) semantics, clear signals */
#define TARGET_RFSPAWN (1U << 31)

/*
 * from sys/procctl.h
 */
#define TARGET_PROC_SPROTECT            1
#define TARGET_PROC_REAP_ACQUIRE        2
#define TARGET_PROC_REAP_RELEASE        3
#define TARGET_PROC_REAP_STATUS         4
#define TARGET_PROC_REAP_GETPIDS        5
#define TARGET_PROC_REAP_KILL           6

struct target_procctl_reaper_status {
    uint32_t rs_flags;
    uint32_t rs_children;
    uint32_t rs_descendants;
    uint32_t rs_reaper;
    uint32_t rs_pid;
    uint32_t rs_pad0[15];
};

struct target_procctl_reaper_pidinfo {
    uint32_t pi_pid;
    uint32_t pi_subtree;
    uint32_t pi_flags;
    uint32_t pi_pad0[15];
};

struct target_procctl_reaper_pids {
    uint32_t rp_count;
    uint32_t rp_pad0[15];
    abi_ulong rp_pids;
};

struct target_procctl_reaper_kill {
    int32_t  rk_sig;
    uint32_t rk_flags;
    uint32_t rk_subtree;
    uint32_t rk_killed;
    uint32_t rk_fpid;
    uint32_t rk_pad0[15];
};


#define safe_syscall0(type, name) \
type safe_##name(void) \
{ \
    return safe_syscall(SYS_##name); \
}

#define safe_syscall1(type, name, type1, arg1) \
type safe_##name(type1 arg1) \
{ \
    return safe_syscall(SYS_##name, arg1); \
}

#define safe_syscall2(type, name, type1, arg1, type2, arg2) \
type safe_##name(type1 arg1, type2 arg2) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2); \
}

#define safe_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3); \
}

#define safe_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4); \
}

#define safe_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5); \
}

#define safe_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, \
    type4, arg4, type5, arg5, type6, arg6) \
type safe_##name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, \
    type5 arg5, type6 arg6) \
{ \
    return safe_syscall(SYS_##name, arg1, arg2, arg3, arg4, arg5, arg6); \
}

#define safe_fcntl(...) safe_syscall(SYS_fcntl, __VA_ARGS__)

/* So far all target and host bitmasks are the same */
#undef  target_to_host_bitmask
#define target_to_host_bitmask(x, tbl) (x)
#undef  host_to_target_bitmask
#define host_to_target_bitmask(x, tbl) (x)

#endif /* SYSCALL_DEFS_H */
