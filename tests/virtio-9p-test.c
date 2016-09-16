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

static const char mount_tag[] = "qtest";
static char *test_share;

static void qvirtio_9p_start(void)
{
    char *args;

    test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(test_share));

    args = g_strdup_printf("-fsdev local,id=fsdev0,security_model=none,path=%s "
                           "-device virtio-9p-pci,fsdev=fsdev0,mount_tag=%s",
                           test_share, mount_tag);

    qtest_start(args);
    g_free(args);
}

static void qvirtio_9p_stop(void)
{
    qtest_end();
    rmdir(test_share);
    g_free(test_share);
}

static void pci_nop(void)
{
    qvirtio_9p_start();
    qvirtio_9p_stop();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/9p/pci/nop", pci_nop);

    return g_test_run();
}
