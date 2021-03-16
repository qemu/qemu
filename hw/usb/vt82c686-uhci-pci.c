#include "qemu/osdep.h"
#include "hcd-uhci.h"

static void usb_uhci_vt82c686b_realize(PCIDevice *dev, Error **errp)
{
    UHCIState *s = UHCI(dev);
    uint8_t *pci_conf = s->dev.config;

    /* USB misc control 1/2 */
    pci_set_long(pci_conf + 0x40, 0x00001000);
    /* PM capability */
    pci_set_long(pci_conf + 0x80, 0x00020001);
    /* USB legacy support  */
    pci_set_long(pci_conf + 0xc0, 0x00002000);

    usb_uhci_common_realize(dev, errp);
}

static UHCIInfo uhci_info[] = {
    {
        .name      = "vt82c686b-usb-uhci",
        .vendor_id = PCI_VENDOR_ID_VIA,
        .device_id = PCI_DEVICE_ID_VIA_UHCI,
        .revision  = 0x01,
        .irq_pin   = 3,
        .realize   = usb_uhci_vt82c686b_realize,
        .unplug    = true,
    }
};

static const TypeInfo vt82c686b_usb_uhci_type_info = {
    .parent         = TYPE_UHCI,
    .name           = "vt82c686b-usb-uhci",
    .class_init     = uhci_data_class_init,
    .class_data     = uhci_info,
};

static void vt82c686b_usb_uhci_register_types(void)
{
    type_register_static(&vt82c686b_usb_uhci_type_info);
}

type_init(vt82c686b_usb_uhci_register_types)
