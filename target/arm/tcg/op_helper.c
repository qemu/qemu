/*
 *  ARM helper routines
 *
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "cpu-features.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "cpregs.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

int exception_target_el(CPUARMState *env)
{
    int target_el = MAX(1, arm_current_el(env));

    /*
     * No such thing as secure EL1 if EL3 is aarch32,
     * so update the target EL to EL3 in this case.
     */
    if (arm_is_secure(env) && !arm_el_is_aa64(env, 3) && target_el == 1) {
        target_el = 3;
    }

    return target_el;
}

void raise_exception(CPUARMState *env, uint32_t excp,
                     uint32_t syndrome, uint32_t target_el)
{
    CPUState *cs = env_cpu(env);

    if (target_el == 1 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        /*
         * Redirect NS EL1 exceptions to NS EL2. These are reported with
         * their original syndrome register value, with the exception of
         * SIMD/FP access traps, which are reported as uncategorized
         * (see DDI0478C.a D1.10.4)
         */
        target_el = 2;
        if (syn_get_ec(syndrome) == EC_ADVSIMDFPACCESSTRAP) {
            syndrome = syn_uncategorized();
        }
    }

    assert(!excp_is_internal(excp));
    cs->exception_index = excp;
    env->exception.syndrome = syndrome;
    env->exception.target_el = target_el;
    cpu_loop_exit(cs);
}

void raise_exception_ra(CPUARMState *env, uint32_t excp, uint32_t syndrome,
                        uint32_t target_el, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    /*
     * restore_state_to_opc() will set env->exception.syndrome, so
     * we must restore CPU state here before setting the syndrome
     * the caller passed us, and cannot use cpu_loop_exit_restore().
     */
    cpu_restore_state(cs, ra);
    raise_exception(env, excp, syndrome, target_el);
}

uint64_t HELPER(neon_tbl)(CPUARMState *env, uint32_t desc,
                          uint64_t ireg, uint64_t def)
{
    uint64_t tmp, val = 0;
    uint32_t maxindex = ((desc & 3) + 1) * 8;
    uint32_t base_reg = desc >> 2;
    uint32_t shift, index, reg;

    for (shift = 0; shift < 64; shift += 8) {
        index = (ireg >> shift) & 0xff;
        if (index < maxindex) {
            reg = base_reg + (index >> 3);
            tmp = *aa32_vfp_dreg(env, reg);
            tmp = ((tmp >> ((index & 7) << 3)) & 0xff) << shift;
        } else {
            tmp = def & (0xffull << shift);
        }
        val |= tmp;
    }
    return val;
}

void HELPER(v8m_stackcheck)(CPUARMState *env, uint32_t newvalue)
{
    /*
     * Perform the v8M stack limit check for SP updates from translated code,
     * raising an exception if the limit is breached.
     */
    if (newvalue < v7m_sp_limit(env)) {
        /*
         * Stack limit exceptions are a rare case, so rather than syncing
         * PC/condbits before the call, we use raise_exception_ra() so
         * that cpu_restore_state() will sort them out.
         */
        raise_exception_ra(env, EXCP_STKOF, 0, 1, GETPC());
    }
}

uint32_t HELPER(add_setq)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT))
        env->QF = 1;
    return res;
}

uint32_t HELPER(add_saturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT) && !((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(sub_saturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (((res ^ a) & SIGNBIT) && ((a ^ b) & SIGNBIT)) {
        env->QF = 1;
        res = ~(((int32_t)a >> 31) ^ SIGNBIT);
    }
    return res;
}

uint32_t HELPER(add_usaturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (res < a) {
        env->QF = 1;
        res = ~0;
    }
    return res;
}

uint32_t HELPER(sub_usaturate)(CPUARMState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (res > a) {
        env->QF = 1;
        res = 0;
    }
    return res;
}

/* Signed saturation.  */
static inline uint32_t do_ssat(CPUARMState *env, int32_t val, int shift)
{
    int32_t top;
    uint32_t mask;

    top = val >> shift;
    mask = (1u << shift) - 1;
    if (top > 0) {
        env->QF = 1;
        return mask;
    } else if (top < -1) {
        env->QF = 1;
        return ~mask;
    }
    return val;
}

/* Unsigned saturation.  */
static inline uint32_t do_usat(CPUARMState *env, int32_t val, int shift)
{
    uint32_t max;

    max = (1u << shift) - 1;
    if (val < 0) {
        env->QF = 1;
        return 0;
    } else if (val > max) {
        env->QF = 1;
        return max;
    }
    return val;
}

/* Signed saturate.  */
uint32_t HELPER(ssat)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    return do_ssat(env, x, shift);
}

/* Dual halfword signed saturate.  */
uint32_t HELPER(ssat16)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_ssat(env, (int16_t)x, shift);
    res |= do_ssat(env, ((int32_t)x) >> 16, shift) << 16;
    return res;
}

