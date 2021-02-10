/*
 * Protected Virtualization header
 *
 * Copyright IBM Corp. 2020
 * Author(s):
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390_PV_H
#define HW_S390_PV_H

#include "qapi/error.h"
#include "sysemu/kvm.h"

#ifdef CONFIG_KVM
#include "cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"

static inline bool s390_is_pv(void)
{
    static S390CcwMachineState *ccw;
    Object *obj;

    if (ccw) {
        return ccw->pv;
    }

    /* we have to bail out for the "none" machine */
    obj = object_dynamic_cast(qdev_get_machine(),
                              TYPE_S390_CCW_MACHINE);
    if (!obj) {
        return false;
    }
    ccw = S390_CCW_MACHINE(obj);
    return ccw->pv;
}

int s390_pv_vm_enable(void);
void s390_pv_vm_disable(void);
int s390_pv_set_sec_parms(uint64_t origin, uint64_t length);
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak);
void s390_pv_prep_reset(void);
int s390_pv_verify(void);
void s390_pv_unshare(void);
void s390_pv_inject_reset_error(CPUState *cs);
#else /* CONFIG_KVM */
static inline bool s390_is_pv(void) { return false; }
static inline int s390_pv_vm_enable(void) { return 0; }
static inline void s390_pv_vm_disable(void) {}
static inline int s390_pv_set_sec_parms(uint64_t origin, uint64_t length) { return 0; }
static inline int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak) { return 0; }
static inline void s390_pv_prep_reset(void) {}
static inline int s390_pv_verify(void) { return 0; }
static inline void s390_pv_unshare(void) {}
static inline void s390_pv_inject_reset_error(CPUState *cs) {};
#endif /* CONFIG_KVM */

int s390_pv_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);
static inline int s390_pv_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    if (!cgs) {
        return 0;
    }
    if (kvm_enabled()) {
        return s390_pv_kvm_init(cgs, errp);
    }

    error_setg(errp, "Protected Virtualization requires KVM");
    return -1;
}

#endif /* HW_S390_PV_H */
