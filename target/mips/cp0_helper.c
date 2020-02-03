/*
 *  Helpers for emulation of CP0-related MIPS instructions.
 *
 *  Copyright (C) 2004-2005  Jocelyn Mayer
 *  Copyright (C) 2020  Wave Computing, Inc.
 *  Copyright (C) 2020  Aleksandar Markovic <amarkovic@wavecomp.com>
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
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internal.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/memop.h"
#include "sysemu/kvm.h"


#ifndef CONFIG_USER_ONLY
/* SMP helpers.  */
static bool mips_vpe_is_wfi(MIPSCPU *c)
{
    CPUState *cpu = CPU(c);
    CPUMIPSState *env = &c->env;

    /*
     * If the VPE is halted but otherwise active, it means it's waiting for
     * an interrupt.\
     */
    return cpu->halted && mips_vpe_active(env);
}

static bool mips_vp_is_wfi(MIPSCPU *c)
{
    CPUState *cpu = CPU(c);
    CPUMIPSState *env = &c->env;

    return cpu->halted && mips_vp_active(env);
}

static inline void mips_vpe_wake(MIPSCPU *c)
{
    /*
     * Don't set ->halted = 0 directly, let it be done via cpu_has_work
     * because there might be other conditions that state that c should
     * be sleeping.
     */
    qemu_mutex_lock_iothread();
    cpu_interrupt(CPU(c), CPU_INTERRUPT_WAKE);
    qemu_mutex_unlock_iothread();
}

static inline void mips_vpe_sleep(MIPSCPU *cpu)
{
    CPUState *cs = CPU(cpu);

    /*
     * The VPE was shut off, really go to bed.
     * Reset any old _WAKE requests.
     */
    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
}

static inline void mips_tc_wake(MIPSCPU *cpu, int tc)
{
    CPUMIPSState *c = &cpu->env;

    /* FIXME: TC reschedule.  */
    if (mips_vpe_active(c) && !mips_vpe_is_wfi(cpu)) {
        mips_vpe_wake(cpu);
    }
}

static inline void mips_tc_sleep(MIPSCPU *cpu, int tc)
{
    CPUMIPSState *c = &cpu->env;

    /* FIXME: TC reschedule.  */
    if (!mips_vpe_active(c)) {
        mips_vpe_sleep(cpu);
    }
}

/**
 * mips_cpu_map_tc:
 * @env: CPU from which mapping is performed.
 * @tc: Should point to an int with the value of the global TC index.
 *
 * This function will transform @tc into a local index within the
 * returned #CPUMIPSState.
 */

/*
 * FIXME: This code assumes that all VPEs have the same number of TCs,
 *        which depends on runtime setup. Can probably be fixed by
 *        walking the list of CPUMIPSStates.
 */
static CPUMIPSState *mips_cpu_map_tc(CPUMIPSState *env, int *tc)
{
    MIPSCPU *cpu;
    CPUState *cs;
    CPUState *other_cs;
    int vpe_idx;
    int tc_idx = *tc;

    if (!(env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))) {
        /* Not allowed to address other CPUs.  */
        *tc = env->current_tc;
        return env;
    }

    cs = env_cpu(env);
    vpe_idx = tc_idx / cs->nr_threads;
    *tc = tc_idx % cs->nr_threads;
    other_cs = qemu_get_cpu(vpe_idx);
    if (other_cs == NULL) {
        return env;
    }
    cpu = MIPS_CPU(other_cs);
    return &cpu->env;
}

/*
 * The per VPE CP0_Status register shares some fields with the per TC
 * CP0_TCStatus registers. These fields are wired to the same registers,
 * so changes to either of them should be reflected on both registers.
 *
 * Also, EntryHi shares the bottom 8 bit ASID with TCStauts.
 *
 * These helper call synchronizes the regs for a given cpu.
 */

/*
 * Called for updates to CP0_Status.  Defined in "cpu.h" for gdbstub.c.
 * static inline void sync_c0_status(CPUMIPSState *env, CPUMIPSState *cpu,
 *                                   int tc);
 */

/* Called for updates to CP0_TCStatus.  */
static void sync_c0_tcstatus(CPUMIPSState *cpu, int tc,
                             target_ulong v)
{
    uint32_t status;
    uint32_t tcu, tmx, tasid, tksu;
    uint32_t mask = ((1U << CP0St_CU3)
                       | (1 << CP0St_CU2)
                       | (1 << CP0St_CU1)
                       | (1 << CP0St_CU0)
                       | (1 << CP0St_MX)
                       | (3 << CP0St_KSU));

    tcu = (v >> CP0TCSt_TCU0) & 0xf;
    tmx = (v >> CP0TCSt_TMX) & 0x1;
    tasid = v & cpu->CP0_EntryHi_ASID_mask;
    tksu = (v >> CP0TCSt_TKSU) & 0x3;

    status = tcu << CP0St_CU0;
    status |= tmx << CP0St_MX;
    status |= tksu << CP0St_KSU;

    cpu->CP0_Status &= ~mask;
    cpu->CP0_Status |= status;

    /* Sync the TASID with EntryHi.  */
    cpu->CP0_EntryHi &= ~cpu->CP0_EntryHi_ASID_mask;
    cpu->CP0_EntryHi |= tasid;

    compute_hflags(cpu);
}

