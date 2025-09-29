/*
 *  PowerPC exception emulation helpers for QEMU (TCG specific)
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "target/ppc/cpu.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/helper-proto.h"
#include "system/runstate.h"

#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "internal.h"
#include "cpu.h"
#include "trace.h"

/*****************************************************************************/
/* Exceptions processing helpers */

void raise_exception_err_ra(CPUPPCState *env, uint32_t exception,
                            uint32_t error_code, uintptr_t raddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit_restore(cs, raddr);
}

void helper_raise_exception_err(CPUPPCState *env, uint32_t exception,
                                uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

void helper_raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

#ifndef CONFIG_USER_ONLY

static G_NORETURN void raise_exception_err(CPUPPCState *env, uint32_t exception,
                                           uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

static G_NORETURN void raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

#endif /* !CONFIG_USER_ONLY */

void helper_TW(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int32_t)arg1 < (int32_t)arg2 && (flags & 0x10)) ||
                  ((int32_t)arg1 > (int32_t)arg2 && (flags & 0x08)) ||
                  ((int32_t)arg1 == (int32_t)arg2 && (flags & 0x04)) ||
                  ((uint32_t)arg1 < (uint32_t)arg2 && (flags & 0x02)) ||
                  ((uint32_t)arg1 > (uint32_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}

#ifdef TARGET_PPC64
void helper_TD(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int64_t)arg1 < (int64_t)arg2 && (flags & 0x10)) ||
                  ((int64_t)arg1 > (int64_t)arg2 && (flags & 0x08)) ||
                  ((int64_t)arg1 == (int64_t)arg2 && (flags & 0x04)) ||
                  ((uint64_t)arg1 < (uint64_t)arg2 && (flags & 0x02)) ||
                  ((uint64_t)arg1 > (uint64_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}
#endif /* TARGET_PPC64 */

static uint32_t helper_SIMON_LIKE_32_64(uint32_t x, uint64_t key, uint32_t lane)
{
    const uint16_t c = 0xfffc;
    const uint64_t z0 = 0xfa2561cdf44ac398ULL;
    uint16_t z = 0, temp;
    uint16_t k[32], eff_k[32], xleft[33], xright[33], fxleft[32];

    for (int i = 3; i >= 0; i--) {
        k[i] = key & 0xffff;
        key >>= 16;
    }
    xleft[0] = x & 0xffff;
    xright[0] = (x >> 16) & 0xffff;

    for (int i = 0; i < 28; i++) {
        z = (z0 >> (63 - i)) & 1;
        temp = ror16(k[i + 3], 3) ^ k[i + 1];
        k[i + 4] = c ^ z ^ k[i] ^ temp ^ ror16(temp, 1);
    }

    for (int i = 0; i < 8; i++) {
        eff_k[4 * i + 0] = k[4 * i + ((0 + lane) % 4)];
        eff_k[4 * i + 1] = k[4 * i + ((1 + lane) % 4)];
        eff_k[4 * i + 2] = k[4 * i + ((2 + lane) % 4)];
        eff_k[4 * i + 3] = k[4 * i + ((3 + lane) % 4)];
    }

    for (int i = 0; i < 32; i++) {
        fxleft[i] = (rol16(xleft[i], 1) &
            rol16(xleft[i], 8)) ^ rol16(xleft[i], 2);
        xleft[i + 1] = xright[i] ^ fxleft[i] ^ eff_k[i];
        xright[i + 1] = xleft[i];
    }

    return (((uint32_t)xright[32]) << 16) | xleft[32];
}

static uint64_t hash_digest(uint64_t ra, uint64_t rb, uint64_t key)
{
    uint64_t stage0_h = 0ULL, stage0_l = 0ULL;
    uint64_t stage1_h, stage1_l;

    for (int i = 0; i < 4; i++) {
        stage0_h |= ror64(rb & 0xff, 8 * (2 * i + 1));
        stage0_h |= ((ra >> 32) & 0xff) << (8 * 2 * i);
        stage0_l |= ror64((rb >> 32) & 0xff, 8 * (2 * i + 1));
        stage0_l |= (ra & 0xff) << (8 * 2 * i);
        rb >>= 8;
        ra >>= 8;
    }

    stage1_h = (uint64_t)helper_SIMON_LIKE_32_64(stage0_h >> 32, key, 0) << 32;
    stage1_h |= helper_SIMON_LIKE_32_64(stage0_h, key, 1);
    stage1_l = (uint64_t)helper_SIMON_LIKE_32_64(stage0_l >> 32, key, 2) << 32;
    stage1_l |= helper_SIMON_LIKE_32_64(stage0_l, key, 3);

    return stage1_h ^ stage1_l;
}

static void do_hash(CPUPPCState *env, target_ulong ea, target_ulong ra,
                    target_ulong rb, uint64_t key, bool store)
{
    uint64_t calculated_hash = hash_digest(ra, rb, key), loaded_hash;

    if (store) {
        cpu_stq_data_ra(env, ea, calculated_hash, GETPC());
    } else {
        loaded_hash = cpu_ldq_data_ra(env, ea, GETPC());
        if (loaded_hash != calculated_hash) {
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                POWERPC_EXCP_TRAP, GETPC());
        }
    }
}

#include "qemu/guest-random.h"

#ifdef TARGET_PPC64
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    if (env->msr & R_MSR_PR_MASK) {                                           \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PRO_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_HV_MASK)) {                                 \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PNH_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_S_MASK)) {                                  \
        if (!(env->spr[SPR_HDEXCR] & R_HDEXCR_HNU_##dexcr_aspect##_MASK))     \
            return;                                                           \
    }                                                                         \
                                                                              \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#else
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#endif /* TARGET_PPC64 */

HELPER_HASH(HASHST, env->spr[SPR_HASHKEYR], true, NPHIE)
HELPER_HASH(HASHCHK, env->spr[SPR_HASHKEYR], false, NPHIE)
HELPER_HASH(HASHSTP, env->spr[SPR_HASHPKEYR], true, PHIE)
HELPER_HASH(HASHCHKP, env->spr[SPR_HASHPKEYR], false, PHIE)

#ifndef CONFIG_USER_ONLY

void ppc_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);
    uint32_t insn;

    /* Restore state and reload the insn we executed, for filling in DSISR.  */
    cpu_restore_state(cs, retaddr);
    insn = ppc_ldl_code(env, env->nip);

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_4xx:
        env->spr[SPR_40x_DEAR] = vaddr;
        break;
    case POWERPC_MMU_BOOKE:
    case POWERPC_MMU_BOOKE206:
        env->spr[SPR_BOOKE_DEAR] = vaddr;
        break;
    case POWERPC_MMU_REAL:
        if (env->flags & POWERPC_FLAG_PPE42) {
            env->spr[SPR_PPE42_EDR] = vaddr;
            if (access_type == MMU_DATA_STORE) {
                env->spr[SPR_PPE42_ISR] |= PPE42_ISR_ST;
            } else {
                env->spr[SPR_PPE42_ISR] &= ~PPE42_ISR_ST;
            }
        } else {
            env->spr[SPR_DAR] = vaddr;
        }
        break;
    default:
        env->spr[SPR_DAR] = vaddr;
        break;
    }

    cs->exception_index = POWERPC_EXCP_ALIGN;
    env->error_code = insn & 0x03FF0000;
    cpu_loop_exit(cs);
}

