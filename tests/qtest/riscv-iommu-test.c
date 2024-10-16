/*
 * QTest testcase for RISC-V IOMMU
 *
 * Copyright (c) 2024 Ventana Micro Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/riscv-iommu.h"
#include "hw/pci/pci_regs.h"

static uint32_t riscv_iommu_read_reg32(QRISCVIOMMU *r_iommu, int reg_offset)
{
    return qpci_io_readl(&r_iommu->dev, r_iommu->reg_bar, reg_offset);
}

static uint64_t riscv_iommu_read_reg64(QRISCVIOMMU *r_iommu, int reg_offset)
{
    return qpci_io_readq(&r_iommu->dev, r_iommu->reg_bar, reg_offset);
}

static void test_pci_config(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QRISCVIOMMU *r_iommu = obj;
    QPCIDevice *dev = &r_iommu->dev;
    uint16_t vendorid, deviceid, classid;

    vendorid = qpci_config_readw(dev, PCI_VENDOR_ID);
    deviceid = qpci_config_readw(dev, PCI_DEVICE_ID);
    classid = qpci_config_readw(dev, PCI_CLASS_DEVICE);

    g_assert_cmpuint(vendorid, ==, RISCV_IOMMU_PCI_VENDOR_ID);
    g_assert_cmpuint(deviceid, ==, RISCV_IOMMU_PCI_DEVICE_ID);
    g_assert_cmpuint(classid, ==, RISCV_IOMMU_PCI_DEVICE_CLASS);
}

static void test_reg_reset(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QRISCVIOMMU *r_iommu = obj;
    uint64_t cap;
    uint32_t reg;

    cap = riscv_iommu_read_reg64(r_iommu, RISCV_IOMMU_REG_CAP);
    g_assert_cmpuint(cap & RISCV_IOMMU_CAP_VERSION, ==, 0x10);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_CQCSR);
    g_assert_cmpuint(reg & RISCV_IOMMU_CQCSR_CQEN, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_CQCSR_CIE, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_CQCSR_CQON, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_CQCSR_BUSY, ==, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_FQCSR);
    g_assert_cmpuint(reg & RISCV_IOMMU_FQCSR_FQEN, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_FQCSR_FIE, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_FQCSR_FQON, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_FQCSR_BUSY, ==, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_PQCSR);
    g_assert_cmpuint(reg & RISCV_IOMMU_PQCSR_PQEN, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_PQCSR_PIE, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_PQCSR_PQON, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_PQCSR_BUSY, ==, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_DDTP);
    g_assert_cmpuint(reg & RISCV_IOMMU_DDTP_BUSY, ==, 0);
    g_assert_cmpuint(reg & RISCV_IOMMU_DDTP_MODE, ==,
                     RISCV_IOMMU_DDTP_MODE_OFF);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_IPSR);
    g_assert_cmpuint(reg, ==, 0);
}

static void register_riscv_iommu_test(void)
{
    qos_add_test("pci_config", "riscv-iommu-pci", test_pci_config, NULL);
    qos_add_test("reg_reset", "riscv-iommu-pci", test_reg_reset, NULL);
}

libqos_init(register_riscv_iommu_test);
