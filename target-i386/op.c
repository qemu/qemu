/*
 *  i386 micro operations
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
#include "exec.h"

/* n must be a constant to be efficient */
static inline int lshift(int x, int n)
{
    if (n >= 0)
        return x << n;
    else
        return x >> (-n);
}

/* we define the various pieces of code used by the JIT */

#define REG EAX
#define REGNAME _EAX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ECX
#define REGNAME _ECX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDX
#define REGNAME _EDX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBX
#define REGNAME _EBX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESP
#define REGNAME _ESP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBP
#define REGNAME _EBP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESI
#define REGNAME _ESI
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDI
#define REGNAME _EDI
#include "opreg_template.h"
#undef REG
#undef REGNAME

/* operations with flags */

/* update flags with T0 and T1 (add/sub case) */
void OPPROTO op_update2_cc(void)
{
    CC_SRC = T1;
    CC_DST = T0;
}

/* update flags with T0 (logic operation case) */
void OPPROTO op_update1_cc(void)
{
    CC_DST = T0;
}

void OPPROTO op_update_neg_cc(void)
{
    CC_SRC = -T0;
    CC_DST = T0;
}

void OPPROTO op_cmpl_T0_T1_cc(void)
{
    CC_SRC = T1;
    CC_DST = T0 - T1;
}

void OPPROTO op_update_inc_cc(void)
{
    CC_SRC = cc_table[CC_OP].compute_c();
    CC_DST = T0;
}

void OPPROTO op_testl_T0_T1_cc(void)
{
    CC_DST = T0 & T1;
}

/* operations without flags */

void OPPROTO op_addl_T0_T1(void)
{
    T0 += T1;
}

void OPPROTO op_orl_T0_T1(void)
{
    T0 |= T1;
}

void OPPROTO op_andl_T0_T1(void)
{
    T0 &= T1;
}

void OPPROTO op_subl_T0_T1(void)
{
    T0 -= T1;
}

void OPPROTO op_xorl_T0_T1(void)
{
    T0 ^= T1;
}

void OPPROTO op_negl_T0(void)
{
    T0 = -T0;
}

void OPPROTO op_incl_T0(void)
{
    T0++;
}

void OPPROTO op_decl_T0(void)
{
    T0--;
}

void OPPROTO op_notl_T0(void)
{
    T0 = ~T0;
}

void OPPROTO op_bswapl_T0(void)
{
    T0 = bswap32(T0);
}

/* multiply/divide */
void OPPROTO op_mulb_AL_T0(void)
{
    unsigned int res;
    res = (uint8_t)EAX * (uint8_t)T0;
    EAX = (EAX & 0xffff0000) | res;
    CC_SRC = (res & 0xff00);
}

