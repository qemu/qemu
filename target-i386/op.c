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

#define ASM_SOFTMMU
#include "exec.h"

/* n must be a constant to be efficient */
static inline target_long lshift(target_long x, int n)
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

#ifdef TARGET_X86_64

#define REG (env->regs[8])
#define REGNAME _R8
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[9])
#define REGNAME _R9
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[10])
#define REGNAME _R10
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[11])
#define REGNAME _R11
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[12])
#define REGNAME _R12
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[13])
#define REGNAME _R13
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[14])
#define REGNAME _R14
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[15])
#define REGNAME _R15
#include "opreg_template.h"
#undef REG
#undef REGNAME

#endif

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

#ifdef TARGET_X86_64
void OPPROTO op_bswapq_T0(void)
{
    helper_bswapq_T0();
}
#endif

/* multiply/divide */

/* XXX: add eflags optimizations */
/* XXX: add non P4 style flags */

void OPPROTO op_mulb_AL_T0(void)
{
    unsigned int res;
    res = (uint8_t)EAX * (uint8_t)T0;
    EAX = (EAX & ~0xffff) | res;
    CC_DST = res;
    CC_SRC = (res & 0xff00);
}

void OPPROTO op_imulb_AL_T0(void)
{
    int res;
    res = (int8_t)EAX * (int8_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    CC_DST = res;
    CC_SRC = (res != (int8_t)res);
}

void OPPROTO op_mulw_AX_T0(void)
{
    unsigned int res;
    res = (uint16_t)EAX * (uint16_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    EDX = (EDX & ~0xffff) | ((res >> 16) & 0xffff);
    CC_DST = res;
    CC_SRC = res >> 16;
}

void OPPROTO op_imulw_AX_T0(void)
{
    int res;
    res = (int16_t)EAX * (int16_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    EDX = (EDX & ~0xffff) | ((res >> 16) & 0xffff);
    CC_DST = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_mull_EAX_T0(void)
{
    uint64_t res;
    res = (uint64_t)((uint32_t)EAX) * (uint64_t)((uint32_t)T0);
    EAX = (uint32_t)res;
    EDX = (uint32_t)(res >> 32);
    CC_DST = (uint32_t)res;
    CC_SRC = (uint32_t)(res >> 32);
}

void OPPROTO op_imull_EAX_T0(void)
{
    int64_t res;
    res = (int64_t)((int32_t)EAX) * (int64_t)((int32_t)T0);
    EAX = (uint32_t)(res);
    EDX = (uint32_t)(res >> 32);
    CC_DST = res;
    CC_SRC = (res != (int32_t)res);
}

void OPPROTO op_imulw_T0_T1(void)
{
    int res;
    res = (int16_t)T0 * (int16_t)T1;
    T0 = res;
    CC_DST = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_imull_T0_T1(void)
{
    int64_t res;
    res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
    T0 = res;
    CC_DST = res;
    CC_SRC = (res != (int32_t)res);
}

#ifdef TARGET_X86_64
void OPPROTO op_mulq_EAX_T0(void)
{
    helper_mulq_EAX_T0();
}

void OPPROTO op_imulq_EAX_T0(void)
{
    helper_imulq_EAX_T0();
}

void OPPROTO op_imulq_T0_T1(void)
{
    helper_imulq_T0_T1();
}
#endif

/* division, flags are undefined */

void OPPROTO op_divb_AL_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff);
    den = (T0 & 0xff);
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den);
    if (q > 0xff)
        raise_exception(EXCP00_DIVZ);
    q &= 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & ~0xffff) | (r << 8) | q;
}

void OPPROTO op_idivb_AL_T0(void)
{
    int num, den, q, r;

    num = (int16_t)EAX;
    den = (int8_t)T0;
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den);
    if (q != (int8_t)q)
        raise_exception(EXCP00_DIVZ);
    q &= 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & ~0xffff) | (r << 8) | q;
}