/* Called for updates to CP0_EntryHi.  */
static void sync_c0_entryhi(CPUMIPSState *cpu, int tc)
{
    int32_t *tcst;
    uint32_t asid, v = cpu->CP0_EntryHi;

    asid = v & cpu->CP0_EntryHi_ASID_mask;

    if (tc == cpu->current_tc) {
        tcst = &cpu->active_tc.CP0_TCStatus;
    } else {
        tcst = &cpu->tcs[tc].CP0_TCStatus;
    }

    *tcst &= ~cpu->CP0_EntryHi_ASID_mask;
    *tcst |= asid;
}

/* CP0 helpers */
target_ulong helper_mfc0_mvpcontrol(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPControl;
}

target_ulong helper_mfc0_mvpconf0(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPConf0;
}

target_ulong helper_mfc0_mvpconf1(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPConf1;
}

target_ulong helper_mfc0_random(CPUMIPSState *env)
{
    return (int32_t)cpu_mips_get_random(env);
}

target_ulong helper_mfc0_tcstatus(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCStatus;
}

target_ulong helper_mftc0_tcstatus(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCStatus;
    } else {
        return other->tcs[other_tc].CP0_TCStatus;
    }
}

target_ulong helper_mfc0_tcbind(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCBind;
}

target_ulong helper_mftc0_tcbind(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCBind;
    } else {
        return other->tcs[other_tc].CP0_TCBind;
    }
}

target_ulong helper_mfc0_tcrestart(CPUMIPSState *env)
{
    return env->active_tc.PC;
}

target_ulong helper_mftc0_tcrestart(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.PC;
    } else {
        return other->tcs[other_tc].PC;
    }
}

target_ulong helper_mfc0_tchalt(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_mftc0_tchalt(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCHalt;
    } else {
        return other->tcs[other_tc].CP0_TCHalt;
    }
}

target_ulong helper_mfc0_tccontext(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_mftc0_tccontext(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCContext;
    } else {
        return other->tcs[other_tc].CP0_TCContext;
    }
}

target_ulong helper_mfc0_tcschedule(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_mftc0_tcschedule(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCSchedule;
    } else {
        return other->tcs[other_tc].CP0_TCSchedule;
    }
}

target_ulong helper_mfc0_tcschefback(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_mftc0_tcschefback(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.CP0_TCScheFBack;
    } else {
        return other->tcs[other_tc].CP0_TCScheFBack;
    }
}

target_ulong helper_mfc0_count(CPUMIPSState *env)
{
    return (int32_t)cpu_mips_get_count(env);
}

target_ulong helper_mfc0_saar(CPUMIPSState *env)
{
    if ((env->CP0_SAARI & 0x3f) < 2) {
        return (int32_t) env->CP0_SAAR[env->CP0_SAARI & 0x3f];
    }
    return 0;
}

target_ulong helper_mfhc0_saar(CPUMIPSState *env)
{
    if ((env->CP0_SAARI & 0x3f) < 2) {
        return env->CP0_SAAR[env->CP0_SAARI & 0x3f] >> 32;
    }
    return 0;
}

target_ulong helper_mftc0_entryhi(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EntryHi;
}

target_ulong helper_mftc0_cause(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tccause;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        tccause = other->CP0_Cause;
    } else {
        tccause = other->CP0_Cause;
    }

    return tccause;
}

target_ulong helper_mftc0_status(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_Status;
}

target_ulong helper_mfc0_lladdr(CPUMIPSState *env)
{
    return (int32_t)(env->CP0_LLAddr >> env->CP0_LLAddr_shift);
}

target_ulong helper_mfc0_maar(CPUMIPSState *env)
{
    return (int32_t) env->CP0_MAAR[env->CP0_MAARI];
}

target_ulong helper_mfhc0_maar(CPUMIPSState *env)
{
    return env->CP0_MAAR[env->CP0_MAARI] >> 32;
}

target_ulong helper_mfc0_watchlo(CPUMIPSState *env, uint32_t sel)
{
    return (int32_t)env->CP0_WatchLo[sel];
}

target_ulong helper_mfc0_watchhi(CPUMIPSState *env, uint32_t sel)
{
    return (int32_t) env->CP0_WatchHi[sel];
}

target_ulong helper_mfhc0_watchhi(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchHi[sel] >> 32;
}

target_ulong helper_mfc0_debug(CPUMIPSState *env)
{
    target_ulong t0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM) {
        t0 |= 1 << CP0DB_DM;
    }

    return t0;
}

