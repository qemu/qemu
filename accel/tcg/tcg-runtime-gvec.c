/*
 * Generic vectorized operation runtime
 *
 * Copyright (c) 2018 Linaro
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

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"


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

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) + *(uint8_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) + *(uint16_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) + *(uint32_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_add64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) + *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) + (uint8_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) + (uint16_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) + (uint32_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_adds64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) + b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) - *(uint8_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) - *(uint16_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) - *(uint32_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sub64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) - *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) - (uint8_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) - (uint16_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) - (uint32_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_subs64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) - b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) * *(uint8_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) * *(uint16_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) * *(uint32_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_mul64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) * *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls8)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) * (uint8_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls16)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) * (uint16_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls32)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) * (uint32_t)b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_muls64)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) * b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg8)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = -*(uint8_t *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg16)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = -*(uint16_t *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg32)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = -*(uint32_t *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_neg64)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = -*(uint64_t *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_abs8)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        int8_t aa = *(int8_t *)(a + i);
        *(int8_t *)(d + i) = aa < 0 ? -aa : aa;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_abs16)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t aa = *(int16_t *)(a + i);
        *(int16_t *)(d + i) = aa < 0 ? -aa : aa;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_abs32)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int32_t aa = *(int32_t *)(a + i);
        *(int32_t *)(d + i) = aa < 0 ? -aa : aa;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_abs64)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t aa = *(int64_t *)(a + i);
        *(int64_t *)(d + i) = aa < 0 ? -aa : aa;
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

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = ~*(uint64_t *)(a + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_and)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) & *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_or)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) | *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_xor)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) ^ *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_andc)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) &~ *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_orc)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) |~ *(uint64_t *)(b + i);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_nand)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = ~(*(uint64_t *)(a + i) & *(uint64_t *)(b + i));
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_nor)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = ~(*(uint64_t *)(a + i) | *(uint64_t *)(b + i));
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_eqv)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = ~(*(uint64_t *)(a + i) ^ *(uint64_t *)(b + i));
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ands)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) & b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_xors)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) ^ b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_ors)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) | b;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) << shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(int8_t *)(d + i) = *(int8_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(int16_t *)(d + i) = *(int16_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(int32_t *)(d + i) = *(int32_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(int64_t *)(d + i) = *(int64_t *)(a + i) >> shift;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl8i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = rol8(*(uint8_t *)(a + i), shift);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl16i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = rol16(*(uint16_t *)(a + i), shift);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl32i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = rol32(*(uint32_t *)(a + i), shift);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl64i)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    int shift = simd_data(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = rol64(*(uint64_t *)(a + i), shift);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl8v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t sh = *(uint8_t *)(b + i) & 7;
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) << sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl16v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint8_t sh = *(uint16_t *)(b + i) & 15;
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) << sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl32v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint8_t sh = *(uint32_t *)(b + i) & 31;
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) << sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shl64v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint8_t sh = *(uint64_t *)(b + i) & 63;
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) << sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr8v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t sh = *(uint8_t *)(b + i) & 7;
        *(uint8_t *)(d + i) = *(uint8_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr16v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint8_t sh = *(uint16_t *)(b + i) & 15;
        *(uint16_t *)(d + i) = *(uint16_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr32v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint8_t sh = *(uint32_t *)(b + i) & 31;
        *(uint32_t *)(d + i) = *(uint32_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_shr64v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint8_t sh = *(uint64_t *)(b + i) & 63;
        *(uint64_t *)(d + i) = *(uint64_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar8v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        uint8_t sh = *(uint8_t *)(b + i) & 7;
        *(int8_t *)(d + i) = *(int8_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar16v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        uint8_t sh = *(uint16_t *)(b + i) & 15;
        *(int16_t *)(d + i) = *(int16_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar32v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        uint8_t sh = *(uint32_t *)(b + i) & 31;
        *(int32_t *)(d + i) = *(int32_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_sar64v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        uint8_t sh = *(uint64_t *)(b + i) & 63;
        *(int64_t *)(d + i) = *(int64_t *)(a + i) >> sh;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl8v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t sh = *(uint8_t *)(b + i) & 7;
        *(uint8_t *)(d + i) = rol8(*(uint8_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl16v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint8_t sh = *(uint16_t *)(b + i) & 15;
        *(uint16_t *)(d + i) = rol16(*(uint16_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl32v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint8_t sh = *(uint32_t *)(b + i) & 31;
        *(uint32_t *)(d + i) = rol32(*(uint32_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotl64v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint8_t sh = *(uint64_t *)(b + i) & 63;
        *(uint64_t *)(d + i) = rol64(*(uint64_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotr8v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t sh = *(uint8_t *)(b + i) & 7;
        *(uint8_t *)(d + i) = ror8(*(uint8_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotr16v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint8_t sh = *(uint16_t *)(b + i) & 15;
        *(uint16_t *)(d + i) = ror16(*(uint16_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotr32v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint8_t sh = *(uint32_t *)(b + i) & 31;
        *(uint32_t *)(d + i) = ror32(*(uint32_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_rotr64v)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint8_t sh = *(uint64_t *)(b + i) & 63;
        *(uint64_t *)(d + i) = ror64(*(uint64_t *)(a + i), sh);
    }
    clear_high(d, oprsz, desc);
}

#define DO_CMP1(NAME, TYPE, OP)                                            \
void HELPER(NAME)(void *d, void *a, void *b, uint32_t desc)                \
{                                                                          \
    intptr_t oprsz = simd_oprsz(desc);                                     \
    intptr_t i;                                                            \
    for (i = 0; i < oprsz; i += sizeof(TYPE)) {                            \
        *(TYPE *)(d + i) = -(*(TYPE *)(a + i) OP *(TYPE *)(b + i));        \
    }                                                                      \
    clear_high(d, oprsz, desc);                                            \
}

#define DO_CMP2(SZ) \
    DO_CMP1(gvec_eq##SZ, uint##SZ##_t, ==)    \
    DO_CMP1(gvec_ne##SZ, uint##SZ##_t, !=)    \
    DO_CMP1(gvec_lt##SZ, int##SZ##_t, <)      \
    DO_CMP1(gvec_le##SZ, int##SZ##_t, <=)     \
    DO_CMP1(gvec_ltu##SZ, uint##SZ##_t, <)    \
    DO_CMP1(gvec_leu##SZ, uint##SZ##_t, <=)

DO_CMP2(8)
DO_CMP2(16)
DO_CMP2(32)
DO_CMP2(64)

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

void HELPER(gvec_smin8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        int8_t aa = *(int8_t *)(a + i);
        int8_t bb = *(int8_t *)(b + i);
        int8_t dd = aa < bb ? aa : bb;
        *(int8_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smin16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t aa = *(int16_t *)(a + i);
        int16_t bb = *(int16_t *)(b + i);
        int16_t dd = aa < bb ? aa : bb;
        *(int16_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smin32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int32_t aa = *(int32_t *)(a + i);
        int32_t bb = *(int32_t *)(b + i);
        int32_t dd = aa < bb ? aa : bb;
        *(int32_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smin64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t aa = *(int64_t *)(a + i);
        int64_t bb = *(int64_t *)(b + i);
        int64_t dd = aa < bb ? aa : bb;
        *(int64_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smax8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        int8_t aa = *(int8_t *)(a + i);
        int8_t bb = *(int8_t *)(b + i);
        int8_t dd = aa > bb ? aa : bb;
        *(int8_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smax16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t aa = *(int16_t *)(a + i);
        int16_t bb = *(int16_t *)(b + i);
        int16_t dd = aa > bb ? aa : bb;
        *(int16_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smax32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int32_t aa = *(int32_t *)(a + i);
        int32_t bb = *(int32_t *)(b + i);
        int32_t dd = aa > bb ? aa : bb;
        *(int32_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_smax64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t aa = *(int64_t *)(a + i);
        int64_t bb = *(int64_t *)(b + i);
        int64_t dd = aa > bb ? aa : bb;
        *(int64_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umin8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t aa = *(uint8_t *)(a + i);
        uint8_t bb = *(uint8_t *)(b + i);
        uint8_t dd = aa < bb ? aa : bb;
        *(uint8_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umin16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint16_t aa = *(uint16_t *)(a + i);
        uint16_t bb = *(uint16_t *)(b + i);
        uint16_t dd = aa < bb ? aa : bb;
        *(uint16_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umin32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint32_t aa = *(uint32_t *)(a + i);
        uint32_t bb = *(uint32_t *)(b + i);
        uint32_t dd = aa < bb ? aa : bb;
        *(uint32_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umin64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t aa = *(uint64_t *)(a + i);
        uint64_t bb = *(uint64_t *)(b + i);
        uint64_t dd = aa < bb ? aa : bb;
        *(uint64_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umax8)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        uint8_t aa = *(uint8_t *)(a + i);
        uint8_t bb = *(uint8_t *)(b + i);
        uint8_t dd = aa > bb ? aa : bb;
        *(uint8_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umax16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint16_t aa = *(uint16_t *)(a + i);
        uint16_t bb = *(uint16_t *)(b + i);
        uint16_t dd = aa > bb ? aa : bb;
        *(uint16_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umax32)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        uint32_t aa = *(uint32_t *)(a + i);
        uint32_t bb = *(uint32_t *)(b + i);
        uint32_t dd = aa > bb ? aa : bb;
        *(uint32_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_umax64)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t aa = *(uint64_t *)(a + i);
        uint64_t bb = *(uint64_t *)(b + i);
        uint64_t dd = aa > bb ? aa : bb;
        *(uint64_t *)(d + i) = dd;
    }
    clear_high(d, oprsz, desc);
}

void HELPER(gvec_bitsel)(void *d, void *a, void *b, void *c, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t aa = *(uint64_t *)(a + i);
        uint64_t bb = *(uint64_t *)(b + i);
        uint64_t cc = *(uint64_t *)(c + i);
        *(uint64_t *)(d + i) = (bb & aa) | (cc & ~aa);
    }
    clear_high(d, oprsz, desc);
}
