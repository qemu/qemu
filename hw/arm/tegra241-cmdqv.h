/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * NVIDIA Tegra241 CMDQ-Virtualization extension for SMMUv3
 *
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_TEGRA241_CMDQV_H
#define HW_ARM_TEGRA241_CMDQV_H

#include "hw/core/registerfields.h"

#define CMDQV_VER                 1
#define CMDQV_NUM_CMDQ_LOG2       1
#define CMDQV_NUM_SID_PER_VI_LOG2 4

#define TEGRA241_CMDQV_MAX_CMDQ      (1U << CMDQV_NUM_CMDQ_LOG2)
#define TEGRA241_CMDQV_MAX_NUM_SID   (1U << CMDQV_NUM_SID_PER_VI_LOG2)

/*
 * Tegra241 CMDQV MMIO layout (64KB pages)
 *
 * 0x00000  (CMDQ-V Config page)
 * 0x10000  (CMDQ-V CMDQ Page0)
 * 0x20000  (CMDQ-V CMDQ Page1)
 * 0x30000  (Virtual Interface Page0)
 * 0x40000  (Virtual Interface Page1)
 */
#define TEGRA241_CMDQV_IO_LEN 0x50000

/* CMDQV MMIO aperture bases and VCMDQ stride */
#define CMDQV_VCMDQ_PAGE0_BASE  0x10000  /* CMDQV_CMDQ_BASE */
#define CMDQV_VCMDQ_PAGE1_BASE  0x20000
#define CMDQV_VINTF_PAGE0_BASE  0x30000  /* CMDQV_VI_CMDQ_BASE */
#define CMDQV_VINTF_PAGE1_BASE  0x40000
#define CMDQV_VCMDQ_STRIDE      0x80

#define VINTF_PAGE_SIZE 0x10000

struct iommu_viommu_tegra241_cmdqv;

typedef struct Tegra241CMDQV {
    struct iommu_viommu_tegra241_cmdqv *cmdqv_data;
    SMMUv3AccelState *s_accel;
    MemoryRegion mmio_cmdqv;
    qemu_irq irq;
    IOMMUFDVeventq *veventq;
    IOMMUFDHWqueue *vcmdq[TEGRA241_CMDQV_MAX_CMDQ];
    void *vintf_page0;
    MemoryRegion *mr_vintf_page0;

    /* CMDQ-V Config page register cache */
    uint32_t config;
    uint32_t param;
    uint32_t status;
    uint32_t vi_err_map[2];
    uint32_t vi_int_mask[2];
    uint32_t cmdq_err_map[4];
    uint32_t cmdq_alloc_map[TEGRA241_CMDQV_MAX_CMDQ];

    /* VINTF0 register cache (within CMDQ-V Config page) */
    uint32_t vintf_config;
    uint32_t vintf_status;
    uint32_t vintf_sid_match[TEGRA241_CMDQV_MAX_NUM_SID];
    uint32_t vintf_sid_replace[TEGRA241_CMDQV_MAX_NUM_SID];
    uint32_t vintf_cmdq_err_map[4];
    /*
     * VCMDQ register cache. The direct (VCMDQ aperture) and logical
     * (VINTF aperture) views are hardware aliases; both are served from
     * this single cached copy.
     */
    uint32_t vcmdq_cons_indx[TEGRA241_CMDQV_MAX_CMDQ];
    uint32_t vcmdq_prod_indx[TEGRA241_CMDQV_MAX_CMDQ];
    uint32_t vcmdq_config[TEGRA241_CMDQV_MAX_CMDQ];
    uint32_t vcmdq_status[TEGRA241_CMDQV_MAX_CMDQ];
    uint32_t vcmdq_gerror[TEGRA241_CMDQV_MAX_CMDQ];
    uint32_t vcmdq_gerrorn[TEGRA241_CMDQV_MAX_CMDQ];
    uint64_t vcmdq_base[TEGRA241_CMDQV_MAX_CMDQ];
    uint64_t vcmdq_cons_indx_base[TEGRA241_CMDQV_MAX_CMDQ];
} Tegra241CMDQV;

/* CMDQ-V Config page registers (offset 0x00000) */
REG32(CONFIG, 0x0)
FIELD(CONFIG, CMDQV_EN, 0, 1)
FIELD(CONFIG, CMDQV_PER_CMD_OFFSET, 1, 3)
FIELD(CONFIG, CMDQ_MAX_CLK_BATCH, 4, 8)
FIELD(CONFIG, CMDQ_MAX_CMD_BATCH, 12, 8)
FIELD(CONFIG, CONS_DRAM_EN, 20, 1)

