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

#ifdef CONFIG_KVM
bool s390_pci_kvm_interp_allowed(void);
#else
static inline bool s390_pci_kvm_interp_allowed(void)
{
    return false;
}
#endif

#endif
