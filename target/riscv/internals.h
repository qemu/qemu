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

#include "exec/cpu-common.h"
#include "hw/registerfields.h"
#include "fpu/softfloat-types.h"
#include "target/riscv/cpu_bits.h"

/*
 * The current MMU Modes are:
 *  - U                 0b000
 *  - S                 0b001
 *  - S+SUM             0b010
 *  - M                 0b011
 *  - U+2STAGE          0b100
 *  - S+2STAGE          0b101
 *  - S+SUM+2STAGE      0b110
 *  - Shadow stack+U   0b1000
 *  - Shadow stack+S   0b1001
 */
#define MMUIdx_U            0
#define MMUIdx_S            1
#define MMUIdx_S_SUM        2
#define MMUIdx_M            3
#define MMU_2STAGE_BIT      (1 << 2)
#define MMU_IDX_SS_WRITE    (1 << 3)

static inline int mmuidx_priv(int mmu_idx)
{
    int ret = mmu_idx & 3;
    if (ret == MMUIdx_S_SUM) {
        ret = PRV_S;
    }
    return ret;
}

static inline bool mmuidx_sum(int mmu_idx)
{
    return (mmu_idx & 3) == MMUIdx_S_SUM;
}

static inline bool mmuidx_2stage(int mmu_idx)
{
    return mmu_idx & MMU_2STAGE_BIT;
}

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
    if (env_archcpu(env)->cfg.ext_zfinx) {
        return (int32_t)f;
    } else {
        return f | MAKE_64BIT_MASK(32, 32);
    }
}

static inline float32 check_nanbox_s(CPURISCVState *env, uint64_t f)
{
    /* Disable NaN-boxing check when enable zfinx */
    if (env_archcpu(env)->cfg.ext_zfinx) {
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
    if (env_archcpu(env)->cfg.ext_zfinx) {
        return (int16_t)f;
    } else {
        return f | MAKE_64BIT_MASK(16, 48);
    }
}

static inline float16 check_nanbox_h(CPURISCVState *env, uint64_t f)
{
    /* Disable nanbox check when enable zfinx */
    if (env_archcpu(env)->cfg.ext_zfinx) {
        return (uint16_t)f;
    }

    uint64_t mask = MAKE_64BIT_MASK(16, 48);

    if (likely((f & mask) == mask)) {
        return (uint16_t)f;
    } else {
        return 0x7E00u; /* default qnan */
    }
}

/* Our implementation of CPUClass::has_work */
bool riscv_cpu_has_work(CPUState *cs);

/* Zjpm addr masking routine */
static inline target_ulong adjust_addr_body(CPURISCVState *env,
                                            target_ulong addr,
                                            bool is_virt_addr)
{
    RISCVPmPmm pmm = PMM_FIELD_DISABLED;
    uint32_t pmlen = 0;
    bool signext = false;

    /* do nothing for rv32 mode */
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        return addr;
    }

    /* get pmm field depending on whether addr is */
    if (is_virt_addr) {
        pmm = riscv_pm_get_virt_pmm(env);
    } else {
        pmm = riscv_pm_get_pmm(env);
    }

    /* if pointer masking is disabled, return original addr */
    if (pmm == PMM_FIELD_DISABLED) {
        return addr;
    }

    if (!is_virt_addr) {
        signext = riscv_cpu_virt_mem_enabled(env);
    }
    addr = addr << pmlen;
    pmlen = riscv_pm_get_pmlen(pmm);

    /* sign/zero extend masked address by N-1 bit */
    if (signext) {
        addr = (target_long)addr >> pmlen;
    } else {
        addr = addr >> pmlen;
    }

    return addr;
}

static inline target_ulong adjust_addr(CPURISCVState *env,
                                       target_ulong addr)
{
    return adjust_addr_body(env, addr, false);
}

static inline target_ulong adjust_addr_virt(CPURISCVState *env,
                                            target_ulong addr)
{
    return adjust_addr_body(env, addr, true);
}

#endif