/* CMDQV_EN=1, PER_CMD_OFFSET=16B, CLK_BATCH=256, CMD_BATCH=32. */
#define V_CONFIG_RESET 0x00020083

REG32(PARAM, 0x4)
FIELD(PARAM, CMDQV_VER, 0, 4)
FIELD(PARAM, CMDQV_NUM_CMDQ_LOG2, 4, 4)
FIELD(PARAM, CMDQV_NUM_VI_LOG2, 8, 4)
FIELD(PARAM, CMDQV_NUM_SID_PER_VI_LOG2, 12, 4)

REG32(STATUS, 0x8)
FIELD(STATUS, CMDQV_ENABLED, 0, 1)

/* SMMU_CMDQV_VI_ERR_MAP_0/1 definitions */
#define A_VI_ERR_MAP_0 0x14
#define A_VI_ERR_MAP_1 0x18
#define V_VI_ERR_MAP_NO_ERROR (0)
#define V_VI_ERR_MAP_ERROR (1)

/* SMMU_CMDQV_VI_INT_MASK_0/1 definitions */
#define A_VI_INT_MASK_0 0x1c
#define A_VI_INT_MASK_1 0x20
#define V_VI_INT_MASK_NOT_MASKED (0)
#define V_VI_INT_MASK_MASKED (1)

/* SMMU_CMDQV_CMDQ_ERR_MAP_0-3 definitions */
#define A_CMDQ_ERR_MAP_0 0x24
#define A_CMDQ_ERR_MAP_1 0x28
#define A_CMDQ_ERR_MAP_2 0x2c
#define A_CMDQ_ERR_MAP_3 0x30

/*
 * CMDQ_ALLOC_MAP: one entry per physical VCMDQ. Hardware supports up to 128
 * entries (CMDQV_NUM_CMDQ_LOG2=7), but QEMU only exposes
 * TEGRA241_CMDQV_MAX_CMDQ (=2) VCMDQs per VM so only entries 0 and 1 are
 * defined here.
 */
