/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "gdbstub/helpers.h"
#include "exec/helper-proto.h"
#include "qemu/main-loop.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "exec/exec-all.h"
#ifdef CONFIG_TCG
#include "exec/cpu_ldst.h"
#include "semihosting/common-semi.h"
#endif
#if !defined(CONFIG_USER_ONLY)
#include "hw/intc/armv7m_nvic.h"
#endif

static void v7m_msr_xpsr(CPUARMState *env, uint32_t mask,
                         uint32_t reg, uint32_t val)
{
    /* Only APSR is actually writable */
    if (!(reg & 4)) {
        uint32_t apsrmask = 0;

        if (mask & 8) {
            apsrmask |= XPSR_NZCV | XPSR_Q;
        }
        if ((mask & 4) && arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
            apsrmask |= XPSR_GE;
        }
        xpsr_write(env, val, apsrmask);
    }
}

static uint32_t v7m_mrs_xpsr(CPUARMState *env, uint32_t reg, unsigned el)
{
    uint32_t mask = 0;

    if ((reg & 1) && el) {
        mask |= XPSR_EXCP; /* IPSR (unpriv. reads as zero) */
    }
    if (!(reg & 4)) {
        mask |= XPSR_NZCV | XPSR_Q; /* APSR */
        if (arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
            mask |= XPSR_GE;
        }
    }
    /* EPSR reads as zero */
    return xpsr_read(env) & mask;
}

uint32_t arm_v7m_mrs_control(CPUARMState *env, uint32_t secure)
{
    uint32_t value = env->v7m.control[secure];

    if (!secure) {
        /* SFPA is RAZ/WI from NS; FPCA is stored in the M_REG_S bank */
        value |= env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK;
    }
    return value;
}

#ifdef CONFIG_USER_ONLY

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    uint32_t mask = extract32(maskreg, 8, 4);
    uint32_t reg = extract32(maskreg, 0, 8);

    switch (reg) {
    case 0 ... 7: /* xPSR sub-fields */
        v7m_msr_xpsr(env, mask, reg, val);
        break;
    case 20: /* CONTROL */
        /* There are no sub-fields that are actually writable from EL0. */
        break;
    default:
        /* Unprivileged writes to other registers are ignored */
        break;
    }
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    switch (reg) {
    case 0 ... 7: /* xPSR sub-fields */
        return v7m_mrs_xpsr(env, reg, 0);
    case 20: /* CONTROL */
        return arm_v7m_mrs_control(env, 0);
    default:
        /* Unprivileged reads others as zero.  */
        return 0;
    }
}

void HELPER(v7m_bxns)(CPUARMState *env, uint32_t dest)
{
    /* translate.c should never generate calls here in user-only mode */
    g_assert_not_reached();
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{
    /* translate.c should never generate calls here in user-only mode */
    g_assert_not_reached();
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    /* translate.c should never generate calls here in user-only mode */
    g_assert_not_reached();
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    /* translate.c should never generate calls here in user-only mode */
    g_assert_not_reached();
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    /* translate.c should never generate calls here in user-only mode */
    g_assert_not_reached();
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    /*
     * The TT instructions can be used by unprivileged code, but in
     * user-only emulation we don't have the MPU.
     * Luckily since we know we are NonSecure unprivileged (and that in
     * turn means that the A flag wasn't specified), all the bits in the
     * register must be zero:
     *  IREGION: 0 because IRVALID is 0
     *  IRVALID: 0 because NS
     *  S: 0 because NS
     *  NSRW: 0 because NS
     *  NSR: 0 because NS
     *  RW: 0 because unpriv and A flag not set
     *  R: 0 because unpriv and A flag not set
     *  SRVALID: 0 because NS
     *  MRVALID: 0 because unpriv and A flag not set
     *  SREGION: 0 because SRVALID is 0
     *  MREGION: 0 because MRVALID is 0
     */
    return 0;
}

ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    return ARMMMUIdx_MUser;
}

#else /* !CONFIG_USER_ONLY */

static ARMMMUIdx arm_v7m_mmu_idx_all(CPUARMState *env,
                                     bool secstate, bool priv, bool negpri)
{
    ARMMMUIdx mmu_idx = ARM_MMU_IDX_M;

    if (priv) {
        mmu_idx |= ARM_MMU_IDX_M_PRIV;
    }

    if (negpri) {
        mmu_idx |= ARM_MMU_IDX_M_NEGPRI;
    }

    if (secstate) {
        mmu_idx |= ARM_MMU_IDX_M_S;
    }

    return mmu_idx;
}

static ARMMMUIdx arm_v7m_mmu_idx_for_secstate_and_priv(CPUARMState *env,
                                                       bool secstate, bool priv)
{
    bool negpri = armv7m_nvic_neg_prio_requested(env->nvic, secstate);

    return arm_v7m_mmu_idx_all(env, secstate, priv, negpri);
}

/* Return the MMU index for a v7M CPU in the specified security state */
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    bool priv = arm_v7m_is_handler_mode(env) ||
        !(env->v7m.control[secstate] & 1);

    return arm_v7m_mmu_idx_for_secstate_and_priv(env, secstate, priv);
}

/*
 * What kind of stack write are we doing? This affects how exceptions
 * generated during the stacking are treated.
 */
typedef enum StackingMode {
    STACK_NORMAL,
    STACK_IGNFAULTS,
    STACK_LAZYFP,
} StackingMode;

static bool v7m_stack_write(ARMCPU *cpu, uint32_t addr, uint32_t value,
                            ARMMMUIdx mmu_idx, StackingMode mode)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    bool secure = mmu_idx & ARM_MMU_IDX_M_S;
    int exc;
    bool exc_secure;

    if (get_phys_addr(env, addr, MMU_DATA_STORE, mmu_idx, &res, &fi)) {
        /* MPU/SAU lookup failed */
        if (fi.type == ARMFault_QEMU_SFault) {
            if (mode == STACK_LAZYFP) {
                qemu_log_mask(CPU_LOG_INT,
                              "...SecureFault with SFSR.LSPERR "
                              "during lazy stacking\n");
                env->v7m.sfsr |= R_V7M_SFSR_LSPERR_MASK;
            } else {
                qemu_log_mask(CPU_LOG_INT,
                              "...SecureFault with SFSR.AUVIOL "
                              "during stacking\n");
                env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK;
            }
            env->v7m.sfsr |= R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            exc = ARMV7M_EXCP_SECURE;
            exc_secure = false;
        } else {
            if (mode == STACK_LAZYFP) {
                qemu_log_mask(CPU_LOG_INT,
                              "...MemManageFault with CFSR.MLSPERR\n");
                env->v7m.cfsr[secure] |= R_V7M_CFSR_MLSPERR_MASK;
            } else {
                qemu_log_mask(CPU_LOG_INT,
                              "...MemManageFault with CFSR.MSTKERR\n");
                env->v7m.cfsr[secure] |= R_V7M_CFSR_MSTKERR_MASK;
            }
            exc = ARMV7M_EXCP_MEM;
            exc_secure = secure;
        }
        goto pend_fault;
    }
    address_space_stl_le(arm_addressspace(cs, res.f.attrs), res.f.phys_addr,
                         value, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        /* BusFault trying to write the data */
        if (mode == STACK_LAZYFP) {
            qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.LSPERR\n");
            env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_LSPERR_MASK;
        } else {
            qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.STKERR\n");
            env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_STKERR_MASK;
        }
        exc = ARMV7M_EXCP_BUS;
        exc_secure = false;
        goto pend_fault;
    }
    return true;

pend_fault:
    /*
     * By pending the exception at this point we are making
     * the IMPDEF choice "overridden exceptions pended" (see the
     * MergeExcInfo() pseudocode). The other choice would be to not
     * pend them now and then make a choice about which to throw away
     * later if we have two derived exceptions.
     * The only case when we must not pend the exception but instead
     * throw it away is if we are doing the push of the callee registers
     * and we've already generated a derived exception (this is indicated
     * by the caller passing STACK_IGNFAULTS). Even in this case we will
     * still update the fault status registers.
     */
    switch (mode) {
    case STACK_NORMAL:
        armv7m_nvic_set_pending_derived(env->nvic, exc, exc_secure);
        break;
    case STACK_LAZYFP:
        armv7m_nvic_set_pending_lazyfp(env->nvic, exc, exc_secure);
        break;
    case STACK_IGNFAULTS:
        break;
    }
    return false;
}

static bool v7m_stack_read(ARMCPU *cpu, uint32_t *dest, uint32_t addr,
                           ARMMMUIdx mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    bool secure = mmu_idx & ARM_MMU_IDX_M_S;
    int exc;
    bool exc_secure;
    uint32_t value;

    if (get_phys_addr(env, addr, MMU_DATA_LOAD, mmu_idx, &res, &fi)) {
        /* MPU/SAU lookup failed */
        if (fi.type == ARMFault_QEMU_SFault) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault with SFSR.AUVIOL during unstack\n");
            env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK | R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            exc = ARMV7M_EXCP_SECURE;
            exc_secure = false;
        } else {
            qemu_log_mask(CPU_LOG_INT,
                          "...MemManageFault with CFSR.MUNSTKERR\n");
            env->v7m.cfsr[secure] |= R_V7M_CFSR_MUNSTKERR_MASK;
            exc = ARMV7M_EXCP_MEM;
            exc_secure = secure;
        }
        goto pend_fault;
    }

    value = address_space_ldl(arm_addressspace(cs, res.f.attrs),
                              res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        /* BusFault trying to read the data */
        qemu_log_mask(CPU_LOG_INT, "...BusFault with BFSR.UNSTKERR\n");
        env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_UNSTKERR_MASK;
        exc = ARMV7M_EXCP_BUS;
        exc_secure = false;
        goto pend_fault;
    }

    *dest = value;
    return true;

