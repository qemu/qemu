/*
 *  FreeBSD setup_initial_stack() implementation.
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

#ifndef TARGET_OS_STACK_H
#define TARGET_OS_STACK_H

#include <sys/param.h>
#include "target_arch_sigtramp.h"
#include "qemu/guest-random.h"
#include "user/tswap-target.h"

/*
 * The initial FreeBSD stack is as follows:
 * (see kern/kern_exec.c exec_copyout_strings() )
 *
 *  Hi Address -> char **ps_argvstr  (struct ps_strings for ps, w, etc.)
 *                unsigned ps_nargvstr
 *                char **ps_envstr
 *  PS_STRINGS -> unsigned ps_nenvstr
 *
 *                machine dependent sigcode (sv_sigcode of size
 *                                           sv_szsigcode)
 *
 *                execpath          (absolute image path for rtld)
 *
 *                SSP Canary        (sizeof(long) * 8)
 *
 *                page sizes array  (usually sizeof(u_long) )
 *
 *  "destp" ->    argv, env strings (up to 262144 bytes)
 */
static inline int setup_initial_stack(struct bsd_binprm *bprm,
        abi_ulong *ret_addr, abi_ulong *stringp)
{
    int i;
    abi_ulong stack_hi_addr;
    size_t execpath_len, stringspace;
    abi_ulong destp, argvp, envp, p;
    struct target_ps_strings ps_strs;
    char canary[sizeof(abi_long) * 8];

    stack_hi_addr = p = target_stkbas + target_stksiz;

    /* Save some space for ps_strings. */
    p -= sizeof(struct target_ps_strings);

    /* Add machine dependent sigcode. */
    p -= TARGET_SZSIGCODE;
    if (setup_sigtramp(p, (unsigned)offsetof(struct target_sigframe, sf_uc),
            TARGET_FREEBSD_NR_sigreturn)) {
        errno = EFAULT;
        return -1;
    }
    if (bprm->fullpath) {
        execpath_len = strlen(bprm->fullpath) + 1;
        p -= roundup(execpath_len, sizeof(abi_ulong));
        if (memcpy_to_target(p, bprm->fullpath, execpath_len)) {
            errno = EFAULT;
            return -1;
        }
    }
    /* Add canary for SSP. */
    qemu_guest_getrandom_nofail(canary, sizeof(canary));
    p -= roundup(sizeof(canary), sizeof(abi_ulong));
    if (memcpy_to_target(p, canary, sizeof(canary))) {
        errno = EFAULT;
        return -1;
    }
    /* Add page sizes array. */
    p -= sizeof(abi_ulong);
    if (put_user_ual(TARGET_PAGE_SIZE, p)) {
        errno = EFAULT;
        return -1;
    }
    /*
     * Deviate from FreeBSD stack layout: force stack to new page here
     * so that signal trampoline is not sharing the page with user stack
     * frames. This is actively harmful in qemu as it marks pages with
     * code it translated as read-only, which is somewhat problematic
     * for user trying to use the stack as intended.
     */
    p = rounddown(p, TARGET_PAGE_SIZE);

    /* Calculate the string space needed */
    stringspace = 0;
    for (i = 0; i < bprm->argc; ++i) {
        stringspace += strlen(bprm->argv[i]) + 1;
    }
    for (i = 0; i < bprm->envc; ++i) {
        stringspace += strlen(bprm->envp[i]) + 1;
    }
    if (stringspace > TARGET_ARG_MAX) {
        errno = ENOMEM;
        return -1;
    }
    /* Make room for the argv and envp strings */
    destp = rounddown(p - stringspace, sizeof(abi_ulong));
    p = argvp = destp - (bprm->argc + bprm->envc + 2) * sizeof(abi_ulong);
    /* Remember the strings pointer */
    if (stringp) {
        *stringp = destp;
    }
    /*
     * Add argv strings.  Note that the argv[] vectors are added by
     * loader_build_argptr()
     */
    /* XXX need to make room for auxargs */
    ps_strs.ps_argvstr = tswapl(argvp);
    ps_strs.ps_nargvstr = tswap32(bprm->argc);
    for (i = 0; i < bprm->argc; ++i) {
        size_t len = strlen(bprm->argv[i]) + 1;

        if (memcpy_to_target(destp, bprm->argv[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, argvp)) {
            errno = EFAULT;
            return -1;
        }
        argvp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, argvp)) {
        errno = EFAULT;
        return -1;
    }
    /*
     * Add env strings. Note that the envp[] vectors are added by
     * loader_build_argptr().
     */
    envp = argvp + sizeof(abi_ulong);
    ps_strs.ps_envstr = tswapl(envp);
    ps_strs.ps_nenvstr = tswap32(bprm->envc);
    for (i = 0; i < bprm->envc; ++i) {
        size_t len = strlen(bprm->envp[i]) + 1;

        if (memcpy_to_target(destp, bprm->envp[i], len)) {
            errno = EFAULT;
            return -1;
        }
        if (put_user_ual(destp, envp)) {
            errno = EFAULT;
            return -1;
        }
        envp += sizeof(abi_ulong);
        destp += len;
    }
    if (put_user_ual(0, envp)) {
        errno = EFAULT;
        return -1;
    }
    if (memcpy_to_target(stack_hi_addr - sizeof(ps_strs), &ps_strs,
                sizeof(ps_strs))) {
        errno = EFAULT;
        return -1;
    }

    if (ret_addr) {
        *ret_addr = p;
    }

    return 0;
 }

#endif /* TARGET_OS_STACK_H */
