/*
 * QTest for Intel IOMMU (VT-d) with iommu-testdev
 *
 * This QTest file is used to test the Intel IOMMU with iommu-testdev so that
 * we can test VT-d without any guest kernel or firmware.
 *
 * Copyright (c) 2026 Fengyuan Yu <15fengyuan@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/i386/intel_iommu_internal.h"
#include "hw/misc/iommu-testdev.h"
#include "libqos/qos-intel-iommu.h"

#define DMA_LEN           4

static uint64_t intel_iommu_expected_gpa(uint64_t iova)
{
    return (QVTD_PT_VAL & VTD_PAGE_MASK_4K) + (iova & 0xfff);
}

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;

    *pdev = dev;
}

static QPCIDevice *setup_qtest_pci_device(QTestState *qts, QPCIBus **pcibus,
                                          QPCIBar *bar)
{
    QPCIDevice *dev = NULL;

    *pcibus = qpci_new_pc(qts, NULL);
    g_assert(*pcibus != NULL);

    qpci_device_foreach(*pcibus, IOMMU_TESTDEV_VENDOR_ID,
                        IOMMU_TESTDEV_DEVICE_ID, save_fn, &dev);

    g_assert(dev);
    qpci_device_enable(dev);
    *bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar->is_io);

    return dev;
}

static const char *qvtd_iommu_args(QVTDTransMode mode)
{
    switch (mode) {
    case QVTD_TM_SCALABLE_FLT:
        return "-device intel-iommu,scalable-mode=on,flts=on ";
    case QVTD_TM_SCALABLE_PT:
    case QVTD_TM_SCALABLE_SLT:
        return "-device intel-iommu,scalable-mode=on ";
    default:
        return "-device intel-iommu ";
    }
}

static bool qvtd_check_caps(QTestState *qts, QVTDTransMode mode)
{
    uint64_t ecap = qtest_readq(qts,
                                Q35_HOST_BRIDGE_IOMMU_ADDR + DMAR_ECAP_REG);

    /* All scalable modes require SMTS */
    if (qvtd_is_scalable(mode) && !(ecap & VTD_ECAP_SMTS)) {
        g_test_skip("ECAP.SMTS not supported");
        return false;
    }

    switch (mode) {
    case QVTD_TM_SCALABLE_PT:
        if (!(ecap & VTD_ECAP_PT)) {
            g_test_skip("ECAP.PT not supported");
            return false;
        }
        break;
    case QVTD_TM_SCALABLE_SLT:
        if (!(ecap & VTD_ECAP_SSTS)) {
            g_test_skip("ECAP.SSTS not supported");
            return false;
        }
        break;
    case QVTD_TM_SCALABLE_FLT:
        if (!(ecap & VTD_ECAP_FSTS)) {
            g_test_skip("ECAP.FSTS not supported");
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}

static void run_intel_iommu_translation(const QVTDTestConfig *cfg)
{
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
        return;
    }

    /* Initialize QEMU environment for Intel IOMMU testing */
    qts = qtest_initf("-machine q35 -smp 1 -m 512 -net none "
                      "%s -device iommu-testdev",
                      qvtd_iommu_args(cfg->trans_mode));

    /* Check CAP/ECAP capabilities for required translation mode */
    if (!qvtd_check_caps(qts, cfg->trans_mode)) {
        qtest_quit(qts);
        return;
    }

    /* Setup and configure IOMMU-testdev PCI device */
    dev = setup_qtest_pci_device(qts, &pcibus, &bar);
    g_assert(dev);

    g_test_message("### Intel IOMMU translation mode=%d ###", cfg->trans_mode);
    qvtd_run_translation_case(qts, dev, bar, Q35_HOST_BRIDGE_IOMMU_ADDR, cfg);
    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

static void test_intel_iommu_legacy_pt(void)
{
    QVTDTestConfig cfg = {
        .trans_mode = QVTD_TM_LEGACY_PT,
        .dma_gpa = QVTD_IOVA,  /* pass-through: GPA == IOVA */
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_intel_iommu_translation(&cfg);
}

static void test_intel_iommu_legacy_trans(void)
{
    QVTDTestConfig cfg = {
        .trans_mode = QVTD_TM_LEGACY_TRANS,
        .dma_gpa = intel_iommu_expected_gpa(QVTD_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_intel_iommu_translation(&cfg);
}

static void test_intel_iommu_scalable_pt(void)
{
    QVTDTestConfig cfg = {
        .trans_mode = QVTD_TM_SCALABLE_PT,
        .dma_gpa = QVTD_IOVA,  /* pass-through: GPA == IOVA */
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_intel_iommu_translation(&cfg);
}

static void test_intel_iommu_scalable_slt(void)
{
    QVTDTestConfig cfg = {
        .trans_mode = QVTD_TM_SCALABLE_SLT,
        .dma_gpa = intel_iommu_expected_gpa(QVTD_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_intel_iommu_translation(&cfg);
}

static void test_intel_iommu_scalable_flt(void)
{
    QVTDTestConfig cfg = {
        .trans_mode = QVTD_TM_SCALABLE_FLT,
        .dma_gpa = intel_iommu_expected_gpa(QVTD_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_intel_iommu_translation(&cfg);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* Legacy mode tests */
    qtest_add_func("/iommu-testdev/intel/legacy-pt",
                   test_intel_iommu_legacy_pt);
    qtest_add_func("/iommu-testdev/intel/legacy-trans",
                   test_intel_iommu_legacy_trans);

    /* Scalable mode tests */
    qtest_add_func("/iommu-testdev/intel/scalable-pt",
                   test_intel_iommu_scalable_pt);
    qtest_add_func("/iommu-testdev/intel/scalable-slt",
                   test_intel_iommu_scalable_slt);
    qtest_add_func("/iommu-testdev/intel/scalable-flt",
                   test_intel_iommu_scalable_flt);

    return g_test_run();
}