pend_fault:
    /*
     * By pending the exception at this point we are making
     * the IMPDEF choice "overridden exceptions pended" (see the
     * MergeExcInfo() pseudocode). The other choice would be to not
     * pend them now and then make a choice about which to throw away
     * later if we have two derived exceptions.
     */
    armv7m_nvic_set_pending(env->nvic, exc, exc_secure);
    return false;
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    /*
     * Preserve FP state (because LSPACT was set and we are about
     * to execute an FP instruction). This corresponds to the
     * PreserveFPState() pseudocode.
     * We may throw an exception if the stacking fails.
     */
    ARMCPU *cpu = env_archcpu(env);
    bool is_secure = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
    bool negpri = !(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_HFRDY_MASK);
    bool is_priv = !(env->v7m.fpccr[is_secure] & R_V7M_FPCCR_USER_MASK);
    bool splimviol = env->v7m.fpccr[is_secure] & R_V7M_FPCCR_SPLIMVIOL_MASK;
    uint32_t fpcar = env->v7m.fpcar[is_secure];
    bool stacked_ok = true;
    bool ts = is_secure && (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK);
    bool take_exception;

    /* Take the BQL as we are going to touch the NVIC */
    bql_lock();

    /* Check the background context had access to the FPU */
    if (!v7m_cpacr_pass(env, is_secure, is_priv)) {
        armv7m_nvic_set_pending_lazyfp(env->nvic, ARMV7M_EXCP_USAGE, is_secure);
        env->v7m.cfsr[is_secure] |= R_V7M_CFSR_NOCP_MASK;
        stacked_ok = false;
    } else if (!is_secure && !extract32(env->v7m.nsacr, 10, 1)) {
        armv7m_nvic_set_pending_lazyfp(env->nvic, ARMV7M_EXCP_USAGE, M_REG_S);
        env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
        stacked_ok = false;
    }

    if (!splimviol && stacked_ok) {
        /* We only stack if the stack limit wasn't violated */
        int i;
        ARMMMUIdx mmu_idx;

        mmu_idx = arm_v7m_mmu_idx_all(env, is_secure, is_priv, negpri);
        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint64_t dn = *aa32_vfp_dreg(env, i / 2);
            uint32_t faddr = fpcar + 4 * i;
            uint32_t slo = extract64(dn, 0, 32);
            uint32_t shi = extract64(dn, 32, 32);

            if (i >= 16) {
                faddr += 8; /* skip the slot for the FPSCR/VPR */
            }
            stacked_ok = stacked_ok &&
                v7m_stack_write(cpu, faddr, slo, mmu_idx, STACK_LAZYFP) &&
                v7m_stack_write(cpu, faddr + 4, shi, mmu_idx, STACK_LAZYFP);
        }

        stacked_ok = stacked_ok &&
            v7m_stack_write(cpu, fpcar + 0x40,
                            vfp_get_fpscr(env), mmu_idx, STACK_LAZYFP);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            stacked_ok = stacked_ok &&
                v7m_stack_write(cpu, fpcar + 0x44,
                                env->v7m.vpr, mmu_idx, STACK_LAZYFP);
        }
    }

    /*
     * We definitely pended an exception, but it's possible that it
     * might not be able to be taken now. If its priority permits us
     * to take it now, then we must not update the LSPACT or FP regs,
     * but instead jump out to take the exception immediately.
     * If it's just pending and won't be taken until the current
     * handler exits, then we do update LSPACT and the FP regs.
     */
    take_exception = !stacked_ok &&
        armv7m_nvic_can_take_pending_exception(env->nvic);

    bql_unlock();

    if (take_exception) {
        raise_exception_ra(env, EXCP_LAZYFP, 0, 1, GETPC());
    }

    env->v7m.fpccr[is_secure] &= ~R_V7M_FPCCR_LSPACT_MASK;

    if (ts) {
        /* Clear s0 to s31 and the FPSCR and VPR */
        int i;

        for (i = 0; i < 32; i += 2) {
            *aa32_vfp_dreg(env, i / 2) = 0;
        }
        vfp_set_fpscr(env, 0);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            env->v7m.vpr = 0;
        }
    }
    /*
     * Otherwise s0 to s15, FPSCR and VPR are UNKNOWN; we choose to leave them
     * unchanged.
     */
}

/*
 * Write to v7M CONTROL.SPSEL bit for the specified security bank.
 * This may change the current stack pointer between Main and Process
 * stack pointers if it is done for the CONTROL register for the current
 * security state.
 */
static void write_v7m_control_spsel_for_secstate(CPUARMState *env,
                                                 bool new_spsel,
                                                 bool secstate)
{
    bool old_is_psp = v7m_using_psp(env);

    env->v7m.control[secstate] =
        deposit32(env->v7m.control[secstate],
                  R_V7M_CONTROL_SPSEL_SHIFT,
                  R_V7M_CONTROL_SPSEL_LENGTH, new_spsel);

    if (secstate == env->v7m.secure) {
        bool new_is_psp = v7m_using_psp(env);
        uint32_t tmp;

        if (old_is_psp != new_is_psp) {
            tmp = env->v7m.other_sp;
            env->v7m.other_sp = env->regs[13];
            env->regs[13] = tmp;
        }
    }
}

/*
 * Write to v7M CONTROL.SPSEL bit. This may change the current
 * stack pointer between Main and Process stack pointers.
 */
static void write_v7m_control_spsel(CPUARMState *env, bool new_spsel)
{
    write_v7m_control_spsel_for_secstate(env, new_spsel, env->v7m.secure);
}

void write_v7m_exception(CPUARMState *env, uint32_t new_exc)
{
    /*
     * Write a new value to v7m.exception, thus transitioning into or out
     * of Handler mode; this may result in a change of active stack pointer.
     */
    bool new_is_psp, old_is_psp = v7m_using_psp(env);
    uint32_t tmp;

    env->v7m.exception = new_exc;

    new_is_psp = v7m_using_psp(env);

    if (old_is_psp != new_is_psp) {
        tmp = env->v7m.other_sp;
        env->v7m.other_sp = env->regs[13];
        env->regs[13] = tmp;
    }
}

/* Switch M profile security state between NS and S */
static void switch_v7m_security_state(CPUARMState *env, bool new_secstate)
{
    uint32_t new_ss_msp, new_ss_psp;

    if (env->v7m.secure == new_secstate) {
        return;
    }

    /*
     * All the banked state is accessed by looking at env->v7m.secure
     * except for the stack pointer; rearrange the SP appropriately.
     */
    new_ss_msp = env->v7m.other_ss_msp;
    new_ss_psp = env->v7m.other_ss_psp;

    if (v7m_using_psp(env)) {
        env->v7m.other_ss_psp = env->regs[13];
        env->v7m.other_ss_msp = env->v7m.other_sp;
    } else {
        env->v7m.other_ss_msp = env->regs[13];
        env->v7m.other_ss_psp = env->v7m.other_sp;
    }

    env->v7m.secure = new_secstate;

    if (v7m_using_psp(env)) {
        env->regs[13] = new_ss_psp;
        env->v7m.other_sp = new_ss_msp;
    } else {
        env->regs[13] = new_ss_msp;
        env->v7m.other_sp = new_ss_psp;
    }
}

void HELPER(v7m_bxns)(CPUARMState *env, uint32_t dest)
{
    /*
     * Handle v7M BXNS:
     *  - if the return value is a magic value, do exception return (like BX)
     *  - otherwise bit 0 of the return value is the target security state
     */
    uint32_t min_magic;

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        /* Covers FNC_RETURN and EXC_RETURN magic */
        min_magic = FNC_RETURN_MIN_MAGIC;
    } else {
        /* EXC_RETURN magic only */
        min_magic = EXC_RETURN_MIN_MAGIC;
    }

    if (dest >= min_magic) {
        /*
         * This is an exception return magic value; put it where
         * do_v7m_exception_exit() expects and raise EXCEPTION_EXIT.
         * Note that if we ever add gen_ss_advance() singlestep support to
         * M profile this should count as an "instruction execution complete"
         * event (compare gen_bx_excret_final_code()).
         */
        env->regs[15] = dest & ~1;
        env->thumb = dest & 1;
        HELPER(exception_internal)(env, EXCP_EXCEPTION_EXIT);
        /* notreached */
    }

    /* translate.c should have made BXNS UNDEF unless we're secure */
    assert(env->v7m.secure);

    if (!(dest & 1)) {
        env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    }
    switch_v7m_security_state(env, dest & 1);
    env->thumb = true;
    env->regs[15] = dest & ~1;
    arm_rebuild_hflags(env);
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{
    /*
     * Handle v7M BLXNS:
     *  - bit 0 of the destination address is the target security state
     */

    /* At this point regs[15] is the address just after the BLXNS */
    uint32_t nextinst = env->regs[15] | 1;
    uint32_t sp = env->regs[13] - 8;
    uint32_t saved_psr;

    /* translate.c will have made BLXNS UNDEF unless we're secure */
    assert(env->v7m.secure);

    if (dest & 1) {
        /*
         * Target is Secure, so this is just a normal BLX,
         * except that the low bit doesn't indicate Thumb/not.
         */
        env->regs[14] = nextinst;
        env->thumb = true;
        env->regs[15] = dest & ~1;
        return;
    }

    /* Target is non-secure: first push a stack frame */
    if (!QEMU_IS_ALIGNED(sp, 8)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "BLXNS with misaligned SP is UNPREDICTABLE\n");
    }

    if (sp < v7m_sp_limit(env)) {
        raise_exception(env, EXCP_STKOF, 0, 1);
    }

    saved_psr = env->v7m.exception;
    if (env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK) {
        saved_psr |= XPSR_SFPA;
    }

    /* Note that these stores can throw exceptions on MPU faults */
    cpu_stl_data_ra(env, sp, nextinst, GETPC());
    cpu_stl_data_ra(env, sp + 4, saved_psr, GETPC());

    env->regs[13] = sp;
    env->regs[14] = 0xfeffffff;
    if (arm_v7m_is_handler_mode(env)) {
        /*
         * Write a dummy value to IPSR, to avoid leaking the current secure
         * exception number to non-secure code. This is guaranteed not
         * to cause write_v7m_exception() to actually change stacks.
         */
        write_v7m_exception(env, 1);
    }
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    switch_v7m_security_state(env, 0);
    env->thumb = true;
    env->regs[15] = dest;
    arm_rebuild_hflags(env);
}

static bool arm_v7m_load_vector(ARMCPU *cpu, int exc, bool targets_secure,
                                uint32_t *pvec)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult result;
    uint32_t addr = env->v7m.vecbase[targets_secure] + exc * 4;
    uint32_t vector_entry;
    MemTxAttrs attrs = {};
    ARMMMUIdx mmu_idx;
    bool exc_secure;

    qemu_log_mask(CPU_LOG_INT,
                  "...loading from element %d of %s vector table at 0x%x\n",
                  exc, targets_secure ? "secure" : "non-secure", addr);

    mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, targets_secure, true);

    /*
     * We don't do a get_phys_addr() here because the rules for vector
     * loads are special: they always use the default memory map, and
     * the default memory map permits reads from all addresses.
     * Since there's no easy way to pass through to pmsav8_mpu_lookup()
     * that we want this special case which would always say "yes",
     * we just do the SAU lookup here followed by a direct physical load.
     */
    attrs.secure = targets_secure;
    attrs.user = false;

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        V8M_SAttributes sattrs = {};

        v8m_security_lookup(env, addr, MMU_DATA_LOAD, mmu_idx,
                            targets_secure, &sattrs);
        if (sattrs.ns) {
            attrs.secure = false;
        } else if (!targets_secure) {
            /*
             * NS access to S memory: the underlying exception which we escalate
             * to HardFault is SecureFault, which always targets Secure.
             */
            exc_secure = true;
            goto load_fail;
        }
    }

    vector_entry = address_space_ldl(arm_addressspace(cs, attrs), addr,
                                     attrs, &result);
    if (result != MEMTX_OK) {
        /*
         * Underlying exception is BusFault: its target security state
         * depends on BFHFNMINS.
         */
        exc_secure = !(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK);
        goto load_fail;
    }
    *pvec = vector_entry;
    qemu_log_mask(CPU_LOG_INT, "...loaded new PC 0x%x\n", *pvec);
    return true;

load_fail:
    /*
     * All vector table fetch fails are reported as HardFault, with
     * HFSR.VECTTBL and .FORCED set. (FORCED is set because
     * technically the underlying exception is a SecureFault or BusFault
     * that is escalated to HardFault.) This is a terminal exception,
     * so we will either take the HardFault immediately or else enter
     * lockup (the latter case is handled in armv7m_nvic_set_pending_derived()).
     * The HardFault is Secure if BFHFNMINS is 0 (meaning that all HFs are
     * secure); otherwise it targets the same security state as the
     * underlying exception.
     * In v8.1M HardFaults from vector table fetch fails don't set FORCED.
     */
    if (!(cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
        exc_secure = true;
    }
    env->v7m.hfsr |= R_V7M_HFSR_VECTTBL_MASK;
    if (!arm_feature(env, ARM_FEATURE_V8_1M)) {
        env->v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
    }
    armv7m_nvic_set_pending_derived(env->nvic, ARMV7M_EXCP_HARD, exc_secure);
    return false;
}

static uint32_t v7m_integrity_sig(CPUARMState *env, uint32_t lr)
{
    /*
     * Return the integrity signature value for the callee-saves
     * stack frame section. @lr is the exception return payload/LR value
     * whose FType bit forms bit 0 of the signature if FP is present.
     */
    uint32_t sig = 0xfefa125a;

    if (!cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))
        || (lr & R_V7M_EXCRET_FTYPE_MASK)) {
        sig |= 1;
    }
    return sig;
}

