/*
 *  m68k/ColdFire Semihosting syscall interface
 *
 *  Copyright (c) 2005-2007 CodeSourcery.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "cpu.h"
#if defined(CONFIG_USER_ONLY)
#include "qemu.h"
#define SEMIHOSTING_HEAP_SIZE (128 * 1024 * 1024)
#else
#include "qemu-common.h"
#include "gdbstub.h"
#include "softmmu-semi.h"
#endif
#include "sysemu.h"

#define HOSTED_EXIT  0
#define HOSTED_INIT_SIM 1
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
} __attribute__((packed));

struct gdb_timeval {
  gdb_time_t tv_sec;  /* second */
  uint64_t tv_usec;   /* microsecond */
} __attribute__((packed));

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

static void translate_stat(CPUState *env, target_ulong addr, struct stat *s)
{
    struct m68k_gdb_stat *p;

    if (!(p = lock_user(VERIFY_WRITE, addr, sizeof(struct m68k_gdb_stat), 0)))
        /* FIXME - should this return an error code? */
        return;
    p->gdb_st_dev = cpu_to_be32(s->st_dev);
    p->gdb_st_ino = cpu_to_be32(s->st_ino);
    p->gdb_st_mode = cpu_to_be32(s->st_mode);
    p->gdb_st_nlink = cpu_to_be32(s->st_nlink);
    p->gdb_st_uid = cpu_to_be32(s->st_uid);
    p->gdb_st_gid = cpu_to_be32(s->st_gid);
    p->gdb_st_rdev = cpu_to_be32(s->st_rdev);
    p->gdb_st_size = cpu_to_be64(s->st_size);
#ifdef _WIN32
    /* Windows stat is missing some fields.  */
    p->gdb_st_blksize = 0;
    p->gdb_st_blocks = 0;
#else
    p->gdb_st_blksize = cpu_to_be64(s->st_blksize);
    p->gdb_st_blocks = cpu_to_be64(s->st_blocks);
#endif
    p->gdb_st_atime = cpu_to_be32(s->st_atime);
    p->gdb_st_mtime = cpu_to_be32(s->st_mtime);
    p->gdb_st_ctime = cpu_to_be32(s->st_ctime);
    unlock_user(p, addr, sizeof(struct m68k_gdb_stat));
}

static int m68k_semi_is_fseek;

static void m68k_semi_cb(CPUState *env, target_ulong ret, target_ulong err)
{
    target_ulong args;

    args = env->dregs[1];
    if (m68k_semi_is_fseek) {
        /* FIXME: We've already lost the high bits of the fseek
           return value.  */
        /* FIXME - handle put_user() failure */
        put_user_u32(0, args);
        args += 4;
        m68k_semi_is_fseek = 0;
    }
    /* FIXME - handle put_user() failure */
    put_user_u32(ret, args);
    put_user_u32(errno, args + 4);
}

