/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef GENERIC_FCNTL_H
#define GENERIC_FCNTL_H

/* <asm-generic/fcntl.h> values follow.  */
#define TARGET_O_ACCMODE          0003
#define TARGET_O_RDONLY             00
#define TARGET_O_WRONLY             01
#define TARGET_O_RDWR               02
#ifndef TARGET_O_CREAT
#define TARGET_O_CREAT            0100 /* not fcntl */
#endif
#ifndef TARGET_O_EXCL
#define TARGET_O_EXCL             0200 /* not fcntl */
#endif
#ifndef TARGET_O_NOCTTY
#define TARGET_O_NOCTTY           0400 /* not fcntl */
#endif
#ifndef TARGET_O_TRUNC
#define TARGET_O_TRUNC           01000 /* not fcntl */
#endif
#ifndef TARGET_O_APPEND
#define TARGET_O_APPEND          02000
#endif
#ifndef TARGET_O_NONBLOCK
#define TARGET_O_NONBLOCK        04000
#endif
#ifndef TARGET_O_DSYNC
#define TARGET_O_DSYNC          010000
#endif
#ifndef TARGET_FASYNC
#define TARGET_FASYNC           020000 /* fcntl, for BSD compatibility */
#endif
#ifndef TARGET_O_DIRECT
#define TARGET_O_DIRECT         040000 /* direct disk access hint */
#endif
#ifndef TARGET_O_LARGEFILE
#define TARGET_O_LARGEFILE     0100000
#endif
#ifndef TARGET_O_DIRECTORY
#define TARGET_O_DIRECTORY     0200000 /* must be a directory */
#endif
#ifndef TARGET_O_NOFOLLOW
#define TARGET_O_NOFOLLOW      0400000 /* don't follow links */
#endif
#ifndef TARGET_O_NOATIME
#define TARGET_O_NOATIME      01000000
#endif
#ifndef TARGET_O_CLOEXEC
#define TARGET_O_CLOEXEC      02000000
#endif
#ifndef TARGET___O_SYNC
#define TARGET___O_SYNC       04000000
#endif
#ifndef TARGET_O_PATH
#define TARGET_O_PATH        010000000
#endif
#ifndef TARGET___O_TMPFILE
#define TARGET___O_TMPFILE   020000000
#endif
#ifndef TARGET_O_TMPFILE
#define TARGET_O_TMPFILE     (TARGET___O_TMPFILE | TARGET_O_DIRECTORY)
#endif
#ifndef TARGET_O_NDELAY
#define TARGET_O_NDELAY  TARGET_O_NONBLOCK
#endif
#ifndef TARGET_O_SYNC
#define TARGET_O_SYNC    (TARGET___O_SYNC | TARGET_O_DSYNC)
#endif

#define TARGET_F_DUPFD         0       /* dup */
#define TARGET_F_GETFD         1       /* get close_on_exec */
#define TARGET_F_SETFD         2       /* set/clear close_on_exec */
#define TARGET_F_GETFL         3       /* get file->f_flags */
#define TARGET_F_SETFL         4       /* set file->f_flags */
#ifndef TARGET_F_GETLK
#define TARGET_F_GETLK         5
#define TARGET_F_SETLK         6
#define TARGET_F_SETLKW        7
#endif
#ifndef TARGET_F_SETOWN
#define TARGET_F_SETOWN        8       /*  for sockets. */
#define TARGET_F_GETOWN        9       /*  for sockets. */
#endif
#ifndef TARGET_F_SETSIG
#define TARGET_F_SETSIG        10      /*  for sockets. */
#define TARGET_F_GETSIG        11      /*  for sockets. */
#endif

#ifndef TARGET_F_GETLK64
#define TARGET_F_GETLK64       12      /*  using 'struct flock64' */
#define TARGET_F_SETLK64       13
#define TARGET_F_SETLKW64      14
#endif

#define TARGET_F_OFD_GETLK     36
#define TARGET_F_OFD_SETLK     37
#define TARGET_F_OFD_SETLKW    38

#ifndef TARGET_F_SETOWN_EX
#define TARGET_F_SETOWN_EX     15
#define TARGET_F_GETOWN_EX     16
#endif

struct target_f_owner_ex {
        int type;       /* Owner type of ID.  */
        int pid;        /* ID of owner.  */
};

#ifndef TARGET_F_RDLCK
#define TARGET_F_RDLCK         0
#define TARGET_F_WRLCK         1
#define TARGET_F_UNLCK         2
#endif

#ifndef TARGET_F_EXLCK
#define TARGET_F_EXLCK         4
#define TARGET_F_SHLCK         8
#endif

#ifndef TARGET_HAVE_ARCH_STRUCT_FLOCK
#ifndef TARGET_ARCH_FLOCK_PAD
#define TARGET_ARCH_FLOCK_PAD
#endif

struct target_flock {
    short l_type;
    short l_whence;
    abi_long l_start;
    abi_long l_len;
    int l_pid;
    TARGET_ARCH_FLOCK_PAD
};
#endif

#ifndef TARGET_HAVE_ARCH_STRUCT_FLOCK64
#ifndef TARGET_ARCH_FLOCK64_PAD
#define TARGET_ARCH_FLOCK64_PAD
#endif

struct target_flock64 {
    abi_short l_type;
    abi_short l_whence;
    abi_llong l_start;
    abi_llong l_len;
    abi_int   l_pid;
    TARGET_ARCH_FLOCK64_PAD
};
#endif

#endif
