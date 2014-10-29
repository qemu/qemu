/*
 * QTest testcase for USB OHCI controller
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


static void test_ohci_init(void)
{

}

static void test_ohci_hotplug(void)
{
    usb_test_hotplug("ohci", 1, NULL);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ohci/pci/init", test_ohci_init);
    qtest_add_func("/ohci/pci/hotplug", test_ohci_hotplug);

    qtest_start("-device pci-ohci,id=ohci");
    ret = g_test_run();
    qtest_end();

    return ret;
}