static bool v7m_push_callee_stack(ARMCPU *cpu, uint32_t lr, bool dotailchain,
                                  bool ignore_faults)
{
    /*
     * For v8M, push the callee-saves register part of the stack frame.
     * Compare the v8M pseudocode PushCalleeStack().
     * In the tailchaining case this may not be the current stack.
     */
    CPUARMState *env = &cpu->env;
    uint32_t *frame_sp_p;
    uint32_t frameptr;
    ARMMMUIdx mmu_idx;
    bool stacked_ok;
    uint32_t limit;
    bool want_psp;
    uint32_t sig;
    StackingMode smode = ignore_faults ? STACK_IGNFAULTS : STACK_NORMAL;

    if (dotailchain) {
        bool mode = lr & R_V7M_EXCRET_MODE_MASK;
        bool priv = !(env->v7m.control[M_REG_S] & R_V7M_CONTROL_NPRIV_MASK) ||
            !mode;

        mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, M_REG_S, priv);
        frame_sp_p = arm_v7m_get_sp_ptr(env, M_REG_S, mode,
                                        lr & R_V7M_EXCRET_SPSEL_MASK);
        want_psp = mode && (lr & R_V7M_EXCRET_SPSEL_MASK);
        if (want_psp) {
            limit = env->v7m.psplim[M_REG_S];
        } else {
            limit = env->v7m.msplim[M_REG_S];
        }
    } else {
        mmu_idx = arm_mmu_idx(env);
        frame_sp_p = &env->regs[13];
        limit = v7m_sp_limit(env);
    }

    frameptr = *frame_sp_p - 0x28;
    if (frameptr < limit) {
        /*
         * Stack limit failure: set SP to the limit value, and generate
         * STKOF UsageFault. Stack pushes below the limit must not be
         * performed. It is IMPDEF whether pushes above the limit are
         * performed; we choose not to.
         */
        qemu_log_mask(CPU_LOG_INT,
                      "...STKOF during callee-saves register stacking\n");
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                env->v7m.secure);
        *frame_sp_p = limit;
        return true;
    }

    /*
     * Write as much of the stack frame as we can. A write failure may
     * cause us to pend a derived exception.
     */
    sig = v7m_integrity_sig(env, lr);
    stacked_ok =
        v7m_stack_write(cpu, frameptr, sig, mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x8, env->regs[4], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0xc, env->regs[5], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x10, env->regs[6], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x14, env->regs[7], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x18, env->regs[8], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x1c, env->regs[9], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x20, env->regs[10], mmu_idx, smode) &&
        v7m_stack_write(cpu, frameptr + 0x24, env->regs[11], mmu_idx, smode);

    /* Update SP regardless of whether any of the stack accesses failed. */
    *frame_sp_p = frameptr;

    return !stacked_ok;
}

static void v7m_exception_taken(ARMCPU *cpu, uint32_t lr, bool dotailchain,
                                bool ignore_stackfaults)
{
    /*
     * Do the "take the exception" parts of exception entry,
     * but not the pushing of state to the stack. This is
     * similar to the pseudocode ExceptionTaken() function.
     */
    CPUARMState *env = &cpu->env;
    uint32_t addr;
    bool targets_secure;
    int exc;
    bool push_failed = false;

    armv7m_nvic_get_pending_irq_info(env->nvic, &exc, &targets_secure);
    qemu_log_mask(CPU_LOG_INT, "...taking pending %s exception %d\n",
                  targets_secure ? "secure" : "nonsecure", exc);

    if (dotailchain) {
        /* Sanitize LR FType and PREFIX bits */
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            lr |= R_V7M_EXCRET_FTYPE_MASK;
        }
        lr = deposit32(lr, 24, 8, 0xff);
    }

    if (arm_feature(env, ARM_FEATURE_V8)) {
        if (arm_feature(env, ARM_FEATURE_M_SECURITY) &&
            (lr & R_V7M_EXCRET_S_MASK)) {
            /*
             * The background code (the owner of the registers in the
             * exception frame) is Secure. This means it may either already
             * have or now needs to push callee-saves registers.
             */
            if (targets_secure) {
                if (dotailchain && !(lr & R_V7M_EXCRET_ES_MASK)) {
                    /*
                     * We took an exception from Secure to NonSecure
                     * (which means the callee-saved registers got stacked)
                     * and are now tailchaining to a Secure exception.
                     * Clear DCRS so eventual return from this Secure
                     * exception unstacks the callee-saved registers.
                     */
                    lr &= ~R_V7M_EXCRET_DCRS_MASK;
                }
            } else {
                /*
                 * We're going to a non-secure exception; push the
                 * callee-saves registers to the stack now, if they're
                 * not already saved.
                 */
                if (lr & R_V7M_EXCRET_DCRS_MASK &&
                    !(dotailchain && !(lr & R_V7M_EXCRET_ES_MASK))) {
                    push_failed = v7m_push_callee_stack(cpu, lr, dotailchain,
                                                        ignore_stackfaults);
                }
                lr |= R_V7M_EXCRET_DCRS_MASK;
            }
        }

        lr &= ~R_V7M_EXCRET_ES_MASK;
        if (targets_secure) {
            lr |= R_V7M_EXCRET_ES_MASK;
        }
        lr &= ~R_V7M_EXCRET_SPSEL_MASK;
        if (env->v7m.control[targets_secure] & R_V7M_CONTROL_SPSEL_MASK) {
            lr |= R_V7M_EXCRET_SPSEL_MASK;
        }

        /*
         * Clear registers if necessary to prevent non-secure exception
         * code being able to see register values from secure code.
         * Where register values become architecturally UNKNOWN we leave
         * them with their previous values. v8.1M is tighter than v8.0M
         * here and always zeroes the caller-saved registers regardless
         * of the security state the exception is targeting.
         */
        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            if (!targets_secure || arm_feature(env, ARM_FEATURE_V8_1M)) {
                /*
                 * Always clear the caller-saved registers (they have been
                 * pushed to the stack earlier in v7m_push_stack()).
                 * Clear callee-saved registers if the background code is
                 * Secure (in which case these regs were saved in
                 * v7m_push_callee_stack()).
                 */
                int i;
                /*
                 * r4..r11 are callee-saves, zero only if background
                 * state was Secure (EXCRET.S == 1) and exception
                 * targets Non-secure state
                 */
                bool zero_callee_saves = !targets_secure &&
                    (lr & R_V7M_EXCRET_S_MASK);

                for (i = 0; i < 13; i++) {
                    if (i < 4 || i > 11 || zero_callee_saves) {
                        env->regs[i] = 0;
                    }
                }
                /* Clear EAPSR */
                xpsr_write(env, 0, XPSR_NZCV | XPSR_Q | XPSR_GE | XPSR_IT);
            }
        }
    }

    if (push_failed && !ignore_stackfaults) {
        /*
         * Derived exception on callee-saves register stacking:
         * we might now want to take a different exception which
         * targets a different security state, so try again from the top.
         */
        qemu_log_mask(CPU_LOG_INT,
                      "...derived exception on callee-saves register stacking");
        v7m_exception_taken(cpu, lr, true, true);
        return;
    }

    if (!arm_v7m_load_vector(cpu, exc, targets_secure, &addr)) {
        /* Vector load failed: derived exception */
        qemu_log_mask(CPU_LOG_INT, "...derived exception on vector table load");
        v7m_exception_taken(cpu, lr, true, true);
        return;
    }

    /*
     * Now we've done everything that might cause a derived exception
     * we can go ahead and activate whichever exception we're going to
     * take (which might now be the derived exception).
     */
    armv7m_nvic_acknowledge_irq(env->nvic);

    /* Switch to target security state -- must do this before writing SPSEL */
    switch_v7m_security_state(env, targets_secure);
    write_v7m_control_spsel(env, 0);
    arm_clear_exclusive(env);
    /* Clear SFPA and FPCA (has no effect if no FPU) */
    env->v7m.control[M_REG_S] &=
        ~(R_V7M_CONTROL_FPCA_MASK | R_V7M_CONTROL_SFPA_MASK);
    /* Clear IT bits */
    env->condexec_bits = 0;
    env->regs[14] = lr;
    env->regs[15] = addr & 0xfffffffe;
    env->thumb = addr & 1;
    arm_rebuild_hflags(env);
}

static void v7m_update_fpccr(CPUARMState *env, uint32_t frameptr,
                             bool apply_splim)
{
    /*
     * Like the pseudocode UpdateFPCCR: save state in FPCAR and FPCCR
     * that we will need later in order to do lazy FP reg stacking.
     */
    bool is_secure = env->v7m.secure;
    NVICState *nvic = env->nvic;
    /*
     * Some bits are unbanked and live always in fpccr[M_REG_S]; some bits
     * are banked and we want to update the bit in the bank for the
     * current security state; and in one case we want to specifically
     * update the NS banked version of a bit even if we are secure.
     */
    uint32_t *fpccr_s = &env->v7m.fpccr[M_REG_S];
    uint32_t *fpccr_ns = &env->v7m.fpccr[M_REG_NS];
    uint32_t *fpccr = &env->v7m.fpccr[is_secure];
    bool hfrdy, bfrdy, mmrdy, ns_ufrdy, s_ufrdy, sfrdy, monrdy;

    env->v7m.fpcar[is_secure] = frameptr & ~0x7;

    if (apply_splim && arm_feature(env, ARM_FEATURE_V8)) {
        bool splimviol;
        uint32_t splim = v7m_sp_limit(env);
        bool ign = armv7m_nvic_neg_prio_requested(nvic, is_secure) &&
            (env->v7m.ccr[is_secure] & R_V7M_CCR_STKOFHFNMIGN_MASK);

        splimviol = !ign && frameptr < splim;
        *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, SPLIMVIOL, splimviol);
    }

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, LSPACT, 1);

    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, S, is_secure);

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, USER, arm_current_el(env) == 0);

    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, THREAD,
                        !arm_v7m_is_handler_mode(env));

    hfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_HARD, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, HFRDY, hfrdy);

    bfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_BUS, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, BFRDY, bfrdy);

    mmrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_MEM, is_secure);
    *fpccr = FIELD_DP32(*fpccr, V7M_FPCCR, MMRDY, mmrdy);

    ns_ufrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_USAGE, false);
    *fpccr_ns = FIELD_DP32(*fpccr_ns, V7M_FPCCR, UFRDY, ns_ufrdy);

    monrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_DEBUG, false);
    *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, MONRDY, monrdy);

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        s_ufrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_USAGE, true);
        *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, UFRDY, s_ufrdy);

        sfrdy = armv7m_nvic_get_ready_status(nvic, ARMV7M_EXCP_SECURE, false);
        *fpccr_s = FIELD_DP32(*fpccr_s, V7M_FPCCR, SFRDY, sfrdy);
    }
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    /* fptr is the value of Rn, the frame pointer we store the FP regs to */
    ARMCPU *cpu = env_archcpu(env);
    bool s = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
    bool lspact = env->v7m.fpccr[s] & R_V7M_FPCCR_LSPACT_MASK;
    uintptr_t ra = GETPC();

    assert(env->v7m.secure);

    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        return;
    }

    /* Check access to the coprocessor is permitted */
    if (!v7m_cpacr_pass(env, true, arm_current_el(env) != 0)) {
        raise_exception_ra(env, EXCP_NOCP, 0, 1, GETPC());
    }

    if (lspact) {
        /* LSPACT should not be active when there is active FP state */
        raise_exception_ra(env, EXCP_LSERR, 0, 1, GETPC());
    }

    if (fptr & 7) {
        raise_exception_ra(env, EXCP_UNALIGNED, 0, 1, GETPC());
    }

    /*
     * Note that we do not use v7m_stack_write() here, because the
     * accesses should not set the FSR bits for stacking errors if they
     * fail. (In pseudocode terms, they are AccType_NORMAL, not AccType_STACK
     * or AccType_LAZYFP). Faults in cpu_stl_data_ra() will throw exceptions
     * and longjmp out.
     */
    if (!(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPEN_MASK)) {
        bool ts = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK;
        int i;

        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint64_t dn = *aa32_vfp_dreg(env, i / 2);
            uint32_t faddr = fptr + 4 * i;
            uint32_t slo = extract64(dn, 0, 32);
            uint32_t shi = extract64(dn, 32, 32);

            if (i >= 16) {
                faddr += 8; /* skip the slot for the FPSCR */
            }
            cpu_stl_data_ra(env, faddr, slo, ra);
            cpu_stl_data_ra(env, faddr + 4, shi, ra);
        }
        cpu_stl_data_ra(env, fptr + 0x40, vfp_get_fpscr(env), ra);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            cpu_stl_data_ra(env, fptr + 0x44, env->v7m.vpr, ra);
        }

        /*
         * If TS is 0 then s0 to s15, FPSCR and VPR are UNKNOWN; we choose to
         * leave them unchanged, matching our choice in v7m_preserve_fp_state.
         */
        if (ts) {
            for (i = 0; i < 32; i += 2) {
                *aa32_vfp_dreg(env, i / 2) = 0;
            }
            vfp_set_fpscr(env, 0);
            if (cpu_isar_feature(aa32_mve, cpu)) {
                env->v7m.vpr = 0;
            }
        }
    } else {
        v7m_update_fpccr(env, fptr, false);
    }

    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    ARMCPU *cpu = env_archcpu(env);
    uintptr_t ra = GETPC();

    /* fptr is the value of Rn, the frame pointer we load the FP regs from */
    assert(env->v7m.secure);

    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        return;
    }

    /* Check access to the coprocessor is permitted */
    if (!v7m_cpacr_pass(env, true, arm_current_el(env) != 0)) {
        raise_exception_ra(env, EXCP_NOCP, 0, 1, GETPC());
    }

    if (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK) {
        /* State in FP is still valid */
        env->v7m.fpccr[M_REG_S] &= ~R_V7M_FPCCR_LSPACT_MASK;
    } else {
        bool ts = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK;
        int i;
        uint32_t fpscr;

        if (fptr & 7) {
            raise_exception_ra(env, EXCP_UNALIGNED, 0, 1, GETPC());
        }

        for (i = 0; i < (ts ? 32 : 16); i += 2) {
            uint32_t slo, shi;
            uint64_t dn;
            uint32_t faddr = fptr + 4 * i;

            if (i >= 16) {
                faddr += 8; /* skip the slot for the FPSCR and VPR */
            }

            slo = cpu_ldl_data_ra(env, faddr, ra);
            shi = cpu_ldl_data_ra(env, faddr + 4, ra);

            dn = (uint64_t) shi << 32 | slo;
            *aa32_vfp_dreg(env, i / 2) = dn;
        }
        fpscr = cpu_ldl_data_ra(env, fptr + 0x40, ra);
        vfp_set_fpscr(env, fpscr);
        if (cpu_isar_feature(aa32_mve, cpu)) {
            env->v7m.vpr = cpu_ldl_data_ra(env, fptr + 0x44, ra);
        }
    }

    env->v7m.control[M_REG_S] |= R_V7M_CONTROL_FPCA_MASK;
}

