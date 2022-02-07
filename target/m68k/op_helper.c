/*
 *  M68K helper routines
 *
 *  Copyright (c) 2007 CodeSourcery
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "semihosting/semihost.h"

#if !defined(CONFIG_USER_ONLY)

static void cf_rte(CPUM68KState *env)
{
    uint32_t sp;
    uint32_t fmt;

    sp = env->aregs[7];
    fmt = cpu_ldl_mmuidx_ra(env, sp, MMU_KERNEL_IDX, 0);
    env->pc = cpu_ldl_mmuidx_ra(env, sp + 4, MMU_KERNEL_IDX, 0);
    sp |= (fmt >> 28) & 3;
    env->aregs[7] = sp + 8;

    cpu_m68k_set_sr(env, fmt);
}

static void m68k_rte(CPUM68KState *env)
{
    uint32_t sp;
    uint16_t fmt;
    uint16_t sr;

    sp = env->aregs[7];
throwaway:
    sr = cpu_lduw_mmuidx_ra(env, sp, MMU_KERNEL_IDX, 0);
    sp += 2;
    env->pc = cpu_ldl_mmuidx_ra(env, sp, MMU_KERNEL_IDX, 0);
    sp += 4;
    if (m68k_feature(env, M68K_FEATURE_QUAD_MULDIV)) {
        /*  all except 68000 */
        fmt = cpu_lduw_mmuidx_ra(env, sp, MMU_KERNEL_IDX, 0);
        sp += 2;
        switch (fmt >> 12) {
        case 0:
            break;
        case 1:
            env->aregs[7] = sp;
            cpu_m68k_set_sr(env, sr);
            goto throwaway;
        case 2:
        case 3:
            sp += 4;
            break;
        case 4:
            sp += 8;
            break;
        case 7:
            sp += 52;
            break;
        }
    }
    env->aregs[7] = sp;
    cpu_m68k_set_sr(env, sr);
}

static const char *m68k_exception_name(int index)
{
    switch (index) {
    case EXCP_ACCESS:
        return "Access Fault";
    case EXCP_ADDRESS:
        return "Address Error";
    case EXCP_ILLEGAL:
        return "Illegal Instruction";
    case EXCP_DIV0:
        return "Divide by Zero";
    case EXCP_CHK:
        return "CHK/CHK2";
    case EXCP_TRAPCC:
        return "FTRAPcc, TRAPcc, TRAPV";
    case EXCP_PRIVILEGE:
        return "Privilege Violation";
    case EXCP_TRACE:
        return "Trace";
    case EXCP_LINEA:
        return "A-Line";
    case EXCP_LINEF:
        return "F-Line";
    case EXCP_DEBEGBP: /* 68020/030 only */
        return "Copro Protocol Violation";
    case EXCP_FORMAT:
        return "Format Error";
    case EXCP_UNINITIALIZED:
        return "Uninitialized Interrupt";
    case EXCP_SPURIOUS:
        return "Spurious Interrupt";
    case EXCP_INT_LEVEL_1:
        return "Level 1 Interrupt";
    case EXCP_INT_LEVEL_1 + 1:
        return "Level 2 Interrupt";
    case EXCP_INT_LEVEL_1 + 2:
        return "Level 3 Interrupt";
    case EXCP_INT_LEVEL_1 + 3:
        return "Level 4 Interrupt";
    case EXCP_INT_LEVEL_1 + 4:
        return "Level 5 Interrupt";
    case EXCP_INT_LEVEL_1 + 5:
        return "Level 6 Interrupt";
    case EXCP_INT_LEVEL_1 + 6:
        return "Level 7 Interrupt";
    case EXCP_TRAP0:
        return "TRAP #0";
    case EXCP_TRAP0 + 1:
        return "TRAP #1";
    case EXCP_TRAP0 + 2:
        return "TRAP #2";
    case EXCP_TRAP0 + 3:
        return "TRAP #3";
    case EXCP_TRAP0 + 4:
        return "TRAP #4";
    case EXCP_TRAP0 + 5:
        return "TRAP #5";
    case EXCP_TRAP0 + 6:
        return "TRAP #6";
    case EXCP_TRAP0 + 7:
        return "TRAP #7";
    case EXCP_TRAP0 + 8:
        return "TRAP #8";
    case EXCP_TRAP0 + 9:
        return "TRAP #9";
    case EXCP_TRAP0 + 10:
        return "TRAP #10";
    case EXCP_TRAP0 + 11:
        return "TRAP #11";
    case EXCP_TRAP0 + 12:
        return "TRAP #12";
    case EXCP_TRAP0 + 13:
        return "TRAP #13";
    case EXCP_TRAP0 + 14:
        return "TRAP #14";
    case EXCP_TRAP0 + 15:
        return "TRAP #15";
    case EXCP_FP_BSUN:
        return "FP Branch/Set on unordered condition";
    case EXCP_FP_INEX:
        return "FP Inexact Result";
    case EXCP_FP_DZ:
        return "FP Divide by Zero";
    case EXCP_FP_UNFL:
        return "FP Underflow";
    case EXCP_FP_OPERR:
        return "FP Operand Error";
    case EXCP_FP_OVFL:
        return "FP Overflow";
    case EXCP_FP_SNAN:
        return "FP Signaling NAN";
    case EXCP_FP_UNIMP:
        return "FP Unimplemented Data Type";
    case EXCP_MMU_CONF: /* 68030/68851 only */
        return "MMU Configuration Error";
    case EXCP_MMU_ILLEGAL: /* 68851 only */
        return "MMU Illegal Operation";
    case EXCP_MMU_ACCESS: /* 68851 only */
        return "MMU Access Level Violation";
    case 64 ... 255:
        return "User Defined Vector";
    }
    return "Unassigned";
}

