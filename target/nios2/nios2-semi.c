/*
 *  Nios II Semihosting syscall interface.
 *  This code is derived from m68k-semi.c.
 *  The semihosting protocol implemented here is described in the
 *  libgloss sources:
 *  https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=libgloss/nios2/nios2-semi.txt;hb=HEAD
 *
 *  Copyright (c) 2017-2019 Mentor Graphics
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

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/gdbstub.h"
#if defined(CONFIG_USER_ONLY)
#include "qemu.h"
#else
#include "qemu-common.h"
#include "exec/softmmu-semi.h"
#endif
#include "qemu/log.h"

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

struct nios2_gdb_stat {
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
} QEMU_PACKED;

struct gdb_timeval {
  gdb_time_t tv_sec;  /* second */
  uint64_t tv_usec;   /* microsecond */
} QEMU_PACKED;

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

    if (flags & GDB_O_WRONLY) {
        hf = O_WRONLY;
    } else if (flags & GDB_O_RDWR) {
        hf = O_RDWR;
    } else {
        hf = O_RDONLY;
    }

    if (flags & GDB_O_APPEND) {
        hf |= O_APPEND;
    }
    if (flags & GDB_O_CREAT) {
        hf |= O_CREAT;
    }
    if (flags & GDB_O_TRUNC) {
        hf |= O_TRUNC;
    }
    if (flags & GDB_O_EXCL) {
        hf |= O_EXCL;
    }

    return hf;
}

static bool translate_stat(CPUNios2State *env, target_ulong addr,
                           struct stat *s)
{
    struct nios2_gdb_stat *p;

    p = lock_user(VERIFY_WRITE, addr, sizeof(struct nios2_gdb_stat), 0);

    if (!p) {
        return false;
    }
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
    unlock_user(p, addr, sizeof(struct nios2_gdb_stat));
    return true;
}

static void nios2_semi_return_u32(CPUNios2State *env, uint32_t ret,
                                  uint32_t err)
{
    target_ulong args = env->regs[R_ARG1];
    if (put_user_u32(ret, args) ||
        put_user_u32(err, args + 4)) {
        /*
         * The nios2 semihosting ABI does not provide any way to report this
         * error to the guest, so the best we can do is log it in qemu.
         * It is always a guest error not to pass us a valid argument block.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: return value "
                      "discarded because argument block not writable\n");
    }
}

static void nios2_semi_return_u64(CPUNios2State *env, uint64_t ret,
                                  uint32_t err)
{
    target_ulong args = env->regs[R_ARG1];
    if (put_user_u32(ret >> 32, args) ||
        put_user_u32(ret, args + 4) ||
        put_user_u32(err, args + 8)) {
        /* No way to report this via nios2 semihosting ABI; just log it */
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: return value "
                      "discarded because argument block not writable\n");
    }
}

static int nios2_semi_is_lseek;

static void nios2_semi_cb(CPUState *cs, target_ulong ret, target_ulong err)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    if (nios2_semi_is_lseek) {
        /*
         * FIXME: We've already lost the high bits of the lseek
         * return value.
         */
        nios2_semi_return_u64(env, ret, err);
        nios2_semi_is_lseek = 0;
    } else {
        nios2_semi_return_u32(env, ret, err);
    }
}

/*
 * Read the input value from the argument block; fail the semihosting
 * call if the memory read fails.
 */
#define GET_ARG(n) do {                                 \
    if (get_user_ual(arg ## n, args + (n) * 4)) {       \
        result = -1;                                    \
        errno = EFAULT;                                 \
        goto failed;                                    \
    }                                                   \
} while (0)

