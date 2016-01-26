/*
 * QEMU USB EHCI Emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/usb/hcd-ehci.h"
#include "qemu/range.h"

typedef struct EHCIPCIInfo {
    const char *name;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  revision;
    bool companion;
} EHCIPCIInfo;

static void usb_ehci_pci_realize(PCIDevice *dev, Error **errp)
{
    EHCIPCIState *i = PCI_EHCI(dev);
    EHCIState *s = &i->ehci;
    uint8_t *pci_conf = dev->config;

    pci_set_byte(&pci_conf[PCI_CLASS_PROG], 0x20);

    /* capabilities pointer */
    pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x00);
    /* pci_set_byte(&pci_conf[PCI_CAPABILITY_LIST], 0x50); */

    pci_set_byte(&pci_conf[PCI_INTERRUPT_PIN], 4); /* interrupt pin D */
    pci_set_byte(&pci_conf[PCI_MIN_GNT], 0);
    pci_set_byte(&pci_conf[PCI_MAX_LAT], 0);

    /* pci_conf[0x50] = 0x01; *//* power management caps */

    pci_set_byte(&pci_conf[USB_SBRN], USB_RELEASE_2); /* release # (2.1.4) */
    pci_set_byte(&pci_conf[0x61], 0x20);  /* frame length adjustment (2.1.5) */
    pci_set_word(&pci_conf[0x62], 0x00);  /* port wake up capability (2.1.6) */

    pci_conf[0x64] = 0x00;
    pci_conf[0x65] = 0x00;
    pci_conf[0x66] = 0x00;
    pci_conf[0x67] = 0x00;
    pci_conf[0x68] = 0x01;
    pci_conf[0x69] = 0x00;
    pci_conf[0x6a] = 0x00;
    pci_conf[0x6b] = 0x00;  /* USBLEGSUP */
    pci_conf[0x6c] = 0x00;
    pci_conf[0x6d] = 0x00;
    pci_conf[0x6e] = 0x00;
    pci_conf[0x6f] = 0xc0;  /* USBLEFCTLSTS */

    s->irq = pci_allocate_irq(dev);
    s->as = pci_get_address_space(dev);

    usb_ehci_realize(s, DEVICE(dev), NULL);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mem);
}

static void usb_ehci_pci_init(Object *obj)
{
    DeviceClass *dc = OBJECT_GET_CLASS(DeviceClass, obj, TYPE_DEVICE);
    EHCIPCIState *i = PCI_EHCI(obj);
    EHCIState *s = &i->ehci;

    s->caps[0x09] = 0x68;        /* EECP */

    s->capsbase = 0x00;
    s->opregbase = 0x20;
    s->portscbase = 0x44;
    s->portnr = NB_PORTS;

    if (!dc->hotpluggable) {
        s->companion_enable = true;
    }

    usb_ehci_init(s, DEVICE(obj));
}

static void usb_ehci_pci_exit(PCIDevice *dev)
{
    EHCIPCIState *i = PCI_EHCI(dev);
    EHCIState *s = &i->ehci;

    usb_ehci_unrealize(s, DEVICE(dev), NULL);

    g_free(s->irq);
    s->irq = NULL;
}

static void usb_ehci_pci_reset(DeviceState *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    EHCIPCIState *i = PCI_EHCI(pci_dev);
    EHCIState *s = &i->ehci;

    ehci_reset(s);
}

static void usb_ehci_pci_write_config(PCIDevice *dev, uint32_t addr,
                                      uint32_t val, int l)
{
    EHCIPCIState *i = PCI_EHCI(dev);
    bool busmaster;

    pci_default_write_config(dev, addr, val, l);

    if (!range_covers_byte(addr, l, PCI_COMMAND)) {
        return;
    }
    busmaster = pci_get_word(dev->config + PCI_COMMAND) & PCI_COMMAND_MASTER;
    i->ehci.as = busmaster ? pci_get_address_space(dev) : &address_space_memory;
}

