/*
 *  process related system call shims and definitions
 *
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

#ifndef BSD_PROC_H_
#define BSD_PROC_H_

#include <sys/resource.h>

#include "qemu-bsd.h"
#include "gdbstub/syscalls.h"
#include "qemu/plugin.h"

extern int _getlogin(char*, int);
int bsd_get_ncpu(void);

/* exit(2) */
static inline abi_long do_bsd_exit(void *cpu_env, abi_long arg1)
{
    gdb_exit(arg1);
    qemu_plugin_user_exit();
    _exit(arg1);

    return 0;
}

/* getgroups(2) */
static inline abi_long do_bsd_getgroups(abi_long gidsetsize, abi_long arg2)
{
    abi_long ret;
    uint32_t *target_grouplist;
    g_autofree gid_t *grouplist;
    int i;

    grouplist = g_try_new(gid_t, gidsetsize);
    ret = get_errno(getgroups(gidsetsize, grouplist));
    if (gidsetsize != 0) {
        if (!is_error(ret)) {
            target_grouplist = lock_user(VERIFY_WRITE, arg2, gidsetsize * 2, 0);
            if (!target_grouplist) {
                return -TARGET_EFAULT;
            }
            for (i = 0; i < ret; i++) {
                target_grouplist[i] = tswap32(grouplist[i]);
            }
            unlock_user(target_grouplist, arg2, gidsetsize * 2);
        }
    }
    return ret;
}

/* setgroups(2) */
static inline abi_long do_bsd_setgroups(abi_long gidsetsize, abi_long arg2)
{
    uint32_t *target_grouplist;
    g_autofree gid_t *grouplist;
    int i;

    grouplist = g_try_new(gid_t, gidsetsize);
    target_grouplist = lock_user(VERIFY_READ, arg2, gidsetsize * 2, 1);
    if (!target_grouplist) {
        return -TARGET_EFAULT;
    }
    for (i = 0; i < gidsetsize; i++) {
        grouplist[i] = tswap32(target_grouplist[i]);
    }
    unlock_user(target_grouplist, arg2, 0);
    return get_errno(setgroups(gidsetsize, grouplist));
}

/* umask(2) */
static inline abi_long do_bsd_umask(abi_long arg1)
{
    return get_errno(umask(arg1));
}

/* setlogin(2) */
static inline abi_long do_bsd_setlogin(abi_long arg1)
{
    abi_long ret;
    void *p;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(setlogin(p));
    unlock_user(p, arg1, 0);

    return ret;
}

