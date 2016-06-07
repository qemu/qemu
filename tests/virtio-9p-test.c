/*
 * QTest testcase for VirtIO 9P
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

static char test_share[] = "/tmp/qtest.XXXXXX";

int main(int argc, char **argv)
{
    char *args;
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/9p/pci/nop", pci_nop);

    g_assert(mkdtemp(test_share));

    args = g_strdup_printf("-fsdev local,id=fsdev0,security_model=none,path=%s "
                           "-device virtio-9p-pci,fsdev=fsdev0,mount_tag=qtest",
                           test_share);
    qtest_start(args);
    g_free(args);

    ret = g_test_run();

    qtest_end();
    rmdir(test_share);

    return ret;
}
