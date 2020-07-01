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

#endif
