/*
 * QTest testcase for USB OHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/usb.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QOHCI_PCI QOHCI_PCI;

struct QOHCI_PCI {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void test_ohci_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    usb_test_hotplug(global_qtest, "ohci", "1", NULL);
}

static void *ohci_pci_get_driver(void *obj, const char *interface)
{
    QOHCI_PCI *ohci_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ohci_pci->dev;
    }

    fprintf(stderr, "%s not present in pci-ohci\n", interface);
    g_assert_not_reached();
}

static void *ohci_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QOHCI_PCI *ohci_pci = g_new0(QOHCI_PCI, 1);
    ohci_pci->obj.get_driver = ohci_pci_get_driver;

    return &ohci_pci->obj;
}

static void ohci_pci_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=ohci",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("pci-ohci", ohci_pci_create);
    qos_node_consumes("pci-ohci", "pci-bus", &opts);
    qos_node_produces("pci-ohci", "pci-device");
}

libqos_init(ohci_pci_register_nodes);

static void register_ohci_pci_test(void)
{
    qos_add_test("ohci_pci-test-hotplug", "pci-ohci", test_ohci_hotplug, NULL);
}

libqos_init(register_ohci_pci_test);