/* Unsigned saturate.  */
uint32_t HELPER(usat)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    return do_usat(env, x, shift);
}

/* Dual halfword unsigned saturate.  */
uint32_t HELPER(usat16)(CPUARMState *env, uint32_t x, uint32_t shift)
{
    uint32_t res;

    res = (uint16_t)do_usat(env, (int16_t)x, shift);
    res |= do_usat(env, ((int32_t)x) >> 16, shift) << 16;
    return res;
}

void HELPER(setend)(CPUARMState *env)
{
    env->uncached_cpsr ^= CPSR_E;
    arm_rebuild_hflags(env);
}

void HELPER(check_bxj_trap)(CPUARMState *env, uint32_t rm)
{
    /*
     * Only called if in NS EL0 or EL1 for a BXJ for a v7A CPU;
     * check if HSTR.TJDBX means we need to trap to EL2.
     */
    if (env->cp15.hstr_el2 & HSTR_TJDBX) {
        /*
         * We know the condition code check passed, so take the IMPDEF
         * choice to always report CV=1 COND 0xe
         */
        uint32_t syn = syn_bxjtrap(1, 0xe, rm);
        raise_exception_ra(env, EXCP_HYP_TRAP, syn, 2, GETPC());
    }
}

#ifndef CONFIG_USER_ONLY
/* Function checks whether WFx (WFI/WFE) instructions are set up to be trapped.
 * The function returns the target EL (1-3) if the instruction is to be trapped;
 * otherwise it returns 0 indicating it is not trapped.
 */
static inline int check_wfx_trap(CPUARMState *env, bool is_wfe)
{
    int cur_el = arm_current_el(env);
    uint64_t mask;

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* M profile cores can never trap WFI/WFE. */
        return 0;
    }

    /* If we are currently in EL0 then we need to check if SCTLR is set up for
     * WFx instructions being trapped to EL1. These trap bits don't exist in v7.
     */
    if (cur_el < 1 && arm_feature(env, ARM_FEATURE_V8)) {
        int target_el;

        mask = is_wfe ? SCTLR_nTWE : SCTLR_nTWI;
        if (arm_is_secure_below_el3(env) && !arm_el_is_aa64(env, 3)) {
            /* Secure EL0 and Secure PL1 is at EL3 */
            target_el = 3;
        } else {
            target_el = 1;
        }

        if (!(env->cp15.sctlr_el[target_el] & mask)) {
            return target_el;
        }
    }

    /* We are not trapping to EL1; trap to EL2 if HCR_EL2 requires it
     * No need for ARM_FEATURE check as if HCR_EL2 doesn't exist the
     * bits will be zero indicating no trap.
     */
    if (cur_el < 2) {
        mask = is_wfe ? HCR_TWE : HCR_TWI;
        if (arm_hcr_el2_eff(env) & mask) {
            return 2;
        }
    }

    /* We are not trapping to EL1 or EL2; trap to EL3 if SCR_EL3 requires it */
    if (cur_el < 3) {
        mask = (is_wfe) ? SCR_TWE : SCR_TWI;
        if (env->cp15.scr_el3 & mask) {
            return 3;
        }
    }

    return 0;
}
#endif

