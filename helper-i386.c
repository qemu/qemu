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

static inline void get_ss_esp_from_tss(uint32_t *ss_ptr, 
                                       uint32_t *esp_ptr, int dpl)
{
    int type, index, shift;
    
#if 0
    {
        int i;
        printf("TR: base=%p limit=%x\n", env->tr.base, env->tr.limit);
        for(i=0;i<env->tr.limit;i++) {
            printf("%02x ", env->tr.base[i]);
            if ((i & 7) == 7) printf("\n");
        }
        printf("\n");
    }
#endif

    if (!(env->tr.flags & DESC_P_MASK))
        cpu_abort(env, "invalid tss");
    type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if ((type & 7) != 1)
        cpu_abort(env, "invalid tss type");
    shift = type >> 3;
    index = (dpl * 4 + 2) << shift;
    if (index + (4 << shift) - 1 > env->tr.limit)
        raise_exception_err(EXCP0A_TSS, env->tr.selector & 0xfffc);
    if (shift == 0) {
        *esp_ptr = lduw(env->tr.base + index);
        *ss_ptr = lduw(env->tr.base + index + 2);
    } else {
        *esp_ptr = ldl(env->tr.base + index);
        *ss_ptr = lduw(env->tr.base + index + 4);
    }
}

/* return non zero if error */
static inline int load_segment(uint32_t *e1_ptr, uint32_t *e2_ptr,
                               int selector)
{
    SegmentCache *dt;
    int index;
    uint8_t *ptr;

    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    if ((index + 7) > dt->limit)
        return -1;
    ptr = dt->base + index;
    *e1_ptr = ldl(ptr);
    *e2_ptr = ldl(ptr + 4);
    return 0;
}
                                     

/* protected mode interrupt */
static void do_interrupt_protected(int intno, int is_int, int error_code,
                                   unsigned int next_eip, int is_hw)
{
    SegmentCache *dt;
    uint8_t *ptr, *ssp;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss, esp, ss_e1, ss_e2, push_size;
    uint32_t old_cs, old_ss, old_esp, old_eip;

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
        cpu_abort(env, "task gate not supported");
        break;
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
    cpl = env->hflags & HF_CPL_MASK;
    /* check privledge if software int */
    if (is_int && dpl < cpl)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    /* check valid bit */
    if (!(e2 & DESC_P_MASK))
        raise_exception_err(EXCP0B_NOSEG, intno * 8 + 2);
    selector = e1 >> 16;
    offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    if ((selector & 0xfffc) == 0)
        raise_exception_err(EXCP0D_GPF, 0);

    if (load_segment(&e1, &e2, selector) != 0)
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK)))
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl)
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    if (!(e2 & DESC_P_MASK))
        raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
    if (!(e2 & DESC_C_MASK) && dpl < cpl) {
        /* to inner priviledge */
        get_ss_esp_from_tss(&ss, &esp, dpl);
        if ((ss & 0xfffc) == 0)
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        if ((ss & 3) != dpl)
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        if (load_segment(&ss_e1, &ss_e2, ss) != 0)
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
        if (ss_dpl != dpl)
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        if (!(ss_e2 & DESC_S_MASK) ||
            (ss_e2 & DESC_CS_MASK) ||
            !(ss_e2 & DESC_W_MASK))
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        if (!(ss_e2 & DESC_P_MASK))
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        new_stack = 1;
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same priviledge */
        new_stack = 0;
    } else {
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
    }

    shift = type >> 3;
    has_error_code = 0;
    if (!is_int && !is_hw) {
        switch(intno) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
            has_error_code = 1;
            break;
        }
    }
    push_size = 6 + (new_stack << 2) + (has_error_code << 1);
    if (env->eflags & VM_MASK)
        push_size += 8;
    push_size <<= shift;

    /* XXX: check that enough room is available */
    if (new_stack) {
        old_esp = ESP;
        old_ss = env->segs[R_SS].selector;
        load_seg(R_SS, ss, env->eip);
    } else {
        old_esp = 0;
        old_ss = 0;
        esp = ESP;
    }
    if (is_int)
        old_eip = next_eip;
    else
        old_eip = env->eip;
    old_cs = env->segs[R_CS].selector;
    load_seg(R_CS, selector, env->eip);
    env->eip = offset;
    ESP = esp - push_size;
    ssp = env->segs[R_SS].base + esp;
    if (shift == 1) {
        int old_eflags;
        if (env->eflags & VM_MASK) {
            ssp -= 4;
            stl(ssp, env->segs[R_GS].selector);
            ssp -= 4;
            stl(ssp, env->segs[R_FS].selector);
            ssp -= 4;
            stl(ssp, env->segs[R_DS].selector);
            ssp -= 4;
            stl(ssp, env->segs[R_ES].selector);
        }
        if (new_stack) {
            ssp -= 4;
            stl(ssp, old_ss);
            ssp -= 4;
            stl(ssp, old_esp);
        }
        ssp -= 4;
        old_eflags = compute_eflags();
        stl(ssp, old_eflags);
        ssp -= 4;
        stl(ssp, old_cs);
        ssp -= 4;
        stl(ssp, old_eip);
        if (has_error_code) {
            ssp -= 4;
            stl(ssp, error_code);
        }
    } else {
        if (new_stack) {
            ssp -= 2;
            stw(ssp, old_ss);
            ssp -= 2;
            stw(ssp, old_esp);
        }
        ssp -= 2;
        stw(ssp, compute_eflags());
        ssp -= 2;
        stw(ssp, old_cs);
        ssp -= 2;
        stw(ssp, old_eip);
        if (has_error_code) {
            ssp -= 2;
            stw(ssp, error_code);
        }
    }
    
    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);
}

