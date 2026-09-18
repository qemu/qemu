/*
 * System call related declarations
 *
 * Copyright (c) 2013-2015 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include <sys/syscall.h>
#include <sys/resource.h>

#include "errno_defs.h"

#include "os-syscall.h"

/*
 * machine/_types.h
 * or x86/_types.h
 */

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
 * sys/sem.h
 */
#define TARGET_GETNCNT  3   /* Return the value of semncnt {READ} */
#define TARGET_GETPID   4   /* Return the value of sempid {READ} */
#define TARGET_GETVAL   5   /* Return the value of semval {READ} */
#define TARGET_GETALL   6   /* Return semvals into arg.array {READ} */
#define TARGET_GETZCNT  7   /* Return the value of semzcnt {READ} */
#define TARGET_SETVAL   8   /* Set the value of semval to arg.val {ALTER} */
#define TARGET_SETALL   9   /* Set semvals from arg.array {ALTER} */

struct target_sembuf {
    abi_ushort      sem_num;        /* semaphore # */
    abi_short       sem_op;         /* semaphore operation */
    abi_short       sem_flg;        /* operation flags */
};

union target_semun {
    abi_int     val;        /* value for SETVAL */
    abi_ulong   buf;        /* buffer for IPC_STAT & IPC_SET */
    abi_ulong   array;      /* array for GETALL & SETALL */
};

struct target_semid_ds {
    struct target_ipc_perm sem_perm; /* operation permission struct */
    abi_ptr     sem_base;   /* pointer to first semaphore in set */
    abi_ushort  sem_nsems;  /* number of sems in set */
    target_time_t   sem_otime;  /* last operation time */
    target_time_t   sem_ctime;  /* times measured in secs */
};

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
 * sys/msg.h
 */
struct target_msqid_ds {
    struct  target_ipc_perm msg_perm; /* msg queue permission bits */
    abi_ptr     msg_first;  /* first message in the queue */
    abi_ptr     msg_last;   /* last message in the queue */
    abi_ulong   msg_cbytes; /* # of bytes in use on the queue */
    abi_ulong   msg_qnum;   /* number of msgs in the queue */
    abi_ulong   msg_qbytes; /* max # of bytes on the queue */
    int32_t     msg_lspid;  /* pid of last msgsnd() */
    int32_t     msg_lrpid;  /* pid of last msgrcv() */
    target_time_t   msg_stime;  /* time of last msgsnd() */
    target_time_t   msg_rtime;  /* time of last msgrcv() */
    target_time_t   msg_ctime;  /* time of last msgctl() */
};

/*
 * sys/msgbuf.h
 */
struct target_msgbuf {
    abi_long    mtype;      /* message type */
    char        mtext[1];   /* body of message */
};

/*
 *  sys/mman.h
 */
#define TARGET_BSD_MAP_FLAGMASK         0x3ff7

/*
 * sys/time.h
 * sys/timex.h
 */

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
    target_suseconds_t  tv_usec;/* and microseconds */
#if !defined(TARGET_I386) && TARGET_ABI_BITS == 32
    abi_long _pad;
#endif
};

/* compare to sys/timex.h */
struct target_freebsd_ntptimeval {
    struct target_freebsd_timespec  time;
    abi_long    maxerror;
    abi_long    esterror;
    abi_long    tai;
    int32_t     time_state;
};

struct target_freebsd_itimerspec {
    struct target_freebsd_timespec it_interval;
    struct target_freebsd_timespec it_value;
};

struct target_freebsd_timex {
    uint32_t    modes;
    abi_long    offset;
    abi_long    freq;
    abi_long    maxerror;
    abi_long    esterror;
    int32_t     status;
    abi_long    constant;
    abi_long    precision;
    abi_long    tolerance;

    abi_long    ppsfreq;
    abi_long    jitter;
    int32_t     shift;
    abi_long    stabil;
    abi_long    jitcnt;
    abi_long    calcnt;
    abi_long    errcnt;
    abi_long    stbcnt;
};

/* Maxiumum of 32 active POSIX timers allowed at any one time. */
extern int g_posix_timers[32];

#define TIMER_MAGIC 0x0caf0000
#define TIMER_MAGIC_MASK 0xffff0000

/*
 * sys/event.h
 */
struct target_freebsd11_kevent {
    abi_ulong  ident;
    int16_t    filter;
    uint16_t   flags;
    uint32_t   fflags;
    abi_long   data;
    abi_ulong  udata;
};

