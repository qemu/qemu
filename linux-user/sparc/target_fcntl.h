/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef SPARC_TARGET_FCNTL_H
#define SPARC_TARGET_FCNTL_H

#define TARGET_O_APPEND         0x0008
#define TARGET_FASYNC           0x0040  /* fcntl, for BSD compatibility */
#define TARGET_O_CREAT          0x0200  /* not fcntl */
#define TARGET_O_TRUNC          0x0400  /* not fcntl */
#define TARGET_O_EXCL           0x0800  /* not fcntl */
#define TARGET_O_DSYNC          0x2000
#define TARGET_O_NONBLOCK       0x4000
# ifdef TARGET_SPARC64
#  define TARGET_O_NDELAY       0x0004
# else
#  define TARGET_O_NDELAY       (0x0004 | TARGET_O_NONBLOCK)
# endif
#define TARGET_O_NOCTTY         0x8000  /* not fcntl */
#define TARGET_O_LARGEFILE     0x40000
#define TARGET_O_DIRECT       0x100000  /* direct disk access hint */
#define TARGET_O_NOATIME      0x200000
#define TARGET_O_CLOEXEC      0x400000
#define TARGET___O_SYNC       0x800000
#define TARGET_O_PATH        0x1000000
#define TARGET___O_TMPFILE   0x2000000

#define TARGET_F_RDLCK         1
#define TARGET_F_WRLCK         2
#define TARGET_F_UNLCK         3
#define TARGET_F_GETOWN        5       /*  for sockets. */
#define TARGET_F_SETOWN        6       /*  for sockets. */
#define TARGET_F_GETLK         7
#define TARGET_F_SETLK         8
#define TARGET_F_SETLKW        9

#define TARGET_ARCH_FLOCK_PAD abi_short __unused;
#define TARGET_ARCH_FLOCK64_PAD abi_short __unused;

#include "../generic/fcntl.h"
#endif
