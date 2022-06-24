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
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB3_ROOT_PORT);
        break;
    case 4:
        phb_typename = g_strdup(TYPE_PNV_PHB4);
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB4_ROOT_PORT);
        break;
    case 5:
        phb_typename = g_strdup(TYPE_PNV_PHB5);
        phb_rootport_typename = g_strdup(TYPE_PNV_PHB5_ROOT_PORT);
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

static void pnv_phb_register_type(void)
{
    static const TypeInfo pnv_phb_type_info = {
        .name          = TYPE_PNV_PHB,
        .parent        = TYPE_PCIE_HOST_BRIDGE,
        .instance_size = sizeof(PnvPHB),
        .class_init    = pnv_phb_class_init,
    };

    type_register_static(&pnv_phb_type_info);
}
type_init(pnv_phb_register_type)
