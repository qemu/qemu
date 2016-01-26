/*
 *  Lattice Mico32 semihosting syscall interface
 *
 *  Copyright (c) 2014 Michael Walle <michael@walle.cc>
 *
 * Based on target-m68k/m68k-semi.c, which is
 *  Copyright (c) 2005-2007 CodeSourcery.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/log.h"
#include "exec/softmmu-semi.h"

enum {
    TARGET_SYS_exit    = 1,
    TARGET_SYS_open    = 2,
    TARGET_SYS_close   = 3,
    TARGET_SYS_read    = 4,
    TARGET_SYS_write   = 5,
    TARGET_SYS_lseek   = 6,
    TARGET_SYS_fstat   = 10,
    TARGET_SYS_stat    = 15,
};

enum {
    NEWLIB_O_RDONLY    =   0x0,
    NEWLIB_O_WRONLY    =   0x1,
    NEWLIB_O_RDWR      =   0x2,
    NEWLIB_O_APPEND    =   0x8,
    NEWLIB_O_CREAT     = 0x200,
    NEWLIB_O_TRUNC     = 0x400,
    NEWLIB_O_EXCL      = 0x800,
};

static int translate_openflags(int flags)
{
    int hf;

    if (flags & NEWLIB_O_WRONLY) {
        hf = O_WRONLY;
    } else if (flags & NEWLIB_O_RDWR) {
        hf = O_RDWR;
    } else {
        hf = O_RDONLY;
    }

    if (flags & NEWLIB_O_APPEND) {
        hf |= O_APPEND;
    }

    if (flags & NEWLIB_O_CREAT) {
        hf |= O_CREAT;
    }

    if (flags & NEWLIB_O_TRUNC) {
        hf |= O_TRUNC;
    }

    if (flags & NEWLIB_O_EXCL) {
        hf |= O_EXCL;
    }

    return hf;
}

struct newlib_stat {
    int16_t     newlib_st_dev;     /* device */
    uint16_t    newlib_st_ino;     /* inode */
    uint16_t    newlib_st_mode;    /* protection */
    uint16_t    newlib_st_nlink;   /* number of hard links */
    uint16_t    newlib_st_uid;     /* user ID of owner */
    uint16_t    newlib_st_gid;     /* group ID of owner */
    int16_t     newlib_st_rdev;    /* device type (if inode device) */
    int32_t     newlib_st_size;    /* total size, in bytes */
    int32_t     newlib_st_atime;   /* time of last access */
    uint32_t    newlib_st_spare1;
    int32_t     newlib_st_mtime;   /* time of last modification */
    uint32_t    newlib_st_spare2;
    int32_t     newlib_st_ctime;   /* time of last change */
    uint32_t    newlib_st_spare3;
} QEMU_PACKED;

static int translate_stat(CPULM32State *env, target_ulong addr,
        struct stat *s)
{
    struct newlib_stat *p;

    p = lock_user(VERIFY_WRITE, addr, sizeof(struct newlib_stat), 0);
    if (!p) {
        return 0;
    }
    p->newlib_st_dev = cpu_to_be16(s->st_dev);
    p->newlib_st_ino = cpu_to_be16(s->st_ino);
    p->newlib_st_mode = cpu_to_be16(s->st_mode);
    p->newlib_st_nlink = cpu_to_be16(s->st_nlink);
    p->newlib_st_uid = cpu_to_be16(s->st_uid);
    p->newlib_st_gid = cpu_to_be16(s->st_gid);
    p->newlib_st_rdev = cpu_to_be16(s->st_rdev);
    p->newlib_st_size = cpu_to_be32(s->st_size);
    p->newlib_st_atime = cpu_to_be32(s->st_atime);
    p->newlib_st_mtime = cpu_to_be32(s->st_mtime);
    p->newlib_st_ctime = cpu_to_be32(s->st_ctime);
    unlock_user(p, addr, sizeof(struct newlib_stat));

    return 1;
}

bool lm32_cpu_do_semihosting(CPUState *cs)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;

    int ret = -1;
    target_ulong nr, arg0, arg1, arg2;
    void *p;
    struct stat s;

    nr = env->regs[R_R8];
    arg0 = env->regs[R_R1];
    arg1 = env->regs[R_R2];
    arg2 = env->regs[R_R3];

    switch (nr) {
    case TARGET_SYS_exit:
        /* void _exit(int rc) */
        exit(arg0);

    case TARGET_SYS_open:
        /* int open(const char *pathname, int flags) */
        p = lock_user_string(arg0);
        if (!p) {
            ret = -1;
        } else {
            ret = open(p, translate_openflags(arg2));
            unlock_user(p, arg0, 0);
        }
        break;

    case TARGET_SYS_read:
        /* ssize_t read(int fd, const void *buf, size_t count) */
        p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
        if (!p) {
            ret = -1;
        } else {
            ret = read(arg0, p, arg2);
            unlock_user(p, arg1, arg2);
        }
        break;

    case TARGET_SYS_write:
        /* ssize_t write(int fd, const void *buf, size_t count) */
        p = lock_user(VERIFY_READ, arg1, arg2, 1);
        if (!p) {
            ret = -1;
        } else {
            ret = write(arg0, p, arg2);
            unlock_user(p, arg1, 0);
        }
        break;

    case TARGET_SYS_close:
        /* int close(int fd) */
        /* don't close stdin/stdout/stderr */
        if (arg0 > 2) {
            ret = close(arg0);
        } else {
            ret = 0;
        }
        break;

    case TARGET_SYS_lseek:
        /* off_t lseek(int fd, off_t offset, int whence */
        ret = lseek(arg0, arg1, arg2);
        break;

    case TARGET_SYS_stat:
        /* int stat(const char *path, struct stat *buf) */
        p = lock_user_string(arg0);
        if (!p) {
            ret = -1;
        } else {
            ret = stat(p, &s);
            unlock_user(p, arg0, 0);
            if (translate_stat(env, arg1, &s) == 0) {
                ret = -1;
            }
        }
        break;

    case TARGET_SYS_fstat:
        /* int stat(int fd, struct stat *buf) */
        ret = fstat(arg0, &s);
        if (ret == 0) {
            if (translate_stat(env, arg1, &s) == 0) {
                ret = -1;
            }
        }
        break;

    default:
        /* unhandled */
        return false;
    }

    env->regs[R_R1] = ret;
    return true;
}
