/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch float point emulation helpers for QEMU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"
#include "internals.h"

#define FLOAT_TO_INT32_OVERFLOW 0x7fffffff
#define FLOAT_TO_INT64_OVERFLOW 0x7fffffffffffffffULL

static inline uint64_t nanbox_s(float32 fp)
{
    return fp | MAKE_64BIT_MASK(32, 32);
}

/* Convert loongarch rounding mode in fcsr0 to IEEE library */
static const FloatRoundMode ieee_rm[4] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

void restore_fp_status(CPULoongArchState *env)
{
    set_float_rounding_mode(ieee_rm[(env->fcsr0 >> FCSR0_RM) & 0x3],
                            &env->fp_status);
    set_flush_to_zero(0, &env->fp_status);
}

static int ieee_ex_to_loongarch(int xcpt)
{
    int ret = 0;
    if (xcpt & float_flag_invalid) {
        ret |= FP_INVALID;
    }
    if (xcpt & float_flag_overflow) {
        ret |= FP_OVERFLOW;
    }
    if (xcpt & float_flag_underflow) {
        ret |= FP_UNDERFLOW;
    }
    if (xcpt & float_flag_divbyzero) {
        ret |= FP_DIV0;
    }
    if (xcpt & float_flag_inexact) {
        ret |= FP_INEXACT;
    }
    return ret;
}

static void update_fcsr0_mask(CPULoongArchState *env, uintptr_t pc, int mask)
{
    int flags = get_float_exception_flags(&env->fp_status);

    set_float_exception_flags(0, &env->fp_status);

    flags &= ~mask;

    if (!flags) {
        SET_FP_CAUSE(env->fcsr0, flags);
        return;
    } else {
        flags = ieee_ex_to_loongarch(flags);
        SET_FP_CAUSE(env->fcsr0, flags);
    }

    if (GET_FP_ENABLES(env->fcsr0) & flags) {
        do_raise_exception(env, EXCCODE_FPE, pc);
    } else {
        UPDATE_FP_FLAGS(env->fcsr0, flags);
    }
}

static void update_fcsr0(CPULoongArchState *env, uintptr_t pc)
{
    update_fcsr0_mask(env, pc, 0);
}