static bool v7m_push_stack(ARMCPU *cpu)
{
    /*
     * Do the "set up stack frame" part of exception entry,
     * similar to pseudocode PushStack().
     * Return true if we generate a derived exception (and so
     * should ignore further stack faults trying to process
     * that derived exception.)
     */
    bool stacked_ok = true, limitviol = false;
    CPUARMState *env = &cpu->env;
    uint32_t xpsr = xpsr_read(env);
    uint32_t frameptr = env->regs[13];
    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    uint32_t framesize;
    bool nsacr_cp10 = extract32(env->v7m.nsacr, 10, 1);

    if ((env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) &&
        (env->v7m.secure || nsacr_cp10)) {
        if (env->v7m.secure &&
            env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK) {
            framesize = 0xa8;
        } else {
            framesize = 0x68;
        }
    } else {
        framesize = 0x20;
    }

    /* Align stack pointer if the guest wants that */
    if ((frameptr & 4) &&
        (env->v7m.ccr[env->v7m.secure] & R_V7M_CCR_STKALIGN_MASK)) {
        frameptr -= 4;
        xpsr |= XPSR_SPREALIGN;
    }

    xpsr &= ~XPSR_SFPA;
    if (env->v7m.secure &&
        (env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)) {
        xpsr |= XPSR_SFPA;
    }

    frameptr -= framesize;

    if (arm_feature(env, ARM_FEATURE_V8)) {
        uint32_t limit = v7m_sp_limit(env);

        if (frameptr < limit) {
            /*
             * Stack limit failure: set SP to the limit value, and generate
             * STKOF UsageFault. Stack pushes below the limit must not be
             * performed. It is IMPDEF whether pushes above the limit are
             * performed; we choose not to.
             */
            qemu_log_mask(CPU_LOG_INT,
                          "...STKOF during stacking\n");
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            env->regs[13] = limit;
            /*
             * We won't try to perform any further memory accesses but
             * we must continue through the following code to check for
             * permission faults during FPU state preservation, and we
             * must update FPCCR if lazy stacking is enabled.
             */
            limitviol = true;
            stacked_ok = false;
        }
    }

    /*
     * Write as much of the stack frame as we can. If we fail a stack
     * write this will result in a derived exception being pended
     * (which may be taken in preference to the one we started with
     * if it has higher priority).
     */
    stacked_ok = stacked_ok &&
        v7m_stack_write(cpu, frameptr, env->regs[0], mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 4, env->regs[1],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 8, env->regs[2],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 12, env->regs[3],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 16, env->regs[12],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 20, env->regs[14],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 24, env->regs[15],
                        mmu_idx, STACK_NORMAL) &&
        v7m_stack_write(cpu, frameptr + 28, xpsr, mmu_idx, STACK_NORMAL);

    if (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) {
        /* FPU is active, try to save its registers */
        bool fpccr_s = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
        bool lspact = env->v7m.fpccr[fpccr_s] & R_V7M_FPCCR_LSPACT_MASK;

        if (lspact && arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault because LSPACT and FPCA both set\n");
            env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        } else if (!env->v7m.secure && !nsacr_cp10) {
            qemu_log_mask(CPU_LOG_INT,
                          "...Secure UsageFault with CFSR.NOCP because "
                          "NSACR.CP10 prevents stacking FP regs\n");
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, M_REG_S);
            env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
        } else {
            if (!(env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPEN_MASK)) {
                /* Lazy stacking disabled, save registers now */
                int i;
                bool cpacr_pass = v7m_cpacr_pass(env, env->v7m.secure,
                                                 arm_current_el(env) != 0);

                if (stacked_ok && !cpacr_pass) {
                    /*
                     * Take UsageFault if CPACR forbids access. The pseudocode
                     * here does a full CheckCPEnabled() but we know the NSACR
                     * check can never fail as we have already handled that.
                     */
                    qemu_log_mask(CPU_LOG_INT,
                                  "...UsageFault with CFSR.NOCP because "
                                  "CPACR.CP10 prevents stacking FP regs\n");
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            env->v7m.secure);
                    env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_NOCP_MASK;
                    stacked_ok = false;
                }

                for (i = 0; i < ((framesize == 0xa8) ? 32 : 16); i += 2) {
                    uint64_t dn = *aa32_vfp_dreg(env, i / 2);
                    uint32_t faddr = frameptr + 0x20 + 4 * i;
                    uint32_t slo = extract64(dn, 0, 32);
                    uint32_t shi = extract64(dn, 32, 32);

                    if (i >= 16) {
                        faddr += 8; /* skip the slot for the FPSCR and VPR */
                    }
                    stacked_ok = stacked_ok &&
                        v7m_stack_write(cpu, faddr, slo,
                                        mmu_idx, STACK_NORMAL) &&
                        v7m_stack_write(cpu, faddr + 4, shi,
                                        mmu_idx, STACK_NORMAL);
                }
                stacked_ok = stacked_ok &&
                    v7m_stack_write(cpu, frameptr + 0x60,
                                    vfp_get_fpscr(env), mmu_idx, STACK_NORMAL);
                if (cpu_isar_feature(aa32_mve, cpu)) {
                    stacked_ok = stacked_ok &&
                        v7m_stack_write(cpu, frameptr + 0x64,
                                        env->v7m.vpr, mmu_idx, STACK_NORMAL);
                }
                if (cpacr_pass) {
                    for (i = 0; i < ((framesize == 0xa8) ? 32 : 16); i += 2) {
                        *aa32_vfp_dreg(env, i / 2) = 0;
                    }
                    vfp_set_fpscr(env, 0);
                    if (cpu_isar_feature(aa32_mve, cpu)) {
                        env->v7m.vpr = 0;
                    }
                }
            } else {
                /* Lazy stacking enabled, save necessary info to stack later */
                v7m_update_fpccr(env, frameptr + 0x20, true);
            }
        }
    }

    /*
     * If we broke a stack limit then SP was already updated earlier;
     * otherwise we update SP regardless of whether any of the stack
     * accesses failed or we took some other kind of fault.
     */
    if (!limitviol) {
        env->regs[13] = frameptr;
    }

    return !stacked_ok;
}

