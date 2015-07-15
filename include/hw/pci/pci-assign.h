/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Just split from hw/i386/kvm/pci-assign.c.
 */
#ifndef PCI_ASSIGN_H
#define PCI_ASSIGN_H

#include "hw/pci/pci.h"

//#define DEVICE_ASSIGNMENT_DEBUG

#ifdef DEVICE_ASSIGNMENT_DEBUG
#define DEBUG(fmt, ...)                                       \
    do {                                                      \
        fprintf(stderr, "%s: " fmt, __func__ , __VA_ARGS__);  \
    } while (0)
#else
#define DEBUG(fmt, ...)
#endif

void *pci_assign_dev_load_option_rom(PCIDevice *dev, struct Object *owner,
                                     int *size, unsigned int domain,
                                     unsigned int bus, unsigned int slot,
                                     unsigned int function);
#endif /* PCI_ASSIGN_H */