static Property ehci_pci_properties[] = {
    DEFINE_PROP_UINT32("maxframes", EHCIPCIState, ehci.maxframes, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_ehci_pci = {
    .name        = "ehci",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pcidev, EHCIPCIState),
        VMSTATE_STRUCT(ehci, EHCIPCIState, 2, vmstate_ehci, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void ehci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = usb_ehci_pci_realize;
    k->exit = usb_ehci_pci_exit;
    k->class_id = PCI_CLASS_SERIAL_USB;
    k->config_write = usb_ehci_pci_write_config;
    dc->vmsd = &vmstate_ehci_pci;
    dc->props = ehci_pci_properties;
    dc->reset = usb_ehci_pci_reset;
}

static const TypeInfo ehci_pci_type_info = {
    .name = TYPE_PCI_EHCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(EHCIPCIState),
    .instance_init = usb_ehci_pci_init,
    .abstract = true,
    .class_init = ehci_class_init,
};

static void ehci_data_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    EHCIPCIInfo *i = data;

    k->vendor_id = i->vendor_id;
    k->device_id = i->device_id;
    k->revision = i->revision;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    if (i->companion) {
        dc->hotpluggable = false;
    }
}

static struct EHCIPCIInfo ehci_pci_info[] = {
    {
        .name      = "usb-ehci",
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = PCI_DEVICE_ID_INTEL_82801D, /* ich4 */
        .revision  = 0x10,
    },{
        .name      = "ich9-usb-ehci1", /* 00:1d.7 */
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = PCI_DEVICE_ID_INTEL_82801I_EHCI1,
        .revision  = 0x03,
        .companion = true,
    },{
        .name      = "ich9-usb-ehci2", /* 00:1a.7 */
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = PCI_DEVICE_ID_INTEL_82801I_EHCI2,
        .revision  = 0x03,
        .companion = true,
    }
};

static void ehci_pci_register_types(void)
{
    TypeInfo ehci_type_info = {
        .parent        = TYPE_PCI_EHCI,
        .class_init    = ehci_data_class_init,
    };
    int i;

    type_register_static(&ehci_pci_type_info);

    for (i = 0; i < ARRAY_SIZE(ehci_pci_info); i++) {
        ehci_type_info.name = ehci_pci_info[i].name;
        ehci_type_info.class_data = ehci_pci_info + i;
        type_register(&ehci_type_info);
    }
}

type_init(ehci_pci_register_types)

struct ehci_companions {
    const char *name;
    int func;
    int port;
};

static const struct ehci_companions ich9_1d[] = {
    { .name = "ich9-usb-uhci1", .func = 0, .port = 0 },
    { .name = "ich9-usb-uhci2", .func = 1, .port = 2 },
    { .name = "ich9-usb-uhci3", .func = 2, .port = 4 },
};

static const struct ehci_companions ich9_1a[] = {
    { .name = "ich9-usb-uhci4", .func = 0, .port = 0 },
    { .name = "ich9-usb-uhci5", .func = 1, .port = 2 },
    { .name = "ich9-usb-uhci6", .func = 2, .port = 4 },
};

int ehci_create_ich9_with_companions(PCIBus *bus, int slot)
{
    const struct ehci_companions *comp;
    PCIDevice *ehci, *uhci;
    BusState *usbbus;
    const char *name;
    int i;

    switch (slot) {
    case 0x1d:
        name = "ich9-usb-ehci1";
        comp = ich9_1d;
        break;
    case 0x1a:
        name = "ich9-usb-ehci2";
        comp = ich9_1a;
        break;
    default:
        return -1;
    }

    ehci = pci_create_multifunction(bus, PCI_DEVFN(slot, 7), true, name);
    qdev_init_nofail(&ehci->qdev);
    usbbus = QLIST_FIRST(&ehci->qdev.child_bus);

    for (i = 0; i < 3; i++) {
        uhci = pci_create_multifunction(bus, PCI_DEVFN(slot, comp[i].func),
                                        true, comp[i].name);
        qdev_prop_set_string(&uhci->qdev, "masterbus", usbbus->name);
        qdev_prop_set_uint32(&uhci->qdev, "firstport", comp[i].port);
        qdev_init_nofail(&uhci->qdev);
    }
    return 0;
}
