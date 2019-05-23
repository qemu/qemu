/*
 * QEMU TCG support -- s390x vector string instruction support
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
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"

/*
 * Returns a bit set in the MSB of each element that is zero,
 * as defined by the mask.
 */
static inline uint64_t zero_search(uint64_t a, uint64_t mask)
{
    return ~(((a & mask) + mask) | a | mask);
}

/*
 * Returns the byte offset for the first match, or 16 for no match.
 */
static inline int match_index(uint64_t c0, uint64_t c1)
{
    return (c0 ? clz64(c0) : clz64(c1) + 64) >> 3;
}

/*
 * Returns the number of bits composing one element.
 */
static uint8_t get_element_bits(uint8_t es)
{
    return (1 << es) * BITS_PER_BYTE;
}

/*
 * Returns the bitmask for a single element.
 */
static uint64_t get_single_element_mask(uint8_t es)
{
    return -1ull >> (64 - get_element_bits(es));
}

/*
 * Returns the bitmask for a single element (excluding the MSB).
 */
static uint64_t get_single_element_lsbs_mask(uint8_t es)
{
    return -1ull >> (65 - get_element_bits(es));
}

/*
 * Returns the bitmasks for multiple elements (excluding the MSBs).
 */
static uint64_t get_element_lsbs_mask(uint8_t es)
{
    return dup_const(es, get_single_element_lsbs_mask(es));
}

static int vfae(void *v1, const void *v2, const void *v3, bool in,
                bool rt, bool zs, uint8_t es)
{
    const uint64_t mask = get_element_lsbs_mask(es);
    const int bits = get_element_bits(es);
    uint64_t a0, a1, b0, b1, e0, e1, t0, t1, z0, z1;
    uint64_t first_zero = 16;
    uint64_t first_equal;
    int i;

    a0 = s390_vec_read_element64(v2, 0);
    a1 = s390_vec_read_element64(v2, 1);
    b0 = s390_vec_read_element64(v3, 0);
    b1 = s390_vec_read_element64(v3, 1);
    e0 = 0;
    e1 = 0;
    /* compare against equality with every other element */
    for (i = 0; i < 64; i += bits) {
        t0 = rol64(b0, i);
        t1 = rol64(b1, i);
        e0 |= zero_search(a0 ^ t0, mask);
        e0 |= zero_search(a0 ^ t1, mask);
        e1 |= zero_search(a1 ^ t0, mask);
        e1 |= zero_search(a1 ^ t1, mask);
    }
    /* invert the result if requested - invert only the MSBs */
    if (in) {
        e0 = ~e0 & ~mask;
        e1 = ~e1 & ~mask;
    }
    first_equal = match_index(e0, e1);

    if (zs) {
        z0 = zero_search(a0, mask);
        z1 = zero_search(a1, mask);
        first_zero = match_index(z0, z1);
    }

    if (rt) {
        e0 = (e0 >> (bits - 1)) * get_single_element_mask(es);
        e1 = (e1 >> (bits - 1)) * get_single_element_mask(es);
        s390_vec_write_element64(v1, 0, e0);
        s390_vec_write_element64(v1, 1, e1);
    } else {
        s390_vec_write_element64(v1, 0, MIN(first_equal, first_zero));
        s390_vec_write_element64(v1, 1, 0);
    }

    if (first_zero == 16 && first_equal == 16) {
        return 3; /* no match */
    } else if (first_zero == 16) {
        return 1; /* matching elements, no match for zero */
    } else if (first_equal < first_zero) {
        return 2; /* matching elements before match for zero */
    }
    return 0; /* match for zero */
}

#define DEF_VFAE_HELPER(BITS)                                                  \
void HELPER(gvec_vfae##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool rt = extract32(simd_data(desc), 2, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    vfae(v1, v2, v3, in, rt, zs, MO_##BITS);                                   \
}
DEF_VFAE_HELPER(8)
DEF_VFAE_HELPER(16)
DEF_VFAE_HELPER(32)

#define DEF_VFAE_CC_HELPER(BITS)                                               \
void HELPER(gvec_vfae_cc##BITS)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool rt = extract32(simd_data(desc), 2, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    env->cc_op = vfae(v1, v2, v3, in, rt, zs, MO_##BITS);                      \
}
DEF_VFAE_CC_HELPER(8)
DEF_VFAE_CC_HELPER(16)
DEF_VFAE_CC_HELPER(32)
