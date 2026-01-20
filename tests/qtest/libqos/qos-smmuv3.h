/*
 * QOS SMMUv3 Module
 *
 * This module provides SMMUv3-specific helper functions for libqos tests,
 * encapsulating SMMUv3 setup, and assertions.
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QTEST_LIBQOS_SMMUV3_H
#define QTEST_LIBQOS_SMMUV3_H

#include "hw/misc/iommu-testdev.h"

/*
 * SMMU MMIO register base for virt machine-wide SMMU. This does not
 * apply to user-creatable device such as -device arm-smmuv3.
 */
#define VIRT_SMMU_BASE            0x0000000009050000ull

/* SMMU queue and table base addresses */
#define QSMMU_CMDQ_BASE_ADDR      0x000000000e16b000ull
#define QSMMU_EVENTQ_BASE_ADDR    0x000000000e170000ull

/*
 * Translation tables and descriptors for a mapping of
 *   - IOVA (Stage 1 only or nested translation stage)
 *   - IPA  (Stage 2 only)
 * to GPA.
 *
 * The translation is based on the Arm architecture with the following
 * prerequisites:
 * - Granule size: 4KB pages.
 * - Page table levels: 4 levels (L0, L1, L2, L3), starting at level 0.
 * - IOVA size: The walk resolves a IOVA: 0x8080604567
 * - Address space: The 4-level lookup with 4KB granules supports up to a
 * 48-bit (256TB) virtual address space. Each level uses a 9-bit index
 * (512 entries per table). The breakdown is:
 * - L0 index: IOVA bits [47:39]
 * - L1 index: IOVA bits [38:30]
 * - L2 index: IOVA bits [29:21]
 * - L3 index: IOVA bits [20:12]
 * - Page offset: IOVA bits [11:0]
 *
 * NOTE: All physical addresses defined here (QSMMU_VTTB, table addresses, etc.)
 * appear to be within a secure RAM region. In practice, an offset is added
 * to these values to place them in non-secure RAM. For example, when running
 * in a virt machine type, the RAM base address (e.g., 0x40000000) is added to
 * these constants.
 */
#define QSMMU_IOVA                      0x0000008080604567ull
#define QSMMU_VTTB                      0x000000000e4d0000ull
#define QSMMU_STR_TAB_BASE              0x000000000e179000ull
#define QSMMU_CD_GPA                    (QSMMU_STR_TAB_BASE - 0x40ull)


#define QSMMU_L0_PTE_VAL                0x000000000e4d1000ull
#define QSMMU_L1_PTE_VAL                0x000000000e4d2000ull
#define QSMMU_L2_PTE_VAL                0x000000000e4d3000ull
#define QSMMU_L3_PTE_VAL                0x000000000ecba000ull

#define QSMMU_NON_LEAF_PTE_MASK         0x8000000000000003ull
#define QSMMU_LEAF_PTE_RO_MASK          0x04000000000007e3ull
#define QSMMU_LEAF_PTE_RW_MASK          0x0400000000000763ull
#define QSMMU_PTE_MASK                  0x0000fffffffff000ull

/*
 * Address-space base offsets for test tables.
 * - Non-Secure uses a fixed offset, keeping internal layout identical.
 *
 * Note: Future spaces (e.g. Secure/Realm/Root) are not implemented here.
 * When needed, introduce new offsets and reuse the helpers below so relative
 * layout stays identical across spaces.
 */
#define QSMMU_SPACE_OFFS_NS             0x0000000040000000ull

typedef enum QSMMUSecSID {
    QSMMU_SEC_SID_NONSECURE    = 0,
} QSMMUSecSID;

typedef enum QSMMUSpace {
    QSMMU_SPACE_NONSECURE      = 1,
} QSMMUSpace;

typedef enum QSMMUTransMode {
    QSMMU_TM_S1_ONLY           = 0,
    QSMMU_TM_S2_ONLY           = 1,
    QSMMU_TM_NESTED            = 2,
} QSMMUTransMode;

typedef struct QSMMUTestConfig {
    QSMMUTransMode trans_mode;        /* Translation mode (S1, S2, Nested) */
    QSMMUSecSID sec_sid;              /* SEC_SID of test device */
    uint64_t dma_gpa;                 /* GPA for readback validation */
    uint32_t dma_len;                 /* DMA length for testing */
    uint32_t expected_result;         /* Expected DMA result for validation */
} QSMMUTestConfig;

typedef struct QSMMUTestContext {
    QTestState *qts;            /* QTest state handle */
    QPCIDevice *dev;            /* PCI device handle */
    QPCIBar bar;                /* PCI BAR for MMIO access */
    QSMMUTestConfig config;     /* Test configuration */
    uint64_t smmu_base;         /* SMMU base address */
    uint32_t trans_status;      /* Translation configuration status */
    uint32_t dma_result;        /* DMA operation result */
    uint32_t sid;               /* Stream ID for the test */
    QSMMUSpace tx_space;        /* Cached transaction space */
} QSMMUTestContext;

