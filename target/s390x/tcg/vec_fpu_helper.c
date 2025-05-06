/*
 * QEMU TCG support -- s390x vector floating point instruction support
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "vec.h"
#include "tcg_s390x.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#define VIC_INVALID         0x1
#define VIC_DIVBYZERO       0x2
#define VIC_OVERFLOW        0x3
#define VIC_UNDERFLOW       0x4
#define VIC_INEXACT         0x5

/* returns the VEX. If the VEX is 0, there is no trap */
static uint8_t check_ieee_exc(CPUS390XState *env, uint8_t enr, bool XxC,
                              uint8_t *vec_exc)
{
    uint8_t vece_exc = 0, trap_exc;
    unsigned qemu_exc;

    /* Retrieve and clear the softfloat exceptions */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return 0;
    }
    env->fpu_status.float_exception_flags = 0;

    vece_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    /* Add them to the vector-wide s390x exception bits */
    *vec_exc |= vece_exc;

    /* Check for traps and construct the VXC */
    trap_exc = vece_exc & env->fpc >> 24;
    if (trap_exc) {
        if (trap_exc & S390_IEEE_MASK_INVALID) {
            return enr << 4 | VIC_INVALID;
        } else if (trap_exc & S390_IEEE_MASK_DIVBYZERO) {
            return enr << 4 | VIC_DIVBYZERO;
        } else if (trap_exc & S390_IEEE_MASK_OVERFLOW) {
            return enr << 4 | VIC_OVERFLOW;
        } else if (trap_exc & S390_IEEE_MASK_UNDERFLOW) {
            return enr << 4 | VIC_UNDERFLOW;
        } else if (!XxC) {
            g_assert(trap_exc & S390_IEEE_MASK_INEXACT);
            /* inexact has lowest priority on traps */
            return enr << 4 | VIC_INEXACT;
        }
    }
    return 0;
}

static void handle_ieee_exc(CPUS390XState *env, uint8_t vxc, uint8_t vec_exc,
                            uintptr_t retaddr)
{
    if (vxc) {
        /* on traps, the fpc flags are not updated, instruction is suppressed */
        tcg_s390_vector_exception(env, vxc, retaddr);
    }
    if (vec_exc) {
        /* indicate exceptions for all elements combined */
        env->fpc |= vec_exc << 16;
    }
}

static float32 s390_vec_read_float32(const S390Vector *v, uint8_t enr)
{
    return make_float32(s390_vec_read_element32(v, enr));
}

static float64 s390_vec_read_float64(const S390Vector *v, uint8_t enr)
{
    return make_float64(s390_vec_read_element64(v, enr));
}

static float128 s390_vec_read_float128(const S390Vector *v)
{
    return make_float128(s390_vec_read_element64(v, 0),
                         s390_vec_read_element64(v, 1));
}

static void s390_vec_write_float32(S390Vector *v, uint8_t enr, float32 data)
{
    return s390_vec_write_element32(v, enr, data);
}

static void s390_vec_write_float64(S390Vector *v, uint8_t enr, float64 data)
{
    return s390_vec_write_element64(v, enr, data);
}

static void s390_vec_write_float128(S390Vector *v, float128 data)
{
    s390_vec_write_element64(v, 0, data.high);
    s390_vec_write_element64(v, 1, data.low);
}

