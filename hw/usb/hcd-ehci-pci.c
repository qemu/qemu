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

#include "hw/usb/hcd-ehci.h"
#include "hw/pci.h"

typedef struct EHCIPCIState {
    PCIDevice pcidev;
    EHCIState ehci;
} EHCIPCIState;

static int usb_ehci_pci_initfn(PCIDevice *dev)
{
    EHCIPCIState *i = DO_UPCAST(EHCIPCIState, pcidev, dev);
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

    s->caps[0x09] = 0x68;        /* EECP */

    s->irq = dev->irq[3];
    s->dma = pci_dma_context(dev);

    s->capsbase = 0x00;
    s->opregbase = 0x20;

    usb_ehci_initfn(s, DEVICE(dev));
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mem);

    return 0;
}

static Property ehci_pci_properties[] = {
    DEFINE_PROP_UINT32("maxframes", EHCIPCIState, ehci.maxframes, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_ehci_pci = {
    .name        = "ehci",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pcidev, EHCIPCIState),
        VMSTATE_STRUCT(ehci, EHCIPCIState, 2, vmstate_ehci, EHCIState),
    }
};

static void ehci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = usb_ehci_pci_initfn;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82801D; /* ich4 */
    k->revision = 0x10;
    k->class_id = PCI_CLASS_SERIAL_USB;
    dc->vmsd = &vmstate_ehci;
    dc->props = ehci_pci_properties;
}

static TypeInfo ehci_info = {
    .name          = "usb-ehci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(EHCIState),
    .class_init    = ehci_class_init,
};

static void ich9_ehci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = usb_ehci_pci_initfn;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82801I_EHCI1;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_SERIAL_USB;
    dc->vmsd = &vmstate_ehci;
    dc->props = ehci_pci_properties;
}

static TypeInfo ich9_ehci_info = {
    .name          = "ich9-usb-ehci1",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(EHCIState),
    .class_init    = ich9_ehci_class_init,
};

static void ehci_pci_register_types(void)
{
    type_register_static(&ehci_info);
    type_register_static(&ich9_ehci_info);
}

type_init(ehci_pci_register_types)
