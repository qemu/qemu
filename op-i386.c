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
#include "exec-i386.h"

/* NOTE: data are not static to force relocation generation by GCC */

uint8_t parity_table[256] = {
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

#ifdef USE_X86LDOUBLE
/* an array of Intel 80-bit FP constants, to be loaded via integer ops */
typedef unsigned short f15ld[5];
const f15ld f15rk[] =
{
/*0*/	{0x0000,0x0000,0x0000,0x0000,0x0000},
/*1*/	{0x0000,0x0000,0x0000,0x8000,0x3fff},
/*pi*/	{0xc235,0x2168,0xdaa2,0xc90f,0x4000},
/*lg2*/	{0xf799,0xfbcf,0x9a84,0x9a20,0x3ffd},
/*ln2*/	{0x79ac,0xd1cf,0x17f7,0xb172,0x3ffe},
/*l2e*/	{0xf0bc,0x5c17,0x3b29,0xb8aa,0x3fff},
/*l2t*/	{0x8afe,0xcd1b,0x784b,0xd49a,0x4000}
};
#else
/* the same, 64-bit version */
typedef unsigned short f15ld[4];
const f15ld f15rk[] =
{
#ifndef WORDS_BIGENDIAN
/*0*/	{0x0000,0x0000,0x0000,0x0000},
/*1*/	{0x0000,0x0000,0x0000,0x3ff0},
/*pi*/	{0x2d18,0x5444,0x21fb,0x4009},
/*lg2*/	{0x79ff,0x509f,0x4413,0x3fd3},
/*ln2*/	{0x39ef,0xfefa,0x2e42,0x3fe6},
/*l2e*/	{0x82fe,0x652b,0x1547,0x3ff7},
/*l2t*/	{0xa371,0x0979,0x934f,0x400a}
#else
/*0*/   {0x0000,0x0000,0x0000,0x0000},
/*1*/   {0x3ff0,0x0000,0x0000,0x0000},
/*pi*/  {0x4009,0x21fb,0x5444,0x2d18},
/*lg2*/	{0x3fd3,0x4413,0x509f,0x79ff},
/*ln2*/	{0x3fe6,0x2e42,0xfefa,0x39ef},
/*l2e*/	{0x3ff7,0x1547,0x652b,0x82fe},
/*l2t*/	{0x400a,0x934f,0x0979,0xa371}
#endif
};
#endif
    
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

void OPPROTO op_addl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 += T1;
    CC_DST = T0;
}

void OPPROTO op_orl_T0_T1_cc(void)
{
    T0 |= T1;
    CC_DST = T0;
}

void OPPROTO op_andl_T0_T1_cc(void)
{
    T0 &= T1;
    CC_DST = T0;
}

void OPPROTO op_subl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 -= T1;
    CC_DST = T0;
}

void OPPROTO op_xorl_T0_T1_cc(void)
{
    T0 ^= T1;
    CC_DST = T0;
}

void OPPROTO op_cmpl_T0_T1_cc(void)
{
    CC_SRC = T0;
    CC_DST = T0 - T1;
}

void OPPROTO op_negl_T0_cc(void)
{
    CC_SRC = 0;
    T0 = -T0;
    CC_DST = T0;
}

void OPPROTO op_incl_T0_cc(void)
{
    CC_SRC = cc_table[CC_OP].compute_c();
    T0++;
    CC_DST = T0;
}

