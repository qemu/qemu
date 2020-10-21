#ifndef HW_USB_XHCI_H
#define HW_USB_XHCI_H

#define TYPE_XHCI "base-xhci"
#define TYPE_NEC_XHCI "nec-usb-xhci"
#define TYPE_QEMU_XHCI "qemu-xhci"
#define TYPE_XHCI_SYSBUS "sysbus-xhci"

#define XHCI_MAXPORTS_2 15
#define XHCI_MAXPORTS_3 15

#define XHCI_MAXPORTS (XHCI_MAXPORTS_2 + XHCI_MAXPORTS_3)
#define XHCI_MAXSLOTS 64
#define XHCI_MAXINTRS 16

/* must be power of 2 */
#define XHCI_LEN_REGS 0x4000

void xhci_sysbus_build_aml(Aml *scope, uint32_t mmio, unsigned int irq);

#endif
