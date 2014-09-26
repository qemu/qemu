/*
 * QTest testcase for USB xHCI controller
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
#include "libqos/usb.h"


static void test_xhci_init(void)
{
}

static void test_xhci_hotplug(void)
{
    usb_test_hotplug("xhci", 1, NULL);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/xhci/pci/init", test_xhci_init);
    qtest_add_func("/xhci/pci/hotplug", test_xhci_hotplug);

    qtest_start("-device nec-usb-xhci,id=xhci");
    ret = g_test_run();
    qtest_end();

    return ret;
}