target_ulong helper_mftc0_debug(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        tcstatus = other->active_tc.CP0_Debug_tcstatus;
    } else {
        tcstatus = other->tcs[other_tc].CP0_Debug_tcstatus;
    }

    /* XXX: Might be wrong, check with EJTAG spec. */
    return (other->CP0_Debug & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
            (tcstatus & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

#if defined(TARGET_MIPS64)
target_ulong helper_dmfc0_tcrestart(CPUMIPSState *env)
{
    return env->active_tc.PC;
}

target_ulong helper_dmfc0_tchalt(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_dmfc0_tccontext(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_dmfc0_tcschedule(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_dmfc0_tcschefback(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_dmfc0_lladdr(CPUMIPSState *env)
{
    return env->CP0_LLAddr >> env->CP0_LLAddr_shift;
}

target_ulong helper_dmfc0_maar(CPUMIPSState *env)
{
    return env->CP0_MAAR[env->CP0_MAARI];
}

target_ulong helper_dmfc0_watchlo(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchLo[sel];
}

target_ulong helper_dmfc0_watchhi(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchHi[sel];
}

target_ulong helper_dmfc0_saar(CPUMIPSState *env)
{
    if ((env->CP0_SAARI & 0x3f) < 2) {
        return env->CP0_SAAR[env->CP0_SAARI & 0x3f];
    }
    return 0;
}
#endif /* TARGET_MIPS64 */

void helper_mtc0_index(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t index_p = env->CP0_Index & 0x80000000;
    uint32_t tlb_index = arg1 & 0x7fffffff;
    if (tlb_index < env->tlb->nb_tlb) {
        if (env->insn_flags & ISA_MIPS32R6) {
            index_p |= arg1 & 0x80000000;
        }
        env->CP0_Index = index_p | tlb_index;
    }
}

void helper_mtc0_mvpcontrol(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        mask |= (1 << CP0MVPCo_CPA) | (1 << CP0MVPCo_VPC) |
                (1 << CP0MVPCo_EVP);
    }
    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC)) {
        mask |= (1 << CP0MVPCo_STLB);
    }
    newval = (env->mvp->CP0_MVPControl & ~mask) | (arg1 & mask);

    /* TODO: Enable/disable shared TLB, enable/disable VPEs. */

    env->mvp->CP0_MVPControl = newval;
}

void helper_mtc0_vpecontrol(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (env->CP0_VPEControl & ~mask) | (arg1 & mask);

    /*
     * Yield scheduler intercept not implemented.
     * Gating storage scheduler intercept not implemented.
     */

    /* TODO: Enable/disable TCs. */

    env->CP0_VPEControl = newval;
}

void helper_mttc0_vpecontrol(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (other->CP0_VPEControl & ~mask) | (arg1 & mask);

    /* TODO: Enable/disable TCs.  */

    other->CP0_VPEControl = newval;
}

target_ulong helper_mftc0_vpecontrol(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    /* FIXME: Mask away return zero on read bits.  */
    return other->CP0_VPEControl;
}

target_ulong helper_mftc0_vpeconf0(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_VPEConf0;
}

void helper_mtc0_vpeconf0(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        if (env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA)) {
            mask |= (0xff << CP0VPEC0_XTC);
        }
        mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    }
    newval = (env->CP0_VPEConf0 & ~mask) | (arg1 & mask);

    /* TODO: TC exclusive handling due to ERL/EXL. */

    env->CP0_VPEConf0 = newval;
}

void helper_mttc0_vpeconf0(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    uint32_t mask = 0;
    uint32_t newval;

    mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    newval = (other->CP0_VPEConf0 & ~mask) | (arg1 & mask);

    /* TODO: TC exclusive handling due to ERL/EXL.  */
    other->CP0_VPEConf0 = newval;
}

void helper_mtc0_vpeconf1(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (0xff << CP0VPEC1_NCX) | (0xff << CP0VPEC1_NCP2) |
                (0xff << CP0VPEC1_NCP1);
    newval = (env->CP0_VPEConf1 & ~mask) | (arg1 & mask);

    /* UDI not implemented. */
    /* CP2 not implemented. */

    /* TODO: Handle FPU (CP1) binding. */

    env->CP0_VPEConf1 = newval;
}

void helper_mtc0_yqmask(CPUMIPSState *env, target_ulong arg1)
{
    /* Yield qualifier inputs not implemented. */
    env->CP0_YQMask = 0x00000000;
}

void helper_mtc0_vpeopt(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_VPEOpt = arg1 & 0x0000ffff;
}

#define MTC0_ENTRYLO_MASK(env) ((env->PAMask >> 6) & 0x3FFFFFFF)

void helper_mtc0_entrylo0(CPUMIPSState *env, target_ulong arg1)
{
    /* 1k pages not implemented */
    target_ulong rxi = arg1 & (env->CP0_PageGrain & (3u << CP0PG_XIE));
    env->CP0_EntryLo0 = (arg1 & MTC0_ENTRYLO_MASK(env))
                        | (rxi << (CP0EnLo_XI - 30));
}

#if defined(TARGET_MIPS64)
#define DMTC0_ENTRYLO_MASK(env) (env->PAMask >> 6)

void helper_dmtc0_entrylo0(CPUMIPSState *env, uint64_t arg1)
{
    uint64_t rxi = arg1 & ((env->CP0_PageGrain & (3ull << CP0PG_XIE)) << 32);
    env->CP0_EntryLo0 = (arg1 & DMTC0_ENTRYLO_MASK(env)) | rxi;
}
#endif

void helper_mtc0_tcstatus(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = env->CP0_TCStatus_rw_bitmask;
    uint32_t newval;

    newval = (env->active_tc.CP0_TCStatus & ~mask) | (arg1 & mask);

    env->active_tc.CP0_TCStatus = newval;
    sync_c0_tcstatus(env, env->current_tc, newval);
}

void helper_mttc0_tcstatus(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.CP0_TCStatus = arg1;
    } else {
        other->tcs[other_tc].CP0_TCStatus = arg1;
    }
    sync_c0_tcstatus(other, other_tc, arg1);
}

