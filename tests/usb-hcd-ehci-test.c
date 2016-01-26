/*
 * QTest testcase for USB EHCI
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "hw/usb/uhci-regs.h"
#include "hw/usb/ehci-regs.h"
#include "libqos/usb.h"

static QPCIBus *pcibus;
static struct qhc uhci1;
static struct qhc uhci2;
static struct qhc uhci3;
static struct qhc ehci1;

/* helpers */

#if 0
static void uhci_port_update(struct qhc *hc, int port,
                             uint16_t set, uint16_t clear)
{
    void *addr = hc->base + 0x10 + 2 * port;
    uint16_t value;

    value = qpci_io_readw(hc->dev, addr);
    value |= set;
    value &= ~clear;
    qpci_io_writew(hc->dev, addr, value);
}
#endif

static void ehci_port_test(struct qhc *hc, int port, uint32_t expect)
{
    void *addr = hc->base + 0x64 + 4 * port;
    uint32_t value = qpci_io_readl(hc->dev, addr);
    uint16_t mask = ~(PORTSC_CSC | PORTSC_PEDC | PORTSC_OCC);

#if 0
    fprintf(stderr, "%s: %d, have 0x%08x, want 0x%08x\n",
            __func__, port, value & mask, expect & mask);
#endif
    g_assert((value & mask) == (expect & mask));
}

/* tests */

static void pci_init(void)
{
    if (pcibus) {
        return;
    }
    pcibus = qpci_init_pc();
    g_assert(pcibus != NULL);

    qusb_pci_init_one(pcibus, &uhci1, QPCI_DEVFN(0x1d, 0), 4);
    qusb_pci_init_one(pcibus, &uhci2, QPCI_DEVFN(0x1d, 1), 4);
    qusb_pci_init_one(pcibus, &uhci3, QPCI_DEVFN(0x1d, 2), 4);
    qusb_pci_init_one(pcibus, &ehci1, QPCI_DEVFN(0x1d, 7), 0);
}

static void pci_uhci_port_1(void)
{
    g_assert(pcibus != NULL);

    uhci_port_test(&uhci1, 0, UHCI_PORT_CCS); /* usb-tablet  */
    uhci_port_test(&uhci1, 1, UHCI_PORT_CCS); /* usb-storage */
    uhci_port_test(&uhci2, 0, 0);
    uhci_port_test(&uhci2, 1, 0);
    uhci_port_test(&uhci3, 0, 0);
    uhci_port_test(&uhci3, 1, 0);
}

static void pci_ehci_port_1(void)
{
    int i;

    g_assert(pcibus != NULL);

    for (i = 0; i < 6; i++) {
        ehci_port_test(&ehci1, i, PORTSC_POWNER | PORTSC_PPOWER);
    }
}

static void pci_ehci_config(void)
{
    /* hands over all ports from companion uhci to ehci */
    qpci_io_writew(ehci1.dev, ehci1.base + 0x60, 1);
}

static void pci_uhci_port_2(void)
{
    g_assert(pcibus != NULL);

    uhci_port_test(&uhci1, 0, 0); /* usb-tablet,  @ehci */
    uhci_port_test(&uhci1, 1, 0); /* usb-storage, @ehci */
    uhci_port_test(&uhci2, 0, 0);
    uhci_port_test(&uhci2, 1, 0);
    uhci_port_test(&uhci3, 0, 0);
    uhci_port_test(&uhci3, 1, 0);
}

static void pci_ehci_port_2(void)
{
    static uint32_t expect[] = {
        PORTSC_PPOWER | PORTSC_CONNECT, /* usb-tablet  */
        PORTSC_PPOWER | PORTSC_CONNECT, /* usb-storage */
        PORTSC_PPOWER,
        PORTSC_PPOWER,
        PORTSC_PPOWER,
        PORTSC_PPOWER,
    };
    int i;

    g_assert(pcibus != NULL);

    for (i = 0; i < 6; i++) {
        ehci_port_test(&ehci1, i, expect[i]);
    }
}

static void pci_ehci_port_3_hotplug(void)
{
    /* check for presence of hotplugged usb-tablet */
    g_assert(pcibus != NULL);
    ehci_port_test(&ehci1, 2, PORTSC_PPOWER | PORTSC_CONNECT);
}

static void pci_ehci_port_hotplug(void)
{
    usb_test_hotplug("ich9-ehci-1", 3, pci_ehci_port_3_hotplug);
}


int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ehci/pci/init", pci_init);
    qtest_add_func("/ehci/pci/uhci-port-1", pci_uhci_port_1);
    qtest_add_func("/ehci/pci/ehci-port-1", pci_ehci_port_1);
    qtest_add_func("/ehci/pci/ehci-config", pci_ehci_config);
    qtest_add_func("/ehci/pci/uhci-port-2", pci_uhci_port_2);
    qtest_add_func("/ehci/pci/ehci-port-2", pci_ehci_port_2);
    qtest_add_func("/ehci/pci/ehci-port-3-hotplug", pci_ehci_port_hotplug);

    qtest_start("-machine q35 -device ich9-usb-ehci1,bus=pcie.0,addr=1d.7,"
                "multifunction=on,id=ich9-ehci-1 "
                "-device ich9-usb-uhci1,bus=pcie.0,addr=1d.0,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=0 "
                "-device ich9-usb-uhci2,bus=pcie.0,addr=1d.1,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=2 "
                "-device ich9-usb-uhci3,bus=pcie.0,addr=1d.2,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=4 "
                "-drive if=none,id=usbcdrom,media=cdrom "
                "-device usb-tablet,bus=ich9-ehci-1.0,port=1,usb_version=1 "
                "-device usb-storage,bus=ich9-ehci-1.0,port=2,drive=usbcdrom ");
    ret = g_test_run();

    qtest_end();

    return ret;
}
