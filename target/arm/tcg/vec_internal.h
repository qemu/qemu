/*
 * ARM AdvSIMD / SVE Vector Helpers
 *
 * Copyright (c) 2020 Linaro
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARM_VEC_INTERNAL_H
#define TARGET_ARM_VEC_INTERNAL_H

#include "fpu/softfloat.h"

typedef struct CPUArchState CPUARMState;

/*
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing units smaller than that needs a host-endian fixup.
 *
 * The H<N> macros are used when indexing an array of elements of size N.
 *
 * The H1_<N> macros are used when performing byte arithmetic and then
 * casting the final pointer to a type of size N.
 */
#if HOST_BIG_ENDIAN
#define H1(x)   ((x) ^ 7)
#define H1_2(x) ((x) ^ 6)
#define H1_4(x) ((x) ^ 4)
#define H2(x)   ((x) ^ 3)
#define H4(x)   ((x) ^ 1)
#else
#define H1(x)   (x)
#define H1_2(x) (x)
#define H1_4(x) (x)
#define H2(x)   (x)
#define H4(x)   (x)
#endif
/*
 * Access to 64-bit elements isn't host-endian dependent; we provide H8
 * and H1_8 so that when a function is being generated from a macro we
 * can pass these rather than an empty macro argument, for clarity.
 */
#define H8(x)   (x)
#define H1_8(x) (x)

/*
 * Expand active predicate bits to bytes, for byte elements.
 */
extern const uint64_t expand_pred_b_data[256];
static inline uint64_t expand_pred_b(uint8_t byte)
{
    return expand_pred_b_data[byte];
}

/* Similarly for half-word elements. */
extern const uint64_t expand_pred_h_data[0x55 + 1];
static inline uint64_t expand_pred_h(uint8_t byte)
{
    return expand_pred_h_data[byte & 0x55];
}

static inline void clear_tail(void *vd, uintptr_t opr_sz, uintptr_t max_sz)
{
    uint64_t *d = vd + opr_sz;
    uintptr_t i;

    for (i = opr_sz; i < max_sz; i += 8) {
        *d++ = 0;
    }
}

static inline int32_t do_sqrshl_bhs(int32_t src, int32_t shift, int bits,
                                    bool round, uint32_t *sat)
{
    if (shift <= -bits) {
        /* Rounding the sign bit always produces 0. */
        if (round) {
            return 0;
        }
        return src >> 31;
    } else if (shift < 0) {
        if (round) {
            src >>= -shift - 1;
            return (src >> 1) + (src & 1);
        }
        return src >> -shift;
    } else if (shift < bits) {
        int32_t val = src << shift;
        if (bits == 32) {
            if (!sat || val >> shift == src) {
                return val;
            }
        } else {
            int32_t extval = sextract32(val, 0, bits);
            if (!sat || val == extval) {
                return extval;
            }
        }
    } else if (!sat || src == 0) {
        return 0;
    }

    *sat = 1;
    return (1u << (bits - 1)) - (src >= 0);
}

static inline uint32_t do_uqrshl_bhs(uint32_t src, int32_t shift, int bits,
                                     bool round, uint32_t *sat)
{
    if (shift <= -(bits + round)) {
        return 0;
    } else if (shift < 0) {
        if (round) {
            src >>= -shift - 1;
            return (src >> 1) + (src & 1);
        }
        return src >> -shift;
    } else if (shift < bits) {
        uint32_t val = src << shift;
        if (bits == 32) {
            if (!sat || val >> shift == src) {
                return val;
            }
        } else {
            uint32_t extval = extract32(val, 0, bits);
            if (!sat || val == extval) {
                return extval;
            }
        }
    } else if (!sat || src == 0) {
        return 0;
    }

    *sat = 1;
    return MAKE_64BIT_MASK(0, bits);
}

static inline int32_t do_suqrshl_bhs(int32_t src, int32_t shift, int bits,
                                     bool round, uint32_t *sat)
{
    if (sat && src < 0) {
        *sat = 1;
        return 0;
    }
    return do_uqrshl_bhs(src, shift, bits, round, sat);
}

static inline int64_t do_sqrshl_d(int64_t src, int64_t shift,
                                  bool round, uint32_t *sat)
{
    if (shift <= -64) {
        /* Rounding the sign bit always produces 0. */
        if (round) {
            return 0;
        }
        return src >> 63;
    } else if (shift < 0) {
        if (round) {
            src >>= -shift - 1;
            return (src >> 1) + (src & 1);
        }
        return src >> -shift;
    } else if (shift < 64) {
        int64_t val = src << shift;
        if (!sat || val >> shift == src) {
            return val;
        }
    } else if (!sat || src == 0) {
        return 0;
    }

    *sat = 1;
    return src < 0 ? INT64_MIN : INT64_MAX;
}

