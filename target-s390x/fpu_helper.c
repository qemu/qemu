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
#include "exec/softmmu_exec.h"
#endif

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

#define RET128(F) (env->retxl = F.low, F.high)

#define convert_bit(mask, from, to) \
    (to < from                      \
     ? (mask / (from / to)) & to    \
     : (mask & from) * (to / from))

static void ieee_exception(CPUS390XState *env, uint32_t dxc, uintptr_t retaddr)
{
    /* Install the DXC code.  */
    env->fpc = (env->fpc & ~0xff00) | (dxc << 8);
    /* Trap.  */
    runtime_exception(env, PGM_DATA, retaddr);
}

/* Should be called after any operation that may raise IEEE exceptions.  */
static void handle_exceptions(CPUS390XState *env, uintptr_t retaddr)
{
    unsigned s390_exc, qemu_exc;

    /* Get the exceptions raised by the current operation.  Reset the
       fpu_status contents so that the next operation has a clean slate.  */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return;
    }
    env->fpu_status.float_exception_flags = 0;

    /* Convert softfloat exception bits to s390 exception bits.  */
    s390_exc = 0;
    s390_exc |= convert_bit(qemu_exc, float_flag_invalid, 0x80);
    s390_exc |= convert_bit(qemu_exc, float_flag_divbyzero, 0x40);
    s390_exc |= convert_bit(qemu_exc, float_flag_overflow, 0x20);
    s390_exc |= convert_bit(qemu_exc, float_flag_underflow, 0x10);
    s390_exc |= convert_bit(qemu_exc, float_flag_inexact, 0x08);

    /* Install the exceptions that we raised.  */
    env->fpc |= s390_exc << 16;

    /* Send signals for enabled exceptions.  */
    s390_exc &= env->fpc >> 24;
    if (s390_exc) {
        ieee_exception(env, s390_exc, retaddr);
    }
}

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

uint32_t set_cc_nz_f128(float128 v)
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

/* 32-bit FP addition */
uint64_t HELPER(aeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP addition */
uint64_t HELPER(adb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 128-bit FP addition */
uint64_t HELPER(axb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    float128 ret = float128_add(make_float128(ah, al),
                                make_float128(bh, bl),
                                &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* 32-bit FP subtraction */
uint64_t HELPER(seb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP subtraction */
uint64_t HELPER(sdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 128-bit FP subtraction */
uint64_t HELPER(sxb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    float128 ret = float128_sub(make_float128(ah, al),
                                make_float128(bh, bl),
                                &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* 32-bit FP division */
uint64_t HELPER(deb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP division */
uint64_t HELPER(ddb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 128-bit FP division */
uint64_t HELPER(dxb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    float128 ret = float128_div(make_float128(ah, al),
                                make_float128(bh, bl),
                                &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
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
uint64_t HELPER(ldeb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 64-bit float */
uint64_t HELPER(ldxb)(CPUS390XState *env, uint64_t ah, uint64_t al)
{
    float64 ret = float128_to_float64(make_float128(ah, al), &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit float to 128-bit float */
uint64_t HELPER(lxdb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 128-bit float */
uint64_t HELPER(lxeb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float32_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* convert 64-bit float to 32-bit float */
uint64_t HELPER(ledb)(CPUS390XState *env, uint64_t f2)
{
    float32 ret = float64_to_float32(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 32-bit float */
uint64_t HELPER(lexb)(CPUS390XState *env, uint64_t ah, uint64_t al)
{
    float32 ret = float128_to_float32(make_float128(ah, al), &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
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

/* 32-bit FP compare */
uint32_t HELPER(ceb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    int cmp = float32_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 64-bit FP compare */
uint32_t HELPER(cdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    int cmp = float64_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 128-bit FP compare */
uint32_t HELPER(cxb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    int cmp = float128_compare_quiet(make_float128(ah, al),
                                     make_float128(bh, bl),
                                     &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
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

/* 32-bit FP multiplication RR */
void HELPER(meebr)(CPUS390XState *env, uint32_t f1, uint32_t f2)
{
    env->fregs[f1].l.upper = float32_mul(env->fregs[f1].l.upper,
                                         env->fregs[f2].l.upper,
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
