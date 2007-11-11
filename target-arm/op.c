/*
 *  ARM micro operations
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec.h"

#define REGNAME r0
#define REG (env->regs[0])
#include "op_template.h"

#define REGNAME r1
#define REG (env->regs[1])
#include "op_template.h"

#define REGNAME r2
#define REG (env->regs[2])
#include "op_template.h"

#define REGNAME r3
#define REG (env->regs[3])
#include "op_template.h"

#define REGNAME r4
#define REG (env->regs[4])
#include "op_template.h"

#define REGNAME r5
#define REG (env->regs[5])
#include "op_template.h"

#define REGNAME r6
#define REG (env->regs[6])
#include "op_template.h"

#define REGNAME r7
#define REG (env->regs[7])
#include "op_template.h"

#define REGNAME r8
#define REG (env->regs[8])
#include "op_template.h"

#define REGNAME r9
#define REG (env->regs[9])
#include "op_template.h"

#define REGNAME r10
#define REG (env->regs[10])
#include "op_template.h"

#define REGNAME r11
#define REG (env->regs[11])
#include "op_template.h"

#define REGNAME r12
#define REG (env->regs[12])
#include "op_template.h"

#define REGNAME r13
#define REG (env->regs[13])
#include "op_template.h"

#define REGNAME r14
#define REG (env->regs[14])
#include "op_template.h"

#define REGNAME r15
#define REG (env->regs[15])
#define SET_REG(x) REG = x & ~(uint32_t)1
#include "op_template.h"

void OPPROTO op_bx_T0(void)
{
  env->regs[15] = T0 & ~(uint32_t)1;
  env->thumb = (T0 & 1) != 0;
}

void OPPROTO op_movl_T0_0(void)
{
    T0 = 0;
}

void OPPROTO op_movl_T0_im(void)
{
    T0 = PARAM1;
}

void OPPROTO op_movl_T1_im(void)
{
    T1 = PARAM1;
}

void OPPROTO op_mov_CF_T1(void)
{
    env->CF = ((uint32_t)T1) >> 31;
}

void OPPROTO op_movl_T2_im(void)
{
    T2 = PARAM1;
}

void OPPROTO op_addl_T1_im(void)
{
    T1 += PARAM1;
}

void OPPROTO op_addl_T1_T2(void)
{
    T1 += T2;
}

void OPPROTO op_subl_T1_T2(void)
{
    T1 -= T2;
}

void OPPROTO op_addl_T0_T1(void)
{
    T0 += T1;
}

void OPPROTO op_addl_T0_T1_cc(void)
{
    unsigned int src1;
    src1 = T0;
    T0 += T1;
    env->NZF = T0;
    env->CF = T0 < src1;
    env->VF = (src1 ^ T1 ^ -1) & (src1 ^ T0);
}

void OPPROTO op_adcl_T0_T1(void)
{
    T0 += T1 + env->CF;
}

void OPPROTO op_adcl_T0_T1_cc(void)
{
    unsigned int src1;
    src1 = T0;
    if (!env->CF) {
        T0 += T1;
        env->CF = T0 < src1;
    } else {
        T0 += T1 + 1;
        env->CF = T0 <= src1;
    }
    env->VF = (src1 ^ T1 ^ -1) & (src1 ^ T0);
    env->NZF = T0;
    FORCE_RET();
}

#define OPSUB(sub, sbc, res, T0, T1)            \
                                                \
void OPPROTO op_ ## sub ## l_T0_T1(void)        \
{                                               \
    res = T0 - T1;                              \
}                                               \
                                                \
void OPPROTO op_ ## sub ## l_T0_T1_cc(void)     \
{                                               \
    unsigned int src1;                          \
    src1 = T0;                                  \
    T0 -= T1;                                   \
    env->NZF = T0;                              \
    env->CF = src1 >= T1;                       \
    env->VF = (src1 ^ T1) & (src1 ^ T0);        \
    res = T0;                                   \
}                                               \
                                                \
void OPPROTO op_ ## sbc ## l_T0_T1(void)        \
{                                               \
    res = T0 - T1 + env->CF - 1;                \
}                                               \
                                                \
void OPPROTO op_ ## sbc ## l_T0_T1_cc(void)     \
{                                               \
    unsigned int src1;                          \
    src1 = T0;                                  \
    if (!env->CF) {                             \
        T0 = T0 - T1 - 1;                       \
        env->CF = src1 > T1;                    \
    } else {                                    \
        T0 = T0 - T1;                           \
        env->CF = src1 >= T1;                   \
    }                                           \
    env->VF = (src1 ^ T1) & (src1 ^ T0);        \
    env->NZF = T0;                              \
    res = T0;                                   \
    FORCE_RET();                                \
}

OPSUB(sub, sbc, T0, T0, T1)

OPSUB(rsb, rsc, T0, T1, T0)

void OPPROTO op_andl_T0_T1(void)
{
    T0 &= T1;
}

void OPPROTO op_xorl_T0_T1(void)
{
    T0 ^= T1;
}

void OPPROTO op_orl_T0_T1(void)
{
    T0 |= T1;
}

void OPPROTO op_bicl_T0_T1(void)
{
    T0 &= ~T1;
}

void OPPROTO op_notl_T0(void)
{
    T0 = ~T0;
}

void OPPROTO op_notl_T1(void)
{
    T1 = ~T1;
}

void OPPROTO op_logic_T0_cc(void)
{
    env->NZF = T0;
}

void OPPROTO op_logic_T1_cc(void)
{
    env->NZF = T1;
}

#define EIP (env->regs[15])

void OPPROTO op_test_eq(void)
{
    if (env->NZF == 0)
        GOTO_LABEL_PARAM(1);;
    FORCE_RET();
}

void OPPROTO op_test_ne(void)
{
    if (env->NZF != 0)
        GOTO_LABEL_PARAM(1);;
    FORCE_RET();
}

void OPPROTO op_test_cs(void)
{
    if (env->CF != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_cc(void)
{
    if (env->CF == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_mi(void)
{
    if ((env->NZF & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_pl(void)
{
    if ((env->NZF & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_vs(void)
{
    if ((env->VF & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_vc(void)
{
    if ((env->VF & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_hi(void)
{
    if (env->CF != 0 && env->NZF != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_ls(void)
{
    if (env->CF == 0 || env->NZF == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_ge(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_lt(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_gt(void)
{
    if (env->NZF != 0 && ((env->VF ^ env->NZF) & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_le(void)
{
    if (env->NZF == 0 || ((env->VF ^ env->NZF) & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_T0(void)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}
void OPPROTO op_testn_T0(void)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

void OPPROTO op_movl_T0_cpsr(void)
{
    /* Execution state bits always read as zero.  */
    T0 = cpsr_read(env) & ~CPSR_EXEC;
    FORCE_RET();
}

