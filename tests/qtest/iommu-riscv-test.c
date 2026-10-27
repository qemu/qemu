/*
 * QTest testcase for RISC-V IOMMU with iommu-testdev
 *
 * This QTest file is used to test the RISC-V IOMMU with iommu-testdev so that
 * we can test RISC-V IOMMU without any guest kernel or firmware.
 *
 * Copyright (c) 2026 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqos/generic-pcihost.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/iommu-testdev.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "libqos/qos-riscv-iommu.h"
#include "libqos/riscv-iommu.h"

#define DMA_LEN           4

#define DBG_PAGE_SIZE           0x1000ull
#define DBG_NAPOT_SIZE          0x10000ull
#define DBG_SUPERPAGE_SIZE      0x200000ull
#define DBG_TRANSLATED_PA       0x90000000ull
#define DBG_NESTED_GPA          0x200000ull

/* RISC-V virt machine PCI configuration */
#define RISCV_GPEX_PIO_BASE        0x3000000
#define RISCV_BUS_PIO_LIMIT        0x10000
#define RISCV_BUS_MMIO_ALLOC_PTR   0x40000000
#define RISCV_BUS_MMIO_LIMIT       0x80000000
#define RISCV_ECAM_ALLOC_PTR       0x30000000

typedef struct RiscvIommuTestState {
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *iommu_dev;
    QPCIDevice *testdev;
    QPCIBar testdev_bar;
    uint64_t iommu_base;
} RiscvIommuTestState;

typedef struct RiscvIommuDbgTest {
    const char *name;
    QRIOMMUTransMode mode;
    uint64_t s_page_size;
    uint64_t g_page_size;
    uint64_t pa;
    uint64_t expected_response;
} RiscvIommuDbgTest;

static const RiscvIommuDbgTest debug_tests[] = {
    {
        .name = "page-4k",
        .mode = QRIOMMU_TM_S_STAGE_ONLY,
        .s_page_size = DBG_PAGE_SIZE,
        .pa = DBG_TRANSLATED_PA + DBG_PAGE_SIZE,
        .expected_response = 0x24000400ull,
    }, {
        .name = "napot-64k",
        .mode = QRIOMMU_TM_S_STAGE_ONLY,
        .s_page_size = DBG_NAPOT_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x24001e00ull,
    }, {
        .name = "superpage",
        .mode = QRIOMMU_TM_S_STAGE_ONLY,
        .s_page_size = DBG_SUPERPAGE_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x2403fe00ull,
    }, {
        .name = "g-stage-superpage",
        .mode = QRIOMMU_TM_G_STAGE_ONLY,
        .g_page_size = DBG_SUPERPAGE_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x2403fe00ull,
    }, {
        .name = "nested-s4k-g2m",
        .mode = QRIOMMU_TM_NESTED,
        .s_page_size = DBG_PAGE_SIZE,
        .g_page_size = DBG_SUPERPAGE_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x24001000ull,
    }, {
        .name = "nested-s2m-g4k",
        .mode = QRIOMMU_TM_NESTED,
        .s_page_size = DBG_SUPERPAGE_SIZE,
        .g_page_size = DBG_PAGE_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x24001000ull,
    }, {
        .name = "nested-s2m-g2m",
        .mode = QRIOMMU_TM_NESTED,
        .s_page_size = DBG_SUPERPAGE_SIZE,
        .g_page_size = DBG_SUPERPAGE_SIZE,
        .pa = DBG_TRANSLATED_PA,
        .expected_response = 0x2403fe00ull,
    },
};

static void riscv_config_qpci_bus(QGenericPCIBus *qpci)
{
    qpci->gpex_pio_base = RISCV_GPEX_PIO_BASE;
    qpci->bus.pio_limit = RISCV_BUS_PIO_LIMIT;
    qpci->bus.mmio_alloc_ptr = RISCV_BUS_MMIO_ALLOC_PTR;
    qpci->bus.mmio_limit = RISCV_BUS_MMIO_LIMIT;
    qpci->ecam_alloc_ptr = RISCV_ECAM_ALLOC_PTR;
}

