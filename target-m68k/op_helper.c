/*
 *  M68K helper routines
 *
 *  Copyright (c) 2007 CodeSourcery
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
#include "exec/cpu_ldst.h"

#if defined(CONFIG_USER_ONLY)

void m68k_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

void do_interrupt_m68k_hardirq(CPUM68KState *env)
{
}

#else

extern int semihosting_enabled;

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = m68k_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}

static void do_rte(CPUM68KState *env)
{
    uint32_t sp;
    uint32_t fmt;

    sp = env->aregs[7];
    fmt = cpu_ldl_kernel(env, sp);
    env->pc = cpu_ldl_kernel(env, sp + 4);
    sp |= (fmt >> 28) & 3;
    env->sr = fmt & 0xffff;
    m68k_switch_sp(env);
    env->aregs[7] = sp + 8;
}

static void do_interrupt_all(CPUM68KState *env, int is_hw)
{
    CPUState *cs = CPU(m68k_env_get_cpu(env));
    uint32_t sp;
    uint32_t fmt;
    uint32_t retaddr;
    uint32_t vector;

    fmt = 0;
    retaddr = env->pc;

    if (!is_hw) {
        switch (cs->exception_index) {
        case EXCP_RTE:
            /* Return from an exception.  */
            do_rte(env);
            return;
        case EXCP_HALT_INSN:
            if (semihosting_enabled
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

    sp = env->aregs[7];

    fmt |= 0x40000000;
    fmt |= (sp & 3) << 28;
    fmt |= vector << 16;
    fmt |= env->sr;

    env->sr |= SR_S;
    if (is_hw) {
        env->sr = (env->sr & ~SR_I) | (env->pending_level << SR_I_SHIFT);
        env->sr &= ~SR_M;
    }
    m68k_switch_sp(env);

    /* ??? This could cause MMU faults.  */
    sp &= ~3;
    sp -= 4;
    cpu_stl_kernel(env, sp, retaddr);
    sp -= 4;
    cpu_stl_kernel(env, sp, fmt);
    env->aregs[7] = sp;
    /* Jump to vector.  */
    env->pc = cpu_ldl_kernel(env, env->vbr + vector);
}

void m68k_cpu_do_interrupt(CPUState *cs)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    do_interrupt_all(env, 0);
}

void do_interrupt_m68k_hardirq(CPUM68KState *env)
{
    do_interrupt_all(env, 1);
}
#endif

static void raise_exception(CPUM68KState *env, int tt)
{
    CPUState *cs = CPU(m68k_env_get_cpu(env));

    cs->exception_index = tt;
    cpu_loop_exit(cs);
}

void HELPER(raise_exception)(CPUM68KState *env, uint32_t tt)
{
    raise_exception(env, tt);
}

void HELPER(divu)(CPUM68KState *env, uint32_t word)
{
    uint32_t num;
    uint32_t den;
    uint32_t quot;
    uint32_t rem;
    uint32_t flags;

    num = env->div1;
    den = env->div2;
    /* ??? This needs to make sure the throwing location is accurate.  */
    if (den == 0) {
        raise_exception(env, EXCP_DIV0);
    }
    quot = num / den;
    rem = num % den;
    flags = 0;
    if (word && quot > 0xffff)
        flags |= CCF_V;
    if (quot == 0)
        flags |= CCF_Z;
    else if ((int32_t)quot < 0)
        flags |= CCF_N;
    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
}

void HELPER(divs)(CPUM68KState *env, uint32_t word)
{
    int32_t num;
    int32_t den;
    int32_t quot;
    int32_t rem;
    int32_t flags;

    num = env->div1;
    den = env->div2;
    if (den == 0) {
        raise_exception(env, EXCP_DIV0);
    }
    quot = num / den;
    rem = num % den;
    flags = 0;
    if (word && quot != (int16_t)quot)
        flags |= CCF_V;
    if (quot == 0)
        flags |= CCF_Z;
    else if (quot < 0)
        flags |= CCF_N;
    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
}