void OPPROTO op_movl_T0_spsr(void)
{
    T0 = env->spsr;
}

void OPPROTO op_movl_spsr_T0(void)
{
    uint32_t mask = PARAM1;
    env->spsr = (env->spsr & ~mask) | (T0 & mask);
}

void OPPROTO op_movl_cpsr_T0(void)
{
    cpsr_write(env, T0, PARAM1);
    FORCE_RET();
}

void OPPROTO op_mul_T0_T1(void)
{
    T0 = T0 * T1;
}

/* 64 bit unsigned mul */
void OPPROTO op_mull_T0_T1(void)
{
    uint64_t res;
    res = (uint64_t)T0 * (uint64_t)T1;
    T1 = res >> 32;
    T0 = res;
}

/* 64 bit signed mul */
void OPPROTO op_imull_T0_T1(void)
{
    uint64_t res;
    res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
    T1 = res >> 32;
    T0 = res;
}

/* 48 bit signed mul, top 32 bits */
void OPPROTO op_imulw_T0_T1(void)
{
  uint64_t res;
  res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
  T0 = res >> 16;
}

void OPPROTO op_addq_T0_T1(void)
{
    uint64_t res;
    res = ((uint64_t)T1 << 32) | T0;
    res += ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
    T1 = res >> 32;
    T0 = res;
}

void OPPROTO op_addq_lo_T0_T1(void)
{
    uint64_t res;
    res = ((uint64_t)T1 << 32) | T0;
    res += (uint64_t)(env->regs[PARAM1]);
    T1 = res >> 32;
    T0 = res;
}

/* Dual 16-bit accumulate.  */
void OPPROTO op_addq_T0_T1_dual(void)
{
  uint64_t res;
  res = ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
  res += (int32_t)T0;
  res += (int32_t)T1;
  env->regs[PARAM1] = (uint32_t)res;
  env->regs[PARAM2] = res >> 32;
}

/* Dual 16-bit subtract accumulate.  */
void OPPROTO op_subq_T0_T1_dual(void)
{
  uint64_t res;
  res = ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
  res += (int32_t)T0;
  res -= (int32_t)T1;
  env->regs[PARAM1] = (uint32_t)res;
  env->regs[PARAM2] = res >> 32;
}

void OPPROTO op_logicq_cc(void)
{
    env->NZF = (T1 & 0x80000000) | ((T0 | T1) != 0);
}

/* memory access */

#define MEMSUFFIX _raw
#include "op_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"
#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

void OPPROTO op_clrex(void)
{
    cpu_lock();
    helper_clrex(env);
    cpu_unlock();
}

/* shifts */

/* Used by NEON.  */
void OPPROTO op_shll_T0_im(void)
{
    T1 = T1 << PARAM1;
}

/* T1 based */

void OPPROTO op_shll_T1_im(void)
{
    T1 = T1 << PARAM1;
}

void OPPROTO op_shrl_T1_im(void)
{
    T1 = (uint32_t)T1 >> PARAM1;
}

void OPPROTO op_shrl_T1_0(void)
{
    T1 = 0;
}

void OPPROTO op_sarl_T1_im(void)
{
    T1 = (int32_t)T1 >> PARAM1;
}

void OPPROTO op_sarl_T1_0(void)
{
    T1 = (int32_t)T1 >> 31;
}

void OPPROTO op_rorl_T1_im(void)
{
    int shift;
    shift = PARAM1;
    T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
}

