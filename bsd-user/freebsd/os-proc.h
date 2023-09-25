/*
 *  process related system call shims and definitions
 *
 *  Copyright (c) 2013-14 Stacey D. Son
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

#ifndef BSD_USER_FREEBSD_OS_PROC_H
#define BSD_USER_FREEBSD_OS_PROC_H

#include <sys/param.h>
#include <sys/procctl.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/procdesc.h>
#include <sys/wait.h>
#include <unistd.h>

#include "target_arch_cpu.h"

pid_t safe_wait4(pid_t wpid, int *status, int options, struct rusage *rusage);
pid_t safe_wait6(idtype_t idtype, id_t id, int *status, int options,
    struct __wrusage *wrusage, siginfo_t *infop);

/* execve(2) */
static inline abi_long do_freebsd_execve(abi_ulong path_or_fd, abi_ulong argp,
        abi_ulong envp)
{

    return freebsd_exec_common(path_or_fd, argp, envp, 0);
}

/* fexecve(2) */
static inline abi_long do_freebsd_fexecve(abi_ulong path_or_fd, abi_ulong argp,
        abi_ulong envp)
{

    return freebsd_exec_common(path_or_fd, argp, envp, 1);
}

/* wait4(2) */
static inline abi_long do_freebsd_wait4(abi_long arg1, abi_ulong target_status,
        abi_long arg3, abi_ulong target_rusage)
{
    abi_long ret;
    int status;
    struct rusage rusage, *rusage_ptr = NULL;

    if (target_rusage) {
        rusage_ptr = &rusage;
    }
    ret = get_errno(safe_wait4(arg1, &status, arg3, rusage_ptr));

    if (ret < 0) {
        return ret;
    }
    if (target_status != 0) {
        status = host_to_target_waitstatus(status);
        if (put_user_s32(status, target_status) != 0) {
            return -TARGET_EFAULT;
        }
    }
    if (target_rusage != 0) {
        host_to_target_rusage(target_rusage, &rusage);
    }
    return ret;
}

/* wait6(2) */
static inline abi_long do_freebsd_wait6(void *cpu_env, abi_long idtype,
    abi_long id1, abi_long id2,
    abi_ulong target_status, abi_long options, abi_ulong target_wrusage,
    abi_ulong target_infop, abi_ulong pad1)
{
    abi_long ret;
    int status;
    struct __wrusage wrusage, *wrusage_ptr = NULL;
    siginfo_t info;
    void *p;

    if (regpairs_aligned(cpu_env) != 0) {
        /* printf("shifting args\n"); */
        /* 64-bit id is aligned, so shift all the arguments over by one */
        id1 = id2;
        id2 = target_status;
        target_status = options;
        options = target_wrusage;
        target_wrusage = target_infop;
        target_infop = pad1;
    }

    if (target_wrusage) {
        wrusage_ptr = &wrusage;
    }
    ret = get_errno(safe_wait6(idtype, target_arg64(id1, id2),
                               &status, options, wrusage_ptr, &info));

    if (ret < 0) {
        return ret;
    }
    if (target_status != 0) {
        status = host_to_target_waitstatus(status);
        if (put_user_s32(status, target_status) != 0) {
            return -TARGET_EFAULT;
        }
    }
    if (target_wrusage != 0) {
        host_to_target_wrusage(target_wrusage, &wrusage);
    }
    if (target_infop != 0) {
        p = lock_user(VERIFY_WRITE, target_infop, sizeof(target_siginfo_t), 0);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        host_to_target_siginfo(p, &info);
        unlock_user(p, target_infop, sizeof(target_siginfo_t));
    }
    return ret;
}

/* setloginclass(2) */
static inline abi_long do_freebsd_setloginclass(abi_ulong arg1)
{
    abi_long ret;
    void *p;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(setloginclass(p));
    unlock_user(p, arg1, 0);

    return ret;
}

/* getloginclass(2) */
static inline abi_long do_freebsd_getloginclass(abi_ulong arg1, abi_ulong arg2)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(getloginclass(p, arg2));
    unlock_user(p, arg1, arg2);

    return ret;
}

#endif /* BSD_USER_FREEBSD_OS_PROC_H */