void ppc_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr vaddr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);

    switch (env->excp_model) {
#if defined(TARGET_PPC64)
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        /*
         * Machine check codes can be found in processor User Manual or
         * Linux or skiboot source.
         */
        if (access_type == MMU_DATA_LOAD) {
            env->spr[SPR_DAR] = vaddr;
            env->spr[SPR_DSISR] = PPC_BIT(57);
            env->error_code = PPC_BIT(42);

        } else if (access_type == MMU_DATA_STORE) {
            /*
             * MCE for stores in POWER is asynchronous so hardware does
             * not set DAR, but QEMU can do better.
             */
            env->spr[SPR_DAR] = vaddr;
            env->error_code = PPC_BIT(36) | PPC_BIT(43) | PPC_BIT(45);
            env->error_code |= PPC_BIT(42);

        } else { /* Fetch */
            /*
             * is_prefix_insn_excp() tests !PPC_BIT(42) to avoid fetching
             * the instruction, so that must always be clear for fetches.
             */
            env->error_code = PPC_BIT(36) | PPC_BIT(44) | PPC_BIT(45);
        }
        break;
#endif
    default:
        /*
         * TODO: Check behaviour for other CPUs, for now do nothing.
         * Could add a basic MCE even if real hardware ignores.
         */
        return;
    }

    cs->exception_index = POWERPC_EXCP_MCHECK;
    cpu_loop_exit_restore(cs, retaddr);
}

