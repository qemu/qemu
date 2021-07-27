/*
 * QTest testcase for NVMe
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "libqos/libqtest.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"
#include "include/block/nvme.h"

typedef struct QNvme QNvme;

struct QNvme {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *nvme_get_driver(void *obj, const char *interface)
{
    QNvme *nvme = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &nvme->dev;
    }

    fprintf(stderr, "%s not present in nvme\n", interface);
    g_assert_not_reached();
}

static void *nvme_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QNvme *nvme = g_new0(QNvme, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&nvme->dev, bus, addr);
    nvme->obj.get_driver = nvme_get_driver;

    return &nvme->obj;
}

/* This used to cause a NULL pointer dereference.  */
static void nvmetest_oob_cmb_test(void *obj, void *data, QGuestAllocator *alloc)
{
    const int cmb_bar_size = 2 * MiB;
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar bar;

    qpci_device_enable(pdev);
    bar = qpci_iomap(pdev, 2, NULL);

    qpci_io_writel(pdev, bar, 0, 0xccbbaa99);
    g_assert_cmpint(qpci_io_readb(pdev, bar, 0), ==, 0x99);
    g_assert_cmpint(qpci_io_readw(pdev, bar, 0), ==, 0xaa99);

    /* Test partially out-of-bounds accesses.  */
    qpci_io_writel(pdev, bar, cmb_bar_size - 1, 0x44332211);
    g_assert_cmpint(qpci_io_readb(pdev, bar, cmb_bar_size - 1), ==, 0x11);
    g_assert_cmpint(qpci_io_readw(pdev, bar, cmb_bar_size - 1), !=, 0x2211);
    g_assert_cmpint(qpci_io_readl(pdev, bar, cmb_bar_size - 1), !=, 0x44332211);
}

static void nvmetest_reg_read_test(void *obj, void *data, QGuestAllocator *alloc)
{
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar bar;
    uint32_t cap_lo, cap_hi;
    uint64_t cap;

    qpci_device_enable(pdev);
    bar = qpci_iomap(pdev, 0, NULL);

    cap_lo = qpci_io_readl(pdev, bar, 0x0);
    g_assert_cmpint(NVME_CAP_MQES(cap_lo), ==, 0x7ff);

    cap_hi = qpci_io_readl(pdev, bar, 0x4);
    g_assert_cmpint(NVME_CAP_MPSMAX((uint64_t)cap_hi << 32), ==, 0x4);

    cap = qpci_io_readq(pdev, bar, 0x0);
    g_assert_cmpint(NVME_CAP_MQES(cap), ==, 0x7ff);
    g_assert_cmpint(NVME_CAP_MPSMAX(cap), ==, 0x4);

    qpci_iounmap(pdev, bar);
}

static void nvmetest_pmr_reg_test(void *obj, void *data, QGuestAllocator *alloc)
{
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar pmr_bar, nvme_bar;
    uint32_t pmrcap, pmrsts;

    qpci_device_enable(pdev);
    pmr_bar = qpci_iomap(pdev, 4, NULL);

    /* Without Enabling PMRCTL check bar enablemet */
    qpci_io_writel(pdev, pmr_bar, 0, 0xccbbaa99);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), !=, 0x99);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), !=, 0xaa99);

    /* Map NVMe Bar Register to Enable the Mem Region */
    nvme_bar = qpci_iomap(pdev, 0, NULL);

    pmrcap = qpci_io_readl(pdev, nvme_bar, 0xe00);
    g_assert_cmpint(NVME_PMRCAP_RDS(pmrcap), ==, 0x1);
    g_assert_cmpint(NVME_PMRCAP_WDS(pmrcap), ==, 0x1);
    g_assert_cmpint(NVME_PMRCAP_BIR(pmrcap), ==, 0x4);
    g_assert_cmpint(NVME_PMRCAP_PMRWBM(pmrcap), ==, 0x2);
    g_assert_cmpint(NVME_PMRCAP_CMSS(pmrcap), ==, 0x1);

    /* Enable PMRCTRL */
    qpci_io_writel(pdev, nvme_bar, 0xe04, 0x1);

    qpci_io_writel(pdev, pmr_bar, 0, 0x44332211);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), ==, 0x11);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), ==, 0x2211);
    g_assert_cmpint(qpci_io_readl(pdev, pmr_bar, 0), ==, 0x44332211);

    pmrsts = qpci_io_readl(pdev, nvme_bar, 0xe08);
    g_assert_cmpint(NVME_PMRSTS_NRDY(pmrsts), ==, 0x0);

    /* Disable PMRCTRL */
    qpci_io_writel(pdev, nvme_bar, 0xe04, 0x0);

    qpci_io_writel(pdev, pmr_bar, 0, 0x88776655);
    g_assert_cmpint(qpci_io_readb(pdev, pmr_bar, 0), !=, 0x55);
    g_assert_cmpint(qpci_io_readw(pdev, pmr_bar, 0), !=, 0x6655);
    g_assert_cmpint(qpci_io_readl(pdev, pmr_bar, 0), !=, 0x88776655);

    pmrsts = qpci_io_readl(pdev, nvme_bar, 0xe08);
    g_assert_cmpint(NVME_PMRSTS_NRDY(pmrsts), ==, 0x1);

    qpci_iounmap(pdev, nvme_bar);
    qpci_iounmap(pdev, pmr_bar);
}

static void nvme_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,drive=drv0,serial=foo",
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw "
                           "-object memory-backend-ram,id=pmr0,"
                           "share=on,size=8",
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("nvme", nvme_create);
    qos_node_consumes("nvme", "pci-bus", &opts);
    qos_node_produces("nvme", "pci-device");

    qos_add_test("oob-cmb-access", "nvme", nvmetest_oob_cmb_test, &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "cmb_size_mb=2"
    });

    qos_add_test("pmr-test-access", "nvme", nvmetest_pmr_reg_test,
                 &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "pmrdev=pmr0"
    });

    qos_add_test("reg-read", "nvme", nvmetest_reg_read_test, NULL);
}

libqos_init(nvme_register_nodes);