void HELPER(wfi)(CPUARMState *env, uint32_t insn_len)
{
#ifdef CONFIG_USER_ONLY
    /*
     * WFI in the user-mode emulator is technically permitted but not
     * something any real-world code would do. AArch64 Linux kernels
     * trap it via SCTRL_EL1.nTWI and make it an (expensive) NOP;
     * AArch32 kernels don't trap it so it will delay a bit.
     * For QEMU, make it NOP here, because trying to raise EXCP_HLT
     * would trigger an abort.
     */
    return;
#else
    CPUState *cs = env_cpu(env);
    int target_el = check_wfx_trap(env, false);

    if (cpu_has_work(cs)) {
        /* Don't bother to go into our "low power state" if
         * we would just wake up immediately.
         */
        return;
    }

    if (target_el) {
        if (env->aarch64) {
            env->pc -= insn_len;
        } else {
            env->regs[15] -= insn_len;
        }

        raise_exception(env, EXCP_UDEF, syn_wfx(1, 0xe, 0, insn_len == 2),
                        target_el);
    }

    cs->exception_index = EXCP_HLT;
    cs->halted = 1;
    cpu_loop_exit(cs);
#endif
}

void HELPER(wfe)(CPUARMState *env)
{
    /* This is a hint instruction that is semantically different
     * from YIELD even though we currently implement it identically.
     * Don't actually halt the CPU, just yield back to top
     * level loop. This is not going into a "low power state"
     * (ie halting until some event occurs), so we never take
     * a configurable trap to a different exception level.
     */
    HELPER(yield)(env);
}

void HELPER(yield)(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);

    /* This is a non-trappable hint instruction that generally indicates
     * that the guest is currently busy-looping. Yield control back to the
     * top level loop so that a more deserving VCPU has a chance to run.
     */
    cs->exception_index = EXCP_YIELD;
    cpu_loop_exit(cs);
}

/* Raise an internal-to-QEMU exception. This is limited to only
 * those EXCP values which are special cases for QEMU to interrupt
 * execution and not to be used for exceptions which are passed to
 * the guest (those must all have syndrome information and thus should
 * use exception_with_syndrome*).
 */
void HELPER(exception_internal)(CPUARMState *env, uint32_t excp)
{
    CPUState *cs = env_cpu(env);

    assert(excp_is_internal(excp));
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

/* Raise an exception with the specified syndrome register value */
void HELPER(exception_with_syndrome_el)(CPUARMState *env, uint32_t excp,
                                        uint32_t syndrome, uint32_t target_el)
{
    raise_exception(env, excp, syndrome, target_el);
}

/*
 * Raise an exception with the specified syndrome register value
 * to the default target el.
 */
void HELPER(exception_with_syndrome)(CPUARMState *env, uint32_t excp,
                                     uint32_t syndrome)
{
    raise_exception(env, excp, syndrome, exception_target_el(env));
}

uint32_t HELPER(cpsr_read)(CPUARMState *env)
{
    return cpsr_read(env) & ~CPSR_EXEC;
}

void HELPER(cpsr_write)(CPUARMState *env, uint32_t val, uint32_t mask)
{
    cpsr_write(env, val, mask, CPSRWriteByInstr);
    /* TODO: Not all cpsr bits are relevant to hflags.  */
    arm_rebuild_hflags(env);
}

/* Write the CPSR for a 32-bit exception return */
void HELPER(cpsr_write_eret)(CPUARMState *env, uint32_t val)
{
    uint32_t mask;

    qemu_mutex_lock_iothread();
    arm_call_pre_el_change_hook(env_archcpu(env));
    qemu_mutex_unlock_iothread();

    mask = aarch32_cpsr_valid_mask(env->features, &env_archcpu(env)->isar);
    cpsr_write(env, val, mask, CPSRWriteExceptionReturn);

    /* Generated code has already stored the new PC value, but
     * without masking out its low bits, because which bits need
     * masking depends on whether we're returning to Thumb or ARM
     * state. Do the masking now.
     */
    env->regs[15] &= (env->thumb ? ~1 : ~3);
    arm_rebuild_hflags(env);

    qemu_mutex_lock_iothread();
    arm_call_el_change_hook(env_archcpu(env));
    qemu_mutex_unlock_iothread();
}

/* Access to user mode registers from privileged modes.  */
uint32_t HELPER(get_user_reg)(CPUARMState *env, uint32_t regno)
{
    uint32_t val;

    if (regno == 13) {
        val = env->banked_r13[BANK_USRSYS];
    } else if (regno == 14) {
        val = env->banked_r14[BANK_USRSYS];
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        val = env->usr_regs[regno - 8];
    } else {
        val = env->regs[regno];
    }
    return val;
}

void HELPER(set_user_reg)(CPUARMState *env, uint32_t regno, uint32_t val)
{
    if (regno == 13) {
        env->banked_r13[BANK_USRSYS] = val;
    } else if (regno == 14) {
        env->banked_r14[BANK_USRSYS] = val;
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        env->usr_regs[regno - 8] = val;
    } else {
        env->regs[regno] = val;
    }
}

void HELPER(set_r13_banked)(CPUARMState *env, uint32_t mode, uint32_t val)
{
    if ((env->uncached_cpsr & CPSR_M) == mode) {
        env->regs[13] = val;
    } else {
        env->banked_r13[bank_number(mode)] = val;
    }
}

uint32_t HELPER(get_r13_banked)(CPUARMState *env, uint32_t mode)
{
    if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_SYS) {
        /* SRS instruction is UNPREDICTABLE from System mode; we UNDEF.
         * Other UNPREDICTABLE and UNDEF cases were caught at translate time.
         */
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }

    if ((env->uncached_cpsr & CPSR_M) == mode) {
        return env->regs[13];
    } else {
        return env->banked_r13[bank_number(mode)];
    }
}