void ppc_cpu_debug_excp_handler(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        if (cs->watchpoint_hit) {
            if (cs->watchpoint_hit->flags & BP_CPU) {
                env->spr[SPR_DAR] = cs->watchpoint_hit->hitaddr;
                env->spr[SPR_DSISR] = PPC_BIT(41);
                cs->watchpoint_hit = NULL;
                raise_exception(env, POWERPC_EXCP_DSI);
            }
            cs->watchpoint_hit = NULL;
        } else if (cpu_breakpoint_test(cs, env->nip, BP_CPU)) {
            raise_exception_err(env, POWERPC_EXCP_TRACE,
                                PPC_BIT(33) | PPC_BIT(43));
        }
    }
#endif
}

bool ppc_cpu_debug_check_breakpoint(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        target_ulong priv;

        priv = env->spr[SPR_CIABR] & PPC_BITMASK(62, 63);
        switch (priv) {
        case 0x1: /* problem */
            return env->msr & ((target_ulong)1 << MSR_PR);
        case 0x2: /* supervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                    !(env->msr & ((target_ulong)1 << MSR_HV)));
        case 0x3: /* hypervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                     (env->msr & ((target_ulong)1 << MSR_HV)));
        default:
            g_assert_not_reached();
        }
    }
#endif

    return false;
}

bool ppc_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);
    bool wt, wti, hv, sv, pr;
    uint32_t dawrx;

    if ((env->insns_flags2 & PPC2_ISA207S) &&
        (wp == env->dawr_watchpoint[0])) {
        dawrx = env->spr[SPR_DAWRX0];
    } else if ((env->insns_flags2 & PPC2_ISA310) &&
               (wp == env->dawr_watchpoint[1])) {
        dawrx = env->spr[SPR_DAWRX1];
    } else {
        return false;
    }

    wt = extract32(dawrx, PPC_BIT_NR(59), 1);
    wti = extract32(dawrx, PPC_BIT_NR(60), 1);
    hv = extract32(dawrx, PPC_BIT_NR(61), 1);
    sv = extract32(dawrx, PPC_BIT_NR(62), 1);
    pr = extract32(dawrx, PPC_BIT_NR(62), 1);

    if ((env->msr & ((target_ulong)1 << MSR_PR)) && !pr) {
        return false;
    } else if ((env->msr & ((target_ulong)1 << MSR_HV)) && !hv) {
        return false;
    } else if (!sv) {
        return false;
    }

    if (!wti) {
        if (env->msr & ((target_ulong)1 << MSR_DR)) {
            return wt;
        } else {
            return !wt;
        }
    }

    return true;
#endif

    return false;
}

/*
 * This stops the machine and logs CPU state without killing QEMU (like
 * cpu_abort()) because it is often a guest error as opposed to a QEMU error,
 * so the machine can still be debugged.
 */
G_NORETURN void powerpc_checkstop(CPUPPCState *env, const char *reason)
{
    CPUState *cs = env_cpu(env);
    FILE *f;

    f = qemu_log_trylock();
    if (f) {
        fprintf(f, "Entering checkstop state: %s\n", reason);
        cpu_dump_state(cs, f, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_unlock(f);
    }

    /*
     * This stops the machine and logs CPU state without killing QEMU
     * (like cpu_abort()) so the machine can still be debugged (because
     * it is often a guest error).
     */
    qemu_system_guest_panicked(NULL);
    cpu_loop_exit_noexc(cs);
}

/* Return true iff byteswap is needed to load instruction */
static inline bool insn_need_byteswap(CPUArchState *env)
{
    /* SYSTEM builds TARGET_BIG_ENDIAN. Need to swap when MSR[LE] is set */
    return !!(env->msr & ((target_ulong)1 << MSR_LE));
}

uint32_t ppc_ldl_code(CPUArchState *env, target_ulong addr)
{
    uint32_t insn = cpu_ldl_code(env, addr);

    if (insn_need_byteswap(env)) {
        insn = bswap32(insn);
    }

    return insn;
}

#if defined(TARGET_PPC64)
void helper_attn(CPUPPCState *env)
{
    /* POWER attn is unprivileged when enabled by HID, otherwise illegal */
    if ((*env->check_attn)(env)) {
        powerpc_checkstop(env, "host executed attn");
    } else {
        raise_exception_err(env, POWERPC_EXCP_HV_EMU,
                            POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL);
    }
}

void helper_scv(CPUPPCState *env, uint32_t lev)
{
    if (env->spr[SPR_FSCR] & (1ull << FSCR_SCV)) {
        raise_exception_err(env, POWERPC_EXCP_SYSCALL_VECTORED, lev);
    } else {
        raise_exception_err(env, POWERPC_EXCP_FU, FSCR_IC_SCV);
    }
}

void helper_pminsn(CPUPPCState *env, uint32_t insn)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;

    /* Condition for waking up at 0x100 */
    env->resume_as_sreset = (insn != PPC_PM_STOP) ||
        (env->spr[SPR_PSSCR] & PSSCR_EC);

    /* HDECR is not to wake from PM state, it may have already fired */
    if (env->resume_as_sreset) {
        PowerPCCPU *cpu = env_archcpu(env);
        ppc_set_irq(cpu, PPC_INTERRUPT_HDECR, 0);
    }

    ppc_maybe_interrupt(env);
}

#endif /* TARGET_PPC64 */
void helper_store_msr(CPUPPCState *env, target_ulong val)
{
    uint32_t excp = hreg_store_msr(env, val, 0);

    if (excp != 0) {
        cpu_interrupt_exittb(env_cpu(env));
        raise_exception(env, excp);
    }
}

void helper_ppc_maybe_interrupt(CPUPPCState *env)
{
    ppc_maybe_interrupt(env);
}

static void do_rfi(CPUPPCState *env, target_ulong nip, target_ulong msr)
{
    /* MSR:POW cannot be set by any form of rfi */
    msr &= ~(1ULL << MSR_POW);

    /* MSR:TGPR cannot be set by any form of rfi */
    if (env->flags & POWERPC_FLAG_TGPR) {
        msr &= ~(1ULL << MSR_TGPR);
    }

#ifdef TARGET_PPC64
    /* Switching to 32-bit ? Crop the nip */
    if (!msr_is_64bit(env, msr)) {
        nip = (uint32_t)nip;
    }
#else
    nip = (uint32_t)nip;
#endif
    /* XXX: beware: this is false if VLE is supported */
    env->nip = nip & ~((target_ulong)0x00000003);
    hreg_store_msr(env, msr, 1);
    trace_ppc_excp_rfi(env->nip, env->msr);
    /*
     * No need to raise an exception here, as rfi is always the last
     * insn of a TB
     */
    cpu_interrupt_exittb(env_cpu(env));
    /* Reset the reservation */
    env->reserve_addr = -1;

    /* Context synchronizing: check if TCG TLB needs flush */
    check_tlb_flush(env, false);
}

void helper_rfi(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_SRR0], env->spr[SPR_SRR1] & 0xfffffffful);
}

