/*
 * ARM AArch64 sysarch() system call emulation for bsd-user.
 *
 * Copyright (c) 2015 <sson at FreeBSD>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_SYSARCH_H
#define TARGET_ARCH_SYSARCH_H

#include "target_syscall.h"
#include "target_arch.h"

/* See sysarch() in sys/arm64/arm64/sys_machdep.c */
static inline abi_long do_freebsd_arch_sysarch(CPUARMState *env, int op,
        abi_ulong parms)
{
    int ret = -TARGET_EOPNOTSUPP;

    fprintf(stderr, "sysarch");
    return ret;
}

static inline void do_freebsd_arch_print_sysarch(
        const struct syscallname *name, abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{
}

#endif /* TARGET_ARCH_SYSARCH_H */
