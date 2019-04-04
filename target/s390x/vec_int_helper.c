/*
 * QEMU TCG support -- s390x vector integer instruction support
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
#include "vec.h"
#include "exec/helper-proto.h"

#define DEF_VAVG(BITS)                                                         \
void HELPER(gvec_vavg##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const int32_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, i);   \
        const int32_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, i);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVG(8)
DEF_VAVG(16)

#define DEF_VAVGL(BITS)                                                        \
void HELPER(gvec_vavgl##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVGL(8)
DEF_VAVGL(16)

#define DEF_VCLZ(BITS)                                                         \
void HELPER(gvec_vclz##BITS)(void *v1, const void *v2, uint32_t desc)          \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, clz32(a) - 32 + BITS);             \
    }                                                                          \
}
DEF_VCLZ(8)
DEF_VCLZ(16)