void OPPROTO op_divw_AX_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (T0 & 0xffff);
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den);
    if (q > 0xffff)
        raise_exception(EXCP00_DIVZ);
    q &= 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & ~0xffff) | q;
    EDX = (EDX & ~0xffff) | r;
}

void OPPROTO op_idivw_AX_T0(void)
{
    int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (int16_t)T0;
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    q = (num / den);
    if (q != (int16_t)q)
        raise_exception(EXCP00_DIVZ);
    q &= 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & ~0xffff) | q;
    EDX = (EDX & ~0xffff) | r;
}

void OPPROTO op_divl_EAX_T0(void)
{
    helper_divl_EAX_T0();
}

void OPPROTO op_idivl_EAX_T0(void)
{
    helper_idivl_EAX_T0();
}

#ifdef TARGET_X86_64
void OPPROTO op_divq_EAX_T0(void)
{
    helper_divq_EAX_T0();
}

void OPPROTO op_idivq_EAX_T0(void)
{
    helper_idivq_EAX_T0();
}
#endif

/* constant load & misc op */

/* XXX: consistent names */
void OPPROTO op_movl_T0_imu(void)
{
    T0 = (uint32_t)PARAM1;
}

void OPPROTO op_movl_T0_im(void)
{
    T0 = (int32_t)PARAM1;
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

void OPPROTO op_movl_T1_imu(void)
{
    T1 = (uint32_t)PARAM1;
}

void OPPROTO op_movl_T1_im(void)
{
    T1 = (int32_t)PARAM1;
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
    A0 = (uint32_t)PARAM1;
}

void OPPROTO op_addl_A0_im(void)
{
    A0 = (uint32_t)(A0 + PARAM1);
}

void OPPROTO op_movl_A0_seg(void)
{
    A0 = (uint32_t)*(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_addl_A0_seg(void)
{
    A0 = (uint32_t)(A0 + *(target_ulong *)((char *)env + PARAM1));
}

void OPPROTO op_addl_A0_AL(void)
{
    A0 = (uint32_t)(A0 + (EAX & 0xff));
}

#ifdef WORDS_BIGENDIAN
typedef union UREG64 {
    struct { uint16_t v3, v2, v1, v0; } w;
    struct { uint32_t v1, v0; } l;
    uint64_t q;
} UREG64;
#else
typedef union UREG64 {
    struct { uint16_t v0, v1, v2, v3; } w;
    struct { uint32_t v0, v1; } l;
    uint64_t q;
} UREG64;
#endif

#define PARAMQ1 \
({\
    UREG64 __p;\
    __p.l.v1 = PARAM1;\
    __p.l.v0 = PARAM2;\
    __p.q;\
})

#ifdef TARGET_X86_64

void OPPROTO op_movq_T0_im64(void)
{
    T0 = PARAMQ1;
}

void OPPROTO op_movq_T1_im64(void)
{
    T1 = PARAMQ1;
}

void OPPROTO op_movq_A0_im(void)
{
    A0 = (int32_t)PARAM1;
}

void OPPROTO op_movq_A0_im64(void)
{
    A0 = PARAMQ1;
}

void OPPROTO op_addq_A0_im(void)
{
    A0 = (A0 + (int32_t)PARAM1);
}

void OPPROTO op_addq_A0_im64(void)
{
    A0 = (A0 + PARAMQ1);
}

void OPPROTO op_movq_A0_seg(void)
{
    A0 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_addq_A0_seg(void)
{
    A0 += *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_addq_A0_AL(void)
{
    A0 = (A0 + (EAX & 0xff));
}

#endif

void OPPROTO op_andl_A0_ffff(void)
{
    A0 = A0 & 0xffff;
}

/* memory access */

#define MEMSUFFIX _raw
#include "ops_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _kernel
#include "ops_mem.h"

#define MEMSUFFIX _user
#include "ops_mem.h"
#endif

/* indirect jump */

void OPPROTO op_jmp_T0(void)
{
    EIP = T0;
}

void OPPROTO op_movl_eip_im(void)
{
    EIP = (uint32_t)PARAM1;
}

#ifdef TARGET_X86_64
void OPPROTO op_movq_eip_im(void)
{
    EIP = (int32_t)PARAM1;
}

void OPPROTO op_movq_eip_im64(void)
{
    EIP = PARAMQ1;
}
#endif

void OPPROTO op_hlt(void)
{
    helper_hlt();
}

void OPPROTO op_monitor(void)
{
    helper_monitor();
}

void OPPROTO op_mwait(void)
{
    helper_mwait();
}

void OPPROTO op_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void OPPROTO op_raise_interrupt(void)
{
    int intno, next_eip_addend;
    intno = PARAM1;
    next_eip_addend = PARAM2;
    raise_interrupt(intno, 1, 0, next_eip_addend);
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

void OPPROTO op_rsm(void)
{
    helper_rsm();
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
    low = ldsw(A0);
    high = ldsw(A0 + 2);
    v = (int16_t)T0;
    if (v < low || v > high) {
        raise_exception(EXCP05_BOUND);
    }
    FORCE_RET();
}

void OPPROTO op_boundl(void)
{
    int low, high, v;
    low = ldl(A0);
    high = ldl(A0 + 4);
    v = T0;
    if (v < low || v > high) {
        raise_exception(EXCP05_BOUND);
    }
    FORCE_RET();
}

void OPPROTO op_cmpxchg8b(void)
{
    helper_cmpxchg8b();
}

void OPPROTO op_single_step(void)
{
    helper_single_step();
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

#ifdef TARGET_X86_64

#define SHIFT 3
#include "ops_template.h"
#undef SHIFT

#endif

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
    EAX = (uint32_t)((int16_t)EAX);
}

#ifdef TARGET_X86_64
void OPPROTO op_movslq_T0_T0(void)
{
    T0 = (int32_t)T0;
}

void OPPROTO op_movslq_RAX_EAX(void)
{
    EAX = (int32_t)EAX;
}
#endif

void OPPROTO op_movsbw_AX_AL(void)
{
    EAX = (EAX & ~0xffff) | ((int8_t)EAX & 0xffff);
}

void OPPROTO op_movslq_EDX_EAX(void)
{
    EDX = (uint32_t)((int32_t)EAX >> 31);
}

void OPPROTO op_movswl_DX_AX(void)
{
    EDX = (EDX & ~0xffff) | (((int16_t)EAX >> 15) & 0xffff);
}

#ifdef TARGET_X86_64
void OPPROTO op_movsqo_RDX_RAX(void)
{
    EDX = (int64_t)EAX >> 63;
}
#endif

/* string ops helpers */

void OPPROTO op_addl_ESI_T0(void)
{
    ESI = (uint32_t)(ESI + T0);
}

void OPPROTO op_addw_ESI_T0(void)
{
    ESI = (ESI & ~0xffff) | ((ESI + T0) & 0xffff);
}

void OPPROTO op_addl_EDI_T0(void)
{
    EDI = (uint32_t)(EDI + T0);
}

void OPPROTO op_addw_EDI_T0(void)
{
    EDI = (EDI & ~0xffff) | ((EDI + T0) & 0xffff);
}

void OPPROTO op_decl_ECX(void)
{
    ECX = (uint32_t)(ECX - 1);
}

void OPPROTO op_decw_ECX(void)
{
    ECX = (ECX & ~0xffff) | ((ECX - 1) & 0xffff);
}

#ifdef TARGET_X86_64
void OPPROTO op_addq_ESI_T0(void)
{
    ESI = (ESI + T0);
}

void OPPROTO op_addq_EDI_T0(void)
{
    EDI = (EDI + T0);
}

void OPPROTO op_decq_ECX(void)
{
    ECX--;
}
#endif

/* push/pop utils */

void op_addl_A0_SS(void)
{
    A0 = (uint32_t)(A0 + env->segs[R_SS].base);
}

void op_subl_A0_2(void)
{
    A0 = (uint32_t)(A0 - 2);
}

void op_subl_A0_4(void)
{
    A0 = (uint32_t)(A0 - 4);
}

void op_addl_ESP_4(void)
{
    ESP = (uint32_t)(ESP + 4);
}

void op_addl_ESP_2(void)
{
    ESP = (uint32_t)(ESP + 2);
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
    ESP = (uint32_t)(ESP + PARAM1);
}

void op_addw_ESP_im(void)
{
    ESP = (ESP & ~0xffff) | ((ESP + PARAM1) & 0xffff);
}

#ifdef TARGET_X86_64
void op_subq_A0_2(void)
{
    A0 -= 2;
}

void op_subq_A0_8(void)
{
    A0 -= 8;
}

void op_addq_ESP_8(void)
{
    ESP += 8;
}

void op_addq_ESP_im(void)
{
    ESP += PARAM1;
}
#endif

void OPPROTO op_rdtsc(void)
{
    helper_rdtsc();
}

void OPPROTO op_rdpmc(void)
{
    helper_rdpmc();
}

void OPPROTO op_cpuid(void)
{
    helper_cpuid();
}

void OPPROTO op_enter_level(void)
{
    helper_enter_level(PARAM1, PARAM2);
}

#ifdef TARGET_X86_64
void OPPROTO op_enter64_level(void)
{
    helper_enter64_level(PARAM1, PARAM2);
}
#endif

void OPPROTO op_sysenter(void)
{
    helper_sysenter();
}

void OPPROTO op_sysexit(void)
{
    helper_sysexit();
}

#ifdef TARGET_X86_64
void OPPROTO op_syscall(void)
{
    helper_syscall(PARAM1);
}

void OPPROTO op_sysret(void)
{
    helper_sysret(PARAM1);
}
#endif

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
    FORCE_RET();
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
    FORCE_RET();
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
    FORCE_RET();
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
    FORCE_RET();
}

/* segment handling */

/* never use it with R_CS */
void OPPROTO op_movl_seg_T0(void)
{
    load_seg(PARAM1, T0);
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
    sc->base = (selector << 4);
}

void OPPROTO op_movl_T0_seg(void)
{
    T0 = env->segs[PARAM1].selector;
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
    helper_ljmp_protected_T0_T1(PARAM1);
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
    helper_iret_protected(PARAM1, PARAM2);
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

/* CR registers access. */
void OPPROTO op_movl_crN_T0(void)
{
    helper_movl_crN_T0(PARAM1);
}

/* These pseudo-opcodes check for SVM intercepts. */
void OPPROTO op_svm_check_intercept(void)
{
    A0 = PARAM1 & PARAM2;
    svm_check_intercept(PARAMQ1);
}

void OPPROTO op_svm_check_intercept_param(void)
{
    A0 = PARAM1 & PARAM2;
    svm_check_intercept_param(PARAMQ1, T1);
}

void OPPROTO op_svm_vmexit(void)
{
    A0 = PARAM1 & PARAM2;
    vmexit(PARAMQ1, T1);
}

void OPPROTO op_geneflags(void)
{
    CC_SRC = cc_table[CC_OP].compute_all();
}

/* This pseudo-opcode checks for IO intercepts. */
#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_svm_check_intercept_io(void)
{
    A0 = PARAM1 & PARAM2;
    /* PARAMQ1 = TYPE (0 = OUT, 1 = IN; 4 = STRING; 8 = REP)
       T0      = PORT
       T1      = next eip */
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), T1);
    /* ASIZE does not appear on real hw */
    svm_check_intercept_param(SVM_EXIT_IOIO,
                              (PARAMQ1 & ~SVM_IOIO_ASIZE_MASK) |
                              ((T0 & 0xffff) << 16));
}
#endif

#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_movtl_T0_cr8(void)
{
    T0 = cpu_get_apic_tpr(env);
}
#endif

/* DR registers access */
void OPPROTO op_movl_drN_T0(void)
{
    helper_movl_drN_T0(PARAM1);
}

void OPPROTO op_lmsw_T0(void)
{
    /* only 4 lower bits of CR0 are modified. PE cannot be set to zero
       if already set to one. */
    T0 = (env->cr[0] & ~0xe) | (T0 & 0xf);
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

void OPPROTO op_movtl_T0_env(void)
{
    T0 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T0(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movtl_T1_env(void)
{
    T1 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T1(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T1;
}

void OPPROTO op_clts(void)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
}

/* flags handling */

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

void OPPROTO op_jnz_T0_label(void)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_jz_T0_label(void)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
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

void OPPROTO op_mov_T0_cc(void)
{
    T0 = cc_table[CC_OP].compute_all();
}

/* XXX: clear VIF/VIP in all ops ? */

void OPPROTO op_movl_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK));
}

void OPPROTO op_movw_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK));
}

void OPPROTO op_movw_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK));
}

