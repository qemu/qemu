/*
 * QTest testcase for USB xHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/usb.h"


static void test_xhci_init(void)
{
}

static void test_xhci_hotplug(void)
{
    usb_test_hotplug(global_qtest, "xhci", "1", NULL);
}

static void test_usb_uas_hotplug(void)
{
    QTestState *qts = global_qtest;

    qtest_qmp_device_add(qts, "usb-uas", "uas", "{}");
    qtest_qmp_device_add(qts, "scsi-hd", "scsihd", "{'drive': 'drive0'}");

    /* TODO:
        UAS HBA driver in libqos, to check that
        added disk is visible after BUS rescan
    */

    qtest_qmp_device_del(qts, "scsihd");
    qtest_qmp_device_del(qts, "uas");
}

static void test_usb_ccid_hotplug(void)
{
    QTestState *qts = global_qtest;

    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
    /* check the device can be added again */
    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/xhci/pci/init", test_xhci_init);
    qtest_add_func("/xhci/pci/hotplug", test_xhci_hotplug);
    qtest_add_func("/xhci/pci/hotplug/usb-uas", test_usb_uas_hotplug);
    qtest_add_func("/xhci/pci/hotplug/usb-ccid", test_usb_ccid_hotplug);

    qtest_start("-device nec-usb-xhci,id=xhci"
                " -drive id=drive0,if=none,file=null-co://,"
                "file.read-zeroes=on,format=raw");
    ret = g_test_run();
    qtest_end();

    return ret;
}
