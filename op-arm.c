/*
 *  ARM micro operations
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
#include "exec-arm.h"

#define REGNAME r0
#define REG (env->regs[0])
#include "op-arm-template.h"

#define REGNAME r1
#define REG (env->regs[1])
#include "op-arm-template.h"

#define REGNAME r2
#define REG (env->regs[2])
#include "op-arm-template.h"

#define REGNAME r3
#define REG (env->regs[3])
#include "op-arm-template.h"

#define REGNAME r4
#define REG (env->regs[4])
#include "op-arm-template.h"

#define REGNAME r5
#define REG (env->regs[5])
#include "op-arm-template.h"

#define REGNAME r6
#define REG (env->regs[6])
#include "op-arm-template.h"

#define REGNAME r7
#define REG (env->regs[7])
#include "op-arm-template.h"

#define REGNAME r8
#define REG (env->regs[8])
#include "op-arm-template.h"

#define REGNAME r9
#define REG (env->regs[9])
#include "op-arm-template.h"

#define REGNAME r10
#define REG (env->regs[10])
#include "op-arm-template.h"

#define REGNAME r11
#define REG (env->regs[11])
#include "op-arm-template.h"

#define REGNAME r12
#define REG (env->regs[12])
#include "op-arm-template.h"

#define REGNAME r13
#define REG (env->regs[13])
#include "op-arm-template.h"

#define REGNAME r14
#define REG (env->regs[14])
#include "op-arm-template.h"

#define REGNAME r15
#define REG (env->regs[15])
#include "op-arm-template.h"

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
        env->CF = src1 >= T1;                   \
    } else {                                    \
        T0 = T0 - T1;                           \
        env->CF = src1 > T1;                    \
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
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_ne(void)
{
    if (env->NZF != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_cs(void)
{
    if (env->CF != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_cc(void)
{
    if (env->CF == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_mi(void)
{
    if ((env->NZF & 0x80000000) != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_pl(void)
{
    if ((env->NZF & 0x80000000) == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_vs(void)
{
    if ((env->VF & 0x80000000) != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_vc(void)
{
    if ((env->VF & 0x80000000) == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_hi(void)
{
    if (env->CF != 0 && env->NZF != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_ls(void)
{
    if (env->CF == 0 || env->NZF == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_ge(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_lt(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_gt(void)
{
    if (env->NZF != 0 && ((env->VF ^ env->NZF) & 0x80000000) == 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_test_le(void)
{
    if (env->NZF == 0 || ((env->VF ^ env->NZF) & 0x80000000) != 0)
        JUMP_TB(PARAM1, 0, PARAM2);
    FORCE_RET();
}

void OPPROTO op_jmp(void)
{
    JUMP_TB(PARAM1, 1, PARAM2);
}

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

void OPPROTO op_movl_T0_psr(void)
{
    T0 = compute_cpsr();
}

/* NOTE: N = 1 and Z = 1 cannot be stored currently */
void OPPROTO op_movl_psr_T0(void)
{
    unsigned int psr;
    psr = T0;
    env->CF = (psr >> 29) & 1;
    env->NZF = (psr & 0xc0000000) ^ 0x40000000;
    env->VF = (psr << 3) & 0x80000000;
    /* for user mode we do not update other state info */
}

void OPPROTO op_mul_T0_T1(void)
{
    T0 = T0 * T1;
}

/* 64 bit unsigned mul */
void OPPROTO op_mull_T0_T1(void)
{
    uint64_t res;
    res = T0 * T1;
    T1 = res >> 32;
    T0 = res;
}

/* 64 bit signed mul */
void OPPROTO op_imull_T0_T1(void)
{
    uint64_t res;
    res = (int32_t)T0 * (int32_t)T1;
    T1 = res >> 32;
    T0 = res;
}

void OPPROTO op_addq_T0_T1(void)
{
    uint64_t res;
    res = ((uint64_t)T1 << 32) | T0;
    res += ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
    T1 = res >> 32;
    T0 = res;
}

void OPPROTO op_logicq_cc(void)
{
    env->NZF = (T1 & 0x80000000) | ((T0 | T1) != 0);
}

/* memory access */

void OPPROTO op_ldub_T0_T1(void)
{
    T0 = ldub((void *)T1);
}

void OPPROTO op_ldsb_T0_T1(void)
{
    T0 = ldsb((void *)T1);
}

void OPPROTO op_lduw_T0_T1(void)
{
    T0 = lduw((void *)T1);
}

void OPPROTO op_ldsw_T0_T1(void)
{
    T0 = ldsw((void *)T1);
}

void OPPROTO op_ldl_T0_T1(void)
{
    T0 = ldl((void *)T1);
}

void OPPROTO op_stb_T0_T1(void)
{
    stb((void *)T1, T0);
}

void OPPROTO op_stw_T0_T1(void)
{
    stw((void *)T1, T0);
}

void OPPROTO op_stl_T0_T1(void)
{
    stl((void *)T1, T0);
}

void OPPROTO op_swpb_T0_T1(void)
{
    int tmp;

    cpu_lock();
    tmp = ldub((void *)T1);
    stb((void *)T1, T0);
    T0 = tmp;
    cpu_unlock();
}

void OPPROTO op_swpl_T0_T1(void)
{
    int tmp;

    cpu_lock();
    tmp = ldl((void *)T1);
    stl((void *)T1, T0);
    T0 = tmp;
    cpu_unlock();
}

/* shifts */

/* T1 based */
void OPPROTO op_shll_T1_im(void)
{
    T1 = T1 << PARAM1;
}

void OPPROTO op_shrl_T1_im(void)
{
    T1 = (uint32_t)T1 >> PARAM1;
}

void OPPROTO op_sarl_T1_im(void)
{
    T1 = (int32_t)T1 >> PARAM1;
}

void OPPROTO op_rorl_T1_im(void)
{
    int shift;
    shift = PARAM1;
    T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
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

void OPPROTO op_sarl_T1_im_cc(void)
{
    env->CF = (T1 >> (PARAM1 - 1)) & 1;
    T1 = (int32_t)T1 >> PARAM1;
}

void OPPROTO op_rorl_T1_im_cc(void)
{
    int shift;
    shift = PARAM1;
    env->CF = (T1 >> (shift - 1)) & 1;
    T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
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

void OPPROTO op_sarl_T2_im(void)
{
    T2 = (int32_t)T2 >> PARAM1;
}

void OPPROTO op_rorl_T2_im(void)
{
    int shift;
    shift = PARAM1;
    T2 = ((uint32_t)T2 >> shift) | (T2 << (32 - shift));
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
    } else {
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

