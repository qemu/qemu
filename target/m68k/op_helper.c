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
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/semihost.h"

#if defined(CONFIG_USER_ONLY)

void m68k_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

static inline void do_interrupt_m68k_hardirq(CPUM68KState *env)
{
}

#else

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = m68k_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
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
    env->aregs[7] = sp + 8;

    helper_set_sr(env, fmt);
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

    fmt |= 0x40000000;
    fmt |= vector << 16;
    fmt |= env->sr;
    fmt |= cpu_m68k_get_ccr(env);

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

static inline void do_interrupt_m68k_hardirq(CPUM68KState *env)
{
    do_interrupt_all(env, 1);
}
#endif

bool m68k_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    if (interrupt_request & CPU_INTERRUPT_HARD
        && ((env->sr & SR_I) >> SR_I_SHIFT) < env->pending_level) {
        /* Real hardware gets the interrupt vector via an IACK cycle
           at this point.  Current emulated hardware doesn't rely on
           this, so we provide/save the vector when the interrupt is
           first signalled.  */
        cs->exception_index = env->pending_vector;
        do_interrupt_m68k_hardirq(env);
        return true;
    }
    return false;
}

static void raise_exception_ra(CPUM68KState *env, int tt, uintptr_t raddr)
{
    CPUState *cs = CPU(m68k_env_get_cpu(env));

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
        /* real 68040 keeps N and unset Z on overflow,
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
        /* real 68040 keeps N and unset Z on overflow,
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
        /* real 68040 keeps N and unset Z on overflow,
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
        /* real 68040 keeps N and unset Z on overflow,
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

    if (parallel_cpus) {
        /* Tell the main loop we need to serialize this insn.  */
        cpu_loop_exit_atomic(ENV_GET_CPU(env), ra);
    } else {
        /* We're executing in a serial context -- no need to be atomic.  */
        l1 = cpu_lduw_data_ra(env, a1, ra);
        l2 = cpu_lduw_data_ra(env, a2, ra);
        if (l1 == c1 && l2 == c2) {
            cpu_stw_data_ra(env, a1, u1, ra);
            cpu_stw_data_ra(env, a2, u2, ra);
        }
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

void HELPER(cas2l)(CPUM68KState *env, uint32_t regs, uint32_t a1, uint32_t a2)
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
#if defined(CONFIG_ATOMIC64) && !defined(CONFIG_USER_ONLY)
    int mmu_idx = cpu_mmu_index(env, 0);
    TCGMemOpIdx oi;
#endif

    if (parallel_cpus) {
        /* We're executing in a parallel context -- must be atomic.  */
#ifdef CONFIG_ATOMIC64
        uint64_t c, u, l;
        if ((a1 & 7) == 0 && a2 == a1 + 4) {
            c = deposit64(c2, 32, 32, c1);
            u = deposit64(u2, 32, 32, u1);
#ifdef CONFIG_USER_ONLY
            l = helper_atomic_cmpxchgq_be(env, a1, c, u);
#else
            oi = make_memop_idx(MO_BEQ, mmu_idx);
            l = helper_atomic_cmpxchgq_be_mmu(env, a1, c, u, oi, ra);
#endif
            l1 = l >> 32;
            l2 = l;
        } else if ((a2 & 7) == 0 && a1 == a2 + 4) {
            c = deposit64(c1, 32, 32, c2);
            u = deposit64(u1, 32, 32, u2);
#ifdef CONFIG_USER_ONLY
            l = helper_atomic_cmpxchgq_be(env, a2, c, u);
#else
            oi = make_memop_idx(MO_BEQ, mmu_idx);
            l = helper_atomic_cmpxchgq_be_mmu(env, a2, c, u, oi, ra);
#endif
            l2 = l >> 32;
            l1 = l;
        } else
#endif
        {
            /* Tell the main loop we need to serialize this insn.  */
            cpu_loop_exit_atomic(ENV_GET_CPU(env), ra);
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

    /* Compute the number of bytes required (minus one) to
       satisfy the bitfield.  */
    blen = (bofs + len - 1) / 8;

    /* Canonicalize the bit offset for data loaded into a 64-bit big-endian
       word.  For the cases where BLEN is not a power of 2, adjust ADDR so
       that we can use the next power of two sized load without crossing a
       page boundary, unless the field itself crosses the boundary.  */
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

    /* Put CC_N at the top of the high word; put the zero-extended value
       at the bottom of the low word.  */
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

    /* Return FFO in the low word and N in the high word.
       Note that because of MASK and the shift, the low word
       is already zero.  */
    return n | ffo;
}
