/*
 *  x86_64 sysarch() syscall emulation
 *
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

static inline abi_long do_freebsd_arch_sysarch(CPUX86State *env, int op,
        abi_ulong parms)
{
    abi_long ret = 0;
    abi_ulong val;
    int idx;

    switch (op) {
    case TARGET_FREEBSD_AMD64_SET_GSBASE:
    case TARGET_FREEBSD_AMD64_SET_FSBASE:
        if (op == TARGET_FREEBSD_AMD64_SET_GSBASE) {
            idx = R_GS;
        } else {
            idx = R_FS;
        }
        if (get_user(val, parms, abi_ulong)) {
            return -TARGET_EFAULT;
        }
        cpu_x86_load_seg(env, idx, 0);
        env->segs[idx].base = val;
        break;

    case TARGET_FREEBSD_AMD64_GET_GSBASE:
    case TARGET_FREEBSD_AMD64_GET_FSBASE:
        if (op == TARGET_FREEBSD_AMD64_GET_GSBASE) {
            idx = R_GS;
        } else {
            idx = R_FS;
        }
        val = env->segs[idx].base;
        if (put_user(val, parms, abi_ulong)) {
            return -TARGET_EFAULT;
        }
        break;

    /* XXX handle the others... */
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

#endif /*! BSD_USER_ARCH_SYSARCH_H_ */