static void do_v7m_exception_exit(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    uint32_t excret;
    uint32_t xpsr, xpsr_mask;
    bool ufault = false;
    bool sfault = false;
    bool return_to_sp_process;
    bool return_to_handler;
    bool rettobase = false;
    bool exc_secure = false;
    bool return_to_secure;
    bool ftype;
    bool restore_s16_s31 = false;

    /*
     * If we're not in Handler mode then jumps to magic exception-exit
     * addresses don't have magic behaviour. However for the v8M
     * security extensions the magic secure-function-return has to
     * work in thread mode too, so to avoid doing an extra check in
     * the generated code we allow exception-exit magic to also cause the
     * internal exception and bring us here in thread mode. Correct code
     * will never try to do this (the following insn fetch will always
     * fault) so we the overhead of having taken an unnecessary exception
     * doesn't matter.
     */
    if (!arm_v7m_is_handler_mode(env)) {
        return;
    }

    /*
     * In the spec pseudocode ExceptionReturn() is called directly
     * from BXWritePC() and gets the full target PC value including
     * bit zero. In QEMU's implementation we treat it as a normal
     * jump-to-register (which is then caught later on), and so split
     * the target value up between env->regs[15] and env->thumb in
     * gen_bx(). Reconstitute it.
     */
    excret = env->regs[15];
    if (env->thumb) {
        excret |= 1;
    }

    qemu_log_mask(CPU_LOG_INT, "Exception return: magic PC %" PRIx32
                  " previous exception %d\n",
                  excret, env->v7m.exception);

    if ((excret & R_V7M_EXCRET_RES1_MASK) != R_V7M_EXCRET_RES1_MASK) {
        qemu_log_mask(LOG_GUEST_ERROR, "M profile: zero high bits in exception "
                      "exit PC value 0x%" PRIx32 " are UNPREDICTABLE\n",
                      excret);
    }

    ftype = excret & R_V7M_EXCRET_FTYPE_MASK;

    if (!ftype && !cpu_isar_feature(aa32_vfp_simd, cpu)) {
        qemu_log_mask(LOG_GUEST_ERROR, "M profile: zero FTYPE in exception "
                      "exit PC value 0x%" PRIx32 " is UNPREDICTABLE "
                      "if FPU not present\n",
                      excret);
        ftype = true;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        /*
         * EXC_RETURN.ES validation check (R_SMFL). We must do this before
         * we pick which FAULTMASK to clear.
         */
        if (!env->v7m.secure &&
            ((excret & R_V7M_EXCRET_ES_MASK) ||
             !(excret & R_V7M_EXCRET_DCRS_MASK))) {
            sfault = 1;
            /* For all other purposes, treat ES as 0 (R_HXSR) */
            excret &= ~R_V7M_EXCRET_ES_MASK;
        }
        exc_secure = excret & R_V7M_EXCRET_ES_MASK;
    }

    if (env->v7m.exception != ARMV7M_EXCP_NMI) {
        /*
         * Auto-clear FAULTMASK on return from other than NMI.
         * If the security extension is implemented then this only
         * happens if the raw execution priority is >= 0; the
         * value of the ES bit in the exception return value indicates
         * which security state's faultmask to clear. (v8M ARM ARM R_KBNF.)
         */
        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            if (armv7m_nvic_raw_execution_priority(env->nvic) >= 0) {
                env->v7m.faultmask[exc_secure] = 0;
            }
        } else {
            env->v7m.faultmask[M_REG_NS] = 0;
        }
    }

    switch (armv7m_nvic_complete_irq(env->nvic, env->v7m.exception,
                                     exc_secure)) {
    case -1:
        /* attempt to exit an exception that isn't active */
        ufault = true;
        break;
    case 0:
        /* still an irq active now */
        break;
    case 1:
        /*
         * We returned to base exception level, no nesting.
         * (In the pseudocode this is written using "NestedActivation != 1"
         * where we have 'rettobase == false'.)
         */
        rettobase = true;
        break;
    default:
        g_assert_not_reached();
    }

    return_to_handler = !(excret & R_V7M_EXCRET_MODE_MASK);
    return_to_sp_process = excret & R_V7M_EXCRET_SPSEL_MASK;
    return_to_secure = arm_feature(env, ARM_FEATURE_M_SECURITY) &&
        (excret & R_V7M_EXCRET_S_MASK);

    if (arm_feature(env, ARM_FEATURE_V8)) {
        if (!arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            /*
             * UNPREDICTABLE if S == 1 or DCRS == 0 or ES == 1 (R_XLCP);
             * we choose to take the UsageFault.
             */
            if ((excret & R_V7M_EXCRET_S_MASK) ||
                (excret & R_V7M_EXCRET_ES_MASK) ||
                !(excret & R_V7M_EXCRET_DCRS_MASK)) {
                ufault = true;
            }
        }
        if (excret & R_V7M_EXCRET_RES0_MASK) {
            ufault = true;
        }
    } else {
        /* For v7M we only recognize certain combinations of the low bits */
        switch (excret & 0xf) {
        case 1: /* Return to Handler */
            break;
        case 13: /* Return to Thread using Process stack */
        case 9: /* Return to Thread using Main stack */
            /*
             * We only need to check NONBASETHRDENA for v7M, because in
             * v8M this bit does not exist (it is RES1).
             */
            if (!rettobase &&
                !(env->v7m.ccr[env->v7m.secure] &
                  R_V7M_CCR_NONBASETHRDENA_MASK)) {
                ufault = true;
            }
            break;
        default:
            ufault = true;
        }
    }

    /*
     * Set CONTROL.SPSEL from excret.SPSEL. Since we're still in
     * Handler mode (and will be until we write the new XPSR.Interrupt
     * field) this does not switch around the current stack pointer.
     * We must do this before we do any kind of tailchaining, including
     * for the derived exceptions on integrity check failures, or we will
     * give the guest an incorrect EXCRET.SPSEL value on exception entry.
     */
    write_v7m_control_spsel_for_secstate(env, return_to_sp_process, exc_secure);

    /*
     * Clear scratch FP values left in caller saved registers; this
     * must happen before any kind of tail chaining.
     */
    if ((env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_CLRONRET_MASK) &&
        (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK)) {
        if (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK) {
            env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
            qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                          "stackframe: error during lazy state deactivation\n");
            v7m_exception_taken(cpu, excret, true, false);
            return;
        } else {
            if (arm_feature(env, ARM_FEATURE_V8_1M)) {
                /* v8.1M adds this NOCP check */
                bool nsacr_pass = exc_secure ||
                    extract32(env->v7m.nsacr, 10, 1);
                bool cpacr_pass = v7m_cpacr_pass(env, exc_secure, true);
                if (!nsacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, true);
                    env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                        "stackframe: NSACR prevents clearing FPU registers\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                } else if (!cpacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            exc_secure);
                    env->v7m.cfsr[exc_secure] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                        "stackframe: CPACR prevents clearing FPU registers\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                }
            }
            /* Clear s0..s15, FPSCR and VPR */
            int i;

            for (i = 0; i < 16; i += 2) {
                *aa32_vfp_dreg(env, i / 2) = 0;
            }
            vfp_set_fpscr(env, 0);
            if (cpu_isar_feature(aa32_mve, cpu)) {
                env->v7m.vpr = 0;
            }
        }
    }

    if (sfault) {
        env->v7m.sfsr |= R_V7M_SFSR_INVER_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                      "stackframe: failed EXC_RETURN.ES validity check\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    if (ufault) {
        /*
         * Bad exception return: instead of popping the exception
         * stack, directly take a usage fault on the current stack.
         */
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                      "stackframe: failed exception return integrity check\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    /*
     * Tailchaining: if there is currently a pending exception that
     * is high enough priority to preempt execution at the level we're
     * about to return to, then just directly take that exception now,
     * avoiding an unstack-and-then-stack. Note that now we have
     * deactivated the previous exception by calling armv7m_nvic_complete_irq()
     * our current execution priority is already the execution priority we are
     * returning to -- none of the state we would unstack or set based on
     * the EXCRET value affects it.
     */
    if (armv7m_nvic_can_take_pending_exception(env->nvic)) {
        qemu_log_mask(CPU_LOG_INT, "...tailchaining to pending exception\n");
        v7m_exception_taken(cpu, excret, true, false);
        return;
    }

    switch_v7m_security_state(env, return_to_secure);

    {
        /*
         * The stack pointer we should be reading the exception frame from
         * depends on bits in the magic exception return type value (and
         * for v8M isn't necessarily the stack pointer we will eventually
         * end up resuming execution with). Get a pointer to the location
         * in the CPU state struct where the SP we need is currently being
         * stored; we will use and modify it in place.
         * We use this limited C variable scope so we don't accidentally
         * use 'frame_sp_p' after we do something that makes it invalid.
         */
        bool spsel = env->v7m.control[return_to_secure] & R_V7M_CONTROL_SPSEL_MASK;
        uint32_t *frame_sp_p = arm_v7m_get_sp_ptr(env, return_to_secure,
                                                  !return_to_handler, spsel);
        uint32_t frameptr = *frame_sp_p;
        bool pop_ok = true;
        ARMMMUIdx mmu_idx;
        bool return_to_priv = return_to_handler ||
            !(env->v7m.control[return_to_secure] & R_V7M_CONTROL_NPRIV_MASK);

        mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, return_to_secure,
                                                        return_to_priv);

        if (!QEMU_IS_ALIGNED(frameptr, 8) &&
            arm_feature(env, ARM_FEATURE_V8)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "M profile exception return with non-8-aligned SP "
                          "for destination state is UNPREDICTABLE\n");
        }

        /* Do we need to pop callee-saved registers? */
        if (return_to_secure &&
            ((excret & R_V7M_EXCRET_ES_MASK) == 0 ||
             (excret & R_V7M_EXCRET_DCRS_MASK) == 0)) {
            uint32_t actual_sig;

            pop_ok = v7m_stack_read(cpu, &actual_sig, frameptr, mmu_idx);

            if (pop_ok && v7m_integrity_sig(env, excret) != actual_sig) {
                /* Take a SecureFault on the current stack */
                env->v7m.sfsr |= R_V7M_SFSR_INVIS_MASK;
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
                qemu_log_mask(CPU_LOG_INT, "...taking SecureFault on existing "
                              "stackframe: failed exception return integrity "
                              "signature check\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }

            pop_ok = pop_ok &&
                v7m_stack_read(cpu, &env->regs[4], frameptr + 0x8, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[5], frameptr + 0xc, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[6], frameptr + 0x10, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[7], frameptr + 0x14, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[8], frameptr + 0x18, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[9], frameptr + 0x1c, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[10], frameptr + 0x20, mmu_idx) &&
                v7m_stack_read(cpu, &env->regs[11], frameptr + 0x24, mmu_idx);

            frameptr += 0x28;
        }

        /* Pop registers */
        pop_ok = pop_ok &&
            v7m_stack_read(cpu, &env->regs[0], frameptr, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[1], frameptr + 0x4, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[2], frameptr + 0x8, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[3], frameptr + 0xc, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[12], frameptr + 0x10, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[14], frameptr + 0x14, mmu_idx) &&
            v7m_stack_read(cpu, &env->regs[15], frameptr + 0x18, mmu_idx) &&
            v7m_stack_read(cpu, &xpsr, frameptr + 0x1c, mmu_idx);

        if (!pop_ok) {
            /*
             * v7m_stack_read() pended a fault, so take it (as a tail
             * chained exception on the same stack frame)
             */
            qemu_log_mask(CPU_LOG_INT, "...derived exception on unstacking\n");
            v7m_exception_taken(cpu, excret, true, false);
            return;
        }

        /*
         * Returning from an exception with a PC with bit 0 set is defined
         * behaviour on v8M (bit 0 is ignored), but for v7M it was specified
         * to be UNPREDICTABLE. In practice actual v7M hardware seems to ignore
         * the lsbit, and there are several RTOSes out there which incorrectly
         * assume the r15 in the stack frame should be a Thumb-style "lsbit
         * indicates ARM/Thumb" value, so ignore the bit on v7M as well, but
         * complain about the badly behaved guest.
         */
        if (env->regs[15] & 1) {
            env->regs[15] &= ~1U;
            if (!arm_feature(env, ARM_FEATURE_V8)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "M profile return from interrupt with misaligned "
                              "PC is UNPREDICTABLE on v7M\n");
            }
        }

        if (arm_feature(env, ARM_FEATURE_V8)) {
            /*
             * For v8M we have to check whether the xPSR exception field
             * matches the EXCRET value for return to handler/thread
             * before we commit to changing the SP and xPSR.
             */
            bool will_be_handler = (xpsr & XPSR_EXCP) != 0;
            if (return_to_handler != will_be_handler) {
                /*
                 * Take an INVPC UsageFault on the current stack.
                 * By this point we will have switched to the security state
                 * for the background state, so this UsageFault will target
                 * that state.
                 */
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                        env->v7m.secure);
                env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
                qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on existing "
                              "stackframe: failed exception return integrity "
                              "check\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }
        }

        if (!ftype) {
            /* FP present and we need to handle it */
            if (!return_to_secure &&
                (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_LSPACT_MASK)) {
                armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
                env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
                qemu_log_mask(CPU_LOG_INT,
                              "...taking SecureFault on existing stackframe: "
                              "Secure LSPACT set but exception return is "
                              "not to secure state\n");
                v7m_exception_taken(cpu, excret, true, false);
                return;
            }

            restore_s16_s31 = return_to_secure &&
                (env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_TS_MASK);

            if (env->v7m.fpccr[return_to_secure] & R_V7M_FPCCR_LSPACT_MASK) {
                /* State in FPU is still valid, just clear LSPACT */
                env->v7m.fpccr[return_to_secure] &= ~R_V7M_FPCCR_LSPACT_MASK;
            } else {
                int i;
                uint32_t fpscr;
                bool cpacr_pass, nsacr_pass;

                cpacr_pass = v7m_cpacr_pass(env, return_to_secure,
                                            return_to_priv);
                nsacr_pass = return_to_secure ||
                    extract32(env->v7m.nsacr, 10, 1);

                if (!cpacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                            return_to_secure);
                    env->v7m.cfsr[return_to_secure] |= R_V7M_CFSR_NOCP_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...taking UsageFault on existing "
                                  "stackframe: CPACR.CP10 prevents unstacking "
                                  "FP regs\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                } else if (!nsacr_pass) {
                    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, true);
                    env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_INVPC_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...taking Secure UsageFault on existing "
                                  "stackframe: NSACR.CP10 prevents unstacking "
                                  "FP regs\n");
                    v7m_exception_taken(cpu, excret, true, false);
                    return;
                }

                for (i = 0; i < (restore_s16_s31 ? 32 : 16); i += 2) {
                    uint32_t slo, shi;
                    uint64_t dn;
                    uint32_t faddr = frameptr + 0x20 + 4 * i;

                    if (i >= 16) {
                        faddr += 8; /* Skip the slot for the FPSCR and VPR */
                    }

                    pop_ok = pop_ok &&
                        v7m_stack_read(cpu, &slo, faddr, mmu_idx) &&
                        v7m_stack_read(cpu, &shi, faddr + 4, mmu_idx);

                    if (!pop_ok) {
                        break;
                    }

                    dn = (uint64_t)shi << 32 | slo;
                    *aa32_vfp_dreg(env, i / 2) = dn;
                }
                pop_ok = pop_ok &&
                    v7m_stack_read(cpu, &fpscr, frameptr + 0x60, mmu_idx);
                if (pop_ok) {
                    vfp_set_fpscr(env, fpscr);
                }
                if (cpu_isar_feature(aa32_mve, cpu)) {
                    pop_ok = pop_ok &&
                        v7m_stack_read(cpu, &env->v7m.vpr,
                                       frameptr + 0x64, mmu_idx);
                }
                if (!pop_ok) {
                    /*
                     * These regs are 0 if security extension present;
                     * otherwise merely UNKNOWN. We zero always.
                     */
                    for (i = 0; i < (restore_s16_s31 ? 32 : 16); i += 2) {
                        *aa32_vfp_dreg(env, i / 2) = 0;
                    }
                    vfp_set_fpscr(env, 0);
                    if (cpu_isar_feature(aa32_mve, cpu)) {
                        env->v7m.vpr = 0;
                    }
                }
            }
        }
        env->v7m.control[M_REG_S] = FIELD_DP32(env->v7m.control[M_REG_S],
                                               V7M_CONTROL, FPCA, !ftype);

        /* Commit to consuming the stack frame */
        frameptr += 0x20;
        if (!ftype) {
            frameptr += 0x48;
            if (restore_s16_s31) {
                frameptr += 0x40;
            }
        }
        /*
         * Undo stack alignment (the SPREALIGN bit indicates that the original
         * pre-exception SP was not 8-aligned and we added a padding word to
         * align it, so we undo this by ORing in the bit that increases it
         * from the current 8-aligned value to the 8-unaligned value. (Adding 4
         * would work too but a logical OR is how the pseudocode specifies it.)
         */
        if (xpsr & XPSR_SPREALIGN) {
            frameptr |= 4;
        }
        *frame_sp_p = frameptr;
    }

    xpsr_mask = ~(XPSR_SPREALIGN | XPSR_SFPA);
    if (!arm_feature(env, ARM_FEATURE_THUMB_DSP)) {
        xpsr_mask &= ~XPSR_GE;
    }
    /* This xpsr_write() will invalidate frame_sp_p as it may switch stack */
    xpsr_write(env, xpsr, xpsr_mask);

    if (env->v7m.secure) {
        bool sfpa = xpsr & XPSR_SFPA;

        env->v7m.control[M_REG_S] = FIELD_DP32(env->v7m.control[M_REG_S],
                                               V7M_CONTROL, SFPA, sfpa);
    }

    /*
     * The restored xPSR exception field will be zero if we're
     * resuming in Thread mode. If that doesn't match what the
     * exception return excret specified then this is a UsageFault.
     * v7M requires we make this check here; v8M did it earlier.
     */
    if (return_to_handler != arm_v7m_is_handler_mode(env)) {
        /*
         * Take an INVPC UsageFault by pushing the stack again;
         * we know we're v7M so this is never a Secure UsageFault.
         */
        bool ignore_stackfaults;

        assert(!arm_feature(env, ARM_FEATURE_V8));
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, false);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
        ignore_stackfaults = v7m_push_stack(cpu);
        qemu_log_mask(CPU_LOG_INT, "...taking UsageFault on new stackframe: "
                      "failed exception return integrity check\n");
        v7m_exception_taken(cpu, excret, false, ignore_stackfaults);
        return;
    }

    /* Otherwise, we have a successful exception exit. */
    arm_clear_exclusive(env);
    arm_rebuild_hflags(env);
    qemu_log_mask(CPU_LOG_INT, "...successful exception return\n");
}

