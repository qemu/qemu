#ifndef QEMU_USB_OHCI_H
#define QEMU_USB_OHCI_H

#include "qemu-common.h"

void usb_ohci_init_pci(struct PCIBus *bus, int devfn, int be);
void usb_ohci_init_pxa(target_phys_addr_t base, int num_ports, int devfn,
                       qemu_irq irq, int be);
#endif

