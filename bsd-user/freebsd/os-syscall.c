/*
 *  BSD syscalls
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2013-2014 Stacey D. Son
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

/*
 * We need the FreeBSD "legacy" definitions. Rust needs the FreeBSD 11 system
 * calls since it doesn't use libc at all, so we have to emulate that despite
 * FreeBSD 11 being EOL'd.
 */
#define _WANT_FREEBSD11_STAT
#define _WANT_FREEBSD11_STATFS
#define _WANT_FREEBSD11_DIRENT
#define _WANT_KERNEL_ERRNO
#define _WANT_SEMUN
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include <sys/syscall.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <utime.h>

#include "qemu.h"
#include "signal-common.h"
#include "user/syscall-trace.h"

#include "bsd-file.h"
#include "bsd-proc.h"

/* I/O */
safe_syscall3(int, open, const char *, path, int, flags, mode_t, mode);
safe_syscall4(int, openat, int, fd, const char *, path, int, flags, mode_t,
    mode);

safe_syscall3(ssize_t, read, int, fd, void *, buf, size_t, nbytes);
safe_syscall4(ssize_t, pread, int, fd, void *, buf, size_t, nbytes, off_t,
    offset);
safe_syscall3(ssize_t, readv, int, fd, const struct iovec *, iov, int, iovcnt);
safe_syscall4(ssize_t, preadv, int, fd, const struct iovec *, iov, int, iovcnt,
    off_t, offset);

safe_syscall3(ssize_t, write, int, fd, void *, buf, size_t, nbytes);
safe_syscall4(ssize_t, pwrite, int, fd, void *, buf, size_t, nbytes, off_t,
    offset);
safe_syscall3(ssize_t, writev, int, fd, const struct iovec *, iov, int, iovcnt);
safe_syscall4(ssize_t, pwritev, int, fd, const struct iovec *, iov, int, iovcnt,
    off_t, offset);

void target_set_brk(abi_ulong new_brk)
{
}

/*
 * errno conversion.
 */
abi_long get_errno(abi_long ret)
{
    if (ret == -1) {
        return -host_to_target_errno(errno);
    } else {
        return ret;
    }
}

int host_to_target_errno(int err)
{
    /*
     * All the BSDs have the property that the error numbers are uniform across
     * all architectures for a given BSD, though they may vary between different
     * BSDs.
     */
    return err;
}

bool is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

/*
 * Unlocks a iovec. Unlike unlock_iovec, it assumes the tvec array itself is
 * already locked from target_addr. It will be unlocked as well as all the iovec
 * elements.
 */
static void helper_unlock_iovec(struct target_iovec *target_vec,
                                abi_ulong target_addr, struct iovec *vec,
                                int count, int copy)
{
    for (int i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);

        if (vec[i].iov_base) {
            unlock_user(vec[i].iov_base, base, copy ? vec[i].iov_len : 0);
        }
    }
    unlock_user(target_vec, target_addr, 0);
}

struct iovec *lock_iovec(int type, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;
    struct iovec *vec;
    abi_ulong total_len, max_len;
    int i;
    int err = 0;

    if (count == 0) {
        errno = 0;
        return NULL;
    }
    if (count < 0 || count > IOV_MAX) {
        errno = EINVAL;
        return NULL;
    }

    vec = g_try_new0(struct iovec, count);
    if (vec == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec == NULL) {
        err = EFAULT;
        goto fail2;
    }

    max_len = 0x7fffffff & MIN(TARGET_PAGE_MASK, PAGE_MASK);
    total_len = 0;

    for (i = 0; i < count; i++) {
        abi_ulong base = tswapal(target_vec[i].iov_base);
        abi_long len = tswapal(target_vec[i].iov_len);

        if (len < 0) {
            err = EINVAL;
            goto fail;
        } else if (len == 0) {
            /* Zero length pointer is ignored. */
            vec[i].iov_base = 0;
        } else {
            vec[i].iov_base = lock_user(type, base, len, copy);
            /*
             * If the first buffer pointer is bad, this is a fault.  But
             * subsequent bad buffers will result in a partial write; this is
             * realized by filling the vector with null pointers and zero
             * lengths.
             */
            if (!vec[i].iov_base) {
                if (i == 0) {
                    err = EFAULT;
                    goto fail;
                } else {
                    /*
                     * Fail all the subsequent addresses, they are already
                     * zero'd.
                     */
                    goto out;
                }
            }
            if (len > max_len - total_len) {
                len = max_len - total_len;
            }
        }
        vec[i].iov_len = len;
        total_len += len;
    }
out:
    unlock_user(target_vec, target_addr, 0);
    return vec;

fail:
    helper_unlock_iovec(target_vec, target_addr, vec, i, copy);
fail2:
    g_free(vec);
    errno = err;
    return NULL;
}

