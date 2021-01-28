/*
 *  SPARC sysarch() system call emulation
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

#ifndef BSD_USER_ARCH_SYSARCH_H_
#define BSD_USER_ARCH_SYSARCH_H_

#include "target_syscall.h"

static inline abi_long do_freebsd_arch_sysarch(void *env, int op,
        abi_ulong parms)
{
    int ret = 0;

    switch (op) {
    case TARGET_SPARC_SIGTRAMP_INSTALL:
        /* XXX not currently handled */
    case TARGET_SPARC_UTRAP_INSTALL:
        /* XXX not currently handled */
    default:
        ret = -TARGET_EINVAL;
        break;
    }

    return ret;
}

static inline void do_freebsd_arch_print_sysarch(
        const struct syscallname *name, abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{

    gemu_log("%s(%d, " TARGET_ABI_FMT_lx ", " TARGET_ABI_FMT_lx ", "
        TARGET_ABI_FMT_lx ")", name->name, (int)arg1, arg2, arg3, arg4);
}

#endif /*!BSD_USER_ARCH_SYSARCH_H_ */