static uint64_t riscv_iommu_expected_gpa(uint64_t iova)
{
    return QRIOMMU_SPACE_OFFS + QRIOMMU_L2_PTE_VAL + (iova & 0xfff);
}

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;
    uint16_t vendor = qpci_config_readw(dev, 0);
    uint16_t device = qpci_config_readw(dev, 2);

    g_test_message("Found PCI device: vendor=0x%04x device=0x%04x devfn=0x%02x",
                   vendor, device, devfn);

    if (!*pdev) {
        *pdev = dev;
    }
}

static QPCIDevice *find_riscv_iommu_pci(QGenericPCIBus *gbus,
                                        uint64_t *iommu_base)
{
    QPCIDevice *iommu_dev = NULL;
    QPCIBar iommu_bar;

    g_test_message("Searching for riscv-iommu-pci "
                   "(vendor=0x%04x, device=0x%04x)",
                   RISCV_IOMMU_PCI_VENDOR_ID, RISCV_IOMMU_PCI_DEVICE_ID);

    qpci_device_foreach(&gbus->bus, RISCV_IOMMU_PCI_VENDOR_ID,
                        RISCV_IOMMU_PCI_DEVICE_ID, save_fn, &iommu_dev);

    if (!iommu_dev) {
        g_test_message("riscv-iommu-pci device not found!");
        return NULL;
    }

    g_test_message("Found riscv-iommu-pci at devfn=0x%02x", iommu_dev->devfn);

    qpci_device_enable(iommu_dev);
    iommu_bar = qpci_iomap(iommu_dev, 0, NULL);
    g_assert_false(iommu_bar.is_io);

    *iommu_base = iommu_bar.addr;
    g_test_message("RISC-V IOMMU MMIO base address: 0x%" PRIx64, *iommu_base);

    return iommu_dev;
}

static QPCIDevice *find_iommu_testdev(QGenericPCIBus *gbus, QPCIBar *bar)
{
    QPCIDevice *dev = NULL;

    g_test_message("Searching for iommu-testdev (vendor=0x%04x, device=0x%04x)",
                   IOMMU_TESTDEV_VENDOR_ID, IOMMU_TESTDEV_DEVICE_ID);

    qpci_device_foreach(&gbus->bus, IOMMU_TESTDEV_VENDOR_ID,
                        IOMMU_TESTDEV_DEVICE_ID, save_fn, &dev);
    g_assert(dev);

    qpci_device_enable(dev);
    *bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar->is_io);

    return dev;
}

static bool riscv_iommu_test_setup(RiscvIommuTestState *state)
{
    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return false;
    }

    state->qts = qtest_init("-machine virt,acpi=off "
                            "-cpu max -smp 1 -m 512 -net none "
                            "-device riscv-iommu-pci "
                            "-device iommu-testdev");

    qpci_init_generic(&state->gbus, state->qts, NULL, false);
    riscv_config_qpci_bus(&state->gbus);

    state->iommu_dev = find_riscv_iommu_pci(&state->gbus, &state->iommu_base);
    g_assert(state->iommu_dev);

    state->testdev = find_iommu_testdev(&state->gbus, &state->testdev_bar);
    g_assert(state->testdev);

    return true;
}

static void riscv_iommu_test_teardown(RiscvIommuTestState *state)
{
    g_free(state->iommu_dev);
    g_free(state->testdev);
    qtest_quit(state->qts);
}

