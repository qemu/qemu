/*
 * Unified Hosting Interface syscalls.
 *
 * Copyright (c) 2015 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "exec/softmmu-semi.h"
#include "semihosting/semihost.h"
#include "semihosting/console.h"

typedef enum UHIOp {
    UHI_exit = 1,
    UHI_open = 2,
    UHI_close = 3,
    UHI_read = 4,
    UHI_write = 5,
    UHI_lseek = 6,
    UHI_unlink = 7,
    UHI_fstat = 8,
    UHI_argc = 9,
    UHI_argnlen = 10,
    UHI_argn = 11,
    UHI_plog = 13,
    UHI_assert = 14,
    UHI_pread = 19,
    UHI_pwrite = 20,
    UHI_link = 22
} UHIOp;

typedef struct UHIStat {
    int16_t uhi_st_dev;
    uint16_t uhi_st_ino;
    uint32_t uhi_st_mode;
    uint16_t uhi_st_nlink;
    uint16_t uhi_st_uid;
    uint16_t uhi_st_gid;
    int16_t uhi_st_rdev;
    uint64_t uhi_st_size;
    uint64_t uhi_st_atime;
    uint64_t uhi_st_spare1;
    uint64_t uhi_st_mtime;
    uint64_t uhi_st_spare2;
    uint64_t uhi_st_ctime;
    uint64_t uhi_st_spare3;
    uint64_t uhi_st_blksize;
    uint64_t uhi_st_blocks;
    uint64_t uhi_st_spare4[2];
} UHIStat;

enum UHIOpenFlags {
    UHIOpen_RDONLY = 0x0,
    UHIOpen_WRONLY = 0x1,
    UHIOpen_RDWR   = 0x2,
    UHIOpen_APPEND = 0x8,
    UHIOpen_CREAT  = 0x200,
    UHIOpen_TRUNC  = 0x400,
    UHIOpen_EXCL   = 0x800
};

static int errno_mips(int host_errno)
{
    /* Errno values taken from asm-mips/errno.h */
    switch (host_errno) {
    case 0:             return 0;
    case ENAMETOOLONG:  return 78;
#ifdef EOVERFLOW
    case EOVERFLOW:     return 79;
#endif
#ifdef ELOOP
    case ELOOP:         return 90;
#endif
    default:            return EINVAL;
    }
}

static int copy_stat_to_target(CPUMIPSState *env, const struct stat *src,
                               target_ulong vaddr)
{
    hwaddr len = sizeof(struct UHIStat);
    UHIStat *dst = lock_user(VERIFY_WRITE, vaddr, len, 0);
    if (!dst) {
        errno = EFAULT;
        return -1;
    }

    dst->uhi_st_dev = tswap16(src->st_dev);
    dst->uhi_st_ino = tswap16(src->st_ino);
    dst->uhi_st_mode = tswap32(src->st_mode);
    dst->uhi_st_nlink = tswap16(src->st_nlink);
    dst->uhi_st_uid = tswap16(src->st_uid);
    dst->uhi_st_gid = tswap16(src->st_gid);
    dst->uhi_st_rdev = tswap16(src->st_rdev);
    dst->uhi_st_size = tswap64(src->st_size);
    dst->uhi_st_atime = tswap64(src->st_atime);
    dst->uhi_st_mtime = tswap64(src->st_mtime);
    dst->uhi_st_ctime = tswap64(src->st_ctime);
#ifdef _WIN32
    dst->uhi_st_blksize = 0;
    dst->uhi_st_blocks = 0;
#else
    dst->uhi_st_blksize = tswap64(src->st_blksize);
    dst->uhi_st_blocks = tswap64(src->st_blocks);
#endif
    unlock_user(dst, vaddr, len);
    return 0;
}

