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
#include <poll.h>

#include "include/gdbstub/syscalls.h"

#include "qemu.h"
#include "signal-common.h"

/* BSD independent syscall shims */
#include "bsd-file.h"
#include "bsd-ioctl.h"
#include "bsd-mem.h"
#include "bsd-proc.h"
#include "bsd-misc.h"
#include "bsd-signal.h"
#include "bsd-socket.h"

/* BSD dependent syscall shims */
#include "os-stat.h"
#include "os-proc.h"
#include "os-signal.h"
#include "os-file.h"
#include "os-socket.h"
#include "os-time.h"
#include "os-thread.h"
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

safe_syscall4(int, ppoll, struct pollfd *, fds, nfds_t, nfds,
    const struct timespec *, restrict_timeout, const sigset_t *,
    restrict_newsigmask);
safe_syscall6(ssize_t, copy_file_range, int, infd, off_t *, inoffp, int, outfd,
    off_t *, outoffp, size_t, len, unsigned int, flags);

/* used in bsd-socket */
safe_syscall5(int, select, int, nfds, fd_set *, readfs, fd_set *, writefds,
    fd_set *, exceptfds, struct timeval *, timeout);
safe_syscall6(int, pselect, int, nfds, fd_set *restrict, readfs,
    fd_set *restrict, writefds, fd_set *restrict, exceptfds,
    const struct timespec *restrict, timeout, const sigset_t *restrict,
    newsigmask);
safe_syscall6(ssize_t, recvfrom, int, fd, void *, buf, size_t, len, int, flags,
    struct sockaddr *restrict, from, socklen_t *restrict, fromlen);
safe_syscall6(ssize_t, sendto, int, fd, const void *, buf, size_t, len, int,
    flags, const struct sockaddr *, to, socklen_t, tolen);
safe_syscall3(ssize_t, recvmsg, int, s, struct msghdr *, msg, int, flags);
safe_syscall3(ssize_t, sendmsg, int, s, const struct msghdr *, msg, int, flags);

/* used in os-thread */
safe_syscall1(int, thr_suspend, struct timespec *, timeout);
safe_syscall5(int, _umtx_op, void *, obj, int, op, unsigned long, val, void *,
    uaddr, void *, uaddr2);

/* used in os-time */
safe_syscall2(int, nanosleep, const struct timespec *, rqtp, struct timespec *,
    rmtp);
safe_syscall4(int, clock_nanosleep, clockid_t, clock_id, int, flags,
    const struct timespec *, rqtp, struct timespec *, rmtp);
safe_syscall6(int, kevent, int, kq, const struct kevent *, changelist,
    int, nchanges, struct kevent *, eventlist, int, nevents,
    const struct timespec *, timeout);

