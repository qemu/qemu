/*
 *  BSD process related system call helpers
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
#include "qemu/osdep.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cpuset.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "qemu.h"
#include "qemu-bsd.h"
#include "signal-common.h"

#include "bsd-proc.h"

/*
 * resource/rusage conversion
 */
int target_to_host_resource(int code)
{
    return code;
}

rlim_t target_to_host_rlim(abi_llong target_rlim)
{
    return tswap64(target_rlim);
}

abi_llong host_to_target_rlim(rlim_t rlim)
{
    return tswap64(rlim);
}

void h2g_rusage(const struct rusage *rusage,
                struct target_freebsd_rusage *target_rusage)
{
    __put_user(rusage->ru_utime.tv_sec, &target_rusage->ru_utime.tv_sec);
    __put_user(rusage->ru_utime.tv_usec, &target_rusage->ru_utime.tv_usec);

    __put_user(rusage->ru_stime.tv_sec, &target_rusage->ru_stime.tv_sec);
    __put_user(rusage->ru_stime.tv_usec, &target_rusage->ru_stime.tv_usec);

    __put_user(rusage->ru_maxrss, &target_rusage->ru_maxrss);
    __put_user(rusage->ru_idrss, &target_rusage->ru_idrss);
    __put_user(rusage->ru_idrss, &target_rusage->ru_idrss);
    __put_user(rusage->ru_isrss, &target_rusage->ru_isrss);
    __put_user(rusage->ru_minflt, &target_rusage->ru_minflt);
    __put_user(rusage->ru_majflt, &target_rusage->ru_majflt);
    __put_user(rusage->ru_nswap, &target_rusage->ru_nswap);
    __put_user(rusage->ru_inblock, &target_rusage->ru_inblock);
    __put_user(rusage->ru_oublock, &target_rusage->ru_oublock);
    __put_user(rusage->ru_msgsnd, &target_rusage->ru_msgsnd);
    __put_user(rusage->ru_msgrcv, &target_rusage->ru_msgrcv);
    __put_user(rusage->ru_nsignals, &target_rusage->ru_nsignals);
    __put_user(rusage->ru_nvcsw, &target_rusage->ru_nvcsw);
    __put_user(rusage->ru_nivcsw, &target_rusage->ru_nivcsw);
}

abi_long host_to_target_rusage(abi_ulong target_addr,
        const struct rusage *rusage)
{
    struct target_freebsd_rusage *target_rusage;

    if (!lock_user_struct(VERIFY_WRITE, target_rusage, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    h2g_rusage(rusage, target_rusage);
    unlock_user_struct(target_rusage, target_addr, 1);

    return 0;
}

abi_long host_to_target_wrusage(abi_ulong target_addr,
                                const struct __wrusage *wrusage)
{
    struct target_freebsd__wrusage *target_wrusage;

    if (!lock_user_struct(VERIFY_WRITE, target_wrusage, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    h2g_rusage(&wrusage->wru_self, &target_wrusage->wru_self);
    h2g_rusage(&wrusage->wru_children, &target_wrusage->wru_children);
    unlock_user_struct(target_wrusage, target_addr, 1);

    return 0;
}

/*
 * wait status conversion.
 *
 * Map host to target signal numbers for the wait family of syscalls.
 * Assume all other status bits are the same.
 */
int host_to_target_waitstatus(int status)
{
    if (WIFSIGNALED(status)) {
        return host_to_target_signal(WTERMSIG(status)) | (status & ~0x7f);
    }
    if (WIFSTOPPED(status)) {
        return (host_to_target_signal(WSTOPSIG(status)) << 8) | (status & 0xff);
    }
    return status;
}

int bsd_get_ncpu(void)
{
    int ncpu = -1;
    cpuset_t mask;

    CPU_ZERO(&mask);

    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(mask),
                           &mask) == 0) {
        ncpu = CPU_COUNT(&mask);
    }

    if (ncpu == -1) {
        ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    }

    if (ncpu == -1) {
        gemu_log("XXX Missing bsd_get_ncpu() implementation\n");
        ncpu = 1;
    }

    return ncpu;
}