void helper_mtc0_tcbind(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC)) {
        mask |= (1 << CP0TCBd_CurVPE);
    }
    newval = (env->active_tc.CP0_TCBind & ~mask) | (arg1 & mask);
    env->active_tc.CP0_TCBind = newval;
}

void helper_mttc0_tcbind(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC)) {
        mask |= (1 << CP0TCBd_CurVPE);
    }
    if (other_tc == other->current_tc) {
        newval = (other->active_tc.CP0_TCBind & ~mask) | (arg1 & mask);
        other->active_tc.CP0_TCBind = newval;
    } else {
        newval = (other->tcs[other_tc].CP0_TCBind & ~mask) | (arg1 & mask);
        other->tcs[other_tc].CP0_TCBind = newval;
    }
}

void helper_mtc0_tcrestart(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.PC = arg1;
    env->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0;
    env->lladdr = 0;
    /* MIPS16 not implemented. */
}

void helper_mttc0_tcrestart(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.PC = arg1;
        other->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        other->CP0_LLAddr = 0;
        other->lladdr = 0;
        /* MIPS16 not implemented. */
    } else {
        other->tcs[other_tc].PC = arg1;
        other->tcs[other_tc].CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        other->CP0_LLAddr = 0;
        other->lladdr = 0;
        /* MIPS16 not implemented. */
    }
}

void helper_mtc0_tchalt(CPUMIPSState *env, target_ulong arg1)
{
    MIPSCPU *cpu = env_archcpu(env);

    env->active_tc.CP0_TCHalt = arg1 & 0x1;

    /* TODO: Halt TC / Restart (if allocated+active) TC. */
    if (env->active_tc.CP0_TCHalt & 1) {
        mips_tc_sleep(cpu, env->current_tc);
    } else {
        mips_tc_wake(cpu, env->current_tc);
    }
}

void helper_mttc0_tchalt(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    MIPSCPU *other_cpu = env_archcpu(other);

    /* TODO: Halt TC / Restart (if allocated+active) TC. */

    if (other_tc == other->current_tc) {
        other->active_tc.CP0_TCHalt = arg1;
    } else {
        other->tcs[other_tc].CP0_TCHalt = arg1;
    }

    if (arg1 & 1) {
        mips_tc_sleep(other_cpu, other_tc);
    } else {
        mips_tc_wake(other_cpu, other_tc);
    }
}

void helper_mtc0_tccontext(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCContext = arg1;
}

void helper_mttc0_tccontext(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.CP0_TCContext = arg1;
    } else {
        other->tcs[other_tc].CP0_TCContext = arg1;
    }
}

void helper_mtc0_tcschedule(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCSchedule = arg1;
}

void helper_mttc0_tcschedule(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.CP0_TCSchedule = arg1;
    } else {
        other->tcs[other_tc].CP0_TCSchedule = arg1;
    }
}

void helper_mtc0_tcschefback(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCScheFBack = arg1;
}

void helper_mttc0_tcschefback(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.CP0_TCScheFBack = arg1;
    } else {
        other->tcs[other_tc].CP0_TCScheFBack = arg1;
    }
}

void helper_mtc0_entrylo1(CPUMIPSState *env, target_ulong arg1)
{
    /* 1k pages not implemented */
    target_ulong rxi = arg1 & (env->CP0_PageGrain & (3u << CP0PG_XIE));
    env->CP0_EntryLo1 = (arg1 & MTC0_ENTRYLO_MASK(env))
                        | (rxi << (CP0EnLo_XI - 30));
}

#if defined(TARGET_MIPS64)
void helper_dmtc0_entrylo1(CPUMIPSState *env, uint64_t arg1)
{
    uint64_t rxi = arg1 & ((env->CP0_PageGrain & (3ull << CP0PG_XIE)) << 32);
    env->CP0_EntryLo1 = (arg1 & DMTC0_ENTRYLO_MASK(env)) | rxi;
}
#endif

void helper_mtc0_context(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (arg1 & ~0x007FFFFF);
}

void helper_mtc0_memorymapid(CPUMIPSState *env, target_ulong arg1)
{
    int32_t old;
    old = env->CP0_MemoryMapID;
    env->CP0_MemoryMapID = (int32_t) arg1;
    /* If the MemoryMapID changes, flush qemu's TLB.  */
    if (old != env->CP0_MemoryMapID) {
        cpu_mips_tlb_flush(env);
    }
}

void update_pagemask(CPUMIPSState *env, target_ulong arg1, int32_t *pagemask)
{
    uint64_t mask = arg1 >> (TARGET_PAGE_BITS + 1);
    if (!(env->insn_flags & ISA_MIPS32R6) || (arg1 == ~0) ||
        (mask == 0x0000 || mask == 0x0003 || mask == 0x000F ||
         mask == 0x003F || mask == 0x00FF || mask == 0x03FF ||
         mask == 0x0FFF || mask == 0x3FFF || mask == 0xFFFF)) {
        env->CP0_PageMask = arg1 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
    }
}

void helper_mtc0_pagemask(CPUMIPSState *env, target_ulong arg1)
{
    update_pagemask(env, arg1, &env->CP0_PageMask);
}

