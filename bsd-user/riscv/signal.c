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

/*
 * Compare with get_mcontext() in riscv/riscv/machdep.c
 * Assumes that the memory is locked if mcp points to user memory.
 */
abi_long get_mcontext(CPURISCVState *regs, target_mcontext_t *mcp,
        int flags)
{

    mcp->mc_gpregs.gp_t[0] = tswap64(regs->gpr[5]);
    mcp->mc_gpregs.gp_t[1] = tswap64(regs->gpr[6]);
    mcp->mc_gpregs.gp_t[2] = tswap64(regs->gpr[7]);
    mcp->mc_gpregs.gp_t[3] = tswap64(regs->gpr[28]);
    mcp->mc_gpregs.gp_t[4] = tswap64(regs->gpr[29]);
    mcp->mc_gpregs.gp_t[5] = tswap64(regs->gpr[30]);
    mcp->mc_gpregs.gp_t[6] = tswap64(regs->gpr[31]);

    mcp->mc_gpregs.gp_s[0] = tswap64(regs->gpr[8]);
    mcp->mc_gpregs.gp_s[1] = tswap64(regs->gpr[9]);
    mcp->mc_gpregs.gp_s[2] = tswap64(regs->gpr[18]);
    mcp->mc_gpregs.gp_s[3] = tswap64(regs->gpr[19]);
    mcp->mc_gpregs.gp_s[4] = tswap64(regs->gpr[20]);
    mcp->mc_gpregs.gp_s[5] = tswap64(regs->gpr[21]);
    mcp->mc_gpregs.gp_s[6] = tswap64(regs->gpr[22]);
    mcp->mc_gpregs.gp_s[7] = tswap64(regs->gpr[23]);
    mcp->mc_gpregs.gp_s[8] = tswap64(regs->gpr[24]);
    mcp->mc_gpregs.gp_s[9] = tswap64(regs->gpr[25]);
    mcp->mc_gpregs.gp_s[10] = tswap64(regs->gpr[26]);
    mcp->mc_gpregs.gp_s[11] = tswap64(regs->gpr[27]);

    mcp->mc_gpregs.gp_a[0] = tswap64(regs->gpr[10]);
    mcp->mc_gpregs.gp_a[1] = tswap64(regs->gpr[11]);
    mcp->mc_gpregs.gp_a[2] = tswap64(regs->gpr[12]);
    mcp->mc_gpregs.gp_a[3] = tswap64(regs->gpr[13]);
    mcp->mc_gpregs.gp_a[4] = tswap64(regs->gpr[14]);
    mcp->mc_gpregs.gp_a[5] = tswap64(regs->gpr[15]);
    mcp->mc_gpregs.gp_a[6] = tswap64(regs->gpr[16]);
    mcp->mc_gpregs.gp_a[7] = tswap64(regs->gpr[17]);

    if (flags & TARGET_MC_GET_CLEAR_RET) {
        mcp->mc_gpregs.gp_a[0] = 0; /* a0 */
        mcp->mc_gpregs.gp_a[1] = 0; /* a1 */
        mcp->mc_gpregs.gp_t[0] = 0; /* clear syscall error */
    }

    mcp->mc_gpregs.gp_ra = tswap64(regs->gpr[1]);
    mcp->mc_gpregs.gp_sp = tswap64(regs->gpr[2]);
    mcp->mc_gpregs.gp_gp = tswap64(regs->gpr[3]);
    mcp->mc_gpregs.gp_tp = tswap64(regs->gpr[4]);
    mcp->mc_gpregs.gp_sepc = tswap64(regs->pc);

    return 0;
}

/* Compare with set_mcontext() in riscv/riscv/exec_machdep.c */
abi_long set_mcontext(CPURISCVState *regs, target_mcontext_t *mcp,
        int srflag)
{

    regs->gpr[5] = tswap64(mcp->mc_gpregs.gp_t[0]);
    regs->gpr[6] = tswap64(mcp->mc_gpregs.gp_t[1]);
    regs->gpr[7] = tswap64(mcp->mc_gpregs.gp_t[2]);
    regs->gpr[28] = tswap64(mcp->mc_gpregs.gp_t[3]);
    regs->gpr[29] = tswap64(mcp->mc_gpregs.gp_t[4]);
    regs->gpr[30] = tswap64(mcp->mc_gpregs.gp_t[5]);
    regs->gpr[31] = tswap64(mcp->mc_gpregs.gp_t[6]);

    regs->gpr[8] = tswap64(mcp->mc_gpregs.gp_s[0]);
    regs->gpr[9] = tswap64(mcp->mc_gpregs.gp_s[1]);
    regs->gpr[18] = tswap64(mcp->mc_gpregs.gp_s[2]);
    regs->gpr[19] = tswap64(mcp->mc_gpregs.gp_s[3]);
    regs->gpr[20] = tswap64(mcp->mc_gpregs.gp_s[4]);
    regs->gpr[21] = tswap64(mcp->mc_gpregs.gp_s[5]);
    regs->gpr[22] = tswap64(mcp->mc_gpregs.gp_s[6]);
    regs->gpr[23] = tswap64(mcp->mc_gpregs.gp_s[7]);
    regs->gpr[24] = tswap64(mcp->mc_gpregs.gp_s[8]);
    regs->gpr[25] = tswap64(mcp->mc_gpregs.gp_s[9]);
    regs->gpr[26] = tswap64(mcp->mc_gpregs.gp_s[10]);
    regs->gpr[27] = tswap64(mcp->mc_gpregs.gp_s[11]);

    regs->gpr[10] = tswap64(mcp->mc_gpregs.gp_a[0]);
    regs->gpr[11] = tswap64(mcp->mc_gpregs.gp_a[1]);
    regs->gpr[12] = tswap64(mcp->mc_gpregs.gp_a[2]);
    regs->gpr[13] = tswap64(mcp->mc_gpregs.gp_a[3]);
    regs->gpr[14] = tswap64(mcp->mc_gpregs.gp_a[4]);
    regs->gpr[15] = tswap64(mcp->mc_gpregs.gp_a[5]);
    regs->gpr[16] = tswap64(mcp->mc_gpregs.gp_a[6]);
    regs->gpr[17] = tswap64(mcp->mc_gpregs.gp_a[7]);


    regs->gpr[1] = tswap64(mcp->mc_gpregs.gp_ra);
    regs->gpr[2] = tswap64(mcp->mc_gpregs.gp_sp);
    regs->gpr[3] = tswap64(mcp->mc_gpregs.gp_gp);
    regs->gpr[4] = tswap64(mcp->mc_gpregs.gp_tp);
    regs->pc = tswap64(mcp->mc_gpregs.gp_sepc);

    return 0;
}

/* Compare with sys_sigreturn() in riscv/riscv/machdep.c */
abi_long get_ucontext_sigreturn(CPURISCVState *regs,
                        abi_ulong target_sf, abi_ulong *target_uc)
{

    *target_uc = target_sf;
    return 0;
}
