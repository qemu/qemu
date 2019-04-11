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
