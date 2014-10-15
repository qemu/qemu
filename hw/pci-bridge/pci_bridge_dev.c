/*
 * Standard PCI Bridge Device
 *
 * Copyright (c) 2011 Red Hat Inc. Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * http://www.pcisig.com/specifications/conventional/pci_to_pci_bridge_architecture/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/shpc.h"
#include "hw/pci/slotid_cap.h"
#include "exec/memory.h"
#include "hw/pci/pci_bus.h"
#include "hw/hotplug.h"

#define TYPE_PCI_BRIDGE_DEV "pci-bridge"
#define PCI_BRIDGE_DEV(obj) \
    OBJECT_CHECK(PCIBridgeDev, (obj), TYPE_PCI_BRIDGE_DEV)

struct PCIBridgeDev {
    /*< private >*/
    PCIBridge parent_obj;
    /*< public >*/

    MemoryRegion bar;
    uint8_t chassis_nr;
#define PCI_BRIDGE_DEV_F_MSI_REQ 0
    uint32_t flags;
};
typedef struct PCIBridgeDev PCIBridgeDev;

static int pci_bridge_dev_initfn(PCIDevice *dev)
{
    PCIBridge *br = PCI_BRIDGE(dev);
    PCIBridgeDev *bridge_dev = PCI_BRIDGE_DEV(dev);
    int err;

    err = pci_bridge_initfn(dev, TYPE_PCI_BUS);
    if (err) {
        goto bridge_error;
    }
    dev->config[PCI_INTERRUPT_PIN] = 0x1;
    memory_region_init(&bridge_dev->bar, OBJECT(dev), "shpc-bar", shpc_bar_size(dev));
    err = shpc_init(dev, &br->sec_bus, &bridge_dev->bar, 0);
    if (err) {
        goto shpc_error;
    }
    err = slotid_cap_init(dev, 0, bridge_dev->chassis_nr, 0);
    if (err) {
        goto slotid_error;
    }
    if ((bridge_dev->flags & (1 << PCI_BRIDGE_DEV_F_MSI_REQ)) &&
        msi_supported) {
        err = msi_init(dev, 0, 1, true, true);
        if (err < 0) {
            goto msi_error;
        }
    }
    /* TODO: spec recommends using 64 bit prefetcheable BAR.
     * Check whether that works well. */
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
		     PCI_BASE_ADDRESS_MEM_TYPE_64, &bridge_dev->bar);
    return 0;
msi_error:
    slotid_cap_cleanup(dev);
slotid_error:
    shpc_cleanup(dev, &bridge_dev->bar);
shpc_error:
    pci_bridge_exitfn(dev);
bridge_error:
    return err;
}

static void pci_bridge_dev_exitfn(PCIDevice *dev)
{
    PCIBridgeDev *bridge_dev = PCI_BRIDGE_DEV(dev);
    if (msi_present(dev)) {
        msi_uninit(dev);
    }
    slotid_cap_cleanup(dev);
    shpc_cleanup(dev, &bridge_dev->bar);
    pci_bridge_exitfn(dev);
}

static void pci_bridge_dev_write_config(PCIDevice *d,
                                        uint32_t address, uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    if (msi_present(d)) {
        msi_write_config(d, address, val, len);
    }
    shpc_cap_write_config(d, address, val, len);
}

static void qdev_pci_bridge_dev_reset(DeviceState *qdev)
{
    PCIDevice *dev = PCI_DEVICE(qdev);

    pci_bridge_reset(qdev);
    shpc_reset(dev);
}

static Property pci_bridge_dev_properties[] = {
                    /* Note: 0 is not a legal chassis number. */
    DEFINE_PROP_UINT8("chassis_nr", PCIBridgeDev, chassis_nr, 0),
    DEFINE_PROP_BIT("msi", PCIBridgeDev, flags, PCI_BRIDGE_DEV_F_MSI_REQ, true),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription pci_bridge_dev_vmstate = {
    .name = "pci_bridge",
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
        SHPC_VMSTATE(shpc, PCIDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void pci_bridge_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->init = pci_bridge_dev_initfn;
    k->exit = pci_bridge_dev_exitfn;
    k->config_write = pci_bridge_dev_write_config;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_BRIDGE;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    k->is_bridge = 1,
    dc->desc = "Standard PCI Bridge";
    dc->reset = qdev_pci_bridge_dev_reset;
    dc->props = pci_bridge_dev_properties;
    dc->vmsd = &pci_bridge_dev_vmstate;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    hc->plug = shpc_device_hotplug_cb;
    hc->unplug_request = shpc_device_hot_unplug_request_cb;
}

static const TypeInfo pci_bridge_dev_info = {
    .name          = TYPE_PCI_BRIDGE_DEV,
    .parent        = TYPE_PCI_BRIDGE,
    .instance_size = sizeof(PCIBridgeDev),
    .class_init = pci_bridge_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void pci_bridge_dev_register(void)
{
    type_register_static(&pci_bridge_dev_info);
}

type_init(pci_bridge_dev_register);