static void msr_mrs_banked_exc_checks(CPUARMState *env, uint32_t tgtmode,
                                      uint32_t regno)
{
    /* Raise an exception if the requested access is one of the UNPREDICTABLE
     * cases; otherwise return. This broadly corresponds to the pseudocode
     * BankedRegisterAccessValid() and SPSRAccessValid(),
     * except that we have already handled some cases at translate time.
     */
    int curmode = env->uncached_cpsr & CPSR_M;

    if (regno == 17) {
        /* ELR_Hyp: a special case because access from tgtmode is OK */
        if (curmode != ARM_CPU_MODE_HYP && curmode != ARM_CPU_MODE_MON) {
            goto undef;
        }
        return;
    }

    if (curmode == tgtmode) {
        goto undef;
    }

    if (tgtmode == ARM_CPU_MODE_USR) {
        switch (regno) {
        case 8 ... 12:
            if (curmode != ARM_CPU_MODE_FIQ) {
                goto undef;
            }
            break;
        case 13:
            if (curmode == ARM_CPU_MODE_SYS) {
                goto undef;
            }
            break;
        case 14:
            if (curmode == ARM_CPU_MODE_HYP || curmode == ARM_CPU_MODE_SYS) {
                goto undef;
            }
            break;
        default:
            break;
        }
    }

    if (tgtmode == ARM_CPU_MODE_HYP) {
        /* SPSR_Hyp, r13_hyp: accessible from Monitor mode only */
        if (curmode != ARM_CPU_MODE_MON) {
            goto undef;
        }
    }

    return;

undef:
    raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                    exception_target_el(env));
}

