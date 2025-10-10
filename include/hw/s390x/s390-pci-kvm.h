/*
 * s390 PCI KVM interfaces
 *
 * Copyright 2022 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PCI_KVM_H
#define HW_S390_PCI_KVM_H

#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-inst.h"
#include "system/kvm.h"

#ifdef CONFIG_KVM
static inline void s390_pcihost_kvm_realize(void)
{
    kvm_msi_via_irqfd_allowed = kvm_irqfds_enabled();
}

bool s390_pci_kvm_interp_allowed(void);
int s390_pci_kvm_aif_enable(S390PCIBusDevice *pbdev, ZpciFib *fib, bool assist);
int s390_pci_kvm_aif_disable(S390PCIBusDevice *pbdev);
#else
static inline void s390_pcihost_kvm_realize(void) {}
static inline bool s390_pci_kvm_interp_allowed(void)
{
    return false;
}
static inline int s390_pci_kvm_aif_enable(S390PCIBusDevice *pbdev, ZpciFib *fib,
                                          bool assist)
{
    return -EINVAL;
}
static inline int s390_pci_kvm_aif_disable(S390PCIBusDevice *pbdev)
{
    return -EINVAL;
}
#endif

#endif