#define ARG(n)					\
({						\
    target_ulong __arg;				\
    /* FIXME - handle get_user() failure */	\
    get_user_ual(__arg, args + (n) * 4);	\
    __arg;					\
})
#define PARG(x) ((unsigned long)ARG(x))
void do_m68k_semihosting(CPUM68KState *env, int nr)
{
    uint32_t args;
    void *p;
    void *q;
    uint32_t len;
    uint32_t result;

    args = env->dregs[1];
    switch (nr) {
    case HOSTED_EXIT:
        gdb_exit(env, env->dregs[0]);
        exit(env->dregs[0]);
    case HOSTED_OPEN:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "open,%s,%x,%x", ARG(0), (int)ARG(1),
                           ARG(2), ARG(3));
            return;
        } else {
            if (!(p = lock_user_string(ARG(0)))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = open(p, translate_openflags(ARG(2)), ARG(3));
                unlock_user(p, ARG(0), 0);
            }
        }
        break;
    case HOSTED_CLOSE:
        {
            /* Ignore attempts to close stdin/out/err.  */
            int fd = ARG(0);
            if (fd > 2) {
                if (use_gdb_syscalls()) {
                    gdb_do_syscall(m68k_semi_cb, "close,%x", ARG(0));
                    return;
                } else {
                    result = close(fd);
                }
            } else {
                result = 0;
            }
            break;
        }
    case HOSTED_READ:
        len = ARG(2);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "read,%x,%x,%x",
                           ARG(0), ARG(1), len);
            return;
        } else {
            if (!(p = lock_user(VERIFY_WRITE, ARG(1), len, 0))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = read(ARG(0), p, len);
                unlock_user(p, ARG(1), len);
            }
        }
        break;
    case HOSTED_WRITE:
        len = ARG(2);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "write,%x,%x,%x",
                           ARG(0), ARG(1), len);
            return;
        } else {
            if (!(p = lock_user(VERIFY_READ, ARG(1), len, 1))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = write(ARG(0), p, len);
                unlock_user(p, ARG(0), 0);
            }
        }
        break;
    case HOSTED_LSEEK:
        {
            uint64_t off;
            off = (uint32_t)ARG(2) | ((uint64_t)ARG(1) << 32);
            if (use_gdb_syscalls()) {
                m68k_semi_is_fseek = 1;
                gdb_do_syscall(m68k_semi_cb, "fseek,%x,%lx,%x",
                               ARG(0), off, ARG(3));
            } else {
                off = lseek(ARG(0), off, ARG(3));
                /* FIXME - handle put_user() failure */
                put_user_u32(off >> 32, args);
                put_user_u32(off, args + 4);
                put_user_u32(errno, args + 8);
            }
            return;
        }
    case HOSTED_RENAME:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "rename,%s,%s",
                           ARG(0), (int)ARG(1), ARG(2), (int)ARG(3));
            return;
        } else {
            p = lock_user_string(ARG(0));
            q = lock_user_string(ARG(2));
            if (!p || !q) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = rename(p, q);
            }
            unlock_user(p, ARG(0), 0);
            unlock_user(q, ARG(2), 0);
        }
        break;
    case HOSTED_UNLINK:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "unlink,%s",
                           ARG(0), (int)ARG(1));
            return;
        } else {
            if (!(p = lock_user_string(ARG(0)))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = unlink(p);
                unlock_user(p, ARG(0), 0);
            }
        }
        break;
    case HOSTED_STAT:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "stat,%s,%x",
                           ARG(0), (int)ARG(1), ARG(2));
            return;
        } else {
            struct stat s;
            if (!(p = lock_user_string(ARG(0)))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = stat(p, &s);
                unlock_user(p, ARG(0), 0);
            }
            if (result == 0) {
                translate_stat(env, ARG(2), &s);
            }
        }
        break;
    case HOSTED_FSTAT:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "fstat,%x,%x",
                           ARG(0), ARG(1));
            return;
        } else {
            struct stat s;
            result = fstat(ARG(0), &s);
            if (result == 0) {
                translate_stat(env, ARG(1), &s);
            }
        }
        break;
    case HOSTED_GETTIMEOFDAY:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "gettimeofday,%x,%x",
                           ARG(0), ARG(1));
            return;
        } else {
            qemu_timeval tv;
            struct gdb_timeval *p;
            result = qemu_gettimeofday(&tv);
            if (result != 0) {
                if (!(p = lock_user(VERIFY_WRITE,
                                    ARG(0), sizeof(struct gdb_timeval), 0))) {
                    /* FIXME - check error code? */
                    result = -1;
                } else {
                    p->tv_sec = cpu_to_be32(tv.tv_sec);
                    p->tv_usec = cpu_to_be64(tv.tv_usec);
                    unlock_user(p, ARG(0), sizeof(struct gdb_timeval));
                }
            }
        }
        break;
    case HOSTED_ISATTY:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "isatty,%x", ARG(0));
            return;
        } else {
            result = isatty(ARG(0));
        }
        break;
    case HOSTED_SYSTEM:
        if (use_gdb_syscalls()) {
            gdb_do_syscall(m68k_semi_cb, "system,%s",
                           ARG(0), (int)ARG(1));
            return;
        } else {
            if (!(p = lock_user_string(ARG(0)))) {
                /* FIXME - check error code? */
                result = -1;
            } else {
                result = system(p);
                unlock_user(p, ARG(0), 0);
            }
        }
        break;
    case HOSTED_INIT_SIM:
#if defined(CONFIG_USER_ONLY)
        {
        TaskState *ts = env->opaque;
        /* Allocate the heap using sbrk.  */
        if (!ts->heap_limit) {
            long ret;
            uint32_t size;
            uint32_t base;

            base = do_brk(0);
            size = SEMIHOSTING_HEAP_SIZE;
            /* Try a big heap, and reduce the size if that fails.  */
            for (;;) {
                ret = do_brk(base + size);
                if (ret != -1)
                    break;
                size >>= 1;
            }
            ts->heap_limit = base + size;
        }
        /* This call may happen before we have writable memory, so return
           values directly in registers.  */
        env->dregs[1] = ts->heap_limit;
        env->aregs[7] = ts->stack_base;
        }
#else
        /* FIXME: This is wrong for boards where RAM does not start at
           address zero.  */
        env->dregs[1] = ram_size;
        env->aregs[7] = ram_size;
#endif
        return;
    default:
        cpu_abort(env, "Unsupported semihosting syscall %d\n", nr);
        result = 0;
    }
    /* FIXME - handle put_user() failure */
    put_user_u32(result, args);
    put_user_u32(errno, args + 4);
}
