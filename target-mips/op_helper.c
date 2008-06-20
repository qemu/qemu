/*
 *  MIPS emulation helpers for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
#include <stdlib.h>
#include "exec.h"

#include "host-utils.h"

/*****************************************************************************/
/* Exceptions processing helpers */

void do_raise_exception_err (uint32_t exception, int error_code)
{
#if 1
    if (logfile && exception < 0x100)
        fprintf(logfile, "%s: %d %d\n", __func__, exception, error_code);
#endif
    env->exception_index = exception;
    env->error_code = error_code;
    T0 = 0;
    cpu_loop_exit();
}

void do_raise_exception (uint32_t exception)
{
    do_raise_exception_err(exception, 0);
}

void do_interrupt_restart (void)
{
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (env->CP0_Status & (1 << CP0St_IE)) &&
        (env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask)) {
        env->CP0_Cause &= ~(0x1f << CP0Ca_EC);
        do_raise_exception(EXCP_EXT_INTERRUPT);
    }
}

void do_restore_state (void *pc_ptr)
{
    TranslationBlock *tb;
    unsigned long pc = (unsigned long) pc_ptr;
    
    tb = tb_find_pc (pc);
    if (tb) {
        cpu_restore_state (tb, env, pc, NULL);
    }
}

void do_clo (void)
{
    T0 = clo32(T0);
}

void do_clz (void)
{
    T0 = clz32(T0);
}

#if defined(TARGET_MIPS64)
#if TARGET_LONG_BITS > HOST_LONG_BITS
/* Those might call libgcc functions.  */
void do_dsll (void)
{
    T0 = T0 << T1;
}

void do_dsll32 (void)
{
    T0 = T0 << (T1 + 32);
}

void do_dsra (void)
{
    T0 = (int64_t)T0 >> T1;
}

void do_dsra32 (void)
{
    T0 = (int64_t)T0 >> (T1 + 32);
}

void do_dsrl (void)
{
    T0 = T0 >> T1;
}

void do_dsrl32 (void)
{
    T0 = T0 >> (T1 + 32);
}

void do_drotr (void)
{
    target_ulong tmp;

    if (T1) {
        tmp = T0 << (0x40 - T1);
        T0 = (T0 >> T1) | tmp;
    }
}

void do_drotr32 (void)
{
    target_ulong tmp;

    tmp = T0 << (0x40 - (32 + T1));
    T0 = (T0 >> (32 + T1)) | tmp;
}

void do_dsllv (void)
{
    T0 = T1 << (T0 & 0x3F);
}

void do_dsrav (void)
{
    T0 = (int64_t)T1 >> (T0 & 0x3F);
}

void do_dsrlv (void)
{
    T0 = T1 >> (T0 & 0x3F);
}

void do_drotrv (void)
{
    target_ulong tmp;

    T0 &= 0x3F;
    if (T0) {
        tmp = T1 << (0x40 - T0);
        T0 = (T1 >> T0) | tmp;
    } else
        T0 = T1;
}

#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

void do_dclo (void)
{
    T0 = clo64(T0);
}

void do_dclz (void)
{
    T0 = clz64(T0);
}

#endif /* TARGET_MIPS64 */

/* 64 bits arithmetic for 32 bits hosts */
static always_inline uint64_t get_HILO (void)
{
    return ((uint64_t)(env->HI[env->current_tc][0]) << 32) | (uint32_t)env->LO[env->current_tc][0];
}

static always_inline void set_HILO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)HILO;
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HIT0_LO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    T0 = env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HI_LOT0 (uint64_t HILO)
{
    T0 = env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_madd (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() + tmp);
}

void do_maddu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() + tmp);
}

void do_msub (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() - tmp);
}

void do_msubu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() - tmp);
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

