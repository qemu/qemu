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

int bsd_get_ncpu(void);

/* exit(2) */
static inline abi_long do_bsd_exit(void *cpu_env, abi_long arg1)
{
#ifdef TARGET_GPROF
    _mcleanup();
#endif
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

#endif /* !BSD_PROC_H_ */
