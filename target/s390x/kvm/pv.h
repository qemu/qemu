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
#include "hw/s390x/s390-virtio-ccw.h"

#ifdef CONFIG_KVM
#include "cpu.h"

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

int s390_pv_query_info(void);
int s390_pv_vm_enable(void);
void s390_pv_vm_disable(void);
bool s390_pv_vm_try_disable_async(S390CcwMachineState *ms);
int s390_pv_set_sec_parms(uint64_t origin, uint64_t length);
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak);
void s390_pv_prep_reset(void);
int s390_pv_verify(void);
void s390_pv_unshare(void);
void s390_pv_inject_reset_error(CPUState *cs);
uint64_t kvm_s390_pv_dmp_get_size_cpu(void);
uint64_t kvm_s390_pv_dmp_get_size_mem_state(void);
uint64_t kvm_s390_pv_dmp_get_size_completion_data(void);
bool kvm_s390_pv_info_basic_valid(void);
int kvm_s390_dump_init(void);
int kvm_s390_dump_cpu(S390CPU *cpu, void *buff);
int kvm_s390_dump_mem_state(uint64_t addr, size_t len, void *dest);
int kvm_s390_dump_completion_data(void *buff);
#else /* CONFIG_KVM */
static inline bool s390_is_pv(void) { return false; }
static inline int s390_pv_query_info(void) { return 0; }
static inline int s390_pv_vm_enable(void) { return 0; }
static inline void s390_pv_vm_disable(void) {}
static inline bool s390_pv_vm_try_disable_async(S390CcwMachineState *ms) { return false; }
static inline int s390_pv_set_sec_parms(uint64_t origin, uint64_t length) { return 0; }
static inline int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak) { return 0; }
static inline void s390_pv_prep_reset(void) {}
static inline int s390_pv_verify(void) { return 0; }
static inline void s390_pv_unshare(void) {}
static inline void s390_pv_inject_reset_error(CPUState *cs) {};
static inline uint64_t kvm_s390_pv_dmp_get_size_cpu(void) { return 0; }
static inline uint64_t kvm_s390_pv_dmp_get_size_mem_state(void) { return 0; }
static inline uint64_t kvm_s390_pv_dmp_get_size_completion_data(void) { return 0; }
static inline bool kvm_s390_pv_info_basic_valid(void) { return false; }
static inline int kvm_s390_dump_init(void) { return 0; }
static inline int kvm_s390_dump_cpu(S390CPU *cpu, void *buff) { return 0; }
static inline int kvm_s390_dump_mem_state(uint64_t addr, size_t len,
                                          void *dest) { return 0; }
static inline int kvm_s390_dump_completion_data(void *buff) { return 0; }
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
