/*
 *  ARM helper routines
 *
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

static void raise_exception(CPUARMState *env, uint32_t excp,
                            uint32_t syndrome, uint32_t target_el)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));

    assert(!excp_is_internal(excp));
    cs->exception_index = excp;
    env->exception.syndrome = syndrome;
    env->exception.target_el = target_el;
    cpu_loop_exit(cs);
}

static int exception_target_el(CPUARMState *env)
{
    int target_el = MAX(1, arm_current_el(env));

    /* No such thing as secure EL1 if EL3 is aarch32, so update the target EL
     * to EL3 in this case.
     */
    if (arm_is_secure(env) && !arm_el_is_aa64(env, 3) && target_el == 1) {
        target_el = 3;
    }

    return target_el;
}

uint32_t HELPER(neon_tbl)(CPUARMState *env, uint32_t ireg, uint32_t def,
                          uint32_t rn, uint32_t maxindex)
{
    uint32_t val;
    uint32_t tmp;
    int index;
    int shift;
    uint64_t *table;
    table = (uint64_t *)&env->vfp.regs[rn];
    val = 0;
    for (shift = 0; shift < 32; shift += 8) {
        index = (ireg >> shift) & 0xff;
        if (index < maxindex) {
            tmp = (table[index >> 3] >> ((index & 7) << 3)) & 0xff;
            val |= tmp << shift;
        } else {
            val |= def & (0xff << shift);
        }
    }
    return val;
}

#if !defined(CONFIG_USER_ONLY)

static inline uint32_t merge_syn_data_abort(uint32_t template_syn,
                                            unsigned int target_el,
                                            bool same_el,
                                            bool s1ptw, int is_write,
                                            int fsc)
{
    uint32_t syn;

    /* ISV is only set for data aborts routed to EL2 and
     * never for stage-1 page table walks faulting on stage 2.
     *
     * Furthermore, ISV is only set for certain kinds of load/stores.
     * If the template syndrome does not have ISV set, we should leave
     * it cleared.
     *
     * See ARMv8 specs, D7-1974:
     * ISS encoding for an exception from a Data Abort, the
     * ISV field.
     */
    if (!(template_syn & ARM_EL_ISV) || target_el != 2 || s1ptw) {
        syn = syn_data_abort_no_iss(same_el,
                                    0, 0, s1ptw, is_write == 1, fsc);
    } else {
        /* Fields: IL, ISV, SAS, SSE, SRT, SF and AR come from the template
         * syndrome created at translation time.
         * Now we create the runtime syndrome with the remaining fields.
         */
        syn = syn_data_abort_with_iss(same_el,
                                      0, 0, 0, 0, 0,
                                      0, 0, s1ptw, is_write == 1, fsc,
                                      false);
        /* Merge the runtime syndrome with the template syndrome.  */
        syn |= template_syn;
    }
    return syn;
}

/* try to fill the TLB and return an exception if error. If retaddr is
 * NULL, it means that the function was called in C code (i.e. not
 * from generated code or from helper.c)
 */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    bool ret;
    uint32_t fsr = 0;
    ARMMMUFaultInfo fi = {};

    ret = arm_tlb_fill(cs, addr, is_write, mmu_idx, &fsr, &fi);
    if (unlikely(ret)) {
        ARMCPU *cpu = ARM_CPU(cs);
        CPUARMState *env = &cpu->env;
        uint32_t syn, exc;
        unsigned int target_el;
        bool same_el;

        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }

        target_el = exception_target_el(env);
        if (fi.stage2) {
            target_el = 2;
            env->cp15.hpfar_el2 = extract64(fi.s2addr, 12, 47) << 4;
        }
        same_el = arm_current_el(env) == target_el;
        /* AArch64 syndrome does not have an LPAE bit */
        syn = fsr & ~(1 << 9);

        /* For insn and data aborts we assume there is no instruction syndrome
         * information; this is always true for exceptions reported to EL1.
         */
        if (is_write == 2) {
            syn = syn_insn_abort(same_el, 0, fi.s1ptw, syn);
            exc = EXCP_PREFETCH_ABORT;
        } else {
            syn = merge_syn_data_abort(env->exception.syndrome, target_el,
                                       same_el, fi.s1ptw, is_write, syn);
            if (is_write == 1 && arm_feature(env, ARM_FEATURE_V6)) {
                fsr |= (1 << 11);
            }
            exc = EXCP_DATA_ABORT;
        }

        env->exception.vaddress = addr;
        env->exception.fsr = fsr;
        raise_exception(env, exc, syn, target_el);
    }
}