void OPPROTO op_movw_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK) & 0xffff);
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

CCTable cc_table[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = { /* should never happen */ },

    [CC_OP_EFLAGS] = { compute_all_eflags, compute_c_eflags },

    [CC_OP_MULB] = { compute_all_mulb, compute_c_mull },
    [CC_OP_MULW] = { compute_all_mulw, compute_c_mull },
    [CC_OP_MULL] = { compute_all_mull, compute_c_mull },

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

#ifdef TARGET_X86_64
    [CC_OP_MULQ] = { compute_all_mulq, compute_c_mull },

    [CC_OP_ADDQ] = { compute_all_addq, compute_c_addq  },

    [CC_OP_ADCQ] = { compute_all_adcq, compute_c_adcq  },

    [CC_OP_SUBQ] = { compute_all_subq, compute_c_subq  },

    [CC_OP_SBBQ] = { compute_all_sbbq, compute_c_sbbq  },

    [CC_OP_LOGICQ] = { compute_all_logicq, compute_c_logicq },

    [CC_OP_INCQ] = { compute_all_incq, compute_c_incl },

    [CC_OP_DECQ] = { compute_all_decq, compute_c_incl },

    [CC_OP_SHLQ] = { compute_all_shlq, compute_c_shlq },

    [CC_OP_SARQ] = { compute_all_sarq, compute_c_sarl },
#endif
};