#ifdef TARGET_PPC64
void helper_rfid(CPUPPCState *env)
{
    /*
     * The architecture defines a number of rules for which bits can
     * change but in practice, we handle this in hreg_store_msr()
     * which will be called by do_rfi(), so there is no need to filter
     * here
     */
    do_rfi(env, env->spr[SPR_SRR0], env->spr[SPR_SRR1]);
}

void helper_rfscv(CPUPPCState *env)
{
    do_rfi(env, env->lr, env->ctr);
}

void helper_hrfid(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_HSRR0], env->spr[SPR_HSRR1]);
}

void helper_rfebb(CPUPPCState *env, target_ulong s)
{
    target_ulong msr = env->msr;

    /*
     * Handling of BESCR bits 32:33 according to PowerISA v3.1:
     *
     * "If BESCR 32:33 != 0b00 the instruction is treated as if
     *  the instruction form were invalid."
     */
    if (env->spr[SPR_BESCR] & BESCR_INVALID) {
        raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                            POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL);
    }

    env->nip = env->spr[SPR_EBBRR];

    /* Switching to 32-bit ? Crop the nip */
    if (!msr_is_64bit(env, msr)) {
        env->nip = (uint32_t)env->spr[SPR_EBBRR];
    }

    if (s) {
        env->spr[SPR_BESCR] |= BESCR_GE;
    } else {
        env->spr[SPR_BESCR] &= ~BESCR_GE;
    }
}

/*
 * Triggers or queues an 'ebb_excp' EBB exception. All checks
 * but FSCR, HFSCR and msr_pr must be done beforehand.
 *
 * PowerISA v3.1 isn't clear about whether an EBB should be
 * postponed or cancelled if the EBB facility is unavailable.
 * Our assumption here is that the EBB is cancelled if both
 * FSCR and HFSCR EBB facilities aren't available.
 */
