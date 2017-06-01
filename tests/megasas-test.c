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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/megasas/pci/nop", pci_nop);

    return g_test_run();
}