void HELPER(msr_banked)(CPUARMState *env, uint32_t value, uint32_t tgtmode,
                        uint32_t regno)
{
    msr_mrs_banked_exc_checks(env, tgtmode, regno);

    switch (regno) {
    case 16: /* SPSRs */
        env->banked_spsr[bank_number(tgtmode)] = value;
        break;
    case 17: /* ELR_Hyp */
        env->elr_el[2] = value;
        break;
    case 13:
        env->banked_r13[bank_number(tgtmode)] = value;
        break;
    case 14:
        env->banked_r14[r14_bank_number(tgtmode)] = value;
        break;
    case 8 ... 12:
        switch (tgtmode) {
        case ARM_CPU_MODE_USR:
            env->usr_regs[regno - 8] = value;
            break;
        case ARM_CPU_MODE_FIQ:
            env->fiq_regs[regno - 8] = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        g_assert_not_reached();
    }
}

uint32_t HELPER(mrs_banked)(CPUARMState *env, uint32_t tgtmode, uint32_t regno)
{
    msr_mrs_banked_exc_checks(env, tgtmode, regno);

    switch (regno) {
    case 16: /* SPSRs */
        return env->banked_spsr[bank_number(tgtmode)];
    case 17: /* ELR_Hyp */
        return env->elr_el[2];
    case 13:
        return env->banked_r13[bank_number(tgtmode)];
    case 14:
        return env->banked_r14[r14_bank_number(tgtmode)];
    case 8 ... 12:
        switch (tgtmode) {
        case ARM_CPU_MODE_USR:
            return env->usr_regs[regno - 8];
        case ARM_CPU_MODE_FIQ:
            return env->fiq_regs[regno - 8];
        default:
            g_assert_not_reached();
        }
    default:
        g_assert_not_reached();
    }
}

const void *HELPER(access_check_cp_reg)(CPUARMState *env, uint32_t key,
                                        uint32_t syndrome, uint32_t isread)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    CPAccessResult res = CP_ACCESS_OK;
    int target_el;

    assert(ri != NULL);

    if (arm_feature(env, ARM_FEATURE_XSCALE) && ri->cp < 14
        && extract32(env->cp15.c15_cpar, ri->cp, 1) == 0) {
        res = CP_ACCESS_TRAP;
        goto fail;
    }

    if (ri->accessfn) {
        res = ri->accessfn(env, ri, isread);
    }

    /*
     * If the access function indicates a trap from EL0 to EL1 then
     * that always takes priority over the HSTR_EL2 trap. (If it indicates
     * a trap to EL3, then the HSTR_EL2 trap takes priority; if it indicates
     * a trap to EL2, then the syndrome is the same either way so we don't
     * care whether technically the architecture says that HSTR_EL2 trap or
     * the other trap takes priority. So we take the "check HSTR_EL2" path
     * for all of those cases.)
     */
    if (res != CP_ACCESS_OK && ((res & CP_ACCESS_EL_MASK) == 0) &&
        arm_current_el(env) == 0) {
        goto fail;
    }

    /*
     * HSTR_EL2 traps from EL1 are checked earlier, in generated code;
     * we only need to check here for traps from EL0.
     */
    if (!is_a64(env) && arm_current_el(env) == 0 && ri->cp == 15 &&
        arm_is_el2_enabled(env) &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        uint32_t mask = 1 << ri->crn;

        if (ri->type & ARM_CP_64BIT) {
            mask = 1 << ri->crm;
        }

        /* T4 and T14 are RES0 */
        mask &= ~((1 << 4) | (1 << 14));

        if (env->cp15.hstr_el2 & mask) {
            res = CP_ACCESS_TRAP_EL2;
            goto fail;
        }
    }

    /*
     * Fine-grained traps also are lower priority than undef-to-EL1,
     * higher priority than trap-to-EL3, and we don't care about priority
     * order with other EL2 traps because the syndrome value is the same.
     */
    if (arm_fgt_active(env, arm_current_el(env))) {
        uint64_t trapword = 0;
        unsigned int idx = FIELD_EX32(ri->fgt, FGT, IDX);
        unsigned int bitpos = FIELD_EX32(ri->fgt, FGT, BITPOS);
        bool rev = FIELD_EX32(ri->fgt, FGT, REV);
        bool trapbit;

        if (ri->fgt & FGT_EXEC) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_exec));
            trapword = env->cp15.fgt_exec[idx];
        } else if (isread && (ri->fgt & FGT_R)) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_read));
            trapword = env->cp15.fgt_read[idx];
        } else if (!isread && (ri->fgt & FGT_W)) {
            assert(idx < ARRAY_SIZE(env->cp15.fgt_write));
            trapword = env->cp15.fgt_write[idx];
        }

        trapbit = extract64(trapword, bitpos, 1);
        if (trapbit != rev) {
            res = CP_ACCESS_TRAP_EL2;
            goto fail;
        }
    }

    if (likely(res == CP_ACCESS_OK)) {
        return ri;
    }

 fail:
    switch (res & ~CP_ACCESS_EL_MASK) {
    case CP_ACCESS_TRAP:
        break;
    case CP_ACCESS_TRAP_UNCATEGORIZED:
        /* Only CP_ACCESS_TRAP traps are direct to a specified EL */
        assert((res & CP_ACCESS_EL_MASK) == 0);
        if (cpu_isar_feature(aa64_ids, cpu) && isread &&
            arm_cpreg_in_idspace(ri)) {
            /*
             * FEAT_IDST says this should be reported as EC_SYSTEMREGISTERTRAP,
             * not EC_UNCATEGORIZED
             */
            break;
        }
        syndrome = syn_uncategorized();
        break;
    default:
        g_assert_not_reached();
    }

    target_el = res & CP_ACCESS_EL_MASK;
    switch (target_el) {
    case 0:
        target_el = exception_target_el(env);
        break;
    case 2:
        assert(arm_current_el(env) != 3);
        assert(arm_is_el2_enabled(env));
        break;
    case 3:
        assert(arm_feature(env, ARM_FEATURE_EL3));
        break;
    default:
        /* No "direct" traps to EL1 */
        g_assert_not_reached();
    }

    raise_exception(env, EXCP_UDEF, syndrome, target_el);
}