uint64_t helper_fadd_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_add((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fadd_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_add(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fsub_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_sub((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fsub_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_sub(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmul_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_mul((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmul_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_mul(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fdiv_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_div((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fdiv_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_div(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmax_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_maxnum((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmax_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_maxnum(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmin_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_minnum((uint32_t)fj, (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmin_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_minnum(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmaxa_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_maxnummag((uint32_t)fj,
                                    (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmaxa_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_maxnummag(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmina_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = nanbox_s(float32_minnummag((uint32_t)fj,
                                    (uint32_t)fk, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmina_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;

    fd = float64_minnummag(fj, fk, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fscaleb_s(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;
    int32_t n = (int32_t)fk;

    fd = nanbox_s(float32_scalbn((uint32_t)fj,
                                 n >  0x200 ?  0x200 :
                                 n < -0x200 ? -0x200 : n,
                                 &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fscaleb_d(CPULoongArchState *env, uint64_t fj, uint64_t fk)
{
    uint64_t fd;
    int64_t n = (int64_t)fk;

    fd = float64_scalbn(fj,
                        n >  0x1000 ?  0x1000 :
                        n < -0x1000 ? -0x1000 : n,
                        &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fsqrt_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = nanbox_s(float32_sqrt((uint32_t)fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fsqrt_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float64_sqrt(fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frecip_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = nanbox_s(float32_div(float32_one, (uint32_t)fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frecip_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float64_div(float64_one, fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frsqrt_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    uint32_t fp;

    fp = float32_sqrt((uint32_t)fj, &env->fp_status);
    fd = nanbox_s(float32_div(float32_one, fp, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frsqrt_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fp, fd;

    fp = float64_sqrt(fj, &env->fp_status);
    fd = float64_div(float64_one, fp, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_flogb_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    uint32_t fp;
    float_status *status = &env->fp_status;
    FloatRoundMode old_mode = get_float_rounding_mode(status);

    set_float_rounding_mode(float_round_down, status);
    fp = float32_log2((uint32_t)fj, status);
    fd = nanbox_s(float32_round_to_int(fp, status));
    set_float_rounding_mode(old_mode, status);
    update_fcsr0_mask(env, GETPC(), float_flag_inexact);
    return fd;
}

uint64_t helper_flogb_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    float_status *status = &env->fp_status;
    FloatRoundMode old_mode = get_float_rounding_mode(status);

    set_float_rounding_mode(float_round_down, status);
    fd = float64_log2(fj, status);
    fd = float64_round_to_int(fd, status);
    set_float_rounding_mode(old_mode, status);
    update_fcsr0_mask(env, GETPC(), float_flag_inexact);
    return fd;
}

uint64_t helper_fclass_s(CPULoongArchState *env, uint64_t fj)
{
    float32 f = fj;
    bool sign = float32_is_neg(f);

    if (float32_is_infinity(f)) {
        return sign ? 1 << 2 : 1 << 6;
    } else if (float32_is_zero(f)) {
        return sign ? 1 << 5 : 1 << 9;
    } else if (float32_is_zero_or_denormal(f)) {
        return sign ? 1 << 4 : 1 << 8;
    } else if (float32_is_any_nan(f)) {
        float_status s = { }; /* for snan_bit_is_one */
        return float32_is_quiet_nan(f, &s) ? 1 << 1 : 1 << 0;
    } else {
        return sign ? 1 << 3 : 1 << 7;
    }
}

uint64_t helper_fclass_d(CPULoongArchState *env, uint64_t fj)
{
    float64 f = fj;
    bool sign = float64_is_neg(f);

    if (float64_is_infinity(f)) {
        return sign ? 1 << 2 : 1 << 6;
    } else if (float64_is_zero(f)) {
        return sign ? 1 << 5 : 1 << 9;
    } else if (float64_is_zero_or_denormal(f)) {
        return sign ? 1 << 4 : 1 << 8;
    } else if (float64_is_any_nan(f)) {
        float_status s = { }; /* for snan_bit_is_one */
        return float64_is_quiet_nan(f, &s) ? 1 << 1 : 1 << 0;
    } else {
        return sign ? 1 << 3 : 1 << 7;
    }
}

uint64_t helper_fmuladd_s(CPULoongArchState *env, uint64_t fj,
                          uint64_t fk, uint64_t fa, uint32_t flag)
{
    uint64_t fd;

    fd = nanbox_s(float32_muladd((uint32_t)fj, (uint32_t)fk,
                                 (uint32_t)fa, flag, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fmuladd_d(CPULoongArchState *env, uint64_t fj,
                          uint64_t fk, uint64_t fa, uint32_t flag)
{
    uint64_t fd;

    fd = float64_muladd(fj, fk, fa, flag, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

static uint64_t fcmp_common(CPULoongArchState *env, FloatRelation cmp,
                            uint32_t flags)
{
    bool ret;

    switch (cmp) {
    case float_relation_less:
        ret = (flags & FCMP_LT);
        break;
    case float_relation_equal:
        ret = (flags & FCMP_EQ);
        break;
    case float_relation_greater:
        ret = (flags & FCMP_GT);
        break;
    case float_relation_unordered:
        ret = (flags & FCMP_UN);
        break;
    default:
        g_assert_not_reached();
    }
    update_fcsr0(env, GETPC());

    return ret;
}

/* fcmp_cXXX_s */
uint64_t helper_fcmp_c_s(CPULoongArchState *env, uint64_t fj,
                         uint64_t fk, uint32_t flags)
{
    FloatRelation cmp = float32_compare_quiet((uint32_t)fj,
                                              (uint32_t)fk, &env->fp_status);
    return fcmp_common(env, cmp, flags);
}

/* fcmp_sXXX_s */
uint64_t helper_fcmp_s_s(CPULoongArchState *env, uint64_t fj,
                         uint64_t fk, uint32_t flags)
{
    FloatRelation cmp = float32_compare((uint32_t)fj,
                                        (uint32_t)fk, &env->fp_status);
    return fcmp_common(env, cmp, flags);
}

/* fcmp_cXXX_d */
uint64_t helper_fcmp_c_d(CPULoongArchState *env, uint64_t fj,
                         uint64_t fk, uint32_t flags)
{
    FloatRelation cmp = float64_compare_quiet(fj, fk, &env->fp_status);
    return fcmp_common(env, cmp, flags);
}

/* fcmp_sXXX_d */
uint64_t helper_fcmp_s_d(CPULoongArchState *env, uint64_t fj,
                         uint64_t fk, uint32_t flags)
{
    FloatRelation cmp = float64_compare(fj, fk, &env->fp_status);
    return fcmp_common(env, cmp, flags);
}

/* floating point conversion */
uint64_t helper_fcvt_s_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = nanbox_s(float64_to_float32(fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_fcvt_d_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float32_to_float64((uint32_t)fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ffint_s_w(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = nanbox_s(int32_to_float32((int32_t)fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ffint_s_l(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = nanbox_s(int64_to_float32(fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ffint_d_w(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = int32_to_float64((int32_t)fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ffint_d_l(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = int64_to_float64(fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frint_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = (uint64_t)(float32_round_to_int((uint32_t)fj, &env->fp_status));
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_frint_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float64_round_to_int(fj, &env->fp_status);
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrm_l_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_down, &env->fp_status);
    fd = float64_to_int64(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrm_l_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_down, &env->fp_status);
    fd = float32_to_int64((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrm_w_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_down, &env->fp_status);
    fd = (uint64_t)float64_to_int32(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrm_w_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_down, &env->fp_status);
    fd = (uint64_t)float32_to_int32((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrp_l_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_up, &env->fp_status);
    fd = float64_to_int64(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrp_l_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_up, &env->fp_status);
    fd = float32_to_int64((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrp_w_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_up, &env->fp_status);
    fd = (uint64_t)float64_to_int32(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrp_w_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_up, &env->fp_status);
    fd = (uint64_t)float32_to_int32((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrz_l_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    fd = float64_to_int64_round_to_zero(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrz_l_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    fd = float32_to_int64_round_to_zero((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrz_w_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    fd = (uint64_t)float64_to_int32_round_to_zero(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrz_w_s(CPULoongArchState *env, uint64_t fj)
{
    uint32_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    fd = float32_to_int32_round_to_zero((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return (uint64_t)fd;
}

uint64_t helper_ftintrne_l_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    fd = float64_to_int64(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrne_l_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    fd = float32_to_int64((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrne_w_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    fd = (uint64_t)float64_to_int32(fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftintrne_w_s(CPULoongArchState *env, uint64_t fj)
{
    uint32_t fd;
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    fd = float32_to_int32((uint32_t)fj, &env->fp_status);
    set_float_rounding_mode(old_mode, &env->fp_status);

    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return (uint64_t)fd;
}

uint64_t helper_ftint_l_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float64_to_int64(fj, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftint_l_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = float32_to_int64((uint32_t)fj, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) &
        (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT64_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftint_w_s(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = (uint64_t)float32_to_int32((uint32_t)fj, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

uint64_t helper_ftint_w_d(CPULoongArchState *env, uint64_t fj)
{
    uint64_t fd;

    fd = (uint64_t)float64_to_int32(fj, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        fd = FLOAT_TO_INT32_OVERFLOW;
    }
    update_fcsr0(env, GETPC());
    return fd;
}

void helper_set_rounding_mode(CPULoongArchState *env, uint32_t fcsr0)
{
    set_float_rounding_mode(ieee_rm[(fcsr0 >> FCSR0_RM) & 0x3],
                            &env->fp_status);
}