/* Multiplication variants of the vr54xx. */
void do_muls (void)
{
    set_HI_LOT0(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_mulsu (void)
{
    set_HI_LOT0(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_macc (void)
{
    set_HI_LOT0(((int64_t)get_HILO()) + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_macchi (void)
{
    set_HIT0_LO(((int64_t)get_HILO()) + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_maccu (void)
{
    set_HI_LOT0(((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_macchiu (void)
{
    set_HIT0_LO(((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_msac (void)
{
    set_HI_LOT0(((int64_t)get_HILO()) - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_msachi (void)
{
    set_HIT0_LO(((int64_t)get_HILO()) - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_msacu (void)
{
    set_HI_LOT0(((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_msachiu (void)
{
    set_HIT0_LO(((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_mulhi (void)
{
    set_HIT0_LO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
}

void do_mulhiu (void)
{
    set_HIT0_LO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
}

void do_mulshi (void)
{
    set_HIT0_LO(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_mulshiu (void)
{
    set_HIT0_LO(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

#ifdef TARGET_MIPS64
void do_dmult (void)
{
    muls64(&(env->LO[env->current_tc][0]), &(env->HI[env->current_tc][0]), T0, T1);
}

void do_dmultu (void)
{
    mulu64(&(env->LO[env->current_tc][0]), &(env->HI[env->current_tc][0]), T0, T1);
}
#endif

#ifdef CONFIG_USER_ONLY
void do_mfc0_random (void)
{
    cpu_abort(env, "mfc0 random\n");
}

void do_mfc0_count (void)
{
    cpu_abort(env, "mfc0 count\n");
}

void cpu_mips_store_count(CPUState *env, uint32_t value)
{
    cpu_abort(env, "mtc0 count\n");
}

void cpu_mips_store_compare(CPUState *env, uint32_t value)
{
    cpu_abort(env, "mtc0 compare\n");
}

void cpu_mips_start_count(CPUState *env)
{
    cpu_abort(env, "start count\n");
}

void cpu_mips_stop_count(CPUState *env)
{
    cpu_abort(env, "stop count\n");
}

void cpu_mips_update_irq(CPUState *env)
{
    cpu_abort(env, "mtc0 status / mtc0 cause\n");
}

void do_mtc0_status_debug(uint32_t old, uint32_t val)
{
    cpu_abort(env, "mtc0 status debug\n");
}

void do_mtc0_status_irqraise_debug (void)
{
    cpu_abort(env, "mtc0 status irqraise debug\n");
}

void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    cpu_abort(env, "mips_tlb_flush\n");
}

#else

/* CP0 helpers */
void do_mfc0_mvpcontrol (void)
{
    T0 = env->mvp->CP0_MVPControl;
}

void do_mfc0_mvpconf0 (void)
{
    T0 = env->mvp->CP0_MVPConf0;
}

void do_mfc0_mvpconf1 (void)
{
    T0 = env->mvp->CP0_MVPConf1;
}

void do_mfc0_random (void)
{
    T0 = (int32_t)cpu_mips_get_random(env);
}

void do_mfc0_tcstatus (void)
{
    T0 = env->CP0_TCStatus[env->current_tc];
}

void do_mftc0_tcstatus(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCStatus[other_tc];
}

void do_mfc0_tcbind (void)
{
    T0 = env->CP0_TCBind[env->current_tc];
}

void do_mftc0_tcbind(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCBind[other_tc];
}

void do_mfc0_tcrestart (void)
{
    T0 = env->PC[env->current_tc];
}

void do_mftc0_tcrestart(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->PC[other_tc];
}

void do_mfc0_tchalt (void)
{
    T0 = env->CP0_TCHalt[env->current_tc];
}

void do_mftc0_tchalt(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCHalt[other_tc];
}

void do_mfc0_tccontext (void)
{
    T0 = env->CP0_TCContext[env->current_tc];
}

void do_mftc0_tccontext(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCContext[other_tc];
}

void do_mfc0_tcschedule (void)
{
    T0 = env->CP0_TCSchedule[env->current_tc];
}

void do_mftc0_tcschedule(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCSchedule[other_tc];
}

void do_mfc0_tcschefback (void)
{
    T0 = env->CP0_TCScheFBack[env->current_tc];
}

void do_mftc0_tcschefback(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCScheFBack[other_tc];
}

void do_mfc0_count (void)
{
    T0 = (int32_t)cpu_mips_get_count(env);
}

void do_mftc0_entryhi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = (env->CP0_EntryHi & ~0xff) | (env->CP0_TCStatus[other_tc] & 0xff);
}

void do_mftc0_status(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t tcstatus = env->CP0_TCStatus[other_tc];

    T0 = env->CP0_Status & ~0xf1000018;
    T0 |= tcstatus & (0xf << CP0TCSt_TCU0);
    T0 |= (tcstatus & (1 << CP0TCSt_TMX)) >> (CP0TCSt_TMX - CP0St_MX);
    T0 |= (tcstatus & (0x3 << CP0TCSt_TKSU)) >> (CP0TCSt_TKSU - CP0St_KSU);
}

void do_mfc0_lladdr (void)
{
    T0 = (int32_t)env->CP0_LLAddr >> 4;
}

void do_mfc0_watchlo (uint32_t sel)
{
    T0 = (int32_t)env->CP0_WatchLo[sel];
}

void do_mfc0_watchhi (uint32_t sel)
{
    T0 = env->CP0_WatchHi[sel];
}

void do_mfc0_debug (void)
{
    T0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM)
        T0 |= 1 << CP0DB_DM;
}

void do_mftc0_debug(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    /* XXX: Might be wrong, check with EJTAG spec. */
    T0 = (env->CP0_Debug & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
         (env->CP0_Debug_tcstatus[other_tc] &
          ((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

#if defined(TARGET_MIPS64)
void do_dmfc0_tcrestart (void)
{
    T0 = env->PC[env->current_tc];
}

void do_dmfc0_tchalt (void)
{
    T0 = env->CP0_TCHalt[env->current_tc];
}

void do_dmfc0_tccontext (void)
{
    T0 = env->CP0_TCContext[env->current_tc];
}

void do_dmfc0_tcschedule (void)
{
    T0 = env->CP0_TCSchedule[env->current_tc];
}

void do_dmfc0_tcschefback (void)
{
    T0 = env->CP0_TCScheFBack[env->current_tc];
}

void do_dmfc0_lladdr (void)
{
    T0 = env->CP0_LLAddr >> 4;
}

void do_dmfc0_watchlo (uint32_t sel)
{
    T0 = env->CP0_WatchLo[sel];
}
#endif /* TARGET_MIPS64 */

void do_mtc0_index (void)
{
    int num = 1;
    unsigned int tmp = env->tlb->nb_tlb;

    do {
        tmp >>= 1;
        num <<= 1;
    } while (tmp);
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (T0 & (num - 1));
}

void do_mtc0_mvpcontrol (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))
        mask |= (1 << CP0MVPCo_CPA) | (1 << CP0MVPCo_VPC) |
                (1 << CP0MVPCo_EVP);
    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0MVPCo_STLB);
    newval = (env->mvp->CP0_MVPControl & ~mask) | (T0 & mask);

    // TODO: Enable/disable shared TLB, enable/disable VPEs.

    env->mvp->CP0_MVPControl = newval;
}

void do_mtc0_vpecontrol (void)
{
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (env->CP0_VPEControl & ~mask) | (T0 & mask);

    /* Yield scheduler intercept not implemented. */
    /* Gating storage scheduler intercept not implemented. */

    // TODO: Enable/disable TCs.

    env->CP0_VPEControl = newval;
}

void do_mtc0_vpeconf0 (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        if (env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA))
            mask |= (0xff << CP0VPEC0_XTC);
        mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    }
    newval = (env->CP0_VPEConf0 & ~mask) | (T0 & mask);

    // TODO: TC exclusive handling due to ERL/EXL.

    env->CP0_VPEConf0 = newval;
}

void do_mtc0_vpeconf1 (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (0xff << CP0VPEC1_NCX) | (0xff << CP0VPEC1_NCP2) |
                (0xff << CP0VPEC1_NCP1);
    newval = (env->CP0_VPEConf1 & ~mask) | (T0 & mask);

    /* UDI not implemented. */
    /* CP2 not implemented. */

    // TODO: Handle FPU (CP1) binding.

    env->CP0_VPEConf1 = newval;
}

void do_mtc0_yqmask (void)
{
    /* Yield qualifier inputs not implemented. */
    env->CP0_YQMask = 0x00000000;
}

void do_mtc0_vpeopt (void)
{
    env->CP0_VPEOpt = T0 & 0x0000ffff;
}

void do_mtc0_entrylo0 (void)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = T0 & 0x3FFFFFFF;
}

void do_mtc0_tcstatus (void)
{
    uint32_t mask = env->CP0_TCStatus_rw_bitmask;
    uint32_t newval;

    newval = (env->CP0_TCStatus[env->current_tc] & ~mask) | (T0 & mask);

    // TODO: Sync with CP0_Status.

    env->CP0_TCStatus[env->current_tc] = newval;
}

void do_mttc0_tcstatus (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Sync with CP0_Status.

    env->CP0_TCStatus[other_tc] = T0;
}

void do_mtc0_tcbind (void)
{
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->CP0_TCBind[env->current_tc] & ~mask) | (T0 & mask);
    env->CP0_TCBind[env->current_tc] = newval;
}

void do_mttc0_tcbind (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->CP0_TCBind[other_tc] & ~mask) | (T0 & mask);
    env->CP0_TCBind[other_tc] = newval;
}

void do_mtc0_tcrestart (void)
{
    env->PC[env->current_tc] = T0;
    env->CP0_TCStatus[env->current_tc] &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0ULL;
    /* MIPS16 not implemented. */
}

void do_mttc0_tcrestart (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->PC[other_tc] = T0;
    env->CP0_TCStatus[other_tc] &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0ULL;
    /* MIPS16 not implemented. */
}

void do_mtc0_tchalt (void)
{
    env->CP0_TCHalt[env->current_tc] = T0 & 0x1;

    // TODO: Halt TC / Restart (if allocated+active) TC.
}

void do_mttc0_tchalt (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Halt TC / Restart (if allocated+active) TC.

    env->CP0_TCHalt[other_tc] = T0;
}

void do_mtc0_tccontext (void)
{
    env->CP0_TCContext[env->current_tc] = T0;
}

void do_mttc0_tccontext (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCContext[other_tc] = T0;
}

void do_mtc0_tcschedule (void)
{
    env->CP0_TCSchedule[env->current_tc] = T0;
}

void do_mttc0_tcschedule (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCSchedule[other_tc] = T0;
}

void do_mtc0_tcschefback (void)
{
    env->CP0_TCScheFBack[env->current_tc] = T0;
}

void do_mttc0_tcschefback (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCScheFBack[other_tc] = T0;
}

void do_mtc0_entrylo1 (void)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = T0 & 0x3FFFFFFF;
}

void do_mtc0_context (void)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (T0 & ~0x007FFFFF);
}

void do_mtc0_pagemask (void)
{
    /* 1k pages not implemented */
    env->CP0_PageMask = T0 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
}

void do_mtc0_pagegrain (void)
{
    /* SmartMIPS not implemented */
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = 0;
}

void do_mtc0_wired (void)
{
    env->CP0_Wired = T0 % env->tlb->nb_tlb;
}

void do_mtc0_srsconf0 (void)
{
    env->CP0_SRSConf0 |= T0 & env->CP0_SRSConf0_rw_bitmask;
}

void do_mtc0_srsconf1 (void)
{
    env->CP0_SRSConf1 |= T0 & env->CP0_SRSConf1_rw_bitmask;
}

void do_mtc0_srsconf2 (void)
{
    env->CP0_SRSConf2 |= T0 & env->CP0_SRSConf2_rw_bitmask;
}

void do_mtc0_srsconf3 (void)
{
    env->CP0_SRSConf3 |= T0 & env->CP0_SRSConf3_rw_bitmask;
}

void do_mtc0_srsconf4 (void)
{
    env->CP0_SRSConf4 |= T0 & env->CP0_SRSConf4_rw_bitmask;
}

void do_mtc0_hwrena (void)
{
    env->CP0_HWREna = T0 & 0x0000000F;
}

void do_mtc0_count (void)
{
    cpu_mips_store_count(env, T0);
}

void do_mtc0_entryhi (void)
{
    target_ulong old, val;

    /* 1k pages not implemented */
    val = T0 & ((TARGET_PAGE_MASK << 1) | 0xFF);
#if defined(TARGET_MIPS64)
    val &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        uint32_t tcst = env->CP0_TCStatus[env->current_tc] & ~0xff;
        env->CP0_TCStatus[env->current_tc] = tcst | (val & 0xff);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & 0xFF) != (val & 0xFF))
        cpu_mips_tlb_flush(env, 1);
}

void do_mttc0_entryhi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_EntryHi = (env->CP0_EntryHi & 0xff) | (T0 & ~0xff);
    env->CP0_TCStatus[other_tc] = (env->CP0_TCStatus[other_tc] & ~0xff) | (T0 & 0xff);
}

void do_mtc0_compare (void)
{
    cpu_mips_store_compare(env, T0);
}

void do_mtc0_status (void)
{
    uint32_t val, old;
    uint32_t mask = env->CP0_Status_rw_bitmask;

    val = T0 & mask;
    old = env->CP0_Status;
    env->CP0_Status = (env->CP0_Status & ~mask) | val;
    compute_hflags(env);
    if (loglevel & CPU_LOG_EXEC)
        do_mtc0_status_debug(old, val);
    cpu_mips_update_irq(env);
}

void do_mttc0_status(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t tcstatus = env->CP0_TCStatus[other_tc];

    env->CP0_Status = T0 & ~0xf1000018;
    tcstatus = (tcstatus & ~(0xf << CP0TCSt_TCU0)) | (T0 & (0xf << CP0St_CU0));
    tcstatus = (tcstatus & ~(1 << CP0TCSt_TMX)) | ((T0 & (1 << CP0St_MX)) << (CP0TCSt_TMX - CP0St_MX));
    tcstatus = (tcstatus & ~(0x3 << CP0TCSt_TKSU)) | ((T0 & (0x3 << CP0St_KSU)) << (CP0TCSt_TKSU - CP0St_KSU));
    env->CP0_TCStatus[other_tc] = tcstatus;
}

void do_mtc0_intctl (void)
{
    /* vectored interrupts not implemented, no performance counters. */
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000002e0) | (T0 & 0x000002e0);
}

void do_mtc0_srsctl (void)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (T0 & mask);
}

void do_mtc0_cause (void)
{
    uint32_t mask = 0x00C00300;
    uint32_t old = env->CP0_Cause;

    if (env->insn_flags & ISA_MIPS32R2)
        mask |= 1 << CP0Ca_DC;

    env->CP0_Cause = (env->CP0_Cause & ~mask) | (T0 & mask);

    if ((old ^ env->CP0_Cause) & (1 << CP0Ca_DC)) {
        if (env->CP0_Cause & (1 << CP0Ca_DC))
            cpu_mips_stop_count(env);
        else
            cpu_mips_start_count(env);
    }

    /* Handle the software interrupt as an hardware one, as they
       are very similar */
    if (T0 & CP0Ca_IP_mask) {
        cpu_mips_update_irq(env);
    }
}

void do_mtc0_ebase (void)
{
    /* vectored interrupts not implemented */
    /* Multi-CPU not implemented */
    env->CP0_EBase = 0x80000000 | (T0 & 0x3FFFF000);
}

void do_mtc0_config0 (void)
{
    env->CP0_Config0 = (env->CP0_Config0 & 0x81FFFFF8) | (T0 & 0x00000007);
}

void do_mtc0_config2 (void)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
}

void do_mtc0_watchlo (uint32_t sel)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo[sel] = (T0 & ~0x7);
}

void do_mtc0_watchhi (uint32_t sel)
{
    env->CP0_WatchHi[sel] = (T0 & 0x40FF0FF8);
    env->CP0_WatchHi[sel] &= ~(env->CP0_WatchHi[sel] & T0 & 0x7);
}

void do_mtc0_xcontext (void)
{
    target_ulong mask = (1ULL << (env->SEGBITS - 7)) - 1;
    env->CP0_XContext = (env->CP0_XContext & mask) | (T0 & ~mask);
}

void do_mtc0_framemask (void)
{
    env->CP0_Framemask = T0; /* XXX */
}

void do_mtc0_debug (void)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (T0 & 0x13300120);
    if (T0 & (1 << CP0DB_DM))
        env->hflags |= MIPS_HFLAG_DM;
    else
        env->hflags &= ~MIPS_HFLAG_DM;
}

void do_mttc0_debug(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    /* XXX: Might be wrong, check with EJTAG spec. */
    env->CP0_Debug_tcstatus[other_tc] = T0 & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt));
    env->CP0_Debug = (env->CP0_Debug & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
                     (T0 & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

void do_mtc0_performance0 (void)
{
    env->CP0_Performance0 = T0 & 0x000007ff;
}

void do_mtc0_taglo (void)
{
    env->CP0_TagLo = T0 & 0xFFFFFCF6;
}

void do_mtc0_datalo (void)
{
    env->CP0_DataLo = T0; /* XXX */
}

void do_mtc0_taghi (void)
{
    env->CP0_TagHi = T0; /* XXX */
}

void do_mtc0_datahi (void)
{
    env->CP0_DataHi = T0; /* XXX */
}

void do_mtc0_status_debug(uint32_t old, uint32_t val)
{
    fprintf(logfile, "Status %08x (%08x) => %08x (%08x) Cause %08x",
            old, old & env->CP0_Cause & CP0Ca_IP_mask,
            val, val & env->CP0_Cause & CP0Ca_IP_mask,
            env->CP0_Cause);
    switch (env->hflags & MIPS_HFLAG_KSU) {
    case MIPS_HFLAG_UM: fputs(", UM\n", logfile); break;
    case MIPS_HFLAG_SM: fputs(", SM\n", logfile); break;
    case MIPS_HFLAG_KM: fputs("\n", logfile); break;
    default: cpu_abort(env, "Invalid MMU mode!\n"); break;
    }
}

void do_mtc0_status_irqraise_debug(void)
{
    fprintf(logfile, "Raise pending IRQs\n");
}
#endif /* !CONFIG_USER_ONLY */

/* MIPS MT functions */
void do_mftgpr(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->gpr[other_tc][sel];
}

void do_mftlo(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->LO[other_tc][sel];
}

void do_mfthi(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->HI[other_tc][sel];
}

void do_mftacx(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->ACX[other_tc][sel];
}

void do_mftdsp(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->DSPControl[other_tc];
}

void do_mttgpr(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->gpr[other_tc][sel];
}

void do_mttlo(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->LO[other_tc][sel];
}

void do_mtthi(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->HI[other_tc][sel];
}

void do_mttacx(uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->ACX[other_tc][sel];
}

void do_mttdsp(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->DSPControl[other_tc];
}

/* MIPS MT functions */
void do_dmt(void)
{
    // TODO
    T0 = 0;
    // rt = T0
}

void do_emt(void)
{
    // TODO
    T0 = 0;
    // rt = T0
}

void do_dvpe(void)
{
    // TODO
    T0 = 0;
    // rt = T0
}

void do_evpe(void)
{
    // TODO
    T0 = 0;
    // rt = T0
}

void do_fork(void)
{
    // T0 = rt, T1 = rs
    T0 = 0;
    // TODO: store to TC register
}

void do_yield(void)
{
    if (T0 < 0) {
        /* No scheduling policy implemented. */
        if (T0 != -2) {
            if (env->CP0_VPEControl & (1 << CP0VPECo_YSI) &&
                env->CP0_TCStatus[env->current_tc] & (1 << CP0TCSt_DT)) {
                env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
                env->CP0_VPEControl |= 4 << CP0VPECo_EXCPT;
                do_raise_exception(EXCP_THREAD);
            }
        }
    } else if (T0 == 0) {
	if (0 /* TODO: TC underflow */) {
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            do_raise_exception(EXCP_THREAD);
        } else {
            // TODO: Deallocate TC
        }
    } else if (T0 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        do_raise_exception(EXCP_THREAD);
    }
    T0 = env->CP0_YQMask;
}

/* CP1 functions */
void fpu_handle_exception(void)
{
#ifdef CONFIG_SOFTFLOAT
    int flags = get_float_exception_flags(&env->fpu->fp_status);
    unsigned int cpuflags = 0, enable, cause = 0;

    enable = GET_FP_ENABLE(env->fpu->fcr31);

    /* determine current flags */
    if (flags & float_flag_invalid) {
        cpuflags |= FP_INVALID;
        cause |= FP_INVALID & enable;
    }
    if (flags & float_flag_divbyzero) {
        cpuflags |= FP_DIV0;
        cause |= FP_DIV0 & enable;
    }
    if (flags & float_flag_overflow) {
        cpuflags |= FP_OVERFLOW;
        cause |= FP_OVERFLOW & enable;
    }
    if (flags & float_flag_underflow) {
        cpuflags |= FP_UNDERFLOW;
        cause |= FP_UNDERFLOW & enable;
    }
    if (flags & float_flag_inexact) {
        cpuflags |= FP_INEXACT;
        cause |= FP_INEXACT & enable;
    }
    SET_FP_FLAGS(env->fpu->fcr31, cpuflags);
    SET_FP_CAUSE(env->fpu->fcr31, cause);
#else
    SET_FP_FLAGS(env->fpu->fcr31, 0);
    SET_FP_CAUSE(env->fpu->fcr31, 0);
#endif
}

#ifndef CONFIG_USER_ONLY
/* TLB management */
void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush (env, flush_global);
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}

static void r4k_mips_tlb_flush_extra (CPUState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static void r4k_fill_tlb (int idx)
{
    r4k_tlb_t *tlb;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & 0xFF;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->PFN[0] = (env->CP0_EntryLo0 >> 6) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->PFN[1] = (env->CP0_EntryLo1 >> 6) << 12;
}

void r4k_do_tlbwi (void)
{
    /* Discard cached TLB entries.  We could avoid doing this if the
       tlbwi is just upgrading access permissions on the current entry;
       that might be a further win.  */
    r4k_mips_tlb_flush_extra (env, env->tlb->nb_tlb);

    r4k_invalidate_tlb(env, env->CP0_Index % env->tlb->nb_tlb, 0);
    r4k_fill_tlb(env->CP0_Index % env->tlb->nb_tlb);
}

void r4k_do_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    r4k_invalidate_tlb(env, r, 1);
    r4k_fill_tlb(r);
}

void r4k_do_tlbp (void)
{
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint8_t ASID;
    int i;

    ASID = env->CP0_EntryHi & 0xFF;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == env->tlb->nb_tlb) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = env->tlb->nb_tlb; i < env->tlb->tlb_in_use; i++) {
	    tlb = &env->tlb->mmu.r4k.tlb[i];
	    /* 1k pages are not supported. */
	    mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
	    tag = env->CP0_EntryHi & ~mask;
	    VPN = tlb->VPN & ~mask;
	    /* Check ASID, virtual page number & size */
	    if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
                r4k_mips_tlb_flush_extra (env, i);
	        break;
	    }
	}

        env->CP0_Index |= 0x80000000;
    }
}

void r4k_do_tlbr (void)
{
    r4k_tlb_t *tlb;
    uint8_t ASID;

    ASID = env->CP0_EntryHi & 0xFF;
    tlb = &env->tlb->mmu.r4k.tlb[env->CP0_Index % env->tlb->nb_tlb];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush (env, 1);

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    env->CP0_PageMask = tlb->PageMask;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
}

#endif /* !CONFIG_USER_ONLY */

void dump_ldst (const unsigned char *func)
{
    if (loglevel)
        fprintf(logfile, "%s => " TARGET_FMT_lx " " TARGET_FMT_lx "\n", __func__, T0, T1);
}

void dump_sc (void)
{
    if (loglevel) {
        fprintf(logfile, "%s " TARGET_FMT_lx " at " TARGET_FMT_lx " (" TARGET_FMT_lx ")\n", __func__,
                T1, T0, env->CP0_LLAddr);
    }
}

/* Specials */
void do_di (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 & ~(1 << CP0St_IE);
    cpu_mips_update_irq(env);
}

void do_ei (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 | (1 << CP0St_IE);
    cpu_mips_update_irq(env);
}

void debug_pre_eret (void)
{
    fprintf(logfile, "ERET: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
            env->PC[env->current_tc], env->CP0_EPC);
    if (env->CP0_Status & (1 << CP0St_ERL))
        fprintf(logfile, " ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
    if (env->hflags & MIPS_HFLAG_DM)
        fprintf(logfile, " DEPC " TARGET_FMT_lx, env->CP0_DEPC);
    fputs("\n", logfile);
}

void debug_post_eret (void)
{
    fprintf(logfile, "  =>  PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
            env->PC[env->current_tc], env->CP0_EPC);
    if (env->CP0_Status & (1 << CP0St_ERL))
        fprintf(logfile, " ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
    if (env->hflags & MIPS_HFLAG_DM)
        fprintf(logfile, " DEPC " TARGET_FMT_lx, env->CP0_DEPC);
    switch (env->hflags & MIPS_HFLAG_KSU) {
    case MIPS_HFLAG_UM: fputs(", UM\n", logfile); break;
    case MIPS_HFLAG_SM: fputs(", SM\n", logfile); break;
    case MIPS_HFLAG_KM: fputs("\n", logfile); break;
    default: cpu_abort(env, "Invalid MMU mode!\n"); break;
    }
}

void do_eret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        debug_pre_eret();
    if (env->CP0_Status & (1 << CP0St_ERL)) {
        env->PC[env->current_tc] = env->CP0_ErrorEPC;
        env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        env->PC[env->current_tc] = env->CP0_EPC;
        env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    compute_hflags(env);
    if (loglevel & CPU_LOG_EXEC)
        debug_post_eret();
    env->CP0_LLAddr = 1;
}

void do_deret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        debug_pre_eret();
    env->PC[env->current_tc] = env->CP0_DEPC;
    env->hflags &= MIPS_HFLAG_DM;
    compute_hflags(env);
    if (loglevel & CPU_LOG_EXEC)
        debug_post_eret();
    env->CP0_LLAddr = 1;
}

void do_rdhwr_cpunum(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 0)))
        T0 = env->CP0_EBase & 0x3ff;
    else
        do_raise_exception(EXCP_RI);
}

void do_rdhwr_synci_step(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 1)))
        T0 = env->SYNCI_Step;
    else
        do_raise_exception(EXCP_RI);
}

void do_rdhwr_cc(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 2)))
        T0 = env->CP0_Count;
    else
        do_raise_exception(EXCP_RI);
}

void do_rdhwr_ccres(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 3)))
        T0 = env->CCRes;
    else
        do_raise_exception(EXCP_RI);
}

/* Bitfield operations. */
void do_ext(uint32_t pos, uint32_t size)
{
    T0 = (int32_t)((T1 >> pos) & ((size < 32) ? ((1 << size) - 1) : ~0));
}

void do_ins(uint32_t pos, uint32_t size)
{
    target_ulong mask = ((size < 32) ? ((1 << size) - 1) : ~0) << pos;

    T0 = (int32_t)((T0 & ~mask) | ((T1 << pos) & mask));
}

void do_wsbh(void)
{
    T0 = (int32_t)(((T1 << 8) & ~0x00FF00FF) | ((T1 >> 8) & 0x00FF00FF));
}

#if defined(TARGET_MIPS64)
void do_dext(uint32_t pos, uint32_t size)
{
    T0 = (T1 >> pos) & ((size < 64) ? ((1ULL << size) - 1) : ~0ULL);
}

void do_dins(uint32_t pos, uint32_t size)
{
    target_ulong mask = ((size < 64) ? ((1ULL << size) - 1) : ~0ULL) << pos;

    T0 = (T0 & ~mask) | ((T1 << pos) & mask);
}

void do_dsbh(void)
{
    T0 = ((T1 << 8) & ~0x00FF00FF00FF00FFULL) | ((T1 >> 8) & 0x00FF00FF00FF00FFULL);
}

void do_dshd(void)
{
    T1 = ((T1 << 16) & ~0x0000FFFF0000FFFFULL) | ((T1 >> 16) & 0x0000FFFF0000FFFFULL);
    T0 = (T1 << 32) | (T1 >> 32);
}
#endif

void do_pmon (int function)
{
    function /= 2;
    switch (function) {
    case 2: /* TODO: char inbyte(int waitflag); */
        if (env->gpr[env->current_tc][4] == 0)
            env->gpr[env->current_tc][2] = -1;
        /* Fall through */
    case 11: /* TODO: char inbyte (void); */
        env->gpr[env->current_tc][2] = -1;
        break;
    case 3:
    case 12:
        printf("%c", (char)(env->gpr[env->current_tc][4] & 0xFF));
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)(unsigned long)env->gpr[env->current_tc][4];
            printf("%s", fmt);
        }
        break;
    }
}

