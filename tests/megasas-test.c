/*
 * QTest testcase for LSI MegaRAID
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"

static QOSState *qmegasas_start(const char *extra_opts)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-drive id=hd0,if=none,file=null-co://,format=raw "
                      "-device megasas,id=scsi0,addr=04.0 "
                      "-device scsi-hd,bus=scsi0.0,drive=hd0 %s";

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        return qtest_pc_boot(cmd, extra_opts ? : "");
    }

    g_printerr("virtio-scsi tests are only available on x86 or ppc64\n");
    exit(EXIT_FAILURE);
}

static void qmegasas_stop(QOSState *qs)
{
    qtest_shutdown(qs);
}

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
    QOSState *qs;

    qs = qmegasas_start(NULL);
    qmegasas_stop(qs);
}

/* This used to cause a NULL pointer dereference.  */
static void megasas_pd_get_info_fuzz(void)
{
    QPCIDevice *dev;
    QOSState *qs;
    QPCIBar bar;
    uint32_t context[256];
    uint64_t context_pa;
    int i;

    qs = qmegasas_start(NULL);
    dev = qpci_device_find(qs->pcibus, QPCI_DEVFN(4,0));
    g_assert(dev != NULL);

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    memset(context, 0, sizeof(context));
    context[0] = cpu_to_le32(0x05050505);
    context[1] = cpu_to_le32(0x01010101);
    for (i = 2; i < ARRAY_SIZE(context); i++) {
        context[i] = cpu_to_le32(0x41414141);
    }
    context[6] = cpu_to_le32(0x02020000);
    context[7] = cpu_to_le32(0);

    context_pa = qmalloc(qs, sizeof(context));
    memwrite(context_pa, context, sizeof(context));
    qpci_io_writel(dev, bar, 0x40, context_pa);

    g_free(dev);
    qmegasas_stop(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/megasas/pci/nop", pci_nop);
    qtest_add_func("/megasas/dcmd/pd-get-info/fuzz", megasas_pd_get_info_fuzz);

    return g_test_run();
}