void OPPROTO op_imulb_AL_T0(void)
{
    int res;
    res = (int8_t)EAX * (int8_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    CC_SRC = (res != (int8_t)res);
}

void OPPROTO op_mulw_AX_T0(void)
{
    unsigned int res;
    res = (uint16_t)EAX * (uint16_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    EDX = (EDX & 0xffff0000) | ((res >> 16) & 0xffff);
    CC_SRC = res >> 16;
}

void OPPROTO op_imulw_AX_T0(void)
{
    int res;
    res = (int16_t)EAX * (int16_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    EDX = (EDX & 0xffff0000) | ((res >> 16) & 0xffff);
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_mull_EAX_T0(void)
{
    uint64_t res;
    res = (uint64_t)((uint32_t)EAX) * (uint64_t)((uint32_t)T0);
    EAX = res;
    EDX = res >> 32;
    CC_SRC = res >> 32;
}

void OPPROTO op_imull_EAX_T0(void)
{
    int64_t res;
    res = (int64_t)((int32_t)EAX) * (int64_t)((int32_t)T0);
    EAX = res;
    EDX = res >> 32;
    CC_SRC = (res != (int32_t)res);
}

void OPPROTO op_imulw_T0_T1(void)
{
    int res;
    res = (int16_t)T0 * (int16_t)T1;
    T0 = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_imull_T0_T1(void)
{
    int64_t res;
    res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
    T0 = res;
    CC_SRC = (res != (int32_t)res);
}

/* division, flags are undefined */
/* XXX: add exceptions for overflow */

void OPPROTO op_divb_AL_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff);
    den = (T0 & 0xff);
    if (den == 0) {
        EIP = PARAM1;
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_idivb_AL_T0(void)
{
    int num, den, q, r;

    num = (int16_t)EAX;
    den = (int8_t)T0;
    if (den == 0) {
        EIP = PARAM1;
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_divw_AX_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (T0 & 0xffff);
    if (den == 0) {
        EIP = PARAM1;
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den) & 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & 0xffff0000) | q;
    EDX = (EDX & 0xffff0000) | r;
}

void OPPROTO op_idivw_AX_T0(void)
{
    int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (int16_t)T0;
    if (den == 0) {
        EIP = PARAM1;
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den) & 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & 0xffff0000) | q;
    EDX = (EDX & 0xffff0000) | r;
}

void OPPROTO op_divl_EAX_T0(void)
{
    helper_divl_EAX_T0(PARAM1);
}

void OPPROTO op_idivl_EAX_T0(void)
{
    helper_idivl_EAX_T0(PARAM1);
}

/* constant load & misc op */

void OPPROTO op_movl_T0_im(void)
{
    T0 = PARAM1;
}

void OPPROTO op_addl_T0_im(void)
{
    T0 += PARAM1;
}

void OPPROTO op_andl_T0_ffff(void)
{
    T0 = T0 & 0xffff;
}

void OPPROTO op_andl_T0_im(void)
{
    T0 = T0 & PARAM1;
}

void OPPROTO op_movl_T0_T1(void)
{
    T0 = T1;
}

void OPPROTO op_movl_T1_im(void)
{
    T1 = PARAM1;
}

void OPPROTO op_addl_T1_im(void)
{
    T1 += PARAM1;
}

void OPPROTO op_movl_T1_A0(void)
{
    T1 = A0;
}

void OPPROTO op_movl_A0_im(void)
{
    A0 = PARAM1;
}

void OPPROTO op_addl_A0_im(void)
{
    A0 += PARAM1;
}

void OPPROTO op_addl_A0_AL(void)
{
    A0 += (EAX & 0xff);
}

void OPPROTO op_andl_A0_ffff(void)
{
    A0 = A0 & 0xffff;
}

/* memory access */

#define MEMSUFFIX _raw
#include "ops_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "ops_mem.h"

#define MEMSUFFIX _kernel
#include "ops_mem.h"
#endif

/* used for bit operations */

void OPPROTO op_add_bitw_A0_T1(void)
{
    A0 += ((int32_t)T1 >> 4) << 1;
}

void OPPROTO op_add_bitl_A0_T1(void)
{
    A0 += ((int32_t)T1 >> 5) << 2;
}

/* indirect jump */

void OPPROTO op_jmp_T0(void)
{
    EIP = T0;
}

void OPPROTO op_jmp_im(void)
{
    EIP = PARAM1;
}

void OPPROTO op_hlt(void)
{
    env->exception_index = EXCP_HLT;
    cpu_loop_exit();
}

void OPPROTO op_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void OPPROTO op_raise_interrupt(void)
{
    int intno;
    unsigned int next_eip;
    intno = PARAM1;
    next_eip = PARAM2;
    raise_interrupt(intno, 1, 0, next_eip);
}

void OPPROTO op_raise_exception(void)
{
    int exception_index;
    exception_index = PARAM1;
    raise_exception(exception_index);
}

void OPPROTO op_into(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_O) {
        raise_interrupt(EXCP04_INTO, 1, 0, PARAM1);
    }
    FORCE_RET();
}

void OPPROTO op_cli(void)
{
    env->eflags &= ~IF_MASK;
}

void OPPROTO op_sti(void)
{
    env->eflags |= IF_MASK;
}

void OPPROTO op_set_inhibit_irq(void)
{
    env->hflags |= HF_INHIBIT_IRQ_MASK;
}

void OPPROTO op_reset_inhibit_irq(void)
{
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
}

#if 0
/* vm86plus instructions */
void OPPROTO op_cli_vm(void)
{
    env->eflags &= ~VIF_MASK;
}

void OPPROTO op_sti_vm(void)
{
    env->eflags |= VIF_MASK;
    if (env->eflags & VIP_MASK) {
        EIP = PARAM1;
        raise_exception(EXCP0D_GPF);
    }
    FORCE_RET();
}
#endif

void OPPROTO op_boundw(void)
{
    int low, high, v;
    low = ldsw((uint8_t *)A0);
    high = ldsw((uint8_t *)A0 + 2);
    v = (int16_t)T0;
    if (v < low || v > high) {
        EIP = PARAM1;
        raise_exception(EXCP05_BOUND);
    }
    FORCE_RET();
}

void OPPROTO op_boundl(void)
{
    int low, high, v;
    low = ldl((uint8_t *)A0);
    high = ldl((uint8_t *)A0 + 4);
    v = T0;
    if (v < low || v > high) {
        EIP = PARAM1;
        raise_exception(EXCP05_BOUND);
    }
    FORCE_RET();
}

void OPPROTO op_cmpxchg8b(void)
{
    helper_cmpxchg8b();
}

void OPPROTO op_jmp(void)
{
    JUMP_TB(op_jmp, PARAM1, 0, PARAM2);
}

void OPPROTO op_movl_T0_0(void)
{
    T0 = 0;
}

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

/* multiple size ops */

#define ldul ldl

#define SHIFT 0
#include "ops_template.h"
#undef SHIFT

#define SHIFT 1
#include "ops_template.h"
#undef SHIFT

#define SHIFT 2
#include "ops_template.h"
#undef SHIFT

/* sign extend */

void OPPROTO op_movsbl_T0_T0(void)
{
    T0 = (int8_t)T0;
}

void OPPROTO op_movzbl_T0_T0(void)
{
    T0 = (uint8_t)T0;
}

void OPPROTO op_movswl_T0_T0(void)
{
    T0 = (int16_t)T0;
}

void OPPROTO op_movzwl_T0_T0(void)
{
    T0 = (uint16_t)T0;
}

void OPPROTO op_movswl_EAX_AX(void)
{
    EAX = (int16_t)EAX;
}

void OPPROTO op_movsbw_AX_AL(void)
{
    EAX = (EAX & 0xffff0000) | ((int8_t)EAX & 0xffff);
}

void OPPROTO op_movslq_EDX_EAX(void)
{
    EDX = (int32_t)EAX >> 31;
}

void OPPROTO op_movswl_DX_AX(void)
{
    EDX = (EDX & 0xffff0000) | (((int16_t)EAX >> 15) & 0xffff);
}

/* string ops helpers */

void OPPROTO op_addl_ESI_T0(void)
{
    ESI += T0;
}

void OPPROTO op_addw_ESI_T0(void)
{
    ESI = (ESI & ~0xffff) | ((ESI + T0) & 0xffff);
}

void OPPROTO op_addl_EDI_T0(void)
{
    EDI += T0;
}

void OPPROTO op_addw_EDI_T0(void)
{
    EDI = (EDI & ~0xffff) | ((EDI + T0) & 0xffff);
}

void OPPROTO op_decl_ECX(void)
{
    ECX--;
}

void OPPROTO op_decw_ECX(void)
{
    ECX = (ECX & ~0xffff) | ((ECX - 1) & 0xffff);
}

/* push/pop */

void op_pushl_T0(void)
{
    uint32_t offset;
    offset = ESP - 4;
    stl((void *)offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushw_T0(void)
{
    uint32_t offset;
    offset = ESP - 2;
    stw((void *)offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushl_ss32_T0(void)
{
    uint32_t offset;
    offset = ESP - 4;
    stl(env->segs[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushw_ss32_T0(void)
{
    uint32_t offset;
    offset = ESP - 2;
    stw(env->segs[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushl_ss16_T0(void)
{
    uint32_t offset;
    offset = (ESP - 4) & 0xffff;
    stl(env->segs[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = (ESP & ~0xffff) | offset;
}

void op_pushw_ss16_T0(void)
{
    uint32_t offset;
    offset = (ESP - 2) & 0xffff;
    stw(env->segs[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = (ESP & ~0xffff) | offset;
}

/* NOTE: ESP update is done after */
void op_popl_T0(void)
{
    T0 = ldl((void *)ESP);
}

void op_popw_T0(void)
{
    T0 = lduw((void *)ESP);
}

void op_popl_ss32_T0(void)
{
    T0 = ldl(env->segs[R_SS].base + ESP);
}

void op_popw_ss32_T0(void)
{
    T0 = lduw(env->segs[R_SS].base + ESP);
}

void op_popl_ss16_T0(void)
{
    T0 = ldl(env->segs[R_SS].base + (ESP & 0xffff));
}

void op_popw_ss16_T0(void)
{
    T0 = lduw(env->segs[R_SS].base + (ESP & 0xffff));
}

void op_addl_ESP_4(void)
{
    ESP += 4;
}

void op_addl_ESP_2(void)
{
    ESP += 2;
}

void op_addw_ESP_4(void)
{
    ESP = (ESP & ~0xffff) | ((ESP + 4) & 0xffff);
}

void op_addw_ESP_2(void)
{
    ESP = (ESP & ~0xffff) | ((ESP + 2) & 0xffff);
}

void op_addl_ESP_im(void)
{
    ESP += PARAM1;
}

void op_addw_ESP_im(void)
{
    ESP = (ESP & ~0xffff) | ((ESP + PARAM1) & 0xffff);
}

void OPPROTO op_rdtsc(void)
{
    helper_rdtsc();
}

void OPPROTO op_cpuid(void)
{
    helper_cpuid();
}

void OPPROTO op_rdmsr(void)
{
    helper_rdmsr();
}

void OPPROTO op_wrmsr(void)
{
    helper_wrmsr();
}

/* bcd */

/* XXX: exception */
void OPPROTO op_aam(void)
{
    int base = PARAM1;
    int al, ah;
    al = EAX & 0xff;
    ah = al / base;
    al = al % base;
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_DST = al;
}

void OPPROTO op_aad(void)
{
    int base = PARAM1;
    int al, ah;
    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;
    al = ((ah * base) + al) & 0xff;
    EAX = (EAX & ~0xffff) | al;
    CC_DST = al;
}

void OPPROTO op_aaa(void)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    af = eflags & CC_A;
    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;

    icarry = (al > 0xf9);
    if (((al & 0x0f) > 9 ) || af) {
        al = (al + 6) & 0x0f;
        ah = (ah + 1 + icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_SRC = eflags;
}

void OPPROTO op_aas(void)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    af = eflags & CC_A;
    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;

    icarry = (al < 6);
    if (((al & 0x0f) > 9 ) || af) {
        al = (al - 6) & 0x0f;
        ah = (ah - 1 - icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_SRC = eflags;
}

void OPPROTO op_daa(void)
{
    int al, af, cf;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    cf = eflags & CC_C;
    af = eflags & CC_A;
    al = EAX & 0xff;

    eflags = 0;
    if (((al & 0x0f) > 9 ) || af) {
        al = (al + 6) & 0xff;
        eflags |= CC_A;
    }
    if ((al > 0x9f) || cf) {
        al = (al + 0x60) & 0xff;
        eflags |= CC_C;
    }
    EAX = (EAX & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

void OPPROTO op_das(void)
{
    int al, al1, af, cf;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    cf = eflags & CC_C;
    af = eflags & CC_A;
    al = EAX & 0xff;

    eflags = 0;
    al1 = al;
    if (((al & 0x0f) > 9 ) || af) {
        eflags |= CC_A;
        if (al < 6 || cf)
            eflags |= CC_C;
        al = (al - 6) & 0xff;
    }
    if ((al1 > 0x99) || cf) {
        al = (al - 0x60) & 0xff;
        eflags |= CC_C;
    }
    EAX = (EAX & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

/* segment handling */

/* never use it with R_CS */
void OPPROTO op_movl_seg_T0(void)
{
    load_seg(PARAM1, T0 & 0xffff, PARAM2);
}

/* faster VM86 version */
void OPPROTO op_movl_seg_T0_vm(void)
{
    int selector;
    SegmentCache *sc;
    
    selector = T0 & 0xffff;
    /* env->segs[] access */
    sc = (SegmentCache *)((char *)env + PARAM1);
    sc->selector = selector;
    sc->base = (void *)(selector << 4);
}

void OPPROTO op_movl_T0_seg(void)
{
    T0 = env->segs[PARAM1].selector;
}

void OPPROTO op_movl_A0_seg(void)
{
    A0 = *(unsigned long *)((char *)env + PARAM1);
}

void OPPROTO op_addl_A0_seg(void)
{
    A0 += *(unsigned long *)((char *)env + PARAM1);
}

void OPPROTO op_lsl(void)
{
    helper_lsl();
}

void OPPROTO op_lar(void)
{
    helper_lar();
}

void OPPROTO op_verr(void)
{
    helper_verr();
}

void OPPROTO op_verw(void)
{
    helper_verw();
}

void OPPROTO op_arpl(void)
{
    if ((T0 & 3) < (T1 & 3)) {
        /* XXX: emulate bug or 0xff3f0000 oring as in bochs ? */
        T0 = (T0 & ~3) | (T1 & 3);
        T1 = CC_Z;
   } else {
        T1 = 0;
    }
    FORCE_RET();
}
            
void OPPROTO op_arpl_update(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    CC_SRC = (eflags & ~CC_Z) | T1;
}
    
/* T0: segment, T1:eip */
void OPPROTO op_ljmp_protected_T0_T1(void)
{
    helper_ljmp_protected_T0_T1();
}

void OPPROTO op_lcall_real_T0_T1(void)
{
    helper_lcall_real_T0_T1(PARAM1, PARAM2);
}

void OPPROTO op_lcall_protected_T0_T1(void)
{
    helper_lcall_protected_T0_T1(PARAM1, PARAM2);
}

void OPPROTO op_iret_real(void)
{
    helper_iret_real(PARAM1);
}

void OPPROTO op_iret_protected(void)
{
    helper_iret_protected(PARAM1);
}

void OPPROTO op_lret_protected(void)
{
    helper_lret_protected(PARAM1, PARAM2);
}

void OPPROTO op_lldt_T0(void)
{
    helper_lldt_T0();
}

void OPPROTO op_ltr_T0(void)
{
    helper_ltr_T0();
}

/* CR registers access */
void OPPROTO op_movl_crN_T0(void)
{
    helper_movl_crN_T0(PARAM1);
}

/* DR registers access */
void OPPROTO op_movl_drN_T0(void)
{
    helper_movl_drN_T0(PARAM1);
}

void OPPROTO op_lmsw_T0(void)
{
    /* only 4 lower bits of CR0 are modified */
    T0 = (env->cr[0] & ~0xf) | (T0 & 0xf);
    helper_movl_crN_T0(0);
}

void OPPROTO op_invlpg_A0(void)
{
    helper_invlpg(A0);
}

void OPPROTO op_movl_T0_env(void)
{
    T0 = *(uint32_t *)((char *)env + PARAM1);
}

void OPPROTO op_movl_env_T0(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movl_env_T1(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T1;
}

void OPPROTO op_clts(void)
{
    env->cr[0] &= ~CR0_TS_MASK;
}

/* flags handling */

/* slow jumps cases : in order to avoid calling a function with a
   pointer (which can generate a stack frame on PowerPC), we use
   op_setcc to set T0 and then call op_jcc. */
void OPPROTO op_jcc(void)
{
    if (T0)
        JUMP_TB(op_jcc, PARAM1, 0, PARAM2);
    else
        JUMP_TB(op_jcc, PARAM1, 1, PARAM3);
    FORCE_RET();
}

void OPPROTO op_jcc_im(void)
{
    if (T0)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

/* slow set cases (compute x86 flags) */
void OPPROTO op_seto_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 11) & 1;
}

void OPPROTO op_setb_T0_cc(void)
{
    T0 = cc_table[CC_OP].compute_c();
}

void OPPROTO op_setz_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 6) & 1;
}

void OPPROTO op_setbe_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags & (CC_Z | CC_C)) != 0;
}

void OPPROTO op_sets_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 7) & 1;
}

void OPPROTO op_setp_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 2) & 1;
}

void OPPROTO op_setl_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = ((eflags ^ (eflags >> 4)) >> 7) & 1;
}

void OPPROTO op_setle_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (((eflags ^ (eflags >> 4)) & 0x80) || (eflags & CC_Z)) != 0;
}

void OPPROTO op_xor_T0_1(void)
{
    T0 ^= 1;
}

void OPPROTO op_set_cc_op(void)
{
    CC_OP = PARAM1;
}

#define FL_UPDATE_MASK16 (FL_UPDATE_MASK32 & 0xffff)

void OPPROTO op_movl_eflags_T0(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~FL_UPDATE_MASK32) | 
        (eflags & FL_UPDATE_MASK32);
}

void OPPROTO op_movw_eflags_T0(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~FL_UPDATE_MASK16) | 
        (eflags & FL_UPDATE_MASK16);
}

void OPPROTO op_movl_eflags_T0_cpl0(void)
{
    load_eflags(T0, FL_UPDATE_CPL0_MASK);
}

void OPPROTO op_movw_eflags_T0_cpl0(void)
{
    load_eflags(T0, FL_UPDATE_CPL0_MASK & 0xffff);
}

#if 0
/* vm86plus version */
void OPPROTO op_movw_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK16 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK16);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}

void OPPROTO op_movl_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK32 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK32);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}
#endif

/* XXX: compute only O flag */
void OPPROTO op_movb_eflags_T0(void)
{
    int of;
    of = cc_table[CC_OP].compute_all() & CC_O;
    CC_SRC = (T0 & (CC_S | CC_Z | CC_A | CC_P | CC_C)) | of;
}

void OPPROTO op_movl_T0_eflags(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    T0 = eflags;
}

/* vm86plus version */
#if 0
void OPPROTO op_movl_T0_eflags_vm(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK | IF_MASK);
    if (env->eflags & VIF_MASK)
        eflags |= IF_MASK;
    T0 = eflags;
}
#endif

void OPPROTO op_cld(void)
{
    DF = 1;
}

void OPPROTO op_std(void)
{
    DF = -1;
}

void OPPROTO op_clc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags &= ~CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_stc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_cmc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags ^= CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_salc(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    EAX = (EAX & ~0xff) | ((-cf) & 0xff);
}

static int compute_all_eflags(void)
{
    return CC_SRC;
}

static int compute_c_eflags(void)
{
    return CC_SRC & CC_C;
}

static int compute_c_mul(void)
{
    int cf;
    cf = (CC_SRC != 0);
    return cf;
}

static int compute_all_mul(void)
{
    int cf, pf, af, zf, sf, of;
    cf = (CC_SRC != 0);
    pf = 0; /* undefined */
    af = 0; /* undefined */
    zf = 0; /* undefined */
    sf = 0; /* undefined */
    of = cf << 11;
    return cf | pf | af | zf | sf | of;
}
    
CCTable cc_table[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = { /* should never happen */ },

    [CC_OP_EFLAGS] = { compute_all_eflags, compute_c_eflags },

    [CC_OP_MUL] = { compute_all_mul, compute_c_mul },

    [CC_OP_ADDB] = { compute_all_addb, compute_c_addb },
    [CC_OP_ADDW] = { compute_all_addw, compute_c_addw  },
    [CC_OP_ADDL] = { compute_all_addl, compute_c_addl  },

    [CC_OP_ADCB] = { compute_all_adcb, compute_c_adcb },
    [CC_OP_ADCW] = { compute_all_adcw, compute_c_adcw  },
    [CC_OP_ADCL] = { compute_all_adcl, compute_c_adcl  },

    [CC_OP_SUBB] = { compute_all_subb, compute_c_subb  },
    [CC_OP_SUBW] = { compute_all_subw, compute_c_subw  },
    [CC_OP_SUBL] = { compute_all_subl, compute_c_subl  },
    
    [CC_OP_SBBB] = { compute_all_sbbb, compute_c_sbbb  },
    [CC_OP_SBBW] = { compute_all_sbbw, compute_c_sbbw  },
    [CC_OP_SBBL] = { compute_all_sbbl, compute_c_sbbl  },
    
    [CC_OP_LOGICB] = { compute_all_logicb, compute_c_logicb },
    [CC_OP_LOGICW] = { compute_all_logicw, compute_c_logicw },
    [CC_OP_LOGICL] = { compute_all_logicl, compute_c_logicl },
    
    [CC_OP_INCB] = { compute_all_incb, compute_c_incl },
    [CC_OP_INCW] = { compute_all_incw, compute_c_incl },
    [CC_OP_INCL] = { compute_all_incl, compute_c_incl },
    
    [CC_OP_DECB] = { compute_all_decb, compute_c_incl },
    [CC_OP_DECW] = { compute_all_decw, compute_c_incl },
    [CC_OP_DECL] = { compute_all_decl, compute_c_incl },
    
    [CC_OP_SHLB] = { compute_all_shlb, compute_c_shlb },
    [CC_OP_SHLW] = { compute_all_shlw, compute_c_shlw },
    [CC_OP_SHLL] = { compute_all_shll, compute_c_shll },

    [CC_OP_SARB] = { compute_all_sarb, compute_c_sarl },
    [CC_OP_SARW] = { compute_all_sarw, compute_c_sarl },
    [CC_OP_SARL] = { compute_all_sarl, compute_c_sarl },
};

/* floating point support. Some of the code for complicated x87
   functions comes from the LGPL'ed x86 emulator found in the Willows
   TWIN windows emulator. */

#if defined(__powerpc__)
extern CPU86_LDouble copysign(CPU86_LDouble, CPU86_LDouble);

/* correct (but slow) PowerPC rint() (glibc version is incorrect) */
double qemu_rint(double x)
{
    double y = 4503599627370496.0;
    if (fabs(x) >= y)
        return x;
    if (x < 0) 
        y = -y;
    y = (x + y) - y;
    if (y == 0.0)
        y = copysign(y, x);
    return y;
}

#define rint qemu_rint
#endif

/* fp load FT0 */

void OPPROTO op_flds_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldl((void *)A0);
    FT0 = FP_CONVERT.f;
#else
    FT0 = ldfl((void *)A0);
#endif
}

void OPPROTO op_fldl_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = ldq((void *)A0);
    FT0 = FP_CONVERT.d;
#else
    FT0 = ldfq((void *)A0);
#endif
}

/* helpers are needed to avoid static constant reference. XXX: find a better way */
#ifdef USE_INT_TO_FLOAT_HELPERS

void helper_fild_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)ldsw((void *)A0);
}

void helper_fildl_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
}

void helper_fildll_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
}

