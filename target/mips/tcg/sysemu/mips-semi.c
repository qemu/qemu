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
#include "exec/gdbstub.h"
#include "semihosting/softmmu-uaccess.h"
#include "semihosting/semihost.h"
#include "semihosting/console.h"
#include "semihosting/syscalls.h"
#include "internal.h"

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

enum UHIErrno {
    UHI_EACCESS         = 13,
    UHI_EAGAIN          = 11,
    UHI_EBADF           = 9,
    UHI_EBADMSG         = 77,
    UHI_EBUSY           = 16,
    UHI_ECONNRESET      = 104,
    UHI_EEXIST          = 17,
    UHI_EFBIG           = 27,
    UHI_EINTR           = 4,
    UHI_EINVAL          = 22,
    UHI_EIO             = 5,
    UHI_EISDIR          = 21,
    UHI_ELOOP           = 92,
    UHI_EMFILE          = 24,
    UHI_EMLINK          = 31,
    UHI_ENAMETOOLONG    = 91,
    UHI_ENETDOWN        = 115,
    UHI_ENETUNREACH     = 114,
    UHI_ENFILE          = 23,
    UHI_ENOBUFS         = 105,
    UHI_ENOENT          = 2,
    UHI_ENOMEM          = 12,
    UHI_ENOSPC          = 28,
    UHI_ENOSR           = 63,
    UHI_ENOTCONN        = 128,
    UHI_ENOTDIR         = 20,
    UHI_ENXIO           = 6,
    UHI_EOVERFLOW       = 139,
    UHI_EPERM           = 1,
    UHI_EPIPE           = 32,
    UHI_ERANGE          = 34,
    UHI_EROFS           = 30,
    UHI_ESPIPE          = 29,
    UHI_ETIMEDOUT       = 116,
    UHI_ETXTBSY         = 26,
    UHI_EWOULDBLOCK     = 11,
    UHI_EXDEV           = 18,
};

static void report_fault(CPUMIPSState *env)
{
    int op = env->active_tc.gpr[25];
    error_report("Fault during UHI operation %d", op);
    abort();
}

static void uhi_cb(CPUState *cs, uint64_t ret, int err)
{
    CPUMIPSState *env = cs->env_ptr;

#define E(N) case E##N: err = UHI_E##N; break

    switch (err) {
    case 0:
        break;
    E(PERM);
    E(NOENT);
    E(INTR);
    E(BADF);
    E(BUSY);
    E(EXIST);
    E(NOTDIR);
    E(ISDIR);
    E(INVAL);
    E(NFILE);
    E(MFILE);
    E(FBIG);
    E(NOSPC);
    E(SPIPE);
    E(ROFS);
    E(NAMETOOLONG);
    default:
        err = UHI_EINVAL;
        break;
    case EFAULT:
        report_fault(env);
    }

#undef E

    env->active_tc.gpr[2] = ret;
    env->active_tc.gpr[3] = err;
}

static void uhi_fstat_cb(CPUState *cs, uint64_t ret, int err)
{
    QEMU_BUILD_BUG_ON(sizeof(UHIStat) < sizeof(struct gdb_stat));

    if (!err) {
        CPUMIPSState *env = cs->env_ptr;
        target_ulong addr = env->active_tc.gpr[5];
        UHIStat *dst = lock_user(VERIFY_WRITE, addr, sizeof(UHIStat), 1);
        struct gdb_stat s;

        if (!dst) {
            report_fault(env);
        }

        memcpy(&s, dst, sizeof(struct gdb_stat));
        memset(dst, 0, sizeof(UHIStat));

        dst->uhi_st_dev = tswap16(be32_to_cpu(s.gdb_st_dev));
        dst->uhi_st_ino = tswap16(be32_to_cpu(s.gdb_st_ino));
        dst->uhi_st_mode = tswap32(be32_to_cpu(s.gdb_st_mode));
        dst->uhi_st_nlink = tswap16(be32_to_cpu(s.gdb_st_nlink));
        dst->uhi_st_uid = tswap16(be32_to_cpu(s.gdb_st_uid));
        dst->uhi_st_gid = tswap16(be32_to_cpu(s.gdb_st_gid));
        dst->uhi_st_rdev = tswap16(be32_to_cpu(s.gdb_st_rdev));
        dst->uhi_st_size = tswap64(be64_to_cpu(s.gdb_st_size));
        dst->uhi_st_atime = tswap64(be32_to_cpu(s.gdb_st_atime));
        dst->uhi_st_mtime = tswap64(be32_to_cpu(s.gdb_st_mtime));
        dst->uhi_st_ctime = tswap64(be32_to_cpu(s.gdb_st_ctime));
        dst->uhi_st_blksize = tswap64(be64_to_cpu(s.gdb_st_blksize));
        dst->uhi_st_blocks = tswap64(be64_to_cpu(s.gdb_st_blocks));

        unlock_user(dst, addr, sizeof(UHIStat));
    }

    uhi_cb(cs, ret, err);
}

