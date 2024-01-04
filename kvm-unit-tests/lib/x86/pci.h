#ifndef PCI_H
#define PCI_H

#include <inttypes.h>
#include "libcflat.h"

typedef uint16_t pcidevaddr_t;
enum {
    PCIDEVADDR_INVALID = 0x0
};
pcidevaddr_t pci_find_dev(uint16_t vendor_id, uint16_t device_id);
unsigned long pci_bar_addr(pcidevaddr_t dev, int bar_num);
bool pci_bar_is_memory(pcidevaddr_t dev, int bar_num);
bool pci_bar_is_valid(pcidevaddr_t dev, int bar_num);

#endif
