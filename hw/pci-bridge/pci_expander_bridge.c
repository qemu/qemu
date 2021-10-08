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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_bridge.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/numa.h"
#include "hw/boards.h"
#include "qom/object.h"

#define TYPE_PXB_BUS "pxb-bus"
typedef struct PXBBus PXBBus;
DECLARE_INSTANCE_CHECKER(PXBBus, PXB_BUS,
                         TYPE_PXB_BUS)

#define TYPE_PXB_PCIE_BUS "pxb-pcie-bus"
DECLARE_INSTANCE_CHECKER(PXBBus, PXB_PCIE_BUS,
                         TYPE_PXB_PCIE_BUS)

struct PXBBus {
    /*< private >*/
    PCIBus parent_obj;
    /*< public >*/

    char bus_path[8];
};

#define TYPE_PXB_DEVICE "pxb"
typedef struct PXBDev PXBDev;
DECLARE_INSTANCE_CHECKER(PXBDev, PXB_DEV,
                         TYPE_PXB_DEVICE)

#define TYPE_PXB_PCIE_DEVICE "pxb-pcie"
DECLARE_INSTANCE_CHECKER(PXBDev, PXB_PCIE_DEV,
                         TYPE_PXB_PCIE_DEVICE)

struct PXBDev {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    uint8_t bus_nr;
    uint16_t numa_node;
    bool bypass_iommu;
};

static PXBDev *convert_to_pxb(PCIDevice *dev)
{
    return pci_bus_is_express(pci_get_bus(dev))
        ? PXB_PCIE_DEV(dev) : PXB_DEV(dev);
}

static GList *pxb_dev_list;

#define TYPE_PXB_HOST "pxb-host"

static int pxb_bus_num(PCIBus *bus)
{
    PXBDev *pxb = convert_to_pxb(bus->parent_dev);

    return pxb->bus_nr;
}

static uint16_t pxb_bus_numa_node(PCIBus *bus)
{
    PXBDev *pxb = convert_to_pxb(bus->parent_dev);

    return pxb->numa_node;
}

static void pxb_bus_class_init(ObjectClass *class, void *data)
{
    PCIBusClass *pbc = PCI_BUS_CLASS(class);

    pbc->bus_num = pxb_bus_num;
    pbc->numa_node = pxb_bus_numa_node;
}

static const TypeInfo pxb_bus_info = {
    .name          = TYPE_PXB_BUS,
    .parent        = TYPE_PCI_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static const TypeInfo pxb_pcie_bus_info = {
    .name          = TYPE_PXB_PCIE_BUS,
    .parent        = TYPE_PCIE_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static const char *pxb_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    PXBBus *bus = pci_bus_is_express(rootbus) ?
                  PXB_PCIE_BUS(rootbus) : PXB_BUS(rootbus);

    snprintf(bus->bus_path, 8, "0000:%02x", pxb_bus_num(rootbus));
    return bus->bus_path;
}

static char *pxb_host_ofw_unit_address(const SysBusDevice *dev)
{
    const PCIHostState *pxb_host;
    const PCIBus *pxb_bus;
    const PXBDev *pxb_dev;
    int position;
    const DeviceState *pxb_dev_base;
    const PCIHostState *main_host;
    const SysBusDevice *main_host_sbd;

    pxb_host = PCI_HOST_BRIDGE(dev);
    pxb_bus = pxb_host->bus;
    pxb_dev = convert_to_pxb(pxb_bus->parent_dev);
    position = g_list_index(pxb_dev_list, pxb_dev);
    assert(position >= 0);

    pxb_dev_base = DEVICE(pxb_dev);
    main_host = PCI_HOST_BRIDGE(pxb_dev_base->parent_bus->parent);
    main_host_sbd = SYS_BUS_DEVICE(main_host);

    if (main_host_sbd->num_mmio > 0) {
        return g_strdup_printf(TARGET_FMT_plx ",%x",
                               main_host_sbd->mmio[0].addr, position + 1);
    }
    if (main_host_sbd->num_pio > 0) {
        return g_strdup_printf("i%04x,%x",
                               main_host_sbd->pio[0], position + 1);
    }
    return NULL;
}

static void pxb_host_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    dc->fw_name = "pci";
    /* Reason: Internal part of the pxb/pxb-pcie device, not usable by itself */
    dc->user_creatable = false;
    sbc->explicit_ofw_unit_address = pxb_host_ofw_unit_address;
    hc->root_bus_path = pxb_host_root_bus_path;
}

static const TypeInfo pxb_host_info = {
    .name          = TYPE_PXB_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .class_init    = pxb_host_class_init,
};

/*
 * Registers the PXB bus as a child of pci host root bus.
 */
static void pxb_register_bus(PCIDevice *dev, PCIBus *pxb_bus, Error **errp)
{
    PCIBus *bus = pci_get_bus(dev);
    int pxb_bus_num = pci_bus_num(pxb_bus);

    if (bus->parent_dev) {
        error_setg(errp, "PXB devices can be attached only to root bus");
        return;
    }

    QLIST_FOREACH(bus, &bus->child, sibling) {
        if (pci_bus_num(bus) == pxb_bus_num) {
            error_setg(errp, "Bus %d is already in use", pxb_bus_num);
            return;
        }
    }
    QLIST_INSERT_HEAD(&pci_get_bus(dev)->child, pxb_bus, sibling);
}

