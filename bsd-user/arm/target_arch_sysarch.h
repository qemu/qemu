/*
 *  arm sysarch() system call emulation
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

#ifndef _TARGET_ARCH_SYSARCH_H_
#define _TARGET_ARCH_SYSARCH_H_

#include "target_syscall.h"
#include "target_arch.h"

static inline abi_long do_freebsd_arch_sysarch(CPUARMState *env, int op,
        abi_ulong parms)
{
    int ret = 0;

    switch (op) {
    case TARGET_FREEBSD_ARM_SYNC_ICACHE:
    case TARGET_FREEBSD_ARM_DRAIN_WRITEBUF:
        break;

    case TARGET_FREEBSD_ARM_SET_TP:
        target_cpu_set_tls(env, parms);
        break;

    case TARGET_FREEBSD_ARM_GET_TP:
        ret = target_cpu_get_tls(env);
        break;

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

    switch (arg1) {
    case TARGET_FREEBSD_ARM_SYNC_ICACHE:
        gemu_log("%s(ARM_SYNC_ICACHE, ...)", name->name);
        break;

    case TARGET_FREEBSD_ARM_DRAIN_WRITEBUF:
        gemu_log("%s(ARM_DRAIN_WRITEBUF, ...)", name->name);
        break;

    case TARGET_FREEBSD_ARM_SET_TP:
        gemu_log("%s(ARM_SET_TP, 0x" TARGET_ABI_FMT_lx ")", name->name, arg2);
        break;

    case TARGET_FREEBSD_ARM_GET_TP:
        gemu_log("%s(ARM_GET_TP, 0x" TARGET_ABI_FMT_lx ")", name->name, arg2);
        break;

    default:
        gemu_log("UNKNOWN OP: %d, " TARGET_ABI_FMT_lx ")", (int)arg1, arg2);
    }
}

#endif /*!_TARGET_ARCH_SYSARCH_H_ */