/* Raise a data fault alignment exception for the specified virtual address */
void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr, int is_write,
                                 int is_user, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int target_el;
    bool same_el;
    uint32_t syn;

    if (retaddr) {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, retaddr);
    }

    target_el = exception_target_el(env);
    same_el = (arm_current_el(env) == target_el);

    env->exception.vaddress = vaddr;

    /* the DFSR for an alignment fault depends on whether we're using
     * the LPAE long descriptor format, or the short descriptor format
     */
    if (arm_s1_regime_using_lpae_format(env, cpu_mmu_index(env, false))) {
        env->exception.fsr = 0x21;
    } else {
        env->exception.fsr = 0x1;
    }

    if (is_write == 1 && arm_feature(env, ARM_FEATURE_V6)) {
        env->exception.fsr |= (1 << 11);
    }

    syn = merge_syn_data_abort(env->exception.syndrome, target_el,
                               same_el, 0, is_write, 0x21);
    raise_exception(env, EXCP_DATA_ABORT, syn, target_el);
}

#endif /* !defined(CONFIG_USER_ONLY) */

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

uint32_t HELPER(double_saturate)(CPUARMState *env, int32_t val)
{
    uint32_t res;
    if (val >= 0x40000000) {
        res = ~SIGNBIT;
        env->QF = 1;
    } else if (val <= (int32_t)0xc0000000) {
        res = SIGNBIT;
        env->QF = 1;
    } else {
        res = val << 1;
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
}

/* Function checks whether WFx (WFI/WFE) instructions are set up to be trapped.
 * The function returns the target EL (1-3) if the instruction is to be trapped;
 * otherwise it returns 0 indicating it is not trapped.
 */
static inline int check_wfx_trap(CPUARMState *env, bool is_wfe)
{
    int cur_el = arm_current_el(env);
    uint64_t mask;

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
    if (cur_el < 2 && !arm_is_secure(env)) {
        mask = (is_wfe) ? HCR_TWE : HCR_TWI;
        if (env->cp15.hcr_el2 & mask) {
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

void HELPER(wfi)(CPUARMState *env)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));
    int target_el = check_wfx_trap(env, false);

    if (cpu_has_work(cs)) {
        /* Don't bother to go into our "low power state" if
         * we would just wake up immediately.
         */
        return;
    }

    if (target_el) {
        env->pc -= 4;
        raise_exception(env, EXCP_UDEF, syn_wfx(1, 0xe, 0), target_el);
    }

    cs->exception_index = EXCP_HLT;
    cs->halted = 1;
    cpu_loop_exit(cs);
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
    ARMCPU *cpu = arm_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

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
 * use exception_with_syndrome).
 */
void HELPER(exception_internal)(CPUARMState *env, uint32_t excp)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));

    assert(excp_is_internal(excp));
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

/* Raise an exception with the specified syndrome register value */
void HELPER(exception_with_syndrome)(CPUARMState *env, uint32_t excp,
                                     uint32_t syndrome, uint32_t target_el)
{
    raise_exception(env, excp, syndrome, target_el);
}

uint32_t HELPER(cpsr_read)(CPUARMState *env)
{
    return cpsr_read(env) & ~(CPSR_EXEC | CPSR_RESERVED);
}