void OPPROTO op_decl_T0_cc(void)
{
    CC_SRC = cc_table[CC_OP].compute_c();
    T0--;
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
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_idivb_AL_T0(void)
{
    int num, den, q, r;

    num = (int16_t)EAX;
    den = (int8_t)T0;
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_divw_AX_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (T0 & 0xffff);
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
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
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
    q = (num / den) & 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & 0xffff0000) | q;
    EDX = (EDX & 0xffff0000) | r;
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

void OPPROTO op_divl_EAX_T0(void)
{
    unsigned int den, q, r;
    uint64_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = T0;
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
#ifdef BUGGY_GCC_DIV64
    r = div64(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    EAX = q;
    EDX = r;
}

void OPPROTO op_idivl_EAX_T0(void)
{
    int den, q, r;
    int64_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = T0;
    if (den == 0)
        raise_exception(EXCP00_DIVZ);
#ifdef BUGGY_GCC_DIV64
    r = idiv64(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    EAX = q;
    EDX = r;
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

void OPPROTO op_ldub_T0_A0(void)
{
    T0 = ldub((uint8_t *)A0);
}

void OPPROTO op_ldsb_T0_A0(void)
{
    T0 = ldsb((int8_t *)A0);
}

void OPPROTO op_lduw_T0_A0(void)
{
    T0 = lduw((uint8_t *)A0);
}

void OPPROTO op_ldsw_T0_A0(void)
{
    T0 = ldsw((int8_t *)A0);
}

void OPPROTO op_ldl_T0_A0(void)
{
    T0 = ldl((uint8_t *)A0);
}

void OPPROTO op_ldub_T1_A0(void)
{
    T1 = ldub((uint8_t *)A0);
}

void OPPROTO op_ldsb_T1_A0(void)
{
    T1 = ldsb((int8_t *)A0);
}

void OPPROTO op_lduw_T1_A0(void)
{
    T1 = lduw((uint8_t *)A0);
}

void OPPROTO op_ldsw_T1_A0(void)
{
    T1 = ldsw((int8_t *)A0);
}

void OPPROTO op_ldl_T1_A0(void)
{
    T1 = ldl((uint8_t *)A0);
}

void OPPROTO op_stb_T0_A0(void)
{
    stb((uint8_t *)A0, T0);
}

void OPPROTO op_stw_T0_A0(void)
{
    stw((uint8_t *)A0, T0);
}

void OPPROTO op_stl_T0_A0(void)
{
    stl((uint8_t *)A0, T0);
}

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

void OPPROTO op_int_im(void)
{
    int intno;
    intno = PARAM1;
    EIP = PARAM2;
    raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
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
        raise_exception(EXCP04_INTO);
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
    if (v < low || v > high)
        raise_exception(EXCP05_BOUND);
    FORCE_RET();
}

void OPPROTO op_boundl(void)
{
    int low, high, v;
    low = ldl((uint8_t *)A0);
    high = ldl((uint8_t *)A0 + 4);
    v = T0;
    if (v < low || v > high)
        raise_exception(EXCP05_BOUND);
    FORCE_RET();
}

void OPPROTO op_cmpxchg8b(void)
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
    FORCE_RET();
}

/* string ops */

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
    stl(env->seg_cache[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushw_ss32_T0(void)
{
    uint32_t offset;
    offset = ESP - 2;
    stw(env->seg_cache[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushl_ss16_T0(void)
{
    uint32_t offset;
    offset = (ESP - 4) & 0xffff;
    stl(env->seg_cache[R_SS].base + offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = (ESP & ~0xffff) | offset;
}

void op_pushw_ss16_T0(void)
{
    uint32_t offset;
    offset = (ESP - 2) & 0xffff;
    stw(env->seg_cache[R_SS].base + offset, T0);
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
    T0 = ldl(env->seg_cache[R_SS].base + ESP);
}

void op_popw_ss32_T0(void)
{
    T0 = lduw(env->seg_cache[R_SS].base + ESP);
}

void op_popl_ss16_T0(void)
{
    T0 = ldl(env->seg_cache[R_SS].base + (ESP & 0xffff));
}

void op_popw_ss16_T0(void)
{
    T0 = lduw(env->seg_cache[R_SS].base + (ESP & 0xffff));
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

/* rdtsc */
#ifndef __i386__
uint64_t emu_time;
#endif

void OPPROTO op_rdtsc(void)
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
    } else {
        /* EAX = 1 info */
        EAX = 0x52b;
        EBX = 0;
        ECX = 0;
        EDX = CPUID_FP87 | CPUID_DE | CPUID_PSE |
            CPUID_TSC | CPUID_MSR | CPUID_MCE |
            CPUID_CX8;
    }
}

void OPPROTO op_cpuid(void)
{
    helper_cpuid();
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

/* XXX: use static VM86 information */
void load_seg(int seg_reg, int selector)
{
    SegmentCache *sc;
    SegmentDescriptorTable *dt;
    int index;
    uint32_t e1, e2;
    uint8_t *ptr;

    sc = &env->seg_cache[seg_reg];
    if (env->eflags & VM_MASK) {
        sc->base = (void *)(selector << 4);
        sc->limit = 0xffff;
        sc->seg_32bit = 0;
    } else {
        if (selector & 0x4)
            dt = &env->ldt;
        else
            dt = &env->gdt;
        index = selector & ~7;
        if ((index + 7) > dt->limit)
            raise_exception_err(EXCP0D_GPF, selector);
        ptr = dt->base + index;
        e1 = ldl(ptr);
        e2 = ldl(ptr + 4);
        sc->base = (void *)((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
        sc->limit = (e1 & 0xffff) | (e2 & 0x000f0000);
        if (e2 & (1 << 23))
            sc->limit = (sc->limit << 12) | 0xfff;
        sc->seg_32bit = (e2 >> 22) & 1;
#if 0
        fprintf(logfile, "load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx seg_32bit=%d\n", 
                selector, (unsigned long)sc->base, sc->limit, sc->seg_32bit);
#endif
    }
    env->segs[seg_reg] = selector;
}

void OPPROTO op_movl_seg_T0(void)
{
    load_seg(PARAM1, T0 & 0xffff);
}

void OPPROTO op_movl_T0_seg(void)
{
    T0 = env->segs[PARAM1];
}

void OPPROTO op_movl_A0_seg(void)
{
    A0 = *(unsigned long *)((char *)env + PARAM1);
}

void OPPROTO op_addl_A0_seg(void)
{
    A0 += *(unsigned long *)((char *)env + PARAM1);
}

/* flags handling */

/* slow jumps cases (compute x86 flags) */
void OPPROTO op_jo_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_O)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jb_cc(void)
{
    if (cc_table[CC_OP].compute_c())
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jz_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_Z)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jbe_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & (CC_Z | CC_C))
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_js_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_S)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jp_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_P)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jl_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if ((eflags ^ (eflags >> 4)) & 0x80)
        EIP = PARAM1;
    else
        EIP = PARAM2;
    FORCE_RET();
}

void OPPROTO op_jle_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (((eflags ^ (eflags >> 4)) & 0x80) || (eflags & CC_Z))
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

#define FL_UPDATE_MASK32 (TF_MASK | AC_MASK | ID_MASK)
#define FL_UPDATE_MASK16 (TF_MASK)

void OPPROTO op_movl_eflags_T0(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~FL_UPDATE_MASK32) | (eflags & FL_UPDATE_MASK32);
}

void OPPROTO op_movw_eflags_T0(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~FL_UPDATE_MASK16) | (eflags & FL_UPDATE_MASK16);
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
    
    [CC_OP_SHLB] = { compute_all_shlb, compute_c_shll },
    [CC_OP_SHLW] = { compute_all_shlw, compute_c_shll },
    [CC_OP_SHLL] = { compute_all_shll, compute_c_shll },

    [CC_OP_SARB] = { compute_all_sarb, compute_c_shll },
    [CC_OP_SARW] = { compute_all_sarw, compute_c_shll },
    [CC_OP_SARL] = { compute_all_sarl, compute_c_shll },
};

/* floating point support. Some of the code for complicated x87
   functions comes from the LGPL'ed x86 emulator found in the Willows
   TWIN windows emulator. */

#ifdef USE_X86LDOUBLE
/* use long double functions */
#define lrint lrintl
#define llrint llrintl
#define fabs fabsl
#define sin sinl
#define cos cosl
#define sqrt sqrtl
#define pow powl
#define log logl
#define tan tanl
#define atan2 atan2l
#define floor floorl
#define ceil ceill
#define rint rintl
#endif

extern int lrint(CPU86_LDouble x);
extern int64_t llrint(CPU86_LDouble x);
extern CPU86_LDouble fabs(CPU86_LDouble x);
extern CPU86_LDouble sin(CPU86_LDouble x);
extern CPU86_LDouble cos(CPU86_LDouble x);
extern CPU86_LDouble sqrt(CPU86_LDouble x);
extern CPU86_LDouble pow(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble log(CPU86_LDouble x);
extern CPU86_LDouble tan(CPU86_LDouble x);
extern CPU86_LDouble atan2(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble floor(CPU86_LDouble x);
extern CPU86_LDouble ceil(CPU86_LDouble x);
extern CPU86_LDouble rint(CPU86_LDouble x);

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

#define RC_MASK         0xc00
#define RC_NEAR		0x000
#define RC_DOWN		0x400
#define RC_UP		0x800
#define RC_CHOP		0xc00

#define MAXTAN 9223372036854775808.0

#ifdef USE_X86LDOUBLE

/* only for x86 */
typedef union {
    long double d;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
} CPU86_LDoubleU;

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)	(fp.l.upper & 0x7fff)
#define SIGND(fp)	((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#else

typedef union {
    double d;
#ifndef WORDS_BIGENDIAN
    struct {
        unsigned long lower;
        long upper;
    } l;
#else
    struct {
        long upper;
        unsigned long lower;
    } l;
#endif
    long long ll;
} CPU86_LDoubleU;

/* the following deal with IEEE double-precision numbers */
#define MAXEXPD 0x7ff
#define EXPBIAS 1023
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & 0x80000000)
#define MANTD(fp)	(fp.ll & ((1LL << 52) - 1))
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7ff << 20)) | (EXPBIAS << 20)
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
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldl((void *)A0);
    ST0 = FP_CONVERT.f;
#else
    ST0 = ldfl((void *)A0);
#endif
}

void OPPROTO op_fldl_ST0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = ldq((void *)A0);
    ST0 = FP_CONVERT.d;
#else
    ST0 = ldfq((void *)A0);
#endif
}

