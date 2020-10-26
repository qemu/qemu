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

#ifdef CONFIG_LINUX
bool s390_pci_update_dma_avail(int fd, unsigned int *avail);
#else
static inline bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    return false;
}
#endif

#endif