void helper_mtc0_pagegrain(CPUMIPSState *env, target_ulong arg1)
{
    /* SmartMIPS not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = (arg1 & env->CP0_PageGrain_rw_bitmask) |
                         (env->CP0_PageGrain & ~env->CP0_PageGrain_rw_bitmask);
    compute_hflags(env);
    restore_pamask(env);
}

void helper_mtc0_segctl0(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = env_cpu(env);

    env->CP0_SegCtl0 = arg1 & CP0SC0_MASK;
    tlb_flush(cs);
}

void helper_mtc0_segctl1(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = env_cpu(env);

    env->CP0_SegCtl1 = arg1 & CP0SC1_MASK;
    tlb_flush(cs);
}

void helper_mtc0_segctl2(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = env_cpu(env);

    env->CP0_SegCtl2 = arg1 & CP0SC2_MASK;
    tlb_flush(cs);
}

void helper_mtc0_pwfield(CPUMIPSState *env, target_ulong arg1)
{
#if defined(TARGET_MIPS64)
    uint64_t mask = 0x3F3FFFFFFFULL;
    uint32_t old_ptei = (env->CP0_PWField >> CP0PF_PTEI) & 0x3FULL;
    uint32_t new_ptei = (arg1 >> CP0PF_PTEI) & 0x3FULL;

    if ((env->insn_flags & ISA_MIPS32R6)) {
        if (((arg1 >> CP0PF_BDI) & 0x3FULL) < 12) {
            mask &= ~(0x3FULL << CP0PF_BDI);
        }
        if (((arg1 >> CP0PF_GDI) & 0x3FULL) < 12) {
            mask &= ~(0x3FULL << CP0PF_GDI);
        }
        if (((arg1 >> CP0PF_UDI) & 0x3FULL) < 12) {
            mask &= ~(0x3FULL << CP0PF_UDI);
        }
        if (((arg1 >> CP0PF_MDI) & 0x3FULL) < 12) {
            mask &= ~(0x3FULL << CP0PF_MDI);
        }
        if (((arg1 >> CP0PF_PTI) & 0x3FULL) < 12) {
            mask &= ~(0x3FULL << CP0PF_PTI);
        }
    }
    env->CP0_PWField = arg1 & mask;

    if ((new_ptei >= 32) ||
            ((env->insn_flags & ISA_MIPS32R6) &&
                    (new_ptei == 0 || new_ptei == 1))) {
        env->CP0_PWField = (env->CP0_PWField & ~0x3FULL) |
                (old_ptei << CP0PF_PTEI);
    }
#else
    uint32_t mask = 0x3FFFFFFF;
    uint32_t old_ptew = (env->CP0_PWField >> CP0PF_PTEW) & 0x3F;
    uint32_t new_ptew = (arg1 >> CP0PF_PTEW) & 0x3F;

    if ((env->insn_flags & ISA_MIPS32R6)) {
        if (((arg1 >> CP0PF_GDW) & 0x3F) < 12) {
            mask &= ~(0x3F << CP0PF_GDW);
        }
        if (((arg1 >> CP0PF_UDW) & 0x3F) < 12) {
            mask &= ~(0x3F << CP0PF_UDW);
        }
        if (((arg1 >> CP0PF_MDW) & 0x3F) < 12) {
            mask &= ~(0x3F << CP0PF_MDW);
        }
        if (((arg1 >> CP0PF_PTW) & 0x3F) < 12) {
            mask &= ~(0x3F << CP0PF_PTW);
        }
    }
    env->CP0_PWField = arg1 & mask;

    if ((new_ptew >= 32) ||
            ((env->insn_flags & ISA_MIPS32R6) &&
                    (new_ptew == 0 || new_ptew == 1))) {
        env->CP0_PWField = (env->CP0_PWField & ~0x3F) |
                (old_ptew << CP0PF_PTEW);
    }
#endif
}

void helper_mtc0_pwsize(CPUMIPSState *env, target_ulong arg1)
{
#if defined(TARGET_MIPS64)
    env->CP0_PWSize = arg1 & 0x3F7FFFFFFFULL;
#else
    env->CP0_PWSize = arg1 & 0x3FFFFFFF;
#endif
}

void helper_mtc0_wired(CPUMIPSState *env, target_ulong arg1)
{
    if (env->insn_flags & ISA_MIPS32R6) {
        if (arg1 < env->tlb->nb_tlb) {
            env->CP0_Wired = arg1;
        }
    } else {
        env->CP0_Wired = arg1 % env->tlb->nb_tlb;
    }
}

void helper_mtc0_pwctl(CPUMIPSState *env, target_ulong arg1)
{
#if defined(TARGET_MIPS64)
    /* PWEn = 0. Hardware page table walking is not implemented. */
    env->CP0_PWCtl = (env->CP0_PWCtl & 0x000000C0) | (arg1 & 0x5C00003F);
#else
    env->CP0_PWCtl = (arg1 & 0x800000FF);
#endif
}

void helper_mtc0_srsconf0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf0 |= arg1 & env->CP0_SRSConf0_rw_bitmask;
}

void helper_mtc0_srsconf1(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf1 |= arg1 & env->CP0_SRSConf1_rw_bitmask;
}

void helper_mtc0_srsconf2(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf2 |= arg1 & env->CP0_SRSConf2_rw_bitmask;
}

void helper_mtc0_srsconf3(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf3 |= arg1 & env->CP0_SRSConf3_rw_bitmask;
}

void helper_mtc0_srsconf4(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf4 |= arg1 & env->CP0_SRSConf4_rw_bitmask;
}

