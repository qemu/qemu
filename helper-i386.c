/*
 *  i386 helpers
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "exec-i386.h"

const uint8_t parity_table[256] = {
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
};

/* modulo 17 table */
const uint8_t rclw_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 9,10,11,12,13,14,15,
   16, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9,10,11,12,13,14,
};

/* modulo 9 table */
const uint8_t rclb_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 0, 1, 2, 3, 4, 5, 
    6, 7, 8, 0, 1, 2, 3, 4,
};

const CPU86_LDouble f15rk[7] =
{
    0.00000000000000000000L,
    1.00000000000000000000L,
    3.14159265358979323851L,  /*pi*/
    0.30102999566398119523L,  /*lg2*/
    0.69314718055994530943L,  /*ln2*/
    1.44269504088896340739L,  /*l2e*/
    3.32192809488736234781L,  /*l2t*/
};
    
/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

void cpu_loop_exit(void)
{
    /* NOTE: the register at this point must be saved by hand because
       longjmp restore them */
#ifdef reg_EAX
    env->regs[R_EAX] = EAX;
#endif
#ifdef reg_ECX
    env->regs[R_ECX] = ECX;
#endif
#ifdef reg_EDX
    env->regs[R_EDX] = EDX;
#endif
#ifdef reg_EBX
    env->regs[R_EBX] = EBX;
#endif
#ifdef reg_ESP
    env->regs[R_ESP] = ESP;
#endif
#ifdef reg_EBP
    env->regs[R_EBP] = EBP;
#endif
#ifdef reg_ESI
    env->regs[R_ESI] = ESI;
#endif
#ifdef reg_EDI
    env->regs[R_EDI] = EDI;
#endif
    longjmp(env->jmp_env, 1);
}

#if 0
/* full interrupt support (only useful for real CPU emulation, not
   finished) - I won't do it any time soon, finish it if you want ! */