void HELPER(cpsr_write)(CPUARMState *env, uint32_t val, uint32_t mask)
{
    cpsr_write(env, val, mask, CPSRWriteByInstr);
}

/* Write the CPSR for a 32-bit exception return */
void HELPER(cpsr_write_eret)(CPUARMState *env, uint32_t val)
{
    cpsr_write(env, val, CPSR_ERET_MASK, CPSRWriteExceptionReturn);
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
        switch (regno) {
        case 17: /* ELR_Hyp */
            if (curmode != ARM_CPU_MODE_HYP && curmode != ARM_CPU_MODE_MON) {
                goto undef;
            }
            break;
        default:
            if (curmode != ARM_CPU_MODE_MON) {
                goto undef;
            }
            break;
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
        env->banked_r14[bank_number(tgtmode)] = value;
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
        return env->banked_r14[bank_number(tgtmode)];
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

void HELPER(access_check_cp_reg)(CPUARMState *env, void *rip, uint32_t syndrome,
                                 uint32_t isread)
{
    const ARMCPRegInfo *ri = rip;
    int target_el;

    if (arm_feature(env, ARM_FEATURE_XSCALE) && ri->cp < 14
        && extract32(env->cp15.c15_cpar, ri->cp, 1) == 0) {
        raise_exception(env, EXCP_UDEF, syndrome, exception_target_el(env));
    }

    if (!ri->accessfn) {
        return;
    }

    switch (ri->accessfn(env, ri, isread)) {
    case CP_ACCESS_OK:
        return;
    case CP_ACCESS_TRAP:
        target_el = exception_target_el(env);
        break;
    case CP_ACCESS_TRAP_EL2:
        /* Requesting a trap to EL2 when we're in EL3 or S-EL0/1 is
         * a bug in the access function.
         */
        assert(!arm_is_secure(env) && arm_current_el(env) != 3);
        target_el = 2;
        break;
    case CP_ACCESS_TRAP_EL3:
        target_el = 3;
        break;
    case CP_ACCESS_TRAP_UNCATEGORIZED:
        target_el = exception_target_el(env);
        syndrome = syn_uncategorized();
        break;
    case CP_ACCESS_TRAP_UNCATEGORIZED_EL2:
        target_el = 2;
        syndrome = syn_uncategorized();
        break;
    case CP_ACCESS_TRAP_UNCATEGORIZED_EL3:
        target_el = 3;
        syndrome = syn_uncategorized();
        break;
    case CP_ACCESS_TRAP_FP_EL2:
        target_el = 2;
        /* Since we are an implementation that takes exceptions on a trapped
         * conditional insn only if the insn has passed its condition code
         * check, we take the IMPDEF choice to always report CV=1 COND=0xe
         * (which is also the required value for AArch64 traps).
         */
        syndrome = syn_fp_access_trap(1, 0xe, false);
        break;
    case CP_ACCESS_TRAP_FP_EL3:
        target_el = 3;
        syndrome = syn_fp_access_trap(1, 0xe, false);
        break;
    default:
        g_assert_not_reached();
    }

    raise_exception(env, EXCP_UDEF, syndrome, target_el);
}

void HELPER(set_cp_reg)(CPUARMState *env, void *rip, uint32_t value)
{
    const ARMCPRegInfo *ri = rip;

    ri->writefn(env, ri, value);
}

uint32_t HELPER(get_cp_reg)(CPUARMState *env, void *rip)
{
    const ARMCPRegInfo *ri = rip;

    return ri->readfn(env, ri);
}

void HELPER(set_cp_reg64)(CPUARMState *env, void *rip, uint64_t value)
{
    const ARMCPRegInfo *ri = rip;

    ri->writefn(env, ri, value);
}

uint64_t HELPER(get_cp_reg64)(CPUARMState *env, void *rip)
{
    const ARMCPRegInfo *ri = rip;

    return ri->readfn(env, ri);
}

void HELPER(msr_i_pstate)(CPUARMState *env, uint32_t op, uint32_t imm)
{
    /* MSR_i to update PSTATE. This is OK from EL0 only if UMA is set.
     * Note that SPSel is never OK from EL0; we rely on handle_msr_i()
     * to catch that case at translate time.
     */
    if (arm_current_el(env) == 0 && !(env->cp15.sctlr_el[1] & SCTLR_UMA)) {
        uint32_t syndrome = syn_aa64_sysregtrap(0, extract32(op, 0, 3),
                                                extract32(op, 3, 3), 4,
                                                imm, 0x1f, 0);
        raise_exception(env, EXCP_UDEF, syndrome, exception_target_el(env));
    }

    switch (op) {
    case 0x05: /* SPSel */
        update_spsel(env, imm);
        break;
    case 0x1e: /* DAIFSet */
        env->daif |= (imm << 6) & PSTATE_DAIF;
        break;
    case 0x1f: /* DAIFClear */
        env->daif &= ~((imm << 6) & PSTATE_DAIF);
        break;
    default:
        g_assert_not_reached();
    }
}

void HELPER(clear_pstate_ss)(CPUARMState *env)
{
    env->pstate &= ~PSTATE_SS;
}

void HELPER(pre_hvc)(CPUARMState *env)
{
    ARMCPU *cpu = arm_env_get_cpu(env);
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
    ARMCPU *cpu = arm_env_get_cpu(env);
    int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    bool smd = env->cp15.scr_el3 & SCR_SMD;
    /* On ARMv8 with EL3 AArch64, SMD applies to both S and NS state.
     * On ARMv8 with EL3 AArch32, or ARMv7 with the Virtualization
     *  extensions, SMD only applies to NS state.
     * On ARMv7 without the Virtualization extensions, the SMD bit
     * doesn't exist, but we forbid the guest to set it to 1 in scr_write(),
     * so we need not special case this here.
     */
    bool undef = arm_feature(env, ARM_FEATURE_AARCH64) ? smd : smd && !secure;

    if (arm_is_psci_call(cpu, EXCP_SMC)) {
        /* If PSCI is enabled and this looks like a valid PSCI call then
         * that overrides the architecturally mandated SMC behaviour.
         */
        return;
    }

    if (!arm_feature(env, ARM_FEATURE_EL3)) {
        /* If we have no EL3 then SMC always UNDEFs */
        undef = true;
    } else if (!secure && cur_el == 1 && (env->cp15.hcr_el2 & HCR_TSC)) {
        /* In NS EL1, HCR controlled routing to EL2 has priority over SMD. */
        raise_exception(env, EXCP_HYP_TRAP, syndrome, 2);
    }

    if (undef) {
        raise_exception(env, EXCP_UDEF, syn_uncategorized(),
                        exception_target_el(env));
    }
}

static int el_from_spsr(uint32_t spsr)
{
    /* Return the exception level that this SPSR is requesting a return to,
     * or -1 if it is invalid (an illegal return)
     */
    if (spsr & PSTATE_nRW) {
        switch (spsr & CPSR_M) {
        case ARM_CPU_MODE_USR:
            return 0;
        case ARM_CPU_MODE_HYP:
            return 2;
        case ARM_CPU_MODE_FIQ:
        case ARM_CPU_MODE_IRQ:
        case ARM_CPU_MODE_SVC:
        case ARM_CPU_MODE_ABT:
        case ARM_CPU_MODE_UND:
        case ARM_CPU_MODE_SYS:
            return 1;
        case ARM_CPU_MODE_MON:
            /* Returning to Mon from AArch64 is never possible,
             * so this is an illegal return.
             */
        default:
            return -1;
        }
    } else {
        if (extract32(spsr, 1, 1)) {
            /* Return with reserved M[1] bit set */
            return -1;
        }
        if (extract32(spsr, 0, 4) == 1) {
            /* return to EL0 with M[0] bit set */
            return -1;
        }
        return extract32(spsr, 2, 2);
    }
}

void HELPER(exception_return)(CPUARMState *env)
{
    int cur_el = arm_current_el(env);
    unsigned int spsr_idx = aarch64_banked_spsr_index(cur_el);
    uint32_t spsr = env->banked_spsr[spsr_idx];
    int new_el;
    bool return_to_aa64 = (spsr & PSTATE_nRW) == 0;

    aarch64_save_sp(env, cur_el);

    env->exclusive_addr = -1;

    /* We must squash the PSTATE.SS bit to zero unless both of the
     * following hold:
     *  1. debug exceptions are currently disabled
     *  2. singlestep will be active in the EL we return to
     * We check 1 here and 2 after we've done the pstate/cpsr write() to
     * transition to the EL we're going to.
     */
    if (arm_generate_debug_exceptions(env)) {
        spsr &= ~PSTATE_SS;
    }

    new_el = el_from_spsr(spsr);
    if (new_el == -1) {
        goto illegal_return;
    }
    if (new_el > cur_el
        || (new_el == 2 && !arm_feature(env, ARM_FEATURE_EL2))) {
        /* Disallow return to an EL which is unimplemented or higher
         * than the current one.
         */
        goto illegal_return;
    }

    if (new_el != 0 && arm_el_is_aa64(env, new_el) != return_to_aa64) {
        /* Return to an EL which is configured for a different register width */
        goto illegal_return;
    }

    if (new_el == 2 && arm_is_secure_below_el3(env)) {
        /* Return to the non-existent secure-EL2 */
        goto illegal_return;
    }

    if (new_el == 1 && (env->cp15.hcr_el2 & HCR_TGE)
        && !arm_is_secure_below_el3(env)) {
        goto illegal_return;
    }

    if (!return_to_aa64) {
        env->aarch64 = 0;
        /* We do a raw CPSR write because aarch64_sync_64_to_32()
         * will sort the register banks out for us, and we've already
         * caught all the bad-mode cases in el_from_spsr().
         */
        cpsr_write(env, spsr, ~0, CPSRWriteRaw);
        if (!arm_singlestep_active(env)) {
            env->uncached_cpsr &= ~PSTATE_SS;
        }
        aarch64_sync_64_to_32(env);

        if (spsr & CPSR_T) {
            env->regs[15] = env->elr_el[cur_el] & ~0x1;
        } else {
            env->regs[15] = env->elr_el[cur_el] & ~0x3;
        }
    } else {
        env->aarch64 = 1;
        pstate_write(env, spsr);
        if (!arm_singlestep_active(env)) {
            env->pstate &= ~PSTATE_SS;
        }
        aarch64_restore_sp(env, new_el);
        env->pc = env->elr_el[cur_el];
    }

    return;

illegal_return:
    /* Illegal return events of various kinds have architecturally
     * mandated behaviour:
     * restore NZCV and DAIF from SPSR_ELx
     * set PSTATE.IL
     * restore PC from ELR_ELx
     * no change to exception level, execution state or stack pointer
     */
    env->pstate |= PSTATE_IL;
    env->pc = env->elr_el[cur_el];
    spsr &= PSTATE_NZCV | PSTATE_DAIF;
    spsr |= pstate_read(env) & ~(PSTATE_NZCV | PSTATE_DAIF);
    pstate_write(env, spsr);
    if (!arm_singlestep_active(env)) {
        env->pstate &= ~PSTATE_SS;
    }
}

/* Return true if the linked breakpoint entry lbn passes its checks */
static bool linked_bp_matches(ARMCPU *cpu, int lbn)
{
    CPUARMState *env = &cpu->env;
    uint64_t bcr = env->cp15.dbgbcr[lbn];
    int brps = extract32(cpu->dbgdidr, 24, 4);
    int ctx_cmps = extract32(cpu->dbgdidr, 20, 4);
    int bt;
    uint32_t contextidr;

    /* Links to unimplemented or non-context aware breakpoints are
     * CONSTRAINED UNPREDICTABLE: either behave as if disabled, or
     * as if linked to an UNKNOWN context-aware breakpoint (in which
     * case DBGWCR<n>_EL1.LBN must indicate that breakpoint).
     * We choose the former.
     */
    if (lbn > brps || lbn < (brps - ctx_cmps)) {
        return false;
    }

    bcr = env->cp15.dbgbcr[lbn];

    if (extract64(bcr, 0, 1) == 0) {
        /* Linked breakpoint disabled : generate no events */
        return false;
    }

    bt = extract64(bcr, 20, 4);

    /* We match the whole register even if this is AArch32 using the
     * short descriptor format (in which case it holds both PROCID and ASID),
     * since we don't implement the optional v7 context ID masking.
     */
    contextidr = extract64(env->cp15.contextidr_el[1], 0, 32);

    switch (bt) {
    case 3: /* linked context ID match */
        if (arm_current_el(env) > 1) {
            /* Context matches never fire in EL2 or (AArch64) EL3 */
            return false;
        }
        return (contextidr == extract64(env->cp15.dbgbvr[lbn], 0, 32));
    case 5: /* linked address mismatch (reserved in AArch64) */
    case 9: /* linked VMID match (reserved if no EL2) */
    case 11: /* linked context ID and VMID match (reserved if no EL2) */
    default:
        /* Links to Unlinked context breakpoints must generate no
         * events; we choose to do the same for reserved values too.
         */
        return false;
    }

    return false;
}

static bool bp_wp_matches(ARMCPU *cpu, int n, bool is_wp)
{
    CPUARMState *env = &cpu->env;
    uint64_t cr;
    int pac, hmc, ssc, wt, lbn;
    /* Note that for watchpoints the check is against the CPU security
     * state, not the S/NS attribute on the offending data access.
     */
    bool is_secure = arm_is_secure(env);
    int access_el = arm_current_el(env);

    if (is_wp) {
        CPUWatchpoint *wp = env->cpu_watchpoint[n];

        if (!wp || !(wp->flags & BP_WATCHPOINT_HIT)) {
            return false;
        }
        cr = env->cp15.dbgwcr[n];
        if (wp->hitattrs.user) {
            /* The LDRT/STRT/LDT/STT "unprivileged access" instructions should
             * match watchpoints as if they were accesses done at EL0, even if
             * the CPU is at EL1 or higher.
             */
            access_el = 0;
        }
    } else {
        uint64_t pc = is_a64(env) ? env->pc : env->regs[15];

        if (!env->cpu_breakpoint[n] || env->cpu_breakpoint[n]->pc != pc) {
            return false;
        }
        cr = env->cp15.dbgbcr[n];
    }
    /* The WATCHPOINT_HIT flag guarantees us that the watchpoint is
     * enabled and that the address and access type match; for breakpoints
     * we know the address matched; check the remaining fields, including
     * linked breakpoints. We rely on WCR and BCR having the same layout
     * for the LBN, SSC, HMC, PAC/PMC and is-linked fields.
     * Note that some combinations of {PAC, HMC, SSC} are reserved and
     * must act either like some valid combination or as if the watchpoint
     * were disabled. We choose the former, and use this together with
     * the fact that EL3 must always be Secure and EL2 must always be
     * Non-Secure to simplify the code slightly compared to the full
     * table in the ARM ARM.
     */
    pac = extract64(cr, 1, 2);
    hmc = extract64(cr, 13, 1);
    ssc = extract64(cr, 14, 2);

    switch (ssc) {
    case 0:
        break;
    case 1:
    case 3:
        if (is_secure) {
            return false;
        }
        break;
    case 2:
        if (!is_secure) {
            return false;
        }
        break;
    }

    switch (access_el) {
    case 3:
    case 2:
        if (!hmc) {
            return false;
        }
        break;
    case 1:
        if (extract32(pac, 0, 1) == 0) {
            return false;
        }
        break;
    case 0:
        if (extract32(pac, 1, 1) == 0) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }

    wt = extract64(cr, 20, 1);
    lbn = extract64(cr, 16, 4);

    if (wt && !linked_bp_matches(cpu, lbn)) {
        return false;
    }

    return true;
}

static bool check_watchpoints(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    int n;

    /* If watchpoints are disabled globally or we can't take debug
     * exceptions here then watchpoint firings are ignored.
     */
    if (extract32(env->cp15.mdscr_el1, 15, 1) == 0
        || !arm_generate_debug_exceptions(env)) {
        return false;
    }

    for (n = 0; n < ARRAY_SIZE(env->cpu_watchpoint); n++) {
        if (bp_wp_matches(cpu, n, true)) {
            return true;
        }
    }
    return false;
}

static bool check_breakpoints(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    int n;

    /* If breakpoints are disabled globally or we can't take debug
     * exceptions here then breakpoint firings are ignored.
     */
    if (extract32(env->cp15.mdscr_el1, 15, 1) == 0
        || !arm_generate_debug_exceptions(env)) {
        return false;
    }

    for (n = 0; n < ARRAY_SIZE(env->cpu_breakpoint); n++) {
        if (bp_wp_matches(cpu, n, false)) {
            return true;
        }
    }
    return false;
}

void HELPER(check_breakpoints)(CPUARMState *env)
{
    ARMCPU *cpu = arm_env_get_cpu(env);

    if (check_breakpoints(cpu)) {
        HELPER(exception_internal(env, EXCP_DEBUG));
    }
}

bool arm_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
    /* Called by core code when a CPU watchpoint fires; need to check if this
     * is also an architectural watchpoint match.
     */
    ARMCPU *cpu = ARM_CPU(cs);

    return check_watchpoints(cpu);
}

void arm_debug_excp_handler(CPUState *cs)
{
    /* Called by core code when a watchpoint or breakpoint fires;
     * need to check which one and raise the appropriate exception.
     */
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    CPUWatchpoint *wp_hit = cs->watchpoint_hit;

    if (wp_hit) {
        if (wp_hit->flags & BP_CPU) {
            bool wnr = (wp_hit->flags & BP_WATCHPOINT_HIT_WRITE) != 0;
            bool same_el = arm_debug_target_el(env) == arm_current_el(env);

            cs->watchpoint_hit = NULL;

            if (extended_addresses_enabled(env)) {
                env->exception.fsr = (1 << 9) | 0x22;
            } else {
                env->exception.fsr = 0x2;
            }
            env->exception.vaddress = wp_hit->hitaddr;
            raise_exception(env, EXCP_DATA_ABORT,
                    syn_watchpoint(same_el, 0, wnr),
                    arm_debug_target_el(env));
        }
    } else {
        uint64_t pc = is_a64(env) ? env->pc : env->regs[15];
        bool same_el = (arm_debug_target_el(env) == arm_current_el(env));

        /* (1) GDB breakpoints should be handled first.
         * (2) Do not raise a CPU exception if no CPU breakpoint has fired,
         * since singlestep is also done by generating a debug internal
         * exception.
         */
        if (cpu_breakpoint_test(cs, pc, BP_GDB)
            || !cpu_breakpoint_test(cs, pc, BP_CPU)) {
            return;
        }

        if (extended_addresses_enabled(env)) {
            env->exception.fsr = (1 << 9) | 0x22;
        } else {
            env->exception.fsr = 0x2;
        }
        /* FAR is UNKNOWN, so doesn't need setting */
        raise_exception(env, EXCP_PREFETCH_ABORT,
                        syn_breakpoint(same_el),
                        arm_debug_target_el(env));
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
