/*
 * QTest testcase for LSI MegaRAID
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QMegasas QMegasas;

struct QMegasas {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *megasas_get_driver(void *obj, const char *interface)
{
    QMegasas *megasas = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &megasas->dev;
    }

    fprintf(stderr, "%s not present in megasas\n", interface);
    g_assert_not_reached();
}

static void *megasas_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QMegasas *megasas = g_new0(QMegasas, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&megasas->dev, bus, addr);
    megasas->obj.get_driver = megasas_get_driver;

    return &megasas->obj;
}

/* This used to cause a NULL pointer dereference.  */
static void megasas_pd_get_info_fuzz(void *obj, void *data, QGuestAllocator *alloc)
{
    QMegasas *megasas = obj;
    QPCIDevice *dev = &megasas->dev;
    QPCIBar bar;
    uint32_t context[256];
    uint64_t context_pa;
    int i;

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);

    memset(context, 0, sizeof(context));
    context[0] = cpu_to_le32(0x05050505);
    context[1] = cpu_to_le32(0x01010101);
    for (i = 2; i < ARRAY_SIZE(context); i++) {
        context[i] = cpu_to_le32(0x41414141);
    }
    context[6] = cpu_to_le32(0x02020000);
    context[7] = cpu_to_le32(0);

    context_pa = guest_alloc(alloc, sizeof(context));
    qtest_memwrite(dev->bus->qts, context_pa, context, sizeof(context));
    qpci_io_writel(dev, bar, 0x40, context_pa);
}

static void megasas_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=scsi0",
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw",
        .after_cmd_line = "-device scsi-hd,bus=scsi0.0,drive=drv0",
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("megasas", megasas_create);
    qos_node_consumes("megasas", "pci-bus", &opts);
    qos_node_produces("megasas", "pci-device");

    qos_add_test("dcmd/pd-get-info/fuzz", "megasas", megasas_pd_get_info_fuzz, NULL);
}
libqos_init(megasas_register_nodes);