void OPPROTO op_rrxl_T1(void)
{
    T1 = ((uint32_t)T1 >> 1) | ((uint32_t)env->CF << 31);
}

/* T1 based, set C flag */
void OPPROTO op_shll_T1_im_cc(void)
{
    env->CF = (T1 >> (32 - PARAM1)) & 1;
    T1 = T1 << PARAM1;
}

void OPPROTO op_shrl_T1_im_cc(void)
{
    env->CF = (T1 >> (PARAM1 - 1)) & 1;
    T1 = (uint32_t)T1 >> PARAM1;
}

void OPPROTO op_shrl_T1_0_cc(void)
{
    env->CF = (T1 >> 31) & 1;
    T1 = 0;
}

void OPPROTO op_sarl_T1_im_cc(void)
{
    env->CF = (T1 >> (PARAM1 - 1)) & 1;
    T1 = (int32_t)T1 >> PARAM1;
}

void OPPROTO op_sarl_T1_0_cc(void)
{
    env->CF = (T1 >> 31) & 1;
    T1 = (int32_t)T1 >> 31;
}

void OPPROTO op_rorl_T1_im_cc(void)
{
    int shift;
    shift = PARAM1;
    env->CF = (T1 >> (shift - 1)) & 1;
    T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
}

void OPPROTO op_rrxl_T1_cc(void)
{
    uint32_t c;
    c = T1 & 1;
    T1 = ((uint32_t)T1 >> 1) | ((uint32_t)env->CF << 31);
    env->CF = c;
}

/* T2 based */
void OPPROTO op_shll_T2_im(void)
{
    T2 = T2 << PARAM1;
}

void OPPROTO op_shrl_T2_im(void)
{
    T2 = (uint32_t)T2 >> PARAM1;
}

void OPPROTO op_shrl_T2_0(void)
{
    T2 = 0;
}

void OPPROTO op_sarl_T2_im(void)
{
    T2 = (int32_t)T2 >> PARAM1;
}

void OPPROTO op_sarl_T2_0(void)
{
    T2 = (int32_t)T2 >> 31;
}

void OPPROTO op_rorl_T2_im(void)
{
    int shift;
    shift = PARAM1;
    T2 = ((uint32_t)T2 >> shift) | (T2 << (32 - shift));
}

void OPPROTO op_rrxl_T2(void)
{
    T2 = ((uint32_t)T2 >> 1) | ((uint32_t)env->CF << 31);
}

/* T1 based, use T0 as shift count */

void OPPROTO op_shll_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        T1 = 0;
    else
        T1 = T1 << shift;
    FORCE_RET();
}

void OPPROTO op_shrl_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        T1 = 0;
    else
        T1 = (uint32_t)T1 >> shift;
    FORCE_RET();
}

void OPPROTO op_sarl_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        shift = 31;
    T1 = (int32_t)T1 >> shift;
}

void OPPROTO op_rorl_T1_T0(void)
{
    int shift;
    shift = T0 & 0x1f;
    if (shift) {
        T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
    }
    FORCE_RET();
}

/* T1 based, use T0 as shift count and compute CF */

void OPPROTO op_shll_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = T1 & 1;
        else
            env->CF = 0;
        T1 = 0;
    } else if (shift != 0) {
        env->CF = (T1 >> (32 - shift)) & 1;
        T1 = T1 << shift;
    }
    FORCE_RET();
}

void OPPROTO op_shrl_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = (T1 >> 31) & 1;
        else
            env->CF = 0;
        T1 = 0;
    } else if (shift != 0) {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = (uint32_t)T1 >> shift;
    }
    FORCE_RET();
}

void OPPROTO op_sarl_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        env->CF = (T1 >> 31) & 1;
        T1 = (int32_t)T1 >> 31;
    } else if (shift != 0) {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = (int32_t)T1 >> shift;
    }
    FORCE_RET();
}

void OPPROTO op_rorl_T1_T0_cc(void)
{
    int shift1, shift;
    shift1 = T0 & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0)
            env->CF = (T1 >> 31) & 1;
    } else {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
    }
    FORCE_RET();
}

/* misc */
void OPPROTO op_clz_T0(void)
{
    int count;
    for (count = 32; T0 > 0; count--)
        T0 = T0 >> 1;
    T0 = count;
    FORCE_RET();
}

void OPPROTO op_sarl_T0_im(void)
{
    T0 = (int32_t)T0 >> PARAM1;
}

/* Sign/zero extend */
void OPPROTO op_sxth_T0(void)
{
  T0 = (int16_t)T0;
}

void OPPROTO op_sxth_T1(void)
{
  T1 = (int16_t)T1;
}

void OPPROTO op_sxtb_T1(void)
{
    T1 = (int8_t)T1;
}

void OPPROTO op_uxtb_T1(void)
{
    T1 = (uint8_t)T1;
}

void OPPROTO op_uxth_T1(void)
{
    T1 = (uint16_t)T1;
}

void OPPROTO op_sxtb16_T1(void)
{
    uint32_t res;
    res = (uint16_t)(int8_t)T1;
    res |= (uint32_t)(int8_t)(T1 >> 16) << 16;
    T1 = res;
}