static void do_ebb(CPUPPCState *env, int ebb_excp)
{
    PowerPCCPU *cpu = env_archcpu(env);

    /*
     * FSCR_EBB and FSCR_IC_EBB are the same bits used with
     * HFSCR.
     */
    helper_fscr_facility_check(env, FSCR_EBB, 0, FSCR_IC_EBB);
    helper_hfscr_facility_check(env, FSCR_EBB, "EBB", FSCR_IC_EBB);

    if (ebb_excp == POWERPC_EXCP_PERFM_EBB) {
        env->spr[SPR_BESCR] |= BESCR_PMEO;
    } else if (ebb_excp == POWERPC_EXCP_EXTERNAL_EBB) {
        env->spr[SPR_BESCR] |= BESCR_EEO;
    }

    if (FIELD_EX64(env->msr, MSR, PR)) {
        powerpc_excp(cpu, ebb_excp);
    } else {
        ppc_set_irq(cpu, PPC_INTERRUPT_EBB, 1);
    }
}

void raise_ebb_perfm_exception(CPUPPCState *env)
{
    bool perfm_ebb_enabled = env->spr[SPR_POWER_MMCR0] & MMCR0_EBE &&
                             env->spr[SPR_BESCR] & BESCR_PME &&
                             env->spr[SPR_BESCR] & BESCR_GE;

    if (!perfm_ebb_enabled) {
        return;
    }

    do_ebb(env, POWERPC_EXCP_PERFM_EBB);
}
#endif /* TARGET_PPC64 */

/*****************************************************************************/
/* Embedded PowerPC specific helpers */
void helper_40x_rfci(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_40x_SRR2], env->spr[SPR_40x_SRR3]);
}

void helper_rfci(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_BOOKE_CSRR0], env->spr[SPR_BOOKE_CSRR1]);
}

void helper_rfdi(CPUPPCState *env)
{
    /* FIXME: choose CSRR1 or DSRR1 based on cpu type */
    do_rfi(env, env->spr[SPR_BOOKE_DSRR0], env->spr[SPR_BOOKE_DSRR1]);
}

void helper_rfmci(CPUPPCState *env)
{
    /* FIXME: choose CSRR1 or MCSRR1 based on cpu type */
    do_rfi(env, env->spr[SPR_BOOKE_MCSRR0], env->spr[SPR_BOOKE_MCSRR1]);
}

/* Embedded.Processor Control */
static int dbell2irq(target_ulong rb)
{
    int msg = rb & DBELL_TYPE_MASK;
    int irq = -1;

    switch (msg) {
    case DBELL_TYPE_DBELL:
        irq = PPC_INTERRUPT_DOORBELL;
        break;
    case DBELL_TYPE_DBELL_CRIT:
        irq = PPC_INTERRUPT_CDOORBELL;
        break;
    case DBELL_TYPE_G_DBELL:
    case DBELL_TYPE_G_DBELL_CRIT:
    case DBELL_TYPE_G_DBELL_MC:
        /* XXX implement */
    default:
        break;
    }

    return irq;
}

void helper_msgclr(CPUPPCState *env, target_ulong rb)
{
    int irq = dbell2irq(rb);

    if (irq < 0) {
        return;
    }

    ppc_set_irq(env_archcpu(env), irq, 0);
}

void helper_msgsnd(target_ulong rb)
{
    int irq = dbell2irq(rb);
    int pir = rb & DBELL_PIRTAG_MASK;
    CPUState *cs;

    if (irq < 0) {
        return;
    }

    bql_lock();
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        CPUPPCState *cenv = &cpu->env;

        if ((rb & DBELL_BRDCAST_MASK) || (cenv->spr[SPR_BOOKE_PIR] == pir)) {
            ppc_set_irq(cpu, irq, 1);
        }
    }
    bql_unlock();
}

/* Server Processor Control */

static bool dbell_type_server(target_ulong rb)
{
    /*
     * A Directed Hypervisor Doorbell message is sent only if the
     * message type is 5. All other types are reserved and the
     * instruction is a no-op
     */
    return (rb & DBELL_TYPE_MASK) == DBELL_TYPE_DBELL_SERVER;
}

