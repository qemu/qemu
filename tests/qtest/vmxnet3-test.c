/*
 * QTest testcase for vmxnet3 NIC
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

typedef struct QVmxnet3 QVmxnet3;

struct QVmxnet3 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *vmxnet3_get_driver(void *obj, const char *interface)
{
    QVmxnet3 *vmxnet3 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &vmxnet3->dev;
    }

    fprintf(stderr, "%s not present in vmxnet3\n", interface);
    g_assert_not_reached();
}

static void *vmxnet3_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QVmxnet3 *vmxnet3 = g_new0(QVmxnet3, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&vmxnet3->dev, bus, addr);
    vmxnet3->obj.get_driver = vmxnet3_get_driver;

    return &vmxnet3->obj;
}

static void vmxnet3_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("vmxnet3", vmxnet3_create);
    qos_node_consumes("vmxnet3", "pci-bus", &opts);
    qos_node_produces("vmxnet3", "pci-device");
}

libqos_init(vmxnet3_register_nodes);