void OPPROTO op_uxtb16_T1(void)
{
    uint32_t res;
    res = (uint16_t)(uint8_t)T1;
    res |= (uint32_t)(uint8_t)(T1 >> 16) << 16;
    T1 = res;
}

#define SIGNBIT (uint32_t)0x80000000
/* saturating arithmetic  */
void OPPROTO op_addl_T0_T1_setq(void)
{
  uint32_t res;

  res = T0 + T1;
  if (((res ^ T0) & SIGNBIT) && !((T0 ^ T1) & SIGNBIT))
      env->QF = 1;

  T0 = res;
  FORCE_RET();
}

void OPPROTO op_addl_T0_T1_saturate(void)
{
  uint32_t res;

  res = T0 + T1;
  if (((res ^ T0) & SIGNBIT) && !((T0 ^ T1) & SIGNBIT)) {
      env->QF = 1;
      if (T0 & SIGNBIT)
          T0 = 0x80000000;
      else
          T0 = 0x7fffffff;
  }
  else
    T0 = res;

  FORCE_RET();
}

void OPPROTO op_subl_T0_T1_saturate(void)
{
  uint32_t res;

  res = T0 - T1;
  if (((res ^ T0) & SIGNBIT) && ((T0 ^ T1) & SIGNBIT)) {
      env->QF = 1;
      if (T0 & SIGNBIT)
          T0 = 0x80000000;
      else
          T0 = 0x7fffffff;
  }
  else
    T0 = res;

  FORCE_RET();
}

void OPPROTO op_double_T1_saturate(void)
{
  int32_t val;

  val = T1;
  if (val >= 0x40000000) {
      T1 = 0x7fffffff;
      env->QF = 1;
  } else if (val <= (int32_t)0xc0000000) {
      T1 = 0x80000000;
      env->QF = 1;
  } else {
      T1 = val << 1;
  }
  FORCE_RET();
}

/* Unsigned saturating arithmetic for NEON.  */
void OPPROTO op_addl_T0_T1_usaturate(void)
{
  uint32_t res;

  res = T0 + T1;
  if (res < T0) {
      env->QF = 1;
      T0 = 0xffffffff;
  } else {
      T0 = res;
  }

  FORCE_RET();
}

void OPPROTO op_subl_T0_T1_usaturate(void)
{
  uint32_t res;

  res = T0 - T1;
  if (res > T0) {
      env->QF = 1;
      T0 = 0;
  } else {
      T0 = res;
  }

  FORCE_RET();
}

/* Thumb shift by immediate */
void OPPROTO op_shll_T0_im_thumb_cc(void)
{
    int shift;
    shift = PARAM1;
    if (shift != 0) {
	env->CF = (T0 >> (32 - shift)) & 1;
	T0 = T0 << shift;
    }
    env->NZF = T0;
    FORCE_RET();
}

void OPPROTO op_shll_T0_im_thumb(void)
{
    T0 = T0 << PARAM1;
    FORCE_RET();
}

void OPPROTO op_shrl_T0_im_thumb_cc(void)
{
    int shift;

    shift = PARAM1;
    if (shift == 0) {
	env->CF = ((uint32_t)T0) >> 31;
	T0 = 0;
    } else {
	env->CF = (T0 >> (shift - 1)) & 1;
	T0 = T0 >> shift;
    }
    env->NZF = T0;
    FORCE_RET();
}

void OPPROTO op_shrl_T0_im_thumb(void)
{
    int shift;

    shift = PARAM1;
    if (shift == 0) {
	T0 = 0;
    } else {
	T0 = T0 >> shift;
    }
    FORCE_RET();
}

void OPPROTO op_sarl_T0_im_thumb_cc(void)
{
    int shift;

    shift = PARAM1;
    if (shift == 0) {
	T0 = ((int32_t)T0) >> 31;
	env->CF = T0 & 1;
    } else {
	env->CF = (T0 >> (shift - 1)) & 1;
	T0 = ((int32_t)T0) >> shift;
    }
    env->NZF = T0;
    FORCE_RET();
}

void OPPROTO op_sarl_T0_im_thumb(void)
{
    int shift;

    shift = PARAM1;
    if (shift == 0) {
	env->CF = T0 & 1;
    } else {
	T0 = ((int32_t)T0) >> shift;
    }
    FORCE_RET();
}

/* exceptions */

void OPPROTO op_swi(void)
{
    env->exception_index = EXCP_SWI;
    cpu_loop_exit();
}

void OPPROTO op_undef_insn(void)
{
    env->exception_index = EXCP_UDEF;
    cpu_loop_exit();
}

void OPPROTO op_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void OPPROTO op_wfi(void)
{
    env->exception_index = EXCP_HLT;
    env->halted = 1;
    cpu_loop_exit();
}

void OPPROTO op_bkpt(void)
{
    env->exception_index = EXCP_BKPT;
    cpu_loop_exit();
}

void OPPROTO op_exception_exit(void)
{
    env->exception_index = EXCP_EXCEPTION_EXIT;
    cpu_loop_exit();
}

