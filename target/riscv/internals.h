/*
 * QEMU RISC-V CPU -- internal functions and types
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_INTERNALS_H
#define RISCV_CPU_INTERNALS_H

#include "hw/registerfields.h"

/* share data between vector helpers and decode code */
FIELD(VDATA, MLEN, 0, 8)
FIELD(VDATA, VM, 8, 1)
FIELD(VDATA, LMUL, 9, 2)
FIELD(VDATA, NF, 11, 4)
FIELD(VDATA, WD, 11, 1)

/* float point classify helpers */
target_ulong fclass_h(uint64_t frs1);
target_ulong fclass_s(uint64_t frs1);
target_ulong fclass_d(uint64_t frs1);

#define SEW8  0
#define SEW16 1
#define SEW32 2
#define SEW64 3

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_riscv_cpu;
#endif

static inline uint64_t nanbox_s(float32 f)
{
    return f | MAKE_64BIT_MASK(32, 32);
}

static inline float32 check_nanbox_s(uint64_t f)
{
    uint64_t mask = MAKE_64BIT_MASK(32, 32);

    if (likely((f & mask) == mask)) {
        return (uint32_t)f;
    } else {
        return 0x7fc00000u; /* default qnan */
    }
}

static inline uint64_t nanbox_h(float16 f)
{
    return f | MAKE_64BIT_MASK(16, 48);
}

static inline float16 check_nanbox_h(uint64_t f)
{
    uint64_t mask = MAKE_64BIT_MASK(16, 48);

    if (likely((f & mask) == mask)) {
        return (uint16_t)f;
    } else {
        return 0x7E00u; /* default qnan */
    }
}

#endif