#ifdef USE_X86LDOUBLE
void OPPROTO op_fldt_ST0_A0(void)
{
    ST0 = *(long double *)A0;
}
#else
void helper_fldt_ST0_A0(void)
{
    CPU86_LDoubleU temp;
    int upper, e;
    /* mantissa */
    upper = lduw((uint8_t *)A0 + 8);
    /* XXX: handle overflow ? */
    e = (upper & 0x7fff) - 16383 + EXPBIAS; /* exponent */
    e |= (upper >> 4) & 0x800; /* sign */
    temp.ll = ((ldq((void *)A0) >> 11) & ((1LL << 52) - 1)) | ((uint64_t)e << 52);
    ST0 = temp.d;
}

void OPPROTO op_fldt_ST0_A0(void)
{
    helper_fldt_ST0_A0();
}
#endif

/* helpers are needed to avoid static constant reference. XXX: find a better way */
#ifdef USE_INT_TO_FLOAT_HELPERS

void helper_fild_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)ldsw((void *)A0);
}

void helper_fildl_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
}

void helper_fildll_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
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
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = ldsw((void *)A0);
    ST0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    ST0 = (CPU86_LDouble)ldsw((void *)A0);
#endif
}

void OPPROTO op_fildl_ST0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i32 = (int32_t) ldl((void *)A0);
    ST0 = (CPU86_LDouble)FP_CONVERT.i32;
