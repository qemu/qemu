/*
 *  RISC-V signal definitions
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
#include "qemu/osdep.h"

#include "qemu.h"

/*
 * Compare with sendsig() in riscv/riscv/exec_machdep.c
 * Assumes that target stack frame memory is locked.
 */
abi_long
set_sigtramp_args(CPURISCVState *regs, int sig, struct target_sigframe *frame,
    abi_ulong frame_addr, struct target_sigaction *ka)
{
    /*
     * Arguments to signal handler:
     *  a0 (10) = signal number
     *  a1 (11) = siginfo pointer
     *  a2 (12) = ucontext pointer
     *  pc      = signal pointer handler
     *  sp (2)  = sigframe pointer
     *  ra (1)  = sigtramp at base of user stack
     */

     regs->gpr[xA0] = sig;
     regs->gpr[xA1] = frame_addr +
         offsetof(struct target_sigframe, sf_si);
     regs->gpr[xA2] = frame_addr +
         offsetof(struct target_sigframe, sf_uc);
     regs->pc = ka->_sa_handler;
     regs->gpr[xSP] = frame_addr;
     regs->gpr[xRA] = TARGET_PS_STRINGS - TARGET_SZSIGCODE;
     return 0;
}

/*
 * Compare to riscv/riscv/exec_machdep.c sendsig()
 * Assumes that the memory is locked if frame points to user memory.
 */
abi_long setup_sigframe_arch(CPURISCVState *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags)
{
    target_mcontext_t *mcp = &frame->sf_uc.uc_mcontext;

    get_mcontext(env, mcp, flags);
    return 0;
}
