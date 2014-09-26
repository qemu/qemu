#ifndef LIBQOS_USB_H
#define LIBQOS_USB_H

#include "libqos/pci-pc.h"

struct qhc {
    QPCIDevice *dev;
    void *base;
};

void qusb_pci_init_one(QPCIBus *pcibus, struct qhc *hc,
                       uint32_t devfn, int bar);
void uhci_port_test(struct qhc *hc, int port, uint16_t expect);
#endif
