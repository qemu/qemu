/*
 * QTest testcase for NVMe
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "libqtest.h"
#include "libqos/libqos-pc.h"

static QOSState *qnvme_start(const char *extra_opts)
{
    QOSState *qs;
    const char *arch = qtest_get_arch();
    const char *cmd = "-drive id=drv0,if=none,file=null-co://,format=raw "
                      "-device nvme,addr=0x4.0,serial=foo,drive=drv0 %s";

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qs = qtest_pc_boot(cmd, extra_opts ? : "");
        global_qtest = qs->qts;
        return qs;
    }

    g_printerr("nvme tests are only available on x86\n");
    exit(EXIT_FAILURE);
}

static void qnvme_stop(QOSState *qs)
{
    qtest_shutdown(qs);
}

static void nop(void)
{
    QOSState *qs;

    qs = qnvme_start(NULL);
    qnvme_stop(qs);
}

static void nvmetest_cmb_test(void)
{
    const int cmb_bar_size = 2 * MiB;
    QOSState *qs;
    QPCIDevice *pdev;
    QPCIBar bar;

    qs = qnvme_start("-global nvme.cmb_size_mb=2");
    pdev = qpci_device_find(qs->pcibus, QPCI_DEVFN(4,0));
    g_assert(pdev != NULL);

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
    g_free(pdev);

    qnvme_stop(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nvme/nop", nop);
    qtest_add_func("/nvme/cmb_test", nvmetest_cmb_test);

    return g_test_run();
}