struct target_freebsd_kevent {
    abi_ulong  ident;
    int16_t    filter;
    uint16_t   flags;
    uint32_t   fflags;
    int64_t data;
    abi_ulong  udata;
    uint64_t  ext[4];
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
 * sys/socket.h
 */

/*
 * Types
 */
#define TARGET_SOCK_STREAM      1   /* stream socket */
#define TARGET_SOCK_DGRAM       2   /* datagram socket */
#define TARGET_SOCK_RAW         3   /* raw-protocol interface */
#define TARGET_SOCK_RDM         4   /* reliably-delivered message */
#define TARGET_SOCK_SEQPACKET   5   /* sequenced packet stream */


/*
 * Option flags per-socket.
 */

#define TARGET_SO_DEBUG         0x0001  /* turn on debugging info recording */
#define TARGET_SO_ACCEPTCONN    0x0002  /* socket has had listen() */
#define TARGET_SO_REUSEADDR     0x0004  /* allow local address reuse */
#define TARGET_SO_KEEPALIVE     0x0008  /* keep connections alive */
#define TARGET_SO_DONTROUTE     0x0010  /* just use interface addresses */
#define TARGET_SO_BROADCAST     0x0020  /* permit sending of broadcast msgs */
#define TARGET_SO_USELOOPBACK   0x0040  /* bypass hardware when possible */
#define TARGET_SO_LINGER        0x0080  /* linger on close if data present */
#define TARGET_SO_OOBINLINE     0x0100  /* leave received OOB data in line */
#define TARGET_SO_REUSEPORT     0x0200  /* allow local address & port reuse */
#define TARGET_SO_TIMESTAMP     0x0400  /* timestamp received dgram traffic */
#define TARGET_SO_NOSIGPIPE     0x0800  /* no SIGPIPE from EPIPE */
#define TARGET_SO_ACCEPTFILTER  0x1000  /* there is an accept filter */
#define TARGET_SO_BINTIME       0x2000  /* timestamp received dgram traffic */
#define TARGET_SO_NO_OFFLOAD    0x4000  /* socket cannot be offloaded */
#define TARGET_SO_NO_DDP        0x8000  /* disable direct data placement */

/*
 * Additional options, not kept in so_options.
 */
#define TARGET_SO_SNDBUF        0x1001  /* send buffer size */
#define TARGET_SO_RCVBUF        0x1002  /* receive buffer size */
#define TARGET_SO_SNDLOWAT      0x1003  /* send low-water mark */
#define TARGET_SO_RCVLOWAT      0x1004  /* receive low-water mark */
#define TARGET_SO_SNDTIMEO      0x1005  /* send timeout */
#define TARGET_SO_RCVTIMEO      0x1006  /* receive timeout */
#define TARGET_SO_ERROR         0x1007  /* get error status and clear */
#define TARGET_SO_TYPE          0x1008  /* get socket type */
#define TARGET_SO_LABEL         0x1009  /* socket's MAC label */
#define TARGET_SO_PEERLABEL     0x1010  /* socket's peer's MAC label */
#define TARGET_SO_LISTENQLIMIT  0x1011  /* socket's backlog limit */
#define TARGET_SO_LISTENQLEN    0x1012  /* socket's complete queue length */
#define TARGET_SO_LISTENINCQLEN 0x1013  /* socket's incomplete queue length */
#define TARGET_SO_SETFIB        0x1014  /* use this FIB to route */
#define TARGET_SO_USER_COOKIE   0x1015  /* user cookie (dummynet etc.) */
#define TARGET_SO_PROTOCOL      0x1016  /* get socket protocol (Linux name) */

/* alias for SO_PROTOCOL (SunOS name) */
#define TARGET_SO_PROTOTYPE     TARGET_SO_PROTOCOL

/*
 * Level number for (get/set)sockopt() to apply to socket itself.
 */
#define TARGET_SOL_SOCKET       0xffff  /* options for socket level */

#ifndef CMSG_ALIGN
#define CMSG_ALIGN(len) (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#endif

/*
 * sys/socket.h
 */
struct target_msghdr {
    abi_long    msg_name;       /* Socket name */
    int32_t     msg_namelen;    /* Length of name */
    abi_long    msg_iov;        /* Data blocks */
    int32_t     msg_iovlen;     /* Number of blocks */
    abi_long    msg_control;    /* Per protocol magic (eg BSD fd passing) */
    int32_t     msg_controllen; /* Length of cmsg list */
    int32_t     msg_flags;      /* flags on received message */
};

struct target_sockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    uint8_t sa_data[14];
} QEMU_PACKED;

struct target_in_addr {
    uint32_t s_addr; /* big endian */
};

struct target_cmsghdr {
    uint32_t    cmsg_len;
    int32_t     cmsg_level;
    int32_t     cmsg_type;
};