/* real mode interrupt */
static void do_interrupt_real(int intno, int is_int, int error_code,
                                 unsigned int next_eip)
{
    SegmentCache *dt;
    uint8_t *ptr, *ssp;
    int selector;
    uint32_t offset, esp;
    uint32_t old_cs, old_eip;

    /* real mode (simpler !) */
    dt = &env->idt;
    if (intno * 4 + 3 > dt->limit)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    ptr = dt->base + intno * 4;
    offset = lduw(ptr);
    selector = lduw(ptr + 2);
    esp = ESP;
    ssp = env->segs[R_SS].base;
    if (is_int)
        old_eip = next_eip;
    else
        old_eip = env->eip;
    old_cs = env->segs[R_CS].selector;
    esp -= 2;
    stw(ssp + (esp & 0xffff), compute_eflags());
    esp -= 2;
    stw(ssp + (esp & 0xffff), old_cs);
    esp -= 2;
    stw(ssp + (esp & 0xffff), old_eip);
    
    /* update processor state */
    ESP = (ESP & ~0xffff) | (esp & 0xffff);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (uint8_t *)(selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

/* fake user mode interrupt */
void do_interrupt_user(int intno, int is_int, int error_code, 
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
    cpl = env->hflags & HF_CPL_MASK;
    /* check privledge if software int */
    if (is_int && dpl < cpl)
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code */
    if (is_int)
        EIP = next_eip;
}

/*
 * Begin excution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the EIP value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.  
 */
void do_interrupt(int intno, int is_int, int error_code, 
                  unsigned int next_eip, int is_hw)
{
    if (env->cr[0] & CR0_PE_MASK) {
        do_interrupt_protected(intno, is_int, error_code, next_eip, is_hw);
    } else {
        do_interrupt_real(intno, is_int, error_code, next_eip);
    }
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * EIP value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.  
 */
void raise_interrupt(int intno, int is_int, int error_code, 
                     unsigned int next_eip)
{
    env->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = next_eip;
    cpu_loop_exit();
}

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
        int family, model, stepping;
        /* EAX = 1 info */
#if 0
        /* pentium 75-200 */
        family = 5;
        model = 2;
        stepping = 11;
#else
        /* pentium pro */
        family = 6;
        model = 1;
        stepping = 3;
#endif
        EAX = (family << 8) | (model << 4) | stepping;
        EBX = 0;
        ECX = 0;
        EDX = CPUID_FP87 | CPUID_DE | CPUID_PSE |
            CPUID_TSC | CPUID_MSR | CPUID_MCE |
            CPUID_CX8 | CPUID_PGE | CPUID_CMOV;
    }
}

static inline void load_seg_cache(SegmentCache *sc, uint32_t e1, uint32_t e2)
{
    sc->base = (void *)((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    sc->limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        sc->limit = (sc->limit << 12) | 0xfff;
    sc->flags = e2;
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
        /* NULL selector case: invalid LDT */
        env->tr.base = NULL;
        env->tr.limit = 0;
        env->tr.flags = 0;
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
    uint32_t e1, e2;
    
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
            sc->flags = 0;
        }
    } else {
        if (load_segment(&e1, &e2, selector) != 0) {
            EIP = cur_eip;
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
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
        fprintf(logfile, "load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx flags=%08x\n", 
                selector, (unsigned long)sc->base, sc->limit, sc->flags);
#endif
    }
    if (seg_reg == R_CS) {
        cpu_x86_set_cpl(env, selector & 3);
    }
    sc->selector = selector;
}

/* protected mode jump */
void helper_ljmp_protected_T0_T1(void)
{
    int new_cs, new_eip;
    SegmentCache sc1;
    uint32_t e1, e2, cpl, dpl, rpl;

    new_cs = T0;
    new_eip = T1;
    if ((new_cs & 0xfffc) == 0)
        raise_exception_err(EXCP0D_GPF, 0);
    if (load_segment(&e1, &e2, new_cs) != 0)
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK))
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_CS_MASK) {
            /* conforming code segment */
            if (dpl > cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            if (dpl != cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
        load_seg_cache(&sc1, e1, e2);
        if (new_eip > sc1.limit)
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        env->segs[R_CS].base = sc1.base;
        env->segs[R_CS].limit = sc1.limit;
        env->segs[R_CS].flags = sc1.flags;
        env->segs[R_CS].selector = (new_cs & 0xfffc) | cpl;
        EIP = new_eip;
    } else {
        cpu_abort(env, "jmp to call/task gate not supported 0x%04x:0x%08x", 
                  new_cs, new_eip);
    }
}

/* real mode call */
void helper_lcall_real_T0_T1(int shift, int next_eip)
{
    int new_cs, new_eip;
    uint32_t esp, esp_mask;
    uint8_t *ssp;

    new_cs = T0;
    new_eip = T1;
    esp = ESP;
    esp_mask = 0xffffffff;
    if (!(env->segs[R_SS].flags & DESC_B_MASK))
        esp_mask = 0xffff;
    ssp = env->segs[R_SS].base;
    if (shift) {
        esp -= 4;
        stl(ssp + (esp & esp_mask), env->segs[R_CS].selector);
        esp -= 4;
        stl(ssp + (esp & esp_mask), next_eip);
    } else {
        esp -= 2;
        stw(ssp + (esp & esp_mask), env->segs[R_CS].selector);
        esp -= 2;
        stw(ssp + (esp & esp_mask), next_eip);
    }

    if (!(env->segs[R_SS].flags & DESC_B_MASK))
        ESP = (ESP & ~0xffff) | (esp & 0xffff);
    else
        ESP = esp;
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (uint8_t *)(new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected_T0_T1(int shift, int next_eip)
{
    int new_cs, new_eip;
    SegmentCache sc1;
    uint32_t e1, e2, cpl, dpl, rpl, selector, offset, param_count;
    uint32_t ss, ss_e1, ss_e2, push_size, sp, type, ss_dpl;
    uint32_t old_ss, old_esp, val, i;
    uint8_t *ssp, *old_ssp;
    
    new_cs = T0;
    new_eip = T1;
    if ((new_cs & 0xfffc) == 0)
        raise_exception_err(EXCP0D_GPF, 0);
    if (load_segment(&e1, &e2, new_cs) != 0)
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK))
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_CS_MASK) {
            /* conforming code segment */
            if (dpl > cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            if (dpl != cpl)
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);

        sp = ESP;
        if (!(env->segs[R_SS].flags & DESC_B_MASK))
            sp &= 0xffff;
        ssp = env->segs[R_SS].base + sp;
        if (shift) {
            ssp -= 4;
            stl(ssp, env->segs[R_CS].selector);
            ssp -= 4;
            stl(ssp, next_eip);
        } else {
            ssp -= 2;
            stw(ssp, env->segs[R_CS].selector);
            ssp -= 2;
            stw(ssp, next_eip);
        }
        sp -= (4 << shift);
        
        load_seg_cache(&sc1, e1, e2);
        if (new_eip > sc1.limit)
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        /* from this point, not restartable */
        if (!(env->segs[R_SS].flags & DESC_B_MASK))
            ESP = (ESP & 0xffff0000) | (sp & 0xffff);
        else
            ESP = sp;
        env->segs[R_CS].base = sc1.base;
        env->segs[R_CS].limit = sc1.limit;
        env->segs[R_CS].flags = sc1.flags;
        env->segs[R_CS].selector = (new_cs & 0xfffc) | cpl;
        EIP = new_eip;
    } else {
        /* check gate type */
        type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
        switch(type) {
        case 1: /* available 286 TSS */
        case 9: /* available 386 TSS */
        case 5: /* task gate */
            cpu_abort(env, "task gate not supported");
            break;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            break;
        default:
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            break;
        }
        shift = type >> 3;

        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        if (dpl < cpl || dpl < rpl)
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        /* check valid bit */
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG,  new_cs & 0xfffc);
        selector = e1 >> 16;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
        if ((selector & 0xfffc) == 0)
            raise_exception_err(EXCP0D_GPF, 0);

        if (load_segment(&e1, &e2, selector) != 0)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK)))
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (dpl > cpl)
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);

        if (!(e2 & DESC_C_MASK) && dpl < cpl) {
            /* to inner priviledge */
            get_ss_esp_from_tss(&ss, &sp, dpl);
            if ((ss & 0xfffc) == 0)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if ((ss & 3) != dpl)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (load_segment(&ss_e1, &ss_e2, ss) != 0)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (ss_dpl != dpl)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK))
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (!(ss_e2 & DESC_P_MASK))
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            
            param_count = e2 & 0x1f;
            push_size = ((param_count * 2) + 8) << shift;

            old_esp = ESP;
            old_ss = env->segs[R_SS].selector;
            if (!(env->segs[R_SS].flags & DESC_B_MASK))
                old_esp &= 0xffff;
            old_ssp = env->segs[R_SS].base + old_esp;
            
            /* XXX: from this point not restartable */
            load_seg(R_SS, ss, env->eip);

            if (!(env->segs[R_SS].flags & DESC_B_MASK))
                sp &= 0xffff;
            ssp = env->segs[R_SS].base + sp;
            if (shift) {
                ssp -= 4;
                stl(ssp, old_ss);
                ssp -= 4;
                stl(ssp, old_esp);
                ssp -= 4 * param_count;
                for(i = 0; i < param_count; i++) {
                    val = ldl(old_ssp + i * 4);
                    stl(ssp + i * 4, val);
                }
            } else {
                ssp -= 2;
                stw(ssp, old_ss);
                ssp -= 2;
                stw(ssp, old_esp);
                ssp -= 2 * param_count;
                for(i = 0; i < param_count; i++) {
                    val = lduw(old_ssp + i * 2);
                    stw(ssp + i * 2, val);
                }
            }
        } else {
            /* to same priviledge */
            if (!(env->segs[R_SS].flags & DESC_B_MASK))
                sp &= 0xffff;
            ssp = env->segs[R_SS].base + sp;
            push_size = (4 << shift);
        }

        if (shift) {
            ssp -= 4;
            stl(ssp, env->segs[R_CS].selector);
            ssp -= 4;
            stl(ssp, next_eip);
        } else {
            ssp -= 2;
            stw(ssp, env->segs[R_CS].selector);
            ssp -= 2;
            stw(ssp, next_eip);
        }

        sp -= push_size;
        load_seg(R_CS, selector, env->eip);
        /* from this point, not restartable if same priviledge */
        if (!(env->segs[R_SS].flags & DESC_B_MASK))
            ESP = (ESP & 0xffff0000) | (sp & 0xffff);
        else
            ESP = sp;
        EIP = offset;
    }
}

