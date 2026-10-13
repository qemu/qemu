/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "reg_fields.h"
#include "mmvec_qfloat.h"

#define BIAS_QF32 127
#define BIAS_EXTQF32 383
#define BIAS_QF16 15
#define BIAS_SF 127
#define BIAS_HF 15

#define E_MAX_QF16 16
#define E_MIN_QF16 -15
#define E_MAX_EXTQF16 E_MAX_QF16
#define E_MAX_EXTQF32 256
#define E_MAX_SF 128
#define E_MIN_SF -126
#define E_MAX_HF 16
#define E_MIN_HF -14

#define sf_MANTBITS 23
#define hf_MANTBITS 10
#define bf_MANTBITS 7

#define EXTQF32_BITMASK 0xFFFFFFFFFULL
#define EXTQF16_BITMASK 0x3FFFFULL

#define MAX_SIG_QF16 0x3ff
#define MAX_SIG_QF32 0x7fffff

#define ieee_pos_NaN_32 0x7FFFFFFF
#define ieee_pos_NaN_16 0x7FFF
#define ieee_pos_inf_32 0x7f800000
#define ieee_pos_inf_16 0x7C00
#define BF_POS_NAN 0x7FFF
#define BF_POS_INF 0x7f80

#define QF32_ILOG2_ZERO_EXP 0x80000000
#define QF32_ILOG2_NAN_EXP  0x7FFFFFFF
#define QF32_ILOG2_INF_EXP  0x7FFFFFFE
#define QF16_ILOG2_ZERO_EXP 0x8000
#define QF16_ILOG2_NAN_EXP  0x7FFF
#define QF16_ILOG2_INF_EXP  0x7FFE

typedef uint8_t LREQ_t;

enum {
    LREQ_Q = 1 << 0,
    LREQ_E = 1 << 1,
    LREQ_R = 1 << 2,
    LREQ_L = 1 << 3,
};

/*
 * Common indicators
 */

bool is_unfloat_neg(unfloat u)
{
    return u.parts.sign ^ u.sign;
}

/* One's complement the LRQ bits, but leave E (exponent extension) alone */
static LREQ_t negate_lreq(LREQ_t lreq)
{
    LREQ_t neg_lreq = ~lreq & EXT32_BITMASK;

    return (neg_lreq & ~LREQ_E) | (lreq & LREQ_E);
}

static bool is_extqf32_nan(uint64_t in)
{
    return in == extqf32_pos_nan || in == extqf32_neg_nan ||
           in == extqf32_pos_nan_inexact || in == extqf32_neg_nan_inexact;
}

static bool is_extqf32_inf(uint64_t in)
{
    return in == extqf32_pos_inf_exact || in == extqf32_pos_inf_inexact ||
           in == extqf32_neg_inf_exact || in == extqf32_neg_inf_inexact;
}

static bool is_extqf16_nan(uint32_t in)
{
    return in == extqf16_pos_nan || in == extqf16_neg_nan;
}

static bool is_extqf16_inf(uint32_t in)
{
    return in == extqf16_pos_inf || in == extqf16_neg_inf;
}

static FloatParts64 qfloat_unpack_ext_parts(unsigned size, uint32_t value,
                                            uint8_t ext)
{
    int32_t sig;
    int exp;
    unsigned precision;
    uint64_t frac;
    bool sign;

    if (size == 32) {
        LREQ_t lreq = ext & EXT32_BITMASK;

        /* L is the low bit of the signed 24-bit significand. */
        sig = (sextract32(value, 7, 25) & ~1) | !!(lreq & LREQ_L);
        /* The R bit is encoded as a deferred negation. */
        sig += !!(lreq & LREQ_R);
        sign = sig < 0;
        exp = ((((value & 0xff) | (!!(lreq & LREQ_E) << 8)) - 128) &
                0x1ff) - 255;
        precision = 23;
    } else {
        LREQ_t lreq = (ext & EXT16_BITMASK) << 2;

        sig = (sextract32(value, 4, 12) & ~1) | !!(lreq & LREQ_L);
        sig += !!(lreq & LREQ_R);
        sign = sig < 0;
        exp = (value & 0x1f) - BIAS_QF16;
        precision = 10;
    }

    if (sig == 0) {
        return (FloatParts64) { .cls = float_class_zero };
    }

    frac = sig < 0 ? -(int64_t)sig : sig;
    return (FloatParts64) {
        .cls = float_class_normal,
        .sign = sign,
        .exp = exp + 63 - clz64(frac) - precision,
        .frac = frac << clz64(frac),
    };
}