#else
    ST0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
#endif
}

void OPPROTO op_fildll_ST0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.i64 = (int64_t) ldq((void *)A0);
    ST0 = (CPU86_LDouble)FP_CONVERT.i64;
#else
    ST0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
#endif
}

#endif

/* fp store */

void OPPROTO op_fsts_ST0_A0(void)
{
#ifdef USE_FP_CONVERT
    FP_CONVERT.d = ST0;
    stfl((void *)A0, FP_CONVERT.f);
#else
    stfl((void *)A0, (float)ST0);
#endif
}

void OPPROTO op_fstl_ST0_A0(void)
{
    stfq((void *)A0, (double)ST0);
}

#ifdef USE_X86LDOUBLE
void OPPROTO op_fstt_ST0_A0(void)
{
    *(long double *)A0 = ST0;
}
#else
void helper_fstt_ST0_A0(void)
{
    CPU86_LDoubleU temp;
    int e;
    temp.d = ST0;
    /* mantissa */
    stq((void *)A0, (MANTD(temp) << 11) | (1LL << 63));
    /* exponent + sign */
    e = EXPD(temp) - EXPBIAS + 16383;
    e |= SIGND(temp) >> 16;
    stw((uint8_t *)A0 + 8, e);
}

void OPPROTO op_fstt_ST0_A0(void)
{
    helper_fstt_ST0_A0();
}
#endif

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

void OPPROTO op_fbld_ST0_A0(void)
{
    helper_fbld_ST0_A0();
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

void OPPROTO op_fbst_ST0_A0(void)
{
    helper_fbst_ST0_A0();
}

/* FPU move */

static inline void fpush(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(void)
{
    env->fptags[env->fpstt] = 1; /* invvalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

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

void OPPROTO op_fxam_ST0(void)
{
    helper_fxam_ST0();
}

void OPPROTO op_fld1_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[1];
}

void OPPROTO op_fldl2t_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[6];
}

void OPPROTO op_fldl2e_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[5];
}

void OPPROTO op_fldpi_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[2];
}

void OPPROTO op_fldlg2_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[3];
}

void OPPROTO op_fldln2_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[4];
}

void OPPROTO op_fldz_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[0];
}

void OPPROTO op_fldz_FT0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[0];
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
    ST0 = rint(ST0);
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

/* threading support */
void OPPROTO op_lock(void)
{
    cpu_lock();
}

void OPPROTO op_unlock(void)
{
    cpu_unlock();
}
