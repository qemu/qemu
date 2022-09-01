#ifndef HW_VT82C686_H
#define HW_VT82C686_H

#include "hw/pci/pci.h"

#define TYPE_VT82C686B_ISA "vt82c686b-isa"
#define TYPE_VT82C686B_USB_UHCI "vt82c686b-usb-uhci"
#define TYPE_VT8231_ISA "vt8231-isa"
#define TYPE_VIA_AC97 "via-ac97"
#define TYPE_VIA_IDE "via-ide"
#define TYPE_VIA_MC97 "via-mc97"

void via_isa_set_irq(PCIDevice *d, int n, int level);

#endif
