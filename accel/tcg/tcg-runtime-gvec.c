/*
 * Generic vectorized operation runtime
 *
 * Copyright (c) 2018 Linaro
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "tcg-gvec-desc.h"


/* Virtually all hosts support 16-byte vectors.  Those that don't can emulate
 * them via GCC's generic vector extension.  This turns out to be simpler and
 * more reliable than getting the compiler to autovectorize.
 *
 * In tcg-op-gvec.c, we asserted that both the size and alignment of the data
 * are multiples of 16.
 *
 * When the compiler does not support all of the operations we require, the
 * loops are written so that we can always fall back on the base types.
 */
#ifdef CONFIG_VECTOR16
typedef uint8_t vec8 __attribute__((vector_size(16)));
typedef uint16_t vec16 __attribute__((vector_size(16)));
typedef uint32_t vec32 __attribute__((vector_size(16)));
typedef uint64_t vec64 __attribute__((vector_size(16)));

typedef int8_t svec8 __attribute__((vector_size(16)));
typedef int16_t svec16 __attribute__((vector_size(16)));
typedef int32_t svec32 __attribute__((vector_size(16)));
typedef int64_t svec64 __attribute__((vector_size(16)));

#define DUP16(X)  { X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X }
#define DUP8(X)   { X, X, X, X, X, X, X, X }
#define DUP4(X)   { X, X, X, X }
#define DUP2(X)   { X, X }
#else
typedef uint8_t vec8;
typedef uint16_t vec16;
typedef uint32_t vec32;
typedef uint64_t vec64;

typedef int8_t svec8;
typedef int16_t svec16;
typedef int32_t svec32;
typedef int64_t svec64;

#define DUP16(X)  X
#define DUP8(X)   X
#define DUP4(X)   X
#define DUP2(X)   X
#endif /* CONFIG_VECTOR16 */

static inline void clear_high(void *d, intptr_t oprsz, uint32_t desc)
{
    intptr_t maxsz = simd_maxsz(desc);
    intptr_t i;

    if (unlikely(maxsz > oprsz)) {
        for (i = oprsz; i < maxsz; i += sizeof(uint64_t)) {
            *(uint64_t *)(d + i) = 0;
        }
    }
}

void HELPER(gvec_add8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) + *(vec8 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) + *(vec16 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) + *(vec32 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) + *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec8 vecb = (vec8)DUP16(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) + vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec16 vecb = (vec16)DUP8(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) + vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec32 vecb = (vec32)DUP4(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) + vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) + vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) - *(vec8 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) - *(vec16 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) - *(vec32 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) - *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec8 vecb = (vec8)DUP16(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) - vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec16 vecb = (vec16)DUP8(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) - vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec32 vecb = (vec32)DUP4(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) - vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) - vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) * *(vec8 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) * *(vec16 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) * *(vec32 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) * *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec8 vecb = (vec8)DUP16(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) * vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec16 vecb = (vec16)DUP8(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) * vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec32 vecb = (vec32)DUP4(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) * vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) * vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg8)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = -*(vec8 *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg16)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = -*(vec16 *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg32)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = -*(vec32 *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg64)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = -*(vec64 *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mov)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);

    memcpy(d, a, oprsz);
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_dup64)(void *d, uint32_t desc, uint64_t c)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    if (c == 0) {
        oprsz = 0;
    } else {
        for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
            *(uint64_t *)(d + i) = c;
        }
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_dup32)(void *d, uint32_t desc, uint32_t c)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    if (c == 0) {
        oprsz = 0;
    } else {
        for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
            *(uint32_t *)(d + i) = c;
        }
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_dup16)(void *d, uint32_t desc, uint32_t c)
{
    HELPER(gvec_dup32)(d, desc, 0x00010001 * (c & 0xffff));
}

void HELPER(gvec_dup8)(void *d, uint32_t desc, uint32_t c)
{
    HELPER(gvec_dup32)(d, desc, 0x01010101 * (c & 0xff));
}

