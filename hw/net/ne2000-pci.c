/*
 * QEMU NE2000 emulation (PCI bus)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "ne2000.h"
#include "system/system.h"

typedef struct PCINE2000State {
    PCIDevice dev;
    NE2000State ne2000;
} PCINE2000State;

static const VMStateDescription vmstate_pci_ne2000 = {
    .name = "ne2000",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCINE2000State),
        VMSTATE_STRUCT(ne2000, PCINE2000State, 0, vmstate_ne2000, NE2000State),
        VMSTATE_END_OF_LIST()
    }
};

static NetClientInfo net_ne2000_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = ne2000_receive,
};

static void pci_ne2000_realize(PCIDevice *pci_dev, Error **errp)
{
    PCINE2000State *d = DO_UPCAST(PCINE2000State, dev, pci_dev);
    NE2000State *s;
    uint8_t *pci_conf;

    pci_conf = d->dev.config;
    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */

    s = &d->ne2000;
    ne2000_setup_io(s, DEVICE(pci_dev), 0x100);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    s->irq = pci_allocate_irq(&d->dev);

    qemu_macaddr_default_if_unset(&s->c.macaddr);
    ne2000_reset(s);

    s->nic = qemu_new_nic(&net_ne2000_info, &s->c,
                          object_get_typename(OBJECT(pci_dev)),
                          pci_dev->qdev.id,
                          &pci_dev->qdev.mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->c.macaddr.a);
}

static void pci_ne2000_exit(PCIDevice *pci_dev)
{
    PCINE2000State *d = DO_UPCAST(PCINE2000State, dev, pci_dev);
    NE2000State *s = &d->ne2000;

    qemu_del_nic(s->nic);
    qemu_free_irq(s->irq);
}

static void ne2000_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    PCINE2000State *d = DO_UPCAST(PCINE2000State, dev, pci_dev);
    NE2000State *s = &d->ne2000;

    device_add_bootindex_property(obj, &s->c.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  &pci_dev->qdev);
}

static const Property ne2000_properties[] = {
    DEFINE_NIC_PROPERTIES(PCINE2000State, ne2000.c),
};

static void ne2000_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_ne2000_realize;
    k->exit = pci_ne2000_exit;
    k->romfile = "efi-ne2k_pci.rom",
    k->vendor_id = PCI_VENDOR_ID_REALTEK;
    k->device_id = PCI_DEVICE_ID_REALTEK_8029;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->vmsd = &vmstate_pci_ne2000;
    device_class_set_props(dc, ne2000_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo ne2000_info = {
    .name          = "ne2k_pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCINE2000State),
    .class_init    = ne2000_class_init,
    .instance_init = ne2000_instance_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void ne2000_register_types(void)
{
    type_register_static(&ne2000_info);
}

type_init(ne2000_register_types)