/* init the segment cache in vm86 mode */
static inline void load_seg_vm(int seg, int selector)
{
    SegmentCache *sc = &env->segs[seg];
    selector &= 0xffff;
    sc->base = (uint8_t *)(selector << 4);
    sc->selector = selector;
    sc->flags = 0;
    sc->limit = 0xffff;
}

/* real mode iret */
void helper_iret_real(int shift)
{
    uint32_t sp, new_cs, new_eip, new_eflags, new_esp;
    uint8_t *ssp;
    int eflags_mask;
    
    sp = ESP & 0xffff;
    ssp = env->segs[R_SS].base + sp;
    if (shift == 1) {
        /* 32 bits */
        new_eflags = ldl(ssp + 8);
        new_cs = ldl(ssp + 4) & 0xffff;
        new_eip = ldl(ssp) & 0xffff;
    } else {
        /* 16 bits */
        new_eflags = lduw(ssp + 4);
        new_cs = lduw(ssp + 2);
        new_eip = lduw(ssp);
    }
    new_esp = sp + (6 << shift);
    ESP = (ESP & 0xffff0000) | 
        (new_esp & 0xffff);
    load_seg_vm(R_CS, new_cs);
    env->eip = new_eip;
    eflags_mask = FL_UPDATE_CPL0_MASK;
    if (shift == 0)
        eflags_mask &= 0xffff;
    load_eflags(new_eflags, eflags_mask);
}

