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

#include <linux/kvm.h>

#include "kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "hw/s390x/s390-pci-inst.h"
#include "cpu_models.h"

bool s390_pci_kvm_interp_allowed(void)
{
    return kvm_s390_get_zpci_op() && !s390_is_pv();
}

int s390_pci_kvm_aif_enable(S390PCIBusDevice *pbdev, ZpciFib *fib, bool assist)
{
    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh,
        .op = KVM_S390_ZPCIOP_REG_AEN,
        .u.reg_aen.ibv = fib->aibv,
        .u.reg_aen.sb = fib->aisb,
        .u.reg_aen.noi = FIB_DATA_NOI(fib->data),
        .u.reg_aen.isc = FIB_DATA_ISC(fib->data),
        .u.reg_aen.sbo = FIB_DATA_AISBO(fib->data),
        .u.reg_aen.flags = (assist) ? 0 : KVM_S390_ZPCIOP_REGAEN_HOST
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
}

int s390_pci_kvm_aif_disable(S390PCIBusDevice *pbdev)
{
    struct kvm_s390_zpci_op args = {
        .fh = pbdev->fh,
        .op = KVM_S390_ZPCIOP_DEREG_AEN
    };

    return kvm_vm_ioctl(kvm_state, KVM_S390_ZPCI_OP, &args);
}
