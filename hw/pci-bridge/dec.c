/*
 * QEMU DEC 21154 PCI bridge
 *
 * Copyright (c) 2006-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "dec.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"

#define DEC_21154(obj) OBJECT_CHECK(DECState, (obj), TYPE_DEC_21154)

typedef struct DECState {
    PCIHostState parent_obj;
} DECState;

static int dec_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num;
}

static void dec_pci_bridge_realize(PCIDevice *pci_dev, Error **errp)
{
    pci_bridge_initfn(pci_dev, TYPE_PCI_BUS);
}

static void dec_21154_pci_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    k->realize = dec_pci_bridge_realize;
    k->exit = pci_bridge_exitfn;
    k->vendor_id = PCI_VENDOR_ID_DEC;
    k->device_id = PCI_DEVICE_ID_DEC_21154;
    k->config_write = pci_bridge_write_config;
    k->is_bridge = true;
    dc->desc = "DEC 21154 PCI-PCI bridge";
    dc->reset = pci_bridge_reset;
    dc->vmsd = &vmstate_pci_device;
}

static const TypeInfo dec_21154_pci_bridge_info = {
    .name          = "dec-21154-p2p-bridge",
    .parent        = TYPE_PCI_BRIDGE,
    .instance_size = sizeof(PCIBridge),
    .class_init    = dec_21154_pci_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

PCIBus *pci_dec_21154_init(PCIBus *parent_bus, int devfn)
{
    PCIDevice *dev;
    PCIBridge *br;

    dev = pci_new_multifunction(devfn, false, "dec-21154-p2p-bridge");
    br = PCI_BRIDGE(dev);
    pci_bridge_map_irq(br, "DEC 21154 PCI-PCI bridge", dec_map_irq);
    pci_realize_and_unref(dev, parent_bus, &error_fatal);
    return pci_bridge_get_sec_bus(br);
}

static void pci_dec_21154_device_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *phb;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    phb = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&phb->conf_mem, OBJECT(dev), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&phb->data_mem, OBJECT(dev), &pci_host_data_le_ops,
                          dev, "pci-data-idx", 0x1000);
    sysbus_init_mmio(sbd, &phb->conf_mem);
    sysbus_init_mmio(sbd, &phb->data_mem);
}

static void dec_21154_pci_host_realize(PCIDevice *d, Error **errp)
{
    /* PCI2PCI bridge same values as PearPC - check this */
}

static void dec_21154_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    k->realize = dec_21154_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_DEC;
    k->device_id = PCI_DEVICE_ID_DEC_21154;
    k->revision = 0x02;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    k->is_bridge = true;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo dec_21154_pci_host_info = {
    .name          = "dec-21154",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = dec_21154_pci_host_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_dec_21154_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pci_dec_21154_device_realize;
}

static const TypeInfo pci_dec_21154_device_info = {
    .name          = TYPE_DEC_21154,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(DECState),
    .class_init    = pci_dec_21154_device_class_init,
};

static void dec_register_types(void)
{
    type_register_static(&pci_dec_21154_device_info);
    type_register_static(&dec_21154_pci_host_info);
    type_register_static(&dec_21154_pci_bridge_info);
}

type_init(dec_register_types)
