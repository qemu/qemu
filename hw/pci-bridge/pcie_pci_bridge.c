/*
 * QEMU Generic PCIE-PCI Bridge
 *
 * Copyright (c) 2017 Aleksandr Bezzubikov
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/msi.h"
#include "hw/pci/shpc.h"
#include "hw/pci/slotid_cap.h"
#include "hw/qdev-properties.h"

typedef struct PCIEPCIBridge {
    /*< private >*/
    PCIBridge parent_obj;

    OnOffAuto msi;
    MemoryRegion shpc_bar;
    /*< public >*/
} PCIEPCIBridge;

#define TYPE_PCIE_PCI_BRIDGE_DEV "pcie-pci-bridge"
#define PCIE_PCI_BRIDGE_DEV(obj) \
        OBJECT_CHECK(PCIEPCIBridge, (obj), TYPE_PCIE_PCI_BRIDGE_DEV)

static void pcie_pci_bridge_realize(PCIDevice *d, Error **errp)
{
    PCIBridge *br = PCI_BRIDGE(d);
    PCIEPCIBridge *pcie_br = PCIE_PCI_BRIDGE_DEV(d);
    int rc, pos;

    pci_bridge_initfn(d, TYPE_PCI_BUS);

    d->config[PCI_INTERRUPT_PIN] = 0x1;
    memory_region_init(&pcie_br->shpc_bar, OBJECT(d), "shpc-bar",
                       shpc_bar_size(d));
    rc = shpc_init(d, &br->sec_bus, &pcie_br->shpc_bar, 0, errp);
    if (rc) {
        goto error;
    }

    rc = pcie_cap_init(d, 0, PCI_EXP_TYPE_PCI_BRIDGE, 0, errp);
    if (rc < 0) {
        goto cap_error;
    }

    pos = pci_add_capability(d, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pos < 0) {
        goto pm_error;
    }
    d->exp.pm_cap = pos;
    pci_set_word(d->config + pos + PCI_PM_PMC, 0x3);

    pcie_cap_arifwd_init(d);
    pcie_cap_deverr_init(d);

    rc = pcie_aer_init(d, PCI_ERR_VER, 0x100, PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto aer_error;
    }

    Error *local_err = NULL;
    if (pcie_br->msi != ON_OFF_AUTO_OFF) {
        rc = msi_init(d, 0, 1, true, true, &local_err);
        if (rc < 0) {
            assert(rc == -ENOTSUP);
            if (pcie_br->msi != ON_OFF_AUTO_ON) {
                error_free(local_err);
            } else {
                /* failed to satisfy user's explicit request for MSI */
                error_propagate(errp, local_err);
                goto msi_error;
            }
        }
    }
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &pcie_br->shpc_bar);
    return;

msi_error:
    pcie_aer_exit(d);
aer_error:
pm_error:
    pcie_cap_exit(d);
cap_error:
    shpc_cleanup(d, &pcie_br->shpc_bar);
error:
    pci_bridge_exitfn(d);
}

static void pcie_pci_bridge_exit(PCIDevice *d)
{
    PCIEPCIBridge *bridge_dev = PCIE_PCI_BRIDGE_DEV(d);
    pcie_cap_exit(d);
    shpc_cleanup(d, &bridge_dev->shpc_bar);
    pci_bridge_exitfn(d);
}

static void pcie_pci_bridge_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    pci_bridge_reset(qdev);
    if (msi_present(d)) {
        msi_reset(d);
    }
    shpc_reset(d);
}

static void pcie_pci_bridge_write_config(PCIDevice *d,
        uint32_t address, uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    if (msi_present(d)) {
        msi_write_config(d, address, val, len);
    }
    shpc_cap_write_config(d, address, val, len);
}

static Property pcie_pci_bridge_dev_properties[] = {
        DEFINE_PROP_ON_OFF_AUTO("msi", PCIEPCIBridge, msi, ON_OFF_AUTO_AUTO),
        DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription pcie_pci_bridge_dev_vmstate = {
        .name = TYPE_PCIE_PCI_BRIDGE_DEV,
        .priority = MIG_PRI_PCI_BUS,
        .fields = (VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
            SHPC_VMSTATE(shpc, PCIDevice, NULL),
            VMSTATE_END_OF_LIST()
        }
};

static void pcie_pci_bridge_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->is_bridge = true;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_BRIDGE;
    k->realize = pcie_pci_bridge_realize;
    k->exit = pcie_pci_bridge_exit;
    k->config_write = pcie_pci_bridge_write_config;
    dc->vmsd = &pcie_pci_bridge_dev_vmstate;
    dc->props = pcie_pci_bridge_dev_properties;
    dc->reset = &pcie_pci_bridge_reset;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    hc->plug = pci_bridge_dev_plug_cb;
    hc->unplug = pci_bridge_dev_unplug_cb;
    hc->unplug_request = pci_bridge_dev_unplug_request_cb;
}

static const TypeInfo pcie_pci_bridge_info = {
        .name = TYPE_PCIE_PCI_BRIDGE_DEV,
        .parent = TYPE_PCI_BRIDGE,
        .instance_size = sizeof(PCIEPCIBridge),
        .class_init = pcie_pci_bridge_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { INTERFACE_PCIE_DEVICE },
            { },
        }
};

static void pciepci_register(void)
{
    type_register_static(&pcie_pci_bridge_info);
}

type_init(pciepci_register);
