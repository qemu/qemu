/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "kvm_s390x.h"
#include "target/s390x/kvm/pv.h"

int kvm_s390_get_protected_dump(void)
{
    return false;
}

bool s390_is_pv(void)
{
    return false;
}

int s390_pv_query_info(void)
{
    return 0;
}

int s390_pv_vm_enable(void)
{
    return 0;
}

void s390_pv_vm_disable(void)
{
}

bool s390_pv_vm_try_disable_async(S390CcwMachineState *ms)
{
    return false;
}

int s390_pv_set_sec_parms(uint64_t origin, uint64_t length,
                          struct S390PVResponse *pv_resp, Error **errp)
{
    return 0;
}

int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak,
                   struct S390PVResponse *pv_resp)
{
    return 0;
}

void s390_pv_prep_reset(void)
{
}

int s390_pv_verify(struct S390PVResponse *pv_resp)
{
    return 0;
}

void s390_pv_unshare(void)
{
}

void s390_pv_inject_reset_error(CPUState *cs, struct S390PVResponse pv_resp)
{
}

uint64_t kvm_s390_pv_dmp_get_size_cpu(void)
{
    return 0;
}

uint64_t kvm_s390_pv_dmp_get_size_mem_state(void)
{
    return 0;
}

uint64_t kvm_s390_pv_dmp_get_size_completion_data(void)
{
    return 0;
}

bool kvm_s390_pv_info_basic_valid(void)
{
    return false;
}

int kvm_s390_dump_init(void)
{
    return 0;
}

int kvm_s390_dump_cpu(S390CPU *cpu, void *buff)
{
    return 0;
}

int kvm_s390_dump_mem_state(uint64_t addr, size_t len, void *dest)
{
    return 0;
}

int kvm_s390_dump_completion_data(void *buff)
{
    return 0;
}