static bool do_v7m_function_return(ARMCPU *cpu)
{
    /*
     * v8M security extensions magic function return.
     * We may either:
     *  (1) throw an exception (longjump)
     *  (2) return true if we successfully handled the function return
     *  (3) return false if we failed a consistency check and have
     *      pended a UsageFault that needs to be taken now
     *
     * At this point the magic return value is split between env->regs[15]
     * and env->thumb. We don't bother to reconstitute it because we don't
     * need it (all values are handled the same way).
     */
    CPUARMState *env = &cpu->env;
    uint32_t newpc, newpsr, newpsr_exc;

    qemu_log_mask(CPU_LOG_INT, "...really v7M secure function return\n");

    {
        bool threadmode, spsel;
        MemOpIdx oi;
        ARMMMUIdx mmu_idx;
        uint32_t *frame_sp_p;
        uint32_t frameptr;

        /* Pull the return address and IPSR from the Secure stack */
        threadmode = !arm_v7m_is_handler_mode(env);
        spsel = env->v7m.control[M_REG_S] & R_V7M_CONTROL_SPSEL_MASK;

        frame_sp_p = arm_v7m_get_sp_ptr(env, true, threadmode, spsel);
        frameptr = *frame_sp_p;

        /*
         * These loads may throw an exception (for MPU faults). We want to
         * do them as secure, so work out what MMU index that is.
         */
        mmu_idx = arm_v7m_mmu_idx_for_secstate(env, true);
        oi = make_memop_idx(MO_LEUL, arm_to_core_mmu_idx(mmu_idx));
        newpc = cpu_ldl_mmu(env, frameptr, oi, 0);
        newpsr = cpu_ldl_mmu(env, frameptr + 4, oi, 0);

        /* Consistency checks on new IPSR */
        newpsr_exc = newpsr & XPSR_EXCP;
        if (!((env->v7m.exception == 0 && newpsr_exc == 0) ||
              (env->v7m.exception == 1 && newpsr_exc != 0))) {
            /* Pend the fault and tell our caller to take it */
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVPC_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            qemu_log_mask(CPU_LOG_INT,
                          "...taking INVPC UsageFault: "
                          "IPSR consistency check failed\n");
            return false;
        }

        *frame_sp_p = frameptr + 8;
    }

    /* This invalidates frame_sp_p */
    switch_v7m_security_state(env, true);
    env->v7m.exception = newpsr_exc;
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    if (newpsr & XPSR_SFPA) {
        env->v7m.control[M_REG_S] |= R_V7M_CONTROL_SFPA_MASK;
    }
    xpsr_write(env, 0, XPSR_IT);
    env->thumb = newpc & 1;
    env->regs[15] = newpc & ~1;
    arm_rebuild_hflags(env);

    qemu_log_mask(CPU_LOG_INT, "...function return successful\n");
    return true;
}

static bool v7m_read_half_insn(ARMCPU *cpu, ARMMMUIdx mmu_idx, bool secure,
                               uint32_t addr, uint16_t *insn)
{
    /*
     * Load a 16-bit portion of a v7M instruction, returning true on success,
     * or false on failure (in which case we will have pended the appropriate
     * exception).
     * We need to do the instruction fetch's MPU and SAU checks
     * like this because there is no MMU index that would allow
     * doing the load with a single function call. Instead we must
     * first check that the security attributes permit the load
     * and that they don't mismatch on the two halves of the instruction,
     * and then we do the load as a secure load (ie using the security
     * attributes of the address, not the CPU, as architecturally required).
     */
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    V8M_SAttributes sattrs = {};
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    MemTxResult txres;

    v8m_security_lookup(env, addr, MMU_INST_FETCH, mmu_idx, secure, &sattrs);
    if (!sattrs.nsc || sattrs.ns) {
        /*
         * This must be the second half of the insn, and it straddles a
         * region boundary with the second half not being S&NSC.
         */
        env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        qemu_log_mask(CPU_LOG_INT,
                      "...really SecureFault with SFSR.INVEP\n");
        return false;
    }
    if (get_phys_addr(env, addr, MMU_INST_FETCH, mmu_idx, &res, &fi)) {
        /* the MPU lookup failed */
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_IACCVIOL_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM, env->v7m.secure);
        qemu_log_mask(CPU_LOG_INT, "...really MemManage with CFSR.IACCVIOL\n");
        return false;
    }
    *insn = address_space_lduw_le(arm_addressspace(cs, res.f.attrs),
                                  res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_IBUSERR_MASK;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
        qemu_log_mask(CPU_LOG_INT, "...really BusFault with CFSR.IBUSERR\n");
        return false;
    }
    return true;
}

static bool v7m_read_sg_stack_word(ARMCPU *cpu, ARMMMUIdx mmu_idx,
                                   uint32_t addr, uint32_t *spdata)
{
    /*
     * Read a word of data from the stack for the SG instruction,
     * writing the value into *spdata. If the load succeeds, return
     * true; otherwise pend an appropriate exception and return false.
     * (We can't use data load helpers here that throw an exception
     * because of the context we're called in, which is halfway through
     * arm_v7m_cpu_do_interrupt().)
     */
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    MemTxResult txres;
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo fi = {};
    uint32_t value;

    if (get_phys_addr(env, addr, MMU_DATA_LOAD, mmu_idx, &res, &fi)) {
        /* MPU/SAU lookup failed */
        if (fi.type == ARMFault_QEMU_SFault) {
            qemu_log_mask(CPU_LOG_INT,
                          "...SecureFault during stack word read\n");
            env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK | R_V7M_SFSR_SFARVALID_MASK;
            env->v7m.sfar = addr;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        } else {
            qemu_log_mask(CPU_LOG_INT,
                          "...MemManageFault during stack word read\n");
            env->v7m.cfsr[M_REG_S] |= R_V7M_CFSR_DACCVIOL_MASK |
                R_V7M_CFSR_MMARVALID_MASK;
            env->v7m.mmfar[M_REG_S] = addr;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM, false);
        }
        return false;
    }
    value = address_space_ldl(arm_addressspace(cs, res.f.attrs),
                              res.f.phys_addr, res.f.attrs, &txres);
    if (txres != MEMTX_OK) {
        /* BusFault trying to read the data */
        qemu_log_mask(CPU_LOG_INT,
                      "...BusFault during stack word read\n");
        env->v7m.cfsr[M_REG_NS] |=
            (R_V7M_CFSR_PRECISERR_MASK | R_V7M_CFSR_BFARVALID_MASK);
        env->v7m.bfar = addr;
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
        return false;
    }

    *spdata = value;
    return true;
}