FloatParts64 qfloat_unfloat_parts(unfloat u)
{
    FloatParts64 parts = u.parts;

    parts.sign ^= u.sign;
    return parts;
}

static void qfloat_apply_inexact(FloatParts64 *parts, uint8_t ext)
{
    LREQ_t lreq = ext & EXT32_BITMASK;

    if (parts->cls != float_class_normal ||
        !!(lreq & LREQ_R) == !!(lreq & LREQ_Q)) {
        return;
    }
    if (!parts->sign) {
        parts->frac |= 1;
    } else if (--parts->frac < (UINT64_C(1) << 63)) {
        parts->frac = parts->frac << 1 | 1;
        parts->exp--;
    }
}

static uint64_t qfloat_pack_ext(unsigned size, int64_t sig, int biased_exp,
                                LREQ_t lreq)
{
    lreq = (lreq & ~LREQ_E) | (((biased_exp >> 8) & 1) ? LREQ_E : 0);
    if (size == 32) {
        uint64_t value = ((uint64_t)sig << 8) | (biased_exp & 0xff);

        return ((value & QF32_BITMASK) << 4) |
               (lreq & EXT32_BITMASK);
    }
    return (((uint64_t)sig << 5 | (biased_exp & 0x1f)) & QF16_BITMASK) << 2 |
           ((lreq >> 2) & EXT16_BITMASK);
}

static uint64_t qfloat_ext_overflow(unsigned size, bool sign,
                                    qfrnd_mode_enum_t mode)
{
    uint32_t sig = size == 32 ? MAX_SIG_QF32 : MAX_SIG_QF16;
    int exp = size == 32 ? E_MAX_EXTQF32 : E_MAX_EXTQF16;
    int bias = size == 32 ? BIAS_EXTQF32 : BIAS_QF16;
    LREQ_t lreq = 0;

    if (mode == RND_TO_ZERO ||
        (mode == RND_TOWARDS_NEG_INF && !sign) ||
        (mode == RND_TOWARDS_POS_INF && sign)) {
        exp--;
    }
    lreq |= LREQ_L | LREQ_Q;
    if (sign) {
        sig = ~sig;
        lreq = negate_lreq(lreq);
    }
    return qfloat_pack_ext(size, sig, exp + bias, lreq);
}

static uint64_t qfloat_ext_underflow(unsigned size, bool sign,
                                     qfrnd_mode_enum_t mode)
{
    int exp = size == 32 ? E_MIN_EXTQF32 : E_MIN_EXTQF16;
    int bias = size == 32 ? BIAS_EXTQF32 : BIAS_QF16;
    LREQ_t lreq = 0;
    uint32_t sig = 0;

    if (mode == RND_TOWARDS_POS_INF) {
        lreq |= LREQ_R;
    } else if (mode == RND_TOWARDS_NEG_INF) {
        lreq |= LREQ_Q;
    } else {
        lreq |= LREQ_Q;
    }
    if (sign) {
        sig = ~sig;
        lreq = negate_lreq(lreq);
    }
    return qfloat_pack_ext(size, sig, exp + bias, lreq);
}