void unlock_iovec(struct iovec *vec, abi_ulong target_addr,
        int count, int copy)
{
    struct target_iovec *target_vec;

    target_vec = lock_user(VERIFY_READ, target_addr,
                           count * sizeof(struct target_iovec), 1);
    if (target_vec) {
        helper_unlock_iovec(target_vec, target_addr, vec, count, copy);
    }

    g_free(vec);
}

/*
 * All errnos that freebsd_syscall() returns must be -TARGET_<errcode>.
 */
static abi_long freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                                abi_long arg2, abi_long arg3, abi_long arg4,
                                abi_long arg5, abi_long arg6, abi_long arg7,
                                abi_long arg8)
{
    abi_long ret;

    switch (num) {
        /*
         * process system calls
         */
    case TARGET_FREEBSD_NR_exit: /* exit(2) */
        ret = do_bsd_exit(cpu_env, arg1);
        break;

        /*
         * File system calls.
         */
    case TARGET_FREEBSD_NR_read: /* read(2) */
        ret = do_bsd_read(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pread: /* pread(2) */
        ret = do_bsd_pread(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_readv: /* readv(2) */
        ret = do_bsd_readv(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_preadv: /* preadv(2) */
        ret = do_bsd_preadv(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);

    case TARGET_FREEBSD_NR_write: /* write(2) */
        ret = do_bsd_write(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwrite: /* pwrite(2) */
        ret = do_bsd_pwrite(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_writev: /* writev(2) */
        ret = do_bsd_writev(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwritev: /* pwritev(2) */
        ret = do_bsd_pwritev(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_open: /* open(2) */
        ret = do_bsd_open(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_openat: /* openat(2) */
        ret = do_bsd_openat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_close: /* close(2) */
        ret = do_bsd_close(arg1);
        break;

    case TARGET_FREEBSD_NR_fdatasync: /* fdatasync(2) */
        ret = do_bsd_fdatasync(arg1);
        break;

    case TARGET_FREEBSD_NR_fsync: /* fsync(2) */
        ret = do_bsd_fsync(arg1);
        break;

    case TARGET_FREEBSD_NR_freebsd12_closefrom: /* closefrom(2) */
        ret = do_bsd_closefrom(arg1);
        break;

    case TARGET_FREEBSD_NR_revoke: /* revoke(2) */
        ret = do_bsd_revoke(arg1);
        break;

    case TARGET_FREEBSD_NR_access: /* access(2) */
        ret = do_bsd_access(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_eaccess: /* eaccess(2) */
        ret = do_bsd_eaccess(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_faccessat: /* faccessat(2) */
        ret = do_bsd_faccessat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_chdir: /* chdir(2) */
        ret = do_bsd_chdir(arg1);
        break;

    case TARGET_FREEBSD_NR_fchdir: /* fchdir(2) */
        ret = do_bsd_fchdir(arg1);
        break;

    case TARGET_FREEBSD_NR_rename: /* rename(2) */
        ret = do_bsd_rename(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_renameat: /* renameat(2) */
        ret = do_bsd_renameat(arg1, arg2, arg3, arg4);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported syscall: %d\n", num);
        ret = -TARGET_ENOSYS;
        break;
    }

    return ret;
}

/*
 * do_freebsd_syscall() should always have a single exit point at the end so
 * that actions, such as logging of syscall results, can be performed. This
 * as a wrapper around freebsd_syscall() so that actually happens. Since
 * that is a singleton, modern compilers will inline it anyway...
 */
abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    CPUState *cpu = env_cpu(cpu_env);
    int ret;

    trace_guest_user_syscall(cpu, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
    if (do_strace) {
        print_freebsd_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    ret = freebsd_syscall(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6,
                          arg7, arg8);
    if (do_strace) {
        print_freebsd_syscall_ret(num, ret);
    }
    trace_guest_user_syscall_ret(cpu, num, ret);

    return ret;
}

void syscall_init(void)
{
}
