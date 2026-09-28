/*
 * CXL PCI Expander Bridge Device Emulation
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci-bridge/pci_expander_bridge_cxl.h"
#include "qom/object.h"
#include "pci_expander_bridge_common.h"

DECLARE_INSTANCE_CHECKER(PXBBus, PXB_CXL_BUS,
                         TYPE_PXB_CXL_BUS)

CXLComponentState *cxl_get_hb_cstate(PCIHostState *hb)
{
    CXLHost *host = PXB_CXL_HOST(hb);

    return &host->cxl_cstate;
}

bool cxl_get_hb_passthrough(PCIHostState *hb)
{
    CXLHost *host = PXB_CXL_HOST(hb);

    return host->passthrough;
}

static const char *pxb_cxl_host_root_bus_path(PCIHostState *host_bridge,
                                              PCIBus *rootbus)
{
    PXBBus *bus = PXB_CXL_BUS(rootbus);
    snprintf(bus->bus_path, 8, "0000:%02x", pxb_bus_num(rootbus));
    return bus->bus_path;
}


static const TypeInfo pxb_cxl_bus_info = {
    .name          = TYPE_PXB_CXL_BUS,
    .parent        = TYPE_CXL_BUS,
    .instance_size = sizeof(PXBBus),
    .class_init    = pxb_bus_class_init,
};

static void pxb_cxl_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    CXLHost *cxl = PXB_CXL_HOST(dev);
    CXLComponentState *cxl_cstate = &cxl->cxl_cstate;
    struct MemoryRegion *mr = &cxl_cstate->crb.component_registers;

    cxl_component_register_block_init(OBJECT(dev), cxl_cstate,
                                      TYPE_PXB_CXL_HOST);
    sysbus_init_mmio(sbd, mr);
}

/*
 * Host bridge realization has no means of knowning state associated
 * with a particular machine. As such, it is nececssary to delay
 * final setup of the host bridge register space until later in the
 * machine bring up.
 */
void pxb_cxl_hook_up_registers(CXLState *cxl_state, PCIBus *bus, Error **errp)
{
    PXBCXLDev *pxb =  PXB_CXL_DEV(pci_bridge_get_device(bus));
    CXLHost *cxl = pxb->cxl_host_bridge;
    CXLComponentState *cxl_cstate = &cxl->cxl_cstate;
    struct MemoryRegion *mr = &cxl_cstate->crb.component_registers;
    hwaddr offset;

    offset = memory_region_size(mr) * cxl_state->next_mr_idx;
    if (offset > memory_region_size(&cxl_state->host_mr)) {
        error_setg(errp, "Insufficient space for pxb cxl host register space");
        return;
    }

    memory_region_add_subregion(&cxl_state->host_mr, offset, mr);
    cxl_state->next_mr_idx++;
}

static void pxb_cxl_host_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(class);

    hc->root_bus_path = pxb_cxl_host_root_bus_path;
    dc->fw_name = "cxl";
    dc->realize = pxb_cxl_realize;
    /* Reason: Internal part of the pxb/pxb-pcie device, not usable by itself */
    dc->user_creatable = false;
}

/*
 * This is a device to handle the MMIO for a CXL host bridge. It does nothing
 * else.
 */
static const TypeInfo cxl_host_info = {
    .name          = TYPE_PXB_CXL_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(CXLHost),
    .class_init    = pxb_cxl_host_class_init,
};

static void pxb_cxl_dev_reset(DeviceState *dev)
{
    CXLHost *cxl = PXB_CXL_DEV(dev)->cxl_host_bridge;
    CXLComponentState *cxl_cstate = &cxl->cxl_cstate;
    PCIHostState *hb = PCI_HOST_BRIDGE(cxl);
    uint32_t *reg_state = cxl_cstate->crb.cache_mem_registers;
    uint32_t *write_msk = cxl_cstate->crb.cache_mem_regs_write_mask;
    int dsp_count = 0;

    cxl_component_register_init_common(reg_state, write_msk, CXL2_RC, false);
    /*
     * The CXL specification allows for host bridges with no HDM decoders
     * if they only have a single root port.
     */
    if (!PXB_CXL_DEV(dev)->hdm_for_passthrough) {
        dsp_count = pcie_count_ds_ports(hb->bus);
    }
    /* Initial reset will have 0 dsp so wait until > 0 */
    if (dsp_count == 1) {
        cxl->passthrough = true;
        /* Set Capability ID in header to NONE */
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_CAPABILITY_HEADER, ID, 0);
    } else {
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, TARGET_COUNT,
                         8);
    }
}

static void pxb_cxl_dev_realize(PCIDevice *dev, Error **errp)
{
    /* A CXL PXB's parent bus is still PCIe */
    if (!pci_bus_is_express(pci_get_bus(dev))) {
        error_setg(errp, "pxb-cxl devices cannot reside on a PCI bus");
        return;
    }

    if (!pxb_dev_realize_common(dev, CXL, errp)) {
        return;
    }
    pxb_cxl_dev_reset(DEVICE(dev));
}

static const Property pxb_cxl_dev_properties[] = {
    DEFINE_PROP_BOOL("hdm_for_passthrough", PXBCXLDev,
                     hdm_for_passthrough, false),
};

static void pxb_cxl_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc   = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize             = pxb_cxl_dev_realize;
    k->exit                = pxb_dev_exitfn;
    /*
     * XXX: These types of bridges don't actually show up in the hierarchy so
     * vendor, device, class, etc. ids are intentionally left out.
     */

    dc->desc = "CXL Host Bridge";
    device_class_set_props(dc, pxb_cxl_dev_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);

    /* Host bridges aren't hotpluggable. FIXME: spec reference */
    dc->hotpluggable = false;
    device_class_set_legacy_reset(dc, pxb_cxl_dev_reset);
}

static const TypeInfo pxb_cxl_dev_info = {
    .name          = TYPE_PXB_CXL_DEV,
    .parent        = TYPE_PXB_PCIE_DEV,
    .instance_size = sizeof(PXBCXLDev),
    .class_init    = pxb_cxl_dev_class_init,
    .interfaces =
        (const InterfaceInfo[]){
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            {},
        },
};

static void pxb_cxl_register_types(void)
{
    type_register_static(&pxb_cxl_bus_info);
    type_register_static(&cxl_host_info);
    type_register_static(&pxb_cxl_dev_info);
}

type_init(pxb_cxl_register_types)
