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

#ifndef TARGET_ARM_VEC_INTERNALS_H
#define TARGET_ARM_VEC_INTERNALS_H

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

#endif /* TARGET_ARM_VEC_INTERNALS_H */
