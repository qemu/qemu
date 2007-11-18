/*
 *  PowerPC emulation special registers manipulation helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(__HELPER_REGS_H__)
#define __HELPER_REGS_H__

static always_inline target_ulong hreg_load_xer (CPUPPCState *env)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

static always_inline void hreg_store_xer (CPUPPCState *env, target_ulong value)
{
    xer_so = (value >> XER_SO) & 0x01;
    xer_ov = (value >> XER_OV) & 0x01;
    xer_ca = (value >> XER_CA) & 0x01;
    xer_cmp = (value >> XER_CMP) & 0xFF;
    xer_bc = (value >> XER_BC) & 0x7F;
}

/* Swap temporary saved registers with GPRs */
static always_inline void hreg_swap_gpr_tgpr (CPUPPCState *env)
{
    ppc_gpr_t tmp;

    tmp = env->gpr[0];
    env->gpr[0] = env->tgpr[0];
    env->tgpr[0] = tmp;
    tmp = env->gpr[1];
    env->gpr[1] = env->tgpr[1];
    env->tgpr[1] = tmp;
    tmp = env->gpr[2];
    env->gpr[2] = env->tgpr[2];
    env->tgpr[2] = tmp;
    tmp = env->gpr[3];
    env->gpr[3] = env->tgpr[3];
    env->tgpr[3] = tmp;
}

static always_inline void hreg_compute_mem_idx (CPUPPCState *env)
{
    /* Precompute MMU index */
    if (msr_pr == 0 && msr_hv != 0) {
        env->mmu_idx = 2;
    } else {
        env->mmu_idx = 1 - msr_pr;
    }
}

static always_inline void hreg_compute_hflags (CPUPPCState *env)
{
    target_ulong hflags_mask;

    /* We 'forget' FE0 & FE1: we'll never generate imprecise exceptions */
    hflags_mask = (1 << MSR_VR) | (1 << MSR_AP) | (1 << MSR_SA) |
        (1 << MSR_PR) | (1 << MSR_FP) | (1 << MSR_SE) | (1 << MSR_BE) |
        (1 << MSR_LE);
    hflags_mask |= (1ULL << MSR_CM) | (1ULL << MSR_SF) | MSR_HVB;
    hreg_compute_mem_idx(env);
    env->hflags = env->msr & hflags_mask;
    /* Merge with hflags coming from other registers */
    env->hflags |= env->hflags_nmsr;
}

static always_inline int hreg_store_msr (CPUPPCState *env, target_ulong value,
                                         int alter_hv)
{
    int excp;

    excp = 0;
    value &= env->msr_mask;
#if !defined (CONFIG_USER_ONLY)
    if (!alter_hv) {
        /* mtmsr cannot alter the hypervisor state */
        value &= ~MSR_HVB;
        value |= env->msr & MSR_HVB;
    }
    if (((value >> MSR_IR) & 1) != msr_ir ||
        ((value >> MSR_DR) & 1) != msr_dr) {
        /* Flush all tlb when changing translation mode */
        tlb_flush(env, 1);
        excp = POWERPC_EXCP_NONE;
        env->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
    if (unlikely((env->flags & POWERPC_FLAG_TGPR) &&
                 ((value ^ env->msr) & (1 << MSR_TGPR)))) {
        /* Swap temporary saved registers with GPRs */
        hreg_swap_gpr_tgpr(env);
    }
    if (unlikely((value >> MSR_EP) & 1) != msr_ep) {
        /* Change the exception prefix on PowerPC 601 */
        env->excp_prefix = ((value >> MSR_EP) & 1) * 0xFFF00000;
    }
#endif
    env->msr = value;
    hreg_compute_hflags(env);
#if !defined (CONFIG_USER_ONLY)
    if (unlikely(msr_pow == 1)) {
        if ((*env->check_pow)(env)) {
            env->halted = 1;
            excp = EXCP_HALTED;
        }
    }
#endif

    return excp;
}

#endif /* !defined(__HELPER_REGS_H__) */