static inline uint64_t do_uqrshl_d(uint64_t src, int64_t shift,
                                   bool round, uint32_t *sat)
{
    if (shift <= -(64 + round)) {
        return 0;
    } else if (shift < 0) {
        if (round) {
            src >>= -shift - 1;
            return (src >> 1) + (src & 1);
        }
        return src >> -shift;
    } else if (shift < 64) {
        uint64_t val = src << shift;
        if (!sat || val >> shift == src) {
            return val;
        }
    } else if (!sat || src == 0) {
        return 0;
    }

    *sat = 1;
    return UINT64_MAX;
}

static inline int64_t do_suqrshl_d(int64_t src, int64_t shift,
                                   bool round, uint32_t *sat)
{
    if (sat && src < 0) {
        *sat = 1;
        return 0;
    }
    return do_uqrshl_d(src, shift, round, sat);
}

int8_t do_sqrdmlah_b(int8_t, int8_t, int8_t, bool, bool);
int16_t do_sqrdmlah_h(int16_t, int16_t, int16_t, bool, bool, uint32_t *);
int32_t do_sqrdmlah_s(int32_t, int32_t, int32_t, bool, bool, uint32_t *);
int64_t do_sqrdmlah_d(int64_t, int64_t, int64_t, bool, bool);

#define do_ssat_b(val)  MIN(MAX(val, INT8_MIN), INT8_MAX)
#define do_ssat_h(val)  MIN(MAX(val, INT16_MIN), INT16_MAX)
#define do_ssat_s(val)  MIN(MAX(val, INT32_MIN), INT32_MAX)
#define do_usat_b(val)  MIN(MAX(val, 0), UINT8_MAX)
#define do_usat_h(val)  MIN(MAX(val, 0), UINT16_MAX)
#define do_usat_s(val)  MIN(MAX(val, 0), UINT32_MAX)

static inline uint64_t do_urshr(uint64_t x, unsigned sh)
{
    if (likely(sh < 64)) {
        return (x >> sh) + ((x >> (sh - 1)) & 1);
    } else if (sh == 64) {
        return x >> 63;
    } else {
        return 0;
    }
}

static inline int64_t do_srshr(int64_t x, unsigned sh)
{
    if (likely(sh < 64)) {
        return (x >> sh) + ((x >> (sh - 1)) & 1);
    } else {
        /* Rounding the sign bit always produces 0. */
        return 0;
    }
}

/**
 * bfdotadd:
 * @sum: addend
 * @e1, @e2: multiplicand vectors
 * @fpst: floating-point status to use
 *
 * BFloat16 2-way dot product of @e1 & @e2, accumulating with @sum.
 * The @e1 and @e2 operands correspond to the 32-bit source vector
 * slots and contain two Bfloat16 values each.
 *
 * Corresponds to the ARM pseudocode function BFDotAdd, specialized
 * for the FPCR.EBF == 0 case.
 */
float32 bfdotadd(float32 sum, uint32_t e1, uint32_t e2, float_status *fpst);
/**
 * bfdotadd_ebf:
 * @sum: addend
 * @e1, @e2: multiplicand vectors
 * @fpst: floating-point status to use
 * @fpst_odd: floating-point status to use for round-to-odd operations
 *
 * BFloat16 2-way dot product of @e1 & @e2, accumulating with @sum.
 * The @e1 and @e2 operands correspond to the 32-bit source vector
 * slots and contain two Bfloat16 values each.
 *
 * Corresponds to the ARM pseudocode function BFDotAdd, specialized
 * for the FPCR.EBF == 1 case.
 */
float32 bfdotadd_ebf(float32 sum, uint32_t e1, uint32_t e2,
                     float_status *fpst, float_status *fpst_odd);

/**
 * is_ebf:
 * @env: CPU state
 * @statusp: pointer to floating point status to fill in
 * @oddstatusp: pointer to floating point status to fill in for round-to-odd
 *
 * Determine whether a BFDotAdd operation should use FPCR.EBF = 0
 * or FPCR.EBF = 1 semantics. On return, has initialized *statusp
 * and *oddstatusp to suitable float_status arguments to use with either
 * bfdotadd() or bfdotadd_ebf().
 * Returns true for EBF = 1, false for EBF = 0. (The caller should use this
 * to decide whether to call bfdotadd() or bfdotadd_ebf().)
 */
bool is_ebf(CPUARMState *env, float_status *statusp, float_status *oddstatusp);

/*
 * Negate as for FPCR.AH=1 -- do not negate NaNs.
 */
static inline float16 bfloat16_ah_chs(float16 a)
{
    return bfloat16_is_any_nan(a) ? a : bfloat16_chs(a);
}

static inline float16 float16_ah_chs(float16 a)
{
    return float16_is_any_nan(a) ? a : float16_chs(a);
}