static uint64_t riscv_iommu_check(QTestState *qts, uint64_t iommu_base,
                                  QRIOMMUTransMode mode)
{
    uint64_t cap;
    uint64_t ddtp;
    uint32_t cqcsr;
    uint32_t fqcsr;
    uint32_t pqcsr;
    uint32_t fctl;
    uint32_t fctl_mask;
    uint32_t fctl_desired;
    uint32_t igs;

    cap = qtest_readq(qts, iommu_base + RISCV_IOMMU_REG_CAP);
    g_assert_cmpuint((uint32_t)(cap & RISCV_IOMMU_CAP_VERSION), ==,
                     RISCV_IOMMU_SPEC_DOT_VER);

    fctl = qtest_readl(qts, iommu_base + RISCV_IOMMU_REG_FCTL);
    igs = (cap & RISCV_IOMMU_CAP_IGS) >> 28;
    g_assert_cmpuint(igs, <=, RISCV_IOMMU_CAP_IGS_BOTH);

    fctl_mask = RISCV_IOMMU_FCTL_BE | RISCV_IOMMU_FCTL_WSI |
                RISCV_IOMMU_FCTL_GXL;
    fctl_desired = fctl & ~fctl_mask;
    if (igs == RISCV_IOMMU_CAP_IGS_WSI) {
        fctl_desired |= RISCV_IOMMU_FCTL_WSI;
    }

    if ((fctl & fctl_mask) != (fctl_desired & fctl_mask)) {
        ddtp = qtest_readq(qts, iommu_base + RISCV_IOMMU_REG_DDTP);
        cqcsr = qtest_readl(qts, iommu_base + RISCV_IOMMU_REG_CQCSR);
        fqcsr = qtest_readl(qts, iommu_base + RISCV_IOMMU_REG_FQCSR);
        pqcsr = qtest_readl(qts, iommu_base + RISCV_IOMMU_REG_PQCSR);

        g_assert_cmpuint((uint32_t)(ddtp & RISCV_IOMMU_DDTP_MODE), ==,
                         RISCV_IOMMU_DDTP_MODE_OFF);
        g_assert_cmpuint(cqcsr & RISCV_IOMMU_CQCSR_CQON, ==, 0);
        g_assert_cmpuint(fqcsr & RISCV_IOMMU_FQCSR_FQON, ==, 0);
        g_assert_cmpuint(pqcsr & RISCV_IOMMU_PQCSR_PQON, ==, 0);

        qtest_writel(qts, iommu_base + RISCV_IOMMU_REG_FCTL, fctl_desired);
        fctl = qtest_readl(qts, iommu_base + RISCV_IOMMU_REG_FCTL);
    }

    g_assert_cmpuint(fctl & fctl_mask, ==, fctl_desired & fctl_mask);

    if (mode == QRIOMMU_TM_S_STAGE_ONLY || mode == QRIOMMU_TM_NESTED) {
        g_assert((cap & RISCV_IOMMU_CAP_SV39) != 0);
    }
    if (mode == QRIOMMU_TM_G_STAGE_ONLY || mode == QRIOMMU_TM_NESTED) {
        g_assert((cap & RISCV_IOMMU_CAP_SV39X4) != 0);
        g_assert_cmpuint(fctl & RISCV_IOMMU_FCTL_GXL, ==, 0);
    }

    return cap;
}

static void run_riscv_iommu_translation(const QRIOMMUTestConfig *cfg)
{
    RiscvIommuTestState state = { 0 };

    if (!riscv_iommu_test_setup(&state)) {
        return;
    }

    riscv_iommu_check(state.qts, state.iommu_base, cfg->trans_mode);

    g_test_message("### RISC-V IOMMU translation mode=%d ###",
                   cfg->trans_mode);
    qriommu_run_translation_case(state.qts, state.testdev, state.testdev_bar,
                                 state.iommu_base, cfg);
    riscv_iommu_test_teardown(&state);
}

