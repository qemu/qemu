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
#include "libqos/usb.h"
#include "hw/usb/uhci-regs.h"


static void test_uhci_init(void)
{
}

static void test_port_1(void)
{
    QPCIBus *pcibus;
    struct qhc uhci;

    pcibus = qpci_init_pc();
    g_assert(pcibus != NULL);
    qusb_pci_init_one(pcibus, &uhci, QPCI_DEVFN(0x1d, 0), 4);
    uhci_port_test(&uhci, 0, UHCI_PORT_CCS);
}


int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/uhci/pci/init", test_uhci_init);
    qtest_add_func("/uhci/pci/port1", test_port_1);

    qtest_start("-device piix3-usb-uhci,id=uhci,addr=1d.0"
                " -device usb-tablet,bus=uhci.0,port=1");
    ret = g_test_run();
    qtest_end();

    return ret;
}
