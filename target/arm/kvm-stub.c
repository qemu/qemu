/*
 * QEMU KVM ARM specific function stubs
 *
 * Copyright Linaro Limited 2013
 *
 * Author: Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "kvm_arm.h"

bool write_kvmstate_to_list(ARMCPU *cpu)
{
    g_assert_not_reached();
}

bool write_list_to_kvmstate(ARMCPU *cpu, int level)
{
    g_assert_not_reached();
}

/*
 * It's safe to call these functions without KVM support.
 * They should either do nothing or return "not supported".
 */
bool kvm_arm_aarch32_supported(void)
{
    return false;
}

bool kvm_arm_pmu_supported(void)
{
    return false;
}

bool kvm_arm_sve_supported(void)
{
    return false;
}

bool kvm_arm_mte_supported(void)
{
    return false;
}

bool kvm_arm_el2_supported(void)
{
    return false;
}

/*
 * These functions should never actually be called without KVM support.
 */
void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void kvm_arm_add_vcpu_properties(ARMCPU *cpu)
{
    g_assert_not_reached();
}

int kvm_arm_get_max_vm_ipa_size(MachineState *ms, bool *fixed_ipa)
{
    g_assert_not_reached();
}

int kvm_arm_vgic_probe(void)
{
    g_assert_not_reached();
}

void kvm_arm_pmu_set_irq(ARMCPU *cpu, int irq)
{
    g_assert_not_reached();
}

void kvm_arm_pmu_init(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void kvm_arm_pvtime_init(ARMCPU *cpu, uint64_t ipa)
{
    g_assert_not_reached();
}

void kvm_arm_steal_time_finalize(ARMCPU *cpu, Error **errp)
{
    g_assert_not_reached();
}

uint32_t kvm_arm_sve_get_vls(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void kvm_arm_enable_mte(Object *cpuobj, Error **errp)
{
    g_assert_not_reached();
}

void kvm_arm_reset_vcpu(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void arm_cpu_kvm_set_irq(void *arm_cpu, int irq, int level)
{
    g_assert_not_reached();
}

void kvm_arm_cpu_pre_save(ARMCPU *cpu)
{
    g_assert_not_reached();
}

bool kvm_arm_cpu_post_load(ARMCPU *cpu)
{
    g_assert_not_reached();
}