void OPPROTO op_fild_FT0_A0(void)
{
    helper_fild_FT0_A0();
}

void OPPROTO op_fildl_FT0_A0(void)
{
    helper_fildl_FT0_A0();
}

void OPPROTO op_fildll_FT0_A0(void)
{
    helper_fildll_FT0_A0();
}

#else

void OPPROTO op_fild_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldsw((void *)A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    FT0 = (CPU86_LDouble)ldsw((void *)A0);
#endif
}

void OPPROTO op_fildl_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = (int32_t) ldl((void *)A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    FT0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
#endif
}

void OPPROTO op_fildll_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = (int64_t) ldq((void *)A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i64;
#else
    FT0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
#endif
}
#endif

/* fp load ST0 */

void OPPROTO op_flds_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldl((void *)A0);
    env->fpregs[new_fpstt] = FP_CONVERT.f;
#else
    env->fpregs[new_fpstt] = ldfl((void *)A0);
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fldl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = ldq((void *)A0);
    env->fpregs[new_fpstt] = FP_CONVERT.d;
#else
    env->fpregs[new_fpstt] = ldfq((void *)A0);
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fldt_ST0_A0(void)
{
    helper_fldt_ST0_A0();
}

/* helpers are needed to avoid static constant reference. XXX: find a better way */
#ifdef USE_INT_TO_FLOAT_HELPERS

