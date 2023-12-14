/*
 * s390 vfio-pci interfaces
 *
 * Copyright 2020 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PCI_VFIO_H
#define HW_S390_PCI_VFIO_H

#include "hw/s390x/s390-pci-bus.h"
#include CONFIG_DEVICES

#ifdef CONFIG_VFIO
bool s390_pci_update_dma_avail(int fd, unsigned int *avail);
S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                          S390PCIBusDevice *pbdev);
void s390_pci_end_dma_count(S390pciState *s, S390PCIDMACount *cnt);
bool s390_pci_get_host_fh(S390PCIBusDevice *pbdev, uint32_t *fh);
void s390_pci_get_clp_info(S390PCIBusDevice *pbdev);
#else
static inline bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    return false;
}
static inline S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                                        S390PCIBusDevice *pbdev)
{
    return NULL;
}
static inline void s390_pci_end_dma_count(S390pciState *s,
                                          S390PCIDMACount *cnt) { }
static inline bool s390_pci_get_host_fh(S390PCIBusDevice *pbdev, uint32_t *fh)
{
    return false;
}
static inline void s390_pci_get_clp_info(S390PCIBusDevice *pbdev) { }
#endif

#endif