const void *HELPER(lookup_cp_reg)(CPUARMState *env, uint32_t key)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(cpu->cp_regs, key);

    assert(ri != NULL);
    return ri;
}

/*
 * Test for HCR_EL2.TIDCP at EL1.
 * Since implementation defined registers are rare, and within QEMU
 * most of them are no-op, do not waste HFLAGS space for this and
 * always use a helper.
 */
void HELPER(tidcp_el1)(CPUARMState *env, uint32_t syndrome)
{
    if (arm_hcr_el2_eff(env) & HCR_TIDCP) {
        raise_exception_ra(env, EXCP_UDEF, syndrome, 2, GETPC());
    }
}

/*
 * Similarly, for FEAT_TIDCP1 at EL0.
 * We have already checked for the presence of the feature.
 */
void HELPER(tidcp_el0)(CPUARMState *env, uint32_t syndrome)
{
    /* See arm_sctlr(), but we also need the sctlr el. */
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, 0);
    int target_el = mmu_idx == ARMMMUIdx_E20_0 ? 2 : 1;

    /*
     * The bit is not valid unless the target el is aa64, but since the
     * bit test is simpler perform that first and check validity after.
     */
    if ((env->cp15.sctlr_el[target_el] & SCTLR_TIDCP)
        && arm_el_is_aa64(env, target_el)) {
        raise_exception_ra(env, EXCP_UDEF, syndrome, target_el, GETPC());
    }
}

void HELPER(set_cp_reg)(CPUARMState *env, const void *rip, uint32_t value)
{
    const ARMCPRegInfo *ri = rip;

    if (ri->type & ARM_CP_IO) {
        qemu_mutex_lock_iothread();
        ri->writefn(env, ri, value);
        qemu_mutex_unlock_iothread();
    } else {
        ri->writefn(env, ri, value);
    }
}

uint32_t HELPER(get_cp_reg)(CPUARMState *env, const void *rip)
{
    const ARMCPRegInfo *ri = rip;
    uint32_t res;

    if (ri->type & ARM_CP_IO) {
        qemu_mutex_lock_iothread();
        res = ri->readfn(env, ri);
        qemu_mutex_unlock_iothread();
    } else {
        res = ri->readfn(env, ri);
    }

    return res;
}

void HELPER(set_cp_reg64)(CPUARMState *env, const void *rip, uint64_t value)
{
    const ARMCPRegInfo *ri = rip;

    if (ri->type & ARM_CP_IO) {
        qemu_mutex_lock_iothread();
        ri->writefn(env, ri, value);
        qemu_mutex_unlock_iothread();
    } else {
        ri->writefn(env, ri, value);
    }
}

uint64_t HELPER(get_cp_reg64)(CPUARMState *env, const void *rip)
{
    const ARMCPRegInfo *ri = rip;
    uint64_t res;

    if (ri->type & ARM_CP_IO) {
        qemu_mutex_lock_iothread();
        res = ri->readfn(env, ri);
        qemu_mutex_unlock_iothread();
    } else {
        res = ri->readfn(env, ri);
    }

    return res;
}