/* protected mode iret */
static inline void helper_ret_protected(int shift, int is_iret, int addend)
{
    uint32_t sp, new_cs, new_eip, new_eflags, new_esp, new_ss;
    uint32_t new_es, new_ds, new_fs, new_gs;
    uint32_t e1, e2;
    int cpl, dpl, rpl, eflags_mask;
    uint8_t *ssp;
    
    sp = ESP;
    if (!(env->segs[R_SS].flags & DESC_B_MASK))
        sp &= 0xffff;
    ssp = env->segs[R_SS].base + sp;
    if (shift == 1) {
        /* 32 bits */
        if (is_iret)
            new_eflags = ldl(ssp + 8);
        new_cs = ldl(ssp + 4) & 0xffff;
        new_eip = ldl(ssp);
        if (is_iret && (new_eflags & VM_MASK))
            goto return_to_vm86;
    } else {
        /* 16 bits */
        if (is_iret)
            new_eflags = lduw(ssp + 4);
        new_cs = lduw(ssp + 2);
        new_eip = lduw(ssp);
    }
    if ((new_cs & 0xfffc) == 0)
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    if (load_segment(&e1, &e2, new_cs) != 0)
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    if (!(e2 & DESC_S_MASK) ||
        !(e2 & DESC_CS_MASK))
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    cpl = env->hflags & HF_CPL_MASK;
    rpl = new_cs & 3; 
    if (rpl < cpl)
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (e2 & DESC_CS_MASK) {
        if (dpl > rpl)
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    } else {
        if (dpl != rpl)
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK))
        raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
    
    if (rpl == cpl) {
        /* return to same priledge level */
        load_seg(R_CS, new_cs, env->eip);
        new_esp = sp + (4 << shift) + ((2 * is_iret) << shift) + addend;
    } else {
        /* return to different priviledge level */
        ssp += (4 << shift) + ((2 * is_iret) << shift) + addend;
        if (shift == 1) {
            /* 32 bits */
            new_esp = ldl(ssp);
            new_ss = ldl(ssp + 4) & 0xffff;
        } else {
            /* 16 bits */
            new_esp = lduw(ssp);
            new_ss = lduw(ssp + 2);
        }
        
        if ((new_ss & 3) != rpl)
            raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
        if (load_segment(&e1, &e2, new_ss) != 0)
            raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
        if (!(e2 & DESC_S_MASK) ||
            (e2 & DESC_CS_MASK) ||
            !(e2 & DESC_W_MASK))
            raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (dpl != rpl)
            raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, new_ss & 0xfffc);

        load_seg(R_CS, new_cs, env->eip);
        load_seg(R_SS, new_ss, env->eip);
    }
    if (env->segs[R_SS].flags & DESC_B_MASK)
        ESP = new_esp;
    else
        ESP = (ESP & 0xffff0000) | 
            (new_esp & 0xffff);
    env->eip = new_eip;
    if (is_iret) {
        if (cpl == 0)
            eflags_mask = FL_UPDATE_CPL0_MASK;
        else
            eflags_mask = FL_UPDATE_MASK32;
        if (shift == 0)
            eflags_mask &= 0xffff;
        load_eflags(new_eflags, eflags_mask);
    }
    return;

 return_to_vm86:
    new_esp = ldl(ssp + 12);
    new_ss = ldl(ssp + 16);
    new_es = ldl(ssp + 20);
    new_ds = ldl(ssp + 24);
    new_fs = ldl(ssp + 28);
    new_gs = ldl(ssp + 32);
    
    /* modify processor state */
    load_eflags(new_eflags, FL_UPDATE_CPL0_MASK | VM_MASK | VIF_MASK | VIP_MASK);
    load_seg_vm(R_CS, new_cs);
    cpu_x86_set_cpl(env, 3);
    load_seg_vm(R_SS, new_ss);
    load_seg_vm(R_ES, new_es);
    load_seg_vm(R_DS, new_ds);
    load_seg_vm(R_FS, new_fs);
    load_seg_vm(R_GS, new_gs);

    env->eip = new_eip;
    ESP = new_esp;
}

