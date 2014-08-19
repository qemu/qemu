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
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "exec/cpu_ldst.h"

#define SIGNBIT (uint32_t)0x80000000
#define SIGNBIT64 ((uint64_t)1 << 63)

static void raise_exception(CPUARMState *env, int tt)
{
    ARMCPU *cpu = arm_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = tt;
    cpu_loop_exit(cs);
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

/* try to fill the TLB and return an exception if error. If retaddr is
 * NULL, it means that the function was called in C code (i.e. not
 * from generated code or from helper.c)
 */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = arm_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        ARMCPU *cpu = ARM_CPU(cs);
        CPUARMState *env = &cpu->env;

        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        raise_exception(env, cs->exception_index);
    }
}
#endif

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

void HELPER(wfi)(CPUARMState *env)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));

    cs->exception_index = EXCP_HLT;
    cs->halted = 1;
    cpu_loop_exit(cs);
}

void HELPER(wfe)(CPUARMState *env)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));

    /* Don't actually halt the CPU, just yield back to top
     * level loop
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
                                     uint32_t syndrome)
{
    CPUState *cs = CPU(arm_env_get_cpu(env));

    assert(!excp_is_internal(excp));
    cs->exception_index = excp;
    env->exception.syndrome = syndrome;
    cpu_loop_exit(cs);
}

uint32_t HELPER(cpsr_read)(CPUARMState *env)
{
    return cpsr_read(env) & ~(CPSR_EXEC | CPSR_RESERVED);
}

void HELPER(cpsr_write)(CPUARMState *env, uint32_t val, uint32_t mask)
{
    cpsr_write(env, val, mask);
}

/* Access to user mode registers from privileged modes.  */
uint32_t HELPER(get_user_reg)(CPUARMState *env, uint32_t regno)
{
    uint32_t val;

    if (regno == 13) {
        val = env->banked_r13[0];
    } else if (regno == 14) {
        val = env->banked_r14[0];
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
        env->banked_r13[0] = val;
    } else if (regno == 14) {
        env->banked_r14[0] = val;
    } else if (regno >= 8
               && (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        env->usr_regs[regno - 8] = val;
    } else {
        env->regs[regno] = val;
    }
}

void HELPER(access_check_cp_reg)(CPUARMState *env, void *rip, uint32_t syndrome)
{
    const ARMCPRegInfo *ri = rip;
    switch (ri->accessfn(env, ri)) {
    case CP_ACCESS_OK:
        return;
    case CP_ACCESS_TRAP:
        env->exception.syndrome = syndrome;
        break;
    case CP_ACCESS_TRAP_UNCATEGORIZED:
        env->exception.syndrome = syn_uncategorized();
        break;
    default:
        g_assert_not_reached();
    }
    raise_exception(env, EXCP_UDEF);
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
    if (arm_current_pl(env) == 0 && !(env->cp15.c1_sys & SCTLR_UMA)) {
        raise_exception(env, EXCP_UDEF);
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

void HELPER(exception_return)(CPUARMState *env)
{
    int cur_el = arm_current_pl(env);
    unsigned int spsr_idx = aarch64_banked_spsr_index(cur_el);
    uint32_t spsr = env->banked_spsr[spsr_idx];
    int new_el, i;

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

    if (spsr & PSTATE_nRW) {
        /* TODO: We currently assume EL1/2/3 are running in AArch64.  */
        env->aarch64 = 0;
        new_el = 0;
        env->uncached_cpsr = 0x10;
        cpsr_write(env, spsr, ~0);
        if (!arm_singlestep_active(env)) {
            env->uncached_cpsr &= ~PSTATE_SS;
        }
        for (i = 0; i < 15; i++) {
            env->regs[i] = env->xregs[i];
        }

        env->regs[15] = env->elr_el[1] & ~0x1;
    } else {
        new_el = extract32(spsr, 2, 2);
        if (new_el > cur_el
            || (new_el == 2 && !arm_feature(env, ARM_FEATURE_EL2))) {
            /* Disallow return to an EL which is unimplemented or higher
             * than the current one.
             */
            goto illegal_return;
        }
        if (extract32(spsr, 1, 1)) {
            /* Return with reserved M[1] bit set */
            goto illegal_return;
        }
        if (new_el == 0 && (spsr & PSTATE_SP)) {
            /* Return to EL0 with M[0] bit set */
            goto illegal_return;
        }
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