static int pxb_map_irq_fn(PCIDevice *pci_dev, int pin)
{
    PCIDevice *pxb = pci_get_bus(pci_dev)->parent_dev;

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

static gint pxb_compare(gconstpointer a, gconstpointer b)
{
    const PXBDev *pxb_a = a, *pxb_b = b;

    return pxb_a->bus_nr < pxb_b->bus_nr ? -1 :
           pxb_a->bus_nr > pxb_b->bus_nr ?  1 :
           0;
}

static void pxb_dev_realize_common(PCIDevice *dev, bool pcie, Error **errp)
{
    PXBDev *pxb = convert_to_pxb(dev);
    DeviceState *ds, *bds = NULL;
    PCIBus *bus;
    const char *dev_name = NULL;
    Error *local_err = NULL;
    MachineState *ms = MACHINE(qdev_get_machine());

    if (ms->numa_state == NULL) {
        error_setg(errp, "NUMA is not supported by this machine-type");
        return;
    }

    if (pxb->numa_node != NUMA_NODE_UNASSIGNED &&
        pxb->numa_node >= ms->numa_state->num_nodes) {
        error_setg(errp, "Illegal numa node %d", pxb->numa_node);
        return;
    }

    if (dev->qdev.id && *dev->qdev.id) {
        dev_name = dev->qdev.id;
    }

    ds = qdev_new(TYPE_PXB_HOST);
    if (pcie) {
        bus = pci_root_bus_new(ds, dev_name, NULL, NULL, 0, TYPE_PXB_PCIE_BUS);
    } else {
        bus = pci_root_bus_new(ds, "pxb-internal", NULL, NULL, 0, TYPE_PXB_BUS);
        bds = qdev_new("pci-bridge");
        bds->id = g_strdup(dev_name);
        qdev_prop_set_uint8(bds, PCI_BRIDGE_DEV_PROP_CHASSIS_NR, pxb->bus_nr);
        qdev_prop_set_bit(bds, PCI_BRIDGE_DEV_PROP_SHPC, false);
    }

    bus->parent_dev = dev;
    bus->address_space_mem = pci_get_bus(dev)->address_space_mem;
    bus->address_space_io = pci_get_bus(dev)->address_space_io;
    bus->map_irq = pxb_map_irq_fn;

    PCI_HOST_BRIDGE(ds)->bus = bus;
    PCI_HOST_BRIDGE(ds)->bypass_iommu = pxb->bypass_iommu;

    pxb_register_bus(dev, bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto err_register_bus;
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(ds), &error_fatal);
    if (bds) {
        qdev_realize_and_unref(bds, &bus->qbus, &error_fatal);
    }

    pci_word_test_and_set_mask(dev->config + PCI_STATUS,
                               PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_HOST);

    pxb_dev_list = g_list_insert_sorted(pxb_dev_list, pxb, pxb_compare);
    return;

err_register_bus:
    object_unref(OBJECT(bds));
    object_unparent(OBJECT(bus));
    object_unref(OBJECT(ds));
}

static void pxb_dev_realize(PCIDevice *dev, Error **errp)
{
    if (pci_bus_is_express(pci_get_bus(dev))) {
        error_setg(errp, "pxb devices cannot reside on a PCIe bus");
        return;
    }

    pxb_dev_realize_common(dev, false, errp);
}

static void pxb_dev_exitfn(PCIDevice *pci_dev)
{
    PXBDev *pxb = convert_to_pxb(pci_dev);

    pxb_dev_list = g_list_remove(pxb_dev_list, pxb);
}

static Property pxb_dev_properties[] = {
    /* Note: 0 is not a legal PXB bus number. */
    DEFINE_PROP_UINT8("bus_nr", PXBDev, bus_nr, 0),
    DEFINE_PROP_UINT16("numa_node", PXBDev, numa_node, NUMA_NODE_UNASSIGNED),
    DEFINE_PROP_BOOL("bypass_iommu", PXBDev, bypass_iommu, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxb_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pxb_dev_realize;
    k->exit = pxb_dev_exitfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PXB;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "PCI Expander Bridge";
    device_class_set_props(dc, pxb_dev_properties);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pxb_dev_info = {
    .name          = TYPE_PXB_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PXBDev),
    .class_init    = pxb_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pxb_pcie_dev_realize(PCIDevice *dev, Error **errp)
{
    if (!pci_bus_is_express(pci_get_bus(dev))) {
        error_setg(errp, "pxb-pcie devices cannot reside on a PCI bus");
        return;
    }

    pxb_dev_realize_common(dev, true, errp);
}

static void pxb_pcie_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pxb_pcie_dev_realize;
    k->exit = pxb_dev_exitfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PXB_PCIE;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    dc->desc = "PCI Express Expander Bridge";
    device_class_set_props(dc, pxb_dev_properties);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pxb_pcie_dev_info = {
    .name          = TYPE_PXB_PCIE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PXBDev),
    .class_init    = pxb_pcie_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pxb_register_types(void)
{
    type_register_static(&pxb_bus_info);
    type_register_static(&pxb_pcie_bus_info);
    type_register_static(&pxb_host_info);
    type_register_static(&pxb_dev_info);
    type_register_static(&pxb_pcie_dev_info);
}

type_init(pxb_register_types)