void helper_iret_protected(int shift)
{
    helper_ret_protected(shift, 1, 0);
}

void helper_lret_protected(int shift, int addend)
{
    helper_ret_protected(shift, 0, addend);
}

void helper_movl_crN_T0(int reg)
{
    env->cr[reg] = T0;
    switch(reg) {
    case 0:
        cpu_x86_update_cr0(env);
        break;
    case 3:
        cpu_x86_update_cr3(env);
        break;
    }
}

/* XXX: do more */
void helper_movl_drN_T0(int reg)
{
    env->dr[reg] = T0;
}

void helper_invlpg(unsigned int addr)
{
    cpu_x86_flush_tlb(env, addr);
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

void helper_wrmsr(void)
{
    switch(ECX) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = EAX & 0xffff;
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = EAX;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = EAX;
        break;
    default:
        /* XXX: exception ? */
        break; 
    }
}

void helper_rdmsr(void)
{
    switch(ECX) {
    case MSR_IA32_SYSENTER_CS:
        EAX = env->sysenter_cs;
        EDX = 0;
        break;
    case MSR_IA32_SYSENTER_ESP:
        EAX = env->sysenter_esp;
        EDX = 0;
        break;
    case MSR_IA32_SYSENTER_EIP:
        EAX = env->sysenter_eip;
        EDX = 0;
        break;
    default:
        /* XXX: exception ? */
        break; 
    }
}