static bool v7m_handle_execute_nsc(ARMCPU *cpu)
{
    /*
     * Check whether this attempt to execute code in a Secure & NS-Callable
     * memory region is for an SG instruction; if so, then emulate the
     * effect of the SG instruction and return true. Otherwise pend
     * the correct kind of exception and return false.
     */
    CPUARMState *env = &cpu->env;
    ARMMMUIdx mmu_idx;
    uint16_t insn;

    /*
     * We should never get here unless get_phys_addr_pmsav8() caused
     * an exception for NS executing in S&NSC memory.
     */
    assert(!env->v7m.secure);
    assert(arm_feature(env, ARM_FEATURE_M_SECURITY));

    /* We want to do the MPU lookup as secure; work out what mmu_idx that is */
    mmu_idx = arm_v7m_mmu_idx_for_secstate(env, true);

    if (!v7m_read_half_insn(cpu, mmu_idx, true, env->regs[15], &insn)) {
        return false;
    }

    if (!env->thumb) {
        goto gen_invep;
    }

    if (insn != 0xe97f) {
        /*
         * Not an SG instruction first half (we choose the IMPDEF
         * early-SG-check option).
         */
        goto gen_invep;
    }

    if (!v7m_read_half_insn(cpu, mmu_idx, true, env->regs[15] + 2, &insn)) {
        return false;
    }

    if (insn != 0xe97f) {
        /*
         * Not an SG instruction second half (yes, both halves of the SG
         * insn have the same hex value)
         */
        goto gen_invep;
    }

    /*
     * OK, we have confirmed that we really have an SG instruction.
     * We know we're NS in S memory so don't need to repeat those checks.
     */
    qemu_log_mask(CPU_LOG_INT, "...really an SG instruction at 0x%08" PRIx32
                  ", executing it\n", env->regs[15]);

    if (cpu_isar_feature(aa32_m_sec_state, cpu) &&
        !arm_v7m_is_handler_mode(env)) {
        /*
         * v8.1M exception stack frame integrity check. Note that we
         * must perform the memory access even if CCR_S.TRD is zero
         * and we aren't going to check what the data loaded is.
         */
        uint32_t spdata, sp;

        /*
         * We know we are currently NS, so the S stack pointers must be
         * in other_ss_{psp,msp}, not in regs[13]/other_sp.
         */
        sp = v7m_using_psp(env) ? env->v7m.other_ss_psp : env->v7m.other_ss_msp;
        if (!v7m_read_sg_stack_word(cpu, mmu_idx, sp, &spdata)) {
            /* Stack access failed and an exception has been pended */
            return false;
        }

        if (env->v7m.ccr[M_REG_S] & R_V7M_CCR_TRD_MASK) {
            if (((spdata & ~1) == 0xfefa125a) ||
                !(env->v7m.control[M_REG_S] & 1)) {
                goto gen_invep;
            }
        }
    }

    env->regs[14] &= ~1;
    env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
    switch_v7m_security_state(env, true);
    xpsr_write(env, 0, XPSR_IT);
    env->regs[15] += 4;
    arm_rebuild_hflags(env);
    return true;

gen_invep:
    env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
    armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
    qemu_log_mask(CPU_LOG_INT,
                  "...really SecureFault with SFSR.INVEP\n");
    return false;
}

void arm_v7m_cpu_do_interrupt(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t lr;
    bool ignore_stackfaults;

    arm_log_exception(cs);

    /*
     * For exceptions we just mark as pending on the NVIC, and let that
     * handle it.
     */
    switch (cs->exception_index) {
    case EXCP_UDEF:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNDEFINSTR_MASK;
        break;
    case EXCP_NOCP:
    {
        /*
         * NOCP might be directed to something other than the current
         * security state if this fault is because of NSACR; we indicate
         * the target security state using exception.target_el.
         */
        int target_secstate;

        if (env->exception.target_el == 3) {
            target_secstate = M_REG_S;
        } else {
            target_secstate = env->v7m.secure;
        }
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, target_secstate);
        env->v7m.cfsr[target_secstate] |= R_V7M_CFSR_NOCP_MASK;
        break;
    }
    case EXCP_INVSTATE:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_INVSTATE_MASK;
        break;
    case EXCP_STKOF:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_STKOF_MASK;
        break;
    case EXCP_LSERR:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
        env->v7m.sfsr |= R_V7M_SFSR_LSERR_MASK;
        break;
    case EXCP_UNALIGNED:
        /* Unaligned faults reported by M-profile aware code */
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNALIGNED_MASK;
        break;
    case EXCP_DIVBYZERO:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE, env->v7m.secure);
        env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_DIVBYZERO_MASK;
        break;
    case EXCP_SWI:
        /* The PC already points to the next instruction.  */
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SVC, env->v7m.secure);
        break;
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
        /*
         * Note that for M profile we don't have a guest facing FSR, but
         * the env->exception.fsr will be populated by the code that
         * raises the fault, in the A profile short-descriptor format.
         *
         * Log the exception.vaddress now regardless of subtype, because
         * logging below only logs it when it goes into a guest visible
         * register.
         */
        qemu_log_mask(CPU_LOG_INT, "...at fault address 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        switch (env->exception.fsr & 0xf) {
        case M_FAKE_FSR_NSC_EXEC:
            /*
             * Exception generated when we try to execute code at an address
             * which is marked as Secure & Non-Secure Callable and the CPU
             * is in the Non-Secure state. The only instruction which can
             * be executed like this is SG (and that only if both halves of
             * the SG instruction have the same security attributes.)
             * Everything else must generate an INVEP SecureFault, so we
             * emulate the SG instruction here.
             */
            if (v7m_handle_execute_nsc(cpu)) {
                return;
            }
            break;
        case M_FAKE_FSR_SFAULT:
            /*
             * Various flavours of SecureFault for attempts to execute or
             * access data in the wrong security state.
             */
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                if (env->v7m.secure) {
                    env->v7m.sfsr |= R_V7M_SFSR_INVTRAN_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...really SecureFault with SFSR.INVTRAN\n");
                } else {
                    env->v7m.sfsr |= R_V7M_SFSR_INVEP_MASK;
                    qemu_log_mask(CPU_LOG_INT,
                                  "...really SecureFault with SFSR.INVEP\n");
                }
                break;
            case EXCP_DATA_ABORT:
                /* This must be an NS access to S memory */
                env->v7m.sfsr |= R_V7M_SFSR_AUVIOL_MASK;
                qemu_log_mask(CPU_LOG_INT,
                              "...really SecureFault with SFSR.AUVIOL\n");
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_SECURE, false);
            break;
        case 0x8: /* External Abort */
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                env->v7m.cfsr[M_REG_NS] |= R_V7M_CFSR_IBUSERR_MASK;
                qemu_log_mask(CPU_LOG_INT, "...with CFSR.IBUSERR\n");
                break;
            case EXCP_DATA_ABORT:
                env->v7m.cfsr[M_REG_NS] |=
                    (R_V7M_CFSR_PRECISERR_MASK | R_V7M_CFSR_BFARVALID_MASK);
                env->v7m.bfar = env->exception.vaddress;
                qemu_log_mask(CPU_LOG_INT,
                              "...with CFSR.PRECISERR and BFAR 0x%x\n",
                              env->v7m.bfar);
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_BUS, false);
            break;
        case 0x1: /* Alignment fault reported by generic code */
            qemu_log_mask(CPU_LOG_INT,
                          "...really UsageFault with UFSR.UNALIGNED\n");
            env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_UNALIGNED_MASK;
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_USAGE,
                                    env->v7m.secure);
            break;
        default:
            /*
             * All other FSR values are either MPU faults or "can't happen
             * for M profile" cases.
             */
            switch (cs->exception_index) {
            case EXCP_PREFETCH_ABORT:
                env->v7m.cfsr[env->v7m.secure] |= R_V7M_CFSR_IACCVIOL_MASK;
                qemu_log_mask(CPU_LOG_INT, "...with CFSR.IACCVIOL\n");
                break;
            case EXCP_DATA_ABORT:
                env->v7m.cfsr[env->v7m.secure] |=
                    (R_V7M_CFSR_DACCVIOL_MASK | R_V7M_CFSR_MMARVALID_MASK);
                env->v7m.mmfar[env->v7m.secure] = env->exception.vaddress;
                qemu_log_mask(CPU_LOG_INT,
                              "...with CFSR.DACCVIOL and MMFAR 0x%x\n",
                              env->v7m.mmfar[env->v7m.secure]);
                break;
            }
            armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_MEM,
                                    env->v7m.secure);
            break;
        }
        break;
    case EXCP_SEMIHOST:
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%x\n",
                      env->regs[0]);
#ifdef CONFIG_TCG
        do_common_semihosting(cs);
#else
        g_assert_not_reached();