void HELPER(pre_hvc)(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    int cur_el = arm_current_el(env);
    /* FIXME: Use actual secure state.  */
    bool secure = false;
    bool undef;

    if (arm_is_psci_call(cpu, EXCP_HVC)) {
        /* If PSCI is enabled and this looks like a valid PSCI call then
         * that overrides the architecturally mandated HVC behaviour.
         */
        return;
    }

    if (!arm_feature(env, ARM_FEATURE_EL2)) {
        /* If EL2 doesn't exist, HVC always UNDEFs */
        undef = true;
    } else if (arm_feature(env, ARM_FEATURE_EL3)) {
        /* EL3.HCE has priority over EL2.HCD. */
        undef = !(env->cp15.scr_el3 & SCR_HCE);
    } else {
        undef = env->cp15.hcr_el2 & HCR_HCD;
    }

    /* In ARMv7 and ARMv8/AArch32, HVC is undef in secure state.
     * For ARMv8/AArch64, HVC is allowed in EL3.
     * Note that we've already trapped HVC from EL0 at translation
     * time.
     */
    if (secure && (!is_a64(env) || cur_el == 1)) {
        undef = true;
    }

    if (undef) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }
}

void HELPER(pre_smc)(CPUARMState *env, uint32_t syndrome)
{
    ARMCPU *cpu = env_archcpu(env);
    int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    bool smd_flag = env->cp15.scr_el3 & SCR_SMD;

    /*
     * SMC behaviour is summarized in the following table.
     * This helper handles the "Trap to EL2" and "Undef insn" cases.
     * The "Trap to EL3" and "PSCI call" cases are handled in the exception
     * helper.
     *
     *  -> ARM_FEATURE_EL3 and !SMD
     *                           HCR_TSC && NS EL1   !HCR_TSC || !NS EL1
     *
     *  Conduit SMC, valid call  Trap to EL2         PSCI Call
     *  Conduit SMC, inval call  Trap to EL2         Trap to EL3
     *  Conduit not SMC          Trap to EL2         Trap to EL3
     *
     *
     *  -> ARM_FEATURE_EL3 and SMD
     *                           HCR_TSC && NS EL1   !HCR_TSC || !NS EL1
     *
     *  Conduit SMC, valid call  Trap to EL2         PSCI Call
     *  Conduit SMC, inval call  Trap to EL2         Undef insn
     *  Conduit not SMC          Trap to EL2         Undef insn
     *
     *
     *  -> !ARM_FEATURE_EL3
     *                           HCR_TSC && NS EL1   !HCR_TSC || !NS EL1
     *
     *  Conduit SMC, valid call  Trap to EL2         PSCI Call
     *  Conduit SMC, inval call  Trap to EL2         Undef insn
     *  Conduit not SMC          Undef insn          Undef insn
     */

    /* On ARMv8 with EL3 AArch64, SMD applies to both S and NS state.
     * On ARMv8 with EL3 AArch32, or ARMv7 with the Virtualization
     *  extensions, SMD only applies to NS state.
     * On ARMv7 without the Virtualization extensions, the SMD bit
     * doesn't exist, but we forbid the guest to set it to 1 in scr_write(),
     * so we need not special case this here.
     */
    bool smd = arm_feature(env, ARM_FEATURE_AARCH64) ? smd_flag
                                                     : smd_flag && !secure;

    if (!arm_feature(env, ARM_FEATURE_EL3) &&
        cpu->psci_conduit != QEMU_PSCI_CONDUIT_SMC) {
        /* If we have no EL3 then SMC always UNDEFs and can't be
         * trapped to EL2. PSCI-via-SMC is a sort of ersatz EL3
         * firmware within QEMU, and we want an EL2 guest to be able
         * to forbid its EL1 from making PSCI calls into QEMU's
         * "firmware" via HCR.TSC, so for these purposes treat
         * PSCI-via-SMC as implying an EL3.
         * This handles the very last line of the previous table.
         */
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }

    if (cur_el == 1 && (arm_hcr_el2_eff(env) & HCR_TSC)) {
        /* In NS EL1, HCR controlled routing to EL2 has priority over SMD.
         * We also want an EL2 guest to be able to forbid its EL1 from
         * making PSCI calls into QEMU's "firmware" via HCR.TSC.
         * This handles all the "Trap to EL2" cases of the previous table.
         */
        raise_exception(env, EXCP_HYP_TRAP, syndrome, 2);
    }

    /* Catch the two remaining "Undef insn" cases of the previous table:
     *    - PSCI conduit is SMC but we don't have a valid PCSI call,
     *    - We don't have EL3 or SMD is set.
     */
    if (!arm_is_psci_call(cpu, EXCP_SMC) &&
        (smd || !arm_feature(env, ARM_FEATURE_EL3))) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }
}