/* floating point support. Some of the code for complicated x87
   functions comes from the LGPL'ed x86 emulator found in the Willows
   TWIN windows emulator. */

/* fp load FT0 */

void OPPROTO op_flds_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldl(A0);
    FT0 = FP_CONVERT.f;
#else
    FT0 = ldfl(A0);
#endif
}

void OPPROTO op_fldl_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = ldq(A0);
    FT0 = FP_CONVERT.d;
#else
    FT0 = ldfq(A0);
#endif
}

/* helpers are needed to avoid static constant reference. XXX: find a better way */
#ifdef USE_INT_TO_FLOAT_HELPERS

void helper_fild_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)ldsw(A0);
}

void helper_fildl_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int32_t)ldl(A0));
}

void helper_fildll_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int64_t)ldq(A0));
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
    FP_CONVERT.i32 = ldsw(A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    FT0 = (CPU86_LDouble)ldsw(A0);
#endif
}

void OPPROTO op_fildl_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = (int32_t) ldl(A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    FT0 = (CPU86_LDouble)((int32_t)ldl(A0));
#endif
}

void OPPROTO op_fildll_FT0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = (int64_t) ldq(A0);
    FT0 = (CPU86_LDouble)FP_CONVERT.i64;
#else
    FT0 = (CPU86_LDouble)((int64_t)ldq(A0));