/*
 * mips32 is the exception to the general rule of long-alignment; it
 * unconditionally uses 64-bit alignment instead.
 */
#if defined(TARGET_MIPS) && TARGET_ABI_BITS == 32
#define TARGET_ALIGNBYTES   (sizeof(abi_llong) - 1)
#else
#define TARGET_ALIGNBYTES   (sizeof(abi_long) - 1)
#endif

#define TARGET_CMSG_NXTHDR(mhdr, cmsg, cmsg_start) \
                               __target_cmsg_nxthdr(mhdr, cmsg, cmsg_start)
#define TARGET_CMSG_ALIGN(len) (((len) + TARGET_ALIGNBYTES) \
                               & (size_t) ~TARGET_ALIGNBYTES)
#define TARGET_CMSG_DATA(cmsg) \
    ((unsigned char *)(cmsg) + TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)))
#define TARGET_CMSG_SPACE(len) \
    (TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)) + TARGET_CMSG_ALIGN(len))
#define TARGET_CMSG_LEN(len) \
    (TARGET_CMSG_ALIGN(sizeof(struct target_cmsghdr)) + (len))

static inline struct target_cmsghdr *
__target_cmsg_nxthdr(struct target_msghdr *__mhdr,
                     struct target_cmsghdr *__cmsg,
                     struct target_cmsghdr *__cmsg_start)
{
    struct target_cmsghdr *__ptr;

    __ptr = (struct target_cmsghdr *)((unsigned char *) __cmsg +
        TARGET_CMSG_ALIGN(tswap32(__cmsg->cmsg_len)));
    if ((unsigned long)((char *)(__ptr + 1) - (char *)__cmsg_start) >
        tswap32(__mhdr->msg_controllen)) {
        /* No more entries.  */
        return (struct target_cmsghdr *)0;
    }
    return __ptr;
}

/*
 * netinet/in.h
 */
struct target_ip_mreq {
    struct target_in_addr   imr_multiaddr;
    struct target_in_addr   imr_interface;
};