void helper_fild_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt] = (CPU86_LDouble)ldsw((void *)A0);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt] = (CPU86_LDouble)((int32_t)ldl((void *)A0));
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildll_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt] = (CPU86_LDouble)((int64_t)ldq((void *)A0));
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fild_ST0_A0(void)
{
    helper_fild_ST0_A0();
}

void OPPROTO op_fildl_ST0_A0(void)
{
    helper_fildl_ST0_A0();
}

void OPPROTO op_fildll_ST0_A0(void)
{
    helper_fildll_ST0_A0();
}

#else

void OPPROTO op_fild_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldsw((void *)A0);
    env->fpregs[new_fpstt] = (CPU86_LDouble)FP_CONVERT.i32;
#else
    env->fpregs[new_fpstt] = (CPU86_LDouble)ldsw((void *)A0);
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fildl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = (int32_t) ldl((void *)A0);
    env->fpregs[new_fpstt] = (CPU86_LDouble)FP_CONVERT.i32;
#else
    env->fpregs[new_fpstt] = (CPU86_LDouble)((int32_t)ldl((void *)A0));
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fildll_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = (int64_t) ldq((void *)A0);
    env->fpregs[new_fpstt] = (CPU86_LDouble)FP_CONVERT.i64;
#else
    env->fpregs[new_fpstt] = (CPU86_LDouble)((int64_t)ldq((void *)A0));
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