#endif
}
#endif

/* fp load ST0 */

void OPPROTO op_flds_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldl(A0);
    env->fpregs[new_fpstt].d = FP_CONVERT.f;
#else
    env->fpregs[new_fpstt].d = ldfl(A0);
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fldl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = ldq(A0);
    env->fpregs[new_fpstt].d = FP_CONVERT.d;
#else
    env->fpregs[new_fpstt].d = ldfq(A0);
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
    env->fpregs[new_fpstt].d = (CPU86_LDouble)ldsw(A0);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = (CPU86_LDouble)((int32_t)ldl(A0));
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildll_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = (CPU86_LDouble)((int64_t)ldq(A0));
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
    FP_CONVERT.i32 = ldsw(A0);
    env->fpregs[new_fpstt].d = (CPU86_LDouble)FP_CONVERT.i32;
#else
    env->fpregs[new_fpstt].d = (CPU86_LDouble)ldsw(A0);
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fildl_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = (int32_t) ldl(A0);
    env->fpregs[new_fpstt].d = (CPU86_LDouble)FP_CONVERT.i32;
#else
    env->fpregs[new_fpstt].d = (CPU86_LDouble)((int32_t)ldl(A0));
#endif
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void OPPROTO op_fildll_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = (int64_t) ldq(A0);
    env->fpregs[new_fpstt].d = (CPU86_LDouble)FP_CONVERT.i64;