struct target_ip_mreqn {
    struct target_in_addr   imr_multiaddr;
    struct target_in_addr   imr_address;
    int32_t                 imr_ifindex;
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

#if defined(TARGET_I386) && !defined(TARGET_X86_64)
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

/* sys/specialfd.h */
enum target_specialfd_type {
    TARGET_SPECIALFD_EVENT         = 1,
};

struct target_specialfd_eventfd {
    unsigned int initval;
    int flags;
};

/*
 * FreeBSD thread and user mutex support.
 */

/* sys/thr.h */
#define TARGET_THR_SUSPENDED    0x0001
#define TARGET_THR_SYSTEM_SCOPE 0x0002

struct target_freebsd_thr_param {
    abi_ulong   start_func; /* thread entry function. */
    abi_ulong   arg;        /* argument for entry function. */
    abi_ulong   stack_base; /* stack base address. */
    abi_ulong   stack_size; /* stack size. */
    abi_ulong   tls_base;   /* tls base address. */
    abi_ulong   tls_size;   /* tls size. */
    abi_ulong   child_tid;  /* address to store new TID. */
    abi_ulong   parent_tid; /* parent access the new TID here. */
    int32_t     flags;      /* thread flags. */
    abi_ulong   rtp;        /* Real-time scheduling priority. */
    abi_ulong   spare[3];   /* spares. */
};

/* sys/rtprio.h */
struct target_freebsd_rtprio {
    uint16_t    type;
    uint16_t    prio;
};

typedef struct {
    CPUArchState *env;
    long parent_tid;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    sigset_t sigmask;
    struct target_freebsd_thr_param param;
} new_freebsd_thread_info_t;

/* sys/utmx.h */
/* op code for _umtx_op */
#define TARGET_UMTX_OP_LOCK                 0
#define TARGET_UMTX_OP_UNLOCK               1
#define TARGET_UMTX_OP_WAIT                 2
#define TARGET_UMTX_OP_WAKE                 3
#define TARGET_UMTX_OP_MUTEX_TRYLOCK        4
#define TARGET_UMTX_OP_MUTEX_LOCK           5
#define TARGET_UMTX_OP_MUTEX_UNLOCK         6
#define TARGET_UMTX_OP_SET_CEILING          7
#define TARGET_UMTX_OP_CV_WAIT              8
#define TARGET_UMTX_OP_CV_SIGNAL            9
#define TARGET_UMTX_OP_CV_BROADCAST         10
#define TARGET_UMTX_OP_WAIT_UINT            11
#define TARGET_UMTX_OP_RW_RDLOCK            12
#define TARGET_UMTX_OP_RW_WRLOCK            13
#define TARGET_UMTX_OP_RW_UNLOCK            14
#define TARGET_UMTX_OP_WAIT_UINT_PRIVATE    15
#define TARGET_UMTX_OP_WAKE_PRIVATE         16
#define TARGET_UMTX_OP_MUTEX_WAIT           17
#define TARGET_UMTX_OP_MUTEX_WAKE           18
#define TARGET_UMTX_OP_SEM_WAIT             19
#define TARGET_UMTX_OP_SEM_WAKE             20
#define TARGET_UMTX_OP_NWAKE_PRIVATE        21
#define TARGET_UMTX_OP_MUTEX_WAKE2          22
#define TARGET_UMTX_OP_SEM2_WAIT            23
#define TARGET_UMTX_OP_SEM2_WAKE            24
#define TARGET_UMTX_OP_SHM                  25
#define TARGET_UMTX_OP_ROBUST_LISTS         26

/* flags for UMTX_OP_CV_WAIT */
#define TARGET_CVWAIT_CHECK_UNPARKING       0x01
#define TARGET_CVWAIT_ABSTIME               0x02
#define TARGET_CVWAIT_CLOCKID               0x04

#define TARGET_UMTX_UNOWNED                 0x0
#define TARGET_UMUTEX_UNOWNED               0x0
#define TARGET_UMTX_CONTESTED               (abi_ulong)(-1)
#define TARGET_UMUTEX_CONTESTED             0x80000000U

/* flags for umutex */
#define TARGET_UMUTEX_ERROR_CHECK   0x0002  /* Error-checking mutex */
#define TARGET_UMUTEX_PRIO_INHERIT  0x0004  /* Priority inherited mutex */
#define TARGET_UMUTEX_PRIO_PROTECT  0x0008  /* Priority protect mutex */

#define TARGET_UMUTEX_TRY           1
#define TARGET_UMUTEX_WAIT          2

/* urwlock flags */
#define TARGET_URWLOCK_PREFER_READER    0x0002
#define TARGET_URWLOCK_WRITE_OWNER      0x80000000U
#define TARGET_URWLOCK_WRITE_WAITERS    0x40000000U
#define TARGET_URWLOCK_READ_WAITERS     0x20000000U
#define TARGET_URWLOCK_MAX_READERS      0x1fffffffU
#define TARGET_URWLOCK_READER_COUNT(c)  ((c) & TARGET_URWLOCK_MAX_READERS)

/*
 * sys/acl.h
 */
#define TARGET_FREEBSD_ACL_MAX_ENTRIES          254

/* vaild acl_type_t arguments */
#define TARGET_FREEBSD_ACL_TYPE_ACCESS_OLD      0x00000000
#define TARGET_FREEBSD_ACL_TYPE_DEFAULT_OLD     0x00000001
#define TARGET_FREEBSD_ACL_TYPE_ACCESS          0x00000002
#define TARGET_FREEBSD_ACL_TYPE_DEFAULT         0x00000003
#define TARGET_FREEBSD_ACL_TYPE_NFS4            0x00000004

struct target_freebsd_acl_entry {
    uint32_t    ae_tag;
    uint32_t    ae_id;
    uint32_t    ae_perm;
    uint16_t    ae_entry_type;
    uint16_t    ae_flags;
};

struct target_freebsd_acl {
    uint32_t            acl_maxcnt;
    uint32_t            acl_cnt;
    int32_t             acl_spare[4];
    struct target_freebsd_acl_entry  acl_entry[TARGET_FREEBSD_ACL_MAX_ENTRIES];
};

/*
 *  sys/uuid.h
 */

#define TARGET_UUID_NODE_LEN    6

struct target_uuid {
    uint32_t    time_low;
    uint16_t    time_mid;
    uint16_t    time_hi_and_version;
    uint8_t     clock_seq_hi_and_reserved;
    uint8_t     clock_seq_low;
    uint8_t     node[TARGET_UUID_NODE_LEN];
};

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

#define safe_ioctl(...) safe_syscall(SYS_ioctl, __VA_ARGS__)
#define safe_fcntl(...) safe_syscall(SYS_fcntl, __VA_ARGS__)

struct target_pollfd {
    int32_t fd;         /* file descriptor */
    int16_t events;     /* requested events */
    int16_t revents;    /* returned events */
};

/* So far all target and host bitmasks are the same */
#undef  target_to_host_bitmask
#define target_to_host_bitmask(x, tbl) (x)
#undef  host_to_target_bitmask
#define host_to_target_bitmask(x, tbl) (x)

#endif /* SYSCALL_DEFS_H */
