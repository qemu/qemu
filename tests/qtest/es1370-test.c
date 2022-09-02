/*
 * QTest testcase for ES1370
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

typedef struct QES1370 QES1370;

struct QES1370 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *es1370_get_driver(void *obj, const char *interface)
{
    QES1370 *es1370 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &es1370->dev;
    }

    fprintf(stderr, "%s not present in es1370\n", interface);
    g_assert_not_reached();
}

static void *es1370_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QES1370 *es1370 = g_new0(QES1370, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&es1370->dev, bus, addr);
    es1370->obj.get_driver = es1370_get_driver;

    return &es1370->obj;
}

static void es1370_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("ES1370", es1370_create);
    qos_node_consumes("ES1370", "pci-bus", &opts);
    qos_node_produces("ES1370", "pci-device");
}

libqos_init(es1370_register_nodes);
