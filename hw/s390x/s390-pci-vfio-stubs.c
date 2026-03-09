/*
 * s390 vfio-pci stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/s390x/s390-pci-vfio.h"

bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    return false;
}

S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                          S390PCIBusDevice *pbdev)
{
    return NULL;
}

void s390_pci_end_dma_count(S390pciState *s, S390PCIDMACount *cnt)
{
}

bool s390_pci_get_host_fh(S390PCIBusDevice *pbdev, uint32_t *fh)
{
    return false;
}

void s390_pci_get_clp_info(S390PCIBusDevice *pbdev)
{
}
