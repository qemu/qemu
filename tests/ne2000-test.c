/*
 * QTest testcase for ne2000 NIC
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

typedef struct QNe2k_pci QNe2k_pci;

struct QNe2k_pci {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *ne2k_pci_get_driver(void *obj, const char *interface)
{
    QNe2k_pci *ne2k_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ne2k_pci->dev;
    }

    fprintf(stderr, "%s not present in ne2k_pci\n", interface);
    g_assert_not_reached();
}

static void *ne2k_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QNe2k_pci *ne2k_pci = g_new0(QNe2k_pci, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&ne2k_pci->dev, bus, addr);
    ne2k_pci->obj.get_driver = ne2k_pci_get_driver;

    return &ne2k_pci->obj;
}

static void ne2000_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("ne2k_pci", ne2k_pci_create);
    qos_node_consumes("ne2k_pci", "pci-bus", &opts);
    qos_node_produces("ne2k_pci", "pci-device");
}

libqos_init(ne2000_register_nodes);
