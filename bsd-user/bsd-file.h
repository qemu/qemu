/*
 *  file related system call shims and definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef BSD_FILE_H
#define BSD_FILE_H

#include "qemu/path.h"

extern struct iovec *lock_iovec(int type, abi_ulong target_addr, int count,
        int copy);
extern void unlock_iovec(struct iovec *vec, abi_ulong target_addr, int count,
        int copy);

ssize_t safe_read(int fd, void *buf, size_t nbytes);
ssize_t safe_pread(int fd, void *buf, size_t nbytes, off_t offset);
ssize_t safe_readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t safe_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);

/* read(2) */
static abi_long do_bsd_read(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_WRITE, arg2, arg3, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(safe_read(arg1, p, arg3));
    unlock_user(p, arg2, ret);

    return ret;
}

/* pread(2) */
static abi_long do_bsd_pread(void *cpu_env, abi_long arg1,
    abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_WRITE, arg2, arg3, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    if (regpairs_aligned(cpu_env) != 0) {
        arg4 = arg5;
        arg5 = arg6;
    }
    ret = get_errno(safe_pread(arg1, p, arg3, target_arg64(arg4, arg5)));
    unlock_user(p, arg2, ret);

    return ret;
}

/* readv(2) */
static abi_long do_bsd_readv(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    struct iovec *vec = lock_iovec(VERIFY_WRITE, arg2, arg3, 0);

    if (vec != NULL) {
        ret = get_errno(safe_readv(arg1, vec, arg3));
        unlock_iovec(vec, arg2, arg3, 1);
    } else {
        ret = -host_to_target_errno(errno);
    }

    return ret;
}

/* preadv(2) */
static abi_long do_bsd_preadv(void *cpu_env, abi_long arg1,
    abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    struct iovec *vec = lock_iovec(VERIFY_WRITE, arg2, arg3, 1);

    if (vec != NULL) {
        if (regpairs_aligned(cpu_env) != 0) {
            arg4 = arg5;
            arg5 = arg6;
        }
        ret = get_errno(safe_preadv(arg1, vec, arg3, target_arg64(arg4, arg5)));
        unlock_iovec(vec, arg2, arg3, 0);
    } else {
        ret = -host_to_target_errno(errno);
    }

    return ret;
}

#endif /* BSD_FILE_H */
