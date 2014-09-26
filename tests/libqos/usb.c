/*
 * common code shared by usb tests
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * Authors:
 *     Gerd Hoffmann <kraxel@redhat.com>
 *     John Snow <jsnow@redhat.com>
 *     Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"
#include "hw/usb/uhci-regs.h"
#include "libqos/usb.h"

void qusb_pci_init_one(QPCIBus *pcibus, struct qhc *hc, uint32_t devfn, int bar)
{
    hc->dev = qpci_device_find(pcibus, devfn);
    g_assert(hc->dev != NULL);
    qpci_device_enable(hc->dev);
    hc->base = qpci_iomap(hc->dev, bar, NULL);
    g_assert(hc->base != NULL);
}

void uhci_port_test(struct qhc *hc, int port, uint16_t expect)
{
    void *addr = hc->base + 0x10 + 2 * port;
    uint16_t value = qpci_io_readw(hc->dev, addr);
    uint16_t mask = ~(UHCI_PORT_WRITE_CLEAR | UHCI_PORT_RSVD1);

    g_assert((value & mask) == (expect & mask));
}