/* ??? Flag setting arithmetic is awkward because we need to do comparisons.
   The only way to do that in TCG is a conditional branch, which clobbers
   all our temporaries.  For now implement these as helper functions.  */

/* Similarly for variable shift instructions.  */

uint32_t HELPER(shl_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = x & 1;
        else
            env->CF = 0;
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (32 - shift)) & 1;
        return x << shift;
    }
    return x;
}

uint32_t HELPER(shr_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = (x >> 31) & 1;
        else
            env->CF = 0;
        return 0;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return x >> shift;
    }
    return x;
}

uint32_t HELPER(sar_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift = i & 0xff;
    if (shift >= 32) {
        env->CF = (x >> 31) & 1;
        return (int32_t)x >> 31;
    } else if (shift != 0) {
        env->CF = (x >> (shift - 1)) & 1;
        return (int32_t)x >> shift;
    }
    return x;
}

uint32_t HELPER(ror_cc)(CPUARMState *env, uint32_t x, uint32_t i)
{
    int shift1, shift;
    shift1 = i & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0)
            env->CF = (x >> 31) & 1;
        return x;
    } else {
        env->CF = (x >> (shift - 1)) & 1;
        return ((uint32_t)x >> shift) | (x << (32 - shift));
    }
}

void HELPER(probe_access)(CPUARMState *env, target_ulong ptr,
                          uint32_t access_type, uint32_t mmu_idx,
                          uint32_t size)
{
    uint32_t in_page = -((uint32_t)ptr | TARGET_PAGE_SIZE);
    uintptr_t ra = GETPC();

    if (likely(size <= in_page)) {
        probe_access(env, ptr, size, access_type, mmu_idx, ra);
    } else {
        probe_access(env, ptr, in_page, access_type, mmu_idx, ra);
        probe_access(env, ptr + in_page, size - in_page,
                     access_type, mmu_idx, ra);
    }
}

/*
 * This function corresponds to AArch64.vESBOperation().
 * Note that the AArch32 version is not functionally different.
 */
void HELPER(vesb)(CPUARMState *env)
{
    /*
     * The EL2Enabled() check is done inside arm_hcr_el2_eff,
     * and will return HCR_EL2.VSE == 0, so nothing happens.
     */
    uint64_t hcr = arm_hcr_el2_eff(env);
    bool enabled = !(hcr & HCR_TGE) && (hcr & HCR_AMO);
    bool pending = enabled && (hcr & HCR_VSE);
    bool masked  = (env->daif & PSTATE_A);

    /* If VSE pending and masked, defer the exception.  */
    if (pending && masked) {
        uint32_t syndrome;

        if (arm_el_is_aa64(env, 1)) {
            /* Copy across IDS and ISS from VSESR. */
            syndrome = env->cp15.vsesr_el2 & 0x1ffffff;
        } else {
            ARMMMUFaultInfo fi = { .type = ARMFault_AsyncExternal };

            if (extended_addresses_enabled(env)) {
                syndrome = arm_fi_to_lfsc(&fi);
            } else {
                syndrome = arm_fi_to_sfsc(&fi);
            }
            /* Copy across AET and ExT from VSESR. */
            syndrome |= env->cp15.vsesr_el2 & 0xd000;
        }

        /* Set VDISR_EL2.A along with the syndrome. */
        env->cp15.vdisr_el2 = syndrome | (1u << 31);

        /* Clear pending virtual SError */
        env->cp15.hcr_el2 &= ~HCR_VSE;
        cpu_reset_interrupt(env_cpu(env), CPU_INTERRUPT_VSERR);
    }
}