void helper_mtc0_hwrena(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0x0000000F;

    if ((env->CP0_Config1 & (1 << CP0C1_PC)) &&
        (env->insn_flags & ISA_MIPS32R6)) {
        mask |= (1 << 4);
    }
    if (env->insn_flags & ISA_MIPS32R6) {
        mask |= (1 << 5);
    }
    if (env->CP0_Config3 & (1 << CP0C3_ULRI)) {
        mask |= (1 << 29);

        if (arg1 & (1 << 29)) {
            env->hflags |= MIPS_HFLAG_HWRENA_ULR;
        } else {
            env->hflags &= ~MIPS_HFLAG_HWRENA_ULR;
        }
    }

    env->CP0_HWREna = arg1 & mask;
}

void helper_mtc0_count(CPUMIPSState *env, target_ulong arg1)
{
    cpu_mips_store_count(env, arg1);
}

void helper_mtc0_saari(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t target = arg1 & 0x3f;
    if (target <= 1) {
        env->CP0_SAARI = target;
    }
}

void helper_mtc0_saar(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t target = env->CP0_SAARI & 0x3f;
    if (target < 2) {
        env->CP0_SAAR[target] = arg1 & 0x00000ffffffff03fULL;
        switch (target) {
        case 0:
            if (env->itu) {
                itc_reconfigure(env->itu);
            }
            break;
        }
    }
}

void helper_mthc0_saar(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t target = env->CP0_SAARI & 0x3f;
    if (target < 2) {
        env->CP0_SAAR[target] =
            (((uint64_t) arg1 << 32) & 0x00000fff00000000ULL) |
            (env->CP0_SAAR[target] & 0x00000000ffffffffULL);
        switch (target) {
        case 0:
            if (env->itu) {
                itc_reconfigure(env->itu);
            }
            break;
        }
    }
}

void helper_mtc0_entryhi(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong old, val, mask;
    mask = (TARGET_PAGE_MASK << 1) | env->CP0_EntryHi_ASID_mask;
    if (((env->CP0_Config4 >> CP0C4_IE) & 0x3) >= 2) {
        mask |= 1 << CP0EnHi_EHINV;
    }

    /* 1k pages not implemented */
#if defined(TARGET_MIPS64)
    if (env->insn_flags & ISA_MIPS32R6) {
        int entryhi_r = extract64(arg1, 62, 2);
        int config0_at = extract32(env->CP0_Config0, 13, 2);
        bool no_supervisor = (env->CP0_Status_rw_bitmask & 0x8) == 0;
        if ((entryhi_r == 2) ||
            (entryhi_r == 1 && (no_supervisor || config0_at == 1))) {
            /* skip EntryHi.R field if new value is reserved */
            mask &= ~(0x3ull << 62);
        }
    }
    mask &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    val = (arg1 & mask) | (old & ~mask);
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        sync_c0_entryhi(env, env->current_tc);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & env->CP0_EntryHi_ASID_mask) !=
        (val & env->CP0_EntryHi_ASID_mask)) {
        tlb_flush(env_cpu(env));
    }
}

void helper_mttc0_entryhi(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    other->CP0_EntryHi = arg1;
    sync_c0_entryhi(other, other_tc);
}

void helper_mtc0_compare(CPUMIPSState *env, target_ulong arg1)
{
    cpu_mips_store_compare(env, arg1);
}

void helper_mtc0_status(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t val, old;

    old = env->CP0_Status;
    cpu_mips_store_status(env, arg1);
    val = env->CP0_Status;

    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("Status %08x (%08x) => %08x (%08x) Cause %08x",
                old, old & env->CP0_Cause & CP0Ca_IP_mask,
                val, val & env->CP0_Cause & CP0Ca_IP_mask,
                env->CP0_Cause);
        switch (cpu_mmu_index(env, false)) {
        case 3:
            qemu_log(", ERL\n");
            break;
        case MIPS_HFLAG_UM:
            qemu_log(", UM\n");
            break;
        case MIPS_HFLAG_SM:
            qemu_log(", SM\n");
            break;
        case MIPS_HFLAG_KM:
            qemu_log("\n");
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid MMU mode!\n");
            break;
        }
    }
}

void helper_mttc0_status(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = env->CP0_Status_rw_bitmask & ~0xf1000018;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    other->CP0_Status = (other->CP0_Status & ~mask) | (arg1 & mask);
    sync_c0_status(env, other, other_tc);
}

void helper_mtc0_intctl(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000003e0) | (arg1 & 0x000003e0);
}

void helper_mtc0_srsctl(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (arg1 & mask);
}

void helper_mtc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    cpu_mips_store_cause(env, arg1);
}

void helper_mttc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    cpu_mips_store_cause(other, arg1);
}

target_ulong helper_mftc0_epc(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EPC;
}

target_ulong helper_mftc0_ebase(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EBase;
}

void helper_mtc0_ebase(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong mask = 0x3FFFF000 | env->CP0_EBaseWG_rw_bitmask;
    if (arg1 & env->CP0_EBaseWG_rw_bitmask) {
        mask |= ~0x3FFFFFFF;
    }
    env->CP0_EBase = (env->CP0_EBase & ~mask) | (arg1 & mask);
}