void do_nios2_semihosting(CPUNios2State *env)
{
    int nr;
    uint32_t args;
    target_ulong arg0, arg1, arg2, arg3;
    void *p;
    void *q;
    uint32_t len;
    uint32_t result;

    nr = env->regs[R_ARG0];
    args = env->regs[R_ARG1];
    switch (nr) {
    case HOSTED_EXIT:
        gdb_exit(env->regs[R_ARG0]);
        exit(env->regs[R_ARG0]);
    case HOSTED_OPEN:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "open,%s,%x,%x", arg0, (int)arg1,
                           arg2, arg3);
            return;
        } else {
            p = lock_user_string(arg0);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = open(p, translate_openflags(arg2), arg3);
                unlock_user(p, arg0, 0);
            }
        }
        break;
    case HOSTED_CLOSE:
        {
            /* Ignore attempts to close stdin/out/err.  */
            GET_ARG(0);
            int fd = arg0;
            if (fd > 2) {
                if (use_gdb_syscalls()) {
                    gdb_do_syscall(nios2_semi_cb, "close,%x", arg0);
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
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "read,%x,%x,%x",
                           arg0, arg1, len);
            return;
        } else {
            p = lock_user(VERIFY_WRITE, arg1, len, 0);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = read(arg0, p, len);
                unlock_user(p, arg1, len);
            }
        }
        break;
    case HOSTED_WRITE:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        len = arg2;
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "write,%x,%x,%x",
                           arg0, arg1, len);
            return;
        } else {
            p = lock_user(VERIFY_READ, arg1, len, 1);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = write(arg0, p, len);
                unlock_user(p, arg0, 0);
            }
        }
        break;
    case HOSTED_LSEEK:
        {
            uint64_t off;
            GET_ARG(0);
            GET_ARG(1);
            GET_ARG(2);
            GET_ARG(3);
            off = (uint32_t)arg2 | ((uint64_t)arg1 << 32);
            if (use_gdb_syscalls()) {
                nios2_semi_is_lseek = 1;
                gdb_do_syscall(nios2_semi_cb, "lseek,%x,%lx,%x",
                               arg0, off, arg3);
            } else {
                off = lseek(arg0, off, arg3);
                nios2_semi_return_u64(env, off, errno);
            }
            return;
        }
    case HOSTED_RENAME:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        GET_ARG(3);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "rename,%s,%s",
                           arg0, (int)arg1, arg2, (int)arg3);
            return;
        } else {
            p = lock_user_string(arg0);
            q = lock_user_string(arg2);
            if (!p || !q) {
                result = -1;
                errno = EFAULT;
            } else {
                result = rename(p, q);
            }
            unlock_user(p, arg0, 0);
            unlock_user(q, arg2, 0);
        }
        break;
    case HOSTED_UNLINK:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "unlink,%s",
                           arg0, (int)arg1);
            return;
        } else {
            p = lock_user_string(arg0);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = unlink(p);
                unlock_user(p, arg0, 0);
            }
        }
        break;
    case HOSTED_STAT:
        GET_ARG(0);
        GET_ARG(1);
        GET_ARG(2);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "stat,%s,%x",
                           arg0, (int)arg1, arg2);
            return;
        } else {
            struct stat s;
            p = lock_user_string(arg0);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = stat(p, &s);
                unlock_user(p, arg0, 0);
            }
            if (result == 0 && !translate_stat(env, arg2, &s)) {
                result = -1;
                errno = EFAULT;
            }
        }
        break;
    case HOSTED_FSTAT:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "fstat,%x,%x",
                           arg0, arg1);
            return;
        } else {
            struct stat s;
            result = fstat(arg0, &s);
            if (result == 0 && !translate_stat(env, arg1, &s)) {
                result = -1;
                errno = EFAULT;
            }
        }
        break;
    case HOSTED_GETTIMEOFDAY:
        /* Only the tv parameter is used.  tz is assumed NULL.  */
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "gettimeofday,%x,%x",
                           arg0, 0);
            return;
        } else {
            qemu_timeval tv;
            struct gdb_timeval *p;
            result = qemu_gettimeofday(&tv);
            if (result == 0) {
                p = lock_user(VERIFY_WRITE, arg0, sizeof(struct gdb_timeval),
                              0);
                if (!p) {
                    result = -1;
                    errno = EFAULT;
                } else {
                    p->tv_sec = cpu_to_be32(tv.tv_sec);
                    p->tv_usec = cpu_to_be64(tv.tv_usec);
                    unlock_user(p, arg0, sizeof(struct gdb_timeval));
                }
            }
        }
        break;
    case HOSTED_ISATTY:
        GET_ARG(0);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "isatty,%x", arg0);
            return;
        } else {
            result = isatty(arg0);
        }
        break;
    case HOSTED_SYSTEM:
        GET_ARG(0);
        GET_ARG(1);
        if (use_gdb_syscalls()) {
            gdb_do_syscall(nios2_semi_cb, "system,%s",
                           arg0, (int)arg1);
            return;
        } else {
            p = lock_user_string(arg0);
            if (!p) {
                result = -1;
                errno = EFAULT;
            } else {
                result = system(p);
                unlock_user(p, arg0, 0);
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "nios2-semihosting: unsupported "
                      "semihosting syscall %d\n", nr);
        result = 0;
    }
failed:
    nios2_semi_return_u32(env, result, errno);
}
