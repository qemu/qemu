/*
 * QTest testcase for tpci200 PCI-IndustryPack bridge
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QTpci200 QTpci200;
typedef struct QIpack QIpack;

struct QIpack {

};
struct QTpci200 {
    QOSGraphObject obj;
    QPCIDevice dev;
    QIpack ipack;
};

/* tpci200 */
static void *tpci200_get_driver(void *obj, const char *interface)
{
    QTpci200 *tpci200 = obj;
    if (!g_strcmp0(interface, "ipack")) {
        return &tpci200->ipack;
    }
    if (!g_strcmp0(interface, "pci-device")) {
        return &tpci200->dev;
    }

    fprintf(stderr, "%s not present in tpci200\n", interface);
    g_assert_not_reached();
}

static void *tpci200_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QTpci200 *tpci200 = g_new0(QTpci200, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&tpci200->dev, bus, addr);
    tpci200->obj.get_driver = tpci200_get_driver;
    return &tpci200->obj;
}

static void tpci200_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=ipack0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("tpci200", tpci200_create);
    qos_node_consumes("tpci200", "pci-bus", &opts);
    qos_node_produces("tpci200", "ipack");
    qos_node_produces("tpci200", "pci-device");
}

libqos_init(tpci200_register_nodes);