void helper_mttc0_ebase(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    target_ulong mask = 0x3FFFF000 | env->CP0_EBaseWG_rw_bitmask;
    if (arg1 & env->CP0_EBaseWG_rw_bitmask) {
        mask |= ~0x3FFFFFFF;
    }
    other->CP0_EBase = (other->CP0_EBase & ~mask) | (arg1 & mask);
}

target_ulong helper_mftc0_configx(CPUMIPSState *env, target_ulong idx)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    switch (idx) {
    case 0: return other->CP0_Config0;
    case 1: return other->CP0_Config1;
    case 2: return other->CP0_Config2;
    case 3: return other->CP0_Config3;
    /* 4 and 5 are reserved.  */
    case 6: return other->CP0_Config6;
    case 7: return other->CP0_Config7;
    default:
        break;
    }
    return 0;
}

void helper_mtc0_config0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config0 = (env->CP0_Config0 & 0x81FFFFF8) | (arg1 & 0x00000007);
}

void helper_mtc0_config2(CPUMIPSState *env, target_ulong arg1)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
}

void helper_mtc0_config3(CPUMIPSState *env, target_ulong arg1)
{
    if (env->insn_flags & ASE_MICROMIPS) {
        env->CP0_Config3 = (env->CP0_Config3 & ~(1 << CP0C3_ISA_ON_EXC)) |
                           (arg1 & (1 << CP0C3_ISA_ON_EXC));
    }
}

void helper_mtc0_config4(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config4 = (env->CP0_Config4 & (~env->CP0_Config4_rw_bitmask)) |
                       (arg1 & env->CP0_Config4_rw_bitmask);
}

void helper_mtc0_config5(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config5 = (env->CP0_Config5 & (~env->CP0_Config5_rw_bitmask)) |
                       (arg1 & env->CP0_Config5_rw_bitmask);
    env->CP0_EntryHi_ASID_mask = (env->CP0_Config5 & (1 << CP0C5_MI)) ?
            0x0 : (env->CP0_Config4 & (1 << CP0C4_AE)) ? 0x3ff : 0xff;
    compute_hflags(env);
}

void helper_mtc0_lladdr(CPUMIPSState *env, target_ulong arg1)
{
    target_long mask = env->CP0_LLAddr_rw_bitmask;
    arg1 = arg1 << env->CP0_LLAddr_shift;
    env->CP0_LLAddr = (env->CP0_LLAddr & ~mask) | (arg1 & mask);
}

#define MTC0_MAAR_MASK(env) \
        ((0x1ULL << 63) | ((env->PAMask >> 4) & ~0xFFFull) | 0x3)

void helper_mtc0_maar(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_MAAR[env->CP0_MAARI] = arg1 & MTC0_MAAR_MASK(env);
}

void helper_mthc0_maar(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_MAAR[env->CP0_MAARI] =
        (((uint64_t) arg1 << 32) & MTC0_MAAR_MASK(env)) |
        (env->CP0_MAAR[env->CP0_MAARI] & 0x00000000ffffffffULL);
}

void helper_mtc0_maari(CPUMIPSState *env, target_ulong arg1)
{
    int index = arg1 & 0x3f;
    if (index == 0x3f) {
        /*
         * Software may write all ones to INDEX to determine the
         *  maximum value supported.
         */
        env->CP0_MAARI = MIPS_MAAR_MAX - 1;
    } else if (index < MIPS_MAAR_MAX) {
        env->CP0_MAARI = index;
    }
    /*
     * Other than the all ones, if the value written is not supported,
     * then INDEX is unchanged from its previous value.
     */
}

void helper_mtc0_watchlo(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    /*
     * Watch exceptions for instructions, data loads, data stores
     * not implemented.
     */
    env->CP0_WatchLo[sel] = (arg1 & ~0x7);
}

void helper_mtc0_watchhi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    uint64_t mask = 0x40000FF8 | (env->CP0_EntryHi_ASID_mask << CP0WH_ASID);
    if ((env->CP0_Config5 >> CP0C5_MI) & 1) {
        mask |= 0xFFFFFFFF00000000ULL; /* MMID */
    }
    env->CP0_WatchHi[sel] = arg1 & mask;
    env->CP0_WatchHi[sel] &= ~(env->CP0_WatchHi[sel] & arg1 & 0x7);
}

void helper_mthc0_watchhi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    env->CP0_WatchHi[sel] = ((uint64_t) (arg1) << 32) |
                            (env->CP0_WatchHi[sel] & 0x00000000ffffffffULL);
}

void helper_mtc0_xcontext(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong mask = (1ULL << (env->SEGBITS - 7)) - 1;
    env->CP0_XContext = (env->CP0_XContext & mask) | (arg1 & ~mask);
}

void helper_mtc0_framemask(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Framemask = arg1; /* XXX */
}

void helper_mtc0_debug(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (arg1 & 0x13300120);
    if (arg1 & (1 << CP0DB_DM)) {
        env->hflags |= MIPS_HFLAG_DM;
    } else {
        env->hflags &= ~MIPS_HFLAG_DM;
    }
}