#endif

/* fp store */

void OPPROTO op_fsts_ST0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.f = (float)ST0;
    stfl((void *)A0, FP_CONVERT.f);
#else
    stfl((void *)A0, (float)ST0);
#endif
}

void OPPROTO op_fstl_ST0_A0(void)
{
    stfq((void *)A0, (double)ST0);
}

void OPPROTO op_fstt_ST0_A0(void)
{
    helper_fstt_ST0_A0();
}

void OPPROTO op_fist_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int val;

    d = ST0;
    val = lrint(d);
    if (val != (int16_t)val)
        val = -32768;
    stw((void *)A0, val);
}

void OPPROTO op_fistl_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int val;

    d = ST0;
    val = lrint(d);
    stl((void *)A0, val);
}

void OPPROTO op_fistll_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int64_t val;

    d = ST0;
    val = llrint(d);
    stq((void *)A0, val);
}

void OPPROTO op_fbld_ST0_A0(void)
{
    helper_fbld_ST0_A0();
}

void OPPROTO op_fbst_ST0_A0(void)
{
    helper_fbst_ST0_A0();
}

/* FPU move */

void OPPROTO op_fpush(void)
{
    fpush();
}

void OPPROTO op_fpop(void)
{
    fpop();
}

void OPPROTO op_fdecstp(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fpus &= (~0x4700);
}