static inline float32 float32_ah_chs(float32 a)
{
    return float32_is_any_nan(a) ? a : float32_chs(a);
}

static inline float64 float64_ah_chs(float64 a)
{
    return float64_is_any_nan(a) ? a : float64_chs(a);
}

static inline float16 float16_maybe_ah_chs(float16 a, bool fpcr_ah)
{
    return fpcr_ah && float16_is_any_nan(a) ? a : float16_chs(a);
}

static inline float32 float32_maybe_ah_chs(float32 a, bool fpcr_ah)
{
    return fpcr_ah && float32_is_any_nan(a) ? a : float32_chs(a);
}

static inline float64 float64_maybe_ah_chs(float64 a, bool fpcr_ah)
{
    return fpcr_ah && float64_is_any_nan(a) ? a : float64_chs(a);
}

/* Not actually called directly as a helper, but uses similar machinery. */
bfloat16 helper_sme2_ah_fmax_b16(bfloat16 a, bfloat16 b, float_status *fpst);
bfloat16 helper_sme2_ah_fmin_b16(bfloat16 a, bfloat16 b, float_status *fpst);

float32 sve_f16_to_f32(float16 f, float_status *fpst);
float16 sve_f32_to_f16(float32 f, float_status *fpst);

/*
 * Decode helper functions for predicate as counter.
 */

typedef struct {
    unsigned count;
    unsigned lg2_stride;
    bool invert;
} DecodeCounter;

static inline DecodeCounter
decode_counter(unsigned png, unsigned vl, unsigned v_esz)
{
    DecodeCounter ret = { };

    /* C.f. Arm pseudocode CounterToPredicate. */
    if (likely(png & 0xf)) {
        unsigned p_esz = ctz32(png);

        /*
         * maxbit = log2(pl(bits) * 4)
         *        = log2(vl(bytes) * 4)
         *        = log2(vl) + 2
         * maxbit_mask = ones<maxbit:0>
         *             = (1 << (maxbit + 1)) - 1
         *             = (1 << (log2(vl) + 2 + 1)) - 1
         *             = (1 << (log2(vl) + 3)) - 1
         *             = (pow2ceil(vl) << 3) - 1
         */
        ret.count = png & (((unsigned)pow2ceil(vl) << 3) - 1);
        ret.count >>= p_esz + 1;

        ret.invert = (png >> 15) & 1;

        /*
         * The Arm pseudocode for CounterToPredicate expands the count to
         * a set of bits, and then the operation proceeds as for the original
         * interpretation of predicates as a set of bits.
         *
         * We can avoid the expansion by adjusting the count and supplying
         * an element stride.
         */
        if (unlikely(p_esz != v_esz)) {
            if (p_esz < v_esz) {
                /*
                 * For predicate esz < vector esz, the expanded predicate
                 * will have more bits set than will be consumed.
                 * Adjust the count down, rounding up.
                 * Consider p_esz = MO_8, v_esz = MO_64, count 14:
                 * The expanded predicate would be
                 *    0011 1111 1111 1111
                 * The significant bits are
                 *    ...1 ...1 ...1 ...1
                 */
                unsigned shift = v_esz - p_esz;
                unsigned trunc = ret.count >> shift;
                ret.count = trunc + (ret.count != (trunc << shift));
            } else {
                /*
                 * For predicate esz > vector esz, the expanded predicate
                 * will have bits set only at power-of-two multiples of
                 * the vector esz.  Bits at other multiples will all be
                 * false.  Adjust the count up, and supply the caller
                 * with a stride of elements to skip.
                 */
                unsigned shift = p_esz - v_esz;
                ret.count <<= shift;
                ret.lg2_stride = shift;
            }
        }
    }
    return ret;
}

/* Extract @len bits from an array of uint64_t at offset @pos bits. */
static inline uint64_t extractn(uint64_t *p, unsigned pos, unsigned len)
{
    uint64_t x;

    p += pos / 64;
    pos = pos % 64;

    x = p[0];
    if (pos + len > 64) {
        x = (x >> pos) | (p[1] << (-pos & 63));
        pos = 0;
    }
    return extract64(x, pos, len);
}

/* Deposit @len bits into an array of uint64_t at offset @pos bits. */
static inline void depositn(uint64_t *p, unsigned pos,
                            unsigned len, uint64_t val)
{
    p += pos / 64;
    pos = pos % 64;

    if (pos + len <= 64) {
        p[0] = deposit64(p[0], pos, len, val);
    } else {
        unsigned len0 = 64 - pos;
        unsigned len1 = len - len0;

        p[0] = deposit64(p[0], pos, len0, val);
        p[1] = deposit64(p[1], 0, len1, val >> len0);
    }
}

#endif /* TARGET_ARM_VEC_INTERNAL_H */