uint64_t qfloat_round_ext_parts(unsigned size, FloatParts64 parts, int exp,
                                bool deferred_sign,
                                qfrnd_mode_enum_t mode)
{
    unsigned precision = size == 32 ? 23 : 10;
    int emax = size == 32 ? E_MAX_EXTQF32 : E_MAX_EXTQF16;
    int emin = size == 32 ? E_MIN_EXTQF32 : E_MIN_EXTQF16;
    int bias = size == 32 ? BIAS_EXTQF32 : BIAS_QF16;
    int shift;
    uint64_t integer, discarded, mask;
    bool half, sticky, increment;
    uint64_t quarters, rq1, even;
    int64_t sig;
    LREQ_t lreq = 0;

    if (parts.cls == float_class_zero) {
        exp = emin + bias;
        if (size == 32) {
            return (uint64_t)(exp & 0xff) << 4 |
                   (((exp >> 8) & 1) << 1) | 7;
        }
        return (uint64_t)(exp & 0x1f) << 2 | 3;
    }
    if (parts.exp - exp >= 1) {
        exp++;
    }
    if (exp > emax ||
        (exp == emax && parts.frac > (UINT64_C(1) << 63))) {
        return qfloat_ext_overflow(size, parts.sign, mode);
    }
    if (exp < emin) {
        return qfloat_ext_underflow(size, parts.sign, mode);
    }

    shift = 63 - precision - (parts.exp - exp);
    if (shift > 64) {
        integer = 0;
        half = false;
        sticky = true;
    } else if (shift == 64) {
        integer = 0;
        half = parts.frac >> 63;
        sticky = parts.frac << 1;
    } else if (shift > 0) {
        integer = parts.frac >> shift;
        mask = (UINT64_C(1) << shift) - 1;
        discarded = parts.frac & mask;
        half = discarded & (UINT64_C(1) << (shift - 1));
        sticky = discarded & ((UINT64_C(1) << (shift - 1)) - 1);
    } else {
        integer = parts.frac << -shift;
        half = false;
        sticky = false;
    }
    quarters = integer * 4 + (half ? 2 : 0) + (sticky ? 1 : 0);

    switch (mode) {
    case RND_TO_NEAREST_EVEN:
        increment = (quarters & 3) >= ((quarters >> 2 & 1) ? 2 : 3);
        break;
    case RND_TO_ZERO:
        increment = false;
        break;
    case RND_TOWARDS_NEG_INF:
        increment = parts.sign && (quarters & 3);
        break;
    case RND_TOWARDS_POS_INF:
        increment = !parts.sign && (quarters & 3);
        break;
    default:
        g_assert_not_reached();
    }

    rq1 = (quarters >> 2) * 8 + increment * 4 +
          (increment ^ ((quarters & 3) != 0)) * 2 + 1;
    even = (rq1 >> 3) & ~UINT64_C(1);
    rq1 -= even * 8 + 1;
    sig = even >> 1;
    lreq |= (rq1 >> 3) ? LREQ_L : 0;
    lreq |= ((rq1 >> 2) & 1) ? LREQ_R : 0;
    lreq |= ((rq1 >> 1) & 1) ? LREQ_Q : 0;

    if (parts.sign) {
        sig = ~sig;
        lreq = negate_lreq(lreq);
    }
    if (deferred_sign) {
        lreq = negate_lreq(lreq);
    }

    return qfloat_pack_ext(size, sig, exp + bias, lreq);
}

/*
 * NaN/Inf input behavior
 */

uint64_t handle_infinity_nan_add(unfloat u, unfloat v, uint64_t pos_nan_val,
                                 uint64_t neg_nan_val, uint64_t pos_inf_val,
                                 uint64_t neg_inf_val)
{
    bool u_neg = is_unfloat_neg(u);
    bool v_neg = is_unfloat_neg(v);

    if (u.nan || v.nan) {
        /* +NaN has the highest priority */
        if ((!u_neg && u.nan) || (!v_neg && v.nan)) {
            return pos_nan_val;
        }
        return neg_nan_val;
    }

    /* Infinities of opposing sign added together produce a NaN */
    if (u.inf && v.inf && (u_neg ^ v_neg)) {
        return pos_nan_val;
    }
    if ((u_neg && u.inf) || (v_neg && v.inf)) {
        return neg_inf_val;
    }
    return pos_inf_val;
}

uint64_t handle_infinity_nan_mpy(unfloat u, unfloat v, uint64_t pos_nan_val,
                                 uint64_t neg_nan_val, uint64_t pos_inf_val,
                                 uint64_t neg_inf_val)
{
    bool u_neg = is_unfloat_neg(u);
    bool v_neg = is_unfloat_neg(v);

    if (u.nan || v.nan) {
        return (u_neg ^ v_neg) ? neg_nan_val : pos_nan_val;
    }
    /* Implicitly, one of the operands is infinite: 0 * inf is a NaN */
    if (u.zero || v.zero) {
        return (u_neg ^ v_neg) ? neg_nan_val : pos_nan_val;
    }
    return (u_neg ^ v_neg) ? neg_inf_val : pos_inf_val;
}

/*
 * Parse SF/HF/QF16/QF32 into an unfloat
 */

