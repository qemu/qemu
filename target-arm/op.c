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

void OPPROTO op_addl_T0_T1_cc(void)
{
    unsigned int src1;
    src1 = T0;
    T0 += T1;
    env->NZF = T0;
    env->CF = T0 < src1;
    env->VF = (src1 ^ T1 ^ -1) & (src1 ^ T0);
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