static inline bool dbell_bcast_core(target_ulong rb)
{
    return (rb & DBELL_BRDCAST_MASK) == DBELL_BRDCAST_CORE;
}

static inline bool dbell_bcast_subproc(target_ulong rb)
{
    return (rb & DBELL_BRDCAST_MASK) == DBELL_BRDCAST_SUBPROC;
}

/*
 * Send an interrupt to a thread in the same core as env).
 */
static void msgsnd_core_tir(CPUPPCState *env, uint32_t target_tir, int irq)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (ppc_cpu_lpar_single_threaded(cs)) {
        if (target_tir == 0) {
            ppc_set_irq(cpu, irq, 1);
        }
    } else {
        CPUState *ccs;

        /* Does iothread need to be locked for walking CPU list? */
        bql_lock();
        THREAD_SIBLING_FOREACH(cs, ccs) {
            PowerPCCPU *ccpu = POWERPC_CPU(ccs);
            if (target_tir == ppc_cpu_tir(ccpu)) {
                ppc_set_irq(ccpu, irq, 1);
                break;
            }
        }
        bql_unlock();
    }
}

void helper_book3s_msgclr(CPUPPCState *env, target_ulong rb)
{
    if (!dbell_type_server(rb)) {
        return;
    }

    ppc_set_irq(env_archcpu(env), PPC_INTERRUPT_HDOORBELL, 0);
}

void helper_book3s_msgsnd(CPUPPCState *env, target_ulong rb)
{
    int pir = rb & DBELL_PROCIDTAG_MASK;
    bool brdcast = false;
    CPUState *cs, *ccs;
    PowerPCCPU *cpu;

    if (!dbell_type_server(rb)) {
        return;
    }

    /* POWER8 msgsnd is like msgsndp (targets a thread within core) */
    if (!(env->insns_flags2 & PPC2_ISA300)) {
        msgsnd_core_tir(env, rb & PPC_BITMASK(57, 63), PPC_INTERRUPT_HDOORBELL);
        return;
    }

    /* POWER9 and later msgsnd is a global (targets any thread) */
    cpu = ppc_get_vcpu_by_pir(pir);
    if (!cpu) {
        return;
    }
    cs = CPU(cpu);

    if (dbell_bcast_core(rb) || (dbell_bcast_subproc(rb) &&
                                 (env->flags & POWERPC_FLAG_SMT_1LPAR))) {
        brdcast = true;
    }

    if (ppc_cpu_core_single_threaded(cs) || !brdcast) {
        ppc_set_irq(cpu, PPC_INTERRUPT_HDOORBELL, 1);
        return;
    }

    /*
     * Why is bql needed for walking CPU list? Answer seems to be because ppc
     * irq handling needs it, but ppc_set_irq takes the lock itself if needed,
     * so could this be removed?
     */
    bql_lock();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        ppc_set_irq(POWERPC_CPU(ccs), PPC_INTERRUPT_HDOORBELL, 1);
    }
    bql_unlock();
}

#ifdef TARGET_PPC64
void helper_book3s_msgclrp(CPUPPCState *env, target_ulong rb)
{
    helper_hfscr_facility_check(env, HFSCR_MSGP, "msgclrp", HFSCR_IC_MSGP);

    if (!dbell_type_server(rb)) {
        return;
    }

    ppc_set_irq(env_archcpu(env), PPC_INTERRUPT_DOORBELL, 0);
}

/*
 * sends a message to another thread  on the same
 * multi-threaded processor
 */
void helper_book3s_msgsndp(CPUPPCState *env, target_ulong rb)
{
    helper_hfscr_facility_check(env, HFSCR_MSGP, "msgsndp", HFSCR_IC_MSGP);

    if (!dbell_type_server(rb)) {
        return;
    }

    msgsnd_core_tir(env, rb & PPC_BITMASK(57, 63), PPC_INTERRUPT_DOORBELL);
}
#endif /* TARGET_PPC64 */

/* Single-step tracing */
void helper_book3s_trace(CPUPPCState *env, target_ulong prev_ip)
{
    uint32_t error_code = 0;
    if (env->insns_flags2 & PPC2_ISA207S) {
        /* Load/store reporting, SRR1[35, 36] and SDAR, are not implemented. */
        env->spr[SPR_POWER_SIAR] = prev_ip;
        error_code = PPC_BIT(33);
    }
    raise_exception_err(env, POWERPC_EXCP_TRACE, error_code);
}
#endif /* !CONFIG_USER_ONLY */
