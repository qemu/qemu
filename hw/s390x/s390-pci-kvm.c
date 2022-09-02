/*
 * s390 zPCI KVM interfaces
 *
 * Copyright 2022 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"

#include "kvm/kvm_s390x.h"
#include "hw/s390x/pv.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "cpu_models.h"

bool s390_pci_kvm_interp_allowed(void)
{
    return kvm_s390_get_zpci_op() && !s390_is_pv();
}
