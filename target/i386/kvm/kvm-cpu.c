/*
 * x86 KVM CPU type initialization
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "host-cpu.h"
#include "kvm-cpu.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"

#include "kvm_i386.h"
#include "hw/core/accel-cpu.h"

static bool kvm_cpu_realizefn(CPUState *cs, Error **errp)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /*
     * The realize order is important, since x86_cpu_realize() checks if
     * nothing else has been set by the user (or by accelerators) in
     * cpu->ucode_rev and cpu->phys_bits, and updates the CPUID results in
     * mwait.ecx.
     * This accel realization code also assumes cpu features are already expanded.
     *
     * realize order:
     *
     * x86_cpu_realize():
     *  -> x86_cpu_expand_features()
     *  -> cpu_exec_realizefn():
     *            -> accel_cpu_common_realize()
     *               kvm_cpu_realizefn() -> host_cpu_realizefn()
     *  -> cpu_common_realizefn()
     *  -> check/update ucode_rev, phys_bits, mwait
     */
    if (cpu->max_features) {
        if (enable_cpu_pm && kvm_has_waitpkg()) {
            env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_WAITPKG;
        }
        if (cpu->ucode_rev == 0) {
            cpu->ucode_rev =
                kvm_arch_get_supported_msr_feature(kvm_state,
                                                   MSR_IA32_UCODE_REV);
        }
    }
    return host_cpu_realizefn(cs, errp);
}

static bool lmce_supported(void)
{
    uint64_t mce_cap = 0;

    if (kvm_ioctl(kvm_state, KVM_X86_GET_MCE_CAP_SUPPORTED, &mce_cap) < 0) {
        return false;
    }
    return !!(mce_cap & MCG_LMCE_P);
}

static void kvm_cpu_max_instance_init(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    KVMState *s = kvm_state;

    host_cpu_max_instance_init(cpu);

    if (lmce_supported()) {
        object_property_set_bool(OBJECT(cpu), "lmce", true, &error_abort);
    }

    env->cpuid_min_level =
        kvm_arch_get_supported_cpuid(s, 0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        kvm_arch_get_supported_cpuid(s, 0xC0000000, 0, R_EAX);
}

static void kvm_cpu_xsave_init(void)
{
    static bool first = true;
    uint32_t eax, ebx, ecx, edx;
    int i;

    if (!first) {
        return;
    }
    first = false;

    /* x87 and SSE states are in the legacy region of the XSAVE area. */
    x86_ext_save_areas[XSTATE_FP_BIT].offset = 0;
    x86_ext_save_areas[XSTATE_SSE_BIT].offset = 0;

    for (i = XSTATE_SSE_BIT + 1; i < XSAVE_STATE_AREA_COUNT; i++) {
        ExtSaveArea *esa = &x86_ext_save_areas[i];

        if (!esa->size) {
            continue;
        }
        if ((x86_cpu_get_supported_feature_word(esa->feature, false) & esa->bits)
            != esa->bits) {
            continue;
        }
        host_cpuid(0xd, i, &eax, &ebx, &ecx, &edx);
        if (eax != 0) {
            assert(esa->size == eax);
            esa->offset = ebx;
            esa->ecx = ecx;
        }
    }
}

/*
 * KVM-specific features that are automatically added/removed
 * from cpudef models when KVM is enabled.
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 *
 * NOTE: features can be enabled by default only if they were
 *       already available in the oldest kernel version supported
 *       by the KVM accelerator (see "OS requirements" section at
 *       docs/system/target-i386.rst)
 */
static PropValue kvm_default_props[] = {
    { "kvmclock", "on" },
    { "kvm-nopiodelay", "on" },
    { "kvm-asyncpf", "on" },
    { "kvm-steal-time", "on" },
    { "kvm-pv-eoi", "on" },
    { "kvmclock-stable-bit", "on" },
    { "x2apic", "on" },
    { "kvm-msi-ext-dest-id", "off" },
    { "acpi", "off" },
    { "monitor", "off" },
    { "svm", "off" },
    { NULL, NULL },
};

/*
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 */
void x86_cpu_change_kvm_default(const char *prop, const char *value)
{
    PropValue *pv;
    for (pv = kvm_default_props; pv->prop; pv++) {
        if (!strcmp(pv->prop, prop)) {
            pv->value = value;
            break;
        }
    }

    /*
     * It is valid to call this function only for properties that
     * are already present in the kvm_default_props table.
     */
    assert(pv->prop);
}

static void kvm_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    host_cpu_instance_init(cpu);

    if (xcc->model) {
        /* only applies to builtin_x86_defs cpus */
        if (!kvm_irqchip_in_kernel()) {
            x86_cpu_change_kvm_default("x2apic", "off");
        } else if (kvm_irqchip_is_split()) {
            x86_cpu_change_kvm_default("kvm-msi-ext-dest-id", "on");
        }

        /* Special cases not set in the X86CPUDefinition structs: */
        x86_cpu_apply_props(cpu, kvm_default_props);
    }

    if (cpu->max_features) {
        kvm_cpu_max_instance_init(cpu);
    }

    kvm_cpu_xsave_init();
}

static void kvm_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_target_realize = kvm_cpu_realizefn;
    acc->cpu_instance_init = kvm_cpu_instance_init;
}
static const TypeInfo kvm_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("kvm"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = kvm_cpu_accel_class_init,
    .abstract = true,
};
static void kvm_cpu_accel_register_types(void)
{
    type_register_static(&kvm_cpu_accel_type_info);
}
type_init(kvm_cpu_accel_register_types);