unfloat parse_hf(int16_t in)
{
    INIT_UNFLOAT(out)
    float_status status = { 0 };
    uint16_t raw = in;

    out.exp = (raw >> hf_MANTBITS) & 0x1f;
    out.exp -= BIAS_HF;
    if (out.exp < E_MIN_HF) {
        out.exp = E_MIN_HF;
    }

    out.sign = raw >> 15;
    out.parts = float16_unpack_canonical(make_float16(in), &status);
    out.inf = out.parts.cls == float_class_inf;
    out.nan = out.parts.cls == float_class_qnan ||
              out.parts.cls == float_class_snan;
    out.zero = out.parts.cls == float_class_zero;
    out.parts.sign = false;
    return out;
}

static unfloat parse_sf(int32_t in)
{
    INIT_UNFLOAT(out)
    float_status status = { 0 };
    uint32_t raw = in;

    out.exp = (raw >> sf_MANTBITS) & 0xff;
    out.exp -= BIAS_SF;
    if (out.exp < E_MIN_SF) {
        out.exp = E_MIN_SF;
    }

    out.sign = raw >> 31;
    out.parts = float32_unpack_canonical(make_float32(in), &status);
    out.inf = out.parts.cls == float_class_inf;
    out.nan = out.parts.cls == float_class_qnan ||
              out.parts.cls == float_class_snan;
    out.zero = out.parts.cls == float_class_zero;
    out.parts.sign = false;
    return out;
}

unfloat parse_sf_daz(uint32_t in, bool daz_mode)
{
    unfloat a = parse_sf(in);

    if (daz_mode && (in & 0x7f800000) == 0 && (in & 0x007fffff)) {
        a.zero = true;
        a.parts = (FloatParts64) { .cls = float_class_zero };
    }
    return a;
}

unfloat parse_extqf16(uint16_t in, uint8_t in_ext)
{
    INIT_UNFLOAT(out)
    LREQ_t lreq = (in_ext & EXT16_BITMASK) << 2;
    uint32_t raw = ((uint32_t)in << 4) | (in_ext & EXT16_BITMASK);

    out.exp = (in & 0x1f) - BIAS_QF16;
    out.sign = !!(lreq & LREQ_R);
    out.inf = is_extqf16_inf(raw);
    out.nan = is_extqf16_nan(raw);
    out.parts = qfloat_unpack_ext_parts(16, in, in_ext);
    out.parts.sign ^= out.sign;
    out.zero = out.parts.cls == float_class_zero;
    return out;
}

unfloat parse_extqf32(uint32_t in, uint8_t in_ext)
{
    INIT_UNFLOAT(out)
    LREQ_t lreq = in_ext & EXT32_BITMASK;
    uint64_t raw = ((uint64_t)in << 4) | (in_ext & EXT32_BITMASK);
    int exponent = (in & 0xff) | (!!(lreq & LREQ_E) << 8);

    /* First step, sub 128 to get to IEEE-like; then sub 255 for qf32 bias */
    out.exp = ((exponent - 128) & 0x1ff) - 255;
    out.sign = !!(lreq & LREQ_R);
    out.inf = is_extqf32_inf(raw);
    out.nan = is_extqf32_nan(raw);
    out.parts = qfloat_unpack_ext_parts(32, in, in_ext);
    out.parts.sign ^= out.sign;
    out.zero = out.parts.cls == float_class_zero;
    return out;
}

/*
 * Convert QF16/QF32 to HF/SF/BF
 */

static FloatRoundMode qfrnd_to_float_round(qfrnd_mode_enum_t qfrnd)
{
    switch (qfrnd) {
    case RND_TO_NEAREST_EVEN:
        return float_round_nearest_even;
    case RND_TO_ZERO:
        return float_round_to_zero;
    case RND_TOWARDS_NEG_INF:
        return float_round_down;
    case RND_TOWARDS_POS_INF:
        return float_round_up;
    default:
        g_assert_not_reached();
    }
}

