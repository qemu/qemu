/*
 * QTest testcase for USB EHCI
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ehci/pci/nop", pci_nop);

    qtest_start("-machine q35 -device ich9-usb-ehci1,bus=pcie.0,addr=1d.7,"
                "multifunction=on,id=ich9-ehci-1 "
                "-device ich9-usb-uhci1,bus=pcie.0,addr=1d.0,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=0 "
                "-device ich9-usb-uhci2,bus=pcie.0,addr=1d.1,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=2 "
                "-device ich9-usb-uhci3,bus=pcie.0,addr=1d.2,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=4");
    ret = g_test_run();

    qtest_end();

    return ret;
}
