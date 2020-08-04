/*
 * QTest testcase for eepro100 NIC
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

typedef struct QEEPRO100 QEEPRO100;

struct QEEPRO100 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static const char *models[] = {
    "i82550",
    "i82551",
    "i82557a",
    "i82557b",
    "i82557c",
    "i82558a",
    "i82558b",
    "i82559a",
    "i82559b",
    "i82559c",
    "i82559er",
    "i82562",
    "i82801",
};

static void *eepro100_get_driver(void *obj, const char *interface)
{
    QEEPRO100 *eepro100 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &eepro100->dev;
    }

    fprintf(stderr, "%s not present in eepro100\n", interface);
    g_assert_not_reached();
}

static void *eepro100_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QEEPRO100 *eepro100 = g_new0(QEEPRO100, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&eepro100->dev, bus, addr);
    eepro100->obj.get_driver = eepro100_get_driver;

    return &eepro100->obj;
}

static void eepro100_register_nodes(void)
{
    int i;
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });
    for (i = 0; i < ARRAY_SIZE(models); i++) {
        qos_node_create_driver(models[i], eepro100_create);
        qos_node_consumes(models[i], "pci-bus", &opts);
        qos_node_produces(models[i], "pci-device");
    }
}

libqos_init(eepro100_register_nodes);