static int get_open_flags(target_ulong target_flags)
{
    int open_flags = 0;

    if (target_flags & UHIOpen_RDWR) {
        open_flags |= O_RDWR;
    } else if (target_flags & UHIOpen_WRONLY) {
        open_flags |= O_WRONLY;
    } else {
        open_flags |= O_RDONLY;
    }

    open_flags |= (target_flags & UHIOpen_APPEND) ? O_APPEND : 0;
    open_flags |= (target_flags & UHIOpen_CREAT)  ? O_CREAT  : 0;
    open_flags |= (target_flags & UHIOpen_TRUNC)  ? O_TRUNC  : 0;
    open_flags |= (target_flags & UHIOpen_EXCL)   ? O_EXCL   : 0;

    return open_flags;
}

static int write_to_file(CPUMIPSState *env, target_ulong fd, target_ulong vaddr,
                         target_ulong len, target_ulong offset)
{
    int num_of_bytes;
    void *dst = lock_user(VERIFY_READ, vaddr, len, 1);
    if (!dst) {
        errno = EFAULT;
        return -1;
    }

    if (offset) {
#ifdef _WIN32
        num_of_bytes = 0;
#else
        num_of_bytes = pwrite(fd, dst, len, offset);
#endif
    } else {
        num_of_bytes = write(fd, dst, len);
    }

    unlock_user(dst, vaddr, 0);
    return num_of_bytes;
}

static int read_from_file(CPUMIPSState *env, target_ulong fd,
                          target_ulong vaddr, target_ulong len,
                          target_ulong offset)
{
    int num_of_bytes;
    void *dst = lock_user(VERIFY_WRITE, vaddr, len, 0);
    if (!dst) {
        errno = EFAULT;
        return -1;
    }

    if (offset) {
#ifdef _WIN32
        num_of_bytes = 0;
#else
        num_of_bytes = pread(fd, dst, len, offset);
#endif
    } else {
        num_of_bytes = read(fd, dst, len);
    }

    unlock_user(dst, vaddr, len);
    return num_of_bytes;
}

static int copy_argn_to_target(CPUMIPSState *env, int arg_num,
                               target_ulong vaddr)
{
    int strsize = strlen(semihosting_get_arg(arg_num)) + 1;
    char *dst = lock_user(VERIFY_WRITE, vaddr, strsize, 0);
    if (!dst) {
        return -1;
    }

    strcpy(dst, semihosting_get_arg(arg_num));

    unlock_user(dst, vaddr, strsize);
    return 0;
}

#define GET_TARGET_STRING(p, addr)              \
    do {                                        \
        p = lock_user_string(addr);             \
        if (!p) {                               \
            gpr[2] = -1;                        \
            gpr[3] = EFAULT;                    \
            return;                             \
        }                                       \
    } while (0)

#define GET_TARGET_STRINGS_2(p, addr, p2, addr2)        \
    do {                                                \
        p = lock_user_string(addr);                     \
        if (!p) {                                       \
            gpr[2] = -1;                                \
            gpr[3] = EFAULT;                            \
            return;                                     \
        }                                               \
        p2 = lock_user_string(addr2);                   \
        if (!p2) {                                      \
            unlock_user(p, addr, 0);                    \
            gpr[2] = -1;                                \
            gpr[3] = EFAULT;                            \
            return;                                     \
        }                                               \
    } while (0)

#define FREE_TARGET_STRING(p, gpr)              \
    do {                                        \
        unlock_user(p, gpr, 0);                 \
    } while (0)