static void cf_interrupt_all(CPUM68KState *env, int is_hw)
{
    CPUState *cs = env_cpu(env);
    uint32_t sp;
    uint32_t sr;
    uint32_t fmt;
    uint32_t retaddr;
    uint32_t vector;

    fmt = 0;
    retaddr = env->pc;

    if (!is_hw) {
        switch (cs->exception_index) {
        case EXCP_RTE:
            /* Return from an exception.  */
            cf_rte(env);
            return;
        case EXCP_HALT_INSN:
            if (semihosting_enabled()
                    && (env->sr & SR_S) != 0
                    && (env->pc & 3) == 0
                    && cpu_lduw_code(env, env->pc - 4) == 0x4e71
                    && cpu_ldl_code(env, env->pc) == 0x4e7bf000) {
                env->pc += 4;
                do_m68k_semihosting(env, env->dregs[0]);
                return;
            }
            cs->halted = 1;
            cs->exception_index = EXCP_HLT;
            cpu_loop_exit(cs);
            return;
        }
        if (cs->exception_index >= EXCP_TRAP0
            && cs->exception_index <= EXCP_TRAP15) {
            /* Move the PC after the trap instruction.  */
            retaddr += 2;
        }
    }

    vector = cs->exception_index << 2;

    sr = env->sr | cpu_m68k_get_ccr(env);
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        qemu_log("INT %6d: %s(%#x) pc=%08x sp=%08x sr=%04x\n",
                 ++count, m68k_exception_name(cs->exception_index),
                 vector, env->pc, env->aregs[7], sr);
    }

    fmt |= 0x40000000;
    fmt |= vector << 16;
    fmt |= sr;

    env->sr |= SR_S;
    if (is_hw) {
        env->sr = (env->sr & ~SR_I) | (env->pending_level << SR_I_SHIFT);
        env->sr &= ~SR_M;
    }
    m68k_switch_sp(env);
    sp = env->aregs[7];
    fmt |= (sp & 3) << 28;

    /* ??? This could cause MMU faults.  */
    sp &= ~3;
    sp -= 4;
    cpu_stl_mmuidx_ra(env, sp, retaddr, MMU_KERNEL_IDX, 0);
    sp -= 4;
    cpu_stl_mmuidx_ra(env, sp, fmt, MMU_KERNEL_IDX, 0);
    env->aregs[7] = sp;
    /* Jump to vector.  */
    env->pc = cpu_ldl_mmuidx_ra(env, env->vbr + vector, MMU_KERNEL_IDX, 0);
}