int g_posix_timers[32] = { 0, } ;

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
static abi_long freebsd_syscall(CPUArchState *env, int num, abi_long arg1,
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
        ret = do_freebsd_fork(env);
        break;

    case TARGET_FREEBSD_NR_vfork: /* vfork(2) */
        ret = do_freebsd_vfork(env);
        break;

    case TARGET_FREEBSD_NR_rfork: /* rfork(2) */
        ret = do_freebsd_rfork(env, arg1);
        break;

    case TARGET_FREEBSD_NR_pdfork: /* pdfork(2) */
        ret = do_freebsd_pdfork(env, arg1, arg2);
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
        ret = do_freebsd_wait6(env, arg1, arg2, arg3,
                               arg4, arg5, arg6, arg7, arg8);
        break;

    case TARGET_FREEBSD_NR_exit: /* exit(2) */
        ret = do_bsd_exit(env, arg1);
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
        ret = do_freebsd_procctl(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

        /*
         * File system calls.
         */
    case TARGET_FREEBSD_NR_read: /* read(2) */
        ret = do_bsd_read(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pread: /* pread(2) */
        ret = do_bsd_pread(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_readv: /* readv(2) */
        ret = do_bsd_readv(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_preadv: /* preadv(2) */
        ret = do_bsd_preadv(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_write: /* write(2) */
        ret = do_bsd_write(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwrite: /* pwrite(2) */
        ret = do_bsd_pwrite(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_writev: /* writev(2) */
        ret = do_bsd_writev(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_pwritev: /* pwritev(2) */
        ret = do_bsd_pwritev(env, arg1, arg2, arg3, arg4, arg5, arg6);
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
        ret = do_bsd_truncate(env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ftruncate: /* ftruncate(2) */
        ret = do_bsd_ftruncate(env, arg1, arg2, arg3, arg4);
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
        ret = do_bsd_readlink(env, arg1, arg2, arg3);
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
        ret = do_bsd_mknodat(env, arg1, arg2, arg3, arg4, arg5, arg6);
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

    case TARGET_FREEBSD_NR_poll: /* poll(2) */
        ret = do_bsd_poll(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_lseek: /* lseek(2) */
        ret = do_bsd_lseek(env, arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_freebsd10_pipe: /* pipe(2) */
        ret = do_bsd_pipe(env, arg1);
        break;

    case TARGET_FREEBSD_NR_pipe2: /* pipe2(2) */
        ret = do_bsd_pipe2(env, arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_swapon: /* swapon(2) */
        ret = do_bsd_swapon(arg1);
        break;

#if TARGET_FREEBSD_NR_freebsd13_swapoff
    case TARGET_FREEBSD_NR_freebsd13_swapoff: /* freebsd13_swapoff(2) */
        ret = do_freebsd13_swapoff(arg1);
        break;
#endif

    case TARGET_FREEBSD_NR_swapoff: /* swapoff(2) */
        ret = do_bsd_swapoff(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_chflagsat: /* chflagsat(2) */
        ret = do_bsd_chflagsat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_close_range: /* close_range(2) */
        ret = do_freebsd_close_range(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR___realpathat:
        /* __realpathat(2) (XXX no realpathat()) */
        ret = do_freebsd_realpathat(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_copy_file_range:
        ret = do_freebsd_copy_file_range(arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR___specialfd:
        ret = do_freebsd___specialfd(arg1, arg2, arg3);
        break;

        /*
         * ioctl(2)
         */
    case TARGET_FREEBSD_NR_ioctl: /* ioctl(2) */
        ret = do_bsd_ioctl(arg1, arg2, arg3);
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
        ret = do_bsd_mmap(env, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
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

        /*
         * System V Semaphores
         */
    case TARGET_FREEBSD_NR_semget: /* semget(2) */
        ret = do_bsd_semget(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_semop: /* semop(2) */
        ret = do_bsd_semop(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR___semctl: { /* __semctl() undocumented */
        ret = do_bsd___semctl(arg1, arg2, arg3, arg4);
        break;
    }

        /*
         * System V Messages
         */
    case TARGET_FREEBSD_NR_msgctl: /* msgctl(2) */
        ret = do_bsd_msgctl(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_msgget: /* msgget(2) */
        ret = do_bsd_msgget(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_msgsnd: /* msgsnd(2) */
        ret = do_bsd_msgsnd(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_msgrcv: /* msgrcv(2) */
        ret = do_bsd_msgrcv(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_freebsd11_vadvise:
        ret = do_bsd_vadvise();
        break;

        /*
         * Misc
         */
    case TARGET_FREEBSD_NR_break:
        ret = do_obreak(arg1);
        break;

    case TARGET_FREEBSD_NR_quotactl: /* quotactl(2) */
        ret = do_bsd_quotactl(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_reboot: /* reboot(2) */
        ret = do_bsd_reboot(arg1);
        break;

    case TARGET_FREEBSD_NR_uuidgen: /* uuidgen(2) */
        ret = do_bsd_uuidgen(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_getdtablesize: /* getdtablesize(2) */
        ret = do_bsd_getdtablesize();
        break;

        /*
         * time related system calls.
         */
    case TARGET_FREEBSD_NR_nanosleep: /* nanosleep(2) */
        ret = do_freebsd_nanosleep(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_clock_nanosleep: /* clock_nanosleep(2) */
        ret = do_freebsd_clock_nanosleep(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_clock_gettime: /* clock_gettime(2) */
        ret = do_freebsd_clock_gettime(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_clock_settime: /* clock_settime(2) */
        ret = do_freebsd_clock_settime(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_clock_getres: /* clock_getres(2) */
        ret = do_freebsd_clock_getres(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_gettimeofday: /* gettimeofday(2) */
        ret = do_freebsd_gettimeofday(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_settimeofday: /* settimeofday(2) */
        ret = do_freebsd_settimeofday(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_adjtime: /* adjtime(2) */
        ret = do_freebsd_adjtime(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_ntp_adjtime: /* ntp_adjtime(2) */
        ret = do_freebsd_ntp_adjtime(arg1);
        break;

    case TARGET_FREEBSD_NR_clock_getcpuclockid2: /* Not documented. */
        ret = do_freebsd_clock_getcpuclockid2(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ntp_gettime: /* ntp_gettime(2) */
        ret = do_freebsd_ntp_gettime(arg1);
        break;

    case TARGET_FREEBSD_NR_utimes: /* utimes(2) */
        ret = do_freebsd_utimes(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_lutimes: /* lutimes(2) */
        ret = do_freebsd_lutimes(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_futimes: /* futimes(2) */
        ret = do_freebsd_futimes(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_futimesat: /* futimesat(2) */
        ret = do_freebsd_futimesat(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_ktimer_create: /* timer_create(2) */
        ret = do_freebsd_ktimer_create(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_ktimer_delete: /* timer_delete(2) */
        ret = do_freebsd_ktimer_delete(arg1);
        break;

    case TARGET_FREEBSD_NR_ktimer_settime: /* timer_settime(2) */
        ret = do_freebsd_ktimer_settime(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_ktimer_gettime: /* timer_gettime(2) */
        ret = do_freebsd_ktimer_gettime(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_select: /* select(2) */
        ret = do_freebsd_select(env, arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_pselect: /* pselect(2) */
        ret = do_freebsd_pselect(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_ppoll: /* ppoll(2) */
        ret = do_freebsd_ppoll(env, arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_kqueue: /* kqueue(2) */
        ret = do_freebsd_kqueue();
        break;

    case TARGET_FREEBSD_NR_freebsd11_kevent: /* kevent(2) */
        ret = do_freebsd_freebsd11_kevent(arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_kevent: /* kevent(2) */
        ret = do_freebsd_kevent(arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_setitimer: /* setitimer(2) */
        ret = do_freebsd_setitimer(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getitimer: /* getitimer(2) */
        ret = do_freebsd_getitimer(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_futimens: /* futimens(2) */
        ret = do_freebsd_futimens(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_utimensat: /* utimensat(2) */
        ret = do_freebsd_utimensat(arg1, arg2, arg3, arg4);
        break;

        /*
         * signal system calls
         */
    case TARGET_FREEBSD_NR_sigtimedwait: /* sigtimedwait(2) */
        ret = do_freebsd_sigtimedwait(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_sigaction: /* sigaction(2) */
        ret = do_bsd_sigaction(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_sigprocmask: /* sigprocmask(2) */
        ret = do_bsd_sigprocmask(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_sigpending: /* sigpending(2) */
        ret = do_bsd_sigpending(arg1);
        break;

    case TARGET_FREEBSD_NR_sigsuspend: /* sigsuspend(2) */
        ret = do_bsd_sigsuspend(env, arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_sigreturn: /* sigreturn(2) */
        ret = do_bsd_sigreturn(env, arg1);
        break;

    case TARGET_FREEBSD_NR_sigwait: /* sigwait(2) */
        ret = do_bsd_sigwait(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_sigwaitinfo: /* sigwaitinfo(2) */
        ret = do_bsd_sigwaitinfo(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_sigqueue: /* sigqueue(2) */
        ret = do_bsd_sigqueue(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_sigaltstack: /* sigaltstack(2) */
        ret = do_bsd_sigaltstack(env, arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_kill: /* kill(2) */
        ret = do_bsd_kill(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_pdkill: /* pdkill(2) */
        ret = do_freebsd_pdkill(arg1, arg2);
        break;

        /*
         * socket related system calls
         */
    case TARGET_FREEBSD_NR_accept: /* accept(2) */
        ret = do_bsd_accept(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_accept4: /* accept4(2) */
        ret = do_freebsd_accept4(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_bind: /* bind(2) */
        ret = do_bsd_bind(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_bindat: /* bindat(2) */
        ret = do_freebsd_bindat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_connect: /* connect(2) */
        ret = do_bsd_connect(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_connectat: /* connectat(2) */
        ret = do_freebsd_connectat(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_getpeername: /* getpeername(2) */
        ret = do_bsd_getpeername(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getsockname: /* getsockname(2) */
        ret = do_bsd_getsockname(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_getsockopt: /* getsockopt(2) */
        ret = do_bsd_getsockopt(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_setsockopt: /* setsockopt(2) */
        ret = do_bsd_setsockopt(arg1, arg2, arg3, arg4, arg5);
        break;

    case TARGET_FREEBSD_NR_listen: /* listen(2) */
        ret = get_errno(listen(arg1, arg2));
        break;

    case TARGET_FREEBSD_NR_recvfrom: /* recvfrom(2) */
        ret = do_bsd_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_recvmsg: /* recvmsg(2) */
        ret = do_sendrecvmsg(arg1, arg2, arg3, 0);
        break;

    case TARGET_FREEBSD_NR_sendmsg: /* sendmsg(2) */
        ret = do_sendrecvmsg(arg1, arg2, arg3, 1);
        break;

    case TARGET_FREEBSD_NR_sendto: /* sendto(2) */
        ret = do_bsd_sendto(arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_socket: /* socket(2) */
        ret = do_bsd_socket(arg1, arg2, arg3);
        break;

    case TARGET_FREEBSD_NR_socketpair: /* socketpair(2) */
        ret = do_bsd_socketpair(arg1, arg2, arg3, arg4);
        break;

    case TARGET_FREEBSD_NR_shutdown: /* shutdown(2) */
        ret = do_bsd_shutdown(arg1, arg2);
        break;

    case TARGET_FREEBSD_NR_setfib: /* setfib(2) */
        ret = do_freebsd_setfib(arg1);
        break;

        /*
         * sys{ctl, arch, call}
         */
    case TARGET_FREEBSD_NR___sysctl: /* sysctl(3) */
        ret = do_freebsd_sysctl(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR___sysctlbyname: /* sysctlbyname(2) */
        ret = do_freebsd_sysctlbyname(env, arg1, arg2, arg3, arg4, arg5, arg6);
        break;

    case TARGET_FREEBSD_NR_sysarch: /* sysarch(2) */
        ret = do_freebsd_sysarch(env, arg1, arg2);
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
abi_long do_freebsd_syscall(CPUArchState *env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8)
{
    abi_long ret;

    if (do_strace) {
        print_freebsd_syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    ret = freebsd_syscall(env, num, arg1, arg2, arg3, arg4, arg5, arg6,
                          arg7, arg8);
    if (do_strace) {
        print_freebsd_syscall_ret(num, ret);
    }

    return ret;
}

void syscall_init(void)
{
    init_bsd_ioctl();
}
