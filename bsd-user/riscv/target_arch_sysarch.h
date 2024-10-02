/*
 *  RISC-V sysarch() system call emulation
 *
 *  Copyright (c) 2019 Mark Corbin
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

#ifndef TARGET_ARCH_SYSARCH_H
#define TARGET_ARCH_SYSARCH_H

#include "target_syscall.h"
#include "target_arch.h"

static inline abi_long do_freebsd_arch_sysarch(CPURISCVState *env, int op,
        abi_ulong parms)
{

    return -TARGET_EOPNOTSUPP;
}

static inline void do_freebsd_arch_print_sysarch(
        const struct syscallname *name, abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6)
{

    gemu_log("UNKNOWN OP: %d, " TARGET_ABI_FMT_lx ")", (int)arg1, arg2);
}

#endif /* TARGET_ARCH_SYSARCH_H */