void mips_semihosting(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    target_ulong *gpr = env->active_tc.gpr;
    const UHIOp op = gpr[25];
    char *p;

    switch (op) {
    case UHI_exit:
        gdb_exit(gpr[4]);
        exit(gpr[4]);

    case UHI_open:
        {
            target_ulong fname = gpr[4];
            int ret = -1;

            p = lock_user_string(fname);
            if (!p) {
                report_fault(env);
            }
            if (!strcmp("/dev/stdin", p)) {
                ret = 0;
            } else if (!strcmp("/dev/stdout", p)) {
                ret = 1;
            } else if (!strcmp("/dev/stderr", p)) {
                ret = 2;
            }
            unlock_user(p, fname, 0);

            /* FIXME: reusing a guest fd doesn't seem correct. */
            if (ret >= 0) {
                gpr[2] = ret;
                break;
            }

            semihost_sys_open(cs, uhi_cb, fname, 0, gpr[5], gpr[6]);
        }
        break;

    case UHI_close:
        semihost_sys_close(cs, uhi_cb, gpr[4]);
        break;
    case UHI_read:
        semihost_sys_read(cs, uhi_cb, gpr[4], gpr[5], gpr[6]);
        break;
    case UHI_write:
        semihost_sys_write(cs, uhi_cb, gpr[4], gpr[5], gpr[6]);
        break;
    case UHI_lseek:
        semihost_sys_lseek(cs, uhi_cb, gpr[4], gpr[5], gpr[6]);
        break;
    case UHI_unlink:
        semihost_sys_remove(cs, uhi_cb, gpr[4], 0);
        break;
    case UHI_fstat:
        semihost_sys_fstat(cs, uhi_fstat_cb, gpr[4], gpr[5]);
        break;

    case UHI_argc:
        gpr[2] = semihosting_get_argc();
        break;
    case UHI_argnlen:
        {
            const char *s = semihosting_get_arg(gpr[4]);
            gpr[2] = s ? strlen(s) : -1;
        }
        break;
    case UHI_argn:
        {
            const char *s = semihosting_get_arg(gpr[4]);
            target_ulong addr;
            size_t len;

            if (!s) {
                gpr[2] = -1;
                break;
            }
            len = strlen(s) + 1;
            addr = gpr[5];
            p = lock_user(VERIFY_WRITE, addr, len, 0);
            if (!p) {
                report_fault(env);
            }
            memcpy(p, s, len);
            unlock_user(p, addr, len);
            gpr[2] = 0;
        }
        break;

    case UHI_plog:
        {
            target_ulong addr = gpr[4];
            ssize_t len = target_strlen(addr);
            GString *str;
            char *pct_d;

            if (len < 0) {
                report_fault(env);
            }
            p = lock_user(VERIFY_READ, addr, len, 1);
            if (!p) {
                report_fault(env);
            }

            pct_d = strstr(p, "%d");
            if (!pct_d) {
                unlock_user(p, addr, 0);
                semihost_sys_write(cs, uhi_cb, 2, addr, len);
                break;
            }

            str = g_string_new_len(p, pct_d - p);
            g_string_append_printf(str, "%d%s", (int)gpr[5], pct_d + 2);
            unlock_user(p, addr, 0);

            /*
             * When we're using gdb, we need a guest address, so
             * drop the string onto the stack below the stack pointer.
             */
            if (use_gdb_syscalls()) {
                addr = gpr[29] - str->len;
                p = lock_user(VERIFY_WRITE, addr, str->len, 0);
                if (!p) {
                    report_fault(env);
                }
                memcpy(p, str->str, str->len);
                unlock_user(p, addr, str->len);
                semihost_sys_write(cs, uhi_cb, 2, addr, str->len);
            } else {
                gpr[2] = qemu_semihosting_console_write(str->str, str->len);
            }
            g_string_free(str, true);
        }
        break;

    case UHI_assert:
        {
            const char *msg, *file;

            msg = lock_user_string(gpr[4]);
            if (!msg) {
                msg = "<EFAULT>";
            }
            file = lock_user_string(gpr[5]);
            if (!file) {
                file = "<EFAULT>";
            }

            error_report("UHI assertion \"%s\": file \"%s\", line %d",
                         msg, file, (int)gpr[6]);
            abort();
        }

    default:
        error_report("Unknown UHI operation %d", op);
        abort();
    }
    return;
}