#endif
        env->regs[15] += env->thumb ? 2 : 4;
        return;
    case EXCP_BKPT:
        armv7m_nvic_set_pending(env->nvic, ARMV7M_EXCP_DEBUG, false);
        break;
    case EXCP_IRQ:
        break;
    case EXCP_EXCEPTION_EXIT:
        if (env->regs[15] < EXC_RETURN_MIN_MAGIC) {
            /* Must be v8M security extension function return */
            assert(env->regs[15] >= FNC_RETURN_MIN_MAGIC);
            assert(arm_feature(env, ARM_FEATURE_M_SECURITY));
            if (do_v7m_function_return(cpu)) {
                return;
            }
        } else {
            do_v7m_exception_exit(cpu);
            return;
        }
        break;
    case EXCP_LAZYFP:
        /*
         * We already pended the specific exception in the NVIC in the
         * v7m_preserve_fp_state() helper function.
         */
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }

    if (arm_feature(env, ARM_FEATURE_V8)) {
        lr = R_V7M_EXCRET_RES1_MASK |
            R_V7M_EXCRET_DCRS_MASK;
        /*
         * The S bit indicates whether we should return to Secure
         * or NonSecure (ie our current state).
         * The ES bit indicates whether we're taking this exception
         * to Secure or NonSecure (ie our target state). We set it
         * later, in v7m_exception_taken().
         * The SPSEL bit is also set in v7m_exception_taken() for v8M.
         * This corresponds to the ARM ARM pseudocode for v8M setting
         * some LR bits in PushStack() and some in ExceptionTaken();
         * the distinction matters for the tailchain cases where we
         * can take an exception without pushing the stack.
         */
        if (env->v7m.secure) {
            lr |= R_V7M_EXCRET_S_MASK;
        }
    } else {
        lr = R_V7M_EXCRET_RES1_MASK |
            R_V7M_EXCRET_S_MASK |
            R_V7M_EXCRET_DCRS_MASK |
            R_V7M_EXCRET_ES_MASK;
        if (env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK) {
            lr |= R_V7M_EXCRET_SPSEL_MASK;
        }
    }
    if (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK)) {
        lr |= R_V7M_EXCRET_FTYPE_MASK;
    }
    if (!arm_v7m_is_handler_mode(env)) {
        lr |= R_V7M_EXCRET_MODE_MASK;
    }

    ignore_stackfaults = v7m_push_stack(cpu);
    v7m_exception_taken(cpu, lr, false, ignore_stackfaults);
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    unsigned el = arm_current_el(env);

    /* First handle registers which unprivileged can read */
    switch (reg) {
    case 0 ... 7: /* xPSR sub-fields */
        return v7m_mrs_xpsr(env, reg, el);
    case 20: /* CONTROL */
        return arm_v7m_mrs_control(env, env->v7m.secure);
    case 0x94: /* CONTROL_NS */
        /*
         * We have to handle this here because unprivileged Secure code
         * can read the NS CONTROL register.
         */
        if (!env->v7m.secure) {
            return 0;
        }
        return env->v7m.control[M_REG_NS] |
            (env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK);
    }

    if (el == 0) {
        return 0; /* unprivileged reads others as zero */
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        switch (reg) {
        case 0x88: /* MSP_NS */
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.other_ss_msp;
        case 0x89: /* PSP_NS */
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.other_ss_psp;
        case 0x8a: /* MSPLIM_NS */
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.msplim[M_REG_NS];
        case 0x8b: /* PSPLIM_NS */
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.psplim[M_REG_NS];
        case 0x90: /* PRIMASK_NS */
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.primask[M_REG_NS];
        case 0x91: /* BASEPRI_NS */
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.basepri[M_REG_NS];
        case 0x93: /* FAULTMASK_NS */
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return 0;
            }
            return env->v7m.faultmask[M_REG_NS];
        case 0x98: /* SP_NS */
        {
            /*
             * This gives the non-secure SP selected based on whether we're
             * currently in handler mode or not, using the NS CONTROL.SPSEL.
             */
            bool spsel = env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK;

            if (!env->v7m.secure) {
                return 0;
            }
            if (!arm_v7m_is_handler_mode(env) && spsel) {
                return env->v7m.other_ss_psp;
            } else {
                return env->v7m.other_ss_msp;
            }
        }
        default:
            break;
        }
    }

    switch (reg) {
    case 8: /* MSP */
        return v7m_using_psp(env) ? env->v7m.other_sp : env->regs[13];
    case 9: /* PSP */
        return v7m_using_psp(env) ? env->regs[13] : env->v7m.other_sp;
    case 10: /* MSPLIM */
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        return env->v7m.msplim[env->v7m.secure];
    case 11: /* PSPLIM */
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        return env->v7m.psplim[env->v7m.secure];
    case 16: /* PRIMASK */
        return env->v7m.primask[env->v7m.secure];
    case 17: /* BASEPRI */
    case 18: /* BASEPRI_MAX */
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        return env->v7m.basepri[env->v7m.secure];
    case 19: /* FAULTMASK */
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        return env->v7m.faultmask[env->v7m.secure];
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR, "Attempt to read unknown special"
                                       " register %d\n", reg);
        return 0;
    }
}

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    /*
     * We're passed bits [11..0] of the instruction; extract
     * SYSm and the mask bits.
     * Invalid combinations of SYSm and mask are UNPREDICTABLE;
     * we choose to treat them as if the mask bits were valid.
     * NB that the pseudocode 'mask' variable is bits [11..10],
     * whereas ours is [11..8].
     */
    uint32_t mask = extract32(maskreg, 8, 4);
    uint32_t reg = extract32(maskreg, 0, 8);
    int cur_el = arm_current_el(env);

    if (cur_el == 0 && reg > 7 && reg != 20) {
        /*
         * only xPSR sub-fields and CONTROL.SFPA may be written by
         * unprivileged code
         */
        return;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        switch (reg) {
        case 0x88: /* MSP_NS */
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.other_ss_msp = val & ~3;
            return;
        case 0x89: /* PSP_NS */
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.other_ss_psp = val & ~3;
            return;
        case 0x8a: /* MSPLIM_NS */
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.msplim[M_REG_NS] = val & ~7;
            return;
        case 0x8b: /* PSPLIM_NS */
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.psplim[M_REG_NS] = val & ~7;
            return;
        case 0x90: /* PRIMASK_NS */
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.primask[M_REG_NS] = val & 1;
            return;
        case 0x91: /* BASEPRI_NS */
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.basepri[M_REG_NS] = val & 0xff;
            return;
        case 0x93: /* FAULTMASK_NS */
            if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
                goto bad_reg;
            }
            if (!env->v7m.secure) {
                return;
            }
            env->v7m.faultmask[M_REG_NS] = val & 1;
            return;
        case 0x94: /* CONTROL_NS */
            if (!env->v7m.secure) {
                return;
            }
            write_v7m_control_spsel_for_secstate(env,
                                                 val & R_V7M_CONTROL_SPSEL_MASK,
                                                 M_REG_NS);
            if (arm_feature(env, ARM_FEATURE_M_MAIN)) {
                env->v7m.control[M_REG_NS] &= ~R_V7M_CONTROL_NPRIV_MASK;
                env->v7m.control[M_REG_NS] |= val & R_V7M_CONTROL_NPRIV_MASK;
            }
            /*
             * SFPA is RAZ/WI from NS. FPCA is RO if NSACR.CP10 == 0,
             * RES0 if the FPU is not present, and is stored in the S bank
             */
            if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env)) &&
                extract32(env->v7m.nsacr, 10, 1)) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_FPCA_MASK;
            }
            return;
        case 0x98: /* SP_NS */
        {
            /*
             * This gives the non-secure SP selected based on whether we're
             * currently in handler mode or not, using the NS CONTROL.SPSEL.
             */
            bool spsel = env->v7m.control[M_REG_NS] & R_V7M_CONTROL_SPSEL_MASK;
            bool is_psp = !arm_v7m_is_handler_mode(env) && spsel;
            uint32_t limit;

            if (!env->v7m.secure) {
                return;
            }

            limit = is_psp ? env->v7m.psplim[false] : env->v7m.msplim[false];

            val &= ~0x3;

            if (val < limit) {
                raise_exception_ra(env, EXCP_STKOF, 0, 1, GETPC());
            }

            if (is_psp) {
                env->v7m.other_ss_psp = val;
            } else {
                env->v7m.other_ss_msp = val;
            }
            return;
        }
        default:
            break;
        }
    }

    switch (reg) {
    case 0 ... 7: /* xPSR sub-fields */
        v7m_msr_xpsr(env, mask, reg, val);
        break;
    case 8: /* MSP */
        if (v7m_using_psp(env)) {
            env->v7m.other_sp = val & ~3;
        } else {
            env->regs[13] = val & ~3;
        }
        break;
    case 9: /* PSP */
        if (v7m_using_psp(env)) {
            env->regs[13] = val & ~3;
        } else {
            env->v7m.other_sp = val & ~3;
        }
        break;
    case 10: /* MSPLIM */
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        env->v7m.msplim[env->v7m.secure] = val & ~7;
        break;
    case 11: /* PSPLIM */
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            goto bad_reg;
        }
        env->v7m.psplim[env->v7m.secure] = val & ~7;
        break;
    case 16: /* PRIMASK */
        env->v7m.primask[env->v7m.secure] = val & 1;
        break;
    case 17: /* BASEPRI */
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        env->v7m.basepri[env->v7m.secure] = val & 0xff;
        break;
    case 18: /* BASEPRI_MAX */
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        val &= 0xff;
        if (val != 0 && (val < env->v7m.basepri[env->v7m.secure]
                         || env->v7m.basepri[env->v7m.secure] == 0)) {
            env->v7m.basepri[env->v7m.secure] = val;
        }
        break;
    case 19: /* FAULTMASK */
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            goto bad_reg;
        }
        env->v7m.faultmask[env->v7m.secure] = val & 1;
        break;
    case 20: /* CONTROL */
        /*
         * Writing to the SPSEL bit only has an effect if we are in
         * thread mode; other bits can be updated by any privileged code.
         * write_v7m_control_spsel() deals with updating the SPSEL bit in
         * env->v7m.control, so we only need update the others.
         * For v7M, we must just ignore explicit writes to SPSEL in handler
         * mode; for v8M the write is permitted but will have no effect.
         * All these bits are writes-ignored from non-privileged code,
         * except for SFPA.
         */
        if (cur_el > 0 && (arm_feature(env, ARM_FEATURE_V8) ||
                           !arm_v7m_is_handler_mode(env))) {
            write_v7m_control_spsel(env, (val & R_V7M_CONTROL_SPSEL_MASK) != 0);
        }
        if (cur_el > 0 && arm_feature(env, ARM_FEATURE_M_MAIN)) {
            env->v7m.control[env->v7m.secure] &= ~R_V7M_CONTROL_NPRIV_MASK;
            env->v7m.control[env->v7m.secure] |= val & R_V7M_CONTROL_NPRIV_MASK;
        }
        if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
            /*
             * SFPA is RAZ/WI from NS or if no FPU.
             * FPCA is RO if NSACR.CP10 == 0, RES0 if the FPU is not present.
             * Both are stored in the S bank.
             */
            if (env->v7m.secure) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_SFPA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_SFPA_MASK;
            }
            if (cur_el > 0 &&
                (env->v7m.secure || !arm_feature(env, ARM_FEATURE_M_SECURITY) ||
                 extract32(env->v7m.nsacr, 10, 1))) {
                env->v7m.control[M_REG_S] &= ~R_V7M_CONTROL_FPCA_MASK;
                env->v7m.control[M_REG_S] |= val & R_V7M_CONTROL_FPCA_MASK;
            }
        }
        break;
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR, "Attempt to write unknown special"
                                       " register %d\n", reg);
        return;
    }
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    /* Implement the TT instruction. op is bits [7:6] of the insn. */
    bool forceunpriv = op & 1;
    bool alt = op & 2;
    V8M_SAttributes sattrs = {};
    uint32_t tt_resp;
    bool r, rw, nsr, nsrw, mrvalid;
    ARMMMUIdx mmu_idx;
    uint32_t mregion;
    bool targetpriv;
    bool targetsec = env->v7m.secure;

    /*
     * Work out what the security state and privilege level we're
     * interested in is...
     */
    if (alt) {
        targetsec = !targetsec;
    }

    if (forceunpriv) {
        targetpriv = false;
    } else {
        targetpriv = arm_v7m_is_handler_mode(env) ||
            !(env->v7m.control[targetsec] & R_V7M_CONTROL_NPRIV_MASK);
    }

    /* ...and then figure out which MMU index this is */
    mmu_idx = arm_v7m_mmu_idx_for_secstate_and_priv(env, targetsec, targetpriv);

    /*
     * We know that the MPU and SAU don't care about the access type
     * for our purposes beyond that we don't want to claim to be
     * an insn fetch, so we arbitrarily call this a read.
     */

    /*
     * MPU region info only available for privileged or if
     * inspecting the other MPU state.
     */
    if (arm_current_el(env) != 0 || alt) {
        GetPhysAddrResult res = {};
        ARMMMUFaultInfo fi = {};

        /* We can ignore the return value as prot is always set */
        pmsav8_mpu_lookup(env, addr, MMU_DATA_LOAD, mmu_idx, targetsec,
                          &res, &fi, &mregion);
        if (mregion == -1) {
            mrvalid = false;
            mregion = 0;
        } else {
            mrvalid = true;
        }
        r = res.f.prot & PAGE_READ;
        rw = res.f.prot & PAGE_WRITE;
    } else {
        r = false;
        rw = false;
        mrvalid = false;
        mregion = 0;
    }

    if (env->v7m.secure) {
        v8m_security_lookup(env, addr, MMU_DATA_LOAD, mmu_idx,
                            targetsec, &sattrs);
        nsr = sattrs.ns && r;
        nsrw = sattrs.ns && rw;
    } else {
        sattrs.ns = true;
        nsr = false;
        nsrw = false;
    }

    tt_resp = (sattrs.iregion << 24) |
        (sattrs.irvalid << 23) |
        ((!sattrs.ns) << 22) |
        (nsrw << 21) |
        (nsr << 20) |
        (rw << 19) |
        (r << 18) |
        (sattrs.srvalid << 17) |
        (mrvalid << 16) |
        (sattrs.sregion << 8) |
        mregion;

    return tt_resp;
}

#endif /* !CONFIG_USER_ONLY */

uint32_t *arm_v7m_get_sp_ptr(CPUARMState *env, bool secure, bool threadmode,
                             bool spsel)
{
    /*
     * Return a pointer to the location where we currently store the
     * stack pointer for the requested security state and thread mode.
     * This pointer will become invalid if the CPU state is updated
     * such that the stack pointers are switched around (eg changing
     * the SPSEL control bit).
     * Compare the v8M ARM ARM pseudocode LookUpSP_with_security_mode().
     * Unlike that pseudocode, we require the caller to pass us in the
     * SPSEL control bit value; this is because we also use this
     * function in handling of pushing of the callee-saves registers
     * part of the v8M stack frame (pseudocode PushCalleeStack()),
     * and in the tailchain codepath the SPSEL bit comes from the exception
     * return magic LR value from the previous exception. The pseudocode
     * opencodes the stack-selection in PushCalleeStack(), but we prefer
     * to make this utility function generic enough to do the job.
     */
    bool want_psp = threadmode && spsel;

    if (secure == env->v7m.secure) {
        if (want_psp == v7m_using_psp(env)) {
            return &env->regs[13];
        } else {
            return &env->v7m.other_sp;
        }
    } else {
        if (want_psp) {
            return &env->v7m.other_ss_psp;
        } else {
            return &env->v7m.other_ss_msp;
        }
    }
}
