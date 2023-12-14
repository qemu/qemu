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

#define LOCK_PATH(p, arg)                   \
do {                                        \
    (p) = lock_user_string(arg);            \
    if ((p) == NULL) {                      \
        return -TARGET_EFAULT;              \
    }                                       \
} while (0)

#define UNLOCK_PATH(p, arg)     unlock_user(p, arg, 0)

#define LOCK_PATH2(p1, arg1, p2, arg2)      \
do {                                        \
    (p1) = lock_user_string(arg1);          \
    if ((p1) == NULL) {                     \
        return -TARGET_EFAULT;              \
    }                                       \
    (p2) = lock_user_string(arg2);          \
    if ((p2) == NULL) {                     \
        unlock_user(p1, arg1, 0);           \
        return -TARGET_EFAULT;              \
    }                                       \
} while (0)

#define UNLOCK_PATH2(p1, arg1, p2, arg2)    \
do {                                        \
    unlock_user(p2, arg2, 0);               \
    unlock_user(p1, arg1, 0);               \
} while (0)

struct iovec *lock_iovec(int type, abi_ulong target_addr, int count, int copy);
void unlock_iovec(struct iovec *vec, abi_ulong target_addr, int count, int copy);

int safe_open(const char *path, int flags, mode_t mode);
int safe_openat(int fd, const char *path, int flags, mode_t mode);

ssize_t safe_read(int fd, void *buf, size_t nbytes);
ssize_t safe_pread(int fd, void *buf, size_t nbytes, off_t offset);
ssize_t safe_readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t safe_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);

ssize_t safe_write(int fd, void *buf, size_t nbytes);
ssize_t safe_pwrite(int fd, void *buf, size_t nbytes, off_t offset);
ssize_t safe_writev(int fd, const struct iovec *iov, int iovcnt);
ssize_t safe_pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);

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

/* write(2) */
static abi_long do_bsd_write(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long nbytes, ret;
    void *p;

    /* nbytes < 0 implies that it was larger than SIZE_MAX. */
    nbytes = arg3;
    if (nbytes < 0) {
        return -TARGET_EINVAL;
    }
    p = lock_user(VERIFY_READ, arg2, nbytes, 1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(safe_write(arg1, p, arg3));
    unlock_user(p, arg2, 0);

    return ret;
}

/* pwrite(2) */
static abi_long do_bsd_pwrite(void *cpu_env, abi_long arg1,
    abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_READ, arg2, arg3, 1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    if (regpairs_aligned(cpu_env) != 0) {
        arg4 = arg5;
        arg5 = arg6;
    }
    ret = get_errno(safe_pwrite(arg1, p, arg3, target_arg64(arg4, arg5)));
    unlock_user(p, arg2, 0);

    return ret;
}

/* writev(2) */
static abi_long do_bsd_writev(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);

    if (vec != NULL) {
        ret = get_errno(safe_writev(arg1, vec, arg3));
        unlock_iovec(vec, arg2, arg3, 0);
    } else {
        ret = -host_to_target_errno(errno);
    }

    return ret;
}

/* pwritev(2) */
static abi_long do_bsd_pwritev(void *cpu_env, abi_long arg1,
    abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_long ret;
    struct iovec *vec = lock_iovec(VERIFY_READ, arg2, arg3, 1);

    if (vec != NULL) {
        if (regpairs_aligned(cpu_env) != 0) {
            arg4 = arg5;
            arg5 = arg6;
        }
        ret = get_errno(safe_pwritev(arg1, vec, arg3, target_arg64(arg4, arg5)));
        unlock_iovec(vec, arg2, arg3, 0);
    } else {
        ret = -host_to_target_errno(errno);
    }

    return ret;
}

