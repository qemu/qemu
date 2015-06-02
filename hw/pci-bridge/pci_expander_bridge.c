/*
 * PCI Expander Bridge Device Emulation
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pci_bus.h"
#include "hw/i386/pc.h"
#include "qemu/range.h"
#include "qemu/error-report.h"

#define TYPE_PXB_BUS "pxb-bus"
#define PXB_BUS(obj) OBJECT_CHECK(PXBBus, (obj), TYPE_PXB_BUS)

typedef struct PXBBus {
    /*< private >*/
    PCIBus parent_obj;
    /*< public >*/

    char bus_path[8];
} PXBBus;

#define TYPE_PXB_DEVICE "pxb"
#define PXB_DEV(obj) OBJECT_CHECK(PXBDev, (obj), TYPE_PXB_DEVICE)

typedef struct PXBDev {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    uint8_t bus_nr;
} PXBDev;

#define TYPE_PXB_HOST "pxb-host"

static int pxb_bus_num(PCIBus *bus)
{
    PXBDev *pxb = PXB_DEV(bus->parent_dev);

    return pxb->bus_nr;
}

static bool pxb_is_root(PCIBus *bus)
{
    return true; /* by definition */
}

static void pxb_bus_class_init(ObjectClass *class, void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(class);

    pbc->bus_num = pxb_bus_num;
    pbc->is_root = pxb_is_root;
}

static const TypeInfo pxb_bus_info = {
    .name          = TYPE_PXB_BUS,
    .parent        = TYPE_PCI_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static const char *pxb_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    PXBBus *bus = PXB_BUS(rootbus);

    snprintf(bus->bus_path, 8, "0000:%02x", pxb_bus_num(rootbus));
    return bus->bus_path;
}

static void pxb_host_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    dc->fw_name = "pci";
    hc->root_bus_path = pxb_host_root_bus_path;
}

static const TypeInfo pxb_host_info = {
    .name          = TYPE_PXB_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .class_init    = pxb_host_class_init,
};

/*
 * Registers the PXB bus as a child of the i440fx root bus.
 *
 * Returns 0 on successs, -1 if i440fx host was not
 * found or the bus number is already in use.
 */
static int pxb_register_bus(PCIDevice *dev, PCIBus *pxb_bus)
{
    PCIBus *bus = dev->bus;
    int pxb_bus_num = pci_bus_num(pxb_bus);

    if (bus->parent_dev) {
        error_report("PXB devices can be attached only to root bus.");
        return -1;
    }

    QLIST_FOREACH(bus, &bus->child, sibling) {
        if (pci_bus_num(bus) == pxb_bus_num) {
            error_report("Bus %d is already in use.", pxb_bus_num);
            return -1;
        }
    }
    QLIST_INSERT_HEAD(&dev->bus->child, pxb_bus, sibling);

    return 0;
}

static int pxb_map_irq_fn(PCIDevice *pci_dev, int pin)
{
    PCIDevice *pxb = pci_dev->bus->parent_dev;

    /*
     * The bios does not index the pxb slot number when
     * it computes the IRQ because it resides on bus 0
     * and not on the current bus.
     * However QEMU routes the irq through bus 0 and adds
     * the pxb slot to the IRQ computation of the PXB
     * device.
     *
     * Synchronize between bios and QEMU by canceling
     * pxb's effect.
     */
    return pin - PCI_SLOT(pxb->devfn);
}

static int pxb_dev_initfn(PCIDevice *dev)
{
    PXBDev *pxb = PXB_DEV(dev);
    DeviceState *ds, *bds;
    PCIBus *bus;
    const char *dev_name = NULL;

    if (dev->qdev.id && *dev->qdev.id) {
        dev_name = dev->qdev.id;
    }

    ds = qdev_create(NULL, TYPE_PXB_HOST);
    bus = pci_bus_new(ds, "pxb-internal", NULL, NULL, 0, TYPE_PXB_BUS);

    bus->parent_dev = dev;
    bus->address_space_mem = dev->bus->address_space_mem;
    bus->address_space_io = dev->bus->address_space_io;
    bus->map_irq = pxb_map_irq_fn;

    bds = qdev_create(BUS(bus), "pci-bridge");
    bds->id = dev_name;
    qdev_prop_set_uint8(bds, "chassis_nr", pxb->bus_nr);

    PCI_HOST_BRIDGE(ds)->bus = bus;

    if (pxb_register_bus(dev, bus)) {
        return -EINVAL;
    }

    qdev_init_nofail(ds);
    qdev_init_nofail(bds);

    pci_word_test_and_set_mask(dev->config + PCI_STATUS,
                               PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_HOST);

    return 0;
}

static Property pxb_dev_properties[] = {
    /* Note: 0 is not a legal a PXB bus number. */
    DEFINE_PROP_UINT8("bus_nr", PXBDev, bus_nr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxb_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pxb_dev_initfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PXB;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "PCI Expander Bridge";
    dc->props = pxb_dev_properties;
}

static const TypeInfo pxb_dev_info = {
    .name          = TYPE_PXB_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PXBDev),
    .class_init    = pxb_dev_class_init,
};

static void pxb_register_types(void)
{
    type_register_static(&pxb_bus_info);
    type_register_static(&pxb_host_info);
    type_register_static(&pxb_dev_info);
}

type_init(pxb_register_types)