static inline void do_stack_frame(CPUM68KState *env, uint32_t *sp,
                                  uint16_t format, uint16_t sr,
                                  uint32_t addr, uint32_t retaddr)
{
    if (m68k_feature(env, M68K_FEATURE_QUAD_MULDIV)) {
        /*  all except 68000 */
        CPUState *cs = env_cpu(env);
        switch (format) {
        case 4:
            *sp -= 4;
            cpu_stl_mmuidx_ra(env, *sp, env->pc, MMU_KERNEL_IDX, 0);
            *sp -= 4;
            cpu_stl_mmuidx_ra(env, *sp, addr, MMU_KERNEL_IDX, 0);
            break;
        case 3:
        case 2:
            *sp -= 4;
            cpu_stl_mmuidx_ra(env, *sp, addr, MMU_KERNEL_IDX, 0);
            break;
        }
        *sp -= 2;
        cpu_stw_mmuidx_ra(env, *sp, (format << 12) + (cs->exception_index << 2),
                          MMU_KERNEL_IDX, 0);
    }
    *sp -= 4;
    cpu_stl_mmuidx_ra(env, *sp, retaddr, MMU_KERNEL_IDX, 0);
    *sp -= 2;
    cpu_stw_mmuidx_ra(env, *sp, sr, MMU_KERNEL_IDX, 0);
}

static void m68k_interrupt_all(CPUM68KState *env, int is_hw)
{
    CPUState *cs = env_cpu(env);
    uint32_t sp;
    uint32_t retaddr;
    uint32_t vector;
    uint16_t sr, oldsr;

    retaddr = env->pc;

    if (!is_hw) {
        switch (cs->exception_index) {
        case EXCP_RTE:
            /* Return from an exception.  */
            m68k_rte(env);
            return;
        case EXCP_TRAP0 ...  EXCP_TRAP15:
            /* Move the PC after the trap instruction.  */
            retaddr += 2;
            break;
        }
    }

    vector = cs->exception_index << 2;

    sr = env->sr | cpu_m68k_get_ccr(env);
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        qemu_log("INT %6d: %s(%#x) pc=%08x sp=%08x sr=%04x\n",
                 ++count, m68k_exception_name(cs->exception_index),
                 vector, env->pc, env->aregs[7], sr);
    }

    /*
     * MC68040UM/AD,  chapter 9.3.10
     */

    /* "the processor first make an internal copy" */
    oldsr = sr;
    /* "set the mode to supervisor" */
    sr |= SR_S;
    /* "suppress tracing" */
    sr &= ~SR_T;
    /* "sets the processor interrupt mask" */
    if (is_hw) {
        sr |= (env->sr & ~SR_I) | (env->pending_level << SR_I_SHIFT);
    }
    cpu_m68k_set_sr(env, sr);
    sp = env->aregs[7];

    if (!m68k_feature(env, M68K_FEATURE_UNALIGNED_DATA)) {
        sp &= ~1;
    }

    if (cs->exception_index == EXCP_ACCESS) {
        if (env->mmu.fault) {
            cpu_abort(cs, "DOUBLE MMU FAULT\n");
        }
        env->mmu.fault = true;
        /* push data 3 */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* push data 2 */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* push data 1 */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 1 / push data 0 */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 1 address */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 2 data */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 2 address */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 3 data */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 3 address */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, env->mmu.ar, MMU_KERNEL_IDX, 0);
        /* fault address */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, env->mmu.ar, MMU_KERNEL_IDX, 0);
        /* write back 1 status */
        sp -= 2;
        cpu_stw_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 2 status */
        sp -= 2;
        cpu_stw_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* write back 3 status */
        sp -= 2;
        cpu_stw_mmuidx_ra(env, sp, 0, MMU_KERNEL_IDX, 0);
        /* special status word */
        sp -= 2;
        cpu_stw_mmuidx_ra(env, sp, env->mmu.ssw, MMU_KERNEL_IDX, 0);
        /* effective address */
        sp -= 4;
        cpu_stl_mmuidx_ra(env, sp, env->mmu.ar, MMU_KERNEL_IDX, 0);

        do_stack_frame(env, &sp, 7, oldsr, 0, retaddr);
        env->mmu.fault = false;
        if (qemu_loglevel_mask(CPU_LOG_INT)) {
            qemu_log("            "
                     "ssw:  %08x ea:   %08x sfc:  %d    dfc: %d\n",
                     env->mmu.ssw, env->mmu.ar, env->sfc, env->dfc);
        }
    } else if (cs->exception_index == EXCP_ADDRESS) {
        do_stack_frame(env, &sp, 2, oldsr, 0, retaddr);
    } else if (cs->exception_index == EXCP_ILLEGAL ||
               cs->exception_index == EXCP_DIV0 ||
               cs->exception_index == EXCP_CHK ||
               cs->exception_index == EXCP_TRAPCC ||
               cs->exception_index == EXCP_TRACE) {
        /* FIXME: addr is not only env->pc */
        do_stack_frame(env, &sp, 2, oldsr, env->pc, retaddr);
    } else if (is_hw && oldsr & SR_M &&
               cs->exception_index >= EXCP_SPURIOUS &&
               cs->exception_index <= EXCP_INT_LEVEL_7) {
        do_stack_frame(env, &sp, 0, oldsr, 0, retaddr);
        oldsr = sr;
        env->aregs[7] = sp;
        cpu_m68k_set_sr(env, sr &= ~SR_M);
        sp = env->aregs[7];
        if (!m68k_feature(env, M68K_FEATURE_UNALIGNED_DATA)) {
            sp &= ~1;
        }
        do_stack_frame(env, &sp, 1, oldsr, 0, retaddr);
    } else {
        do_stack_frame(env, &sp, 0, oldsr, 0, retaddr);
    }

    env->aregs[7] = sp;
    /* Jump to vector.  */
    env->pc = cpu_ldl_mmuidx_ra(env, env->vbr + vector, MMU_KERNEL_IDX, 0);
}