void do_wait (void)
{
    env->halted = 1;
    do_raise_exception(EXCP_HLT);
}

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr)
{
    env->CP0_BadVAddr = addr;
    do_restore_state (retaddr);
    do_raise_exception ((is_write == 1) ? EXCP_AdES : EXCP_AdEL);
}

void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_mips_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        do_raise_exception_err(env->exception_index, env->error_code);
    }
    env = saved_env;
}

void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int unused)
{
    if (is_exec)
        do_raise_exception(EXCP_IBE);
    else
        do_raise_exception(EXCP_DBE);
}
#endif /* !CONFIG_USER_ONLY */

/* Complex FPU operations which may need stack space. */

#define FLOAT_ONE32 make_float32(0x3f8 << 20)
#define FLOAT_ONE64 make_float64(0x3ffULL << 52)
#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)
#define FLOAT_QNAN32 0x7fbfffff
#define FLOAT_QNAN64 0x7ff7ffffffffffffULL
#define FLOAT_SNAN32 0x7fffffff
#define FLOAT_SNAN64 0x7fffffffffffffffULL

/* convert MIPS rounding mode in FCR31 to IEEE library */
unsigned int ieee_rm[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

#define RESTORE_ROUNDING_MODE \
    set_float_rounding_mode(ieee_rm[env->fpu->fcr31 & 3], &env->fpu->fp_status)

void do_cfc1 (uint32_t reg)
{
    switch (reg) {
    case 0:
        T0 = (int32_t)env->fpu->fcr0;
        break;
    case 25:
        T0 = ((env->fpu->fcr31 >> 24) & 0xfe) | ((env->fpu->fcr31 >> 23) & 0x1);
        break;
    case 26:
        T0 = env->fpu->fcr31 & 0x0003f07c;
        break;
    case 28:
        T0 = (env->fpu->fcr31 & 0x00000f83) | ((env->fpu->fcr31 >> 22) & 0x4);
        break;
    default:
        T0 = (int32_t)env->fpu->fcr31;
        break;
    }
}

void do_ctc1 (uint32_t reg)
{
    switch(reg) {
    case 25:
        if (T0 & 0xffffff00)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0x017fffff) | ((T0 & 0xfe) << 24) |
                     ((T0 & 0x1) << 23);
        break;
    case 26:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0xfffc0f83) | (T0 & 0x0003f07c);
        break;
    case 28:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0xfefff07c) | (T0 & 0x00000f83) |
                     ((T0 & 0x4) << 22);
        break;
    case 31:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = T0;
        break;
    default:
        return;
    }
    /* set rounding mode */
    RESTORE_ROUNDING_MODE;
    set_float_exception_flags(0, &env->fpu->fp_status);
    if ((GET_FP_ENABLE(env->fpu->fcr31) | 0x20) & GET_FP_CAUSE(env->fpu->fcr31))
        do_raise_exception(EXCP_FPE);
}

