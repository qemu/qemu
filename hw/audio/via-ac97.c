/*
 * VIA south bridges sound support
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

/*
 * TODO: This is entirely boiler plate just registering empty PCI devices
 * with the right ID guests expect, functionality should be added here.
 */

#include "qemu/osdep.h"
#include "hw/isa/vt82c686.h"
#include "hw/pci/pci.h"

struct VIAAC97State {
    PCIDevice dev;
};

struct VIAMC97State {
    PCIDevice dev;
};

OBJECT_DECLARE_SIMPLE_TYPE(VIAAC97State, VIA_AC97)
OBJECT_DECLARE_SIMPLE_TYPE(VIAMC97State, VIA_MC97)

static void via_ac97_realize(PCIDevice *dev, Error **errp)
{
    VIAAC97State *s = VIA_AC97(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_INVALIDATE |
                 PCI_COMMAND_PARITY);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_CAP_LIST |
                 PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_long(pci_conf + PCI_INTERRUPT_PIN, 0x03);
}

static void via_ac97_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = via_ac97_realize;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_AC97;
    k->revision = 0x50;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "AC97";
}

static const TypeInfo via_ac97_info = {
    .name          = TYPE_VIA_AC97,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VIAAC97State),
    .class_init    = via_ac97_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void via_mc97_realize(PCIDevice *dev, Error **errp)
{
    VIAMC97State *s = VIA_MC97(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_INVALIDATE |
                 PCI_COMMAND_VGA_PALETTE);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_long(pci_conf + PCI_INTERRUPT_PIN, 0x03);
}

static void via_mc97_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = via_mc97_realize;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_MC97;
    k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
    k->revision = 0x30;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "MC97";
}

static const TypeInfo via_mc97_info = {
    .name          = TYPE_VIA_MC97,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VIAMC97State),
    .class_init    = via_mc97_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void via_ac97_register_types(void)
{
    type_register_static(&via_ac97_info);
    type_register_static(&via_mc97_info);
}

type_init(via_ac97_register_types)