void raise_interrupt(int intno, int is_int, int error_code, 
                     unsigned int next_eip)
{
    SegmentDescriptorTable *dt;
    uint8_t *ptr;
    int type, dpl, cpl;
    uint32_t e1, e2;
    
    dt = &env->idt;
    if (intno * 8 + 7 > dt->limit)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    ptr = dt->base + intno * 8;
    e1 = ldl(ptr);
    e2 = ldl(ptr + 4);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch(type) {
    case 5: /* task gate */
    case 6: /* 286 interrupt gate */
    case 7: /* 286 trap gate */
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->segs[R_CS].selector & 3;
    /* check privledge if software int */
    if (is_int && dpl < cpl)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    /* check valid bit */
    if (!(e2 & DESC_P_MASK))
        raise_exception_err(EXCP0B_NOSEG, intno * 8 + 2);
}

#else

/*
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * EIP value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.  
 */
void raise_interrupt(int intno, int is_int, int error_code, 
                     unsigned int next_eip)
{
    SegmentCache *dt;
    uint8_t *ptr;
    int dpl, cpl;
    uint32_t e2;

    dt = &env->idt;
    ptr = dt->base + (intno * 8);
    e2 = ldl(ptr + 4);
    
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = 3;
    /* check privledge if software int */
    if (is_int && dpl < cpl)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code */
    if (is_int)
        EIP = next_eip;
    env->exception_index = intno;
    env->error_code = error_code;

    cpu_loop_exit();
}

#endif

/* shortcuts to generate exceptions */
void raise_exception_err(int exception_index, int error_code)
{
    raise_interrupt(exception_index, 0, error_code, 0);
}

void raise_exception(int exception_index)
{
    raise_interrupt(exception_index, 0, 0, 0);
}

#ifdef BUGGY_GCC_DIV64
/* gcc 2.95.4 on PowerPC does not seem to like using __udivdi3, so we
   call it from another function */
uint32_t div64(uint32_t *q_ptr, uint64_t num, uint32_t den)
{
    *q_ptr = num / den;
    return num % den;
}

int32_t idiv64(int32_t *q_ptr, int64_t num, int32_t den)
{
    *q_ptr = num / den;
    return num % den;
}
#endif

void helper_divl_EAX_T0(uint32_t eip)
{
    unsigned int den, q, r;
    uint64_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = T0;
    if (den == 0) {
        EIP = eip;
        raise_exception(EXCP00_DIVZ);
    }
#ifdef BUGGY_GCC_DIV64
    r = div64(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    EAX = q;
    EDX = r;
}

void helper_idivl_EAX_T0(uint32_t eip)
{
    int den, q, r;
    int64_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = T0;
    if (den == 0) {
        EIP = eip;
        raise_exception(EXCP00_DIVZ);
    }
#ifdef BUGGY_GCC_DIV64
    r = idiv64(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    EAX = q;
    EDX = r;
}

void helper_cmpxchg8b(void)
{
    uint64_t d;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    d = ldq((uint8_t *)A0);
    if (d == (((uint64_t)EDX << 32) | EAX)) {
        stq((uint8_t *)A0, ((uint64_t)ECX << 32) | EBX);
        eflags |= CC_Z;
    } else {
        EDX = d >> 32;
        EAX = d;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

/* We simulate a pre-MMX pentium as in valgrind */
#define CPUID_FP87 (1 << 0)
#define CPUID_VME  (1 << 1)
#define CPUID_DE   (1 << 2)
#define CPUID_PSE  (1 << 3)
#define CPUID_TSC  (1 << 4)
#define CPUID_MSR  (1 << 5)
#define CPUID_PAE  (1 << 6)
#define CPUID_MCE  (1 << 7)
#define CPUID_CX8  (1 << 8)
#define CPUID_APIC (1 << 9)
#define CPUID_SEP  (1 << 11) /* sysenter/sysexit */
#define CPUID_MTRR (1 << 12)
#define CPUID_PGE  (1 << 13)
#define CPUID_MCA  (1 << 14)
#define CPUID_CMOV (1 << 15)
/* ... */
#define CPUID_MMX  (1 << 23)
#define CPUID_FXSR (1 << 24)
#define CPUID_SSE  (1 << 25)
#define CPUID_SSE2 (1 << 26)

void helper_cpuid(void)
{
    if (EAX == 0) {
        EAX = 1; /* max EAX index supported */
        EBX = 0x756e6547;
        ECX = 0x6c65746e;
        EDX = 0x49656e69;
    } else if (EAX == 1) {
        /* EAX = 1 info */
        EAX = 0x52b;
        EBX = 0;
        ECX = 0;
        EDX = CPUID_FP87 | CPUID_DE | CPUID_PSE |
            CPUID_TSC | CPUID_MSR | CPUID_MCE |
            CPUID_CX8;
    }
}

static inline void load_seg_cache(SegmentCache *sc, uint32_t e1, uint32_t e2)
{
    sc->base = (void *)((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    sc->limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & (1 << 23))
        sc->limit = (sc->limit << 12) | 0xfff;
    sc->seg_32bit = (e2 >> 22) & 1;
}

void helper_lldt_T0(void)
{
    int selector;
    SegmentCache *dt;
    uint32_t e1, e2;
    int index;
    uint8_t *ptr;
    
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* XXX: NULL selector case: invalid LDT */
        env->ldt.base = NULL;
        env->ldt.limit = 0;
    } else {
        if (selector & 0x4)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        dt = &env->gdt;
        index = selector & ~7;
        if ((index + 7) > dt->limit)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        ptr = dt->base + index;
        e1 = ldl(ptr);
        e2 = ldl(ptr + 4);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        load_seg_cache(&env->ldt, e1, e2);
    }
    env->ldt.selector = selector;
}

void helper_ltr_T0(void)
{
    int selector;
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, type;
    uint8_t *ptr;
    
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* XXX: NULL selector case: invalid LDT */
        env->tr.base = NULL;
        env->tr.limit = 0;
    } else {
        if (selector & 0x4)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        dt = &env->gdt;
        index = selector & ~7;
        if ((index + 7) > dt->limit)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        ptr = dt->base + index;
        e1 = ldl(ptr);
        e2 = ldl(ptr + 4);
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((e2 & DESC_S_MASK) || 
            (type != 2 && type != 9))
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        load_seg_cache(&env->tr, e1, e2);
        e2 |= 0x00000200; /* set the busy bit */
        stl(ptr + 4, e2);
    }
    env->tr.selector = selector;
}

/* only works if protected mode and not VM86 */
void load_seg(int seg_reg, int selector, unsigned int cur_eip)
{
    SegmentCache *sc;
    SegmentCache *dt;
    int index;
    uint32_t e1, e2;
    uint8_t *ptr;
    
    sc = &env->segs[seg_reg];
    if ((selector & 0xfffc) == 0) {
        /* null selector case */
        if (seg_reg == R_SS) {
            EIP = cur_eip;
            raise_exception_err(EXCP0D_GPF, 0);
        } else {
            /* XXX: each access should trigger an exception */
            sc->base = NULL;
            sc->limit = 0;
            sc->seg_32bit = 1;
        }
    } else {
        if (selector & 0x4)
            dt = &env->ldt;
        else
            dt = &env->gdt;
        index = selector & ~7;
        if ((index + 7) > dt->limit) {
            EIP = cur_eip;
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl(ptr);
        e2 = ldl(ptr + 4);
        if (!(e2 & DESC_S_MASK) ||
            (e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
            EIP = cur_eip;
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }

        if (seg_reg == R_SS) {
            if ((e2 & (DESC_CS_MASK | DESC_W_MASK)) == 0) {
                EIP = cur_eip;
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            }
        } else {
            if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
                EIP = cur_eip;
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            }
        }

        if (!(e2 & DESC_P_MASK)) {
            EIP = cur_eip;
            if (seg_reg == R_SS)
                raise_exception_err(EXCP0C_STACK, selector & 0xfffc);
            else
                raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        }
        load_seg_cache(sc, e1, e2);
#if 0
        fprintf(logfile, "load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx seg_32bit=%d\n", 
                selector, (unsigned long)sc->base, sc->limit, sc->seg_32bit);
#endif
    }
    sc->selector = selector;
}

/* protected mode jump */
void jmp_seg(int selector, unsigned int new_eip)
{
    SegmentCache sc1;
    SegmentCache *dt;
    int index;
    uint32_t e1, e2, cpl, dpl, rpl;
    uint8_t *ptr;

    if ((selector & 0xfffc) == 0) {
        raise_exception_err(EXCP0D_GPF, 0);
    }

    if (selector & 0x4)
      dt = &env->ldt;
    else
      dt = &env->gdt;
    index = selector & ~7;
    if ((index + 7) > dt->limit)
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    ptr = dt->base + index;
    e1 = ldl(ptr);
    e2 = ldl(ptr + 4);
    cpl = env->segs[R_CS].selector & 3;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK))
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_CS_MASK) {
            /* conforming code segment */
            if (dpl > cpl)
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        } else {
            /* non conforming code segment */
            rpl = selector & 3;
            if (rpl > cpl)
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            if (dpl != cpl)
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        load_seg_cache(&sc1, e1, e2);
        if (new_eip > sc1.limit)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        env->segs[R_CS] = sc1;
        env->segs[R_CS].selector = (selector & 0xfffc) | cpl;
        EIP = new_eip;
    } else {
        cpu_abort(env, "jmp to call/task gate not supported 0x%04x:0x%08x", 
                  selector, new_eip);
    }
}

/* XXX: do more */
void helper_movl_crN_T0(int reg)
{
    switch(reg) {
    case 0:
    default:
        env->cr[0] = reg;
        break;
    case 2:
        env->cr[2] = reg;
        break;
    case 3:
        env->cr[3] = reg;
        break;
    case 4:
        env->cr[4] = reg;
        break;
    }
}

/* XXX: do more */
void helper_movl_drN_T0(int reg)
{
    env->dr[reg] = T0;
}

/* rdtsc */
#ifndef __i386__
uint64_t emu_time;
#endif

void helper_rdtsc(void)
{
    uint64_t val;
#ifdef __i386__
    asm("rdtsc" : "=A" (val));
#else
    /* better than nothing: the time increases */
    val = emu_time++;
#endif
    EAX = val;
    EDX = val >> 32;
}

void helper_lsl(void)
{
    unsigned int selector, limit;
    SegmentCache *dt;
    int index;
    uint32_t e1, e2;
    uint8_t *ptr;

    CC_SRC = cc_table[CC_OP].compute_all() & ~CC_Z;
    selector = T0 & 0xffff;
    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    if ((index + 7) > dt->limit)
        return;
    ptr = dt->base + index;
    e1 = ldl(ptr);
    e2 = ldl(ptr + 4);
    limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & (1 << 23))
        limit = (limit << 12) | 0xfff;
    T1 = limit;
    CC_SRC |= CC_Z;
}

void helper_lar(void)
{
    unsigned int selector;
    SegmentCache *dt;
    int index;
    uint32_t e2;
    uint8_t *ptr;

    CC_SRC = cc_table[CC_OP].compute_all() & ~CC_Z;
    selector = T0 & 0xffff;
    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    if ((index + 7) > dt->limit)
        return;
    ptr = dt->base + index;
    e2 = ldl(ptr + 4);
    T1 = e2 & 0x00f0ff00;
    CC_SRC |= CC_Z;
}

/* FPU helpers */

#ifndef USE_X86LDOUBLE
void helper_fldt_ST0_A0(void)
{
    ST0 = helper_fldt((uint8_t *)A0);
}

void helper_fstt_ST0_A0(void)
{
    helper_fstt(ST0, (uint8_t *)A0);
}
#endif

/* BCD ops */

#define MUL10(iv) ( iv + iv + (iv << 3) )

void helper_fbld_ST0_A0(void)
{
    uint8_t *seg;
    CPU86_LDouble fpsrcop;
    int m32i;
    unsigned int v;

    /* in this code, seg/m32i will be used as temporary ptr/int */
    seg = (uint8_t *)A0 + 8;
    v = ldub(seg--);
    /* XXX: raise exception */
    if (v != 0)
        return;
    v = ldub(seg--);
    /* XXX: raise exception */
    if ((v & 0xf0) != 0)
        return;
    m32i = v;  /* <-- d14 */
    v = ldub(seg--);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d13 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d12 */
    v = ldub(seg--);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d11 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d10 */
    v = ldub(seg--);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d9 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d8 */
    fpsrcop = ((CPU86_LDouble)m32i) * 100000000.0;

    v = ldub(seg--);
    m32i = (v >> 4);  /* <-- d7 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d6 */
    v = ldub(seg--);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d5 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d4 */
    v = ldub(seg--);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d3 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d2 */
    v = ldub(seg);
    m32i = MUL10(m32i) + (v >> 4);  /* <-- val * 10 + d1 */
    m32i = MUL10(m32i) + (v & 0xf); /* <-- val * 10 + d0 */
    fpsrcop += ((CPU86_LDouble)m32i);
    if ( ldub(seg+9) & 0x80 )
        fpsrcop = -fpsrcop;
    ST0 = fpsrcop;
}

void helper_fbst_ST0_A0(void)
{
    CPU86_LDouble fptemp;
    CPU86_LDouble fpsrcop;
    int v;
    uint8_t *mem_ref, *mem_end;

    fpsrcop = rint(ST0);
    mem_ref = (uint8_t *)A0;
    mem_end = mem_ref + 8;
    if ( fpsrcop < 0.0 ) {
        stw(mem_end, 0x8000);
        fpsrcop = -fpsrcop;
    } else {
        stw(mem_end, 0x0000);
    }
    while (mem_ref < mem_end) {
        if (fpsrcop == 0.0)
            break;
        fptemp = floor(fpsrcop/10.0);
        v = ((int)(fpsrcop - fptemp*10.0));
        if  (fptemp == 0.0)  { 
            stb(mem_ref++, v); 
            break; 
        }
        fpsrcop = fptemp;
        fptemp = floor(fpsrcop/10.0);
        v |= (((int)(fpsrcop - fptemp*10.0)) << 4);
        stb(mem_ref++, v);
        fpsrcop = fptemp;
    }
    while (mem_ref < mem_end) {
        stb(mem_ref++, 0);
    }
}

void helper_f2xm1(void)
{
    ST0 = pow(2.0,ST0) - 1.0;
}

void helper_fyl2x(void)
{
    CPU86_LDouble fptemp;
    
    fptemp = ST0;
    if (fptemp>0.0){
        fptemp = log(fptemp)/log(2.0);	 /* log2(ST) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fptan(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = tan(fptemp);
        fpush();
        ST0 = 1.0;
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**52 only */
    }
}

void helper_fpatan(void)
{
    CPU86_LDouble fptemp, fpsrcop;

    fpsrcop = ST1;
    fptemp = ST0;
    ST1 = atan2(fpsrcop,fptemp);
    fpop();
}

void helper_fxtract(void)
{
    CPU86_LDoubleU temp;
    unsigned int expdif;

    temp.d = ST0;
    expdif = EXPD(temp) - EXPBIAS;
    /*DP exponent bias*/
    ST0 = expdif;
    fpush();
    BIASEXPONENT(temp);
    ST0 = temp.d;
}

void helper_fprem1(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;

    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if (expdif < 53) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by rounding to the nearest */
        fpsrcop = (fpsrcop-floor(fpsrcop) < ceil(fpsrcop)-fpsrcop)?
            floor(fpsrcop): ceil(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fprem(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;
    
    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if ( expdif < 53 ) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0)?
            -(floor(fabs(fpsrcop))): floor(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fyl2xp1(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp+1.0)>0.0) {
        fptemp = log(fptemp+1.0) / log(2.0); /* log2(ST+1.0) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fsqrt(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if (fptemp<0.0) { 
        env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = sqrt(fptemp);
}

void helper_fsincos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        fpush();
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**63 only */
    }
}

void helper_frndint(void)
{
    CPU86_LDouble a;

    a = ST0;
#ifdef __arm__
    switch(env->fpuc & RC_MASK) {
    default:
    case RC_NEAR:
        asm("rndd %0, %1" : "=f" (a) : "f"(a));
        break;
    case RC_DOWN:
        asm("rnddm %0, %1" : "=f" (a) : "f"(a));
        break;
    case RC_UP:
        asm("rnddp %0, %1" : "=f" (a) : "f"(a));
        break;
    case RC_CHOP:
        asm("rnddz %0, %1" : "=f" (a) : "f"(a));
        break;
    }
#else
    a = rint(a);
#endif
    ST0 = a;
}

void helper_fscale(void)
{
    CPU86_LDouble fpsrcop, fptemp;

    fpsrcop = 2.0;
    fptemp = pow(fpsrcop,ST1);
    ST0 *= fptemp;
}

void helper_fsin(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**53 only */
    }
}

void helper_fcos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg5 < 2**63 only */
    }
}