static always_inline char ieee_ex_to_mips(char xcpt)
{
    return (xcpt & float_flag_inexact) >> 5 |
           (xcpt & float_flag_underflow) >> 3 |
           (xcpt & float_flag_overflow) >> 1 |
           (xcpt & float_flag_divbyzero) << 1 |
           (xcpt & float_flag_invalid) << 4;
}

static always_inline char mips_ex_to_ieee(char xcpt)
{
    return (xcpt & FP_INEXACT) << 5 |
           (xcpt & FP_UNDERFLOW) << 3 |
           (xcpt & FP_OVERFLOW) << 1 |
           (xcpt & FP_DIV0) >> 1 |
           (xcpt & FP_INVALID) >> 4;
}

static always_inline void update_fcr31(void)
{
    int tmp = ieee_ex_to_mips(get_float_exception_flags(&env->fpu->fp_status));

    SET_FP_CAUSE(env->fpu->fcr31, tmp);
    if (GET_FP_ENABLE(env->fpu->fcr31) & tmp)
        do_raise_exception(EXCP_FPE);
    else
        UPDATE_FP_FLAGS(env->fpu->fcr31, tmp);
}

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired single "ps",
   paired single lower "pl", paired single upper "pu".  */

#define FLOAT_OP(name, p) void do_float_##name##_##p(void)

