/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef HPPA_TARGET_FCNTL_H
#define HPPA_TARGET_FCNTL_H

#define TARGET_O_NONBLOCK    000200004 /* HPUX has separate NDELAY & NONBLOCK */
#define TARGET_O_APPEND      000000010
#define TARGET_O_CREAT       000000400 /* not fcntl */
#define TARGET_O_EXCL        000002000 /* not fcntl */
#define TARGET_O_NOCTTY      000400000 /* not fcntl */
#define TARGET_O_DSYNC       001000000
#define TARGET_O_LARGEFILE   000004000
#define TARGET_O_DIRECTORY   000010000 /* must be a directory */
#define TARGET_O_NOFOLLOW    000000200 /* don't follow links */
#define TARGET_O_NOATIME     004000000
#define TARGET_O_CLOEXEC     010000000
#define TARGET___O_SYNC      000100000
#define TARGET_O_PATH        020000000

#define TARGET_F_RDLCK         1
#define TARGET_F_WRLCK         2
#define TARGET_F_UNLCK         3

#define TARGET_F_GETLK64       8       /*  using 'struct flock64' */
#define TARGET_F_SETLK64       9
#define TARGET_F_SETLKW64      10

#define TARGET_F_GETLK         5
#define TARGET_F_SETLK         6
#define TARGET_F_SETLKW        7
#define TARGET_F_GETOWN        11       /*  for sockets. */
#define TARGET_F_SETOWN        12       /*  for sockets. */
#define TARGET_F_SETSIG        13      /*  for sockets. */
#define TARGET_F_GETSIG        14      /*  for sockets. */

#include "../generic/fcntl.h"
#endif
