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

#include "qemu/osdep.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "qos-iommu-testdev.h"
#include "qos-riscv-iommu.h"

/* Apply space offset to address */
static inline uint64_t qriommu_apply_space_offs(uint64_t address)
{
    return address + QRIOMMU_SPACE_OFFS;
}

static uint64_t qriommu_encode_pte(uint64_t pa, uint64_t attrs)
{
    return ((pa >> 12) << 10) | attrs;
}

static void qriommu_wait_for_queue_active(QTestState *qts, uint64_t iommu_base,
                                          uint32_t queue_csr, uint32_t on_bit)
{
    guint64 timeout_us = 2 * 1000 * 1000;
    gint64 start_time = g_get_monotonic_time();
    uint32_t reg;

    for (;;) {
        qtest_clock_step(qts, 100);

        reg = qtest_readl(qts, iommu_base + queue_csr);
        if (reg & on_bit) {
            return;
        }
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

uint32_t qriommu_expected_dma_result(QRIOMMUTestContext *ctx)
{
    return ctx->config.expected_result;
}

uint32_t qriommu_build_dma_attrs(void)
{
    /* RISC-V IOMMU uses standard AXI attributes */
    return 0;
}

uint32_t qriommu_setup_and_enable_translation(QRIOMMUTestContext *ctx)
{
    uint32_t build_result;

    /* Build page tables and RISC-V IOMMU structures first */
    build_result = qriommu_build_translation(
                       ctx->qts, ctx->config.trans_mode,
                       ctx->device_id);
    if (build_result != 0) {
        g_test_message("Build failed: mode=%u device_id=%u status=0x%x",
                       ctx->config.trans_mode, ctx->device_id, build_result);
        ctx->trans_status = build_result;
        return ctx->trans_status;
    }

    /* Program RISC-V IOMMU registers */
    qriommu_program_regs(ctx->qts, ctx->iommu_base);

    ctx->trans_status = 0;
    return ctx->trans_status;
}

static bool qriommu_validate_test_result(QRIOMMUTestContext *ctx)
{
    uint32_t expected = qriommu_expected_dma_result(ctx);
    g_test_message("-> Validating result: expected=0x%x actual=0x%x",
                   expected, ctx->dma_result);
    return (ctx->dma_result == expected);
}

static uint32_t qriommu_single_translation_setup(void *opaque)
{
    return qriommu_setup_and_enable_translation(opaque);
}

static uint32_t qriommu_single_translation_attrs(void *opaque)
{
    return qriommu_build_dma_attrs();
}

static bool qriommu_single_translation_validate(void *opaque)
{
    return qriommu_validate_test_result(opaque);
}

static void qriommu_single_translation_report(void *opaque,
                                              uint32_t dma_result)
{
    QRIOMMUTestContext *ctx = opaque;

    if (dma_result != 0) {
        g_test_message("DMA failed: mode=%u result=0x%x",
                       ctx->config.trans_mode, dma_result);
    } else {
        g_test_message("-> DMA succeeded: mode=%u",
                       ctx->config.trans_mode);
    }
}

void qriommu_run_translation_case(QTestState *qts, QPCIDevice *dev,
                                  QPCIBar bar, uint64_t iommu_base,
                                  const QRIOMMUTestConfig *cfg)
{
    QRIOMMUTestContext ctx = {
        .qts = qts,
        .dev = dev,
        .bar = bar,
        .iommu_base = iommu_base,
        .config = *cfg,
        .device_id = dev->devfn,
    };

    QOSIOMMUTestdevDmaCfg dma = {
        .dev = dev,
        .bar = bar,
        .iova = QRIOMMU_IOVA,
        .gpa = ctx.config.dma_gpa,
        .len = ctx.config.dma_len,
    };

    qtest_memset(qts, cfg->dma_gpa, 0x00, cfg->dma_len);
    qos_iommu_testdev_single_translation(&dma, &ctx,
                                         qriommu_single_translation_setup,
                                         qriommu_single_translation_attrs,
                                         qriommu_single_translation_validate,
                                         qriommu_single_translation_report,
                                         &ctx.dma_result);

    if (ctx.dma_result == 0 && ctx.config.expected_result == 0) {
        g_autofree uint8_t *buf = g_malloc(ctx.config.dma_len);

        qtest_memread(ctx.qts, ctx.config.dma_gpa, buf, ctx.config.dma_len);

        for (int i = 0; i < ctx.config.dma_len; i++) {
            uint8_t expected;

            expected = (ITD_DMA_WRITE_VAL >> ((i % 4) * 8)) & 0xff;
            g_assert_cmpuint(buf[i], ==, expected);
        }
    }
}

static uint32_t qriommu_get_table_index(uint64_t addr, int level)
{
    /* SV39: 39-bit virtual address, 3-level page table */
    switch (level) {
    case 0:
        return (addr >> 30) & 0x1ff;   /* L0: bits [38:30] */
    case 1:
        return (addr >> 21) & 0x1ff;   /* L1: bits [29:21] */
    case 2:
        return (addr >> 12) & 0x1ff;   /* L2: bits [20:12] */
    default:
        g_assert_not_reached();
    }
}

static uint64_t qriommu_get_table_addr(uint64_t base, int level, uint64_t iova)
{
    uint32_t index = qriommu_get_table_index(iova, level);
    return (base & QRIOMMU_PTE_PPN_MASK) + (index * 8);
}

static void qriommu_map_leaf(QTestState *qts, uint64_t root_pa,
                             uint64_t l0_pa, uint64_t l1_pa,
                             uint64_t l0_pte_val, uint64_t l1_pte_val,
                             uint64_t va, uint64_t pa, uint64_t leaf_attrs)
{
    uint64_t l0_addr = qriommu_get_table_addr(root_pa, 0, va);
    uint64_t l1_addr = qriommu_get_table_addr(l0_pa, 1, va);
    uint64_t l2_addr = qriommu_get_table_addr(l1_pa, 2, va);

    qtest_writeq(qts, l0_addr, l0_pte_val);
    qtest_writeq(qts, l1_addr, l1_pte_val);
    qtest_writeq(qts, l2_addr, qriommu_encode_pte(pa, leaf_attrs));
}

static uint64_t qriommu_get_pte_attrs(bool is_leaf)
{
    if (!is_leaf) {
        return QRIOMMU_NON_LEAF_PTE_MASK;
    }

    /* For leaf PTE, set RWX permissions */
    return QRIOMMU_LEAF_PTE_RW_MASK;
}

void qriommu_setup_translation_tables(QTestState *qts,
                                      uint64_t iova,
                                      QRIOMMUTransMode mode)
{
    uint64_t s_root = 0, s_l0_pte_val = 0, s_l1_pte_val = 0;
    uint64_t s_l0_addr = 0, s_l1_addr = 0, s_l2_addr = 0, s_l2_pte_val = 0;
    uint64_t s_l0_pa = 0, s_l1_pa = 0;
    uint64_t s_l2_pa = qriommu_apply_space_offs(QRIOMMU_L2_PTE_VAL);
    uint64_t s_l0_pa_real = 0, s_l1_pa_real = 0;
    uint64_t s_l2_pa_real = qriommu_apply_space_offs(QRIOMMU_L2_PTE_VAL);
    uint64_t non_leaf_attrs = qriommu_get_pte_attrs(false);
    uint64_t leaf_attrs = qriommu_get_pte_attrs(true);

    if (mode != QRIOMMU_TM_G_STAGE_ONLY) {
        /* Setup S-stage 3-level page tables (SV39) */
        s_l0_pa = qriommu_apply_space_offs(QRIOMMU_L0_PTE_VAL);
        s_l1_pa = qriommu_apply_space_offs(QRIOMMU_L1_PTE_VAL);
        s_root = qriommu_apply_space_offs(
            QRIOMMU_IOHGATP & QRIOMMU_PTE_PPN_MASK);
        s_l2_pa = qriommu_apply_space_offs(QRIOMMU_L2_PTE_VAL);

        s_l0_pa_real = s_l0_pa;
        s_l1_pa_real = s_l1_pa;
        s_l2_pa_real = s_l2_pa;

        if (mode == QRIOMMU_TM_NESTED) {
            s_l0_pa = QRIOMMU_L0_PTE_VAL;
            s_l1_pa = QRIOMMU_L1_PTE_VAL;
            s_l2_pa = QRIOMMU_L2_PTE_VAL;

            s_l0_pa_real = qriommu_apply_space_offs(QRIOMMU_L0_PTE_VAL);
            s_l1_pa_real = qriommu_apply_space_offs(QRIOMMU_L1_PTE_VAL);
            s_l2_pa_real = qriommu_apply_space_offs(QRIOMMU_L2_PTE_VAL);
        }

        s_l0_pte_val = qriommu_encode_pte(s_l0_pa, non_leaf_attrs);
        s_l1_pte_val = qriommu_encode_pte(s_l1_pa, non_leaf_attrs);

        s_l0_addr = qriommu_get_table_addr(s_root, 0, iova);
        qtest_writeq(qts, s_l0_addr, s_l0_pte_val);

        s_l1_addr = qriommu_get_table_addr(s_l0_pa_real, 1, iova);
        qtest_writeq(qts, s_l1_addr, s_l1_pte_val);

        s_l2_addr = qriommu_get_table_addr(s_l1_pa_real, 2, iova);
        s_l2_pte_val = qriommu_encode_pte(s_l2_pa, leaf_attrs);
        qtest_writeq(qts, s_l2_addr, s_l2_pte_val);
    }

    if (mode == QRIOMMU_TM_G_STAGE_ONLY || mode == QRIOMMU_TM_NESTED) {
        uint64_t g_root;
        uint64_t g_l0_pa;
        uint64_t g_l1_pa;
        uint64_t g_l0_pte_val;
        uint64_t g_l1_pte_val;

        g_root = qriommu_apply_space_offs(
            QRIOMMU_G_IOHGATP & QRIOMMU_PTE_PPN_MASK);
        g_l0_pa = qriommu_apply_space_offs(QRIOMMU_G_L0_PTE_VAL);
        g_l1_pa = qriommu_apply_space_offs(QRIOMMU_G_L1_PTE_VAL);
        g_l0_pte_val = qriommu_encode_pte(g_l0_pa, non_leaf_attrs);
        g_l1_pte_val = qriommu_encode_pte(g_l1_pa, non_leaf_attrs);

        if (mode == QRIOMMU_TM_G_STAGE_ONLY) {
            qriommu_map_leaf(qts, g_root, g_l0_pa, g_l1_pa,
                             g_l0_pte_val, g_l1_pte_val,
                             iova, s_l2_pa_real, leaf_attrs);
        } else {
            qriommu_map_leaf(qts, g_root, g_l0_pa, g_l1_pa,
                             g_l0_pte_val, g_l1_pte_val,
                             QRIOMMU_IOHGATP, s_root, leaf_attrs);
            qriommu_map_leaf(qts, g_root, g_l0_pa, g_l1_pa,
                             g_l0_pte_val, g_l1_pte_val,
                             QRIOMMU_L0_PTE_VAL, s_l0_pa_real, leaf_attrs);
            qriommu_map_leaf(qts, g_root, g_l0_pa, g_l1_pa,
                             g_l0_pte_val, g_l1_pte_val,
                             QRIOMMU_L1_PTE_VAL, s_l1_pa_real, leaf_attrs);
            qriommu_map_leaf(qts, g_root, g_l0_pa, g_l1_pa,
                             g_l0_pte_val, g_l1_pte_val,
                             QRIOMMU_L2_PTE_VAL, s_l2_pa_real, leaf_attrs);
        }
    }
}

uint32_t qriommu_build_translation(QTestState *qts, QRIOMMUTransMode mode,
                                   uint32_t device_id)
{
    uint64_t dc_addr, dc_addr_real;
    struct riscv_iommu_dc dc;
    uint64_t iohgatp;

    qtest_memset(qts, qriommu_apply_space_offs(QRIOMMU_DDT_BASE), 0, 0x1000);

    dc_addr = device_id * sizeof(struct riscv_iommu_dc) + QRIOMMU_DC_BASE;
    dc_addr_real = qriommu_apply_space_offs(dc_addr);

    /* Build Device Context (DC) */
    memset(&dc, 0, sizeof(dc));

    switch (mode) {
    case QRIOMMU_TM_BARE:
        /* Pass-through mode: tc.V=1, no FSC/IOHGATP */
        dc.tc = RISCV_IOMMU_DC_TC_V;
        break;

    case QRIOMMU_TM_S_STAGE_ONLY:
        /* S-stage only: tc.V=1, set FSC */
        dc.tc = RISCV_IOMMU_DC_TC_V;
        iohgatp = qriommu_apply_space_offs(QRIOMMU_IOHGATP);
        /* FSC mode: SV39 (mode=8) */
        dc.fsc = (iohgatp >> 12) | (8ull << 60);
        break;

    case QRIOMMU_TM_G_STAGE_ONLY:
        /* G-stage only: tc.V=1, set IOHGATP */
        dc.tc = RISCV_IOMMU_DC_TC_V;
        iohgatp = qriommu_apply_space_offs(QRIOMMU_G_IOHGATP);
        /* IOHGATP mode: SV39x4 (mode=8) */
        dc.iohgatp = (iohgatp >> 12) | (8ull << 60);
        break;

    case QRIOMMU_TM_NESTED:
        /* Nested: tc.V=1, set both FSC and IOHGATP */
        dc.tc = RISCV_IOMMU_DC_TC_V;
        /* FSC mode: SV39 (mode=8) */
        dc.fsc = (QRIOMMU_IOHGATP >> 12) | (8ull << 60);
        /* IOHGATP mode: SV39x4 (mode=8) */
        iohgatp = qriommu_apply_space_offs(QRIOMMU_G_IOHGATP);
        dc.iohgatp = (iohgatp >> 12) | (8ull << 60);
        break;

    default:
        g_assert_not_reached();
    }

    /* Write DC to memory */
    qtest_writeq(qts, dc_addr_real + 0,  dc.tc);
    qtest_writeq(qts, dc_addr_real + 8,  dc.iohgatp);
    qtest_writeq(qts, dc_addr_real + 16, dc.ta);
    qtest_writeq(qts, dc_addr_real + 24, dc.fsc);
    qtest_writeq(qts, dc_addr_real + 32, dc.msiptp);
    qtest_writeq(qts, dc_addr_real + 40, dc.msi_addr_mask);
    qtest_writeq(qts, dc_addr_real + 48, dc.msi_addr_pattern);
    qtest_writeq(qts, dc_addr_real + 56, dc._reserved);

    /* Setup translation tables if not in BARE mode */
    if (mode != QRIOMMU_TM_BARE) {
        qriommu_setup_translation_tables(qts, QRIOMMU_IOVA, mode);
    }

    return 0;
}

void qriommu_program_regs(QTestState *qts, uint64_t iommu_base)
{
    uint64_t ddtp, cqb, fqb;
    uint64_t cq_base, fq_base;
    uint64_t cq_align, fq_align;
    uint32_t cq_entries = QRIOMMU_QUEUE_ENTRIES;
    uint32_t fq_entries = QRIOMMU_QUEUE_ENTRIES;
    uint32_t cq_log2sz = ctz32(cq_entries) - 1;
    uint32_t fq_log2sz = ctz32(fq_entries) - 1;

    cq_base = qriommu_apply_space_offs(QRIOMMU_CQ_BASE_ADDR);
    fq_base = qriommu_apply_space_offs(QRIOMMU_FQ_BASE_ADDR);

    cq_align = MAX(0x1000ull, (uint64_t)cq_entries * QRIOMMU_CQ_ENTRY_SIZE);
    fq_align = MAX(0x1000ull, (uint64_t)fq_entries * QRIOMMU_FQ_ENTRY_SIZE);
    g_assert((cq_base & (cq_align - 1)) == 0);
    g_assert((fq_base & (fq_align - 1)) == 0);

    /* Setup Command Queue */
    cqb = (cq_base >> 12) << 10 | cq_log2sz;
    qtest_writeq(qts, iommu_base + RISCV_IOMMU_REG_CQB, cqb);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_CQH, 0);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_CQT, 0);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_CQCSR,
                 RISCV_IOMMU_CQCSR_CQEN);
    qriommu_wait_for_queue_active(qts, iommu_base, RISCV_IOMMU_REG_CQCSR,
                                  RISCV_IOMMU_CQCSR_CQON);

    /* Setup Fault Queue */
    fqb = (fq_base >> 12) << 10 | fq_log2sz;
    qtest_writeq(qts, iommu_base + RISCV_IOMMU_REG_FQB, fqb);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_FQH, 0);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_FQT, 0);
    qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_FQCSR,
                 RISCV_IOMMU_FQCSR_FQEN);
    qriommu_wait_for_queue_active(qts, iommu_base, RISCV_IOMMU_REG_FQCSR,
                                  RISCV_IOMMU_FQCSR_FQON);

    /* Set Device Directory Table Pointer (DDTP) */
    ddtp = qriommu_apply_space_offs(QRIOMMU_DDT_BASE);
    g_assert((ddtp & 0xfff) == 0);
    ddtp = ((ddtp >> 12) << 10) | RISCV_IOMMU_DDTP_MODE_1LVL;
    qtest_writeq(qts, iommu_base + RISCV_IOMMU_REG_DDTP, ddtp);
    g_assert((qtest_readq(qts, iommu_base + RISCV_IOMMU_REG_DDTP) &
              (RISCV_IOMMU_DDTP_PPN | RISCV_IOMMU_DDTP_MODE)) ==
             (ddtp & (RISCV_IOMMU_DDTP_PPN | RISCV_IOMMU_DDTP_MODE)));
}