/* unary operations, modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0, &env->fpu->fp_status); \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0, &env->fpu->fp_status); \
}
FLOAT_UNOP(sqrt)
#undef FLOAT_UNOP

FLOAT_OP(cvtd, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float32_to_float64(FST0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtd, w)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = int32_to_float64(WT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtd, l)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = int64_to_float64(DT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtl, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(cvtl, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}

FLOAT_OP(cvtps, pw)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int32_to_float32(WT0, &env->fpu->fp_status);
    FSTH2 = int32_to_float32(WTH0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtpw, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    WTH2 = float32_to_int32(FSTH0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(cvts, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float64_to_float32(FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, w)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int32_to_float32(WT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, l)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int64_to_float32(DT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, pl)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = WT0;
    update_fcr31();
}
FLOAT_OP(cvts, pu)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = WTH0;
    update_fcr31();
}
FLOAT_OP(cvtw, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(cvtw, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(roundl, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(roundl, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(roundw, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(roundw, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(truncl, d)
{
    DT2 = float64_to_int64_round_to_zero(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(truncl, s)
{
    DT2 = float32_to_int64_round_to_zero(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(truncw, d)
{
    WT2 = float64_to_int32_round_to_zero(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(truncw, s)
{
    WT2 = float32_to_int32_round_to_zero(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(ceill, d)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(ceill, s)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(ceilw, d)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(ceilw, s)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(floorl, d)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(floorl, s)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(floorw, d)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(floorw, s)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

/* unary operations, not modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0);   \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0);   \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    FST2 = float32_ ## name(FST0);   \
    FSTH2 = float32_ ## name(FSTH0); \
}
FLOAT_UNOP(abs)
FLOAT_UNOP(chs)
#undef FLOAT_UNOP

/* MIPS specific unary operations */
FLOAT_OP(recip, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(rsqrt, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_sqrt(FDT0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(recip1, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip1, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip1, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    FSTH2 = float32_div(FLOAT_ONE32, FSTH0, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(rsqrt1, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_sqrt(FDT0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt1, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt1, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FSTH2 = float32_sqrt(FSTH0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    FSTH2 = float32_div(FLOAT_ONE32, FSTH2, &env->fpu->fp_status);
    update_fcr31();
}

/* binary operations */
#define FLOAT_BINOP(name) \
FLOAT_OP(name, d)         \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FDT2 = float64_ ## name (FDT0, FDT1, &env->fpu->fp_status);    \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID)                \
        DT2 = FLOAT_QNAN64;                                        \
}                         \
FLOAT_OP(name, s)         \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fpu->fp_status);    \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID)                \
        WT2 = FLOAT_QNAN32;                                        \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name (FSTH0, FSTH1, &env->fpu->fp_status); \
    update_fcr31();       \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID) {              \
        WT2 = FLOAT_QNAN32;                                        \
        WTH2 = FLOAT_QNAN32;                                       \
    }                     \
}
FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

/* ternary operations */
#define FLOAT_TERNOP(name1, name2) \
FLOAT_OP(name1 ## name2, d)        \
{                                  \
    FDT0 = float64_ ## name1 (FDT0, FDT1, &env->fpu->fp_status);    \
    FDT2 = float64_ ## name2 (FDT0, FDT2, &env->fpu->fp_status);    \
}                                  \
FLOAT_OP(name1 ## name2, s)        \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
}                                  \
FLOAT_OP(name1 ## name2, ps)       \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FSTH0 = float32_ ## name1 (FSTH0, FSTH1, &env->fpu->fp_status); \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name2 (FSTH0, FSTH2, &env->fpu->fp_status); \
}
FLOAT_TERNOP(mul, add)
FLOAT_TERNOP(mul, sub)
#undef FLOAT_TERNOP

/* negated ternary operations */
#define FLOAT_NTERNOP(name1, name2) \
FLOAT_OP(n ## name1 ## name2, d)    \
{                                   \
    FDT0 = float64_ ## name1 (FDT0, FDT1, &env->fpu->fp_status);    \
    FDT2 = float64_ ## name2 (FDT0, FDT2, &env->fpu->fp_status);    \
    FDT2 = float64_chs(FDT2);       \
}                                   \
FLOAT_OP(n ## name1 ## name2, s)    \
{                                   \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FST2 = float32_chs(FST2);       \
}                                   \
FLOAT_OP(n ## name1 ## name2, ps)   \
{                                   \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FSTH0 = float32_ ## name1 (FSTH0, FSTH1, &env->fpu->fp_status); \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name2 (FSTH0, FSTH2, &env->fpu->fp_status); \
    FST2 = float32_chs(FST2);       \
    FSTH2 = float32_chs(FSTH2);     \
}
FLOAT_NTERNOP(mul, add)
FLOAT_NTERNOP(mul, sub)
#undef FLOAT_NTERNOP

/* MIPS specific binary operations */
FLOAT_OP(recip2, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_mul(FDT0, FDT2, &env->fpu->fp_status);
    FDT2 = float64_chs(float64_sub(FDT2, FLOAT_ONE64, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(recip2, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FST2 = float32_chs(float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(recip2, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FSTH2 = float32_mul(FSTH0, FSTH2, &env->fpu->fp_status);
    FST2 = float32_chs(float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status));
    FSTH2 = float32_chs(float32_sub(FSTH2, FLOAT_ONE32, &env->fpu->fp_status));
    update_fcr31();
}

FLOAT_OP(rsqrt2, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_mul(FDT0, FDT2, &env->fpu->fp_status);
    FDT2 = float64_sub(FDT2, FLOAT_ONE64, &env->fpu->fp_status);
    FDT2 = float64_chs(float64_div(FDT2, FLOAT_TWO64, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(rsqrt2, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FST2 = float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status);
    FST2 = float32_chs(float32_div(FST2, FLOAT_TWO32, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(rsqrt2, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FSTH2 = float32_mul(FSTH0, FSTH2, &env->fpu->fp_status);
    FST2 = float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status);
    FSTH2 = float32_sub(FSTH2, FLOAT_ONE32, &env->fpu->fp_status);
    FST2 = float32_chs(float32_div(FST2, FLOAT_TWO32, &env->fpu->fp_status));
    FSTH2 = float32_chs(float32_div(FSTH2, FLOAT_TWO32, &env->fpu->fp_status));
    update_fcr31();
}

FLOAT_OP(addr, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_add (FST0, FSTH0, &env->fpu->fp_status);
    FSTH2 = float32_add (FST1, FSTH1, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(mulr, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul (FST0, FSTH0, &env->fpu->fp_status);
    FSTH2 = float32_mul (FST1, FSTH1, &env->fpu->fp_status);
    update_fcr31();
}

/* compare operations */
#define FOP_COND_D(op, cond)                   \
void do_cmp_d_ ## op (long cc)                 \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}                                              \
void do_cmpabs_d_ ## op (long cc)              \
{                                              \
    int c;                                     \
    FDT0 = float64_abs(FDT0);                  \
    FDT1 = float64_abs(FDT1);                  \
    c = cond;                                  \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}

int float64_is_unordered(int sig, float64 a, float64 b STATUS_PARAM)
{
    if (float64_is_signaling_nan(a) ||
        float64_is_signaling_nan(b) ||
        (sig && (float64_is_nan(a) || float64_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float64_is_nan(a) || float64_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(f,   (float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status), 0))
FOP_COND_D(un,  float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status))
FOP_COND_D(eq,  !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ueq, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(olt, !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ult, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ole, !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_le(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ule, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_le(FDT0, FDT1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(sf,  (float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status), 0))
FOP_COND_D(ngle,float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status))
FOP_COND_D(seq, !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ngl, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(lt,  !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(nge, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(le,  !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_le(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ngt, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_le(FDT0, FDT1, &env->fpu->fp_status))

#define FOP_COND_S(op, cond)                   \
void do_cmp_s_ ## op (long cc)                 \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}                                              \
void do_cmpabs_s_ ## op (long cc)              \
{                                              \
    int c;                                     \
    FST0 = float32_abs(FST0);                  \
    FST1 = float32_abs(FST1);                  \
    c = cond;                                  \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}

flag float32_is_unordered(int sig, float32 a, float32 b STATUS_PARAM)
{
    if (float32_is_signaling_nan(a) ||
        float32_is_signaling_nan(b) ||
        (sig && (float32_is_nan(a) || float32_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float32_is_nan(a) || float32_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(f,   (float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status), 0))
FOP_COND_S(un,  float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status))
FOP_COND_S(eq,  !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ueq, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(olt, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ult, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ole, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_le(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ule, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_le(FST0, FST1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(sf,  (float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status), 0))
FOP_COND_S(ngle,float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status))
FOP_COND_S(seq, !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ngl, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(lt,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(nge, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(le,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_le(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ngt, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_le(FST0, FST1, &env->fpu->fp_status))

#define FOP_COND_PS(op, condl, condh)          \
void do_cmp_ps_ ## op (long cc)                \
{                                              \
    int cl = condl;                            \
    int ch = condh;                            \
    update_fcr31();                            \
    if (cl)                                    \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
    if (ch)                                    \
        SET_FP_COND(cc + 1, env->fpu);         \
    else                                       \
        CLEAR_FP_COND(cc + 1, env->fpu);       \
}                                              \
void do_cmpabs_ps_ ## op (long cc)             \
{                                              \
    int cl, ch;                                \
    FST0 = float32_abs(FST0);                  \
    FSTH0 = float32_abs(FSTH0);                \
    FST1 = float32_abs(FST1);                  \
    FSTH1 = float32_abs(FSTH1);                \
    cl = condl;                                \
    ch = condh;                                \
    update_fcr31();                            \
    if (cl)                                    \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
    if (ch)                                    \
        SET_FP_COND(cc + 1, env->fpu);         \
    else                                       \
        CLEAR_FP_COND(cc + 1, env->fpu);       \
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(f,   (float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status), 0),
                 (float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status), 0))
FOP_COND_PS(un,  float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status))
FOP_COND_PS(eq,  !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_eq(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ueq, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_eq(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(olt, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_lt(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ult, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_lt(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ole, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_le(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ule, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_le(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(sf,  (float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status), 0),
                 (float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status), 0))
FOP_COND_PS(ngle,float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status))
FOP_COND_PS(seq, !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_eq(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ngl, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_eq(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(lt,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_lt(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(nge, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_lt(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(le,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_le(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ngt, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_le(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
