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

static void riscv_iommu_write_reg32(QRISCVIOMMU *r_iommu, int reg_offset,
                                    uint32_t val)
{
    qpci_io_writel(&r_iommu->dev, r_iommu->reg_bar, reg_offset, val);
}

static void riscv_iommu_write_reg64(QRISCVIOMMU *r_iommu, int reg_offset,
                                    uint64_t val)
{
    qpci_io_writeq(&r_iommu->dev, r_iommu->reg_bar, reg_offset, val);
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

/*
 * Common timeout-based poll for CQCSR, FQCSR and PQCSR. All
 * their ON bits are mapped as RISCV_IOMMU_QUEUE_ACTIVE (16),
 */
static void qtest_wait_for_queue_active(QRISCVIOMMU *r_iommu,
                                        uint32_t queue_csr)
{
    QTestState *qts = global_qtest;
    guint64 timeout_us = 2 * 1000 * 1000;
    gint64 start_time = g_get_monotonic_time();
    uint32_t reg;

    for (;;) {
        qtest_clock_step(qts, 100);

        reg = riscv_iommu_read_reg32(r_iommu, queue_csr);
        if (reg & RISCV_IOMMU_QUEUE_ACTIVE) {
            break;
        }
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

/*
 * Goes through the queue activation procedures of chapter 6.2,
 * "Guidelines for initialization", of the RISCV-IOMMU spec.
 */
static void test_iommu_init_queues(void *obj, void *data,
                                   QGuestAllocator *t_alloc)
{
    QRISCVIOMMU *r_iommu = obj;
    uint64_t reg64, q_addr;
    uint32_t reg;
    int k = 2;

    reg64 = riscv_iommu_read_reg64(r_iommu, RISCV_IOMMU_REG_CAP);
    g_assert_cmpuint(reg64 & RISCV_IOMMU_CAP_VERSION, ==, 0x10);

    /*
     * Program the command queue. Write 0xF to civ, fiv, pmiv and
     * piv. With the current PCI device impl we expect 2 writable
     * bits for each (k = 2) since we have N = 4 total vectors (2^k).
     */
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_IVEC, 0xFFFF);
    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_IVEC);
    g_assert_cmpuint(reg & RISCV_IOMMU_REG_IVEC_CIV, ==, 0x3);
    g_assert_cmpuint(reg & RISCV_IOMMU_REG_IVEC_FIV, ==, 0x30);
    g_assert_cmpuint(reg & RISCV_IOMMU_REG_IVEC_PMIV, ==, 0x300);
    g_assert_cmpuint(reg & RISCV_IOMMU_REG_IVEC_PIV, ==, 0x3000);

    /* Alloc a 4*16 bytes buffer and use it to set cqb */
    q_addr = guest_alloc(t_alloc, 4 * 16);
    reg64 = 0;
    deposit64(reg64, RISCV_IOMMU_CQB_PPN_START,
              RISCV_IOMMU_CQB_PPN_LEN, q_addr);
    deposit64(reg64, RISCV_IOMMU_CQB_LOG2SZ_START,
              RISCV_IOMMU_CQB_LOG2SZ_LEN, k - 1);
    riscv_iommu_write_reg64(r_iommu, RISCV_IOMMU_REG_CQB, reg64);

    /* cqt = 0, cqcsr.cqen = 1, poll cqcsr.cqon until it reads 1 */
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_CQT, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_CQCSR);
    reg |= RISCV_IOMMU_CQCSR_CQEN;
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_CQCSR, reg);

    qtest_wait_for_queue_active(r_iommu, RISCV_IOMMU_REG_CQCSR);

    /*
     * Program the fault queue. Alloc a 4*32 bytes (instead of 4*16)
     * buffer and use it to set fqb.
     */
    q_addr = guest_alloc(t_alloc, 4 * 32);
    reg64 = 0;
    deposit64(reg64, RISCV_IOMMU_FQB_PPN_START,
              RISCV_IOMMU_FQB_PPN_LEN, q_addr);
    deposit64(reg64, RISCV_IOMMU_FQB_LOG2SZ_START,
              RISCV_IOMMU_FQB_LOG2SZ_LEN, k - 1);
    riscv_iommu_write_reg64(r_iommu, RISCV_IOMMU_REG_FQB, reg64);

    /* fqt = 0, fqcsr.fqen = 1, poll fqcsr.fqon until it reads 1 */
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_FQT, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_FQCSR);
    reg |= RISCV_IOMMU_FQCSR_FQEN;
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_FQCSR, reg);

    qtest_wait_for_queue_active(r_iommu, RISCV_IOMMU_REG_FQCSR);

    /*
     * Program the page-request queue. Alloc a 4*16 bytes buffer
     * and use it to set pqb.
     */
    q_addr = guest_alloc(t_alloc, 4 * 16);
    reg64 = 0;
    deposit64(reg64, RISCV_IOMMU_PQB_PPN_START,
              RISCV_IOMMU_PQB_PPN_LEN, q_addr);
    deposit64(reg64, RISCV_IOMMU_PQB_LOG2SZ_START,
              RISCV_IOMMU_PQB_LOG2SZ_LEN, k - 1);
    riscv_iommu_write_reg64(r_iommu, RISCV_IOMMU_REG_PQB, reg64);

    /* pqt = 0, pqcsr.pqen = 1, poll pqcsr.pqon until it reads 1 */
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_PQT, 0);

    reg = riscv_iommu_read_reg32(r_iommu, RISCV_IOMMU_REG_PQCSR);
    reg |= RISCV_IOMMU_PQCSR_PQEN;
    riscv_iommu_write_reg32(r_iommu, RISCV_IOMMU_REG_PQCSR, reg);

    qtest_wait_for_queue_active(r_iommu, RISCV_IOMMU_REG_PQCSR);
}

static void register_riscv_iommu_test(void)
{
    qos_add_test("pci_config", "riscv-iommu-pci", test_pci_config, NULL);
    qos_add_test("reg_reset", "riscv-iommu-pci", test_reg_reset, NULL);
    qos_add_test("iommu_init_queues", "riscv-iommu-pci",
                 test_iommu_init_queues, NULL);
}

libqos_init(register_riscv_iommu_test);
