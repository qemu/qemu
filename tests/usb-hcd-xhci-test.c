/*
 * QTest testcase for USB xHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"
#include "libqos/usb.h"


static void test_xhci_init(void)
{
}

static void test_xhci_hotplug(void)
{
    usb_test_hotplug("xhci", 1, NULL);
}

static void test_usb_uas_hotplug(void)
{
    QDict *response;

    response = qmp("{'execute': 'device_add',"
                   " 'arguments': {"
                   "   'driver': 'usb-uas',"
                   "   'id': 'uas'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'device_add',"
                   " 'arguments': {"
                   "   'driver': 'scsi-hd',"
                   "   'drive': 'drive0',"
                   "   'id': 'scsi-hd'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    /* TODO:
        UAS HBA driver in libqos, to check that
        added disk is visible after BUS rescan
    */

    response = qmp("{'execute': 'device_del',"
                           " 'arguments': {"
                           "   'id': 'scsi-hd'"
                           "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("");
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
    QDECREF(response);


    response = qmp("{'execute': 'device_del',"
                           " 'arguments': {"
                           "   'id': 'uas'"
                           "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("");
    g_assert(response);
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
    QDECREF(response);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/xhci/pci/init", test_xhci_init);
    qtest_add_func("/xhci/pci/hotplug", test_xhci_hotplug);
    qtest_add_func("/xhci/pci/hotplug/usb-uas", test_usb_uas_hotplug);

    qtest_start("-device nec-usb-xhci,id=xhci"
                " -drive id=drive0,if=none,file=/dev/null,format=raw");
    ret = g_test_run();
    qtest_end();

    return ret;
}