int32_t conv_sf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    uint64_t raw = ((uint64_t)a << 4) | (a_ext & EXT32_BITMASK);
    FloatParts64 parts;
    float_status status = { 0 };

    if (is_extqf32_nan(raw)) {
        return raw == extqf32_neg_nan || raw == extqf32_neg_nan_inexact ?
               ieee_pos_NaN_32 | 0x80000000 : ieee_pos_NaN_32;
    }
    if (is_extqf32_inf(raw)) {
        return raw == extqf32_neg_inf_exact ||
               raw == extqf32_neg_inf_inexact ?
               ieee_pos_inf_32 | 0x80000000 : ieee_pos_inf_32;
    }
    set_float_rounding_mode(qfrnd_to_float_round(qfrnd), &status);
    parts = qfloat_unpack_ext_parts(32, a, a_ext);
    qfloat_apply_inexact(&parts, a_ext);
    return float32_val(float32_round_pack_canonical(&parts, &status));
}

int16_t conv_hf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    uint64_t raw = ((uint64_t)a << 4) | (a_ext & EXT32_BITMASK);
    FloatParts64 parts;
    float_status status = { 0 };

    if (is_extqf32_nan(raw)) {
        return (int16_t)(raw == extqf32_neg_nan ||
                         raw == extqf32_neg_nan_inexact ?
                         ieee_pos_NaN_16 | 0x8000 : ieee_pos_NaN_16);
    }
    if (is_extqf32_inf(raw)) {
        return (int16_t)(raw == extqf32_neg_inf_exact ||
                         raw == extqf32_neg_inf_inexact ?
                         ieee_pos_inf_16 | 0x8000 : ieee_pos_inf_16);
    }
    set_float_rounding_mode(qfrnd_to_float_round(qfrnd), &status);
    parts = qfloat_unpack_ext_parts(32, a, a_ext);
    qfloat_apply_inexact(&parts, a_ext);
    return float16_val(float16_round_pack_canonical(&parts, &status));
}

int16_t conv_hf_extqf16(uint16_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    uint32_t raw = ((uint32_t)a << 4) | (a_ext & EXT16_BITMASK);
    FloatParts64 parts;
    float_status status = { 0 };

    if (is_extqf16_nan(raw)) {
        return (int16_t)(raw == extqf16_neg_nan ?
                         ieee_pos_NaN_16 | 0x8000 : ieee_pos_NaN_16);
    }
    if (is_extqf16_inf(raw)) {
        return (int16_t)(raw == extqf16_neg_inf ?
                         ieee_pos_inf_16 | 0x8000 : ieee_pos_inf_16);
    }
    set_float_rounding_mode(qfrnd_to_float_round(qfrnd), &status);
    parts = qfloat_unpack_ext_parts(16, a, a_ext);
    return float16_val(float16_round_pack_canonical(&parts, &status));
}

uint16_t conv_qf32_to_bf(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    uint64_t raw = ((uint64_t)a << 4) | (a_ext & EXT32_BITMASK);
    FloatParts64 parts;
    float_status status = { 0 };

    if (is_extqf32_nan(raw)) {
        return raw == extqf32_neg_nan || raw == extqf32_neg_nan_inexact ?
               BF_POS_NAN | 0x8000 : BF_POS_NAN;
    }
    if (is_extqf32_inf(raw)) {
        return raw == extqf32_neg_inf_exact ||
               raw == extqf32_neg_inf_inexact ? BF_POS_INF | 0x8000 :
                                                BF_POS_INF;
    }
    set_float_rounding_mode(qfrnd_to_float_round(qfrnd), &status);
    parts = qfloat_unpack_ext_parts(32, a, a_ext);
    qfloat_apply_inexact(&parts, a_ext);
    return bfloat16_round_pack_canonical(&parts, &status);
}

