#ifndef PCI_SLOTID_CAP_H
#define PCI_SLOTID_CAP_H

#include "qemu-common.h"

int slotid_cap_init(PCIDevice *dev, int nslots,
                    uint8_t chassis,
                    unsigned offset);
void slotid_cap_cleanup(PCIDevice *dev);

#endif