typedef float32 (*vop32_2_fn)(float32 a, float_status *s);
static void vop32_2(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                    bool s, bool XxC, uint8_t erm, vop32_2_fn fn,
                    uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    for (i = 0; i < 4; i++) {
        const float32 a = s390_vec_read_float32(v2, i);

        s390_vec_write_float32(&tmp, i, fn(a, &env->fpu_status));
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

typedef float64 (*vop64_2_fn)(float64 a, float_status *s);
static void vop64_2(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                    bool s, bool XxC, uint8_t erm, vop64_2_fn fn,
                    uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    for (i = 0; i < 2; i++) {
        const float64 a = s390_vec_read_float64(v2, i);

        s390_vec_write_float64(&tmp, i, fn(a, &env->fpu_status));
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

typedef float128 (*vop128_2_fn)(float128 a, float_status *s);
static void vop128_2(S390Vector *v1, const S390Vector *v2, CPUS390XState *env,
                    bool s, bool XxC, uint8_t erm, vop128_2_fn fn,
                    uintptr_t retaddr)
{
    const float128 a = s390_vec_read_float128(v2);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    s390_vec_write_float128(&tmp, fn(a, &env->fpu_status));
    vxc = check_ieee_exc(env, 0, XxC, &vec_exc);
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static float32 vcdg32(float32 a, float_status *s)
{
    return int32_to_float32(a, s);
}

static float32 vcdlg32(float32 a, float_status *s)
{
    return uint32_to_float32(a, s);
}

static float32 vcgd32(float32 a, float_status *s)
{
    const float32 tmp = float32_to_int32(a, s);

    return float32_is_any_nan(a) ? INT32_MIN : tmp;
}

static float32 vclgd32(float32 a, float_status *s)
{
    const float32 tmp = float32_to_uint32(a, s);

    return float32_is_any_nan(a) ? 0 : tmp;
}

static float64 vcdg64(float64 a, float_status *s)
{
    return int64_to_float64(a, s);
}

static float64 vcdlg64(float64 a, float_status *s)
{
    return uint64_to_float64(a, s);
}

static float64 vcgd64(float64 a, float_status *s)
{
    const float64 tmp = float64_to_int64(a, s);

    return float64_is_any_nan(a) ? INT64_MIN : tmp;
}

static float64 vclgd64(float64 a, float_status *s)
{
    const float64 tmp = float64_to_uint64(a, s);

    return float64_is_any_nan(a) ? 0 : tmp;
}

#define DEF_GVEC_VOP2_FN(NAME, FN, BITS)                                       \
void HELPER(gvec_##NAME##BITS)(void *v1, const void *v2, CPUS390XState *env,   \
                               uint32_t desc)                                  \
{                                                                              \
    const uint8_t erm = extract32(simd_data(desc), 4, 4);                      \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
    const bool XxC = extract32(simd_data(desc), 2, 1);                         \
                                                                               \
    vop##BITS##_2(v1, v2, env, se, XxC, erm, FN, GETPC());                     \
}

#define DEF_GVEC_VOP2_32(NAME)                                                 \
DEF_GVEC_VOP2_FN(NAME, NAME##32, 32)

#define DEF_GVEC_VOP2_64(NAME)                                                 \
DEF_GVEC_VOP2_FN(NAME, NAME##64, 64)

#define DEF_GVEC_VOP2(NAME, OP)                                                \
DEF_GVEC_VOP2_FN(NAME, float32_##OP, 32)                                       \
DEF_GVEC_VOP2_FN(NAME, float64_##OP, 64)                                       \
DEF_GVEC_VOP2_FN(NAME, float128_##OP, 128)

DEF_GVEC_VOP2_32(vcdg)
DEF_GVEC_VOP2_32(vcdlg)
DEF_GVEC_VOP2_32(vcgd)
DEF_GVEC_VOP2_32(vclgd)
DEF_GVEC_VOP2_64(vcdg)
DEF_GVEC_VOP2_64(vcdlg)
DEF_GVEC_VOP2_64(vcgd)
DEF_GVEC_VOP2_64(vclgd)
DEF_GVEC_VOP2(vfi, round_to_int)
DEF_GVEC_VOP2(vfsq, sqrt)

typedef float32 (*vop32_3_fn)(float32 a, float32 b, float_status *s);
static void vop32_3(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                    CPUS390XState *env, bool s, vop32_3_fn fn,
                    uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 4; i++) {
        const float32 a = s390_vec_read_float32(v2, i);
        const float32 b = s390_vec_read_float32(v3, i);

        s390_vec_write_float32(&tmp, i, fn(a, b, &env->fpu_status));
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

typedef float64 (*vop64_3_fn)(float64 a, float64 b, float_status *s);
static void vop64_3(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                    CPUS390XState *env, bool s, vop64_3_fn fn,
                    uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = s390_vec_read_float64(v2, i);
        const float64 b = s390_vec_read_float64(v3, i);

        s390_vec_write_float64(&tmp, i, fn(a, b, &env->fpu_status));
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

typedef float128 (*vop128_3_fn)(float128 a, float128 b, float_status *s);
static void vop128_3(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                     CPUS390XState *env, bool s, vop128_3_fn fn,
                     uintptr_t retaddr)
{
    const float128 a = s390_vec_read_float128(v2);
    const float128 b = s390_vec_read_float128(v3);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};

    s390_vec_write_float128(&tmp, fn(a, b, &env->fpu_status));
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

#define DEF_GVEC_VOP3_B(NAME, OP, BITS)                                        \
void HELPER(gvec_##NAME##BITS)(void *v1, const void *v2, const void *v3,       \
                              CPUS390XState *env, uint32_t desc)               \
{                                                                              \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
                                                                               \
    vop##BITS##_3(v1, v2, v3, env, se, float##BITS##_##OP, GETPC());           \
}

#define DEF_GVEC_VOP3(NAME, OP)                                                \
DEF_GVEC_VOP3_B(NAME, OP, 32)                                                  \
DEF_GVEC_VOP3_B(NAME, OP, 64)                                                  \
DEF_GVEC_VOP3_B(NAME, OP, 128)

DEF_GVEC_VOP3(vfa, add)
DEF_GVEC_VOP3(vfs, sub)
DEF_GVEC_VOP3(vfd, div)
DEF_GVEC_VOP3(vfm, mul)

static int wfc32(const S390Vector *v1, const S390Vector *v2,
                 CPUS390XState *env, bool signal, uintptr_t retaddr)
{
    /* only the zero-indexed elements are compared */
    const float32 a = s390_vec_read_float32(v1, 0);
    const float32 b = s390_vec_read_float32(v2, 0);
    uint8_t vxc, vec_exc = 0;
    int cmp;

    if (signal) {
        cmp = float32_compare(a, b, &env->fpu_status);
    } else {
        cmp = float32_compare_quiet(a, b, &env->fpu_status);
    }
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);

    return float_comp_to_cc(env, cmp);
}

static int wfc64(const S390Vector *v1, const S390Vector *v2,
                 CPUS390XState *env, bool signal, uintptr_t retaddr)
{
    /* only the zero-indexed elements are compared */
    const float64 a = s390_vec_read_float64(v1, 0);
    const float64 b = s390_vec_read_float64(v2, 0);
    uint8_t vxc, vec_exc = 0;
    int cmp;

    if (signal) {
        cmp = float64_compare(a, b, &env->fpu_status);
    } else {
        cmp = float64_compare_quiet(a, b, &env->fpu_status);
    }
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);

    return float_comp_to_cc(env, cmp);
}

static int wfc128(const S390Vector *v1, const S390Vector *v2,
                  CPUS390XState *env, bool signal, uintptr_t retaddr)
{
    /* only the zero-indexed elements are compared */
    const float128 a = s390_vec_read_float128(v1);
    const float128 b = s390_vec_read_float128(v2);
    uint8_t vxc, vec_exc = 0;
    int cmp;

    if (signal) {
        cmp = float128_compare(a, b, &env->fpu_status);
    } else {
        cmp = float128_compare_quiet(a, b, &env->fpu_status);
    }
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);

    return float_comp_to_cc(env, cmp);
}

#define DEF_GVEC_WFC_B(NAME, SIGNAL, BITS)                                     \
void HELPER(gvec_##NAME##BITS)(const void *v1, const void *v2,                 \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    env->cc_op = wfc##BITS(v1, v2, env, SIGNAL, GETPC());                      \
}

#define DEF_GVEC_WFC(NAME, SIGNAL)                                             \
     DEF_GVEC_WFC_B(NAME, SIGNAL, 32)                                          \
     DEF_GVEC_WFC_B(NAME, SIGNAL, 64)                                          \
     DEF_GVEC_WFC_B(NAME, SIGNAL, 128)

DEF_GVEC_WFC(wfc, false)
DEF_GVEC_WFC(wfk, true)

typedef bool (*vfc32_fn)(float32 a, float32 b, float_status *status);
static int vfc32(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                 CPUS390XState *env, bool s, vfc32_fn fn, uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int match = 0;
    int i;

    for (i = 0; i < 4; i++) {
        const float32 a = s390_vec_read_float32(v2, i);
        const float32 b = s390_vec_read_float32(v3, i);

        /* swap the order of the parameters, so we can use existing functions */
        if (fn(b, a, &env->fpu_status)) {
            match++;
            s390_vec_write_element32(&tmp, i, -1u);
        }
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }

    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
    if (match) {
        return s || match == 4 ? 0 : 1;
    }
    return 3;
}

typedef bool (*vfc64_fn)(float64 a, float64 b, float_status *status);
static int vfc64(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                 CPUS390XState *env, bool s, vfc64_fn fn, uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int match = 0;
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = s390_vec_read_float64(v2, i);
        const float64 b = s390_vec_read_float64(v3, i);

        /* swap the order of the parameters, so we can use existing functions */
        if (fn(b, a, &env->fpu_status)) {
            match++;
            s390_vec_write_element64(&tmp, i, -1ull);
        }
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }

    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
    if (match) {
        return s || match == 2 ? 0 : 1;
    }
    return 3;
}

typedef bool (*vfc128_fn)(float128 a, float128 b, float_status *status);
static int vfc128(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                 CPUS390XState *env, bool s, vfc128_fn fn, uintptr_t retaddr)
{
    const float128 a = s390_vec_read_float128(v2);
    const float128 b = s390_vec_read_float128(v3);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    bool match = false;

    /* swap the order of the parameters, so we can use existing functions */
    if (fn(b, a, &env->fpu_status)) {
        match = true;
        s390_vec_write_element64(&tmp, 0, -1ull);
        s390_vec_write_element64(&tmp, 1, -1ull);
    }
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
    return match ? 0 : 3;
}

#define DEF_GVEC_VFC_B(NAME, OP, BITS)                                         \
void HELPER(gvec_##NAME##BITS)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
    const bool sq = extract32(simd_data(desc), 2, 1);                          \
    vfc##BITS##_fn fn = sq ? float##BITS##_##OP : float##BITS##_##OP##_quiet;  \
                                                                               \
    vfc##BITS(v1, v2, v3, env, se, fn, GETPC());                               \
}                                                                              \
                                                                               \
void HELPER(gvec_##NAME##BITS##_cc)(void *v1, const void *v2, const void *v3,  \
                                    CPUS390XState *env, uint32_t desc)         \
{                                                                              \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
    const bool sq = extract32(simd_data(desc), 2, 1);                          \
    vfc##BITS##_fn fn = sq ? float##BITS##_##OP : float##BITS##_##OP##_quiet;  \
                                                                               \
    env->cc_op = vfc##BITS(v1, v2, v3, env, se, fn, GETPC());                  \
}

#define DEF_GVEC_VFC(NAME, OP)                                                 \
DEF_GVEC_VFC_B(NAME, OP, 32)                                                   \
DEF_GVEC_VFC_B(NAME, OP, 64)                                                   \
DEF_GVEC_VFC_B(NAME, OP, 128)                                                  \

DEF_GVEC_VFC(vfce, eq)
DEF_GVEC_VFC(vfch, lt)
DEF_GVEC_VFC(vfche, le)

void HELPER(gvec_vfll32)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        /* load from even element */
        const float32 a = s390_vec_read_element32(v2, i * 2);
        const uint64_t ret = float32_to_float64(a, &env->fpu_status);

        s390_vec_write_element64(&tmp, i, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i * 2, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfll64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    /* load from even element */
    const float128 ret = float64_to_float128(s390_vec_read_float64(v2, 0),
                                             &env->fpu_status);
    uint8_t vxc, vec_exc = 0;

    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    s390_vec_write_float128(v1, ret);
}

void HELPER(gvec_vflr64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool s = extract32(simd_data(desc), 3, 1);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    for (i = 0; i < 2; i++) {
        float64 a = s390_vec_read_element64(v2, i);
        uint32_t ret = float64_to_float32(a, &env->fpu_status);

        /* place at even element */
        s390_vec_write_element32(&tmp, i * 2, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vflr128)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint8_t erm = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    uint8_t vxc, vec_exc = 0;
    int old_mode;
    float64 ret;

    old_mode = s390_swap_bfp_rounding_mode(env, erm);
    ret = float128_to_float64(s390_vec_read_float128(v2), &env->fpu_status);
    vxc = check_ieee_exc(env, 0, XxC, &vec_exc);
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());

    /* place at even element, odd element is unpredictable */
    s390_vec_write_float64(v1, 0, ret);
}

static void vfma32(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                   const S390Vector *v4, CPUS390XState *env, bool s, int flags,
                   uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 4; i++) {
        const float32 a = s390_vec_read_float32(v3, i);
        const float32 b = s390_vec_read_float32(v2, i);
        const float32 c = s390_vec_read_float32(v4, i);
        float32 ret = float32_muladd(a, b, c, flags, &env->fpu_status);

        s390_vec_write_float32(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static void vfma64(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                   const S390Vector *v4, CPUS390XState *env, bool s, int flags,
                   uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = s390_vec_read_float64(v3, i);
        const float64 b = s390_vec_read_float64(v2, i);
        const float64 c = s390_vec_read_float64(v4, i);
        const float64 ret = float64_muladd(a, b, c, flags, &env->fpu_status);

        s390_vec_write_float64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static void vfma128(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                    const S390Vector *v4, CPUS390XState *env, bool s, int flags,
                    uintptr_t retaddr)
{
    const float128 a = s390_vec_read_float128(v3);
    const float128 b = s390_vec_read_float128(v2);
    const float128 c = s390_vec_read_float128(v4);
    uint8_t vxc, vec_exc = 0;
    float128 ret;

    ret = float128_muladd(a, b, c, flags, &env->fpu_status);
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    s390_vec_write_float128(v1, ret);
}

#define DEF_GVEC_VFMA_B(NAME, FLAGS, BITS)                                     \
void HELPER(gvec_##NAME##BITS)(void *v1, const void *v2, const void *v3,       \
                               const void *v4, CPUS390XState *env,             \
                               uint32_t desc)                                  \
{                                                                              \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
                                                                               \
    vfma##BITS(v1, v2, v3, v4, env, se, FLAGS, GETPC());                       \
}

#define DEF_GVEC_VFMA(NAME, FLAGS)                                             \
    DEF_GVEC_VFMA_B(NAME, FLAGS, 32)                                           \
    DEF_GVEC_VFMA_B(NAME, FLAGS, 64)                                           \
    DEF_GVEC_VFMA_B(NAME, FLAGS, 128)

DEF_GVEC_VFMA(vfma, 0)
DEF_GVEC_VFMA(vfms, float_muladd_negate_c)
DEF_GVEC_VFMA(vfnma, float_muladd_negate_result)
DEF_GVEC_VFMA(vfnms, float_muladd_negate_c | float_muladd_negate_result)

void HELPER(gvec_vftci32)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    uint16_t i3 = extract32(simd_data(desc), 4, 12);
    bool s = extract32(simd_data(desc), 3, 1);
    int i, match = 0;

    for (i = 0; i < 4; i++) {
        float32 a = s390_vec_read_float32(v2, i);

        if (float32_dcmask(env, a) & i3) {
            match++;
            s390_vec_write_element32(v1, i, -1u);
        } else {
            s390_vec_write_element32(v1, i, 0);
        }
        if (s) {
            break;
        }
    }

    if (match == 4 || (s && match)) {
        env->cc_op = 0;
    } else if (match) {
        env->cc_op = 1;
    } else {
        env->cc_op = 3;
    }
}

void HELPER(gvec_vftci64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint16_t i3 = extract32(simd_data(desc), 4, 12);
    const bool s = extract32(simd_data(desc), 3, 1);
    int i, match = 0;

    for (i = 0; i < 2; i++) {
        const float64 a = s390_vec_read_float64(v2, i);

        if (float64_dcmask(env, a) & i3) {
            match++;
            s390_vec_write_element64(v1, i, -1ull);
        } else {
            s390_vec_write_element64(v1, i, 0);
        }
        if (s) {
            break;
        }
    }

    if (match == 2 || (s && match)) {
        env->cc_op = 0;
    } else if (match) {
        env->cc_op = 1;
    } else {
        env->cc_op = 3;
    }
}

void HELPER(gvec_vftci128)(void *v1, const void *v2, CPUS390XState *env,
                           uint32_t desc)
{
    const float128 a = s390_vec_read_float128(v2);
    uint16_t i3 = extract32(simd_data(desc), 4, 12);

    if (float128_dcmask(env, a) & i3) {
        env->cc_op = 0;
        s390_vec_write_element64(v1, 0, -1ull);
        s390_vec_write_element64(v1, 1, -1ull);
    } else {
        env->cc_op = 3;
        s390_vec_write_element64(v1, 0, 0);
        s390_vec_write_element64(v1, 1, 0);
    }
}

typedef enum S390MinMaxType {
    S390_MINMAX_TYPE_IEEE = 0,
    S390_MINMAX_TYPE_JAVA,
    S390_MINMAX_TYPE_C_MACRO,
    S390_MINMAX_TYPE_CPP,
    S390_MINMAX_TYPE_F,
} S390MinMaxType;

typedef enum S390MinMaxRes {
    S390_MINMAX_RES_MINMAX = 0,
    S390_MINMAX_RES_A,
    S390_MINMAX_RES_B,
    S390_MINMAX_RES_SILENCE_A,
    S390_MINMAX_RES_SILENCE_B,
} S390MinMaxRes;

static S390MinMaxRes vfmin_res(uint16_t dcmask_a, uint16_t dcmask_b,
                               S390MinMaxType type, float_status *s)
{
    const bool neg_a = dcmask_a & DCMASK_NEGATIVE;
    const bool nan_a = dcmask_a & DCMASK_NAN;
    const bool nan_b = dcmask_b & DCMASK_NAN;

    g_assert(type > S390_MINMAX_TYPE_IEEE && type <= S390_MINMAX_TYPE_F);

    if (unlikely((dcmask_a | dcmask_b) & DCMASK_NAN)) {
        const bool sig_a = dcmask_a & DCMASK_SIGNALING_NAN;
        const bool sig_b = dcmask_b & DCMASK_SIGNALING_NAN;

        if ((dcmask_a | dcmask_b) & DCMASK_SIGNALING_NAN) {
            s->float_exception_flags |= float_flag_invalid;
        }
        switch (type) {
        case S390_MINMAX_TYPE_JAVA:
            if (sig_a) {
                return S390_MINMAX_RES_SILENCE_A;
            } else if (sig_b) {
                return S390_MINMAX_RES_SILENCE_B;
            }
            return nan_a ? S390_MINMAX_RES_A : S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_F:
            return nan_b ? S390_MINMAX_RES_A : S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_C_MACRO:
            s->float_exception_flags |= float_flag_invalid;
            return S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_CPP:
            s->float_exception_flags |= float_flag_invalid;
            return S390_MINMAX_RES_A;
        default:
            g_assert_not_reached();
        }
    } else if (unlikely((dcmask_a & DCMASK_ZERO) && (dcmask_b & DCMASK_ZERO))) {
        switch (type) {
        case S390_MINMAX_TYPE_JAVA:
            return neg_a ? S390_MINMAX_RES_A : S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_C_MACRO:
            return S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_F:
            return !neg_a ? S390_MINMAX_RES_B : S390_MINMAX_RES_A;
        case S390_MINMAX_TYPE_CPP:
            return S390_MINMAX_RES_A;
        default:
            g_assert_not_reached();
        }
    }
    return S390_MINMAX_RES_MINMAX;
}

static S390MinMaxRes vfmax_res(uint16_t dcmask_a, uint16_t dcmask_b,
                               S390MinMaxType type, float_status *s)
{
    g_assert(type > S390_MINMAX_TYPE_IEEE && type <= S390_MINMAX_TYPE_F);

    if (unlikely((dcmask_a | dcmask_b) & DCMASK_NAN)) {
        const bool sig_a = dcmask_a & DCMASK_SIGNALING_NAN;
        const bool sig_b = dcmask_b & DCMASK_SIGNALING_NAN;
        const bool nan_a = dcmask_a & DCMASK_NAN;
        const bool nan_b = dcmask_b & DCMASK_NAN;

        if ((dcmask_a | dcmask_b) & DCMASK_SIGNALING_NAN) {
            s->float_exception_flags |= float_flag_invalid;
        }
        switch (type) {
        case S390_MINMAX_TYPE_JAVA:
            if (sig_a) {
                return S390_MINMAX_RES_SILENCE_A;
            } else if (sig_b) {
                return S390_MINMAX_RES_SILENCE_B;
            }
            return nan_a ? S390_MINMAX_RES_A : S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_F:
            return nan_b ? S390_MINMAX_RES_A : S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_C_MACRO:
            s->float_exception_flags |= float_flag_invalid;
            return S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_CPP:
            s->float_exception_flags |= float_flag_invalid;
            return S390_MINMAX_RES_A;
        default:
            g_assert_not_reached();
        }
    } else if (unlikely((dcmask_a & DCMASK_ZERO) && (dcmask_b & DCMASK_ZERO))) {
        const bool neg_a = dcmask_a & DCMASK_NEGATIVE;

        switch (type) {
        case S390_MINMAX_TYPE_JAVA:
        case S390_MINMAX_TYPE_F:
            return neg_a ? S390_MINMAX_RES_B : S390_MINMAX_RES_A;
        case S390_MINMAX_TYPE_C_MACRO:
            return S390_MINMAX_RES_B;
        case S390_MINMAX_TYPE_CPP:
            return S390_MINMAX_RES_A;
        default:
            g_assert_not_reached();
        }
    }
    return S390_MINMAX_RES_MINMAX;
}

static S390MinMaxRes vfminmax_res(uint16_t dcmask_a, uint16_t dcmask_b,
                                  S390MinMaxType type, bool is_min,
                                  float_status *s)
{
    return is_min ? vfmin_res(dcmask_a, dcmask_b, type, s) :
                    vfmax_res(dcmask_a, dcmask_b, type, s);
}

static void vfminmax32(S390Vector *v1, const S390Vector *v2,
                       const S390Vector *v3, CPUS390XState *env,
                       S390MinMaxType type, bool is_min, bool is_abs, bool se,
                       uintptr_t retaddr)
{
    float_status *s = &env->fpu_status;
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 4; i++) {
        float32 a = s390_vec_read_float32(v2, i);
        float32 b = s390_vec_read_float32(v3, i);
        float32 result;

        if (type != S390_MINMAX_TYPE_IEEE) {
            S390MinMaxRes res;

            if (is_abs) {
                a = float32_abs(a);
                b = float32_abs(b);
            }

            res = vfminmax_res(float32_dcmask(env, a), float32_dcmask(env, b),
                               type, is_min, s);
            switch (res) {
            case S390_MINMAX_RES_MINMAX:
                result = is_min ? float32_min(a, b, s) : float32_max(a, b, s);
                break;
            case S390_MINMAX_RES_A:
                result = a;
                break;
            case S390_MINMAX_RES_B:
                result = b;
                break;
            case S390_MINMAX_RES_SILENCE_A:
                result = float32_silence_nan(a, s);
                break;
            case S390_MINMAX_RES_SILENCE_B:
                result = float32_silence_nan(b, s);
                break;
            default:
                g_assert_not_reached();
            }
        } else if (!is_abs) {
            result = is_min ? float32_minnum(a, b, &env->fpu_status) :
                              float32_maxnum(a, b, &env->fpu_status);
        } else {
            result = is_min ? float32_minnummag(a, b, &env->fpu_status) :
                              float32_maxnummag(a, b, &env->fpu_status);
        }

        s390_vec_write_float32(&tmp, i, result);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (se || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static void vfminmax64(S390Vector *v1, const S390Vector *v2,
                       const S390Vector *v3, CPUS390XState *env,
                       S390MinMaxType type, bool is_min, bool is_abs, bool se,
                       uintptr_t retaddr)
{
    float_status *s = &env->fpu_status;
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        float64 a = s390_vec_read_float64(v2, i);
        float64 b = s390_vec_read_float64(v3, i);
        float64 result;

        if (type != S390_MINMAX_TYPE_IEEE) {
            S390MinMaxRes res;

            if (is_abs) {
                a = float64_abs(a);
                b = float64_abs(b);
            }

            res = vfminmax_res(float64_dcmask(env, a), float64_dcmask(env, b),
                               type, is_min, s);
            switch (res) {
            case S390_MINMAX_RES_MINMAX:
                result = is_min ? float64_min(a, b, s) : float64_max(a, b, s);
                break;
            case S390_MINMAX_RES_A:
                result = a;
                break;
            case S390_MINMAX_RES_B:
                result = b;
                break;
            case S390_MINMAX_RES_SILENCE_A:
                result = float64_silence_nan(a, s);
                break;
            case S390_MINMAX_RES_SILENCE_B:
                result = float64_silence_nan(b, s);
                break;
            default:
                g_assert_not_reached();
            }
        } else if (!is_abs) {
            result = is_min ? float64_minnum(a, b, &env->fpu_status) :
                              float64_maxnum(a, b, &env->fpu_status);
        } else {
            result = is_min ? float64_minnummag(a, b, &env->fpu_status) :
                              float64_maxnummag(a, b, &env->fpu_status);
        }

        s390_vec_write_float64(&tmp, i, result);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (se || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static void vfminmax128(S390Vector *v1, const S390Vector *v2,
                        const S390Vector *v3, CPUS390XState *env,
                        S390MinMaxType type, bool is_min, bool is_abs, bool se,
                        uintptr_t retaddr)
{
    float128 a = s390_vec_read_float128(v2);
    float128 b = s390_vec_read_float128(v3);
    float_status *s = &env->fpu_status;
    uint8_t vxc, vec_exc = 0;
    float128 result;

    if (type != S390_MINMAX_TYPE_IEEE) {
        S390MinMaxRes res;

        if (is_abs) {
            a = float128_abs(a);
            b = float128_abs(b);
        }

        res = vfminmax_res(float128_dcmask(env, a), float128_dcmask(env, b),
                           type, is_min, s);
        switch (res) {
        case S390_MINMAX_RES_MINMAX:
            result = is_min ? float128_min(a, b, s) : float128_max(a, b, s);
            break;
        case S390_MINMAX_RES_A:
            result = a;
            break;
        case S390_MINMAX_RES_B:
            result = b;
            break;
        case S390_MINMAX_RES_SILENCE_A:
            result = float128_silence_nan(a, s);
            break;
        case S390_MINMAX_RES_SILENCE_B:
            result = float128_silence_nan(b, s);
            break;
        default:
            g_assert_not_reached();
        }
    } else if (!is_abs) {
        result = is_min ? float128_minnum(a, b, &env->fpu_status) :
                          float128_maxnum(a, b, &env->fpu_status);
    } else {
        result = is_min ? float128_minnummag(a, b, &env->fpu_status) :
                          float128_maxnummag(a, b, &env->fpu_status);
    }

    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    s390_vec_write_float128(v1, result);
}

#define DEF_GVEC_VFMINMAX_B(NAME, IS_MIN, BITS)                                \
void HELPER(gvec_##NAME##BITS)(void *v1, const void *v2, const void *v3,       \
                               CPUS390XState *env, uint32_t desc)              \
{                                                                              \
    const bool se = extract32(simd_data(desc), 3, 1);                          \
    uint8_t type = extract32(simd_data(desc), 4, 4);                           \
    bool is_abs = false;                                                       \
                                                                               \
    if (type >= 8) {                                                           \
        is_abs = true;                                                         \
        type -= 8;                                                             \
    }                                                                          \
                                                                               \
    vfminmax##BITS(v1, v2, v3, env, type, IS_MIN, is_abs, se, GETPC());        \
}

#define DEF_GVEC_VFMINMAX(NAME, IS_MIN)                                        \
    DEF_GVEC_VFMINMAX_B(NAME, IS_MIN, 32)                                      \
    DEF_GVEC_VFMINMAX_B(NAME, IS_MIN, 64)                                      \
    DEF_GVEC_VFMINMAX_B(NAME, IS_MIN, 128)

DEF_GVEC_VFMINMAX(vfmax, false)
DEF_GVEC_VFMINMAX(vfmin, true)