void helper_do_semihosting(CPUMIPSState *env)
{
    target_ulong *gpr = env->active_tc.gpr;
    const UHIOp op = gpr[25];
    char *p, *p2;

    switch (op) {
    case UHI_exit:
        qemu_log("UHI(%d): exit(%d)\n", op, (int)gpr[4]);
        exit(gpr[4]);
    case UHI_open:
        GET_TARGET_STRING(p, gpr[4]);
        if (!strcmp("/dev/stdin", p)) {
            gpr[2] = 0;
        } else if (!strcmp("/dev/stdout", p)) {
            gpr[2] = 1;
        } else if (!strcmp("/dev/stderr", p)) {
            gpr[2] = 2;
        } else {
            gpr[2] = open(p, get_open_flags(gpr[5]), gpr[6]);
            gpr[3] = errno_mips(errno);
        }
        FREE_TARGET_STRING(p, gpr[4]);
        break;
    case UHI_close:
        if (gpr[4] < 3) {
            /* ignore closing stdin/stdout/stderr */
            gpr[2] = 0;
            return;
        }
        gpr[2] = close(gpr[4]);
        gpr[3] = errno_mips(errno);
        break;
    case UHI_read:
        gpr[2] = read_from_file(env, gpr[4], gpr[5], gpr[6], 0);
        gpr[3] = errno_mips(errno);
        break;
    case UHI_write:
        gpr[2] = write_to_file(env, gpr[4], gpr[5], gpr[6], 0);
        gpr[3] = errno_mips(errno);
        break;
    case UHI_lseek:
        gpr[2] = lseek(gpr[4], gpr[5], gpr[6]);
        gpr[3] = errno_mips(errno);
        break;
    case UHI_unlink:
        GET_TARGET_STRING(p, gpr[4]);
        gpr[2] = remove(p);
        gpr[3] = errno_mips(errno);
        FREE_TARGET_STRING(p, gpr[4]);
        break;
    case UHI_fstat:
        {
            struct stat sbuf;
            memset(&sbuf, 0, sizeof(sbuf));
            gpr[2] = fstat(gpr[4], &sbuf);
            gpr[3] = errno_mips(errno);
            if (gpr[2]) {
                return;
            }
            gpr[2] = copy_stat_to_target(env, &sbuf, gpr[5]);
            gpr[3] = errno_mips(errno);
        }
        break;
    case UHI_argc:
        gpr[2] = semihosting_get_argc();
        break;
    case UHI_argnlen:
        if (gpr[4] >= semihosting_get_argc()) {
            gpr[2] = -1;
            return;
        }
        gpr[2] = strlen(semihosting_get_arg(gpr[4]));
        break;
    case UHI_argn:
        if (gpr[4] >= semihosting_get_argc()) {
            gpr[2] = -1;
            return;
        }
        gpr[2] = copy_argn_to_target(env, gpr[4], gpr[5]);
        break;
    case UHI_plog:
        GET_TARGET_STRING(p, gpr[4]);
        p2 = strstr(p, "%d");
        if (p2) {
            int char_num = p2 - p;
            GString *s = g_string_new_len(p, char_num);
            g_string_append_printf(s, "%d%s", (int)gpr[5], p2 + 2);
            gpr[2] = qemu_semihosting_log_out(s->str, s->len);
            g_string_free(s, true);
        } else {
            gpr[2] = qemu_semihosting_log_out(p, strlen(p));
        }
        FREE_TARGET_STRING(p, gpr[4]);
        break;
    case UHI_assert:
        GET_TARGET_STRINGS_2(p, gpr[4], p2, gpr[5]);
        printf("assertion '");
        printf("\"%s\"", p);
        printf("': file \"%s\", line %d\n", p2, (int)gpr[6]);
        FREE_TARGET_STRING(p2, gpr[5]);
        FREE_TARGET_STRING(p, gpr[4]);
        abort();
        break;
    case UHI_pread:
        gpr[2] = read_from_file(env, gpr[4], gpr[5], gpr[6], gpr[7]);
        gpr[3] = errno_mips(errno);
        break;
    case UHI_pwrite:
        gpr[2] = write_to_file(env, gpr[4], gpr[5], gpr[6], gpr[7]);
        gpr[3] = errno_mips(errno);
        break;
#ifndef _WIN32
    case UHI_link:
        GET_TARGET_STRINGS_2(p, gpr[4], p2, gpr[5]);
        gpr[2] = link(p, p2);
        gpr[3] = errno_mips(errno);
        FREE_TARGET_STRING(p2, gpr[5]);
        FREE_TARGET_STRING(p, gpr[4]);
        break;
#endif
    default:
        fprintf(stderr, "Unknown UHI operation %d\n", op);
        abort();
    }
    return;
}
