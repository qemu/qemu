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
FIELD(VDATA, VM, 0, 1)
FIELD(VDATA, LMUL, 1, 3)
FIELD(VDATA, VTA, 4, 1)
FIELD(VDATA, VTA_ALL_1S, 5, 1)
FIELD(VDATA, VMA, 6, 1)
FIELD(VDATA, NF, 7, 4)
FIELD(VDATA, WD, 7, 1)

/* float point classify helpers */
target_ulong fclass_h(uint64_t frs1);
target_ulong fclass_s(uint64_t frs1);
target_ulong fclass_d(uint64_t frs1);

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_riscv_cpu;
#endif

enum {
    RISCV_FRM_RNE = 0,  /* Round to Nearest, ties to Even */
    RISCV_FRM_RTZ = 1,  /* Round towards Zero */
    RISCV_FRM_RDN = 2,  /* Round Down */
    RISCV_FRM_RUP = 3,  /* Round Up */
    RISCV_FRM_RMM = 4,  /* Round to Nearest, ties to Max Magnitude */
    RISCV_FRM_DYN = 7,  /* Dynamic rounding mode */
    RISCV_FRM_ROD = 8,  /* Round to Odd */
};

static inline uint64_t nanbox_s(CPURISCVState *env, float32 f)
{
    /* the value is sign-extended instead of NaN-boxing for zfinx */
    if (RISCV_CPU(env_cpu(env))->cfg.ext_zfinx) {
        return (int32_t)f;
    } else {
        return f | MAKE_64BIT_MASK(32, 32);
    }
}

static inline float32 check_nanbox_s(CPURISCVState *env, uint64_t f)
{
    /* Disable NaN-boxing check when enable zfinx */
    if (RISCV_CPU(env_cpu(env))->cfg.ext_zfinx) {
        return (uint32_t)f;
    }

    uint64_t mask = MAKE_64BIT_MASK(32, 32);

    if (likely((f & mask) == mask)) {
        return (uint32_t)f;
    } else {
        return 0x7fc00000u; /* default qnan */
    }
}

static inline uint64_t nanbox_h(CPURISCVState *env, float16 f)
{
    /* the value is sign-extended instead of NaN-boxing for zfinx */
    if (RISCV_CPU(env_cpu(env))->cfg.ext_zfinx) {
        return (int16_t)f;
    } else {
        return f | MAKE_64BIT_MASK(16, 48);
    }
}

static inline float16 check_nanbox_h(CPURISCVState *env, uint64_t f)
{
    /* Disable nanbox check when enable zfinx */
    if (RISCV_CPU(env_cpu(env))->cfg.ext_zfinx) {
        return (uint16_t)f;
    }

    uint64_t mask = MAKE_64BIT_MASK(16, 48);

    if (likely((f & mask) == mask)) {
        return (uint16_t)f;
    } else {
        return 0x7E00u; /* default qnan */
    }
}

#endif