/* VFP support.  We follow the convention used for VFP instrunctions:
   Single precition routines have a "s" suffix, double precision a
   "d" suffix.  */

#define VFP_OP(name, p) void OPPROTO op_vfp_##name##p(void)

#define VFP_BINOP(name) \
VFP_OP(name, s)             \
{                           \
    FT0s = float32_ ## name (FT0s, FT1s, &env->vfp.fp_status);    \
}                           \
VFP_OP(name, d)             \
{                           \
    FT0d = float64_ ## name (FT0d, FT1d, &env->vfp.fp_status);    \
}
VFP_BINOP(add)
VFP_BINOP(sub)
VFP_BINOP(mul)
VFP_BINOP(div)
#undef VFP_BINOP

#define VFP_HELPER(name)  \
VFP_OP(name, s)           \
{                         \
    do_vfp_##name##s();    \
}                         \
VFP_OP(name, d)           \
{                         \
    do_vfp_##name##d();    \
}
VFP_HELPER(abs)
VFP_HELPER(sqrt)
VFP_HELPER(cmp)
VFP_HELPER(cmpe)
#undef VFP_HELPER

/* XXX: Will this do the right thing for NANs.  Should invert the signbit
   without looking at the rest of the value.  */
VFP_OP(neg, s)
{
    FT0s = float32_chs(FT0s);
}

VFP_OP(neg, d)
{
    FT0d = float64_chs(FT0d);
}

VFP_OP(F1_ld0, s)
{
    union {
        uint32_t i;
        float32 s;
    } v;
    v.i = 0;
    FT1s = v.s;
}

VFP_OP(F1_ld0, d)
{
    union {
        uint64_t i;
        float64 d;
    } v;
    v.i = 0;
    FT1d = v.d;
}

/* Helper routines to perform bitwise copies between float and int.  */
static inline float32 vfp_itos(uint32_t i)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.i = i;
    return v.s;
}

static inline uint32_t vfp_stoi(float32 s)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.s = s;
    return v.i;
}

static inline float64 vfp_itod(uint64_t i)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.i = i;
    return v.d;
}

static inline uint64_t vfp_dtoi(float64 d)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.d = d;
    return v.i;
}