void helper_fxam_ST0(void)
{
    CPU86_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp))
        env->fpus |= 0x200; /* C1 <-- 1 */

    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0)
            env->fpus |=  0x500 /*Infinity*/;
        else
            env->fpus |=  0x100 /*NaN*/;
    } else if (expdif == 0) {
        if (MANTD(temp) == 0)
            env->fpus |=  0x4000 /*Zero*/;
        else
            env->fpus |= 0x4400 /*Denormal*/;
    } else {
        env->fpus |= 0x400;
    }
}

void helper_fstenv(uint8_t *ptr, int data32)
{
    int fpus, fptag, exp, i;
    uint64_t mant;
    CPU86_LDoubleU tmp;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i=7; i>=0; i--) {
	fptag <<= 2;
	if (env->fptags[i]) {
            fptag |= 3;
	} else {
            tmp.d = env->fpregs[i];
            exp = EXPD(tmp);
            mant = MANTD(tmp);
            if (exp == 0 && mant == 0) {
                /* zero */
	        fptag |= 1;
	    } else if (exp == 0 || exp == MAXEXPD
#ifdef USE_X86LDOUBLE
                       || (mant & (1LL << 63)) == 0
#endif
                       ) {
                /* NaNs, infinity, denormal */
                fptag |= 2;
            }
        }
    }
    if (data32) {
        /* 32 bit */
        stl(ptr, env->fpuc);
        stl(ptr + 4, fpus);
        stl(ptr + 8, fptag);
        stl(ptr + 12, 0);
        stl(ptr + 16, 0);
        stl(ptr + 20, 0);
        stl(ptr + 24, 0);
    } else {
        /* 16 bit */
        stw(ptr, env->fpuc);
        stw(ptr + 2, fpus);
        stw(ptr + 4, fptag);
        stw(ptr + 6, 0);
        stw(ptr + 8, 0);
        stw(ptr + 10, 0);
        stw(ptr + 12, 0);
    }
}

