#ifndef LIBQOS_USB_H
#define LIBQOS_USB_H

#include "pci-pc.h"

struct qhc {
    QPCIDevice *dev;
    QPCIBar bar;
};

void qusb_pci_init_one(QPCIBus *pcibus, struct qhc *hc,
                       uint32_t devfn, int bar);
void uhci_port_test(struct qhc *hc, int port, uint16_t expect);
void uhci_deinit(struct qhc *hc);

void usb_test_hotplug(QTestState *qts, const char *bus_name, const char *port,
                      void (*port_check)(void));
#endif
