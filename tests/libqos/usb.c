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
#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"
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

void usb_test_hotplug(const char *hcd_id, const int port,
                      void (*port_check)(void))
{
    QDict *response;
    char  *cmd;

    cmd = g_strdup_printf("{'execute': 'device_add',"
                          " 'arguments': {"
                          "   'driver': 'usb-tablet',"
                          "   'port': '%d',"
                          "   'bus': '%s.0',"
                          "   'id': 'usbdev%d'"
                          "}}", port, hcd_id, port);
    response = qmp(cmd);
    g_free(cmd);
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    if (port_check) {
        port_check();
    }

    cmd = g_strdup_printf("{'execute': 'device_del',"
                           " 'arguments': {"
                           "   'id': 'usbdev%d'"
                           "}}", port);
    response = qmp(cmd);
    g_free(cmd);
    g_assert(response);
    g_assert(qdict_haskey(response, "event"));
    g_assert(!strcmp(qdict_get_str(response, "event"), "DEVICE_DELETED"));
}