#else
    env->fpregs[new_fpstt].d = (CPU86_LDouble)((int64_t)ldq(A0));
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
    stfl(A0, FP_CONVERT.f);
#else
    stfl(A0, (float)ST0);
#endif
    FORCE_RET();
}

void OPPROTO op_fstl_ST0_A0(void)
{
    stfq(A0, (double)ST0);
    FORCE_RET();
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
    val = floatx_to_int32(d, &env->fp_status);
    if (val != (int16_t)val)
        val = -32768;
    stw(A0, val);
    FORCE_RET();
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
    val = floatx_to_int32(d, &env->fp_status);
    stl(A0, val);
    FORCE_RET();
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
    val = floatx_to_int64(d, &env->fp_status);
    stq(A0, val);
    FORCE_RET();
}

void OPPROTO op_fistt_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int val;

    d = ST0;
    val = floatx_to_int32_round_to_zero(d, &env->fp_status);
    if (val != (int16_t)val)
        val = -32768;
    stw(A0, val);
    FORCE_RET();
}

void OPPROTO op_fisttl_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int val;

    d = ST0;
    val = floatx_to_int32_round_to_zero(d, &env->fp_status);
    stl(A0, val);
    FORCE_RET();
}

void OPPROTO op_fisttll_ST0_A0(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
    register CPU86_LDouble d asm("o0");
#else
    CPU86_LDouble d;
#endif
    int64_t val;

    d = ST0;
    val = floatx_to_int64_round_to_zero(d, &env->fp_status);
    stq(A0, val);
    FORCE_RET();
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

void OPPROTO op_ffree_STN(void)
{
    env->fptags[(env->fpstt + PARAM1) & 7] = 1;
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

const int fcom_ccval[4] = {0x0100, 0x4000, 0x0000, 0x4500};

void OPPROTO op_fcom_ST0_FT0(void)
{
    int ret;

    ret = floatx_compare(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
    FORCE_RET();
}

void OPPROTO op_fucom_ST0_FT0(void)
{
    int ret;

    ret = floatx_compare_quiet(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret+ 1];
    FORCE_RET();
}

const int fcomi_ccval[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void OPPROTO op_fcomi_ST0_FT0(void)
{
    int eflags;
    int ret;

    ret = floatx_compare(ST0, FT0, &env->fp_status);
    eflags = cc_table[CC_OP].compute_all();
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    FORCE_RET();
}

void OPPROTO op_fucomi_ST0_FT0(void)
{
    int eflags;
    int ret;

    ret = floatx_compare_quiet(ST0, FT0, &env->fp_status);
    eflags = cc_table[CC_OP].compute_all();
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    FORCE_RET();
}

void OPPROTO op_fcmov_ST0_STN_T0(void)
{
    if (T0) {
        ST0 = ST(PARAM1);
    }
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
    ST0 = helper_fdiv(ST0, FT0);
}

void OPPROTO op_fdivr_ST0_FT0(void)
{
    ST0 = helper_fdiv(FT0, ST0);
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
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = helper_fdiv(*p, ST0);
}

void OPPROTO op_fdivr_STN_ST0(void)
{
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = helper_fdiv(ST0, *p);
}

/* misc FPU operations */
void OPPROTO op_fchs_ST0(void)
{
    ST0 = floatx_chs(ST0);
}

void OPPROTO op_fabs_ST0(void)
{
    ST0 = floatx_abs(ST0);
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
    FT0 = f15rk[0];
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
    stw(A0, fpus);
    FORCE_RET();
}

void OPPROTO op_fnstsw_EAX(void)
{
    int fpus;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    EAX = (EAX & ~0xffff) | fpus;
}

void OPPROTO op_fnstcw_A0(void)
{
    stw(A0, env->fpuc);
    FORCE_RET();
}

void OPPROTO op_fldcw_A0(void)
{
    env->fpuc = lduw(A0);
    update_fp_status();
}

void OPPROTO op_fclex(void)
{
    env->fpus &= 0x7f00;
}

void OPPROTO op_fwait(void)
{
    if (env->fpus & FPUS_SE)
        fpu_raise_exception();
    FORCE_RET();
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
    helper_fstenv(A0, PARAM1);
}

void OPPROTO op_fldenv_A0(void)
{
    helper_fldenv(A0, PARAM1);
}

void OPPROTO op_fnsave_A0(void)
{
    helper_fsave(A0, PARAM1);
}

void OPPROTO op_frstor_A0(void)
{
    helper_frstor(A0, PARAM1);
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

/* SSE support */
static inline void memcpy16(void *d, void *s)
{
    ((uint32_t *)d)[0] = ((uint32_t *)s)[0];
    ((uint32_t *)d)[1] = ((uint32_t *)s)[1];
    ((uint32_t *)d)[2] = ((uint32_t *)s)[2];
    ((uint32_t *)d)[3] = ((uint32_t *)s)[3];
}

void OPPROTO op_movo(void)
{
    /* XXX: badly generated code */
    XMMReg *d, *s;
    d = (XMMReg *)((char *)env + PARAM1);
    s = (XMMReg *)((char *)env + PARAM2);
    memcpy16(d, s);
}

void OPPROTO op_movq(void)
{
    uint64_t *d, *s;
    d = (uint64_t *)((char *)env + PARAM1);
    s = (uint64_t *)((char *)env + PARAM2);
    *d = *s;
}

void OPPROTO op_movl(void)
{
    uint32_t *d, *s;
    d = (uint32_t *)((char *)env + PARAM1);
    s = (uint32_t *)((char *)env + PARAM2);
    *d = *s;
}

void OPPROTO op_movq_env_0(void)
{
    uint64_t *d;
    d = (uint64_t *)((char *)env + PARAM1);
    *d = 0;
}

void OPPROTO op_fxsave_A0(void)
{
    helper_fxsave(A0, PARAM1);
}

void OPPROTO op_fxrstor_A0(void)
{
    helper_fxrstor(A0, PARAM1);
}

/* XXX: optimize by storing fptt and fptags in the static cpu state */
void OPPROTO op_enter_mmx(void)
{
    env->fpstt = 0;
    *(uint32_t *)(env->fptags) = 0;
    *(uint32_t *)(env->fptags + 4) = 0;
}

void OPPROTO op_emms(void)
{
    /* set to empty state */
    *(uint32_t *)(env->fptags) = 0x01010101;
    *(uint32_t *)(env->fptags + 4) = 0x01010101;
}

#define SHIFT 0
#include "ops_sse.h"

#define SHIFT 1
#include "ops_sse.h"

/* Secure Virtual Machine ops */

void OPPROTO op_vmrun(void)
{
    helper_vmrun(EAX);
}

void OPPROTO op_vmmcall(void)
{
    helper_vmmcall();
}

void OPPROTO op_vmload(void)
{
    helper_vmload(EAX);
}

void OPPROTO op_vmsave(void)
{
    helper_vmsave(EAX);
}

void OPPROTO op_stgi(void)
{
    helper_stgi();
}

void OPPROTO op_clgi(void)
{
    helper_clgi();
}

void OPPROTO op_skinit(void)
{
    helper_skinit();
}

void OPPROTO op_invlpga(void)
{
    helper_invlpga();
}