static void do_interrupt_all(CPUM68KState *env, int is_hw)
{
    if (m68k_feature(env, M68K_FEATURE_M68000)) {
        m68k_interrupt_all(env, is_hw);
        return;
    }
    cf_interrupt_all(env, is_hw);
}

void m68k_cpu_do_interrupt(CPUState *cs)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    do_interrupt_all(env, 0);
}

static inline void do_interrupt_m68k_hardirq(CPUM68KState *env)
{
    do_interrupt_all(env, 1);
}

void m68k_cpu_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                                 unsigned size, MMUAccessType access_type,
                                 int mmu_idx, MemTxAttrs attrs,
                                 MemTxResult response, uintptr_t retaddr)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    cpu_restore_state(cs, retaddr, true);

    if (m68k_feature(env, M68K_FEATURE_M68040)) {
        env->mmu.mmusr = 0;

        /*
         * According to the MC68040 users manual the ATC bit of the SSW is
         * used to distinguish between ATC faults and physical bus errors.
         * In the case of a bus error e.g. during nubus read from an empty
         * slot this bit should not be set
         */
        if (response != MEMTX_DECODE_ERROR) {
            env->mmu.ssw |= M68K_ATC_040;
        }

        /* FIXME: manage MMU table access error */
        env->mmu.ssw &= ~M68K_TM_040;
        if (env->sr & SR_S) { /* SUPERVISOR */
            env->mmu.ssw |= M68K_TM_040_SUPER;
        }
        if (access_type == MMU_INST_FETCH) { /* instruction or data */
            env->mmu.ssw |= M68K_TM_040_CODE;
        } else {
            env->mmu.ssw |= M68K_TM_040_DATA;
        }
        env->mmu.ssw &= ~M68K_BA_SIZE_MASK;
        switch (size) {
        case 1:
            env->mmu.ssw |= M68K_BA_SIZE_BYTE;
            break;
        case 2:
            env->mmu.ssw |= M68K_BA_SIZE_WORD;
            break;
        case 4:
            env->mmu.ssw |= M68K_BA_SIZE_LONG;
            break;
        }

        if (access_type != MMU_DATA_STORE) {
            env->mmu.ssw |= M68K_RW_040;
        }

        env->mmu.ar = addr;

        cs->exception_index = EXCP_ACCESS;
        cpu_loop_exit(cs);
    }
}

bool m68k_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    if (interrupt_request & CPU_INTERRUPT_HARD
        && ((env->sr & SR_I) >> SR_I_SHIFT) < env->pending_level) {
        /*
         * Real hardware gets the interrupt vector via an IACK cycle
         * at this point.  Current emulated hardware doesn't rely on
         * this, so we provide/save the vector when the interrupt is
         * first signalled.
         */
        cs->exception_index = env->pending_vector;
        do_interrupt_m68k_hardirq(env);
        return true;
    }
    return false;
}

