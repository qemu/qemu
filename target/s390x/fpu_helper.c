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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"

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
    S390CPU *cpu = s390_env_get_cpu(env);

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
        cpu_abort(CPU(cpu), "unknown return value for float compare\n");
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

/* 32-bit FP multiplication */
uint64_t HELPER(meeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP multiplication */
uint64_t HELPER(mdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64/32-bit FP multiplication */
uint64_t HELPER(mdeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    ret = float64_mul(f1, ret, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 128-bit FP multiplication */
uint64_t HELPER(mxb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    float128 ret = float128_mul(make_float128(ah, al),
                                make_float128(bh, bl),
                                &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* 128/64-bit FP multiplication */
uint64_t HELPER(mxdb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                      uint64_t f2)
{
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    ret = float128_mul(make_float128(ah, al), ret, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 64-bit float */
uint64_t HELPER(ldeb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float64_maybe_silence_nan(ret, &env->fpu_status);
}

/* convert 128-bit float to 64-bit float */
uint64_t HELPER(ldxb)(CPUS390XState *env, uint64_t ah, uint64_t al)
{
    float64 ret = float128_to_float64(make_float128(ah, al), &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float64_maybe_silence_nan(ret, &env->fpu_status);
}

/* convert 64-bit float to 128-bit float */
uint64_t HELPER(lxdb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(float128_maybe_silence_nan(ret, &env->fpu_status));
}

/* convert 32-bit float to 128-bit float */
uint64_t HELPER(lxeb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float32_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(float128_maybe_silence_nan(ret, &env->fpu_status));
}

/* convert 64-bit float to 32-bit float */
uint64_t HELPER(ledb)(CPUS390XState *env, uint64_t f2)
{
    float32 ret = float64_to_float32(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float32_maybe_silence_nan(ret, &env->fpu_status);
}

/* convert 128-bit float to 32-bit float */
uint64_t HELPER(lexb)(CPUS390XState *env, uint64_t ah, uint64_t al)
{
    float32 ret = float128_to_float32(make_float128(ah, al), &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float32_maybe_silence_nan(ret, &env->fpu_status);
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

static int swap_round_mode(CPUS390XState *env, int m3)
{
    int ret = env->fpu_status.float_rounding_mode;
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
    return ret;
}

/* convert 64-bit int to 32-bit float */
uint64_t HELPER(cegb)(CPUS390XState *env, int64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float32 ret = int64_to_float32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit int to 64-bit float */
uint64_t HELPER(cdgb)(CPUS390XState *env, int64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float64 ret = int64_to_float64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit int to 128-bit float */
uint64_t HELPER(cxgb)(CPUS390XState *env, int64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 ret = int64_to_float128(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* convert 64-bit uint to 32-bit float */
uint64_t HELPER(celgb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float32 ret = uint64_to_float32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit uint to 64-bit float */
uint64_t HELPER(cdlgb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float64 ret = uint64_to_float64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit uint to 128-bit float */
uint64_t HELPER(cxlgb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 ret = uint64_to_float128(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 64-bit int */
uint64_t HELPER(cgeb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    int64_t ret = float32_to_int64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit float to 64-bit int */
uint64_t HELPER(cgdb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    int64_t ret = float64_to_int64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 64-bit int */
uint64_t HELPER(cgxb)(CPUS390XState *env, uint64_t h, uint64_t l, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 v2 = make_float128(h, l);
    int64_t ret = float128_to_int64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 32-bit float to 32-bit int */
uint64_t HELPER(cfeb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    int32_t ret = float32_to_int32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit float to 32-bit int */
uint64_t HELPER(cfdb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    int32_t ret = float64_to_int32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 32-bit int */
uint64_t HELPER(cfxb)(CPUS390XState *env, uint64_t h, uint64_t l, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 v2 = make_float128(h, l);
    int32_t ret = float128_to_int32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 32-bit float to 64-bit uint */
uint64_t HELPER(clgeb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    uint64_t ret;
    v2 = float32_to_float64(v2, &env->fpu_status);
    ret = float64_to_uint64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit float to 64-bit uint */
uint64_t HELPER(clgdb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    uint64_t ret = float64_to_uint64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 64-bit uint */
uint64_t HELPER(clgxb)(CPUS390XState *env, uint64_t h, uint64_t l, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 v2 = make_float128(h, l);
    /* ??? Not 100% correct.  */
    uint64_t ret = float128_to_int64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 32-bit float to 32-bit uint */
uint64_t HELPER(clfeb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    uint32_t ret = float32_to_uint32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 64-bit float to 32-bit uint */
uint64_t HELPER(clfdb)(CPUS390XState *env, uint64_t v2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    uint32_t ret = float64_to_uint32(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* convert 128-bit float to 32-bit uint */
uint64_t HELPER(clfxb)(CPUS390XState *env, uint64_t h, uint64_t l, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 v2 = make_float128(h, l);
    /* Not 100% correct.  */
    uint32_t ret = float128_to_int64(v2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* round to integer 32-bit */
uint64_t HELPER(fieb)(CPUS390XState *env, uint64_t f2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float32 ret = float32_round_to_int(f2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* round to integer 64-bit */
uint64_t HELPER(fidb)(CPUS390XState *env, uint64_t f2, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float64 ret = float64_round_to_int(f2, &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* round to integer 128-bit */
uint64_t HELPER(fixb)(CPUS390XState *env, uint64_t ah, uint64_t al, uint32_t m3)
{
    int hold = swap_round_mode(env, m3);
    float128 ret = float128_round_to_int(make_float128(ah, al),
                                         &env->fpu_status);
    set_float_rounding_mode(hold, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

/* 32-bit FP compare and signal */
uint32_t HELPER(keb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    int cmp = float32_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 64-bit FP compare and signal */
uint32_t HELPER(kdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    int cmp = float64_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 128-bit FP compare and signal */
uint32_t HELPER(kxb)(CPUS390XState *env, uint64_t ah, uint64_t al,
                     uint64_t bh, uint64_t bl)
{
    int cmp = float128_compare(make_float128(ah, al),
                               make_float128(bh, bl),
                               &env->fpu_status);
    handle_exceptions(env, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 32-bit FP multiply and add */
uint64_t HELPER(maeb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f2, f3, f1, 0, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP multiply and add */
uint64_t HELPER(madb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f2, f3, f1, 0, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 32-bit FP multiply and subtract */
uint64_t HELPER(mseb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f2, f3, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* 64-bit FP multiply and subtract */
uint64_t HELPER(msdb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f2, f3, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* test data class 32-bit */
uint32_t HELPER(tceb)(CPUS390XState *env, uint64_t f1, uint64_t m2)
{
    float32 v1 = f1;
    int neg = float32_is_neg(v1);
    uint32_t cc = 0;

    if ((float32_is_zero(v1) && (m2 & (1 << (11-neg)))) ||
        (float32_is_infinity(v1) && (m2 & (1 << (5-neg)))) ||
        (float32_is_any_nan(v1) && (m2 & (1 << (3-neg)))) ||
        (float32_is_signaling_nan(v1, &env->fpu_status) &&
         (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }
    /* FIXME: denormalized? */
    return cc;
}

/* test data class 64-bit */
uint32_t HELPER(tcdb)(CPUS390XState *env, uint64_t v1, uint64_t m2)
{
    int neg = float64_is_neg(v1);
    uint32_t cc = 0;

    if ((float64_is_zero(v1) && (m2 & (1 << (11-neg)))) ||
        (float64_is_infinity(v1) && (m2 & (1 << (5-neg)))) ||
        (float64_is_any_nan(v1) && (m2 & (1 << (3-neg)))) ||
        (float64_is_signaling_nan(v1, &env->fpu_status) &&
         (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }
    /* FIXME: denormalized? */
    return cc;
}

/* test data class 128-bit */
uint32_t HELPER(tcxb)(CPUS390XState *env, uint64_t ah,
                      uint64_t al, uint64_t m2)
{
    float128 v1 = make_float128(ah, al);
    int neg = float128_is_neg(v1);
    uint32_t cc = 0;

    if ((float128_is_zero(v1) && (m2 & (1 << (11-neg)))) ||
        (float128_is_infinity(v1) && (m2 & (1 << (5-neg)))) ||
        (float128_is_any_nan(v1) && (m2 & (1 << (3-neg)))) ||
        (float128_is_signaling_nan(v1, &env->fpu_status) &&
         (m2 & (1 << (1-neg))))) {
        cc = 1;
    } else if (m2 & (1 << (9-neg))) {
        /* assume normalized number */
        cc = 1;
    }
    /* FIXME: denormalized? */
    return cc;
}

/* square root 32-bit */
uint64_t HELPER(sqeb)(CPUS390XState *env, uint64_t f2)
{
    float32 ret = float32_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* square root 64-bit */
uint64_t HELPER(sqdb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float64_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, GETPC());
    return ret;
}

/* square root 128-bit */
uint64_t HELPER(sqxb)(CPUS390XState *env, uint64_t ah, uint64_t al)
{
    float128 ret = float128_sqrt(make_float128(ah, al), &env->fpu_status);
    handle_exceptions(env, GETPC());
    return RET128(ret);
}

static const int fpc_to_rnd[4] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

/* set fpc */
void HELPER(sfpc)(CPUS390XState *env, uint64_t fpc)
{
    /* Install everything in the main FPC.  */
    env->fpc = fpc;

    /* Install the rounding mode in the shadow fpu_status.  */
    set_float_rounding_mode(fpc_to_rnd[fpc & 3], &env->fpu_status);
}

/* set fpc and signal */
void HELPER(sfas)(CPUS390XState *env, uint64_t val)
{
    uint32_t signalling = env->fpc;
    uint32_t source = val;
    uint32_t s390_exc;

    /* The contents of the source operand are placed in the FPC register;
       then the flags in the FPC register are set to the logical OR of the
       signalling flags and the source flags.  */
    env->fpc = source | (signalling & 0x00ff0000);
    set_float_rounding_mode(fpc_to_rnd[source & 3], &env->fpu_status);

    /* If any signalling flag is 1 and the corresponding source mask
       is also 1, a simulated-iee-exception trap occurs.  */
    s390_exc = (signalling >> 16) & (source >> 24);
    if (s390_exc) {
        ieee_exception(env, s390_exc | 3, GETPC());
    }
}