static void test_riscv_iommu_bare(void)
{
    QRIOMMUTestConfig cfg = {
        .trans_mode = QRIOMMU_TM_BARE,
        .dma_gpa = QRIOMMU_IOVA,
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_riscv_iommu_translation(&cfg);
}

static void test_riscv_iommu_s_stage_only(void)
{
    QRIOMMUTestConfig cfg = {
        .trans_mode = QRIOMMU_TM_S_STAGE_ONLY,
        .dma_gpa = riscv_iommu_expected_gpa(QRIOMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_riscv_iommu_translation(&cfg);
}

static void test_riscv_iommu_g_stage_only(void)
{
    QRIOMMUTestConfig cfg = {
        .trans_mode = QRIOMMU_TM_G_STAGE_ONLY,
        .dma_gpa = riscv_iommu_expected_gpa(QRIOMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_riscv_iommu_translation(&cfg);
}

static void test_riscv_iommu_nested(void)
{
    QRIOMMUTestConfig cfg = {
        .trans_mode = QRIOMMU_TM_NESTED,
        .dma_gpa = riscv_iommu_expected_gpa(QRIOMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_riscv_iommu_translation(&cfg);
}

static void riscv_iommu_dbg_map_leaf(QTestState *qts, uint64_t l1_pa,
                                    uint64_t l2_pa, uint64_t va, uint64_t pa,
                                    uint64_t page_size)
{
    uint64_t pte = ((pa >> 12) << 10) | QRIOMMU_LEAF_PTE_RW_MASK;
    uint64_t pte_addr;

    g_assert_cmphex(pa & (page_size - 1), ==, 0);

    if (page_size == DBG_SUPERPAGE_SIZE) {
        pte_addr = l1_pa + (((va >> 21) & 0x1ff) * sizeof(uint64_t));
        qtest_writeq(qts, pte_addr, pte);
    } else if (page_size == DBG_NAPOT_SIZE) {
        /* A 64 KiB NAPOT PTE has N=1 and PPN[3:0]=1000. */
        pte |= BIT_ULL(63) | (8ull << 10);
        va &= ~(DBG_NAPOT_SIZE - 1);
        for (unsigned i = 0; i < DBG_NAPOT_SIZE / DBG_PAGE_SIZE; i++) {
            pte_addr = l2_pa + ((((va >> 12) + i) & 0x1ff) *
                                sizeof(uint64_t));
            qtest_writeq(qts, pte_addr, pte);
        }
    } else {
        g_assert_cmphex(page_size, ==, DBG_PAGE_SIZE);
        pte_addr = l2_pa + (((va >> 12) & 0x1ff) * sizeof(uint64_t));
        qtest_writeq(qts, pte_addr, pte);
    }
}

static uint64_t riscv_iommu_dbg_translate(RiscvIommuTestState *state,
                                         uint64_t iova)
{
    uint64_t request;

    qtest_writeq(state->qts,
                 state->iommu_base + RISCV_IOMMU_REG_TR_REQ_IOVA, iova);
    request = ((uint64_t)state->testdev->devfn << 40) |
              RISCV_IOMMU_TR_REQ_CTL_GO_BUSY;
    qtest_writeq(state->qts,
                 state->iommu_base + RISCV_IOMMU_REG_TR_REQ_CTL, request);

    request = qtest_readq(state->qts,
                          state->iommu_base + RISCV_IOMMU_REG_TR_REQ_CTL);
    g_assert_cmpuint(request & RISCV_IOMMU_TR_REQ_CTL_GO_BUSY, ==, 0);

    return qtest_readq(state->qts,
                       state->iommu_base + RISCV_IOMMU_REG_TR_RESPONSE);
}

static void test_riscv_iommu_dbg(gconstpointer data)
{
    const RiscvIommuDbgTest *cfg = data;
    RiscvIommuTestState state = { 0 };
    uint64_t iova = QRIOMMU_IOVA & ~(DBG_PAGE_SIZE - 1);
    uint64_t s_pa = cfg->pa;
    uint64_t page_size = cfg->mode == QRIOMMU_TM_G_STAGE_ONLY ?
                         cfg->g_page_size : cfg->s_page_size;
    uint64_t response;

    if (!riscv_iommu_test_setup(&state)) {
        return;
    }

    riscv_iommu_check(state.qts, state.iommu_base, cfg->mode);
    qriommu_build_translation(state.qts, cfg->mode, state.testdev->devfn);

    if (cfg->mode == QRIOMMU_TM_NESTED) {
        uint64_t g_l1_pa = QRIOMMU_SPACE_OFFS + QRIOMMU_G_L0_PTE_VAL;
        uint64_t g_l2_pa = QRIOMMU_SPACE_OFFS + QRIOMMU_G_L1_PTE_VAL;
        uint64_t offset = iova & (DBG_SUPERPAGE_SIZE - 1);
        uint64_t gpa = DBG_NESTED_GPA + offset;
        uint64_t pte_addr;
        uint64_t pte;

        /* Map all S-stage page tables through one 2 MiB G-stage leaf. */
        riscv_iommu_dbg_map_leaf(state.qts, g_l1_pa, g_l2_pa, 0,
                                 QRIOMMU_SPACE_OFFS, DBG_SUPERPAGE_SIZE);

        if (cfg->g_page_size == DBG_PAGE_SIZE) {
            pte_addr = g_l1_pa + (((gpa >> 21) & 0x1ff) * sizeof(uint64_t));
            pte = ((g_l2_pa >> 12) << 10) | QRIOMMU_NON_LEAF_PTE_MASK;
            qtest_writeq(state.qts, pte_addr, pte);
        }
        riscv_iommu_dbg_map_leaf(state.qts, g_l1_pa, g_l2_pa, gpa,
                                 cfg->pa + (offset & ~(cfg->g_page_size - 1)),
                                 cfg->g_page_size);
        s_pa = gpa & ~(cfg->s_page_size - 1);
        page_size = MIN(page_size, cfg->g_page_size);
    }

    if (cfg->mode == QRIOMMU_TM_G_STAGE_ONLY) {
        riscv_iommu_dbg_map_leaf(state.qts,
                                 QRIOMMU_SPACE_OFFS + QRIOMMU_G_L0_PTE_VAL,
                                 QRIOMMU_SPACE_OFFS + QRIOMMU_G_L1_PTE_VAL,
                                 iova, cfg->pa, cfg->g_page_size);
    } else {
        riscv_iommu_dbg_map_leaf(state.qts,
                                 QRIOMMU_SPACE_OFFS + QRIOMMU_L0_PTE_VAL,
                                 QRIOMMU_SPACE_OFFS + QRIOMMU_L1_PTE_VAL,
                                 iova, s_pa, cfg->s_page_size);
    }
    qriommu_program_regs(state.qts, state.iommu_base);

    /*
     * The spec permits reporting a smaller range. These tests check QEMU's
     * choice to report the effective range returned by an uncached walk.
     * No DMA precedes these requests, and debug requests do not fill the IOTLB.
     */
    response = riscv_iommu_dbg_translate(&state, iova);
    g_assert_cmphex(response, ==, cfg->expected_response);

    if (page_size > DBG_PAGE_SIZE) {
        /* Check both ends as well as the interior of the reported range. */
        iova &= ~(page_size - 1);
        response = riscv_iommu_dbg_translate(&state, iova);
        g_assert_cmphex(response, ==, cfg->expected_response);
        response = riscv_iommu_dbg_translate(&state,
                                            iova + page_size - DBG_PAGE_SIZE);
        g_assert_cmphex(response, ==, cfg->expected_response);
    }

    riscv_iommu_test_teardown(&state);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/iommu-testdev/translation/bare",
                   test_riscv_iommu_bare);
    qtest_add_func("/iommu-testdev/translation/s-stage-only",
                   test_riscv_iommu_s_stage_only);
    qtest_add_func("/iommu-testdev/translation/g-stage-only",
                   test_riscv_iommu_g_stage_only);
    qtest_add_func("/iommu-testdev/translation/ns-nested",
                   test_riscv_iommu_nested);
    for (unsigned i = 0; i < G_N_ELEMENTS(debug_tests); i++) {
        g_autofree char *path = g_strdup_printf("/iommu-testdev/debug/%s",
                                               debug_tests[i].name);

        qtest_add_data_func(path, &debug_tests[i], test_riscv_iommu_dbg);
    }
    return g_test_run();
}