void OPPROTO op_fincstp(void)
{
    env->fpstt = (env->fpstt + 1) & 7;
    env->fpus &= (~0x4700);
}

void OPPROTO op_fmov_ST0_FT0(void)
{
    ST0 = FT0;
}

void OPPROTO op_fmov_FT0_STN(void)
{
    FT0 = ST(PARAM1);
}

void OPPROTO op_fmov_ST0_STN(void)
{
    ST0 = ST(PARAM1);
}

void OPPROTO op_fmov_STN_ST0(void)
{
    ST(PARAM1) = ST0;
}

void OPPROTO op_fxchg_ST0_STN(void)
{
    CPU86_LDouble tmp;
    tmp = ST(PARAM1);
    ST(PARAM1) = ST0;
    ST0 = tmp;
}

/* FPU operations */

/* XXX: handle nans */
void OPPROTO op_fcom_ST0_FT0(void)
{
    env->fpus &= (~0x4500);	/* (C3,C2,C0) <-- 000 */
    if (ST0 < FT0)
        env->fpus |= 0x100;	/* (C3,C2,C0) <-- 001 */
    else if (ST0 == FT0)
        env->fpus |= 0x4000; /* (C3,C2,C0) <-- 100 */
    FORCE_RET();
}

/* XXX: handle nans */
void OPPROTO op_fucom_ST0_FT0(void)
{
    env->fpus &= (~0x4500);	/* (C3,C2,C0) <-- 000 */
    if (ST0 < FT0)
        env->fpus |= 0x100;	/* (C3,C2,C0) <-- 001 */
    else if (ST0 == FT0)
        env->fpus |= 0x4000; /* (C3,C2,C0) <-- 100 */
    FORCE_RET();
}

