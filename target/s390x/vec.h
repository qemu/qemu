/*
 * QEMU TCG support -- s390x vector utilitites
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef S390X_VEC_H
#define S390X_VEC_H

#include "tcg/tcg.h"

typedef union S390Vector {
    uint64_t doubleword[2];
    uint32_t word[4];
    uint16_t halfword[8];
    uint8_t byte[16];
} S390Vector;

/*
 * Each vector is stored as two 64bit host values. So when talking about
 * byte/halfword/word numbers, we have to take care of proper translation
 * between element numbers.
 *
 * Big Endian (target/possible host)
 * B:  [ 0][ 1][ 2][ 3][ 4][ 5][ 6][ 7] - [ 8][ 9][10][11][12][13][14][15]
 * HW: [     0][     1][     2][     3] - [     4][     5][     6][     7]
 * W:  [             0][             1] - [             2][             3]
 * DW: [                             0] - [                             1]
 *
 * Little Endian (possible host)
 * B:  [ 7][ 6][ 5][ 4][ 3][ 2][ 1][ 0] - [15][14][13][12][11][10][ 9][ 8]
 * HW: [     3][     2][     1][     0] - [     7][     6][     5][     4]
 * W:  [             1][             0] - [             3][             2]
 * DW: [                             0] - [                             1]
 */
#ifndef HOST_WORDS_BIGENDIAN
#define H1(x)  ((x) ^ 7)
#define H2(x)  ((x) ^ 3)
#define H4(x)  ((x) ^ 1)
#else
#define H1(x)  (x)
#define H2(x)  (x)
#define H4(x)  (x)
#endif

static inline uint8_t s390_vec_read_element8(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 16);
    return v->byte[H1(enr)];
}

static inline uint16_t s390_vec_read_element16(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 8);
    return v->halfword[H2(enr)];
}

static inline uint32_t s390_vec_read_element32(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 4);
    return v->word[H4(enr)];
}

static inline uint64_t s390_vec_read_element64(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 2);
    return v->doubleword[enr];
}

static inline uint64_t s390_vec_read_element(const S390Vector *v, uint8_t enr,
                                             uint8_t es)
{
    switch (es) {
    case MO_8:
        return s390_vec_read_element8(v, enr);
    case MO_16:
        return s390_vec_read_element16(v, enr);
    case MO_32:
        return s390_vec_read_element32(v, enr);
    case MO_64:
        return s390_vec_read_element64(v, enr);
    default:
        g_assert_not_reached();
    }
}

static inline void s390_vec_write_element8(S390Vector *v, uint8_t enr,
                                           uint8_t data)
{
    g_assert(enr < 16);
    v->byte[H1(enr)] = data;
}

static inline void s390_vec_write_element16(S390Vector *v, uint8_t enr,
                                            uint16_t data)
{
    g_assert(enr < 8);
    v->halfword[H2(enr)] = data;
}

static inline void s390_vec_write_element32(S390Vector *v, uint8_t enr,
                                            uint32_t data)
{
    g_assert(enr < 4);
    v->word[H4(enr)] = data;
}

static inline void s390_vec_write_element64(S390Vector *v, uint8_t enr,
                                            uint64_t data)
{
    g_assert(enr < 2);
    v->doubleword[enr] = data;
}

static inline void s390_vec_write_element(S390Vector *v, uint8_t enr,
                                          uint8_t es, uint64_t data)
{
    switch (es) {
    case MO_8:
        s390_vec_write_element8(v, enr, data);
        break;
    case MO_16:
        s390_vec_write_element16(v, enr, data);
        break;
    case MO_32:
        s390_vec_write_element32(v, enr, data);
        break;
    case MO_64:
        s390_vec_write_element64(v, enr, data);
        break;
    default:
        g_assert_not_reached();
    }
}

#endif /* S390X_VEC_H */