void helper_mttc0_debug(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t val = arg1 & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt));
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    /* XXX: Might be wrong, check with EJTAG spec. */
    if (other_tc == other->current_tc) {
        other->active_tc.CP0_Debug_tcstatus = val;
    } else {
        other->tcs[other_tc].CP0_Debug_tcstatus = val;
    }
    other->CP0_Debug = (other->CP0_Debug &
                     ((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
                     (arg1 & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

void helper_mtc0_performance0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Performance0 = arg1 & 0x000007ff;
}

void helper_mtc0_errctl(CPUMIPSState *env, target_ulong arg1)
{
    int32_t wst = arg1 & (1 << CP0EC_WST);
    int32_t spr = arg1 & (1 << CP0EC_SPR);
    int32_t itc = env->itc_tag ? (arg1 & (1 << CP0EC_ITC)) : 0;

    env->CP0_ErrCtl = wst | spr | itc;

    if (itc && !wst && !spr) {
        env->hflags |= MIPS_HFLAG_ITC_CACHE;
    } else {
        env->hflags &= ~MIPS_HFLAG_ITC_CACHE;
    }
}

void helper_mtc0_taglo(CPUMIPSState *env, target_ulong arg1)
{
    if (env->hflags & MIPS_HFLAG_ITC_CACHE) {
        /*
         * If CACHE instruction is configured for ITC tags then make all
         * CP0.TagLo bits writable. The actual write to ITC Configuration
         * Tag will take care of the read-only bits.
         */
        env->CP0_TagLo = arg1;
    } else {
        env->CP0_TagLo = arg1 & 0xFFFFFCF6;
    }
}

void helper_mtc0_datalo(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_DataLo = arg1; /* XXX */
}

void helper_mtc0_taghi(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_TagHi = arg1; /* XXX */
}

void helper_mtc0_datahi(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_DataHi = arg1; /* XXX */
}

/* MIPS MT functions */
target_ulong helper_mftgpr(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.gpr[sel];
    } else {
        return other->tcs[other_tc].gpr[sel];
    }
}

target_ulong helper_mftlo(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.LO[sel];
    } else {
        return other->tcs[other_tc].LO[sel];
    }
}

target_ulong helper_mfthi(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.HI[sel];
    } else {
        return other->tcs[other_tc].HI[sel];
    }
}

target_ulong helper_mftacx(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.ACX[sel];
    } else {
        return other->tcs[other_tc].ACX[sel];
    }
}

target_ulong helper_mftdsp(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        return other->active_tc.DSPControl;
    } else {
        return other->tcs[other_tc].DSPControl;
    }
}

void helper_mttgpr(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.gpr[sel] = arg1;
    } else {
        other->tcs[other_tc].gpr[sel] = arg1;
    }
}

void helper_mttlo(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.LO[sel] = arg1;
    } else {
        other->tcs[other_tc].LO[sel] = arg1;
    }
}

void helper_mtthi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.HI[sel] = arg1;
    } else {
        other->tcs[other_tc].HI[sel] = arg1;
    }
}

void helper_mttacx(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.ACX[sel] = arg1;
    } else {
        other->tcs[other_tc].ACX[sel] = arg1;
    }
}

void helper_mttdsp(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.DSPControl = arg1;
    } else {
        other->tcs[other_tc].DSPControl = arg1;
    }
}

/* MIPS MT functions */
target_ulong helper_dmt(void)
{
    /* TODO */
    return 0;
}

target_ulong helper_emt(void)
{
    /* TODO */
    return 0;
}

target_ulong helper_dvpe(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->mvp->CP0_MVPControl;

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);
        /* Turn off all VPEs except the one executing the dvpe.  */
        if (&other_cpu->env != env) {
            other_cpu->env.mvp->CP0_MVPControl &= ~(1 << CP0MVPCo_EVP);
            mips_vpe_sleep(other_cpu);
        }
    }
    return prev;
}

target_ulong helper_evpe(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->mvp->CP0_MVPControl;

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);

        if (&other_cpu->env != env
            /* If the VPE is WFI, don't disturb its sleep.  */
            && !mips_vpe_is_wfi(other_cpu)) {
            /* Enable the VPE.  */
            other_cpu->env.mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
            mips_vpe_wake(other_cpu); /* And wake it up.  */
        }
    }
    return prev;
}
#endif /* !CONFIG_USER_ONLY */

/* R6 Multi-threading */
#ifndef CONFIG_USER_ONLY
target_ulong helper_dvp(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->CP0_VPControl;

    if (!((env->CP0_VPControl >> CP0VPCtl_DIS) & 1)) {
        CPU_FOREACH(other_cs) {
            MIPSCPU *other_cpu = MIPS_CPU(other_cs);
            /* Turn off all VPs except the one executing the dvp. */
            if (&other_cpu->env != env) {
                mips_vpe_sleep(other_cpu);
            }
        }
        env->CP0_VPControl |= (1 << CP0VPCtl_DIS);
    }
    return prev;
}

target_ulong helper_evp(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->CP0_VPControl;

    if ((env->CP0_VPControl >> CP0VPCtl_DIS) & 1) {
        CPU_FOREACH(other_cs) {
            MIPSCPU *other_cpu = MIPS_CPU(other_cs);
            if ((&other_cpu->env != env) && !mips_vp_is_wfi(other_cpu)) {
                /*
                 * If the VP is WFI, don't disturb its sleep.
                 * Otherwise, wake it up.
                 */
                mips_vpe_wake(other_cpu);
            }
        }
        env->CP0_VPControl &= ~(1 << CP0VPCtl_DIS);
    }
    return prev;
}
#endif /* !CONFIG_USER_ONLY */