/* Integer to float conversion.  */
VFP_OP(uito, s)
{
    FT0s = uint32_to_float32(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(uito, d)
{
    FT0d = uint32_to_float64(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(sito, s)
{
    FT0s = int32_to_float32(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(sito, d)
{
    FT0d = int32_to_float64(vfp_stoi(FT0s), &env->vfp.fp_status);
}

/* Float to integer conversion.  */
VFP_OP(toui, s)
{
    FT0s = vfp_itos(float32_to_uint32(FT0s, &env->vfp.fp_status));
}

VFP_OP(toui, d)
{
    FT0s = vfp_itos(float64_to_uint32(FT0d, &env->vfp.fp_status));
}

VFP_OP(tosi, s)
{
    FT0s = vfp_itos(float32_to_int32(FT0s, &env->vfp.fp_status));
}

VFP_OP(tosi, d)
{
    FT0s = vfp_itos(float64_to_int32(FT0d, &env->vfp.fp_status));
}

/* TODO: Set rounding mode properly.  */
VFP_OP(touiz, s)
{
    FT0s = vfp_itos(float32_to_uint32_round_to_zero(FT0s, &env->vfp.fp_status));
}

VFP_OP(touiz, d)
{
    FT0s = vfp_itos(float64_to_uint32_round_to_zero(FT0d, &env->vfp.fp_status));
}

VFP_OP(tosiz, s)
{
    FT0s = vfp_itos(float32_to_int32_round_to_zero(FT0s, &env->vfp.fp_status));
}

VFP_OP(tosiz, d)
{
    FT0s = vfp_itos(float64_to_int32_round_to_zero(FT0d, &env->vfp.fp_status));
}

/* floating point conversion */
VFP_OP(fcvtd, s)
{
    FT0d = float32_to_float64(FT0s, &env->vfp.fp_status);
}

VFP_OP(fcvts, d)
{
    FT0s = float64_to_float32(FT0d, &env->vfp.fp_status);
}

/* VFP3 fixed point conversion.  */
#define VFP_CONV_FIX(name, p, ftype, itype, sign) \
VFP_OP(name##to, p) \
{ \
    ftype tmp; \
    tmp = sign##int32_to_##ftype ((itype)vfp_##p##toi(FT0##p), \
                                  &env->vfp.fp_status); \
    FT0##p = ftype##_scalbn(tmp, PARAM1, &env->vfp.fp_status); \
} \
VFP_OP(to##name, p) \
{ \
    ftype tmp; \
    tmp = ftype##_scalbn(FT0##p, PARAM1, &env->vfp.fp_status); \
    FT0##p = vfp_ito##p((itype)ftype##_to_##sign##int32_round_to_zero(tmp, \
            &env->vfp.fp_status)); \
}

VFP_CONV_FIX(sh, d, float64, int16, )
VFP_CONV_FIX(sl, d, float64, int32, )
VFP_CONV_FIX(uh, d, float64, uint16, u)
VFP_CONV_FIX(ul, d, float64, uint32, u)
VFP_CONV_FIX(sh, s, float32, int16, )
VFP_CONV_FIX(sl, s, float32, int32, )
VFP_CONV_FIX(uh, s, float32, uint16, u)
VFP_CONV_FIX(ul, s, float32, uint32, u)

/* Get and Put values from registers.  */
VFP_OP(getreg_F0, d)
{
  FT0d = *(float64 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F0, s)
{
  FT0s = *(float32 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F1, d)
{
  FT1d = *(float64 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F1, s)
{
  FT1s = *(float32 *)((char *) env + PARAM1);
}

VFP_OP(setreg_F0, d)
{
  *(float64 *)((char *) env + PARAM1) = FT0d;
}

VFP_OP(setreg_F0, s)
{
  *(float32 *)((char *) env + PARAM1) = FT0s;
}

void OPPROTO op_vfp_movl_T0_fpscr(void)
{
    do_vfp_get_fpscr ();
}

void OPPROTO op_vfp_movl_T0_fpscr_flags(void)
{
    T0 = env->vfp.xregs[ARM_VFP_FPSCR] & (0xf << 28);
}

void OPPROTO op_vfp_movl_fpscr_T0(void)
{
    do_vfp_set_fpscr();
}

void OPPROTO op_vfp_movl_T0_xreg(void)
{
    T0 = env->vfp.xregs[PARAM1];
}

void OPPROTO op_vfp_movl_xreg_T0(void)
{
    env->vfp.xregs[PARAM1] = T0;
}

/* Move between FT0s to T0  */
void OPPROTO op_vfp_mrs(void)
{
    T0 = vfp_stoi(FT0s);
}

void OPPROTO op_vfp_msr(void)
{
    FT0s = vfp_itos(T0);
}

/* Move between FT0d and {T0,T1} */
void OPPROTO op_vfp_mrrd(void)
{
    CPU_DoubleU u;

    u.d = FT0d;
    T0 = u.l.lower;
    T1 = u.l.upper;
}

void OPPROTO op_vfp_mdrr(void)
{
    CPU_DoubleU u;

    u.l.lower = T0;
    u.l.upper = T1;
    FT0d = u.d;
}

/* Load immediate.  PARAM1 is the 32 most significant bits of the value.  */
void OPPROTO op_vfp_fconstd(void)
{
    CPU_DoubleU u;
    u.l.upper = PARAM1;
    u.l.lower = 0;
    FT0d = u.d;
}

void OPPROTO op_vfp_fconsts(void)
{
    FT0s = vfp_itos(PARAM1);
}

/* Copy the most significant bit of T0 to all bits of T1.  */
void OPPROTO op_signbit_T1_T0(void)
{
    T1 = (int32_t)T0 >> 31;
}

void OPPROTO op_movl_cp_T0(void)
{
    helper_set_cp(env, PARAM1, T0);
    FORCE_RET();
}

void OPPROTO op_movl_T0_cp(void)
{
    T0 = helper_get_cp(env, PARAM1);
    FORCE_RET();
}

void OPPROTO op_movl_cp15_T0(void)
{
    helper_set_cp15(env, PARAM1, T0);
    FORCE_RET();
}

void OPPROTO op_movl_T0_cp15(void)
{
    T0 = helper_get_cp15(env, PARAM1);
    FORCE_RET();
}

/* Access to user mode registers from privileged modes.  */
void OPPROTO op_movl_T0_user(void)
{
    int regno = PARAM1;
    if (regno == 13) {
        T0 = env->banked_r13[0];
    } else if (regno == 14) {
        T0 = env->banked_r14[0];
    } else if ((env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        T0 = env->usr_regs[regno - 8];
    } else {
        T0 = env->regs[regno];
    }
    FORCE_RET();
}


void OPPROTO op_movl_user_T0(void)
{
    int regno = PARAM1;
    if (regno == 13) {
        env->banked_r13[0] = T0;
    } else if (regno == 14) {
        env->banked_r14[0] = T0;
    } else if ((env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        env->usr_regs[regno - 8] = T0;
    } else {
        env->regs[regno] = T0;
    }
    FORCE_RET();
}

void OPPROTO op_movl_T0_T1(void)
{
    T0 = T1;
}

void OPPROTO op_movl_T0_T2(void)
{
    T0 = T2;
}

void OPPROTO op_movl_T1_T0(void)
{
    T1 = T0;
}

void OPPROTO op_movl_T1_T2(void)
{
    T1 = T2;
}

void OPPROTO op_movl_T2_T0(void)
{
    T2 = T0;
}

/* ARMv6 Media instructions.  */

/* Note that signed overflow is undefined in C.  The following routines are
   careful to use unsigned types where modulo arithmetic is required.
   Failure to do so _will_ break on newer gcc.  */

/* Signed saturating arithmetic.  */

/* Perform 16-bit signed satruating addition.  */
static inline uint16_t add16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a + b;
    if (((res ^ a) & 0x8000) && !((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed satruating addition.  */
static inline uint8_t add8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a + b;
    if (((res ^ a) & 0x80) && !((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

/* Perform 16-bit signed satruating subtraction.  */
static inline uint16_t sub16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a - b;
    if (((res ^ a) & 0x8000) && ((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed satruating subtraction.  */
static inline uint8_t sub8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a - b;
    if (((res ^ a) & 0x80) && ((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

#define ADD16(a, b, n) RESULT(add16_sat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_sat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_sat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_sat(a, b), n, 8);
#define PFX q

#include "op_addsub.h"

/* Unsigned saturating arithmetic.  */
static inline uint16_t add16_usat(uint16_t a, uint8_t b)
{
    uint16_t res;
    res = a + b;
    if (res < a)
        res = 0xffff;
    return res;
}

static inline uint16_t sub16_usat(uint16_t a, uint8_t b)
{
    if (a < b)
        return a - b;
    else
        return 0;
}

static inline uint8_t add8_usat(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a + b;
    if (res < a)
        res = 0xff;
    return res;
}

static inline uint8_t sub8_usat(uint8_t a, uint8_t b)
{
    if (a < b)
        return a - b;
    else
        return 0;
}

#define ADD16(a, b, n) RESULT(add16_usat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_usat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_usat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_usat(a, b), n, 8);
#define PFX uq

#include "op_addsub.h"

/* Signed modulo arithmetic.  */
#define SARITH16(a, b, n, op) do { \
    int32_t sum; \
    sum = (int16_t)((uint16_t)(a) op (uint16_t)(b)); \
    RESULT(sum, n, 16); \
    if (sum >= 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SARITH8(a, b, n, op) do { \
    int32_t sum; \
    sum = (int8_t)((uint8_t)(a) op (uint8_t)(b)); \
    RESULT(sum, n, 8); \
    if (sum >= 0) \
        ge |= 1 << n; \
    } while(0)


#define ADD16(a, b, n) SARITH16(a, b, n, +)
#define SUB16(a, b, n) SARITH16(a, b, n, -)
#define ADD8(a, b, n)  SARITH8(a, b, n, +)
#define SUB8(a, b, n)  SARITH8(a, b, n, -)
#define PFX s
#define ARITH_GE

#include "op_addsub.h"

/* Unsigned modulo arithmetic.  */
#define ADD16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define ADD8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SUB16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SUB8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define PFX u
#define ARITH_GE

#include "op_addsub.h"

/* Halved signed arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) + (int32_t)(int16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) - (int32_t)(int16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) + (int32_t)(int8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) - (int32_t)(int8_t)(b)) >> 1, n, 8)
#define PFX sh

#include "op_addsub.h"

/* Halved unsigned arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define PFX uh

#include "op_addsub.h"

void OPPROTO op_pkhtb_T0_T1(void)
{
    T0 = (T0 & 0xffff0000) | (T1 & 0xffff);
}

void OPPROTO op_pkhbt_T0_T1(void)
{
    T0 = (T0 & 0xffff) | (T1 & 0xffff0000);
}
void OPPROTO op_rev_T0(void)
{
    T0 =  ((T0 & 0xff000000) >> 24)
        | ((T0 & 0x00ff0000) >> 8)
        | ((T0 & 0x0000ff00) << 8)
        | ((T0 & 0x000000ff) << 24);
}

void OPPROTO op_revh_T0(void)
{
    T0 = (T0 >> 16) | (T0 << 16);
}

void OPPROTO op_rev16_T0(void)
{
    T0 =  ((T0 & 0xff000000) >> 8)
        | ((T0 & 0x00ff0000) << 8)
        | ((T0 & 0x0000ff00) >> 8)
        | ((T0 & 0x000000ff) << 8);
}

void OPPROTO op_revsh_T0(void)
{
    T0 = (int16_t)(  ((T0 & 0x0000ff00) >> 8)
                   | ((T0 & 0x000000ff) << 8));
}

void OPPROTO op_rbit_T0(void)
{
    T0 =  ((T0 & 0xff000000) >> 24)
        | ((T0 & 0x00ff0000) >> 8)
        | ((T0 & 0x0000ff00) << 8)
        | ((T0 & 0x000000ff) << 24);
    T0 =  ((T0 & 0xf0f0f0f0) >> 4)
        | ((T0 & 0x0f0f0f0f) << 4);
    T0 =  ((T0 & 0x88888888) >> 3)
        | ((T0 & 0x44444444) >> 1)
        | ((T0 & 0x22222222) << 1)
        | ((T0 & 0x11111111) << 3);
}

/* Swap low and high halfwords.  */
void OPPROTO op_swap_half_T1(void)
{
    T1 = (T1 >> 16) | (T1 << 16);
    FORCE_RET();
}

/* Dual 16-bit signed multiply.  */
void OPPROTO op_mul_dual_T0_T1(void)
{
    int32_t low;
    int32_t high;
    low = (int32_t)(int16_t)T0 * (int32_t)(int16_t)T1;
    high = (((int32_t)T0) >> 16) * (((int32_t)T1) >> 16);
    T0 = low;
    T1 = high;
}

void OPPROTO op_sel_T0_T1(void)
{
    uint32_t mask;
    uint32_t flags;

    flags = env->GE;
    mask = 0;
    if (flags & 1)
        mask |= 0xff;
    if (flags & 2)
        mask |= 0xff00;
    if (flags & 4)
        mask |= 0xff0000;
    if (flags & 8)
        mask |= 0xff000000;
    T0 = (T0 & mask) | (T1 & ~mask);
    FORCE_RET();
}

void OPPROTO op_roundqd_T0_T1(void)
{
    T0 = T1 + ((uint32_t)T0 >> 31);
}

/* Signed saturation.  */
static inline uint32_t do_ssat(int32_t val, int shift)
{
    int32_t top;
    uint32_t mask;

    shift = PARAM1;
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
static inline uint32_t do_usat(int32_t val, int shift)
{
    uint32_t max;

    shift = PARAM1;
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
void OPPROTO op_ssat_T1(void)
{
    T0 = do_ssat(T0, PARAM1);
    FORCE_RET();
}

/* Dual halfword signed saturate.  */
void OPPROTO op_ssat16_T1(void)
{
    uint32_t res;

    res = (uint16_t)do_ssat((int16_t)T0, PARAM1);
    res |= do_ssat(((int32_t)T0) >> 16, PARAM1) << 16;
    T0 = res;
    FORCE_RET();
}

/* Unsigned saturate.  */
void OPPROTO op_usat_T1(void)
{
    T0 = do_usat(T0, PARAM1);
    FORCE_RET();
}

/* Dual halfword unsigned saturate.  */
void OPPROTO op_usat16_T1(void)
{
    uint32_t res;

    res = (uint16_t)do_usat((int16_t)T0, PARAM1);
    res |= do_usat(((int32_t)T0) >> 16, PARAM1) << 16;
    T0 = res;
    FORCE_RET();
}

/* Dual 16-bit add.  */
void OPPROTO op_add16_T1_T2(void)
{
    uint32_t mask;
    mask = (T0 & T1) & 0x8000;
    T0 ^= ~0x8000;
    T1 ^= ~0x8000;
    T0 = (T0 + T1) ^ mask;
}

static inline uint8_t do_usad(uint8_t a, uint8_t b)
{
    if (a > b)
        return a - b;
    else
        return b - a;
}

/* Unsigned sum of absolute byte differences.  */
void OPPROTO op_usad8_T0_T1(void)
{
    uint32_t sum;
    sum = do_usad(T0, T1);
    sum += do_usad(T0 >> 8, T1 >> 8);
    sum += do_usad(T0 >> 16, T1 >>16);
    sum += do_usad(T0 >> 24, T1 >> 24);
    T0 = sum;
}

/* Thumb-2 instructions.  */

/* Insert T1 into T0.  Result goes in T1.  */
void OPPROTO op_bfi_T1_T0(void)
{
    int shift = PARAM1;
    uint32_t mask = PARAM2;
    uint32_t bits;

    bits = (T1 << shift) & mask;
    T1 = (T0 & ~mask) | bits;
}

/* Unsigned bitfield extract.  */
void OPPROTO op_ubfx_T1(void)
{
    uint32_t shift = PARAM1;
    uint32_t mask = PARAM2;

    T1 >>= shift;
    T1 &= mask;
}

/* Signed bitfield extract.  */
void OPPROTO op_sbfx_T1(void)
{
    uint32_t shift = PARAM1;
    uint32_t width = PARAM2;
    int32_t val;

    val = T1 << (32 - (shift + width));
    T1 = val >> (32 - width);
}

void OPPROTO op_movtop_T0_im(void)
{
    T0 = (T0 & 0xffff) | PARAM1;
}

/* Used by table branch instructions.  */
void OPPROTO op_jmp_T0_im(void)
{
    env->regs[15] = PARAM1 + (T0 << 1);
}

void OPPROTO op_set_condexec(void)
{
    env->condexec_bits = PARAM1;
}

void OPPROTO op_sdivl_T0_T1(void)
{
  int32_t num;
  int32_t den;
  num = T0;
  den = T1;
  if (den == 0)
    T0 = 0;
  else
    T0 = num / den;
  FORCE_RET();
}

void OPPROTO op_udivl_T0_T1(void)
{
  uint32_t num;
  uint32_t den;
  num = T0;
  den = T1;
  if (den == 0)
    T0 = 0;
  else
    T0 = num / den;
  FORCE_RET();
}

void OPPROTO op_movl_T1_r13_banked(void)
{
    T1 = helper_get_r13_banked(env, PARAM1);
}

void OPPROTO op_movl_r13_T1_banked(void)
{
    helper_set_r13_banked(env, PARAM1, T1);
}

void OPPROTO op_v7m_mrs_T0(void)
{
    T0 = helper_v7m_mrs(env, PARAM1);
}

void OPPROTO op_v7m_msr_T0(void)
{
    helper_v7m_msr(env, PARAM1, T0);
}

void OPPROTO op_movl_T0_sp(void)
{
    if (PARAM1 == env->v7m.current_sp)
        T0 = env->regs[13];
    else
        T0 = env->v7m.other_sp;
    FORCE_RET();
}

#include "op_neon.h"

/* iwMMXt support */
#include "op_iwmmxt.c"
