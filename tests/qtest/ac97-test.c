/*
 * QTest testcase for AC97
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

typedef struct QAC97 QAC97;

struct QAC97 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *ac97_get_driver(void *obj, const char *interface)
{
    QAC97 *ac97 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ac97->dev;
    }

    fprintf(stderr, "%s not present in ac97\n", interface);
    g_assert_not_reached();
}

static void *ac97_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QAC97 *ac97 = g_new0(QAC97, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&ac97->dev, bus, addr);
    ac97->obj.get_driver = ac97_get_driver;
    return &ac97->obj;
}

static void ac97_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("AC97", ac97_create);
    qos_node_produces("AC97", "pci-device");
    qos_node_consumes("AC97", "pci-bus", &opts);
}

libqos_init(ac97_register_nodes);
