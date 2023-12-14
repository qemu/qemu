/*
 * QTest testcase for e1000 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QE1000 QE1000;

struct QE1000 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static const char *models[] = {
    "e1000",
    "e1000-82540em",
    "e1000-82544gc",
    "e1000-82545em",
};

static void *e1000_get_driver(void *obj, const char *interface)
{
    QE1000 *e1000 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &e1000->dev;
    }

    fprintf(stderr, "%s not present in e1000\n", interface);
    g_assert_not_reached();
}

static void *e1000_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QE1000 *e1000 = g_new0(QE1000, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&e1000->dev, bus, addr);
    e1000->obj.get_driver = e1000_get_driver;

    return &e1000->obj;
}

static void e1000_register_nodes(void)
{
    int i;
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    for (i = 0; i < ARRAY_SIZE(models); i++) {
        qos_node_create_driver(models[i], e1000_create);
        qos_node_consumes(models[i], "pci-bus", &opts);
        qos_node_produces(models[i], "pci-device");
    }
}

libqos_init(e1000_register_nodes);