void helper_lsl(void)
{
    unsigned int selector, limit;
    uint32_t e1, e2;

    CC_SRC = cc_table[CC_OP].compute_all() & ~CC_Z;
    selector = T0 & 0xffff;
    if (load_segment(&e1, &e2, selector) != 0)
        return;
    limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & (1 << 23))
        limit = (limit << 12) | 0xfff;
    T1 = limit;
    CC_SRC |= CC_Z;
}

void helper_lar(void)
{
    unsigned int selector;
    uint32_t e1, e2;

    CC_SRC = cc_table[CC_OP].compute_all() & ~CC_Z;
    selector = T0 & 0xffff;
    if (load_segment(&e1, &e2, selector) != 0)
        return;
    T1 = e2 & 0x00f0ff00;
    CC_SRC |= CC_Z;
}

/* FPU helpers */

#ifndef USE_X86LDOUBLE
void helper_fldt_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt] = helper_fldt((uint8_t *)A0);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
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
    CPU86_LDouble tmp;
    uint64_t val;
    unsigned int v;
    int i;

    val = 0;
    for(i = 8; i >= 0; i--) {
        v = ldub((uint8_t *)A0 + i);
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = val;
    if (ldub((uint8_t *)A0 + 9) & 0x80)
        tmp = -tmp;
    fpush();
    ST0 = tmp;
}

void helper_fbst_ST0_A0(void)
{
    CPU86_LDouble tmp;
    int v;
    uint8_t *mem_ref, *mem_end;
    int64_t val;

    tmp = rint(ST0);
    val = (int64_t)tmp;
    mem_ref = (uint8_t *)A0;
    mem_end = mem_ref + 9;
    if (val < 0) {
        stb(mem_end, 0x80);
        val = -val;
    } else {
        stb(mem_end, 0x00);
    }
    while (mem_ref < mem_end) {
        if (val == 0)
            break;
        v = val % 100;
        val = val / 100;
        v = ((v / 10) << 4) | (v % 10);
        stb(mem_ref++, v);
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

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error */
void tlb_fill(unsigned long addr, int is_write, void *retaddr)
{
    TranslationBlock *tb;
    int ret;
    unsigned long pc;
    ret = cpu_x86_handle_mmu_fault(env, addr, is_write);
    if (ret) {
        /* now we have a real cpu fault */
        pc = (unsigned long)retaddr;
        tb = tb_find_pc(pc);
        if (tb) {
            /* the PC is inside the translated code. It means that we have
               a virtual CPU fault */
            cpu_restore_state(tb, env, pc);
        }
        raise_exception_err(EXCP0E_PAGE, env->error_code);
    }
}