/* XXX: handle nans */
void OPPROTO op_fcomi_ST0_FT0(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags &= ~(CC_Z | CC_P | CC_C);
    if (ST0 < FT0)
        eflags |= CC_C;
    else if (ST0 == FT0)
        eflags |= CC_Z;
    CC_SRC = eflags;
    FORCE_RET();
}

/* XXX: handle nans */
void OPPROTO op_fucomi_ST0_FT0(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags &= ~(CC_Z | CC_P | CC_C);
    if (ST0 < FT0)
        eflags |= CC_C;
    else if (ST0 == FT0)
        eflags |= CC_Z;
    CC_SRC = eflags;
    FORCE_RET();
}

void OPPROTO op_fadd_ST0_FT0(void)
{
    ST0 += FT0;
}

void OPPROTO op_fmul_ST0_FT0(void)
{
    ST0 *= FT0;
}

void OPPROTO op_fsub_ST0_FT0(void)
{
    ST0 -= FT0;
}

void OPPROTO op_fsubr_ST0_FT0(void)
{
    ST0 = FT0 - ST0;
}

void OPPROTO op_fdiv_ST0_FT0(void)
{
    ST0 /= FT0;
}

void OPPROTO op_fdivr_ST0_FT0(void)
{
    ST0 = FT0 / ST0;
}

