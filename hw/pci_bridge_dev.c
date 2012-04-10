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

#include "pci_bridge.h"
#include "pci_ids.h"
#include "msi.h"
#include "shpc.h"
#include "slotid_cap.h"
#include "memory.h"
#include "pci_internals.h"

#define REDHAT_PCI_VENDOR_ID 0x1b36
#define PCI_BRIDGE_DEV_VENDOR_ID REDHAT_PCI_VENDOR_ID
#define PCI_BRIDGE_DEV_DEVICE_ID 0x1

struct PCIBridgeDev {
    PCIBridge bridge;
    MemoryRegion bar;
    uint8_t chassis_nr;
#define PCI_BRIDGE_DEV_F_MSI_REQ 0
    uint32_t flags;
};
typedef struct PCIBridgeDev PCIBridgeDev;

/* Mapping mandated by PCI-to-PCI Bridge architecture specification,
 * revision 1.2 */
/* Table 9-1: Interrupt Binding for Devices Behind a Bridge */
static int pci_bridge_dev_map_irq_fn(PCIDevice *dev, int irq_num)
{
    return (irq_num + PCI_SLOT(dev->devfn)) % PCI_NUM_PINS;
}

static int pci_bridge_dev_initfn(PCIDevice *dev)
{
    PCIBridge *br = DO_UPCAST(PCIBridge, dev, dev);
    PCIBridgeDev *bridge_dev = DO_UPCAST(PCIBridgeDev, bridge, br);
    int err;
    pci_bridge_map_irq(br, NULL, pci_bridge_dev_map_irq_fn);
    err = pci_bridge_initfn(dev);
    if (err) {
        goto bridge_error;
    }
    memory_region_init(&bridge_dev->bar, "shpc-bar", shpc_bar_size(dev));
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
    dev->config[PCI_INTERRUPT_PIN] = 0x1;
    return 0;
msi_error:
    slotid_cap_cleanup(dev);
slotid_error:
    shpc_cleanup(dev, &bridge_dev->bar);
shpc_error:
    memory_region_destroy(&bridge_dev->bar);
bridge_error:
    return err;
}

static int pci_bridge_dev_exitfn(PCIDevice *dev)
{
    PCIBridge *br = DO_UPCAST(PCIBridge, dev, dev);
    PCIBridgeDev *bridge_dev = DO_UPCAST(PCIBridgeDev, bridge, br);
    int ret;
    if (msi_present(dev)) {
        msi_uninit(dev);
    }
    slotid_cap_cleanup(dev);
    shpc_cleanup(dev, &bridge_dev->bar);
    memory_region_destroy(&bridge_dev->bar);
    ret = pci_bridge_exitfn(dev);
    assert(!ret);
    return 0;
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
    PCIDevice *dev = DO_UPCAST(PCIDevice, qdev, qdev);
    pci_bridge_reset(qdev);
    if (msi_present(dev)) {
        msi_reset(dev);
    }
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
        VMSTATE_PCI_DEVICE(bridge.dev, PCIBridgeDev),
        SHPC_VMSTATE(bridge.dev.shpc, PCIBridgeDev),
        VMSTATE_END_OF_LIST()
    }
};

static void pci_bridge_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->init = pci_bridge_dev_initfn;
    k->exit = pci_bridge_dev_exitfn;
    k->config_write = pci_bridge_dev_write_config;
    k->vendor_id = PCI_BRIDGE_DEV_VENDOR_ID;
    k->device_id = PCI_BRIDGE_DEV_DEVICE_ID;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    k->is_bridge = 1,
    dc->desc = "Standard PCI Bridge";
    dc->reset = qdev_pci_bridge_dev_reset;
    dc->props = pci_bridge_dev_properties;
    dc->vmsd = &pci_bridge_dev_vmstate;
}

static TypeInfo pci_bridge_dev_info = {
    .name = "pci-bridge",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBridgeDev),
    .class_init = pci_bridge_dev_class_init,
};

static void pci_bridge_dev_register(void)
{
    type_register_static(&pci_bridge_dev_info);
}

type_init(pci_bridge_dev_register);
