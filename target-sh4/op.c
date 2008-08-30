/*
 *  SH4 emulation
 *
 *  Copyright (c) 2005 Samuel Tardieu
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

static inline void set_t(void)
{
    env->sr |= SR_T;
}

static inline void clr_t(void)
{
    env->sr &= ~SR_T;
}

static inline void cond_t(int cond)
{
    if (cond)
	set_t();
    else
	clr_t();
}

void OPPROTO op_cmp_str_T0_T1(void)
{
    cond_t((T0 & 0x000000ff) == (T1 & 0x000000ff) ||
	   (T0 & 0x0000ff00) == (T1 & 0x0000ff00) ||
	   (T0 & 0x00ff0000) == (T1 & 0x00ff0000) ||
	   (T0 & 0xff000000) == (T1 & 0xff000000));
    RETURN();
}

void OPPROTO op_div0s_T0_T1(void)
{
    if (T1 & 0x80000000)
	env->sr |= SR_Q;
    else
	env->sr &= ~SR_Q;
    if (T0 & 0x80000000)
	env->sr |= SR_M;
    else
	env->sr &= ~SR_M;
    cond_t((T1 ^ T0) & 0x80000000);
    RETURN();
}

void OPPROTO op_div1_T0_T1(void)
{
    helper_div1_T0_T1();
    RETURN();
}

void OPPROTO op_shad_T0_T1(void)
{
    if ((T0 & 0x80000000) == 0)
	T1 <<= (T0 & 0x1f);
    else if ((T0 & 0x1f) == 0)
	T1 = (T1 & 0x80000000)? 0xffffffff : 0;
    else
	T1 = ((int32_t) T1) >> ((~T0 & 0x1f) + 1);
    RETURN();
}

void OPPROTO op_shld_T0_T1(void)
{
    if ((T0 & 0x80000000) == 0)
	T1 <<= (T0 & 0x1f);
    else if ((T0 & 0x1f) == 0)
	T1 = 0;
    else
	T1 = ((uint32_t) T1) >> ((~T0 & 0x1f) + 1);
    RETURN();
}

void OPPROTO op_rotcl_Rn(void)
{
    helper_rotcl(&env->gregs[PARAM1]);
    RETURN();
}

void OPPROTO op_rotcr_Rn(void)
{
    helper_rotcr(&env->gregs[PARAM1]);
    RETURN();
}

void OPPROTO op_rotl_Rn(void) 
{
    cond_t(env->gregs[PARAM1] & 0x80000000);
    env->gregs[PARAM1] = (env->gregs[PARAM1] << 1) | (env->sr & SR_T);
    RETURN();
}

void OPPROTO op_rotr_Rn(void)
{
    cond_t(env->gregs[PARAM1] & 1);
    env->gregs[PARAM1] = (env->gregs[PARAM1] >> 1) |
	((env->sr & SR_T) ? 0x80000000 : 0);
    RETURN();
}

void OPPROTO op_fmov_frN_FT0(void)
{
    FT0 = env->fregs[PARAM1];
    RETURN();
}

void OPPROTO op_fmov_drN_DT0(void)
{
    CPU_DoubleU d;

    d.l.upper = *(uint32_t *)&env->fregs[PARAM1];
    d.l.lower = *(uint32_t *)&env->fregs[PARAM1 + 1];
    DT0 = d.d;
    RETURN();
}

void OPPROTO op_fmov_frN_FT1(void)
{
    FT1 = env->fregs[PARAM1];
    RETURN();
}

void OPPROTO op_fmov_drN_DT1(void)
{
    CPU_DoubleU d;

    d.l.upper = *(uint32_t *)&env->fregs[PARAM1];
    d.l.lower = *(uint32_t *)&env->fregs[PARAM1 + 1];
    DT1 = d.d;
    RETURN();
}

void OPPROTO op_fmov_FT0_frN(void)
{
    env->fregs[PARAM1] = FT0;
    RETURN();
}

void OPPROTO op_fmov_DT0_drN(void)
{
    CPU_DoubleU d;

    d.d = DT0;
    *(uint32_t *)&env->fregs[PARAM1] = d.l.upper;
    *(uint32_t *)&env->fregs[PARAM1 + 1] = d.l.lower;
    RETURN();
}

void OPPROTO op_fadd_FT(void)
{
    FT0 = float32_add(FT0, FT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fadd_DT(void)
{
    DT0 = float64_add(DT0, DT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fsub_FT(void)
{
    FT0 = float32_sub(FT0, FT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fsub_DT(void)
{
    DT0 = float64_sub(DT0, DT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fmul_FT(void)
{
    FT0 = float32_mul(FT0, FT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fmul_DT(void)
{
    DT0 = float64_mul(DT0, DT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fdiv_FT(void)
{
    FT0 = float32_div(FT0, FT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fdiv_DT(void)
{
    DT0 = float64_div(DT0, DT1, &env->fp_status);
    RETURN();
}

void OPPROTO op_fcmp_eq_FT(void)
{
    cond_t(float32_compare(FT0, FT1, &env->fp_status) == 0);
    RETURN();
}

void OPPROTO op_fcmp_eq_DT(void)
{
    cond_t(float64_compare(DT0, DT1, &env->fp_status) == 0);
    RETURN();
}

void OPPROTO op_fcmp_gt_FT(void)
{
    cond_t(float32_compare(FT0, FT1, &env->fp_status) == 1);
    RETURN();
}

void OPPROTO op_fcmp_gt_DT(void)
{
    cond_t(float64_compare(DT0, DT1, &env->fp_status) == 1);
    RETURN();
}

void OPPROTO op_float_FT(void)
{
    FT0 = int32_to_float32(env->fpul, &env->fp_status);
    RETURN();
}

void OPPROTO op_float_DT(void)
{
    DT0 = int32_to_float64(env->fpul, &env->fp_status);
    RETURN();
}

void OPPROTO op_ftrc_FT(void)
{
    env->fpul = float32_to_int32_round_to_zero(FT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_ftrc_DT(void)
{
    env->fpul = float64_to_int32_round_to_zero(DT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_fneg_frN(void)
{
    env->fregs[PARAM1] = float32_chs(env->fregs[PARAM1]);
    RETURN();
}

void OPPROTO op_fabs_FT(void)
{
    FT0 = float32_abs(FT0);
    RETURN();
}

void OPPROTO op_fabs_DT(void)
{
    DT0 = float64_abs(DT0);
    RETURN();
}

void OPPROTO op_fcnvsd_FT_DT(void)
{
    DT0 = float32_to_float64(FT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_fcnvds_DT_FT(void)
{
    FT0 = float64_to_float32(DT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_fsqrt_FT(void)
{
    FT0 = float32_sqrt(FT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_fsqrt_DT(void)
{
    DT0 = float64_sqrt(DT0, &env->fp_status);
    RETURN();
}

void OPPROTO op_fmov_T0_frN(void)
{
    *(uint32_t *)&env->fregs[PARAM1] = T0;
    RETURN();
}

void OPPROTO op_movl_fpul_FT0(void)
{
    FT0 = *(float32 *)&env->fpul;
    RETURN();
}

void OPPROTO op_movl_FT0_fpul(void)
{
    *(float32 *)&env->fpul = FT0;
    RETURN();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.c"
#undef MEMSUFFIX
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _kernel
#include "op_mem.c"
#undef MEMSUFFIX
#endif
