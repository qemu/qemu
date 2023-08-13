/*
 *  stat related system call shims and definitions
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

#ifndef BSD_USER_FREEBSD_OS_STAT_H
#define BSD_USER_FREEBSD_OS_STAT_H

/* stat(2) */
static inline abi_long do_freebsd11_stat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct freebsd11_stat st;

    LOCK_PATH(p, arg1);
    ret = get_errno(freebsd11_stat(path(p), &st));
    UNLOCK_PATH(p, arg1);
    if (!is_error(ret)) {
        ret = h2t_freebsd11_stat(arg2, &st);
    }
    return ret;
}

/* lstat(2) */
static inline abi_long do_freebsd11_lstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct freebsd11_stat st;

    LOCK_PATH(p, arg1);
    ret = get_errno(freebsd11_lstat(path(p), &st));
    UNLOCK_PATH(p, arg1);
    if (!is_error(ret)) {
        ret = h2t_freebsd11_stat(arg2, &st);
    }
    return ret;
}

/* fstat(2) */
static inline abi_long do_freebsd_fstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    struct stat st;

    ret = get_errno(fstat(arg1, &st));
    if (!is_error(ret))  {
        ret = h2t_freebsd_stat(arg2, &st);
    }
    return ret;
}

/* fstatat(2) */
static inline abi_long do_freebsd_fstatat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;
    struct stat st;

    LOCK_PATH(p, arg2);
    ret = get_errno(fstatat(arg1, p, &st, arg4));
    UNLOCK_PATH(p, arg2);
    if (!is_error(ret) && arg3) {
        ret = h2t_freebsd_stat(arg3, &st);
    }
    return ret;
}

/* undocummented nstat(char *path, struct nstat *ub) syscall */
static abi_long do_freebsd11_nstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct freebsd11_stat st;

    LOCK_PATH(p, arg1);
    ret = get_errno(freebsd11_nstat(path(p), &st));
    UNLOCK_PATH(p, arg1);
    if (!is_error(ret)) {
        ret = h2t_freebsd11_nstat(arg2, &st);
    }
    return ret;
}

/* undocummented nfstat(int fd, struct nstat *sb) syscall */
static abi_long do_freebsd11_nfstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    struct freebsd11_stat st;

    ret = get_errno(freebsd11_nfstat(arg1, &st));
    if (!is_error(ret))  {
        ret = h2t_freebsd11_nstat(arg2, &st);
    }
    return ret;
}

/* undocummented nlstat(char *path, struct nstat *ub) syscall */
static abi_long do_freebsd11_nlstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct freebsd11_stat st;

    LOCK_PATH(p, arg1);
    ret = get_errno(freebsd11_nlstat(path(p), &st));
    UNLOCK_PATH(p, arg1);
    if (!is_error(ret)) {
        ret = h2t_freebsd11_nstat(arg2, &st);
    }
    return ret;
}

#endif /* BSD_USER_FREEBSD_OS_STAT_H */
