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
#include "cpu.h"
#include "s390x-internal.h"
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
 * Returns a bit set in the MSB of each element that is not zero,
 * as defined by the mask.
 */
static inline uint64_t nonzero_search(uint64_t a, uint64_t mask)
{
    return (((a & mask) + mask) | a) & ~mask;
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

static int vfee(void *v1, const void *v2, const void *v3, bool zs, uint8_t es)
{
    const uint64_t mask = get_element_lsbs_mask(es);
    uint64_t a0, a1, b0, b1, e0, e1, z0, z1;
    uint64_t first_zero = 16;
    uint64_t first_equal;

    a0 = s390_vec_read_element64(v2, 0);
    a1 = s390_vec_read_element64(v2, 1);
    b0 = s390_vec_read_element64(v3, 0);
    b1 = s390_vec_read_element64(v3, 1);
    e0 = zero_search(a0 ^ b0, mask);
    e1 = zero_search(a1 ^ b1, mask);
    first_equal = match_index(e0, e1);

    if (zs) {
        z0 = zero_search(a0, mask);
        z1 = zero_search(a1, mask);
        first_zero = match_index(z0, z1);
    }

    s390_vec_write_element64(v1, 0, MIN(first_equal, first_zero));
    s390_vec_write_element64(v1, 1, 0);
    if (first_zero == 16 && first_equal == 16) {
        return 3; /* no match */
    } else if (first_zero == 16) {
        return 1; /* matching elements, no match for zero */
    } else if (first_equal < first_zero) {
        return 2; /* matching elements before match for zero */
    }
    return 0; /* match for zero */
}

#define DEF_VFEE_HELPER(BITS)                                                  \
void HELPER(gvec_vfee##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    vfee(v1, v2, v3, zs, MO_##BITS);                                           \
}
DEF_VFEE_HELPER(8)
DEF_VFEE_HELPER(16)
DEF_VFEE_HELPER(32)

#define DEF_VFEE_CC_HELPER(BITS)                                               \
void HELPER(gvec_vfee_cc##BITS)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    env->cc_op = vfee(v1, v2, v3, zs, MO_##BITS);                              \
}
DEF_VFEE_CC_HELPER(8)
DEF_VFEE_CC_HELPER(16)
DEF_VFEE_CC_HELPER(32)

static int vfene(void *v1, const void *v2, const void *v3, bool zs, uint8_t es)
{
    const uint64_t mask = get_element_lsbs_mask(es);
    uint64_t a0, a1, b0, b1, e0, e1, z0, z1;
    uint64_t first_zero = 16;
    uint64_t first_inequal;
    bool smaller = false;

    a0 = s390_vec_read_element64(v2, 0);
    a1 = s390_vec_read_element64(v2, 1);
    b0 = s390_vec_read_element64(v3, 0);
    b1 = s390_vec_read_element64(v3, 1);
    e0 = nonzero_search(a0 ^ b0, mask);
    e1 = nonzero_search(a1 ^ b1, mask);
    first_inequal = match_index(e0, e1);

    /* identify the smaller element */
    if (first_inequal < 16) {
        uint8_t enr = first_inequal / (1 << es);
        uint32_t a = s390_vec_read_element(v2, enr, es);
        uint32_t b = s390_vec_read_element(v3, enr, es);

        smaller = a < b;
    }

    if (zs) {
        z0 = zero_search(a0, mask);
        z1 = zero_search(a1, mask);
        first_zero = match_index(z0, z1);
    }

    s390_vec_write_element64(v1, 0, MIN(first_inequal, first_zero));
    s390_vec_write_element64(v1, 1, 0);
    if (first_zero == 16 && first_inequal == 16) {
        return 3;
    } else if (first_zero < first_inequal) {
        return 0;
    }
    return smaller ? 1 : 2;
}

#define DEF_VFENE_HELPER(BITS)                                                 \
void HELPER(gvec_vfene##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    vfene(v1, v2, v3, zs, MO_##BITS);                                          \
}
DEF_VFENE_HELPER(8)
DEF_VFENE_HELPER(16)
DEF_VFENE_HELPER(32)

#define DEF_VFENE_CC_HELPER(BITS)                                              \
void HELPER(gvec_vfene_cc##BITS)(void *v1, const void *v2, const void *v3,     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    env->cc_op = vfene(v1, v2, v3, zs, MO_##BITS);                             \
}
DEF_VFENE_CC_HELPER(8)
DEF_VFENE_CC_HELPER(16)
DEF_VFENE_CC_HELPER(32)

static int vistr(void *v1, const void *v2, uint8_t es)
{
    const uint64_t mask = get_element_lsbs_mask(es);
    uint64_t a0 = s390_vec_read_element64(v2, 0);
    uint64_t a1 = s390_vec_read_element64(v2, 1);
    uint64_t z;
    int cc = 3;

    z = zero_search(a0, mask);
    if (z) {
        a0 &= ~(-1ull >> clz64(z));
        a1 = 0;
        cc = 0;
    } else {
        z = zero_search(a1, mask);
        if (z) {
            a1 &= ~(-1ull >> clz64(z));
            cc = 0;
        }
    }

    s390_vec_write_element64(v1, 0, a0);
    s390_vec_write_element64(v1, 1, a1);
    return cc;
}

#define DEF_VISTR_HELPER(BITS)                                                 \
void HELPER(gvec_vistr##BITS)(void *v1, const void *v2, uint32_t desc)         \
{                                                                              \
    vistr(v1, v2, MO_##BITS);                                                  \
}
DEF_VISTR_HELPER(8)
DEF_VISTR_HELPER(16)
DEF_VISTR_HELPER(32)

#define DEF_VISTR_CC_HELPER(BITS)                                              \
void HELPER(gvec_vistr_cc##BITS)(void *v1, const void *v2, CPUS390XState *env, \
                                uint32_t desc)                                 \
{                                                                              \
    env->cc_op = vistr(v1, v2, MO_##BITS);                                     \
}
DEF_VISTR_CC_HELPER(8)
DEF_VISTR_CC_HELPER(16)
DEF_VISTR_CC_HELPER(32)

static bool element_compare(uint32_t data, uint32_t l, uint8_t c)
{
    const bool equal = extract32(c, 7, 1);
    const bool lower = extract32(c, 6, 1);
    const bool higher = extract32(c, 5, 1);

    if (data < l) {
        return lower;
    } else if (data > l) {
        return higher;
    }
    return equal;
}

static int vstrc(void *v1, const void *v2, const void *v3, const void *v4,
                 bool in, bool rt, bool zs, uint8_t es)
{
    const uint64_t mask = get_element_lsbs_mask(es);
    uint64_t a0 = s390_vec_read_element64(v2, 0);
    uint64_t a1 = s390_vec_read_element64(v2, 1);
    int first_zero = 16, first_match = 16;
    S390Vector rt_result = {};
    uint64_t z0, z1;
    int i, j;

    if (zs) {
        z0 = zero_search(a0, mask);
        z1 = zero_search(a1, mask);
        first_zero = match_index(z0, z1);
    }

    for (i = 0; i < 16 / (1 << es); i++) {
        const uint32_t data = s390_vec_read_element(v2, i, es);
        const int cur_byte = i * (1 << es);
        bool any_match = false;

        /* if we don't need a bit vector, we can stop early */
        if (cur_byte == first_zero && !rt) {
            break;
        }

        for (j = 0; j < 16 / (1 << es); j += 2) {
            const uint32_t l1 = s390_vec_read_element(v3, j, es);
            const uint32_t l2 = s390_vec_read_element(v3, j + 1, es);
            /* we are only interested in the highest byte of each element */
            const uint8_t c1 = s390_vec_read_element8(v4, j * (1 << es));
            const uint8_t c2 = s390_vec_read_element8(v4, (j + 1) * (1 << es));

            if (element_compare(data, l1, c1) &&
                element_compare(data, l2, c2)) {
                any_match = true;
                break;
            }
        }
        /* invert the result if requested */
        any_match = in ^ any_match;

        if (any_match) {
            /* indicate bit vector if requested */
            if (rt) {
                const uint64_t val = -1ull;

                first_match = MIN(cur_byte, first_match);
                s390_vec_write_element(&rt_result, i, es, val);
            } else {
                /* stop on the first match */
                first_match = cur_byte;
                break;
            }
        }
    }

    if (rt) {
        *(S390Vector *)v1 = rt_result;
    } else {
        s390_vec_write_element64(v1, 0, MIN(first_match, first_zero));
        s390_vec_write_element64(v1, 1, 0);
    }

    if (first_zero == 16 && first_match == 16) {
        return 3; /* no match */
    } else if (first_zero == 16) {
        return 1; /* matching elements, no match for zero */
    } else if (first_match < first_zero) {
        return 2; /* matching elements before match for zero */
    }
    return 0; /* match for zero */
}

#define DEF_VSTRC_HELPER(BITS)                                                 \
void HELPER(gvec_vstrc##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, uint32_t desc)                   \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    vstrc(v1, v2, v3, v4, in, 0, zs, MO_##BITS);                               \
}
DEF_VSTRC_HELPER(8)
DEF_VSTRC_HELPER(16)
DEF_VSTRC_HELPER(32)

#define DEF_VSTRC_RT_HELPER(BITS)                                              \
void HELPER(gvec_vstrc_rt##BITS)(void *v1, const void *v2, const void *v3,     \
                                 const void *v4, uint32_t desc)                \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    vstrc(v1, v2, v3, v4, in, 1, zs, MO_##BITS);                               \
}
DEF_VSTRC_RT_HELPER(8)
DEF_VSTRC_RT_HELPER(16)
DEF_VSTRC_RT_HELPER(32)

#define DEF_VSTRC_CC_HELPER(BITS)                                              \
void HELPER(gvec_vstrc_cc##BITS)(void *v1, const void *v2, const void *v3,     \
                                 const void *v4, CPUS390XState *env,           \
                                 uint32_t desc)                                \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    env->cc_op = vstrc(v1, v2, v3, v4, in, 0, zs, MO_##BITS);                  \
}
DEF_VSTRC_CC_HELPER(8)
DEF_VSTRC_CC_HELPER(16)
DEF_VSTRC_CC_HELPER(32)

#define DEF_VSTRC_CC_RT_HELPER(BITS)                                           \
void HELPER(gvec_vstrc_cc_rt##BITS)(void *v1, const void *v2, const void *v3,  \
                                    const void *v4, CPUS390XState *env,        \
                                    uint32_t desc)                             \
{                                                                              \
    const bool in = extract32(simd_data(desc), 3, 1);                          \
    const bool zs = extract32(simd_data(desc), 1, 1);                          \
                                                                               \
    env->cc_op = vstrc(v1, v2, v3, v4, in, 1, zs, MO_##BITS);                  \
}
DEF_VSTRC_CC_RT_HELPER(8)
DEF_VSTRC_CC_RT_HELPER(16)
DEF_VSTRC_CC_RT_HELPER(32)

static int vstrs(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                 const S390Vector *v4, uint8_t es, bool zs)
{
    int substr_elen, substr_0, str_elen, i, j, k, cc;
    int nelem = 16 >> es;
    bool eos = false;

    substr_elen = s390_vec_read_element8(v4, 7) >> es;

    /* If ZS, bound substr length by min(nelem, strlen(v3)). */
    if (zs) {
        substr_elen = MIN(substr_elen, nelem);
        for (i = 0; i < substr_elen; i++) {
            if (s390_vec_read_element(v3, i, es) == 0) {
                substr_elen = i;
                break;
            }
        }
    }

    if (substr_elen == 0) {
        cc = 2; /* full match for degenerate case of empty substr */
        k = 0;
        goto done;
    }

    /* If ZS, look for eos in the searched string. */
    if (zs) {
        for (k = 0; k < nelem; k++) {
            if (s390_vec_read_element(v2, k, es) == 0) {
                eos = true;
                break;
            }
        }
        str_elen = k;
    } else {
        str_elen = nelem;
    }

    substr_0 = s390_vec_read_element(v3, 0, es);

    for (k = 0; ; k++) {
        for (; k < str_elen; k++) {
            if (s390_vec_read_element(v2, k, es) == substr_0) {
                break;
            }
        }

        /* If we reached the end of the string, no match. */
        if (k == str_elen) {
            cc = eos; /* no match (with or without zero char) */
            goto done;
        }

        /* If the substring is only one char, match. */
        if (substr_elen == 1) {
            cc = 2; /* full match */
            goto done;
        }

        /* If the match begins at the last char, we have a partial match. */
        if (k == str_elen - 1) {
            cc = 3; /* partial match */
            goto done;
        }

        i = MIN(nelem, k + substr_elen);
        for (j = k + 1; j < i; j++) {
            uint32_t e2 = s390_vec_read_element(v2, j, es);
            uint32_t e3 = s390_vec_read_element(v3, j - k, es);
            if (e2 != e3) {
                break;
            }
        }
        if (j == i) {
            /* Matched up until "end". */
            cc = i - k == substr_elen ? 2 : 3; /* full or partial match */
            goto done;
        }
    }

 done:
    s390_vec_write_element64(v1, 0, k << es);
    s390_vec_write_element64(v1, 1, 0);
    return cc;
}

#define DEF_VSTRS_HELPER(BITS)                                             \
void QEMU_FLATTEN HELPER(gvec_vstrs_##BITS)(void *v1, const void *v2,      \
    const void *v3, const void *v4, CPUS390XState *env, uint32_t desc)     \
    { env->cc_op = vstrs(v1, v2, v3, v4, MO_##BITS, false); }              \
void QEMU_FLATTEN HELPER(gvec_vstrs_zs##BITS)(void *v1, const void *v2,    \
    const void *v3, const void *v4, CPUS390XState *env, uint32_t desc)     \
    { env->cc_op = vstrs(v1, v2, v3, v4, MO_##BITS, true); }

DEF_VSTRS_HELPER(8)
DEF_VSTRS_HELPER(16)
DEF_VSTRS_HELPER(32)
