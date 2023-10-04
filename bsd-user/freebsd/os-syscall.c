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
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/path.h"
#include <sys/syscall.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <utime.h>

#include "include/gdbstub/syscalls.h"

#include "qemu.h"
#include "signal-common.h"
#include "user/syscall-trace.h"

/* BSD independent syscall shims */
#include "bsd-file.h"
#include "bsd-mem.h"
#include "bsd-proc.h"

/* BSD dependent syscall shims */
#include "os-stat.h"
#include "os-proc.h"
#include "os-misc.h"

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

/* used in os-proc */
safe_syscall4(pid_t, wait4, pid_t, wpid, int *, status, int, options,
    struct rusage *, rusage);
safe_syscall6(pid_t, wait6, idtype_t, idtype, id_t, id, int *, status, int,
    options, struct __wrusage *, wrusage, siginfo_t *, infop);

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
    case TARGET_FREEBSD_NR_fork: /* fork(2) */
        ret = do_freebsd_fork(cpu_env);
        break;

    case TARGET_FREEBSD_NR_vfork: /* vfork(2) */
        ret = do_freebsd_vfork(cpu_env);
        break;

    case TARGET_FREEBSD_NR_rfork: /* rfork(2) */
        ret = do_freebsd_rfork(cpu_env, arg1);
        break;

    case TARGET_FREEBSD_NR_pdfork: /* pdfork(2) */
        ret = do_freebsd_pdfork(cpu_env, arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_execve: /* execve(2) */
        ret = do_freebsd_execve(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fexecve: /* fexecve(2) */
        ret = do_freebsd_fexecve(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_wait4: /* wait4(2) */
        ret = do_freebsd_wait4(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_wait6: /* wait6(2) */
        ret = do_freebsd_wait6(cpu_env, arg1, arg2, arg3,
                               arg4, arg5, arg6, arg7, arg8);
        break;

    case TARGET_FREEBSD_NR_exit: /* exit(2) */
        ret = do_bsd_exit(cpu_env, arg1);
        break;

    case TARGET_FREEBSD_NR_getgroups: /* getgroups(2) */
        ret = do_bsd_getgroups(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setgroups: /* setgroups(2) */
        ret = do_bsd_setgroups(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_umask: /* umask(2) */
        ret = do_bsd_umask(arg1);
        break;

    case TARGET_FREEBSD_NR_setlogin: /* setlogin(2) */
        ret = do_bsd_setlogin(arg1);
        break;

    case TARGET_FREEBSD_NR_getlogin: /* getlogin(2) */
        ret = do_bsd_getlogin(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getrusage: /* getrusage(2) */
        ret = do_bsd_getrusage(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getrlimit: /* getrlimit(2) */
        ret = do_bsd_getrlimit(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setrlimit: /* setrlimit(2) */
        ret = do_bsd_setrlimit(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getpid: /* getpid(2) */
        ret = do_bsd_getpid();
        break;

    case TARGET_FREEBSD_NR_getppid: /* getppid(2) */
        ret = do_bsd_getppid();
        break;

    case TARGET_FREEBSD_NR_getuid: /* getuid(2) */
        ret = do_bsd_getuid();
        break;

    case TARGET_FREEBSD_NR_geteuid: /* geteuid(2) */
        ret = do_bsd_geteuid();
        break;

    case TARGET_FREEBSD_NR_getgid: /* getgid(2) */
        ret = do_bsd_getgid();
        break;

    case TARGET_FREEBSD_NR_getegid: /* getegid(2) */
        ret = do_bsd_getegid();
        break;

    case TARGET_FREEBSD_NR_setuid: /* setuid(2) */
        ret = do_bsd_setuid(arg1);
        break;

    case TARGET_FREEBSD_NR_seteuid: /* seteuid(2) */
        ret = do_bsd_seteuid(arg1);
        break;

    case TARGET_FREEBSD_NR_setgid: /* setgid(2) */
        ret = do_bsd_setgid(arg1);
        break;

    case TARGET_FREEBSD_NR_setegid: /* setegid(2) */
        ret = do_bsd_setegid(arg1);
        break;

    case TARGET_FREEBSD_NR_getpgrp: /* getpgrp(2) */
        ret = do_bsd_getpgrp();
        break;

    case TARGET_FREEBSD_NR_getpgid: /* getpgid(2) */
         ret = do_bsd_getpgid(arg1);
         break;

    case TARGET_FREEBSD_NR_setpgid: /* setpgid(2) */
         ret = do_bsd_setpgid(arg1, arg2);
         break;

    case TARGET_FREEBSD_NR_setreuid: /* setreuid(2) */
        ret = do_bsd_setreuid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setregid: /* setregid(2) */
        ret = do_bsd_setregid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getresuid: /* getresuid(2) */
        ret = do_bsd_getresuid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getresgid: /* getresgid(2) */
        ret = do_bsd_getresgid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_setresuid: /* setresuid(2) */
        ret = do_bsd_setresuid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_setresgid: /* setresgid(2) */
        ret = do_bsd_setresgid(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getsid: /* getsid(2) */
        ret = do_bsd_getsid(arg1);
        break;

    case TARGET_FREEBSD_NR_setsid: /* setsid(2) */
        ret = do_bsd_setsid();
        break;

    case TARGET_FREEBSD_NR_issetugid: /* issetugid(2) */
        ret = do_bsd_issetugid();
        break;

    case TARGET_FREEBSD_NR_profil: /* profil(2) */
        ret = do_bsd_profil(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ktrace: /* ktrace(2) */
        ret = do_bsd_ktrace(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_setloginclass: /* setloginclass(2) */
        ret = do_freebsd_setloginclass(arg1);
        break;

    case TARGET_FREEBSD_NR_getloginclass: /* getloginclass(2) */
        ret = do_freebsd_getloginclass(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_pdgetpid: /* pdgetpid(2) */
        ret = do_freebsd_pdgetpid(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR___setugid: /* undocumented */
        ret = do_freebsd___setugid(arg1);
        break;

    case TARGET_FREEBSD_NR_utrace: /* utrace(2) */
        ret = do_bsd_utrace(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_ptrace: /* ptrace(2) */
        ret = do_bsd_ptrace(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_getpriority: /* getpriority(2) */
        ret = do_bsd_getpriority(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setpriority: /* setpriority(2) */
        ret = do_bsd_setpriority(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_procctl: /* procctl(2) */
        ret = do_freebsd_procctl(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
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
        break;

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

    case TARGET_FREEBSD_NR_link: /* link(2) */
        ret = do_bsd_link(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_linkat: /* linkat(2) */
        ret = do_bsd_linkat(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_unlink: /* unlink(2) */
        ret = do_bsd_unlink(arg1);
        break;

    case TARGET_FREEBSD_NR_unlinkat: /* unlinkat(2) */
        ret = do_bsd_unlinkat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mkdir: /* mkdir(2) */
        ret = do_bsd_mkdir(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkdirat: /* mkdirat(2) */
        ret = do_bsd_mkdirat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_rmdir: /* rmdir(2) (XXX no rmdirat()?) */
        ret = do_bsd_rmdir(arg1);
        break;

    case TARGET_FREEBSD_NR___getcwd: /* undocumented __getcwd() */
        ret = do_bsd___getcwd(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_dup: /* dup(2) */
        ret = do_bsd_dup(arg1);
        break;

    case TARGET_FREEBSD_NR_dup2: /* dup2(2) */
        ret = do_bsd_dup2(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_truncate: /* truncate(2) */
        ret = do_bsd_truncate(cpu_env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ftruncate: /* ftruncate(2) */
        ret = do_bsd_ftruncate(cpu_env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_acct: /* acct(2) */
        ret = do_bsd_acct(arg1);
        break;

    case TARGET_FREEBSD_NR_sync: /* sync(2) */
        ret = do_bsd_sync();
        break;

    case TARGET_FREEBSD_NR_mount: /* mount(2) */
        ret = do_bsd_mount(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_unmount: /* unmount(2) */
        ret = do_bsd_unmount(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_nmount: /* nmount(2) */
        ret = do_bsd_nmount(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_symlink: /* symlink(2) */
        ret = do_bsd_symlink(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_symlinkat: /* symlinkat(2) */
        ret = do_bsd_symlinkat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_readlink: /* readlink(2) */
        ret = do_bsd_readlink(cpu_env, arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_readlinkat: /* readlinkat(2) */
        ret = do_bsd_readlinkat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_chmod: /* chmod(2) */
        ret = do_bsd_chmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchmod: /* fchmod(2) */
        ret = do_bsd_fchmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lchmod: /* lchmod(2) */
        ret = do_bsd_lchmod(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchmodat: /* fchmodat(2) */
        ret = do_bsd_fchmodat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_mknod: /* mknod(2) */
        ret = do_bsd_freebsd11_mknod(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd11_mknodat: /* mknodat(2) */
        ret = do_bsd_freebsd11_mknodat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_mknodat: /* mknodat(2) */
        ret = do_bsd_mknodat(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_chown: /* chown(2) */
        ret = do_bsd_chown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fchown: /* fchown(2) */
        ret = do_bsd_fchown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_lchown: /* lchown(2) */
        ret = do_bsd_lchown(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_fchownat: /* fchownat(2) */
        ret = do_bsd_fchownat(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_chflags: /* chflags(2) */
        ret = do_bsd_chflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lchflags: /* lchflags(2) */
        ret = do_bsd_lchflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fchflags: /* fchflags(2) */
        ret = do_bsd_fchflags(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_chroot: /* chroot(2) */
        ret = do_bsd_chroot(arg1);
        break;

    case TARGET_FREEBSD_NR_flock: /* flock(2) */
        ret = do_bsd_flock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkfifo: /* mkfifo(2) */
        ret = do_bsd_mkfifo(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mkfifoat: /* mkfifoat(2) */
        ret = do_bsd_mkfifoat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pathconf: /* pathconf(2) */
        ret = do_bsd_pathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lpathconf: /* lpathconf(2) */
        ret = do_bsd_lpathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fpathconf: /* fpathconf(2) */
        ret = do_bsd_fpathconf(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_undelete: /* undelete(2) */
        ret = do_bsd_undelete(arg1);
        break;

        /*
         * stat system calls
         */
    case TARGET_FREEBSD_NR_freebsd11_stat: /* stat(2) */
        ret = do_freebsd11_stat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_lstat: /* lstat(2) */
        ret = do_freebsd11_lstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstat: /* fstat(2) */
        ret = do_freebsd11_fstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fstat: /* fstat(2) */
        ret = do_freebsd_fstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstatat: /* fstatat(2) */
        ret = do_freebsd11_fstatat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_fstatat: /* fstatat(2) */
        ret = do_freebsd_fstatat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nstat: /* undocumented */
        ret = do_freebsd11_nstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nfstat: /* undocumented */
        ret = do_freebsd11_nfstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_nlstat: /* undocumented */
        ret = do_freebsd11_nlstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getfh: /* getfh(2) */
        ret = do_freebsd_getfh(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lgetfh: /* lgetfh(2) */
        ret = do_freebsd_lgetfh(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhopen: /* fhopen(2) */
        ret = do_freebsd_fhopen(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fhstat: /* fhstat(2) */
        ret = do_freebsd11_fhstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhstat: /* fhstat(2) */
        ret = do_freebsd_fhstat(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fhstatfs: /* fhstatfs(2) */
        ret = do_freebsd11_fhstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fhstatfs: /* fhstatfs(2) */
        ret = do_freebsd_fhstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_statfs: /* statfs(2) */
        ret = do_freebsd11_statfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_statfs: /* statfs(2) */
        ret = do_freebsd_statfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_fstatfs: /* fstatfs(2) */
        ret = do_freebsd11_fstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_fstatfs: /* fstatfs(2) */
        ret = do_freebsd_fstatfs(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getfsstat: /* getfsstat(2) */
        ret = do_freebsd11_getfsstat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getfsstat: /* getfsstat(2) */
        ret = do_freebsd_getfsstat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getdents: /* getdents(2) */
        ret = do_freebsd11_getdents(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getdirentries: /* getdirentries(2) */
        ret = do_freebsd_getdirentries(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_freebsd11_getdirentries: /* getdirentries(2) */
        ret = do_freebsd11_getdirentries(arg1, arg2, arg3, arg4);
        break;
    case TARGET_FREEBSD_NR_fcntl: /* fcntl(2) */
        ret = do_freebsd_fcntl(arg1, arg2, arg3);
        break;

        /*
         * Memory management system calls.
         */
    case TARGET_FREEBSD_NR_mmap: /* mmap(2) */
        ret = do_bsd_mmap(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                          arg8);
        break;

    case TARGET_FREEBSD_NR_munmap: /* munmap(2) */
        ret = do_bsd_munmap(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mprotect: /* mprotect(2) */
        ret = do_bsd_mprotect(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_msync: /* msync(2) */
        ret = do_bsd_msync(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mlock: /* mlock(2) */
        ret = do_bsd_mlock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_munlock: /* munlock(2) */
        ret = do_bsd_munlock(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_mlockall: /* mlockall(2) */
        ret = do_bsd_mlockall(arg1);
        break;

    case TARGET_FREEBSD_NR_munlockall: /* munlockall(2) */
        ret = do_bsd_munlockall();
        break;

    case TARGET_FREEBSD_NR_madvise: /* madvise(2) */
        ret = do_bsd_madvise(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_minherit: /* minherit(2) */
        ret = do_bsd_minherit(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_mincore: /* mincore(2) */
        ret = do_bsd_mincore(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_freebsd12_shm_open: /* shm_open(2) */
        ret = do_bsd_shm_open(arg1, arg2, arg3);
        break;

#if defined(__FreeBSD_version) && __FreeBSD_version >= 1300048
    case TARGET_FREEBSD_NR_shm_open2: /* shm_open2(2) */
        ret = do_freebsd_shm_open2(arg1, arg2, arg3, arg4, arg5);
        break;
#endif

#if defined(__FreeBSD_version) && __FreeBSD_version >= 1300049
    case TARGET_FREEBSD_NR_shm_rename: /* shm_rename(2) */
        ret = do_freebsd_shm_rename(arg1, arg2, arg3);
        break;
#endif

    case TARGET_FREEBSD_NR_shm_unlink: /* shm_unlink(2) */
        ret = do_bsd_shm_unlink(arg1);
        break;

    case TARGET_FREEBSD_NR_shmget: /* shmget(2) */
        ret = do_bsd_shmget(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmctl: /* shmctl(2) */
        ret = do_bsd_shmctl(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmat: /* shmat(2) */
        ret = do_bsd_shmat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_shmdt: /* shmdt(2) */
        ret = do_bsd_shmdt(arg1);
        break;

    case TARGET_FREEBSD_NR_freebsd11_vadvise:
        ret = do_bsd_vadvise();
        break;

    case TARGET_FREEBSD_NR_sbrk:
        ret = do_bsd_sbrk();
        break;

    case TARGET_FREEBSD_NR_sstk:
        ret = do_bsd_sstk();
        break;

        /*
         * Misc
         */
    case TARGET_FREEBSD_NR_break:
        ret = do_obreak(arg1);
        break;

        /*
         * sys{ctl, arch, call}
         */
    case TARGET_FREEBSD_NR___sysctl: /* sysctl(3) */
        ret = do_freebsd_sysctl(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR___sysctlbyname: /* sysctlbyname(2) */
        ret = do_freebsd_sysctlbyname(cpu_env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_sysarch: /* sysarch(2) */
        ret = do_freebsd_sysarch(cpu_env, arg1, arg2);
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
    abi_long ret;

    if (do_strace) {
        print_freebsd_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    ret = freebsd_syscall(cpu_env, num, arg1, arg2, arg3, arg4, arg5, arg6,
                          arg7, arg8);
    if (do_strace) {
        print_freebsd_syscall_ret(num, ret);
    }

    return ret;
}

void syscall_init(void)
{
}
