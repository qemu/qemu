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

/**
 * bfdotadd:
 * @sum: addend
 * @e1, @e2: multiplicand vectors
 *
 * BFloat16 2-way dot product of @e1 & @e2, accumulating with @sum.
 * The @e1 and @e2 operands correspond to the 32-bit source vector
 * slots and contain two Bfloat16 values each.
 *
 * Corresponds to the ARM pseudocode function BFDotAdd.
 */
float32 bfdotadd(float32 sum, uint32_t e1, uint32_t e2);

#endif /* TARGET_ARM_VEC_INTERNAL_H */
