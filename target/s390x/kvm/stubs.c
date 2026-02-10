/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "target/s390x/kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "target/s390x/cpu_models.h"

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

bool kvm_s390_apply_cpu_model(const S390CPUModel *model,  Error **errp)
{
    g_assert_not_reached();
}

void kvm_s390_access_exception(S390CPU *cpu, uint16_t code, uint64_t te_code)
{
    g_assert_not_reached();
}

int kvm_s390_mem_op(S390CPU *cpu, vaddr addr, uint8_t ar, void *hostbuf,
                    int len, bool is_write)
{
    g_assert_not_reached();
}

int kvm_s390_mem_op_pv(S390CPU *cpu, vaddr addr, void *hostbuf, int len,
                       bool is_write)
{
    g_assert_not_reached();
}

int kvm_s390_set_cpu_state(S390CPU *cpu, uint8_t cpu_state)
{
    g_assert_not_reached();
}

void kvm_s390_vcpu_interrupt_pre_save(S390CPU *cpu)
{
    g_assert_not_reached();
}

int kvm_s390_vcpu_interrupt_post_load(S390CPU *cpu)
{
    g_assert_not_reached();
}

int kvm_s390_get_hpage_1m(void)
{
    g_assert_not_reached();
}

void kvm_s390_enable_css_support(S390CPU *cpu)
{
    g_assert_not_reached();
}

int kvm_s390_assign_subch_ioeventfd(EventNotifier *notifier, uint32_t sch,
                                    int vq, bool assign)
{
    g_assert_not_reached();
}

void kvm_s390_cmma_reset(void)
{
    g_assert_not_reached();
}

void kvm_s390_crypto_reset(void)
{
    g_assert_not_reached();
}

void kvm_s390_set_diag318(CPUState *cs, uint64_t diag318_info)
{
    g_assert_not_reached();
}

int kvm_s390_topology_set_mtcr(uint64_t attr)
{
    g_assert_not_reached();
}
