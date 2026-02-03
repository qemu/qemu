/*
 * QOS RISC-V IOMMU Module
 *
 * This module provides RISC-V IOMMU-specific helper functions for libqos tests,
 * encapsulating RISC-V IOMMU setup, and assertions.
 *
 * Copyright (c) 2026 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QTEST_LIBQOS_RISCV_IOMMU_H
#define QTEST_LIBQOS_RISCV_IOMMU_H

#include "hw/misc/iommu-testdev.h"

/* RISC-V IOMMU MMIO register base for virt machine */
#define VIRT_RISCV_IOMMU_BASE      0x0000000003010000ull

/* RISC-V IOMMU queue and table base addresses */
#define QRIOMMU_CQ_BASE_ADDR       0x000000000e160000ull
#define QRIOMMU_FQ_BASE_ADDR       0x000000000e170000ull

/* RISC-V IOMMU queue sizing */
#define QRIOMMU_QUEUE_ENTRIES  1024
#define QRIOMMU_CQ_ENTRY_SIZE  16
#define QRIOMMU_FQ_ENTRY_SIZE  32

/*
 * Translation tables and descriptors for RISC-V IOMMU.
 * Similar to ARM SMMUv3, but using RISC-V IOMMU terminology:
 * - Device Context (DC) instead of STE
 * - First-stage context (FSC) for S-stage translation
 * - IOHGATP for G-stage translation
 *
 * Granule size: 4KB pages
 * Page table levels: 3 levels for SV39 (L0, L1, L2)
 * IOVA size: 39-bit virtual address space
 */
#define QRIOMMU_IOVA                0x0000000080604567ull
#define QRIOMMU_IOHGATP             0x0000000000010000ull
#define QRIOMMU_DDT_BASE            0x0000000000014000ull
#define QRIOMMU_DC_BASE             (QRIOMMU_DDT_BASE)

#define QRIOMMU_L0_PTE_VAL          0x0000000000011000ull
#define QRIOMMU_L1_PTE_VAL          0x0000000000012000ull
#define QRIOMMU_L2_PTE_VAL          0x0000000000013000ull

#define QRIOMMU_G_IOHGATP           0x0000000000020000ull
#define QRIOMMU_G_L0_PTE_VAL        0x0000000000021000ull
#define QRIOMMU_G_L1_PTE_VAL        0x0000000000022000ull

/*
 * PTE masks for RISC-V IOMMU page tables.
 * Values match PTE_V, PTE_R, PTE_W, PTE_A, PTE_D in target/riscv/cpu_bits.h
 */
#define QRIOMMU_NON_LEAF_PTE_MASK   0x001  /* PTE_V */
#define QRIOMMU_LEAF_PTE_RW_MASK    0x0c7  /* V|R|W|A|D */
#define QRIOMMU_PTE_PPN_MASK        0x003ffffffffffc00ull

/* Address-space base offset for test tables */
#define QRIOMMU_SPACE_OFFS          0x0000000080000000ull

typedef enum QRIOMMUTransMode {
    QRIOMMU_TM_BARE         = 0,    /* No translation (pass-through) */
    QRIOMMU_TM_S_STAGE_ONLY = 1,    /* First-stage only (S-stage) */
    QRIOMMU_TM_G_STAGE_ONLY = 2,    /* Second-stage only (G-stage) */
    QRIOMMU_TM_NESTED       = 3,    /* Nested translation (S + G) */
} QRIOMMUTransMode;

typedef struct QRIOMMUTestConfig {
    QRIOMMUTransMode trans_mode;    /* Translation mode */
    uint64_t dma_gpa;               /* GPA for readback validation */
    uint32_t dma_len;               /* DMA length for testing */
    uint32_t expected_result;       /* Expected DMA result */
} QRIOMMUTestConfig;

typedef struct QRIOMMUTestContext {
    QTestState *qts;                /* QTest state handle */
    QPCIDevice *dev;                /* PCI device handle */
    QPCIBar bar;                    /* PCI BAR for MMIO access */
    QRIOMMUTestConfig config;       /* Test configuration */
    uint64_t iommu_base;            /* RISC-V IOMMU base address */
    uint32_t trans_status;          /* Translation configuration status */
    uint32_t dma_result;            /* DMA operation result */
    uint32_t device_id;             /* Device ID for the test */
} QRIOMMUTestContext;

/*
 * qriommu_setup_and_enable_translation - Complete translation setup and enable
 *
 * @ctx: Test context containing configuration and device handles
 *
 * Returns: Translation status (0 = success, non-zero = error)
 *
 * This function performs the complete translation setup sequence:
 * 1. Builds all required RISC-V IOMMU structures (DC, page tables)
 * 2. Programs RISC-V IOMMU registers
 * 3. Returns configuration status
 */
uint32_t qriommu_setup_and_enable_translation(QRIOMMUTestContext *ctx);

/*
 * qriommu_build_translation - Build RISC-V IOMMU translation structures
 *
 * @qts: QTest state handle
 * @mode: Translation mode (BARE, S_STAGE_ONLY, G_STAGE_ONLY, NESTED)
 * @device_id: Device ID
 *
 * Returns: Build status (0 = success, non-zero = error)
 *
 * Constructs all necessary RISC-V IOMMU translation structures in guest memory:
 * - Device Context (DC) for the given device ID
 * - First-stage context (FSC) if S-stage translation is involved
 * - Complete page table hierarchy based on translation mode
 */
uint32_t qriommu_build_translation(QTestState *qts, QRIOMMUTransMode mode,
                                   uint32_t device_id);

/*
 * qriommu_program_regs - Program all required RISC-V IOMMU registers
 *
 * @qts: QTest state handle
 * @iommu_base: RISC-V IOMMU base address
 *
 * Programs RISC-V IOMMU registers:
 * - Device Directory Table Pointer (DDTP)
 * - Command queue (base, head, tail)
 * - Fault queue (base, head, tail)
 * - Control and status registers
 */
void qriommu_program_regs(QTestState *qts, uint64_t iommu_base);

/*
 * qriommu_setup_translation_tables - Setup RISC-V IOMMU page table hierarchy
 *
 * @qts: QTest state handle
 * @iova: Input Virtual Address to translate
 * @mode: Translation mode
 *
 * This function builds the complete page table structure for translating
 * the given IOVA through the RISC-V IOMMU. The structure varies based on mode:
 *
 * - BARE: No translation (pass-through)
 * - S_STAGE_ONLY: Single S-stage walk (IOVA -> PA)
 * - G_STAGE_ONLY: Single G-stage walk (IPA -> PA)
 * - NESTED: S-stage walk (IOVA -> IPA) + G-stage walk (IPA -> PA)
 */
void qriommu_setup_translation_tables(QTestState *qts,
                                      uint64_t iova,
                                      QRIOMMUTransMode mode);

/* High-level test execution helpers */
void qriommu_run_translation_case(QTestState *qts, QPCIDevice *dev,
                                  QPCIBar bar, uint64_t iommu_base,
                                  const QRIOMMUTestConfig *cfg);

/* Calculate expected DMA result */
uint32_t qriommu_expected_dma_result(QRIOMMUTestContext *ctx);

/* Build DMA attributes for RISC-V IOMMU */
uint32_t qriommu_build_dma_attrs(void);

#endif /* QTEST_LIBQOS_RISCV_IOMMU_H */
