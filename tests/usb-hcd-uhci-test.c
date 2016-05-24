/*
 * QTest testcase for USB UHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/usb.h"
#include "hw/usb/uhci-regs.h"


static void test_uhci_init(void)
{
}

static void test_port(int port)
{
    QPCIBus *pcibus;
    struct qhc uhci;

    g_assert(port > 0);
    pcibus = qpci_init_pc();
    g_assert(pcibus != NULL);
    qusb_pci_init_one(pcibus, &uhci, QPCI_DEVFN(0x1d, 0), 4);
    uhci_port_test(&uhci, port - 1, UHCI_PORT_CCS);
}

static void test_port_1(void)
{
    test_port(1);
}

static void test_port_2(void)
{
    test_port(2);
}

static void test_uhci_hotplug(void)
{
    usb_test_hotplug("uhci", 2, test_port_2);
}

static void test_usb_storage_hotplug(void)
{
    QDict *response;

    response = qmp("{'execute': 'device_add',"
                   " 'arguments': {"
                   "   'driver': 'usb-storage',"
                   "   'drive': 'drive0',"
                   "   'id': 'usbdev0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'device_del',"
                           " 'arguments': {"
                           "   'id': 'usbdev0'"
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

    qtest_add_func("/uhci/pci/init", test_uhci_init);
    qtest_add_func("/uhci/pci/port1", test_port_1);
    qtest_add_func("/uhci/pci/hotplug", test_uhci_hotplug);
    qtest_add_func("/uhci/pci/hotplug/usb-storage", test_usb_storage_hotplug);

    qtest_start("-device piix3-usb-uhci,id=uhci,addr=1d.0"
                " -drive id=drive0,if=none,file=/dev/null,format=raw"
                " -device usb-tablet,bus=uhci.0,port=1");
    ret = g_test_run();
    qtest_end();

    return ret;
}