/* 2 identical register entries */
#define SMMU_CMDQV_CMDQ_ALLOC_MAP_(i)                       \
    REG32(CMDQ_ALLOC_MAP_##i, 0x200 + i * 4)                \
    FIELD(CMDQ_ALLOC_MAP_##i, ALLOC, 0, 1)                  \
    FIELD(CMDQ_ALLOC_MAP_##i, LVCMDQ, 1, 7)                 \
    FIELD(CMDQ_ALLOC_MAP_##i, VIRT_INTF_INDX, 15, 6)

SMMU_CMDQV_CMDQ_ALLOC_MAP_(0)
SMMU_CMDQV_CMDQ_ALLOC_MAP_(1)

/* SMMU_CMDQV_VINTF0 registers (only VINTF0 is exposed to the guest) */
REG32(VINTF0_CONFIG, 0x1000)
FIELD(VINTF0_CONFIG, ENABLE, 0, 1)
FIELD(VINTF0_CONFIG, VMID, 1, 16)
FIELD(VINTF0_CONFIG, HYP_OWN, 17, 1)

REG32(VINTF0_STATUS, 0x1004)
FIELD(VINTF0_STATUS, ENABLE_OK, 0, 1)
FIELD(VINTF0_STATUS, STATUS, 1, 3)
FIELD(VINTF0_STATUS, VI_NUM_LVCMDQ, 16, 8)

#define V_VINTF_STATUS_NO_ERROR    (0 << 1)
#define V_VINTF_STATUS_VCMDQ_ERROR (1 << 1)

/*
 * SMMU_CMDQV_VINTF0_SID_MATCH/_REPLACE: 16 entries per VINTF
 * (CMDQV_NUM_SID_PER_VI_LOG2=4). Only _0 and _15 are defined,
 * used as switch case range bounds.
 */
REG32(VINTF0_SID_MATCH_0, 0x1040)
FIELD(VINTF0_SID_MATCH_0, ENABLE, 0, 1)
FIELD(VINTF0_SID_MATCH_0, VIRT_SID, 1, 20)
#define A_VINTF0_SID_MATCH_15  (A_VINTF0_SID_MATCH_0 + 15 * 4)

REG32(VINTF0_SID_REPLACE_0, 0x1080)
FIELD(VINTF0_SID_REPLACE_0, PHYS_SID, 0, 20)
#define A_VINTF0_SID_REPLACE_15 (A_VINTF0_SID_REPLACE_0 + 15 * 4)

/*
 * SMMU_CMDQV_VINTF0_LVCMDQ_ERR_MAP: 4 registers per VINTF covering 32 logical
 * VCMDQs each. With TEGRA241_CMDQV_MAX_CMDQ=2, only MAP_0 bits [1:0] carry
 * error state. MAP_1..MAP_3 always read as 0. Only _0 and _3 are defined,
 * used as switch case range bounds.
 */
REG32(VINTF0_LVCMDQ_ERR_MAP_0, 0x10c0)
FIELD(VINTF0_LVCMDQ_ERR_MAP_0, LVCMDQ_ERR_MAP, 0, 32)
#define A_VINTF0_LVCMDQ_ERR_MAP_3 (A_VINTF0_LVCMDQ_ERR_MAP_0 + 3 * 4)

/*
 * Direct VCMDQ aperture register windows.
 *
 * Page 0 @ CMDQV_VCMDQ_PAGE0_BASE: VCMDQ control and status registers.
 * Page 1 @ CMDQV_VCMDQ_PAGE1_BASE: VCMDQ base and DRAM address registers.
 *
 * Each VCMDQ occupies a CMDQV_VCMDQ_STRIDE-byte slot within its page.
 */

/* --- Page 0 register macros --- */
#define SMMU_CMDQV_VCMDQi_CONS_INDX_(i)                     \
    REG32(VCMDQ##i##_CONS_INDX,                             \
          CMDQV_VCMDQ_PAGE0_BASE + i * CMDQV_VCMDQ_STRIDE)  \
    FIELD(VCMDQ##i##_CONS_INDX, RD, 0, 20)                  \
    FIELD(VCMDQ##i##_CONS_INDX, ERR, 24, 7)

#define V_VCMDQ_CONS_INDX_ERR_CERROR_NONE         0
#define V_VCMDQ_CONS_INDX_ERR_CERROR_ILL_OPCODE   1
#define V_VCMDQ_CONS_INDX_ERR_CERROR_ABT          2
#define V_VCMDQ_CONS_INDX_ERR_CERROR_ATC_INV_SYNC 3
#define V_VCMDQ_CONS_INDX_ERR_CERROR_ILL_ACCESS   4

#define SMMU_CMDQV_VCMDQi_PROD_INDX_(i)                          \
    REG32(VCMDQ##i##_PROD_INDX,                                  \
          CMDQV_VCMDQ_PAGE0_BASE + 0x4 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_PROD_INDX, WR, 0, 20)

#define SMMU_CMDQV_VCMDQi_CONFIG_(i)                             \
    REG32(VCMDQ##i##_CONFIG,                                     \
          CMDQV_VCMDQ_PAGE0_BASE + 0x8 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_CONFIG, CMDQ_EN, 0, 1)

#define SMMU_CMDQV_VCMDQi_STATUS_(i)                             \
    REG32(VCMDQ##i##_STATUS,                                     \
          CMDQV_VCMDQ_PAGE0_BASE + 0xc + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_STATUS, CMDQ_EN_OK, 0, 1)

#define SMMU_CMDQV_VCMDQi_GERROR_(i)                              \
    REG32(VCMDQ##i##_GERROR,                                      \
          CMDQV_VCMDQ_PAGE0_BASE + 0x10 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_GERROR, CMDQ_ERR, 0, 1)                      \
    FIELD(VCMDQ##i##_GERROR, CONS_DRAM_WR_ABT_ERR, 1, 1)          \
    FIELD(VCMDQ##i##_GERROR, CMDQ_INIT_ERR, 2, 1)

#define SMMU_CMDQV_VCMDQi_GERRORN_(i)                             \
    REG32(VCMDQ##i##_GERRORN,                                     \
          CMDQV_VCMDQ_PAGE0_BASE + 0x14 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_GERRORN, CMDQ_ERR, 0, 1)                     \
    FIELD(VCMDQ##i##_GERRORN, CONS_DRAM_WR_ABT_ERR, 1, 1)         \
    FIELD(VCMDQ##i##_GERRORN, CMDQ_INIT_ERR, 2, 1)

/* Page 0 layout: VCMDQ0 */
SMMU_CMDQV_VCMDQi_CONS_INDX_(0)
SMMU_CMDQV_VCMDQi_PROD_INDX_(0)
SMMU_CMDQV_VCMDQi_CONFIG_(0)
SMMU_CMDQV_VCMDQi_STATUS_(0)
SMMU_CMDQV_VCMDQi_GERROR_(0)
SMMU_CMDQV_VCMDQi_GERRORN_(0)

/* Page 0 layout: VCMDQ1 */
SMMU_CMDQV_VCMDQi_CONS_INDX_(1)
SMMU_CMDQV_VCMDQi_PROD_INDX_(1)
SMMU_CMDQV_VCMDQi_CONFIG_(1)
SMMU_CMDQV_VCMDQi_STATUS_(1)
SMMU_CMDQV_VCMDQi_GERROR_(1)
SMMU_CMDQV_VCMDQi_GERRORN_(1)

/* --- Page 1 register macros --- */
#define SMMU_CMDQV_VCMDQi_BASE_L_(i)                                          \
    REG32(VCMDQ##i##_BASE_L, CMDQV_VCMDQ_PAGE1_BASE + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_BASE_L, LOG2SIZE, 0, 5)                                  \
    FIELD(VCMDQ##i##_BASE_L, ADDR, 5, 27)

#define SMMU_CMDQV_VCMDQi_BASE_H_(i)                             \
    REG32(VCMDQ##i##_BASE_H,                                     \
          CMDQV_VCMDQ_PAGE1_BASE + 0x4 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_BASE_H, ADDR, 0, 16)

#define SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_L_(i)              \
    REG32(VCMDQ##i##_CONS_INDX_BASE_DRAM_L,                      \
          CMDQV_VCMDQ_PAGE1_BASE + 0x8 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_CONS_INDX_BASE_DRAM_L, ADDR, 0, 32)

#define SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_H_(i)              \
    REG32(VCMDQ##i##_CONS_INDX_BASE_DRAM_H,                      \
          CMDQV_VCMDQ_PAGE1_BASE + 0xc + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VCMDQ##i##_CONS_INDX_BASE_DRAM_H, ADDR, 0, 16)

/* Page 1 layout: VCMDQ0 */
SMMU_CMDQV_VCMDQi_BASE_L_(0)
SMMU_CMDQV_VCMDQi_BASE_H_(0)
SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_L_(0)
SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_H_(0)

/* Page 1 layout: VCMDQ1 */
SMMU_CMDQV_VCMDQi_BASE_L_(1)
SMMU_CMDQV_VCMDQi_BASE_H_(1)
SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_L_(1)
SMMU_CMDQV_VCMDQi_CONS_INDX_BASE_DRAM_H_(1)

/*
 * VINTF0 logical VCMDQ aperture register windows.
 *
 * Page 0 @ CMDQV_VINTF_PAGE0_BASE: VCMDQ control and status registers.
 * Page 1 @ CMDQV_VINTF_PAGE1_BASE: VCMDQ base and DRAM address registers.
 *
 * VCMDQs mapped via VINTF are accessed through this aperture as
 * hardware aliases of the direct VCMDQ aperture above.
 */

/* --- Page 0 register macros --- */
#define SMMU_CMDQV_VI_VCMDQi_CONS_INDX_(i)                  \
    REG32(VI_VCMDQ##i##_CONS_INDX,                          \
          CMDQV_VINTF_PAGE0_BASE + i * CMDQV_VCMDQ_STRIDE)  \
    FIELD(VI_VCMDQ##i##_CONS_INDX, RD, 0, 20)               \
    FIELD(VI_VCMDQ##i##_CONS_INDX, ERR, 24, 7)

#define SMMU_CMDQV_VI_VCMDQi_PROD_INDX_(i)                       \
    REG32(VI_VCMDQ##i##_PROD_INDX,                               \
          CMDQV_VINTF_PAGE0_BASE + 0x4 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_PROD_INDX, WR, 0, 20)

#define SMMU_CMDQV_VI_VCMDQi_CONFIG_(i)                          \
    REG32(VI_VCMDQ##i##_CONFIG,                                  \
          CMDQV_VINTF_PAGE0_BASE + 0x8 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_CONFIG, CMDQ_EN, 0, 1)

#define SMMU_CMDQV_VI_VCMDQi_STATUS_(i)                          \
    REG32(VI_VCMDQ##i##_STATUS,                                  \
          CMDQV_VINTF_PAGE0_BASE + 0xc + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_STATUS, CMDQ_EN_OK, 0, 1)

#define SMMU_CMDQV_VI_VCMDQi_GERROR_(i)                           \
    REG32(VI_VCMDQ##i##_GERROR,                                   \
          CMDQV_VINTF_PAGE0_BASE + 0x10 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_GERROR, CMDQ_ERR, 0, 1)                   \
    FIELD(VI_VCMDQ##i##_GERROR, CONS_DRAM_WR_ABT_ERR, 1, 1)       \
    FIELD(VI_VCMDQ##i##_GERROR, CMDQ_INIT_ERR, 2, 1)

#define SMMU_CMDQV_VI_VCMDQi_GERRORN_(i)                          \
    REG32(VI_VCMDQ##i##_GERRORN,                                  \
          CMDQV_VINTF_PAGE0_BASE + 0x14 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_GERRORN, CMDQ_ERR, 0, 1)                  \
    FIELD(VI_VCMDQ##i##_GERRORN, CONS_DRAM_WR_ABT_ERR, 1, 1)      \
    FIELD(VI_VCMDQ##i##_GERRORN, CMDQ_INIT_ERR, 2, 1)

/* Page 0 layout: VCMDQ0 */
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_(0)
SMMU_CMDQV_VI_VCMDQi_PROD_INDX_(0)
SMMU_CMDQV_VI_VCMDQi_CONFIG_(0)
SMMU_CMDQV_VI_VCMDQi_STATUS_(0)
SMMU_CMDQV_VI_VCMDQi_GERROR_(0)
SMMU_CMDQV_VI_VCMDQi_GERRORN_(0)

/* Page 0 layout: VCMDQ1 */
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_(1)
SMMU_CMDQV_VI_VCMDQi_PROD_INDX_(1)
SMMU_CMDQV_VI_VCMDQi_CONFIG_(1)
SMMU_CMDQV_VI_VCMDQi_STATUS_(1)
SMMU_CMDQV_VI_VCMDQi_GERROR_(1)
SMMU_CMDQV_VI_VCMDQi_GERRORN_(1)

/* --- Page 1 register macros --- */
#define SMMU_CMDQV_VI_VCMDQi_BASE_L_(i)                     \
    REG32(VI_VCMDQ##i##_BASE_L,                             \
          CMDQV_VINTF_PAGE1_BASE + i * CMDQV_VCMDQ_STRIDE)  \
    FIELD(VI_VCMDQ##i##_BASE_L, LOG2SIZE, 0, 5)             \
    FIELD(VI_VCMDQ##i##_BASE_L, ADDR, 5, 27)

#define SMMU_CMDQV_VI_VCMDQi_BASE_H_(i)                          \
    REG32(VI_VCMDQ##i##_BASE_H,                                  \
          CMDQV_VINTF_PAGE1_BASE + 0x4 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_BASE_H, ADDR, 0, 16)

#define SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_L_(i)           \
    REG32(VI_VCMDQ##i##_CONS_INDX_BASE_DRAM_L,                   \
          CMDQV_VINTF_PAGE1_BASE + 0x8 + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_CONS_INDX_BASE_DRAM_L, ADDR, 0, 32)

#define SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_H_(i)           \
    REG32(VI_VCMDQ##i##_CONS_INDX_BASE_DRAM_H,                   \
          CMDQV_VINTF_PAGE1_BASE + 0xc + i * CMDQV_VCMDQ_STRIDE) \
    FIELD(VI_VCMDQ##i##_CONS_INDX_BASE_DRAM_H, ADDR, 0, 16)

/* Page 1 layout: VCMDQ0 */
SMMU_CMDQV_VI_VCMDQi_BASE_L_(0)
SMMU_CMDQV_VI_VCMDQi_BASE_H_(0)
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_L_(0)
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_H_(0)

/* Page 1 layout: VCMDQ1 */
SMMU_CMDQV_VI_VCMDQi_BASE_L_(1)
SMMU_CMDQV_VI_VCMDQi_BASE_H_(1)
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_L_(1)
SMMU_CMDQV_VI_VCMDQi_CONS_INDX_BASE_DRAM_H_(1)

static inline bool tegra241_cmdqv_enabled(Tegra241CMDQV *cmdqv)
{
    return cmdqv->status & R_STATUS_CMDQV_ENABLED_MASK;
}

static inline bool tegra241_vintf_enabled(Tegra241CMDQV *cmdqv)
{
    return cmdqv->vintf_status & R_VINTF0_STATUS_ENABLE_OK_MASK;
}

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void);

#endif /* HW_ARM_TEGRA241_CMDQV_H */