#endif /* !CONFIG_USER_ONLY */

static void raise_exception_ra(CPUM68KState *env, int tt, uintptr_t raddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = tt;
    cpu_loop_exit_restore(cs, raddr);
}

static void raise_exception(CPUM68KState *env, int tt)
{
    raise_exception_ra(env, tt, 0);
}

void HELPER(raise_exception)(CPUM68KState *env, uint32_t tt)
{
    raise_exception(env, tt);
}

void HELPER(divuw)(CPUM68KState *env, int destr, uint32_t den)
{
    uint32_t num = env->dregs[destr];
    uint32_t quot, rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0; /* always cleared, even if overflow */
    if (quot > 0xffff) {
        env->cc_v = -1;
        /*
         * real 68040 keeps N and unset Z on overflow,
         * whereas documentation says "undefined"
         */
        env->cc_z = 1;
        return;
    }
    env->dregs[destr] = deposit32(quot, 16, 16, rem);
    env->cc_z = (int16_t)quot;
    env->cc_n = (int16_t)quot;
    env->cc_v = 0;
}

void HELPER(divsw)(CPUM68KState *env, int destr, int32_t den)
{
    int32_t num = env->dregs[destr];
    uint32_t quot, rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0; /* always cleared, even if overflow */
    if (quot != (int16_t)quot) {
        env->cc_v = -1;
        /* nothing else is modified */
        /*
         * real 68040 keeps N and unset Z on overflow,
         * whereas documentation says "undefined"
         */
        env->cc_z = 1;
        return;
    }
    env->dregs[destr] = deposit32(quot, 16, 16, rem);
    env->cc_z = (int16_t)quot;
    env->cc_n = (int16_t)quot;
    env->cc_v = 0;
}

void HELPER(divul)(CPUM68KState *env, int numr, int regr, uint32_t den)
{
    uint32_t num = env->dregs[numr];
    uint32_t quot, rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0;
    env->cc_z = quot;
    env->cc_n = quot;
    env->cc_v = 0;

    if (m68k_feature(env, M68K_FEATURE_CF_ISA_A)) {
        if (numr == regr) {
            env->dregs[numr] = quot;
        } else {
            env->dregs[regr] = rem;
        }
    } else {
        env->dregs[regr] = rem;
        env->dregs[numr] = quot;
    }
}

void HELPER(divsl)(CPUM68KState *env, int numr, int regr, int32_t den)
{
    int32_t num = env->dregs[numr];
    int32_t quot, rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0;
    env->cc_z = quot;
    env->cc_n = quot;
    env->cc_v = 0;

    if (m68k_feature(env, M68K_FEATURE_CF_ISA_A)) {
        if (numr == regr) {
            env->dregs[numr] = quot;
        } else {
            env->dregs[regr] = rem;
        }
    } else {
        env->dregs[regr] = rem;
        env->dregs[numr] = quot;
    }
}

void HELPER(divull)(CPUM68KState *env, int numr, int regr, uint32_t den)
{
    uint64_t num = deposit64(env->dregs[numr], 32, 32, env->dregs[regr]);
    uint64_t quot;
    uint32_t rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0; /* always cleared, even if overflow */
    if (quot > 0xffffffffULL) {
        env->cc_v = -1;
        /*
         * real 68040 keeps N and unset Z on overflow,
         * whereas documentation says "undefined"
         */
        env->cc_z = 1;
        return;
    }
    env->cc_z = quot;
    env->cc_n = quot;
    env->cc_v = 0;

    /*
     * If Dq and Dr are the same, the quotient is returned.
     * therefore we set Dq last.
     */

    env->dregs[regr] = rem;
    env->dregs[numr] = quot;
}