int qf_vilog2(f_type type, uint32_t a, uint8_t a_ext)
{
    unfloat u = { 0 };

    if (type == EXTQF32) {
        uint64_t raw = ((uint64_t)a << 4) | (a_ext & EXT32_BITMASK);
        FloatParts64 p;

        if (is_extqf32_nan(raw)) {
            return QF32_ILOG2_NAN_EXP;
        }
        if (is_extqf32_inf(raw)) {
            return QF32_ILOG2_INF_EXP;
        }
        p = qfloat_unpack_ext_parts(32, a, a_ext);
        return p.cls == float_class_zero ? QF32_ILOG2_ZERO_EXP : p.exp;
    }
    if (type == EXTQF16) {
        uint32_t raw = (a << 4) | (a_ext & EXT16_BITMASK);
        FloatParts64 p;

        if (is_extqf16_nan(raw)) {
            return QF16_ILOG2_NAN_EXP;
        }
        if (is_extqf16_inf(raw)) {
            return QF16_ILOG2_INF_EXP;
        }
        p = qfloat_unpack_ext_parts(16, a, a_ext);
        return p.cls == float_class_zero ? QF16_ILOG2_ZERO_EXP : p.exp;
    }

    switch (type) {
    case SF:
        u = parse_sf(a);
        break;
    case HF:
        u = parse_hf(a);
        break;
    default:
        g_assert_not_reached();
    }

    if (type == SF) {
        if (u.zero) {
            return QF32_ILOG2_ZERO_EXP;
        } else if (u.nan) {
            return QF32_ILOG2_NAN_EXP;
        } else if (u.inf) {
            return QF32_ILOG2_INF_EXP;
        }
    } else {
        if (u.zero) {
            return QF16_ILOG2_ZERO_EXP;
        } else if (u.nan) {
            return QF16_ILOG2_NAN_EXP;
        } else if (u.inf) {
            return QF16_ILOG2_INF_EXP;
        }
    }
    return u.parts.exp;
}

/*
 * USR field accessors
 */

static uint32_t get_usr_field(const CPUHexagonState *env, int field)
{
    return extract32(env->gpr[HEX_REG_USR], reg_field_info[field].offset,
                      reg_field_info[field].width);
}

bool is_daz_mode(const CPUHexagonState *env)
{
    const HexagonCPU *cpu = env_archcpu(env);

    return cpu->cfg.hex_def->hex_version >= HEX_VER_V81 &&
           get_usr_field(env, USR_FPDAZ);
}

bool qfloat_is_extended(const CPUHexagonState *env)
{
    const HexagonCPU *cpu = env_archcpu(env);

    return cpu->cfg.hex_def->hex_version >= HEX_VER_V79;
}

unfloat legacy_parse_qf32(int32_t in)
{
    unfloat out = { 0 };
    int32_t signif;
    uint32_t magnitude;

    out.sign = (uint32_t)in >> 31;
    out.exp = (in & 0xff) - BIAS_QF32;
    signif = sextract32(in, 7, 25) | 1;
    magnitude = signif < 0 ? -(uint32_t)signif : signif;
    out.parts = (FloatParts64) {
        .cls = float_class_normal,
        .exp = out.exp + 31 - clz32(magnitude) - sf_MANTBITS,
        .frac = (uint64_t)magnitude << (32 + clz32(magnitude)),
    };
    return out;
}

unfloat legacy_parse_qf16(int16_t in)
{
    unfloat out = { 0 };
    int16_t signif;
    uint16_t magnitude;

    out.sign = (uint16_t)in >> 15;
    out.exp = (in & 0x1f) - BIAS_QF16;
    signif = sextract32((uint16_t)in, 4, 12) | 1;
    magnitude = signif < 0 ? -(uint16_t)signif : signif;
    out.parts = (FloatParts64) {
        .cls = float_class_normal,
        .exp = out.exp + 31 - clz32(magnitude) - hf_MANTBITS,
        .frac = (uint64_t)magnitude << (32 + clz32(magnitude)),
    };
    return out;
}

static int legacy_add_exp(unfloat a, unfloat b)
{
    if (a.exp > b.exp) {
        return MAX(a.parts.exp, b.exp);
    }
    return MAX(b.parts.exp, a.exp);
}

/*
 * Return the legacy rounded significand numerator.  The result is N where
 * the rounded value is normally N * 2^-(frac + 1).  `half` selects the
 * legacy arm which uses N * 2^-(frac + 2) and increments the exponent.
 */
