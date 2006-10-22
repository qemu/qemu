/*
 *  m68k/ColdFire Semihosting ssycall interface
 * 
 *  Copyright (c) 2005 CodeSourcery, LLC. Written by Paul Brook.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "qemu.h"

#define HOSTED_EXIT  0
#define HOSTED_PUTCHAR 1 /* Obsolete */
#define HOSTED_OPEN 2
#define HOSTED_CLOSE 3
#define HOSTED_READ 4
#define HOSTED_WRITE 5
#define HOSTED_LSEEK 6
#define HOSTED_RENAME 7
#define HOSTED_UNLINK 8
#define HOSTED_STAT 9
#define HOSTED_FSTAT 10
#define HOSTED_GETTIMEOFDAY 11
#define HOSTED_ISATTY 12
#define HOSTED_SYSTEM 13

typedef uint32_t gdb_mode_t;
typedef uint32_t gdb_time_t;

struct m68k_gdb_stat {
  uint32_t    gdb_st_dev;     /* device */
  uint32_t    gdb_st_ino;     /* inode */
  gdb_mode_t  gdb_st_mode;    /* protection */
  uint32_t    gdb_st_nlink;   /* number of hard links */
  uint32_t    gdb_st_uid;     /* user ID of owner */
  uint32_t    gdb_st_gid;     /* group ID of owner */
  uint32_t    gdb_st_rdev;    /* device type (if inode device) */
  uint64_t    gdb_st_size;    /* total size, in bytes */
  uint64_t    gdb_st_blksize; /* blocksize for filesystem I/O */
  uint64_t    gdb_st_blocks;  /* number of blocks allocated */
  gdb_time_t  gdb_st_atime;   /* time of last access */
  gdb_time_t  gdb_st_mtime;   /* time of last modification */
  gdb_time_t  gdb_st_ctime;   /* time of last change */
};

struct gdb_timeval {
  gdb_time_t tv_sec;  /* second */
  uint64_t tv_usec;   /* microsecond */
};

#define GDB_O_RDONLY   0x0
#define GDB_O_WRONLY   0x1
#define GDB_O_RDWR     0x2
#define GDB_O_APPEND   0x8
#define GDB_O_CREAT  0x200
#define GDB_O_TRUNC  0x400
#define GDB_O_EXCL   0x800

static int translate_openflags(int flags)
{
    int hf;

    if (flags & GDB_O_WRONLY)
        hf = O_WRONLY;
    else if (flags & GDB_O_RDWR)
        hf = O_RDWR;
    else
        hf = O_RDONLY;

    if (flags & GDB_O_APPEND) hf |= O_APPEND;
    if (flags & GDB_O_CREAT) hf |= O_CREAT;
    if (flags & GDB_O_TRUNC) hf |= O_TRUNC;
    if (flags & GDB_O_EXCL) hf |= O_EXCL;

    return hf;
}

static void translate_stat(struct m68k_gdb_stat *p, struct stat *s)
{
    p->gdb_st_dev = tswap16(s->st_dev);
    p->gdb_st_ino = tswap16(s->st_ino);
    p->gdb_st_mode = tswap32(s->st_mode);
    p->gdb_st_nlink = tswap16(s->st_nlink);
    p->gdb_st_uid = tswap16(s->st_uid);
    p->gdb_st_gid = tswap16(s->st_gid);
    p->gdb_st_rdev = tswap16(s->st_rdev);
    p->gdb_st_size = tswap32(s->st_size);
    p->gdb_st_atime = tswap32(s->st_atime);
    p->gdb_st_mtime = tswap32(s->st_mtime);
    p->gdb_st_ctime = tswap32(s->st_ctime);
    p->gdb_st_blksize = tswap32(s->st_blksize);
    p->gdb_st_blocks = tswap32(s->st_blocks);
}

static inline uint32_t check_err(CPUM68KState *env, uint32_t code)
{
  if (code == (uint32_t)-1) {
      env->sr |= CCF_C;
  } else {
      env->sr &= ~CCF_C;
      env->dregs[0] = code;
  }
  return code;
}

#define ARG(x) tswap32(args[x])
void do_m68k_semihosting(CPUM68KState *env, int nr)
{
    uint32_t *args;

    args = (uint32_t *)env->dregs[1];
    switch (nr) {
    case HOSTED_EXIT:
        exit(env->dregs[0]);
    case HOSTED_OPEN:
        /* Assume name is NULL terminated.  */
        check_err(env, open((char *)ARG(0), translate_openflags(ARG(2)),
                            ARG(3)));
        break;
    case HOSTED_CLOSE:
        {
            /* Ignore attempts to close stdin/out/err.  */
            int fd = ARG(0);
            if (fd > 2)
              check_err(env, close(fd));
            else
              check_err(env, 0);
            break;
        }
    case HOSTED_READ:
        check_err(env, read(ARG(0), (void *)ARG(1), ARG(2)));
        break;
    case HOSTED_WRITE:
        check_err(env, write(ARG(0), (void *)ARG(1), ARG(2)));
        break;
    case HOSTED_LSEEK:
        {
            uint64_t off;
            off = (uint32_t)ARG(2) | ((uint64_t)ARG(1) << 32);
            check_err(env, lseek(ARG(0), off, ARG(3)));
        }
        break;
    case HOSTED_RENAME:
        /* Assume names are NULL terminated.  */
        check_err(env, rename((char *)ARG(0), (char *)ARG(2)));
        break;
    case HOSTED_UNLINK:
        /* Assume name is NULL terminated.  */
        check_err(env, unlink((char *)ARG(0)));
        break;
    case HOSTED_STAT:
        /* Assume name is NULL terminated.  */
        {
            struct stat s;
            int rc;
            rc = check_err(env, stat((char *)ARG(0), &s));
            if (rc == 0) {
                translate_stat((struct m68k_gdb_stat *)ARG(2), &s);
            }
        }
        break;
    case HOSTED_FSTAT:
        {
            struct stat s;
            int rc;
            rc = check_err(env, fstat(ARG(0), &s));
            if (rc == 0) {
                translate_stat((struct m68k_gdb_stat *)ARG(1), &s);
            }
        }
        break;
    case HOSTED_GETTIMEOFDAY:
        {
            struct timeval tv;
            struct gdb_timeval *p;
            int rc;
            rc = check_err(env, gettimeofday(&tv, NULL));
            if (rc != 0) {
                p = (struct gdb_timeval *)ARG(0);
                p->tv_sec = tswap32(tv.tv_sec);
                p->tv_usec = tswap64(tv.tv_usec);
            }
        }
        break;
    case HOSTED_ISATTY:
        check_err(env, isatty(ARG(0)));
        break;
    case HOSTED_SYSTEM:
        /* Assume name is NULL terminated.  */
        check_err(env, system((char *)ARG(0)));
        break;
    default:
        cpu_abort(env, "Unsupported semihosting syscall %d\n", nr);
    }
}
