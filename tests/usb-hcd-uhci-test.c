/*
 * QTest testcase for USB UHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"


static void test_uhci_init(void)
{
    qtest_start("-device piix3-usb-uhci,id=uhci");

    qtest_end();
}


int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/uhci/pci/init", test_uhci_init);

    ret = g_test_run();

    return ret;
}
