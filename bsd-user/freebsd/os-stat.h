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

/* getfh(2) */
static abi_long do_freebsd_getfh(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    fhandle_t host_fh;

    LOCK_PATH(p, arg1);
    ret = get_errno(getfh(path(p), &host_fh));
    UNLOCK_PATH(p, arg1);
    if (is_error(ret)) {
        return ret;
    }
    return h2t_freebsd_fhandle(arg2, &host_fh);
}

/* lgetfh(2) */
static inline abi_long do_freebsd_lgetfh(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    fhandle_t host_fh;

    LOCK_PATH(p, arg1);
    ret = get_errno(lgetfh(path(p), &host_fh));
    UNLOCK_PATH(p, arg1);
    if (is_error(ret)) {
        return ret;
    }
    return h2t_freebsd_fhandle(arg2, &host_fh);
}

/* fhopen(2) */
static inline abi_long do_freebsd_fhopen(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    fhandle_t host_fh;

    ret = t2h_freebsd_fhandle(&host_fh, arg1);
    if (is_error(ret)) {
        return ret;
    }

    return get_errno(fhopen(&host_fh, arg2));
}

/* fhstat(2) */
static inline abi_long do_freebsd_fhstat(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    fhandle_t host_fh;
    struct stat host_sb;

    ret = t2h_freebsd_fhandle(&host_fh, arg1);
    if (is_error(ret)) {
        return ret;
    }
    ret = get_errno(fhstat(&host_fh, &host_sb));
    if (is_error(ret)) {
        return ret;
    }
    return h2t_freebsd_stat(arg2, &host_sb);
}

/* fhstatfs(2) */
static inline abi_long do_freebsd_fhstatfs(abi_ulong target_fhp_addr,
        abi_ulong target_stfs_addr)
{
    abi_long ret;
    fhandle_t host_fh;
    struct statfs host_stfs;

    ret = t2h_freebsd_fhandle(&host_fh, target_fhp_addr);
    if (is_error(ret)) {
        return ret;
    }
    ret = get_errno(fhstatfs(&host_fh, &host_stfs));
    if (is_error(ret)) {
        return ret;
    }
    return h2t_freebsd_statfs(target_stfs_addr, &host_stfs);
}

/* statfs(2) */
static inline abi_long do_freebsd_statfs(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;
    struct statfs host_stfs;

    LOCK_PATH(p, arg1);
    ret = get_errno(statfs(path(p), &host_stfs));
    UNLOCK_PATH(p, arg1);
    if (is_error(ret)) {
        return ret;
    }

    return h2t_freebsd_statfs(arg2, &host_stfs);
}

/* fstatfs(2) */
static inline abi_long do_freebsd_fstatfs(abi_long fd, abi_ulong target_addr)
{
    abi_long ret;
    struct statfs host_stfs;

    ret = get_errno(fstatfs(fd, &host_stfs));
    if (is_error(ret)) {
        return ret;
    }

    return h2t_freebsd_statfs(target_addr, &host_stfs);
}

/* getfsstat(2) */
static inline abi_long do_freebsd_getfsstat(abi_ulong target_addr,
        abi_long bufsize, abi_long flags)
{
    abi_long ret;
    struct statfs *host_stfs;
    int count;
    long host_bufsize;

    count = bufsize / sizeof(struct target_statfs);

    /* if user buffer is NULL then return number of mounted FS's */
    if (target_addr == 0 || count == 0) {
        return get_errno(freebsd11_getfsstat(NULL, 0, flags));
    }

    /* XXX check count to be reasonable */
    host_bufsize = sizeof(struct statfs) * count;
    host_stfs = alloca(host_bufsize);
    if (!host_stfs) {
        return -TARGET_EINVAL;
    }

    ret = count = get_errno(getfsstat(host_stfs, host_bufsize, flags));
    if (is_error(ret)) {
        return ret;
    }

    while (count--) {
        if (h2t_freebsd_statfs((target_addr +
                        (count * sizeof(struct target_statfs))),
                    &host_stfs[count])) {
            return -TARGET_EFAULT;
        }
    }
    return ret;
}

#endif /* BSD_USER_FREEBSD_OS_STAT_H */
