/*
 * QEMU PowerPC PowerNV Proxy PHB model
 *
 * Copyright (c) 2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/pci-host/pnv_phb3.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/ppc/pnv.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"


static void pnv_phb_realize(DeviceState *dev, Error **errp)
{
    PnvPHB *phb = PNV_PHB(dev);
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    g_autofree char *phb_typename = NULL;
    g_autofree char *phb_rootport_typename = NULL;

    if (!phb->version) {
        error_setg(errp, "version not specified");
        return;
    }

    switch (phb->version) {
    case 3:
        phb_typename = g_strdup(TYPE_PNV_PHB3);
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB_ROOT_PORT);
        break;
    case 4:
        phb_typename = g_strdup(TYPE_PNV_PHB4);
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB_ROOT_PORT);
        break;
    case 5:
        phb_typename = g_strdup(TYPE_PNV_PHB5);
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB_ROOT_PORT);
        break;
    default:
        g_assert_not_reached();
    }

    phb->backend = object_new(phb_typename);
    object_property_add_child(OBJECT(dev), "phb-backend", phb->backend);

    /* Passthrough child device properties to the proxy device */
    object_property_set_uint(phb->backend, "index", phb->phb_id, errp);
    object_property_set_uint(phb->backend, "chip-id", phb->chip_id, errp);
    object_property_set_link(phb->backend, "phb-base", OBJECT(phb), errp);

    if (phb->version == 3) {
        object_property_set_link(phb->backend, "chip",
                                 OBJECT(phb->chip), errp);
    } else {
        object_property_set_link(phb->backend, "pec", OBJECT(phb->pec), errp);
    }

    if (!qdev_realize(DEVICE(phb->backend), NULL, errp)) {
        return;
    }

    if (phb->version == 3) {
        pnv_phb3_bus_init(dev, PNV_PHB3(phb->backend));
    } else {
        pnv_phb4_bus_init(dev, PNV_PHB4(phb->backend));
    }

    pnv_phb_attach_root_port(pci, phb_rootport_typename,
                             phb->phb_id, phb->chip_id);
}

static const char *pnv_phb_root_bus_path(PCIHostState *host_bridge,
                                         PCIBus *rootbus)
{
    PnvPHB *phb = PNV_PHB(host_bridge);

    snprintf(phb->bus_path, sizeof(phb->bus_path), "00%02x:%02x",
             phb->chip_id, phb->phb_id);
    return phb->bus_path;
}

static Property pnv_phb_properties[] = {
    DEFINE_PROP_UINT32("index", PnvPHB, phb_id, 0),
    DEFINE_PROP_UINT32("chip-id", PnvPHB, chip_id, 0),
    DEFINE_PROP_UINT32("version", PnvPHB, version, 0),

    DEFINE_PROP_LINK("chip", PnvPHB, chip, TYPE_PNV_CHIP, PnvChip *),

    DEFINE_PROP_LINK("pec", PnvPHB, pec, TYPE_PNV_PHB4_PEC,
                     PnvPhb4PecState *),

    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb_class_init(ObjectClass *klass, void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    hc->root_bus_path = pnv_phb_root_bus_path;
    dc->realize = pnv_phb_realize;
    device_class_set_props(dc, pnv_phb_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->user_creatable = false;
}

static void pnv_phb_root_port_reset(DeviceState *dev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PnvPHBRootPort *phb_rp = PNV_PHB_ROOT_PORT(dev);
    PCIDevice *d = PCI_DEVICE(dev);
    uint8_t *conf = d->config;

    rpc->parent_reset(dev);

    if (phb_rp->version == 3) {
        return;
    }

    /* PHB4 and later requires these extra reset steps */
    pci_byte_test_and_set_mask(conf + PCI_IO_BASE,
                               PCI_IO_RANGE_MASK & 0xff);
    pci_byte_test_and_clear_mask(conf + PCI_IO_LIMIT,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_set_word(conf + PCI_MEMORY_BASE, 0);
    pci_set_word(conf + PCI_MEMORY_LIMIT, 0xfff0);
    pci_set_word(conf + PCI_PREF_MEMORY_BASE, 0x1);
    pci_set_word(conf + PCI_PREF_MEMORY_LIMIT, 0xfff1);
    pci_set_long(conf + PCI_PREF_BASE_UPPER32, 0x1); /* Hack */
    pci_set_long(conf + PCI_PREF_LIMIT_UPPER32, 0xffffffff);
    pci_config_set_interrupt_pin(conf, 0);
}

static void pnv_phb_root_port_realize(DeviceState *dev, Error **errp)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PnvPHBRootPort *phb_rp = PNV_PHB_ROOT_PORT(dev);
    PCIDevice *pci = PCI_DEVICE(dev);
    uint16_t device_id = 0;
    Error *local_err = NULL;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (phb_rp->version) {
    case 3:
        device_id = PNV_PHB3_DEVICE_ID;
        break;
    case 4:
        device_id = PNV_PHB4_DEVICE_ID;
        break;
    case 5:
        device_id = PNV_PHB5_DEVICE_ID;
        break;
    default:
        g_assert_not_reached();
    }

    pci_config_set_device_id(pci->config, device_id);
    pci_config_set_interrupt_pin(pci->config, 0);
}

static Property pnv_phb_root_port_properties[] = {
    DEFINE_PROP_UINT32("version", PnvPHBRootPort, version, 0),

    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb_root_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    dc->desc     = "IBM PHB PCIE Root Port";

    device_class_set_props(dc, pnv_phb_root_port_properties);
    device_class_set_parent_realize(dc, pnv_phb_root_port_realize,
                                    &rpc->parent_realize);
    device_class_set_parent_reset(dc, pnv_phb_root_port_reset,
                                  &rpc->parent_reset);
    dc->reset = &pnv_phb_root_port_reset;
    dc->user_creatable = false;

    k->vendor_id = PCI_VENDOR_ID_IBM;
    /* device_id will be written during realize() */
    k->device_id = 0;
    k->revision  = 0;

    rpc->exp_offset = 0x48;
    rpc->aer_offset = 0x100;
}

static const TypeInfo pnv_phb_type_info = {
    .name          = TYPE_PNV_PHB,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(PnvPHB),
    .class_init    = pnv_phb_class_init,
};

static const TypeInfo pnv_phb_root_port_info = {
    .name          = TYPE_PNV_PHB_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(PnvPHBRootPort),
    .class_init    = pnv_phb_root_port_class_init,
};

static void pnv_phb_register_types(void)
{
    type_register_static(&pnv_phb_type_info);
    type_register_static(&pnv_phb_root_port_info);
}

type_init(pnv_phb_register_types)
