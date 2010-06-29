#ifndef QEMU_USB_UHCI_H
#define QEMU_USB_UHCI_H

#include "qemu-common.h"

void usb_uhci_piix3_init(PCIBus *bus, int devfn);
void usb_uhci_piix4_init(PCIBus *bus, int devfn);
void usb_uhci_vt82c686b_init(PCIBus *bus, int devfn);

#endif