void HELPER(gvec_not)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = ~*(vec64 *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_and)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) & *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_or)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) | *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_xor)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) ^ *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_andc)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) &~ *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_orc)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) |~ *(vec64 *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ands)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) & vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_xors)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) ^ vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ors)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    vec64 vecb = (vec64)DUP2(b);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) | vecb;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(vec8 *)(d + i) = *(vec8 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(vec16 *)(d + i) = *(vec16 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(vec32 *)(d + i) = *(vec32 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(vec64 *)(d + i) = *(vec64 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec8)) {
        *(svec8 *)(d + i) = *(svec8 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec16)) {
        *(svec16 *)(d + i) = *(svec16 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec32)) {
        *(svec32 *)(d + i) = *(svec32 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(vec64)) {
        *(svec64 *)(d + i) = *(svec64 *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

/* If vectors are enabled, the compiler fills in -1 for true.
   Otherwise, we must take care of this by hand.  */
#ifdef CONFIG_VECTOR16
# define DO_CMP0(X)  X
#else
# define DO_CMP0(X)  -(X)
#endif

#define DO_CMP1(NAME, TYPE, OP)                                            \
void HELPER(NAME)(void *d, void *a, void *b, uint32_t desc)                \
{                                                                          \
    intptr_t oprsz = simd_oprsz(desc);                                     \
    intptr_t i;                                                            \
    for (i = 0; i < oprsz; i += sizeof(TYPE)) {                            \
        *(TYPE *)(d + i) = DO_CMP0(*(TYPE *)(a + i) OP *(TYPE *)(b + i));  \
    }                                                                      \
    clear_high(d, oprsz, desc);                                            \
}

#define DO_CMP2(SZ) \
    DO_CMP1(gvec_eq##SZ, vec##SZ, ==)    \
    DO_CMP1(gvec_ne##SZ, vec##SZ, !=)    \
    DO_CMP1(gvec_lt##SZ, svec##SZ, <)    \
    DO_CMP1(gvec_le##SZ, svec##SZ, <=)   \
    DO_CMP1(gvec_ltu##SZ, vec##SZ, <)    \
    DO_CMP1(gvec_leu##SZ, vec##SZ, <=)

DO_CMP2(8)
DO_CMP2(16)
DO_CMP2(32)
DO_CMP2(64)

#undef DO_CMP0
#undef DO_CMP1
#undef DO_CMP2

void HELPER(gvec_ssadd8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        int r = *(int8_t *)(a + i) + *(int8_t *)(b + i);
        if (r > INT8_MAX) {
            r = INT8_MAX;
        } else if (r < INT8_MIN) {
            r = INT8_MIN;
        }
        *(int8_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ssadd16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int r = *(int16_t *)(a + i) + *(int16_t *)(b + i);
        if (r > INT16_MAX) {
            r = INT16_MAX;
        } else if (r < INT16_MIN) {
            r = INT16_MIN;
        }
        *(int16_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ssadd32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int32_t ai = *(int32_t *)(a + i);
        int32_t bi = *(int32_t *)(b + i);
        int32_t di = ai + bi;
        if (((di ^ ai) &~ (ai ^ bi)) < 0) {
            /* Signed overflow.  */
            di = (di < 0 ? INT32_MAX : INT32_MIN);
        }
        *(int32_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ssadd64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t ai = *(int64_t *)(a + i);
        int64_t bi = *(int64_t *)(b + i);
        int64_t di = ai + bi;
        if (((di ^ ai) &~ (ai ^ bi)) < 0) {
            /* Signed overflow.  */
            di = (di < 0 ? INT64_MAX : INT64_MIN);
        }
        *(int64_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sssub8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        int r = *(int8_t *)(a + i) - *(int8_t *)(b + i);
        if (r > INT8_MAX) {
            r = INT8_MAX;
        } else if (r < INT8_MIN) {
            r = INT8_MIN;
        }
        *(uint8_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sssub16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int r = *(int16_t *)(a + i) - *(int16_t *)(b + i);
        if (r > INT16_MAX) {
            r = INT16_MAX;
        } else if (r < INT16_MIN) {
            r = INT16_MIN;
        }
        *(int16_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sssub32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int32_t ai = *(int32_t *)(a + i);
        int32_t bi = *(int32_t *)(b + i);
        int32_t di = ai - bi;
        if (((di ^ ai) & (ai ^ bi)) < 0) {
            /* Signed overflow.  */
            di = (di < 0 ? INT32_MAX : INT32_MIN);
        }
        *(int32_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sssub64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t ai = *(int64_t *)(a + i);
        int64_t bi = *(int64_t *)(b + i);
        int64_t di = ai - bi;
        if (((di ^ ai) & (ai ^ bi)) < 0) {
            /* Signed overflow.  */
            di = (di < 0 ? INT64_MAX : INT64_MIN);
        }
        *(int64_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_usadd8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        unsigned r = *(uint8_t *)(a + i) + *(uint8_t *)(b + i);
        if (r > UINT8_MAX) {
            r = UINT8_MAX;
        }
        *(uint8_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_usadd16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        unsigned r = *(uint16_t *)(a + i) + *(uint16_t *)(b + i);
        if (r > UINT16_MAX) {
            r = UINT16_MAX;
        }
        *(uint16_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_usadd32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint32_t ai = *(uint32_t *)(a + i);
        uint32_t bi = *(uint32_t *)(b + i);
        uint32_t di = ai + bi;
        if (di < ai) {
            di = UINT32_MAX;
        }
        *(uint32_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_usadd64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t ai = *(uint64_t *)(a + i);
        uint64_t bi = *(uint64_t *)(b + i);
        uint64_t di = ai + bi;
        if (di < ai) {
            di = UINT64_MAX;
        }
        *(uint64_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ussub8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        int r = *(uint8_t *)(a + i) - *(uint8_t *)(b + i);
        if (r < 0) {
            r = 0;
        }
        *(uint8_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ussub16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        int r = *(uint16_t *)(a + i) - *(uint16_t *)(b + i);
        if (r < 0) {
            r = 0;
        }
        *(uint16_t *)(d + i) = r;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ussub32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint32_t ai = *(uint32_t *)(a + i);
        uint32_t bi = *(uint32_t *)(b + i);
        uint32_t di = ai - bi;
        if (ai < bi) {
            di = 0;
        }
        *(uint32_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ussub64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t ai = *(uint64_t *)(a + i);
        uint64_t bi = *(uint64_t *)(b + i);
        uint64_t di = ai - bi;
        if (ai < bi) {
            di = 0;
        }
        *(uint64_t *)(d + i) = di;
    }
    clear_high(d, oprsz, desc);
}
