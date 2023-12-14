/*
 *  i386 dependent signal definitions
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

#include "qemu/osdep.h"
#include "qemu.h"

/*
 * Compare to i386/i386/machdep.c sendsig()
 * Assumes that target stack frame memory is locked.
 */
abi_long set_sigtramp_args(CPUX86State *env, int sig,
                           struct target_sigframe *frame,
                           abi_ulong frame_addr,
                           struct target_sigaction *ka)
{
    /* XXX return -TARGET_EOPNOTSUPP; */
    return 0;
}

/*
 * Compare to i386/i386/exec_machdep.c sendsig()
 * Assumes that the memory is locked if frame points to user memory.
 */
abi_long setup_sigframe_arch(CPUX86State *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags)
{
    target_mcontext_t *mcp = &frame->sf_uc.uc_mcontext;

    get_mcontext(env, mcp, flags);
    return 0;
}

/* Compare to i386/i386/machdep.c get_mcontext() */
abi_long get_mcontext(CPUX86State *regs, target_mcontext_t *mcp, int flags)
{
    /* XXX */
    return -TARGET_EOPNOTSUPP;
}

/* Compare to i386/i386/machdep.c set_mcontext() */
abi_long set_mcontext(CPUX86State *regs, target_mcontext_t *mcp, int srflag)
{
    /* XXX */
    return -TARGET_EOPNOTSUPP;
}

abi_long get_ucontext_sigreturn(CPUX86State *regs, abi_ulong target_sf,
                                abi_ulong *target_uc)
{
    /* XXX */
    *target_uc = 0;
    return -TARGET_EOPNOTSUPP;
}
