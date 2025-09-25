/*
 * QTest for SMMUv3 with iommu-testdev
 *
 * This QTest file is used to test the SMMUv3 with iommu-testdev so that we can
 * test SMMUv3 without any guest kernel or firmware.
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/generic-pcihost.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/iommu-testdev.h"
#include "libqos/qos-smmuv3.h"

#define DMA_LEN           4

static uint64_t smmuv3_expected_gpa(uint64_t iova)
{
    return qsmmu_space_offset(QSMMU_SPACE_NONSECURE) +
           QSMMU_L3_PTE_VAL + (iova & 0xfff);
}

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;

    *pdev = dev;
}

static QPCIDevice *setup_qtest_pci_device(QTestState *qts, QGenericPCIBus *gbus,
                                          QPCIBar *bar)
{
    QPCIDevice *dev = NULL;

    qpci_init_generic(gbus, qts, NULL, false);

    qpci_device_foreach(&gbus->bus, IOMMU_TESTDEV_VENDOR_ID,
                        IOMMU_TESTDEV_DEVICE_ID, save_fn, &dev);
    g_assert(dev);

    qpci_device_enable(dev);
    *bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar->is_io);

    return dev;
}

static void run_smmuv3_translation(const QSMMUTestConfig *cfg)
{
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    /* Initialize QEMU environment for SMMU testing */
    qts = qtest_init("-machine virt,acpi=off,gic-version=3,iommu=smmuv3 "
                     "-smp 1 -m 512 -cpu max -net none "
                     "-device iommu-testdev");

    /* Setup and configure PCI device */
    dev = setup_qtest_pci_device(qts, &gbus, &bar);
    g_assert(dev);

    g_test_message("### SMMUv3 translation mode=%d sec_sid=%d ###",
                   cfg->trans_mode, cfg->sec_sid);
    qsmmu_run_translation_case(qts, dev, bar, VIRT_SMMU_BASE, cfg);
    qtest_quit(qts);
}

static void test_smmuv3_ns_s1_only(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_S1_ONLY,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_gpa = smmuv3_expected_gpa(QSMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

static void test_smmuv3_ns_s2_only(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_S2_ONLY,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_gpa = smmuv3_expected_gpa(QSMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

static void test_smmuv3_ns_nested(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_NESTED,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_gpa = smmuv3_expected_gpa(QSMMU_IOVA),
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/iommu-testdev/translation/ns-s1-only",
                   test_smmuv3_ns_s1_only);
    qtest_add_func("/iommu-testdev/translation/ns-s2-only",
                   test_smmuv3_ns_s2_only);
    qtest_add_func("/iommu-testdev/translation/ns-nested",
                   test_smmuv3_ns_nested);
    return g_test_run();
}
