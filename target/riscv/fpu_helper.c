/*
 * RISC-V FPU Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internals.h"

target_ulong riscv_cpu_get_fflags(CPURISCVState *env)
{
    int soft = get_float_exception_flags(&env->fp_status);
    target_ulong hard = 0;

    hard |= (soft & float_flag_inexact) ? FPEXC_NX : 0;
    hard |= (soft & float_flag_underflow) ? FPEXC_UF : 0;
    hard |= (soft & float_flag_overflow) ? FPEXC_OF : 0;
    hard |= (soft & float_flag_divbyzero) ? FPEXC_DZ : 0;
    hard |= (soft & float_flag_invalid) ? FPEXC_NV : 0;

    return hard;
}

void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong hard)
{
    int soft = 0;

    soft |= (hard & FPEXC_NX) ? float_flag_inexact : 0;
    soft |= (hard & FPEXC_UF) ? float_flag_underflow : 0;
    soft |= (hard & FPEXC_OF) ? float_flag_overflow : 0;
    soft |= (hard & FPEXC_DZ) ? float_flag_divbyzero : 0;
    soft |= (hard & FPEXC_NV) ? float_flag_invalid : 0;

    set_float_exception_flags(soft, &env->fp_status);
}

void helper_set_rounding_mode(CPURISCVState *env, uint32_t rm)
{
    int softrm;

    if (rm == RISCV_FRM_DYN) {
        rm = env->frm;
    }
    switch (rm) {
    case RISCV_FRM_RNE:
        softrm = float_round_nearest_even;
        break;
    case RISCV_FRM_RTZ:
        softrm = float_round_to_zero;
        break;
    case RISCV_FRM_RDN:
        softrm = float_round_down;
        break;
    case RISCV_FRM_RUP:
        softrm = float_round_up;
        break;
    case RISCV_FRM_RMM:
        softrm = float_round_ties_away;
        break;
    default:
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    set_float_rounding_mode(softrm, &env->fp_status);
}

void helper_set_rounding_mode_chkfrm(CPURISCVState *env, uint32_t rm)
{
    int softrm;

    /* Always validate frm, even if rm != DYN. */
    if (unlikely(env->frm >= 5)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (rm == RISCV_FRM_DYN) {
        rm = env->frm;
    }
    switch (rm) {
    case RISCV_FRM_RNE:
        softrm = float_round_nearest_even;
        break;
    case RISCV_FRM_RTZ:
        softrm = float_round_to_zero;
        break;
    case RISCV_FRM_RDN:
        softrm = float_round_down;
        break;
    case RISCV_FRM_RUP:
        softrm = float_round_up;
        break;
    case RISCV_FRM_RMM:
        softrm = float_round_ties_away;
        break;
    case RISCV_FRM_ROD:
        softrm = float_round_to_odd;
        break;
    default:
        g_assert_not_reached();
    }

    set_float_rounding_mode(softrm, &env->fp_status);
}

static uint64_t do_fmadd_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2,
                           uint64_t rs3, int flags)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    float16 frs3 = check_nanbox_h(env, rs3);
    return nanbox_h(env, float16_muladd(frs1, frs2, frs3, flags,
                                        &env->fp_status));
}

static uint64_t do_fmadd_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2,
                           uint64_t rs3, int flags)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    float32 frs3 = check_nanbox_s(env, rs3);
    return nanbox_s(env, float32_muladd(frs1, frs2, frs3, flags,
                                        &env->fp_status));
}

uint64_t helper_fmadd_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, 0);
}

uint64_t helper_fmadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, 0, &env->fp_status);
}

uint64_t helper_fmadd_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, 0);
}

uint64_t helper_fmsub_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, float_muladd_negate_c);
}

uint64_t helper_fmsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_c,
                          &env->fp_status);
}

uint64_t helper_fmsub_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, float_muladd_negate_c);
}

uint64_t helper_fnmsub_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, float_muladd_negate_product);
}

uint64_t helper_fnmsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_product,
                          &env->fp_status);
}

uint64_t helper_fnmsub_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, float_muladd_negate_product);
}

uint64_t helper_fnmadd_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3,
                      float_muladd_negate_c | float_muladd_negate_product);
}

uint64_t helper_fnmadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_c |
                          float_muladd_negate_product, &env->fp_status);
}

uint64_t helper_fnmadd_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3,
                      float_muladd_negate_c | float_muladd_negate_product);
}

uint64_t helper_fadd_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_add(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsub_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_sub(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmul_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_mul(frs1, frs2, &env->fp_status));
}

uint64_t helper_fdiv_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_div(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmin_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                         float32_minnum(frs1, frs2, &env->fp_status) :
                         float32_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fminm_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    float32 ret = float32_min(frs1, frs2, &env->fp_status);
    return nanbox_s(env, ret);
}

uint64_t helper_fmax_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                         float32_maxnum(frs1, frs2, &env->fp_status) :
                         float32_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmaxm_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    float32 ret = float32_max(frs1, frs2, &env->fp_status);
    return nanbox_s(env, ret);
}

uint64_t helper_fsqrt_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return nanbox_s(env, float32_sqrt(frs1, &env->fp_status));
}