void HELPER(divsll)(CPUM68KState *env, int numr, int regr, int32_t den)
{
    int64_t num = deposit64(env->dregs[numr], 32, 32, env->dregs[regr]);
    int64_t quot;
    int32_t rem;

    if (den == 0) {
        raise_exception_ra(env, EXCP_DIV0, GETPC());
    }
    quot = num / den;
    rem = num % den;

    env->cc_c = 0; /* always cleared, even if overflow */
    if (quot != (int32_t)quot) {
        env->cc_v = -1;
        /*
         * real 68040 keeps N and unset Z on overflow,
         * whereas documentation says "undefined"
         */
        env->cc_z = 1;
        return;
    }
    env->cc_z = quot;
    env->cc_n = quot;
    env->cc_v = 0;

    /*
     * If Dq and Dr are the same, the quotient is returned.
     * therefore we set Dq last.
     */

    env->dregs[regr] = rem;
    env->dregs[numr] = quot;
}

/* We're executing in a serial context -- no need to be atomic.  */
void HELPER(cas2w)(CPUM68KState *env, uint32_t regs, uint32_t a1, uint32_t a2)
{
    uint32_t Dc1 = extract32(regs, 9, 3);
    uint32_t Dc2 = extract32(regs, 6, 3);
    uint32_t Du1 = extract32(regs, 3, 3);
    uint32_t Du2 = extract32(regs, 0, 3);
    int16_t c1 = env->dregs[Dc1];
    int16_t c2 = env->dregs[Dc2];
    int16_t u1 = env->dregs[Du1];
    int16_t u2 = env->dregs[Du2];
    int16_t l1, l2;
    uintptr_t ra = GETPC();

    l1 = cpu_lduw_data_ra(env, a1, ra);
    l2 = cpu_lduw_data_ra(env, a2, ra);
    if (l1 == c1 && l2 == c2) {
        cpu_stw_data_ra(env, a1, u1, ra);
        cpu_stw_data_ra(env, a2, u2, ra);
    }

    if (c1 != l1) {
        env->cc_n = l1;
        env->cc_v = c1;
    } else {
        env->cc_n = l2;
        env->cc_v = c2;
    }
    env->cc_op = CC_OP_CMPW;
    env->dregs[Dc1] = deposit32(env->dregs[Dc1], 0, 16, l1);
    env->dregs[Dc2] = deposit32(env->dregs[Dc2], 0, 16, l2);
}

static void do_cas2l(CPUM68KState *env, uint32_t regs, uint32_t a1, uint32_t a2,
                     bool parallel)
{
    uint32_t Dc1 = extract32(regs, 9, 3);
    uint32_t Dc2 = extract32(regs, 6, 3);
    uint32_t Du1 = extract32(regs, 3, 3);
    uint32_t Du2 = extract32(regs, 0, 3);
    uint32_t c1 = env->dregs[Dc1];
    uint32_t c2 = env->dregs[Dc2];
    uint32_t u1 = env->dregs[Du1];
    uint32_t u2 = env->dregs[Du2];
    uint32_t l1, l2;
    uintptr_t ra = GETPC();
#if defined(CONFIG_ATOMIC64)
    int mmu_idx = cpu_mmu_index(env, 0);
    MemOpIdx oi = make_memop_idx(MO_BEUQ, mmu_idx);
#endif

    if (parallel) {
        /* We're executing in a parallel context -- must be atomic.  */
#ifdef CONFIG_ATOMIC64
        uint64_t c, u, l;
        if ((a1 & 7) == 0 && a2 == a1 + 4) {
            c = deposit64(c2, 32, 32, c1);
            u = deposit64(u2, 32, 32, u1);
            l = cpu_atomic_cmpxchgq_be_mmu(env, a1, c, u, oi, ra);
            l1 = l >> 32;
            l2 = l;
        } else if ((a2 & 7) == 0 && a1 == a2 + 4) {
            c = deposit64(c1, 32, 32, c2);
            u = deposit64(u1, 32, 32, u2);
            l = cpu_atomic_cmpxchgq_be_mmu(env, a2, c, u, oi, ra);
            l2 = l >> 32;
            l1 = l;
        } else
#endif
        {
            /* Tell the main loop we need to serialize this insn.  */
            cpu_loop_exit_atomic(env_cpu(env), ra);
        }
    } else {
        /* We're executing in a serial context -- no need to be atomic.  */
        l1 = cpu_ldl_data_ra(env, a1, ra);
        l2 = cpu_ldl_data_ra(env, a2, ra);
        if (l1 == c1 && l2 == c2) {
            cpu_stl_data_ra(env, a1, u1, ra);
            cpu_stl_data_ra(env, a2, u2, ra);
        }
    }

    if (c1 != l1) {
        env->cc_n = l1;
        env->cc_v = c1;
    } else {
        env->cc_n = l2;
        env->cc_v = c2;
    }
    env->cc_op = CC_OP_CMPL;
    env->dregs[Dc1] = l1;
    env->dregs[Dc2] = l2;
}