/* Convert SEC_SID to corresponding Security Space */
QSMMUSpace qsmmu_sec_sid_to_space(QSMMUSecSID sec_sid);

/* Get base offset of the specific Security space */
uint64_t qsmmu_space_offset(QSMMUSpace sp);

uint32_t qsmmu_build_dma_attrs(QSMMUSpace space);

/*
 * qsmmu_setup_and_enable_translation - Complete translation setup and enable
 *
 * @ctx: Test context containing configuration and device handles
 *
 * Returns: Translation status (0 = success, non-zero = error)
 *
 * This function performs the complete translation setup sequence:
 * 1. Builds all required SMMU structures (STE, CD, page tables)
 * 2. Programs SMMU registers for the appropriate security space
 * 3. Returns configuration status
 */
uint32_t qsmmu_setup_and_enable_translation(QSMMUTestContext *ctx);

/*
 * qsmmu_build_translation - Build SMMU translation structures
 *
 * @qts: QTest state handle
 * @mode: Translation mode (S1_ONLY, S2_ONLY, NESTED)
 * @tx_space: Transaction security space
 * @sid: Stream ID
 *
 * Returns: Build status (0 = success, non-zero = error)
 *
 * Constructs all necessary SMMU translation structures in guest memory
 * using the fixed QSMMU_IOVA constant:
 * - Stream Table Entry (STE) for the given SID
 * - Context Descriptor (CD) if Stage 1 translation is involved
 * - Complete page table hierarchy based on translation mode
 *
 * The structures are written to security-space-specific memory regions.
 */
uint32_t qsmmu_build_translation(QTestState *qts, QSMMUTransMode mode,
                                 QSMMUSpace tx_space, uint32_t sid);

/*
 * qsmmu_bank_base - Get SMMU control bank base address
 *
 * @base: SMMU base address
 * @sp: Security space
 *
 * Returns: Bank base address for the given security space
 *
 * Maps security space to the corresponding SMMU control register bank.
 * Currently only Non-Secure bank is supported.
 */
uint64_t qsmmu_bank_base(uint64_t base, QSMMUSpace sp);

/*
 * qsmmu_program_bank - Program SMMU control bank registers
 *
 * @qts: QTest state handle
 * @bank_base: SMMU bank base address
 * @sp: Security space
 *
 * Programs a specific SMMU control bank with minimal configuration:
 * - Global Bypass Attribute (GBPA)
 * - Control registers (CR0, CR1)
 * - Command queue (base, producer, consumer)
 * - Event queue (base, producer, consumer)
 * - Stream table configuration (base, format)
 *
 * Addresses are adjusted based on security space offset.
 */
void qsmmu_program_bank(QTestState *qts, uint64_t bank_base, QSMMUSpace sp);

/*
 * qsmmu_program_regs - Program all required SMMU register banks
 *
 * @qts: QTest state handle
 * @smmu_base: SMMU base address
 * @space: Target security space
 *
 * Programs SMMU registers for the requested security space which is called in
 * qsmmu_setup_and_enable_translation. Always programs Non-Secure bank first,
 * then the target space if different.
 */
void qsmmu_program_regs(QTestState *qts, uint64_t smmu_base, QSMMUSpace space);

/* qsmmu_expected_dma_result - Calculate expected DMA result */
uint32_t qsmmu_expected_dma_result(QSMMUTestContext *ctx);

/*
 * qsmmu_setup_translation_tables - Setup complete SMMU page table hierarchy
 *
 * @qts: QTest state handle
 * @iova: Input Virtual Address or IPA to translate
 * @space: Security space (NONSECURE, SECURE, REALM, ROOT)
 * @is_cd: Whether translating CD address (vs regular IOVA)
 * @mode: Translation mode (S1_ONLY, S2_ONLY, NESTED)
 *
 * This function builds the complete page table structure for translating
 * the given IOVA through the SMMU. The structure varies based on mode:
 *
 * - S1_ONLY: Single Stage 1 walk (IOVA -> PA)
 * - S2_ONLY: Single Stage 2 walk (IPA -> PA)
 * - NESTED: Stage 1 walk (IOVA -> IPA) with nested S2 walks for each
 *   S1 table access, plus final S2 walk for the result IPA
 *
 * For nested mode, this creates a complex hierarchy:
 * - 4 Stage 1 levels (L0-L3), each requiring a 4-level Stage 2 walk
 * - 1 final Stage 2 walk for the resulting IPA
 *
 * The function writes all necessary Page Table Entries (PTEs) to guest
 * memory using qtest_writeq(), setting up the complete translation path
 * that the SMMU hardware will traverse during DMA operations.
 */
void qsmmu_setup_translation_tables(QTestState *qts,
                                    uint64_t iova,
                                    QSMMUSpace space,
                                    bool is_cd,
                                    QSMMUTransMode mode);

/* High-level test execution helpers */
void qsmmu_run_translation_case(QTestState *qts, QPCIDevice *dev,
                                QPCIBar bar, uint64_t smmu_base,
                                const QSMMUTestConfig *cfg);

#endif /* QTEST_LIBQOS_SMMUV3_H */