/* fp operations between STN and ST0 */

void OPPROTO op_fadd_STN_ST0(void)
{
    ST(PARAM1) += ST0;
}

void OPPROTO op_fmul_STN_ST0(void)
{
    ST(PARAM1) *= ST0;
}

void OPPROTO op_fsub_STN_ST0(void)
{
    ST(PARAM1) -= ST0;
}

void OPPROTO op_fsubr_STN_ST0(void)
{
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = ST0 - *p;
}

void OPPROTO op_fdiv_STN_ST0(void)
{
    ST(PARAM1) /= ST0;
}

void OPPROTO op_fdivr_STN_ST0(void)
{
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = ST0 / *p;
}

/* misc FPU operations */
void OPPROTO op_fchs_ST0(void)
{
    ST0 = -ST0;
}

void OPPROTO op_fabs_ST0(void)
{
    ST0 = fabs(ST0);
}

void OPPROTO op_fxam_ST0(void)
{
    helper_fxam_ST0();
}

void OPPROTO op_fld1_ST0(void)
{
    ST0 = f15rk[1];
}

void OPPROTO op_fldl2t_ST0(void)
{
    ST0 = f15rk[6];
}

void OPPROTO op_fldl2e_ST0(void)
{
    ST0 = f15rk[5];
}

void OPPROTO op_fldpi_ST0(void)
{
    ST0 = f15rk[2];
}

void OPPROTO op_fldlg2_ST0(void)
{
    ST0 = f15rk[3];
}

void OPPROTO op_fldln2_ST0(void)
{
    ST0 = f15rk[4];
}

void OPPROTO op_fldz_ST0(void)
{
    ST0 = f15rk[0];
}

void OPPROTO op_fldz_FT0(void)
{
    ST0 = f15rk[0];
}

/* associated heplers to reduce generated code length and to simplify
   relocation (FP constants are usually stored in .rodata section) */

void OPPROTO op_f2xm1(void)
{
    helper_f2xm1();
}

void OPPROTO op_fyl2x(void)
{
    helper_fyl2x();
}

void OPPROTO op_fptan(void)
{
    helper_fptan();
}

void OPPROTO op_fpatan(void)
{
    helper_fpatan();
}

void OPPROTO op_fxtract(void)
{
    helper_fxtract();
}

void OPPROTO op_fprem1(void)
{
    helper_fprem1();
}


void OPPROTO op_fprem(void)
{
    helper_fprem();
}

void OPPROTO op_fyl2xp1(void)
{
    helper_fyl2xp1();
}

void OPPROTO op_fsqrt(void)
{
    helper_fsqrt();
}

void OPPROTO op_fsincos(void)
{
    helper_fsincos();
}

void OPPROTO op_frndint(void)
{
    helper_frndint();
}

void OPPROTO op_fscale(void)
{
    helper_fscale();
}

void OPPROTO op_fsin(void)
{
    helper_fsin();
}

void OPPROTO op_fcos(void)
{
    helper_fcos();
}

void OPPROTO op_fnstsw_A0(void)
{
    int fpus;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    stw((void *)A0, fpus);
}

void OPPROTO op_fnstsw_EAX(void)
{
    int fpus;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    EAX = (EAX & 0xffff0000) | fpus;
}

void OPPROTO op_fnstcw_A0(void)
{
    stw((void *)A0, env->fpuc);
}

void OPPROTO op_fldcw_A0(void)
{
    int rnd_type;
    env->fpuc = lduw((void *)A0);
    /* set rounding mode */
    switch(env->fpuc & RC_MASK) {
    default:
    case RC_NEAR:
        rnd_type = FE_TONEAREST;
        break;
    case RC_DOWN:
        rnd_type = FE_DOWNWARD;
        break;
    case RC_UP:
        rnd_type = FE_UPWARD;
        break;
    case RC_CHOP:
        rnd_type = FE_TOWARDZERO;
        break;
    }
    fesetround(rnd_type);
}

void OPPROTO op_fclex(void)
{
    env->fpus &= 0x7f00;
}

void OPPROTO op_fninit(void)
{
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

void OPPROTO op_fnstenv_A0(void)
{
    helper_fstenv((uint8_t *)A0, PARAM1);
}

void OPPROTO op_fldenv_A0(void)
{
    helper_fldenv((uint8_t *)A0, PARAM1);
}

void OPPROTO op_fnsave_A0(void)
{
    helper_fsave((uint8_t *)A0, PARAM1);
}

void OPPROTO op_frstor_A0(void)
{
    helper_frstor((uint8_t *)A0, PARAM1);
}

/* threading support */
void OPPROTO op_lock(void)
{
    cpu_lock();
}

void OPPROTO op_unlock(void)
{
    cpu_unlock();
}

