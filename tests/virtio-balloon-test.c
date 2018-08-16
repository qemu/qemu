/*
 * QTest testcase for VirtIO Balloon
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/virtio.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void balloon_nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/balloon/nop", balloon_nop);

    global_qtest = qtest_initf("-device virtio-balloon-%s",
                               qvirtio_get_dev_type());
    ret = g_test_run();

    qtest_end();

    return ret;
}