/* getlogin(2) */
static inline abi_long do_bsd_getlogin(abi_long arg1, abi_long arg2)
{
    abi_long ret;
    void *p;

    p = lock_user(VERIFY_WRITE, arg1, arg2, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(_getlogin(p, arg2));
    unlock_user(p, arg1, arg2);

    return ret;
}

/* getrusage(2) */
static inline abi_long do_bsd_getrusage(abi_long who, abi_ulong target_addr)
{
    abi_long ret;
    struct rusage rusage;

    ret = get_errno(getrusage(who, &rusage));
    if (!is_error(ret)) {
        host_to_target_rusage(target_addr, &rusage);
    }
    return ret;
}

/* getrlimit(2) */
static inline abi_long do_bsd_getrlimit(abi_long arg1, abi_ulong arg2)
{
    abi_long ret;
    int resource = target_to_host_resource(arg1);
    struct target_rlimit *target_rlim;
    struct rlimit rlim;

    switch (resource) {
    case RLIMIT_STACK:
        rlim.rlim_cur = target_dflssiz;
        rlim.rlim_max = target_maxssiz;
        ret = 0;
        break;

    case RLIMIT_DATA:
        rlim.rlim_cur = target_dfldsiz;
        rlim.rlim_max = target_maxdsiz;
        ret = 0;
        break;

    default:
        ret = get_errno(getrlimit(resource, &rlim));
        break;
    }
    if (!is_error(ret)) {
        if (!lock_user_struct(VERIFY_WRITE, target_rlim, arg2, 0)) {
            return -TARGET_EFAULT;
        }
        target_rlim->rlim_cur = host_to_target_rlim(rlim.rlim_cur);
        target_rlim->rlim_max = host_to_target_rlim(rlim.rlim_max);
        unlock_user_struct(target_rlim, arg2, 1);
    }
    return ret;
}

/* setrlimit(2) */
static inline abi_long do_bsd_setrlimit(abi_long arg1, abi_ulong arg2)
{
    abi_long ret;
    int resource = target_to_host_resource(arg1);
    struct target_rlimit *target_rlim;
    struct rlimit rlim;

    if (RLIMIT_STACK == resource) {
        /* XXX We should, maybe, allow the stack size to shrink */
        ret = -TARGET_EPERM;
    } else {
        if (!lock_user_struct(VERIFY_READ, target_rlim, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        rlim.rlim_cur = target_to_host_rlim(target_rlim->rlim_cur);
        rlim.rlim_max = target_to_host_rlim(target_rlim->rlim_max);
        unlock_user_struct(target_rlim, arg2, 0);
        ret = get_errno(setrlimit(resource, &rlim));
    }
    return ret;
}

/* getpid(2) */
static inline abi_long do_bsd_getpid(void)
{
    return get_errno(getpid());
}

/* getppid(2) */
static inline abi_long do_bsd_getppid(void)
{
    return get_errno(getppid());
}

/* getuid(2) */
static inline abi_long do_bsd_getuid(void)
{
    return get_errno(getuid());
}

/* geteuid(2) */
static inline abi_long do_bsd_geteuid(void)
{
    return get_errno(geteuid());
}

/* getgid(2) */
static inline abi_long do_bsd_getgid(void)
{
    return get_errno(getgid());
}

/* getegid(2) */
static inline abi_long do_bsd_getegid(void)
{
    return get_errno(getegid());
}

/* setuid(2) */
static inline abi_long do_bsd_setuid(abi_long arg1)
{
    return get_errno(setuid(arg1));
}

/* seteuid(2) */
static inline abi_long do_bsd_seteuid(abi_long arg1)
{
    return get_errno(seteuid(arg1));
}

/* setgid(2) */
static inline abi_long do_bsd_setgid(abi_long arg1)
{
    return get_errno(setgid(arg1));
}

/* setegid(2) */
static inline abi_long do_bsd_setegid(abi_long arg1)
{
    return get_errno(setegid(arg1));
}

/* getpgid(2) */
static inline abi_long do_bsd_getpgid(pid_t pid)
{
    return get_errno(getpgid(pid));
}

/* setpgid(2) */
static inline abi_long do_bsd_setpgid(int pid, int pgrp)
{
    return get_errno(setpgid(pid, pgrp));
}

/* getpgrp(2) */
static inline abi_long do_bsd_getpgrp(void)
{
    return get_errno(getpgrp());
}

/* setreuid(2) */
static inline abi_long do_bsd_setreuid(abi_long arg1, abi_long arg2)
{
    return get_errno(setreuid(arg1, arg2));
}

/* setregid(2) */
static inline abi_long do_bsd_setregid(abi_long arg1, abi_long arg2)
{
    return get_errno(setregid(arg1, arg2));
}

/* setresgid(2) */
static inline abi_long do_bsd_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    return get_errno(setresgid(rgid, egid, sgid));
}

/* setresuid(2) */
static inline abi_long do_bsd_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    return get_errno(setresuid(ruid, euid, suid));
}

/* getresuid(2) */
static inline abi_long do_bsd_getresuid(abi_ulong arg1, abi_ulong arg2,
        abi_ulong arg3)
{
    abi_long ret;
    uid_t ruid, euid, suid;

    ret = get_errno(getresuid(&ruid, &euid, &suid));
    if (is_error(ret)) {
            return ret;
    }
    if (put_user_s32(ruid, arg1)) {
        return -TARGET_EFAULT;
    }
    if (put_user_s32(euid, arg2)) {
        return -TARGET_EFAULT;
    }
    if (put_user_s32(suid, arg3)) {
        return -TARGET_EFAULT;
    }
    return ret;
}

/* getresgid(2) */
static inline abi_long do_bsd_getresgid(abi_ulong arg1, abi_ulong arg2,
                                        abi_ulong arg3)
{
    abi_long ret;
    uid_t ruid, euid, suid;

    ret = get_errno(getresgid(&ruid, &euid, &suid));
    if (is_error(ret)) {
            return ret;
    }
    if (put_user_s32(ruid, arg1)) {
        return -TARGET_EFAULT;
    }
    if (put_user_s32(euid, arg2)) {
        return -TARGET_EFAULT;
    }
    if (put_user_s32(suid, arg3)) {
        return -TARGET_EFAULT;
    }
    return ret;
}

/* getsid(2) */
static inline abi_long do_bsd_getsid(abi_long arg1)
{
    return get_errno(getsid(arg1));
}

/* setsid(2) */
static inline abi_long do_bsd_setsid(void)
{
    return get_errno(setsid());
}

/* issetugid(2) */
static inline abi_long do_bsd_issetugid(void)
{
    return get_errno(issetugid());
}

/* profil(2) */
static inline abi_long do_bsd_profil(abi_long arg1, abi_long arg2,
                                     abi_long arg3, abi_long arg4)
{
    return -TARGET_ENOSYS;
}

/* ktrace(2) */
static inline abi_long do_bsd_ktrace(abi_long arg1, abi_long arg2,
                                     abi_long arg3, abi_long arg4)
{
    return -TARGET_ENOSYS;
}

/* utrace(2) */
static inline abi_long do_bsd_utrace(abi_long arg1, abi_long arg2)
{
    return -TARGET_ENOSYS;
}


/* ptrace(2) */
static inline abi_long do_bsd_ptrace(abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4)
{
    return -TARGET_ENOSYS;
}

/* getpriority(2) */
static inline abi_long do_bsd_getpriority(abi_long which, abi_long who)
{
    abi_long ret;
    /*
     * Note that negative values are valid for getpriority, so we must
     * differentiate based on errno settings.
     */
    errno = 0;
    ret = getpriority(which, who);
    if (ret == -1 && errno != 0) {
        return -host_to_target_errno(errno);
    }

    return ret;
}

/* setpriority(2) */
static inline abi_long do_bsd_setpriority(abi_long which, abi_long who,
                                          abi_long prio)
{
    return get_errno(setpriority(which, who, prio));
}

#endif /* !BSD_PROC_H_ */