static int64_t legacy_round_n(FloatParts64 parts, int exp, int frac, int adj,
                              bool midpoint, bool *half)
{
    int shift = 65 + exp - parts.exp - frac + adj;
    uint64_t mask, low, threshold;
    int64_t base;

    g_assert(parts.cls == float_class_normal);
    if (shift >= 64) {
        return parts.sign ? -1 : 1;
    }
    if (shift <= 0) {
        uint64_t magnitude = parts.frac << -shift;
        int64_t value = parts.sign ? -(int64_t)magnitude : magnitude;

        return value;
    }

    mask = (UINT64_C(1) << shift) - 1;
    if (parts.sign) {
        uint64_t twos = -parts.frac;

        low = twos & mask;
        base = -(int64_t)((parts.frac + mask) >> shift) * 4;
    } else {
        low = parts.frac & mask;
        base = (parts.frac >> shift) * 4;
    }

    if (midpoint) {
        threshold = UINT64_C(3) << (shift - 3);
        if (low <= threshold) {
            return base + 1;
        }
        threshold = UINT64_C(5) << (shift - 3);
        if (low <= threshold) {
            *half = true;
            return base + 2;
        }
    } else {
        threshold = UINT64_C(1) << (shift - 1);
        if (low <= threshold) {
            return base + 1;
        }
    }
    return base + 3;
}

static uint32_t legacy_rnd_sat_qf(int size, int exp, FloatParts64 parts)
{
    int frac = size == 32 ? sf_MANTBITS : hf_MANTBITS;
    int emax = size == 32 ? 128 : 16;
    int emin = size == 32 ? -127 : -15;
    int bias = size == 32 ? BIAS_QF32 : BIAS_QF16;
    uint32_t mask = size == 32 ? 0x7fffff : 0x3ff;
    bool sign = parts.sign;
    bool exact_neg_two = sign && parts.exp == exp + 1 &&
                         parts.frac == UINT64_C(1) << 63;
    bool prod_ovf = parts.cls != float_class_zero &&
                    parts.exp >= exp + 1 && !exact_neg_two;
    bool half = false;
    int adj = 0;
    int64_t n;
    uint32_t sig_out;

    if (parts.cls == float_class_zero) {
        return 0;
    }
    if (exp >= emax + 1 || (prod_ovf && exp == emax)) {
        sig_out = (sign - 1) & mask;
        exp = emax + bias;
        goto pack;
    }
    if (exp <= emin - 2) {
        sig_out = -sign & mask;
        exp = emin + bias;
        goto pack;
    }
    adj = exp == emin - 1 || (prod_ovf && exp < emax);
    n = legacy_round_n(parts, exp, frac, adj,
                       !adj && exp >= emin && exp < emax, &half);
    sig_out = ((n < 0 ? -(uint64_t)n : (uint64_t)n) >>
               (half ? 2 : 1)) & mask;
    if (sign) {
        sig_out = ~sig_out;
    }
    exp += bias + adj + half;

pack:
    return (((sign << frac) | (sig_out & mask)) << (size == 32 ? 8 : 5)) |
           (exp & (size == 32 ? 0xff : 0x1f));
}

uint32_t legacy_qf_add(int size, unfloat a, unfloat b)
{
    float_status status = { 0 };
    FloatParts64 pa = qfloat_unfloat_parts(a);
    FloatParts64 pb = qfloat_unfloat_parts(b);
    FloatParts64 result = parts64_addsub(&pa, &pb, &status, false);

    return legacy_rnd_sat_qf(size, legacy_add_exp(a, b), result);
}

uint32_t legacy_qf_mpy(int size, unfloat a, unfloat b)
{
    float_status status = { 0 };
    FloatParts64 pa = qfloat_unfloat_parts(a);
    FloatParts64 pb = qfloat_unfloat_parts(b);
    FloatParts64 result = parts64_mul(&pa, &pb, &status);

    return legacy_rnd_sat_qf(size, a.exp + b.exp, result);
}

int32_t legacy_conv_sf_qf32(uint32_t a)
{
    unfloat value = legacy_parse_qf32(a);
    FloatParts64 parts = qfloat_unfloat_parts(value);
    float_status status = { 0 };

    return float32_val(float32_round_pack_canonical(&parts, &status));
}

int16_t legacy_conv_hf_qf32(uint32_t a)
{
    unfloat value = legacy_parse_qf32(a);
    FloatParts64 parts = qfloat_unfloat_parts(value);
    float_status status = { 0 };

    return float16_val(float16_round_pack_canonical(&parts, &status));
}

int16_t legacy_conv_hf_qf16(uint16_t a)
{
    unfloat value = legacy_parse_qf16(a);
    FloatParts64 parts = qfloat_unfloat_parts(value);
    float_status status = { 0 };

    return float16_val(float16_round_pack_canonical(&parts, &status));
}
