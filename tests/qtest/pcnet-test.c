/*
 * QTest testcase for PC-Net NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QPCNet QPCNet;

struct QPCNet {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *pcnet_get_driver(void *obj, const char *interface)
{
    QPCNet *pcnet = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &pcnet->dev;
    }

    fprintf(stderr, "%s not present in pcnet\n", interface);
    g_assert_not_reached();
}

static void *pcnet_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QPCNet *pcnet = g_new0(QPCNet, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&pcnet->dev, bus, addr);
    pcnet->obj.get_driver = pcnet_get_driver;

    return &pcnet->obj;
}

static void pcnet_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("pcnet", pcnet_create);
    qos_node_consumes("pcnet", "pci-bus", &opts);
    qos_node_produces("pcnet", "pci-device");
}

libqos_init(pcnet_register_nodes);