void HELPER(cas2l)(CPUM68KState *env, uint32_t regs, uint32_t a1, uint32_t a2)
{
    do_cas2l(env, regs, a1, a2, false);
}

void HELPER(cas2l_parallel)(CPUM68KState *env, uint32_t regs, uint32_t a1,
                            uint32_t a2)
{
    do_cas2l(env, regs, a1, a2, true);
}

struct bf_data {
    uint32_t addr;
    uint32_t bofs;
    uint32_t blen;
    uint32_t len;
};

static struct bf_data bf_prep(uint32_t addr, int32_t ofs, uint32_t len)
{
    int bofs, blen;

    /* Bound length; map 0 to 32.  */
    len = ((len - 1) & 31) + 1;

    /* Note that ofs is signed.  */
    addr += ofs / 8;
    bofs = ofs % 8;
    if (bofs < 0) {
        bofs += 8;
        addr -= 1;
    }

    /*
     * Compute the number of bytes required (minus one) to
     * satisfy the bitfield.
     */
    blen = (bofs + len - 1) / 8;

    /*
     * Canonicalize the bit offset for data loaded into a 64-bit big-endian
     * word.  For the cases where BLEN is not a power of 2, adjust ADDR so
     * that we can use the next power of two sized load without crossing a
     * page boundary, unless the field itself crosses the boundary.
     */
    switch (blen) {
    case 0:
        bofs += 56;
        break;
    case 1:
        bofs += 48;
        break;
    case 2:
        if (addr & 1) {
            bofs += 8;
            addr -= 1;
        }
        /* fallthru */
    case 3:
        bofs += 32;
        break;
    case 4:
        if (addr & 3) {
            bofs += 8 * (addr & 3);
            addr &= -4;
        }
        break;
    default:
        g_assert_not_reached();
    }

    return (struct bf_data){
        .addr = addr,
        .bofs = bofs,
        .blen = blen,
        .len = len,
    };
}

static uint64_t bf_load(CPUM68KState *env, uint32_t addr, int blen,
                        uintptr_t ra)
{
    switch (blen) {
    case 0:
        return cpu_ldub_data_ra(env, addr, ra);
    case 1:
        return cpu_lduw_data_ra(env, addr, ra);
    case 2:
    case 3:
        return cpu_ldl_data_ra(env, addr, ra);
    case 4:
        return cpu_ldq_data_ra(env, addr, ra);
    default:
        g_assert_not_reached();
    }
}

static void bf_store(CPUM68KState *env, uint32_t addr, int blen,
                     uint64_t data, uintptr_t ra)
{
    switch (blen) {
    case 0:
        cpu_stb_data_ra(env, addr, data, ra);
        break;
    case 1:
        cpu_stw_data_ra(env, addr, data, ra);
        break;
    case 2:
    case 3:
        cpu_stl_data_ra(env, addr, data, ra);
        break;
    case 4:
        cpu_stq_data_ra(env, addr, data, ra);
        break;
    default:
        g_assert_not_reached();
    }
}

uint32_t HELPER(bfexts_mem)(CPUM68KState *env, uint32_t addr,
                            int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);

    return (int64_t)(data << d.bofs) >> (64 - d.len);
}

uint64_t HELPER(bfextu_mem)(CPUM68KState *env, uint32_t addr,
                            int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);

    /*
     * Put CC_N at the top of the high word; put the zero-extended value
     * at the bottom of the low word.
     */
    data <<= d.bofs;
    data >>= 64 - d.len;
    data |= data << (64 - d.len);

    return data;
}

uint32_t HELPER(bfins_mem)(CPUM68KState *env, uint32_t addr, uint32_t val,
                           int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);
    uint64_t mask = -1ull << (64 - d.len) >> d.bofs;

    data = (data & ~mask) | (((uint64_t)val << (64 - d.len)) >> d.bofs);

    bf_store(env, d.addr, d.blen, data, ra);

    /* The field at the top of the word is also CC_N for CC_OP_LOGIC.  */
    return val << (32 - d.len);
}

