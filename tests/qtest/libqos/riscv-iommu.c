/*
 * libqos driver riscv-iommu-pci framework
 *
 * Copyright (c) 2024 Ventana Micro Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "pci.h"
#include "riscv-iommu.h"

static void *riscv_iommu_pci_get_driver(void *obj, const char *interface)
{
    QRISCVIOMMU *r_iommu_pci = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &r_iommu_pci->dev;
    }

    fprintf(stderr, "%s not present in riscv_iommu_pci\n", interface);
    g_assert_not_reached();
}

static void riscv_iommu_pci_start_hw(QOSGraphObject *obj)
{
    QRISCVIOMMU *pci = (QRISCVIOMMU *)obj;
    qpci_device_enable(&pci->dev);
}

static void riscv_iommu_pci_destructor(QOSGraphObject *obj)
{
    QRISCVIOMMU *pci = (QRISCVIOMMU *)obj;
    qpci_iounmap(&pci->dev, pci->reg_bar);
}

static void *riscv_iommu_pci_create(void *pci_bus, QGuestAllocator *alloc,
                                    void *addr)
{
    QRISCVIOMMU *r_iommu_pci = g_new0(QRISCVIOMMU, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&r_iommu_pci->dev, bus, addr);
    r_iommu_pci->reg_bar = qpci_iomap(&r_iommu_pci->dev, 0, NULL);

    r_iommu_pci->obj.get_driver = riscv_iommu_pci_get_driver;
    r_iommu_pci->obj.start_hw = riscv_iommu_pci_start_hw;
    r_iommu_pci->obj.destructor = riscv_iommu_pci_destructor;
    return &r_iommu_pci->obj;
}

static void riscv_iommu_pci_register_nodes(void)
{
    QPCIAddress addr = {
        .vendor_id = RISCV_IOMMU_PCI_VENDOR_ID,
        .device_id = RISCV_IOMMU_PCI_DEVICE_ID,
        .devfn = QPCI_DEVFN(1, 0),
    };

    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=01.0",
    };

    add_qpci_address(&opts, &addr);

    qos_node_create_driver("riscv-iommu-pci", riscv_iommu_pci_create);
    qos_node_produces("riscv-iommu-pci", "pci-device");
    qos_node_consumes("riscv-iommu-pci", "pci-bus", &opts);
}

libqos_init(riscv_iommu_pci_register_nodes);
