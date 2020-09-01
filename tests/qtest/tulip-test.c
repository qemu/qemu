/*
 * QTest testcase for DEC/Intel Tulip 21143
 *
 * Copyright (c) 2020 Li Qiang <liq3ea@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"
#include "qemu/bitops.h"
#include "hw/net/tulip.h"

typedef struct QTulip_pci QTulip_pci;

struct QTulip_pci {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *tulip_pci_get_driver(void *obj, const char *interface)
{
    QTulip_pci *tulip_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &tulip_pci->dev;
    }

    fprintf(stderr, "%s not present in tulip_pci\n", interface);
    g_assert_not_reached();
}

static void *tulip_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QTulip_pci *tulip_pci = g_new0(QTulip_pci, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&tulip_pci->dev, bus, addr);
    tulip_pci->obj.get_driver = tulip_pci_get_driver;

    return &tulip_pci->obj;
}

static void tulip_large_tx(void *obj, void *data, QGuestAllocator *alloc)
{
    QTulip_pci *tulip_pci = obj;
    QPCIDevice *dev = &tulip_pci->dev;
    QPCIBar bar;
    struct tulip_descriptor context;
    char guest_data[4096];
    uint64_t context_pa;
    uint64_t guest_pa;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    context_pa = guest_alloc(alloc, sizeof(context));
    guest_pa = guest_alloc(alloc, 4096);
    memset(guest_data, 'A', sizeof(guest_data));
    context.status = TDES0_OWN;
    context.control = TDES1_BUF2_SIZE_MASK << TDES1_BUF2_SIZE_SHIFT |
                      TDES1_BUF1_SIZE_MASK << TDES1_BUF1_SIZE_SHIFT;
    context.buf_addr2 = guest_pa;
    context.buf_addr1 = guest_pa;

    qtest_memwrite(dev->bus->qts, context_pa, &context, sizeof(context));
    qtest_memwrite(dev->bus->qts, guest_pa, guest_data, sizeof(guest_data));
    qpci_io_writel(dev, bar, 0x20, context_pa);
    qpci_io_writel(dev, bar, 0x30, CSR6_ST);
    guest_free(alloc, context_pa);
    guest_free(alloc, guest_pa);
}

static void tulip_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("tulip", tulip_pci_create);
    qos_node_consumes("tulip", "pci-bus", &opts);
    qos_node_produces("tulip", "pci-device");

    qos_add_test("tulip_large_tx", "tulip", tulip_large_tx, NULL);
}

libqos_init(tulip_register_nodes);