uint32_t HELPER(bfchg_mem)(CPUM68KState *env, uint32_t addr,
                           int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);
    uint64_t mask = -1ull << (64 - d.len) >> d.bofs;

    bf_store(env, d.addr, d.blen, data ^ mask, ra);

    return ((data & mask) << d.bofs) >> 32;
}

uint32_t HELPER(bfclr_mem)(CPUM68KState *env, uint32_t addr,
                           int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);
    uint64_t mask = -1ull << (64 - d.len) >> d.bofs;

    bf_store(env, d.addr, d.blen, data & ~mask, ra);

    return ((data & mask) << d.bofs) >> 32;
}

uint32_t HELPER(bfset_mem)(CPUM68KState *env, uint32_t addr,
                           int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);
    uint64_t mask = -1ull << (64 - d.len) >> d.bofs;

    bf_store(env, d.addr, d.blen, data | mask, ra);

    return ((data & mask) << d.bofs) >> 32;
}

uint32_t HELPER(bfffo_reg)(uint32_t n, uint32_t ofs, uint32_t len)
{
    return (n ? clz32(n) : len) + ofs;
}

uint64_t HELPER(bfffo_mem)(CPUM68KState *env, uint32_t addr,
                           int32_t ofs, uint32_t len)
{
    uintptr_t ra = GETPC();
    struct bf_data d = bf_prep(addr, ofs, len);
    uint64_t data = bf_load(env, d.addr, d.blen, ra);
    uint64_t mask = -1ull << (64 - d.len) >> d.bofs;
    uint64_t n = (data & mask) << d.bofs;
    uint32_t ffo = helper_bfffo_reg(n >> 32, ofs, d.len);

    /*
     * Return FFO in the low word and N in the high word.
     * Note that because of MASK and the shift, the low word
     * is already zero.
     */
    return n | ffo;
}

void HELPER(chk)(CPUM68KState *env, int32_t val, int32_t ub)
{
    /*
     * From the specs:
     *   X: Not affected, C,V,Z: Undefined,
     *   N: Set if val < 0; cleared if val > ub, undefined otherwise
     * We implement here values found from a real MC68040:
     *   X,V,Z: Not affected
     *   N: Set if val < 0; cleared if val >= 0
     *   C: if 0 <= ub: set if val < 0 or val > ub, cleared otherwise
     *      if 0 > ub: set if val > ub and val < 0, cleared otherwise
     */
    env->cc_n = val;
    env->cc_c = 0 <= ub ? val < 0 || val > ub : val > ub && val < 0;

    if (val < 0 || val > ub) {
        CPUState *cs = env_cpu(env);

        /* Recover PC and CC_OP for the beginning of the insn.  */
        cpu_restore_state(cs, GETPC(), true);

        /* flags have been modified by gen_flush_flags() */
        env->cc_op = CC_OP_FLAGS;
        /* Adjust PC to end of the insn.  */
        env->pc += 2;

        cs->exception_index = EXCP_CHK;
        cpu_loop_exit(cs);
    }
}

void HELPER(chk2)(CPUM68KState *env, int32_t val, int32_t lb, int32_t ub)
{
    /*
     * From the specs:
     *   X: Not affected, N,V: Undefined,
     *   Z: Set if val is equal to lb or ub
     *   C: Set if val < lb or val > ub, cleared otherwise
     * We implement here values found from a real MC68040:
     *   X,N,V: Not affected
     *   Z: Set if val is equal to lb or ub
     *   C: if lb <= ub: set if val < lb or val > ub, cleared otherwise
     *      if lb > ub: set if val > ub and val < lb, cleared otherwise
     */
    env->cc_z = val != lb && val != ub;
    env->cc_c = lb <= ub ? val < lb || val > ub : val > ub && val < lb;

    if (env->cc_c) {
        CPUState *cs = env_cpu(env);

        /* Recover PC and CC_OP for the beginning of the insn.  */
        cpu_restore_state(cs, GETPC(), true);

        /* flags have been modified by gen_flush_flags() */
        env->cc_op = CC_OP_FLAGS;
        /* Adjust PC to end of the insn.  */
        env->pc += 4;

        cs->exception_index = EXCP_CHK;
        cpu_loop_exit(cs);
    }
}
