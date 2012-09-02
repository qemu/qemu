/*
 *  S/390 FPU helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static inline int float_comp_to_cc(CPUS390XState *env, int float_compare)
{
    switch (float_compare) {
    case float_relation_equal:
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 2;
    case float_relation_unordered:
        return 3;
    default:
        cpu_abort(env, "unknown return value for float compare\n");
    }
}

/* condition codes for binary FP ops */
uint32_t set_cc_f32(CPUS390XState *env, float32 v1, float32 v2)
{
    return float_comp_to_cc(env, float32_compare_quiet(v1, v2,
                                                       &env->fpu_status));
}

uint32_t set_cc_f64(CPUS390XState *env, float64 v1, float64 v2)
{
    return float_comp_to_cc(env, float64_compare_quiet(v1, v2,
                                                       &env->fpu_status));
}

/* condition codes for unary FP ops */
uint32_t set_cc_nz_f32(float32 v)
{
    if (float32_is_any_nan(v)) {
        return 3;
    } else if (float32_is_zero(v)) {
        return 0;
    } else if (float32_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

uint32_t set_cc_nz_f64(float64 v)
{
    if (float64_is_any_nan(v)) {
        return 3;
    } else if (float64_is_zero(v)) {
        return 0;
    } else if (float64_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t set_cc_nz_f128(float128 v)
{
    if (float128_is_any_nan(v)) {
        return 3;
    } else if (float128_is_zero(v)) {
        return 0;
    } else if (float128_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

/* convert 32-bit int to 64-bit float */
void HELPER(cdfbr)(CPUS390XState *env, uint32_t f1, int32_t v2)
{
    HELPER_LOG("%s: converting %d to f%d\n", __func__, v2, f1);
    env->fregs[f1].d = int32_to_float64(v2, &env->fpu_status);
}

/* convert 32-bit int to 128-bit float */
void HELPER(cxfbr)(CPUS390XState *env, uint32_t f1, int32_t v2)
{
    CPU_QuadU v1;

    v1.q = int32_to_float128(v2, &env->fpu_status);
    env->fregs[f1].ll = v1.ll.upper;
    env->fregs[f1 + 2].ll = v1.ll.lower;
}

/* convert 64-bit int to 32-bit float */
void HELPER(cegbr)(CPUS390XState *env, uint32_t f1, int64_t v2)
{
    HELPER_LOG("%s: converting %ld to f%d\n", __func__, v2, f1);
    env->fregs[f1].l.upper = int64_to_float32(v2, &env->fpu_status);
}

/* convert 64-bit int to 64-bit float */
void HELPER(cdgbr)(CPUS390XState *env, uint32_t f1, int64_t v2)
{
    HELPER_LOG("%s: converting %ld to f%d\n", __func__, v2, f1);
    env->fregs[f1].d = int64_to_float64(v2, &env->fpu_status);
}

/* convert 64-bit int to 128-bit float */
void HELPER(cxgbr)(CPUS390XState *env, uint32_t f1, int64_t v2)
{
    CPU_QuadU x1;

    x1.q = int64_to_float128(v2, &env->fpu_status);
    HELPER_LOG("%s: converted %ld to 0x%lx and 0x%lx\n", __func__, v2,
               x1.ll.upper, x1.ll.lower);
    env->fregs[f1].ll = x1.ll.upper;
    env->fregs[f1 + 2].ll = x1.ll.lower;
}

/* convert 32-bit int to 32-bit float */
void HELPER(cefbr)(CPUS390XState *env, uint32_t f1, int32_t v2)
{
    env->fregs[f1].l.upper = int32_to_float32(v2, &env->fpu_status);
    HELPER_LOG("%s: converting %d to 0x%d in f%d\n", __func__, v2,
               env->fregs[f1].l.upper, f1);
}

/* 32-bit FP addition RR */
uint32_t HELPER(aebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_add(env->fregs[f1].l.upper,
                                         env->fregs[f2].l.upper,
                                         &env->fpu_status);
    HELPER_LOG("%s: adding 0x%d resulting in 0x%d in f%d\n", __func__,
               env->fregs[f2].l.upper, env->fregs[f1].l.upper, f1);

    return set_cc_nz_f32(env->fregs[f1].l.upper);
}

/* 64-bit FP addition RR */
uint32_t HELPER(adbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_add(env->fregs[f1].d, env->fregs[f2].d,
                                   &env->fpu_status);
    HELPER_LOG("%s: adding 0x%ld resulting in 0x%ld in f%d\n", __func__,
               env->fregs[f2].d, env->fregs[f1].d, f1);

    return set_cc_nz_f64(env->fregs[f1].d);
}

/* 32-bit FP subtraction RR */
uint32_t HELPER(sebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_sub(env->fregs[f1].l.upper,
                                         env->fregs[f2].l.upper,
                                         &env->fpu_status);
    HELPER_LOG("%s: adding 0x%d resulting in 0x%d in f%d\n", __func__,
               env->fregs[f2].l.upper, env->fregs[f1].l.upper, f1);

    return set_cc_nz_f32(env->fregs[f1].l.upper);
}

/* 64-bit FP subtraction RR */
uint32_t HELPER(sdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_sub(env->fregs[f1].d, env->fregs[f2].d,
                                   &env->fpu_status);
    HELPER_LOG("%s: subtracting 0x%ld resulting in 0x%ld in f%d\n",
               __func__, env->fregs[f2].d, env->fregs[f1].d, f1);

    return set_cc_nz_f64(env->fregs[f1].d);
}

/* 32-bit FP division RR */
void HELPER(debr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_div(env->fregs[f1].l.upper,
                                         env->fregs[f2].l.upper,
                                         &env->fpu_status);
}

/* 128-bit FP division RR */
void HELPER(dxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;
    CPU_QuadU res;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;
    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    res.q = float128_div(v1.q, v2.q, &env->fpu_status);
    env->fregs[f1].ll = res.ll.upper;
    env->fregs[f1 + 2].ll = res.ll.lower;
}

/* 64-bit FP multiplication RR */
void HELPER(mdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_mul(env->fregs[f1].d, env->fregs[f2].d,
                                   &env->fpu_status);
}

/* 128-bit FP multiplication RR */
void HELPER(mxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;
    CPU_QuadU res;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;
    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    res.q = float128_mul(v1.q, v2.q, &env->fpu_status);
    env->fregs[f1].ll = res.ll.upper;
    env->fregs[f1 + 2].ll = res.ll.lower;
}

/* convert 32-bit float to 64-bit float */
void HELPER(ldebr)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    env->fregs[r1].d = float32_to_float64(env->fregs[r2].l.upper,
                                          &env->fpu_status);
}

/* convert 128-bit float to 64-bit float */
void HELPER(ldxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU x2;

    x2.ll.upper = env->fregs[f2].ll;
    x2.ll.lower = env->fregs[f2 + 2].ll;
    env->fregs[f1].d = float128_to_float64(x2.q, &env->fpu_status);
    HELPER_LOG("%s: to 0x%ld\n", __func__, env->fregs[f1].d);
}

/* convert 64-bit float to 128-bit float */
void HELPER(lxdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU res;

    res.q = float64_to_float128(env->fregs[f2].d, &env->fpu_status);
    env->fregs[f1].ll = res.ll.upper;
    env->fregs[f1 + 2].ll = res.ll.lower;
}

/* convert 64-bit float to 32-bit float */
void HELPER(ledbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    float64 d2 = env->fregs[f2].d;

    env->fregs[f1].l.upper = float64_to_float32(d2, &env->fpu_status);
}

/* convert 128-bit float to 32-bit float */
void HELPER(lexbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU x2;

    x2.ll.upper = env->fregs[f2].ll;
    x2.ll.lower = env->fregs[f2 + 2].ll;
    env->fregs[f1].l.upper = float128_to_float32(x2.q, &env->fpu_status);
    HELPER_LOG("%s: to 0x%d\n", __func__, env->fregs[f1].l.upper);
}

/* absolute value of 32-bit float */
uint32_t HELPER(lpebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    float32 v1;
    float32 v2 = env->fregs[f2].d;

    v1 = float32_abs(v2);
    env->fregs[f1].d = v1;
    return set_cc_nz_f32(v1);
}

/* absolute value of 64-bit float */
uint32_t HELPER(lpdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    float64 v1;
    float64 v2 = env->fregs[f2].d;

    v1 = float64_abs(v2);
    env->fregs[f1].d = v1;
    return set_cc_nz_f64(v1);
}

/* absolute value of 128-bit float */
uint32_t HELPER(lpxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;

    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    v1.q = float128_abs(v2.q);
    env->fregs[f1].ll = v1.ll.upper;
    env->fregs[f1 + 2].ll = v1.ll.lower;
    return set_cc_nz_f128(v1.q);
}

/* load and test 64-bit float */
uint32_t HELPER(ltdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = env->fregs[f2].d;
    return set_cc_nz_f64(env->fregs[f1].d);
}

/* load and test 32-bit float */
uint32_t HELPER(ltebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = env->fregs[f2].l.upper;
    return set_cc_nz_f32(env->fregs[f1].l.upper);
}

/* load and test 128-bit float */
uint32_t HELPER(ltxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU x;

    x.ll.upper = env->fregs[f2].ll;
    x.ll.lower = env->fregs[f2 + 2].ll;
    env->fregs[f1].ll = x.ll.upper;
    env->fregs[f1 + 2].ll = x.ll.lower;
    return set_cc_nz_f128(x.q);
}

/* load complement of 32-bit float */
uint32_t HELPER(lcebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_chs(env->fregs[f2].l.upper);

    return set_cc_nz_f32(env->fregs[f1].l.upper);
}

/* load complement of 64-bit float */
uint32_t HELPER(lcdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_chs(env->fregs[f2].d);

    return set_cc_nz_f64(env->fregs[f1].d);
}

/* load complement of 128-bit float */
uint32_t HELPER(lcxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU x1, x2;

    x2.ll.upper = env->fregs[f2].ll;
    x2.ll.lower = env->fregs[f2 + 2].ll;
    x1.q = float128_chs(x2.q);
    env->fregs[f1].ll = x1.ll.upper;
    env->fregs[f1 + 2].ll = x1.ll.lower;
    return set_cc_nz_f128(x1.q);
}

/* 32-bit FP addition RM */
void HELPER(aeb)(CPUS390XState *env, uint32_t f1, uint32_t val)
{
    float32 v1 = env->fregs[f1].l.upper;
    CPU_FloatU v2;

    v2.l = val;
    HELPER_LOG("%s: adding 0x%d from f%d and 0x%d\n", __func__,
               v1, f1, v2.f);
    env->fregs[f1].l.upper = float32_add(v1, v2.f, &env->fpu_status);
}

/* 32-bit FP division RM */
void HELPER(deb)(CPUS390XState *env, uint32_t f1, uint32_t val)
{
    float32 v1 = env->fregs[f1].l.upper;
    CPU_FloatU v2;

    v2.l = val;
    HELPER_LOG("%s: dividing 0x%d from f%d by 0x%d\n", __func__,
               v1, f1, v2.f);
    env->fregs[f1].l.upper = float32_div(v1, v2.f, &env->fpu_status);
}

/* 32-bit FP multiplication RM */
void HELPER(meeb)(CPUS390XState *env, uint32_t f1, uint32_t val)
{
    float32 v1 = env->fregs[f1].l.upper;
    CPU_FloatU v2;

    v2.l = val;
    HELPER_LOG("%s: multiplying 0x%d from f%d and 0x%d\n", __func__,
               v1, f1, v2.f);
    env->fregs[f1].l.upper = float32_mul(v1, v2.f, &env->fpu_status);
}

/* 32-bit FP compare RR */
uint32_t HELPER(cebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    float32 v1 = env->fregs[f1].l.upper;
    float32 v2 = env->fregs[f2].l.upper;

    HELPER_LOG("%s: comparing 0x%d from f%d and 0x%d\n", __func__,
               v1, f1, v2);
    return set_cc_f32(env, v1, v2);
}

/* 64-bit FP compare RR */
uint32_t HELPER(cdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    float64 v1 = env->fregs[f1].d;
    float64 v2 = env->fregs[f2].d;

    HELPER_LOG("%s: comparing 0x%ld from f%d and 0x%ld\n", __func__,
               v1, f1, v2);
    return set_cc_f64(env, v1, v2);
}

/* 128-bit FP compare RR */
uint32_t HELPER(cxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;
    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;

    return float_comp_to_cc(env, float128_compare_quiet(v1.q, v2.q,
                                                   &env->fpu_status));
}

/* 64-bit FP compare RM */
uint32_t HELPER(cdb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    float64 v1 = env->fregs[f1].d;
    CPU_DoubleU v2;

    v2.ll = cpu_ldq_data(env, a2);
    HELPER_LOG("%s: comparing 0x%ld from f%d and 0x%lx\n", __func__, v1,
               f1, v2.d);
    return set_cc_f64(env, v1, v2.d);
}

/* 64-bit FP addition RM */
uint32_t HELPER(adb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    float64 v1 = env->fregs[f1].d;
    CPU_DoubleU v2;

    v2.ll = cpu_ldq_data(env, a2);
    HELPER_LOG("%s: adding 0x%lx from f%d and 0x%lx\n", __func__,
               v1, f1, v2.d);
    env->fregs[f1].d = v1 = float64_add(v1, v2.d, &env->fpu_status);
    return set_cc_nz_f64(v1);
}

/* 32-bit FP subtraction RM */
void HELPER(seb)(CPUS390XState *env, uint32_t f1, uint32_t val)
{
    float32 v1 = env->fregs[f1].l.upper;
    CPU_FloatU v2;

    v2.l = val;
    env->fregs[f1].l.upper = float32_sub(v1, v2.f, &env->fpu_status);
}

/* 64-bit FP subtraction RM */
uint32_t HELPER(sdb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    float64 v1 = env->fregs[f1].d;
    CPU_DoubleU v2;

    v2.ll = cpu_ldq_data(env, a2);
    env->fregs[f1].d = v1 = float64_sub(v1, v2.d, &env->fpu_status);
    return set_cc_nz_f64(v1);
}

/* 64-bit FP multiplication RM */
void HELPER(mdb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    float64 v1 = env->fregs[f1].d;
    CPU_DoubleU v2;

    v2.ll = cpu_ldq_data(env, a2);
    HELPER_LOG("%s: multiplying 0x%lx from f%d and 0x%ld\n", __func__,
               v1, f1, v2.d);
    env->fregs[f1].d = float64_mul(v1, v2.d, &env->fpu_status);
}

/* 64-bit FP division RM */
void HELPER(ddb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    float64 v1 = env->fregs[f1].d;
    CPU_DoubleU v2;

    v2.ll = cpu_ldq_data(env, a2);
    HELPER_LOG("%s: dividing 0x%lx from f%d by 0x%ld\n", __func__,
               v1, f1, v2.d);
    env->fregs[f1].d = float64_div(v1, v2.d, &env->fpu_status);
}

static void set_round_mode(CPUS390XState *env, int m3)
{
    switch (m3) {
    case 0:
        /* current mode */
        break;
    case 1:
        /* biased round no nearest */
    case 4:
        /* round to nearest */
        set_float_rounding_mode(float_round_nearest_even, &env->fpu_status);
        break;
    case 5:
        /* round to zero */
        set_float_rounding_mode(float_round_to_zero, &env->fpu_status);
        break;
    case 6:
        /* round to +inf */
        set_float_rounding_mode(float_round_up, &env->fpu_status);
        break;
    case 7:
        /* round to -inf */
        set_float_rounding_mode(float_round_down, &env->fpu_status);
        break;
    }
}

/* convert 32-bit float to 64-bit int */
uint32_t HELPER(cgebr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    float32 v2 = env->fregs[f2].l.upper;

    set_round_mode(env, m3);
    env->regs[r1] = float32_to_int64(v2, &env->fpu_status);
    return set_cc_nz_f32(v2);
}

/* convert 64-bit float to 64-bit int */
uint32_t HELPER(cgdbr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    float64 v2 = env->fregs[f2].d;

    set_round_mode(env, m3);
    env->regs[r1] = float64_to_int64(v2, &env->fpu_status);
    return set_cc_nz_f64(v2);
}

/* convert 128-bit float to 64-bit int */
uint32_t HELPER(cgxbr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    CPU_QuadU v2;

    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    set_round_mode(env, m3);
    env->regs[r1] = float128_to_int64(v2.q, &env->fpu_status);
    if (float128_is_any_nan(v2.q)) {
        return 3;
    } else if (float128_is_zero(v2.q)) {
        return 0;
    } else if (float128_is_neg(v2.q)) {
        return 1;
    } else {
        return 2;
    }
}

/* convert 32-bit float to 32-bit int */
uint32_t HELPER(cfebr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    float32 v2 = env->fregs[f2].l.upper;

    set_round_mode(env, m3);
    env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) |
        float32_to_int32(v2, &env->fpu_status);
    return set_cc_nz_f32(v2);
}

/* convert 64-bit float to 32-bit int */
uint32_t HELPER(cfdbr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    float64 v2 = env->fregs[f2].d;

    set_round_mode(env, m3);
    env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) |
        float64_to_int32(v2, &env->fpu_status);
    return set_cc_nz_f64(v2);
}

/* convert 128-bit float to 32-bit int */
uint32_t HELPER(cfxbr)(CPUS390XState *env, uint32_t r1, uint32_t f2,
                       uint32_t m3)
{
    CPU_QuadU v2;

    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) |
        float128_to_int32(v2.q, &env->fpu_status);
    return set_cc_nz_f128(v2.q);
}

/* load 32-bit FP zero */
void HELPER(lzer)(CPUS390XState *env, uint32_t f1)
{
    env->fregs[f1].l.upper = float32_zero;
}

/* load 64-bit FP zero */
void HELPER(lzdr)(CPUS390XState *env, uint32_t f1)
{
    env->fregs[f1].d = float64_zero;
}

/* load 128-bit FP zero */
void HELPER(lzxr)(CPUS390XState *env, uint32_t f1)
{
    CPU_QuadU x;

    x.q = float64_to_float128(float64_zero, &env->fpu_status);
    env->fregs[f1].ll = x.ll.upper;
    env->fregs[f1 + 1].ll = x.ll.lower;
}

/* 128-bit FP subtraction RR */
uint32_t HELPER(sxbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;
    CPU_QuadU res;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;
    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    res.q = float128_sub(v1.q, v2.q, &env->fpu_status);
    env->fregs[f1].ll = res.ll.upper;
    env->fregs[f1 + 2].ll = res.ll.lower;
    return set_cc_nz_f128(res.q);
}

/* 128-bit FP addition RR */
uint32_t HELPER(axbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    CPU_QuadU v1;
    CPU_QuadU v2;
    CPU_QuadU res;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;
    v2.ll.upper = env->fregs[f2].ll;
    v2.ll.lower = env->fregs[f2 + 2].ll;
    res.q = float128_add(v1.q, v2.q, &env->fpu_status);
    env->fregs[f1].ll = res.ll.upper;
    env->fregs[f1 + 2].ll = res.ll.lower;
    return set_cc_nz_f128(res.q);
}

/* 32-bit FP multiplication RR */
void HELPER(meebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_mul(env->fregs[f1].l.upper,
                                         env->fregs[f2].l.upper,
                                         &env->fpu_status);
}

/* 64-bit FP division RR */
void HELPER(ddbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_div(env->fregs[f1].d, env->fregs[f2].d,
                                   &env->fpu_status);
}

/* 64-bit FP multiply and add RM */
void HELPER(madb)(CPUS390XState *env, uint32_t f1, uint64_t a2, uint32_t f3)
{
    CPU_DoubleU v2;

    HELPER_LOG("%s: f1 %d a2 0x%lx f3 %d\n", __func__, f1, a2, f3);
    v2.ll = cpu_ldq_data(env, a2);
    env->fregs[f1].d = float64_add(env->fregs[f1].d,
                                   float64_mul(v2.d, env->fregs[f3].d,
                                               &env->fpu_status),
                                   &env->fpu_status);
}

/* 64-bit FP multiply and add RR */
void HELPER(madbr)(CPUS390XState *env, uint32_t f1, uint32_t f3, uint32_t f2)
{
    HELPER_LOG("%s: f1 %d f2 %d f3 %d\n", __func__, f1, f2, f3);
    env->fregs[f1].d = float64_add(float64_mul(env->fregs[f2].d,
                                               env->fregs[f3].d,
                                               &env->fpu_status),
                                   env->fregs[f1].d, &env->fpu_status);
}

/* 64-bit FP multiply and subtract RR */
void HELPER(msdbr)(CPUS390XState *env, uint32_t f1, uint32_t f3, uint32_t f2)
{
    HELPER_LOG("%s: f1 %d f2 %d f3 %d\n", __func__, f1, f2, f3);
    env->fregs[f1].d = float64_sub(float64_mul(env->fregs[f2].d,
                                               env->fregs[f3].d,
                                               &env->fpu_status),
                                   env->fregs[f1].d, &env->fpu_status);
}

/* 32-bit FP multiply and add RR */
void HELPER(maebr)(CPUS390XState *env, uint32_t f1, uint32_t f3, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_add(env->fregs[f1].l.upper,
                                         float32_mul(env->fregs[f2].l.upper,
                                                     env->fregs[f3].l.upper,
                                                     &env->fpu_status),
                                         &env->fpu_status);
}

/* convert 32-bit float to 64-bit float */
void HELPER(ldeb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    uint32_t v2;

    v2 = cpu_ldl_data(env, a2);
    env->fregs[f1].d = float32_to_float64(v2,
                                          &env->fpu_status);
}

/* convert 64-bit float to 128-bit float */
void HELPER(lxdb)(CPUS390XState *env, uint32_t f1, uint64_t a2)
{
    CPU_DoubleU v2;
    CPU_QuadU v1;

    v2.ll = cpu_ldq_data(env, a2);
    v1.q = float64_to_float128(v2.d, &env->fpu_status);
    env->fregs[f1].ll = v1.ll.upper;
    env->fregs[f1 + 2].ll = v1.ll.lower;
}

/* test data class 32-bit */
uint32_t HELPER(tceb)(CPUS390XState *env, uint32_t f1, uint64_t m2)
{
    float32 v1 = env->fregs[f1].l.upper;
    int neg = float32_is_neg(v1);
    uint32_t cc = 0;

    HELPER_LOG("%s: v1 0x%lx m2 0x%lx neg %d\n", __func__, (long)v1, m2, neg);
    if ((float32_is_zero(v1) && (m2 & (1 << (11-neg)))) ||
        (float32_is_infinity(v1) && (m2 & (1 << (5-neg)))) ||
        (float32_is_any_nan(v1) && (m2 & (1 << (3-neg)))) ||
        (float32_is_signaling_nan(v1) && (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }

    /* FIXME: denormalized? */
    return cc;
}

/* test data class 64-bit */
uint32_t HELPER(tcdb)(CPUS390XState *env, uint32_t f1, uint64_t m2)
{
    float64 v1 = env->fregs[f1].d;
    int neg = float64_is_neg(v1);
    uint32_t cc = 0;

    HELPER_LOG("%s: v1 0x%lx m2 0x%lx neg %d\n", __func__, v1, m2, neg);
    if ((float64_is_zero(v1) && (m2 & (1 << (11-neg)))) ||
        (float64_is_infinity(v1) && (m2 & (1 << (5-neg)))) ||
        (float64_is_any_nan(v1) && (m2 & (1 << (3-neg)))) ||
        (float64_is_signaling_nan(v1) && (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }
    /* FIXME: denormalized? */
    return cc;
}

/* test data class 128-bit */
uint32_t HELPER(tcxb)(CPUS390XState *env, uint32_t f1, uint64_t m2)
{
    CPU_QuadU v1;
    uint32_t cc = 0;
    int neg;

    v1.ll.upper = env->fregs[f1].ll;
    v1.ll.lower = env->fregs[f1 + 2].ll;

    neg = float128_is_neg(v1.q);
    if ((float128_is_zero(v1.q) && (m2 & (1 << (11-neg)))) ||
        (float128_is_infinity(v1.q) && (m2 & (1 << (5-neg)))) ||
        (float128_is_any_nan(v1.q) && (m2 & (1 << (3-neg)))) ||
        (float128_is_signaling_nan(v1.q) && (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }
    /* FIXME: denormalized? */
    return cc;
}

/* square root 64-bit RR */
void HELPER(sqdbr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].d = float64_sqrt(env->fregs[f2].d, &env->fpu_status);
}