target_ulong helper_fle_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fcvt_w_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_wu_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return (int32_t)float32_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_s_w(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, int32_to_float32((int32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_wu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, uint32_to_float32((uint32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_l(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, int64_to_float32(rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_lu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, uint64_to_float32(rs1, &env->fp_status));
}

target_ulong helper_fclass_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return fclass_s(frs1);
}

uint64_t helper_fround_s(CPURISCVState *env, uint64_t rs1)
{
    float_status *fs = &env->fp_status;
    uint16_t nx_old = get_float_exception_flags(fs) & float_flag_inexact;
    float32 frs1 = check_nanbox_s(env, rs1);

    frs1 = float32_round_to_int(frs1, fs);

    /* Restore the original NX flag. */
    uint16_t flags = get_float_exception_flags(fs);
    flags &= ~float_flag_inexact;
    flags |= nx_old;
    set_float_exception_flags(flags, fs);

    return nanbox_s(env, frs1);
}

uint64_t helper_froundnx_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    frs1 = float32_round_to_int(frs1, &env->fp_status);
    return nanbox_s(env, frs1);
}

uint64_t helper_fadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_add(frs1, frs2, &env->fp_status);
}

uint64_t helper_fsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_sub(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmul_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_mul(frs1, frs2, &env->fp_status);
}

uint64_t helper_fdiv_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_div(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmin_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return env->priv_ver < PRIV_VERSION_1_11_0 ?
           float64_minnum(frs1, frs2, &env->fp_status) :
           float64_minimum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fminm_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_min(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmax_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return env->priv_ver < PRIV_VERSION_1_11_0 ?
           float64_maxnum(frs1, frs2, &env->fp_status) :
           float64_maximum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmaxm_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_max(frs1, frs2, &env->fp_status);
}

uint64_t helper_fcvt_s_d(CPURISCVState *env, uint64_t rs1)
{
    return nanbox_s(env, float64_to_float32(rs1, &env->fp_status));
}

uint64_t helper_fcvt_d_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_float64(frs1, &env->fp_status);
}

uint64_t helper_fsqrt_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_sqrt(frs1, &env->fp_status);
}

target_ulong helper_fle_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fcvt_w_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_int32(frs1, &env->fp_status);
}

uint64_t helper_fcvtmod_w_d(CPURISCVState *env, uint64_t value)
{
    return float64_to_int32_modulo(value, float_round_to_zero, &env->fp_status);
}

target_ulong helper_fcvt_wu_d(CPURISCVState *env, uint64_t frs1)
{
    return (int32_t)float64_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_d_w(CPURISCVState *env, target_ulong rs1)
{
    return int32_to_float64((int32_t)rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_wu(CPURISCVState *env, target_ulong rs1)
{
    return uint32_to_float64((uint32_t)rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_l(CPURISCVState *env, target_ulong rs1)
{
    return int64_to_float64(rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_lu(CPURISCVState *env, target_ulong rs1)
{
    return uint64_to_float64(rs1, &env->fp_status);
}

target_ulong helper_fclass_d(uint64_t frs1)
{
    return fclass_d(frs1);
}

uint64_t helper_fround_d(CPURISCVState *env, uint64_t frs1)
{
    float_status *fs = &env->fp_status;
    uint16_t nx_old = get_float_exception_flags(fs) & float_flag_inexact;

    frs1 = float64_round_to_int(frs1, fs);

    /* Restore the original NX flag. */
    uint16_t flags = get_float_exception_flags(fs);
    flags &= ~float_flag_inexact;
    flags |= nx_old;
    set_float_exception_flags(flags, fs);

    return frs1;
}

uint64_t helper_froundnx_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_round_to_int(frs1, &env->fp_status);
}

uint64_t helper_fadd_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_add(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsub_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_sub(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmul_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_mul(frs1, frs2, &env->fp_status));
}

uint64_t helper_fdiv_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_div(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmin_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                         float16_minnum(frs1, frs2, &env->fp_status) :
                         float16_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fminm_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    float16 ret = float16_min(frs1, frs2, &env->fp_status);
    return nanbox_h(env, ret);
}

uint64_t helper_fmax_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                         float16_maxnum(frs1, frs2, &env->fp_status) :
                         float16_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmaxm_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    float16 ret = float16_max(frs1, frs2, &env->fp_status);
    return nanbox_h(env, ret);
}

uint64_t helper_fsqrt_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return nanbox_h(env, float16_sqrt(frs1, &env->fp_status));
}

target_ulong helper_fle_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fclass_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return fclass_h(frs1);
}

uint64_t helper_fround_h(CPURISCVState *env, uint64_t rs1)
{
    float_status *fs = &env->fp_status;
    uint16_t nx_old = get_float_exception_flags(fs) & float_flag_inexact;
    float16 frs1 = check_nanbox_h(env, rs1);

    frs1 = float16_round_to_int(frs1, fs);

    /* Restore the original NX flag. */
    uint16_t flags = get_float_exception_flags(fs);
    flags &= ~float_flag_inexact;
    flags |= nx_old;
    set_float_exception_flags(flags, fs);

    return nanbox_h(env, frs1);
}

uint64_t helper_froundnx_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    frs1 = float16_round_to_int(frs1, &env->fp_status);
    return nanbox_h(env, frs1);
}

target_ulong helper_fcvt_w_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_wu_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return (int32_t)float16_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_h_w(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, int32_to_float16((int32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_wu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, uint32_to_float16((uint32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_l(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, int64_to_float16(rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_lu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, uint64_to_float16(rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return nanbox_h(env, float32_to_float16(frs1, true, &env->fp_status));
}

uint64_t helper_fcvt_s_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return nanbox_s(env, float16_to_float32(frs1, true, &env->fp_status));
}

uint64_t helper_fcvt_h_d(CPURISCVState *env, uint64_t rs1)
{
    return nanbox_h(env, float64_to_float16(rs1, true, &env->fp_status));
}

uint64_t helper_fcvt_d_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_float64(frs1, true, &env->fp_status);
}

uint64_t helper_fcvt_bf16_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return nanbox_h(env, float32_to_bfloat16(frs1, &env->fp_status));
}

uint64_t helper_fcvt_s_bf16(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return nanbox_s(env, bfloat16_to_float32(frs1, &env->fp_status));
}