void helper_fldenv(uint8_t *ptr, int data32)
{
    int i, fpus, fptag;

    if (data32) {
	env->fpuc = lduw(ptr);
        fpus = lduw(ptr + 4);
        fptag = lduw(ptr + 8);
    }
    else {
	env->fpuc = lduw(ptr);
        fpus = lduw(ptr + 2);
        fptag = lduw(ptr + 4);
    }
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    for(i = 0;i < 7; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
}

void helper_fsave(uint8_t *ptr, int data32)
{
    CPU86_LDouble tmp;
    int i;

    helper_fstenv(ptr, data32);

    ptr += (14 << data32);
    for(i = 0;i < 8; i++) {
        tmp = ST(i);
#ifdef USE_X86LDOUBLE
        *(long double *)ptr = tmp;
#else
        helper_fstt(tmp, ptr);
#endif        
        ptr += 10;
    }

    /* fninit */
    env->fpus = 0;
    env->fpstt = 0;
    env->fpuc = 0x37f;
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

void helper_frstor(uint8_t *ptr, int data32)
{
    CPU86_LDouble tmp;
    int i;

    helper_fldenv(ptr, data32);
    ptr += (14 << data32);

    for(i = 0;i < 8; i++) {
#ifdef USE_X86LDOUBLE
        tmp = *(long double *)ptr;
#else
        tmp = helper_fldt(ptr);
#endif        
        ST(i) = tmp;
        ptr += 10;
    }
}