/* open(2) */
static abi_long do_bsd_open(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(safe_open(path(p), target_to_host_bitmask(arg2,
                fcntl_flags_tbl), arg3));
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* openat(2) */
static abi_long do_bsd_openat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(safe_openat(arg1, path(p),
                target_to_host_bitmask(arg3, fcntl_flags_tbl), arg4));
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* close(2) */
static abi_long do_bsd_close(abi_long arg1)
{
    return get_errno(close(arg1));
}

/* fdatasync(2) */
static abi_long do_bsd_fdatasync(abi_long arg1)
{
    return get_errno(fdatasync(arg1));
}

/* fsync(2) */
static abi_long do_bsd_fsync(abi_long arg1)
{
    return get_errno(fsync(arg1));
}

/* closefrom(2) */
static abi_long do_bsd_closefrom(abi_long arg1)
{
    closefrom(arg1);  /* returns void */
    return get_errno(0);
}

/* revoke(2) */
static abi_long do_bsd_revoke(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(revoke(p)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* access(2) */
static abi_long do_bsd_access(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(access(path(p), arg2));
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* eaccess(2) */
static abi_long do_bsd_eaccess(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(eaccess(path(p), arg2));
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* faccessat(2) */
static abi_long do_bsd_faccessat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(faccessat(arg1, p, arg3, arg4)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* chdir(2) */
static abi_long do_bsd_chdir(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(chdir(p)); /* XXX  path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchdir(2) */
static abi_long do_bsd_fchdir(abi_long arg1)
{
    return get_errno(fchdir(arg1));
}

/* rename(2) */
static abi_long do_bsd_rename(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg1, p2, arg2);
    ret = get_errno(rename(p1, p2)); /* XXX path(p1), path(p2) */
    UNLOCK_PATH2(p1, arg1, p2, arg2);

    return ret;
}

/* renameat(2) */
static abi_long do_bsd_renameat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg2, p2, arg4);
    ret = get_errno(renameat(arg1, p1, arg3, p2));
    UNLOCK_PATH2(p1, arg2, p2, arg4);

    return ret;
}

/* link(2) */
static abi_long do_bsd_link(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg1, p2, arg2);
    ret = get_errno(link(p1, p2)); /* XXX path(p1), path(p2) */
    UNLOCK_PATH2(p1, arg1, p2, arg2);

    return ret;
}

/* linkat(2) */
static abi_long do_bsd_linkat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg2, p2, arg4);
    ret = get_errno(linkat(arg1, p1, arg3, p2, arg5));
    UNLOCK_PATH2(p1, arg2, p2, arg4);

    return ret;
}

/* unlink(2) */
static abi_long do_bsd_unlink(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(unlink(p)); /* XXX path(p) */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* unlinkat(2) */
static abi_long do_bsd_unlinkat(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(unlinkat(arg1, p, arg3)); /* XXX path(p) */
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* mkdir(2) */
static abi_long do_bsd_mkdir(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(mkdir(p, arg2)); /* XXX path(p) */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* mkdirat(2) */
static abi_long do_bsd_mkdirat(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(mkdirat(arg1, p, arg3));
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* rmdir(2) */
static abi_long do_bsd_rmdir(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(rmdir(p)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* undocumented __getcwd(char *buf, size_t len)  system call */
static abi_long do_bsd___getcwd(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = safe_syscall(SYS___getcwd, p, arg2);
    unlock_user(p, arg1, ret == 0 ? strlen(p) + 1 : 0);

    return get_errno(ret);
}

/* dup(2) */
static abi_long do_bsd_dup(abi_long arg1)
{
    return get_errno(dup(arg1));
}

/* dup2(2) */
static abi_long do_bsd_dup2(abi_long arg1, abi_long arg2)
{
    return get_errno(dup2(arg1, arg2));
}

/* truncate(2) */
static abi_long do_bsd_truncate(void *cpu_env, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    if (regpairs_aligned(cpu_env) != 0) {
        arg2 = arg3;
        arg3 = arg4;
    }
    ret = get_errno(truncate(p, target_arg64(arg2, arg3)));
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* ftruncate(2) */
static abi_long do_bsd_ftruncate(void *cpu_env, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4)
{
    if (regpairs_aligned(cpu_env) != 0) {
        arg2 = arg3;
        arg3 = arg4;
    }
    return get_errno(ftruncate(arg1, target_arg64(arg2, arg3)));
}

/* acct(2) */
static abi_long do_bsd_acct(abi_long arg1)
{
    abi_long ret;
    void *p;

    if (arg1 == 0) {
        ret = get_errno(acct(NULL));
    } else {
        LOCK_PATH(p, arg1);
        ret = get_errno(acct(path(p)));
        UNLOCK_PATH(p, arg1);
    }
    return ret;
}

/* sync(2) */
static abi_long do_bsd_sync(void)
{
    sync();
    return 0;
}

/* mount(2) */
static abi_long do_bsd_mount(abi_long arg1, abi_long arg2, abi_long arg3,
        abi_long arg4)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg1, p2, arg2);
    /*
     * XXX arg4 should be locked, but it isn't clear how to do that since it may
     * be not be a NULL-terminated string.
     */
    if (arg4 == 0) {
        ret = get_errno(mount(p1, p2, arg3, NULL)); /* XXX path(p2)? */
    } else {
        ret = get_errno(mount(p1, p2, arg3, g2h_untagged(arg4))); /* XXX path(p2)? */
    }
    UNLOCK_PATH2(p1, arg1, p2, arg2);

    return ret;
}

/* unmount(2) */
static abi_long do_bsd_unmount(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(unmount(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* nmount(2) */
static abi_long do_bsd_nmount(abi_long arg1, abi_long count,
        abi_long flags)
{
    abi_long ret;
    struct iovec *vec = lock_iovec(VERIFY_READ, arg1, count, 1);

    if (vec != NULL) {
        ret = get_errno(nmount(vec, count, flags));
        unlock_iovec(vec, arg1, count, 0);
    } else {
        return -TARGET_EFAULT;
    }

    return ret;
}

/* symlink(2) */
static abi_long do_bsd_symlink(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg1, p2, arg2);
    ret = get_errno(symlink(p1, p2)); /* XXX path(p1), path(p2) */
    UNLOCK_PATH2(p1, arg1, p2, arg2);

    return ret;
}

/* symlinkat(2) */
static abi_long do_bsd_symlinkat(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH2(p1, arg1, p2, arg3);
    ret = get_errno(symlinkat(p1, arg2, p2)); /* XXX path(p1), path(p2) */
    UNLOCK_PATH2(p1, arg1, p2, arg3);

    return ret;
}

/* readlink(2) */
static abi_long do_bsd_readlink(CPUArchState *env, abi_long arg1,
        abi_long arg2, abi_long arg3)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH(p1, arg1);
    p2 = lock_user(VERIFY_WRITE, arg2, arg3, 0);
    if (p2 == NULL) {
        UNLOCK_PATH(p1, arg1);
        return -TARGET_EFAULT;
    }
    if (strcmp(p1, "/proc/curproc/file") == 0) {
        CPUState *cpu = env_cpu(env);
        TaskState *ts = (TaskState *)cpu->opaque;
        strncpy(p2, ts->bprm->fullpath, arg3);
        ret = MIN((abi_long)strlen(ts->bprm->fullpath), arg3);
    } else {
        ret = get_errno(readlink(path(p1), p2, arg3));
    }
    unlock_user(p2, arg2, ret);
    UNLOCK_PATH(p1, arg1);

    return ret;
}

/* readlinkat(2) */
static abi_long do_bsd_readlinkat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p1, *p2;

    LOCK_PATH(p1, arg2);
    p2 = lock_user(VERIFY_WRITE, arg3, arg4, 0);
    if (p2 == NULL) {
        UNLOCK_PATH(p1, arg2);
        return -TARGET_EFAULT;
    }
    ret = get_errno(readlinkat(arg1, p1, p2, arg4));
    unlock_user(p2, arg3, ret);
    UNLOCK_PATH(p1, arg2);

    return ret;
}

/* chmod(2) */
static abi_long do_bsd_chmod(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(chmod(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchmod(2) */
static abi_long do_bsd_fchmod(abi_long arg1, abi_long arg2)
{
    return get_errno(fchmod(arg1, arg2));
}

/* lchmod(2) */
static abi_long do_bsd_lchmod(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(lchmod(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchmodat(2) */
static abi_long do_bsd_fchmodat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(fchmodat(arg1, p, arg3, arg4));
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* pre-ino64 mknod(2) */
static abi_long do_bsd_freebsd11_mknod(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(syscall(SYS_freebsd11_mknod, p, arg2, arg3));
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* pre-ino64 mknodat(2) */
static abi_long do_bsd_freebsd11_mknodat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(syscall(SYS_freebsd11_mknodat, arg1, p, arg3, arg4));
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* post-ino64 mknodat(2) */
static abi_long do_bsd_mknodat(void *cpu_env, abi_long arg1,
        abi_long arg2, abi_long arg3, abi_long arg4, abi_long arg5,
        abi_long arg6)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
       /* 32-bit arch's use two 32 registers for 64 bit return value */
    if (regpairs_aligned(cpu_env) != 0) {
        ret = get_errno(mknodat(arg1, p, arg3, target_arg64(arg5, arg6)));
    } else {
        ret = get_errno(mknodat(arg1, p, arg3, target_arg64(arg4, arg5)));
    }
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* chown(2) */
static abi_long do_bsd_chown(abi_long arg1, abi_long arg2, abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(chown(p, arg2, arg3)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchown(2) */
static abi_long do_bsd_fchown(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    return get_errno(fchown(arg1, arg2, arg3));
}

/* lchown(2) */
static abi_long do_bsd_lchown(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(lchown(p, arg2, arg3)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchownat(2) */
static abi_long do_bsd_fchownat(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(fchownat(arg1, p, arg3, arg4, arg5)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* chflags(2) */
static abi_long do_bsd_chflags(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(chflags(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* lchflags(2) */
static abi_long do_bsd_lchflags(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(lchflags(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fchflags(2) */
static abi_long do_bsd_fchflags(abi_long arg1, abi_long arg2)
{
    return get_errno(fchflags(arg1, arg2));
}

/* chroot(2) */
static abi_long do_bsd_chroot(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(chroot(p)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* flock(2) */
static abi_long do_bsd_flock(abi_long arg1, abi_long arg2)
{
    return get_errno(flock(arg1, arg2));
}

/* mkfifo(2) */
static abi_long do_bsd_mkfifo(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(mkfifo(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* mkfifoat(2) */
static abi_long do_bsd_mkfifoat(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg2);
    ret = get_errno(mkfifoat(arg1, p, arg3));
    UNLOCK_PATH(p, arg2);

    return ret;
}

/* pathconf(2) */
static abi_long do_bsd_pathconf(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(pathconf(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* lpathconf(2) */
static abi_long do_bsd_lpathconf(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(lpathconf(p, arg2)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

/* fpathconf(2) */
static abi_long do_bsd_fpathconf(abi_long arg1, abi_long arg2)
{
    return get_errno(fpathconf(arg1, arg2));
}

/* undelete(2) */
static abi_long do_bsd_undelete(abi_long arg1)
{
    abi_long ret;
    void *p;

    LOCK_PATH(p, arg1);
    ret = get_errno(undelete(p)); /* XXX path(p)? */
    UNLOCK_PATH(p, arg1);

    return ret;
}

#endif /* BSD_FILE_H */
