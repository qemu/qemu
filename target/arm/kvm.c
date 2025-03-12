/*
 * ARM implementation of KVM hooks
 *
 * Copyright Christoffer Dall 2009-2010
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "system/system.h"
#include "system/runstate.h"
#include "system/kvm.h"
#include "system/kvm_int.h"
#include "kvm_arm.h"
#include "cpu.h"
#include "trace.h"
#include "internals.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "system/address-spaces.h"
#include "gdbstub/enums.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ghes.h"
#include "target/arm/gtimer.h"
#include "migration/blocker.h"

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_INFO(DEVICE_CTRL),
    KVM_CAP_LAST_INFO
};

static bool cap_has_mp_state;
static bool cap_has_inject_serror_esr;
static bool cap_has_inject_ext_dabt;

/**
 * ARMHostCPUFeatures: information about the host CPU (identified
 * by asking the host kernel)
 */
typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint32_t target;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

/**
 * kvm_arm_vcpu_init:
 * @cpu: ARMCPU
 *
 * Initialize (or reinitialize) the VCPU by invoking the
 * KVM_ARM_VCPU_INIT ioctl with the CPU type and feature
 * bitmask specified in the CPUState.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_arm_vcpu_init(ARMCPU *cpu)
{
    struct kvm_vcpu_init init;

    init.target = cpu->kvm_target;
    memcpy(init.features, cpu->kvm_init_features, sizeof(init.features));

    return kvm_vcpu_ioctl(CPU(cpu), KVM_ARM_VCPU_INIT, &init);
}

/**
 * kvm_arm_vcpu_finalize:
 * @cpu: ARMCPU
 * @feature: feature to finalize
 *
 * Finalizes the configuration of the specified VCPU feature by
 * invoking the KVM_ARM_VCPU_FINALIZE ioctl. Features requiring
 * this are documented in the "KVM_ARM_VCPU_FINALIZE" section of
 * KVM's API documentation.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_arm_vcpu_finalize(ARMCPU *cpu, int feature)
{
    return kvm_vcpu_ioctl(CPU(cpu), KVM_ARM_VCPU_FINALIZE, &feature);
}

bool kvm_arm_create_scratch_host_vcpu(const uint32_t *cpus_to_try,
                                      int *fdarray,
                                      struct kvm_vcpu_init *init)
{
    int ret = 0, kvmfd = -1, vmfd = -1, cpufd = -1;
    int max_vm_pa_size;

    kvmfd = qemu_open_old("/dev/kvm", O_RDWR);
    if (kvmfd < 0) {
        goto err;
    }
    max_vm_pa_size = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_ARM_VM_IPA_SIZE);
    if (max_vm_pa_size < 0) {
        max_vm_pa_size = 0;
    }
    do {
        vmfd = ioctl(kvmfd, KVM_CREATE_VM, max_vm_pa_size);
    } while (vmfd == -1 && errno == EINTR);
    if (vmfd < 0) {
        goto err;
    }

    /*
     * The MTE capability must be enabled by the VMM before creating
     * any VCPUs in order to allow the MTE bits of the ID_AA64PFR1
     * register to be probed correctly, as they are masked if MTE
     * is not enabled.
     */
    if (kvm_arm_mte_supported()) {
        KVMState kvm_state;

        kvm_state.fd = kvmfd;
        kvm_state.vmfd = vmfd;
        kvm_vm_enable_cap(&kvm_state, KVM_CAP_ARM_MTE, 0);
    }

    cpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
    if (cpufd < 0) {
        goto err;
    }

    if (!init) {
        /* Caller doesn't want the VCPU to be initialized, so skip it */
        goto finish;
    }

    if (init->target == -1) {
        struct kvm_vcpu_init preferred;

        ret = ioctl(vmfd, KVM_ARM_PREFERRED_TARGET, &preferred);
        if (!ret) {
            init->target = preferred.target;
        }
    }
    if (ret >= 0) {
        ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, init);
        if (ret < 0) {
            goto err;
        }
    } else if (cpus_to_try) {
        /* Old kernel which doesn't know about the
         * PREFERRED_TARGET ioctl: we know it will only support
         * creating one kind of guest CPU which is its preferred
         * CPU type.
         */
        struct kvm_vcpu_init try;

        while (*cpus_to_try != QEMU_KVM_ARM_TARGET_NONE) {
            try.target = *cpus_to_try++;
            memcpy(try.features, init->features, sizeof(init->features));
            ret = ioctl(cpufd, KVM_ARM_VCPU_INIT, &try);
            if (ret >= 0) {
                break;
            }
        }
        if (ret < 0) {
            goto err;
        }
        init->target = try.target;
    } else {
        /* Treat a NULL cpus_to_try argument the same as an empty
         * list, which means we will fail the call since this must
         * be an old kernel which doesn't support PREFERRED_TARGET.
         */
        goto err;
    }

finish:
    fdarray[0] = kvmfd;
    fdarray[1] = vmfd;
    fdarray[2] = cpufd;

    return true;

err:
    if (cpufd >= 0) {
        close(cpufd);
    }
    if (vmfd >= 0) {
        close(vmfd);
    }
    if (kvmfd >= 0) {
        close(kvmfd);
    }

    return false;
}

void kvm_arm_destroy_scratch_host_vcpu(int *fdarray)
{
    int i;

    for (i = 2; i >= 0; i--) {
        close(fdarray[i]);
    }
}

static int read_sys_reg32(int fd, uint32_t *pret, uint64_t id)
{
    uint64_t ret;
    struct kvm_one_reg idreg = { .id = id, .addr = (uintptr_t)&ret };
    int err;

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    err = ioctl(fd, KVM_GET_ONE_REG, &idreg);
    if (err < 0) {
        return -1;
    }
    *pret = ret;
    return 0;
}

static int read_sys_reg64(int fd, uint64_t *pret, uint64_t id)
{
    struct kvm_one_reg idreg = { .id = id, .addr = (uintptr_t)pret };

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64);
    return ioctl(fd, KVM_GET_ONE_REG, &idreg);
}

static bool kvm_arm_pauth_supported(void)
{
    return (kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_ADDRESS) &&
            kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_GENERIC));
}

static bool kvm_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    /* Identify the feature bits corresponding to the host CPU, and
     * fill out the ARMHostCPUClass fields accordingly. To do this
     * we have to create a scratch VM, create a single CPU inside it,
     * and then query that CPU for the relevant ID registers.
     */
    int fdarray[3];
    bool sve_supported;
    bool pmu_supported = false;
    uint64_t features = 0;
    int err;

    /* Old kernels may not know about the PREFERRED_TARGET ioctl: however
     * we know these will only support creating one kind of guest CPU,
     * which is its preferred CPU type. Fortunately these old kernels
     * support only a very limited number of CPUs.
     */
    static const uint32_t cpus_to_try[] = {
        KVM_ARM_TARGET_AEM_V8,
        KVM_ARM_TARGET_FOUNDATION_V8,
        KVM_ARM_TARGET_CORTEX_A57,
        QEMU_KVM_ARM_TARGET_NONE
    };
    /*
     * target = -1 informs kvm_arm_create_scratch_host_vcpu()
     * to use the preferred target
     */
    struct kvm_vcpu_init init = { .target = -1, };

    /*
     * Ask for SVE if supported, so that we can query ID_AA64ZFR0,
     * which is otherwise RAZ.
     */
    sve_supported = kvm_arm_sve_supported();
    if (sve_supported) {
        init.features[0] |= 1 << KVM_ARM_VCPU_SVE;
    }

    /*
     * Ask for Pointer Authentication if supported, so that we get
     * the unsanitized field values for AA64ISAR1_EL1.
     */
    if (kvm_arm_pauth_supported()) {
        init.features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS |
                             1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
    }

    if (kvm_arm_pmu_supported()) {
        init.features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
        pmu_supported = true;
        features |= 1ULL << ARM_FEATURE_PMU;
    }

    if (!kvm_arm_create_scratch_host_vcpu(cpus_to_try, fdarray, &init)) {
        return false;
    }

    ahcf->target = init.target;
    ahcf->dtb_compatible = "arm,arm-v8";

    err = read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64pfr0,
                         ARM64_SYS_REG(3, 0, 0, 4, 0));
    if (unlikely(err < 0)) {
        /*
         * Before v4.15, the kernel only exposed a limited number of system
         * registers, not including any of the interesting AArch64 ID regs.
         * For the most part we could leave these fields as zero with minimal
         * effect, since this does not affect the values seen by the guest.
         *
         * However, it could cause problems down the line for QEMU,
         * so provide a minimal v8.0 default.
         *
         * ??? Could read MIDR and use knowledge from cpu64.c.
         * ??? Could map a page of memory into our temp guest and
         *     run the tiniest of hand-crafted kernels to extract
         *     the values seen by the guest.
         * ??? Either of these sounds like too much effort just
         *     to work around running a modern host kernel.
         */
        ahcf->isar.id_aa64pfr0 = 0x00000011; /* EL1&0, AArch64 only */
        err = 0;
    } else {
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64pfr1,
                              ARM64_SYS_REG(3, 0, 0, 4, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64smfr0,
                              ARM64_SYS_REG(3, 0, 0, 4, 5));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64dfr0,
                              ARM64_SYS_REG(3, 0, 0, 5, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64dfr1,
                              ARM64_SYS_REG(3, 0, 0, 5, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64isar0,
                              ARM64_SYS_REG(3, 0, 0, 6, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64isar1,
                              ARM64_SYS_REG(3, 0, 0, 6, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64isar2,
                              ARM64_SYS_REG(3, 0, 0, 6, 2));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr0,
                              ARM64_SYS_REG(3, 0, 0, 7, 0));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr1,
                              ARM64_SYS_REG(3, 0, 0, 7, 1));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr2,
                              ARM64_SYS_REG(3, 0, 0, 7, 2));
        err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64mmfr3,
                              ARM64_SYS_REG(3, 0, 0, 7, 3));

        /*
         * Note that if AArch32 support is not present in the host,
         * the AArch32 sysregs are present to be read, but will
         * return UNKNOWN values.  This is neither better nor worse
         * than skipping the reads and leaving 0, as we must avoid
         * considering the values in every case.
         */
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr1,
                              ARM64_SYS_REG(3, 0, 0, 1, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_dfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr0,
                              ARM64_SYS_REG(3, 0, 0, 1, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr1,
                              ARM64_SYS_REG(3, 0, 0, 1, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr2,
                              ARM64_SYS_REG(3, 0, 0, 1, 6));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr3,
                              ARM64_SYS_REG(3, 0, 0, 1, 7));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar0,
                              ARM64_SYS_REG(3, 0, 0, 2, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar1,
                              ARM64_SYS_REG(3, 0, 0, 2, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar2,
                              ARM64_SYS_REG(3, 0, 0, 2, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar3,
                              ARM64_SYS_REG(3, 0, 0, 2, 3));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar4,
                              ARM64_SYS_REG(3, 0, 0, 2, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar5,
                              ARM64_SYS_REG(3, 0, 0, 2, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr4,
                              ARM64_SYS_REG(3, 0, 0, 2, 6));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar6,
                              ARM64_SYS_REG(3, 0, 0, 2, 7));

        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr0,
                              ARM64_SYS_REG(3, 0, 0, 3, 0));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr1,
                              ARM64_SYS_REG(3, 0, 0, 3, 1));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr2,
                              ARM64_SYS_REG(3, 0, 0, 3, 2));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_pfr2,
                              ARM64_SYS_REG(3, 0, 0, 3, 4));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_dfr1,
                              ARM64_SYS_REG(3, 0, 0, 3, 5));
        err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_mmfr5,
                              ARM64_SYS_REG(3, 0, 0, 3, 6));

        /*
         * DBGDIDR is a bit complicated because the kernel doesn't
         * provide an accessor for it in 64-bit mode, which is what this
         * scratch VM is in, and there's no architected "64-bit sysreg
         * which reads the same as the 32-bit register" the way there is
         * for other ID registers. Instead we synthesize a value from the
         * AArch64 ID_AA64DFR0, the same way the kernel code in
         * arch/arm64/kvm/sys_regs.c:trap_dbgidr() does.
         * We only do this if the CPU supports AArch32 at EL1.
         */
        if (FIELD_EX32(ahcf->isar.id_aa64pfr0, ID_AA64PFR0, EL1) >= 2) {
            int wrps = FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, WRPS);
            int brps = FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, BRPS);
            int ctx_cmps =
                FIELD_EX64(ahcf->isar.id_aa64dfr0, ID_AA64DFR0, CTX_CMPS);
            int version = 6; /* ARMv8 debug architecture */
            bool has_el3 =
                !!FIELD_EX32(ahcf->isar.id_aa64pfr0, ID_AA64PFR0, EL3);
            uint32_t dbgdidr = 0;

            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, WRPS, wrps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, BRPS, brps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, CTX_CMPS, ctx_cmps);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, VERSION, version);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, NSUHD_IMP, has_el3);
            dbgdidr = FIELD_DP32(dbgdidr, DBGDIDR, SE_IMP, has_el3);
            dbgdidr |= (1 << 15); /* RES1 bit */
            ahcf->isar.dbgdidr = dbgdidr;
        }

        if (pmu_supported) {
            /* PMCR_EL0 is only accessible if the vCPU has feature PMU_V3 */
            err |= read_sys_reg64(fdarray[2], &ahcf->isar.reset_pmcr_el0,
                                  ARM64_SYS_REG(3, 3, 9, 12, 0));
        }

        if (sve_supported) {
            /*
             * There is a range of kernels between kernel commit 73433762fcae
             * and f81cb2c3ad41 which have a bug where the kernel doesn't
             * expose SYS_ID_AA64ZFR0_EL1 via the ONE_REG API unless the VM has
             * enabled SVE support, which resulted in an error rather than RAZ.
             * So only read the register if we set KVM_ARM_VCPU_SVE above.
             */
            err |= read_sys_reg64(fdarray[2], &ahcf->isar.id_aa64zfr0,
                                  ARM64_SYS_REG(3, 0, 0, 4, 4));
        }
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);

    if (err < 0) {
        return false;
    }

    /*
     * We can assume any KVM supporting CPU is at least a v8
     * with VFPv4+Neon; this in turn implies most of the other
     * feature bits.
     */
    features |= 1ULL << ARM_FEATURE_V8;
    features |= 1ULL << ARM_FEATURE_NEON;
    features |= 1ULL << ARM_FEATURE_AARCH64;
    features |= 1ULL << ARM_FEATURE_GENERIC_TIMER;

    ahcf->features = features;

    return true;
}

void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (!arm_host_cpu_features.dtb_compatible) {
        if (!kvm_enabled() ||
            !kvm_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /* We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->kvm_target = QEMU_KVM_ARM_TARGET_NONE;
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->kvm_target = arm_host_cpu_features.target;
    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    env->features = arm_host_cpu_features.features;
}

static bool kvm_no_adjvtime_get(Object *obj, Error **errp)
{
    return !ARM_CPU(obj)->kvm_adjvtime;
}

static void kvm_no_adjvtime_set(Object *obj, bool value, Error **errp)
{
    ARM_CPU(obj)->kvm_adjvtime = !value;
}

static bool kvm_steal_time_get(Object *obj, Error **errp)
{
    return ARM_CPU(obj)->kvm_steal_time != ON_OFF_AUTO_OFF;
}

static void kvm_steal_time_set(Object *obj, bool value, Error **errp)
{
    ARM_CPU(obj)->kvm_steal_time = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
}

/* KVM VCPU properties should be prefixed with "kvm-". */
void kvm_arm_add_vcpu_properties(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    Object *obj = OBJECT(cpu);

    if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
        cpu->kvm_adjvtime = true;
        object_property_add_bool(obj, "kvm-no-adjvtime", kvm_no_adjvtime_get,
                                 kvm_no_adjvtime_set);
        object_property_set_description(obj, "kvm-no-adjvtime",
                                        "Set on to disable the adjustment of "
                                        "the virtual counter. VM stopped time "
                                        "will be counted.");
    }

    cpu->kvm_steal_time = ON_OFF_AUTO_AUTO;
    object_property_add_bool(obj, "kvm-steal-time", kvm_steal_time_get,
                             kvm_steal_time_set);
    object_property_set_description(obj, "kvm-steal-time",
                                    "Set off to disable KVM steal time.");
}

bool kvm_arm_pmu_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_PMU_V3);
}

int kvm_arm_get_max_vm_ipa_size(MachineState *ms, bool *fixed_ipa)
{
    KVMState *s = KVM_STATE(ms->accelerator);
    int ret;

    ret = kvm_check_extension(s, KVM_CAP_ARM_VM_IPA_SIZE);
    *fixed_ipa = ret <= 0;

    return ret > 0 ? ret : 40;
}

int kvm_arch_get_default_type(MachineState *ms)
{
    bool fixed_ipa;
    int size = kvm_arm_get_max_vm_ipa_size(ms, &fixed_ipa);
    return fixed_ipa ? 0 : size;
}

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    int ret = 0;
    /* For ARM interrupt delivery is always asynchronous,
     * whether we are using an in-kernel VGIC or not.
     */
    kvm_async_interrupts_allowed = true;

    /*
     * PSCI wakes up secondary cores, so we always need to
     * have vCPUs waiting in kernel space
     */
    kvm_halt_in_kernel_allowed = true;

    cap_has_mp_state = kvm_check_extension(s, KVM_CAP_MP_STATE);

    /* Check whether user space can specify guest syndrome value */
    cap_has_inject_serror_esr =
        kvm_check_extension(s, KVM_CAP_ARM_INJECT_SERROR_ESR);

    if (ms->smp.cpus > 256 &&
        !kvm_check_extension(s, KVM_CAP_ARM_IRQ_LINE_LAYOUT_2)) {
        error_report("Using more than 256 vcpus requires a host kernel "
                     "with KVM_CAP_ARM_IRQ_LINE_LAYOUT_2");
        ret = -EINVAL;
    }

    if (kvm_check_extension(s, KVM_CAP_ARM_NISV_TO_USER)) {
        if (kvm_vm_enable_cap(s, KVM_CAP_ARM_NISV_TO_USER, 0)) {
            error_report("Failed to enable KVM_CAP_ARM_NISV_TO_USER cap");
        } else {
            /* Set status for supporting the external dabt injection */
            cap_has_inject_ext_dabt = kvm_check_extension(s,
                                    KVM_CAP_ARM_INJECT_EXT_DABT);
        }
    }

    if (s->kvm_eager_split_size) {
        uint32_t sizes;

        sizes = kvm_vm_check_extension(s, KVM_CAP_ARM_SUPPORTED_BLOCK_SIZES);
        if (!sizes) {
            s->kvm_eager_split_size = 0;
            warn_report("Eager Page Split support not available");
        } else if (!(s->kvm_eager_split_size & sizes)) {
            error_report("Eager Page Split requested chunk size not valid");
            ret = -EINVAL;
        } else {
            ret = kvm_vm_enable_cap(s, KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE, 0,
                                    s->kvm_eager_split_size);
            if (ret < 0) {
                error_report("Enabling of Eager Page Split failed: %s",
                             strerror(-ret));
            }
        }
    }

    max_hw_wps = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_WPS);
    hw_watchpoints = g_array_sized_new(true, true,
                                       sizeof(HWWatchpoint), max_hw_wps);

    max_hw_bps = kvm_check_extension(s, KVM_CAP_GUEST_DEBUG_HW_BPS);
    hw_breakpoints = g_array_sized_new(true, true,
                                       sizeof(HWBreakpoint), max_hw_bps);

    return ret;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

/* We track all the KVM devices which need their memory addresses
 * passing to the kernel in a list of these structures.
 * When board init is complete we run through the list and
 * tell the kernel the base addresses of the memory regions.
 * We use a MemoryListener to track mapping and unmapping of
 * the regions during board creation, so the board models don't
 * need to do anything special for the KVM case.
 *
 * Sometimes the address must be OR'ed with some other fields
 * (for example for KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION).
 * @kda_addr_ormask aims at storing the value of those fields.
 */
typedef struct KVMDevice {
    struct kvm_arm_device_addr kda;
    struct kvm_device_attr kdattr;
    uint64_t kda_addr_ormask;
    MemoryRegion *mr;
    QSLIST_ENTRY(KVMDevice) entries;
    int dev_fd;
} KVMDevice;

static QSLIST_HEAD(, KVMDevice) kvm_devices_head;

static void kvm_arm_devlistener_add(MemoryListener *listener,
                                    MemoryRegionSection *section)
{
    KVMDevice *kd;

    QSLIST_FOREACH(kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) {
            kd->kda.addr = section->offset_within_address_space;
        }
    }
}

static void kvm_arm_devlistener_del(MemoryListener *listener,
                                    MemoryRegionSection *section)
{
    KVMDevice *kd;

    QSLIST_FOREACH(kd, &kvm_devices_head, entries) {
        if (section->mr == kd->mr) {
            kd->kda.addr = -1;
        }
    }
}

static MemoryListener devlistener = {
    .name = "kvm-arm",
    .region_add = kvm_arm_devlistener_add,
    .region_del = kvm_arm_devlistener_del,
    .priority = MEMORY_LISTENER_PRIORITY_MIN,
};

static void kvm_arm_set_device_addr(KVMDevice *kd)
{
    struct kvm_device_attr *attr = &kd->kdattr;
    int ret;
    uint64_t addr = kd->kda.addr;

    addr |= kd->kda_addr_ormask;
    attr->addr = (uintptr_t)&addr;
    ret = kvm_device_ioctl(kd->dev_fd, KVM_SET_DEVICE_ATTR, attr);

    if (ret < 0) {
        fprintf(stderr, "Failed to set device address: %s\n",
                strerror(-ret));
        abort();
    }
}

static void kvm_arm_machine_init_done(Notifier *notifier, void *data)
{
    KVMDevice *kd, *tkd;

    QSLIST_FOREACH_SAFE(kd, &kvm_devices_head, entries, tkd) {
        if (kd->kda.addr != -1) {
            kvm_arm_set_device_addr(kd);
        }
        memory_region_unref(kd->mr);
        QSLIST_REMOVE_HEAD(&kvm_devices_head, entries);
        g_free(kd);
    }
    memory_listener_unregister(&devlistener);
}

static Notifier notify = {
    .notify = kvm_arm_machine_init_done,
};

void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd, uint64_t addr_ormask)
{
    KVMDevice *kd;

    if (!kvm_irqchip_in_kernel()) {
        return;
    }

    if (QSLIST_EMPTY(&kvm_devices_head)) {
        memory_listener_register(&devlistener, &address_space_memory);
        qemu_add_machine_init_done_notifier(&notify);
    }
    kd = g_new0(KVMDevice, 1);
    kd->mr = mr;
    kd->kda.id = devid;
    kd->kda.addr = -1;
    kd->kdattr.flags = 0;
    kd->kdattr.group = group;
    kd->kdattr.attr = attr;
    kd->dev_fd = dev_fd;
    kd->kda_addr_ormask = addr_ormask;
    QSLIST_INSERT_HEAD(&kvm_devices_head, kd, entries);
    memory_region_ref(kd->mr);
}

static int compare_u64(const void *a, const void *b)
{
    if (*(uint64_t *)a > *(uint64_t *)b) {
        return 1;
    }
    if (*(uint64_t *)a < *(uint64_t *)b) {
        return -1;
    }
    return 0;
}

/*
 * cpreg_values are sorted in ascending order by KVM register ID
 * (see kvm_arm_init_cpreg_list). This allows us to cheaply find
 * the storage for a KVM register by ID with a binary search.
 */
static uint64_t *kvm_arm_get_cpreg_ptr(ARMCPU *cpu, uint64_t regidx)
{
    uint64_t *res;

    res = bsearch(&regidx, cpu->cpreg_indexes, cpu->cpreg_array_len,
                  sizeof(uint64_t), compare_u64);
    assert(res);

    return &cpu->cpreg_values[res - cpu->cpreg_indexes];
}

/**
 * kvm_arm_reg_syncs_via_cpreg_list:
 * @regidx: KVM register index
 *
 * Return true if this KVM register should be synchronized via the
 * cpreg list of arbitrary system registers, false if it is synchronized
 * by hand using code in kvm_arch_get/put_registers().
 */
static bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx)
{
    switch (regidx & KVM_REG_ARM_COPROC_MASK) {
    case KVM_REG_ARM_CORE:
    case KVM_REG_ARM64_SVE:
        return false;
    default:
        return true;
    }
}

/**
 * kvm_arm_init_cpreg_list:
 * @cpu: ARMCPU
 *
 * Initialize the ARMCPU cpreg list according to the kernel's
 * definition of what CPU registers it knows about (and throw away
 * the previous TCG-created cpreg list).
 *
 * Returns: 0 if success, else < 0 error code
 */
static int kvm_arm_init_cpreg_list(ARMCPU *cpu)
{
    struct kvm_reg_list rl;
    struct kvm_reg_list *rlp;
    int i, ret, arraylen;
    CPUState *cs = CPU(cpu);

    rl.n = 0;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REG_LIST, &rl);
    if (ret != -E2BIG) {
        return ret;
    }
    rlp = g_malloc(sizeof(struct kvm_reg_list) + rl.n * sizeof(uint64_t));
    rlp->n = rl.n;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_REG_LIST, rlp);
    if (ret) {
        goto out;
    }
    /* Sort the list we get back from the kernel, since cpreg_tuples
     * must be in strictly ascending order.
     */
    qsort(&rlp->reg, rlp->n, sizeof(rlp->reg[0]), compare_u64);

    for (i = 0, arraylen = 0; i < rlp->n; i++) {
        if (!kvm_arm_reg_syncs_via_cpreg_list(rlp->reg[i])) {
            continue;
        }
        switch (rlp->reg[i] & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
        case KVM_REG_SIZE_U64:
            break;
        default:
            fprintf(stderr, "Can't handle size of register in kernel list\n");
            ret = -EINVAL;
            goto out;
        }

        arraylen++;
    }

    cpu->cpreg_indexes = g_renew(uint64_t, cpu->cpreg_indexes, arraylen);
    cpu->cpreg_values = g_renew(uint64_t, cpu->cpreg_values, arraylen);
    cpu->cpreg_vmstate_indexes = g_renew(uint64_t, cpu->cpreg_vmstate_indexes,
                                         arraylen);
    cpu->cpreg_vmstate_values = g_renew(uint64_t, cpu->cpreg_vmstate_values,
                                        arraylen);
    cpu->cpreg_array_len = arraylen;
    cpu->cpreg_vmstate_array_len = arraylen;

    for (i = 0, arraylen = 0; i < rlp->n; i++) {
        uint64_t regidx = rlp->reg[i];
        if (!kvm_arm_reg_syncs_via_cpreg_list(regidx)) {
            continue;
        }
        cpu->cpreg_indexes[arraylen] = regidx;
        arraylen++;
    }
    assert(cpu->cpreg_array_len == arraylen);

    if (!write_kvmstate_to_list(cpu)) {
        /* Shouldn't happen unless kernel is inconsistent about
         * what registers exist.
         */
        fprintf(stderr, "Initial read of kernel register state failed\n");
        ret = -EINVAL;
        goto out;
    }

out:
    g_free(rlp);
    return ret;
}

/**
 * kvm_arm_cpreg_level:
 * @regidx: KVM register index
 *
 * Return the level of this coprocessor/system register.  Return value is
 * either KVM_PUT_RUNTIME_STATE, KVM_PUT_RESET_STATE, or KVM_PUT_FULL_STATE.
 */
static int kvm_arm_cpreg_level(uint64_t regidx)
{
    /*
     * All system registers are assumed to be level KVM_PUT_RUNTIME_STATE.
     * If a register should be written less often, you must add it here
     * with a state of either KVM_PUT_RESET_STATE or KVM_PUT_FULL_STATE.
     */
    switch (regidx) {
    case KVM_REG_ARM_TIMER_CNT:
    case KVM_REG_ARM_PTIMER_CNT:
        return KVM_PUT_FULL_STATE;
    }
    return KVM_PUT_RUNTIME_STATE;
}

bool write_kvmstate_to_list(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            ret = kvm_get_one_reg(cs, regidx, &v32);
            if (!ret) {
                cpu->cpreg_values[i] = v32;
            }
            break;
        case KVM_REG_SIZE_U64:
            ret = kvm_get_one_reg(cs, regidx, cpu->cpreg_values + i);
            break;
        default:
            g_assert_not_reached();
        }
        if (ret) {
            ok = false;
        }
    }
    return ok;
}

bool write_list_to_kvmstate(ARMCPU *cpu, int level)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        if (kvm_arm_cpreg_level(regidx) > level) {
            continue;
        }

        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            v32 = cpu->cpreg_values[i];
            ret = kvm_set_one_reg(cs, regidx, &v32);
            break;
        case KVM_REG_SIZE_U64:
            ret = kvm_set_one_reg(cs, regidx, cpu->cpreg_values + i);
            break;
        default:
            g_assert_not_reached();
        }
        if (ret) {
            /* We might fail for "unknown register" and also for
             * "you tried to set a register which is constant with
             * a different value from what it actually contains".
             */
            ok = false;
        }
    }
    return ok;
}

void kvm_arm_cpu_pre_save(ARMCPU *cpu)
{
    /* KVM virtual time adjustment */
    if (cpu->kvm_vtime_dirty) {
        *kvm_arm_get_cpreg_ptr(cpu, KVM_REG_ARM_TIMER_CNT) = cpu->kvm_vtime;
    }
}

void kvm_arm_cpu_post_load(ARMCPU *cpu)
{
    /* KVM virtual time adjustment */
    if (cpu->kvm_adjvtime) {
        cpu->kvm_vtime = *kvm_arm_get_cpreg_ptr(cpu, KVM_REG_ARM_TIMER_CNT);
        cpu->kvm_vtime_dirty = true;
    }
}

void kvm_arm_reset_vcpu(ARMCPU *cpu)
{
    int ret;

    /* Re-init VCPU so that all registers are set to
     * their respective reset values.
     */
    ret = kvm_arm_vcpu_init(cpu);
    if (ret < 0) {
        fprintf(stderr, "kvm_arm_vcpu_init failed: %s\n", strerror(-ret));
        abort();
    }
    if (!write_kvmstate_to_list(cpu)) {
        fprintf(stderr, "write_kvmstate_to_list failed\n");
        abort();
    }
    /*
     * Sync the reset values also into the CPUState. This is necessary
     * because the next thing we do will be a kvm_arch_put_registers()
     * which will update the list values from the CPUState before copying
     * the list values back to KVM. It's OK to ignore failure returns here
     * for the same reason we do so in kvm_arch_get_registers().
     */
    write_list_to_cpustate(cpu);
}

/*
 * Update KVM's MP_STATE based on what QEMU thinks it is
 */
static int kvm_arm_sync_mpstate_to_kvm(ARMCPU *cpu)
{
    if (cap_has_mp_state) {
        struct kvm_mp_state mp_state = {
            .mp_state = (cpu->power_state == PSCI_OFF) ?
            KVM_MP_STATE_STOPPED : KVM_MP_STATE_RUNNABLE
        };
        return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_MP_STATE, &mp_state);
    }
    return 0;
}

/*
 * Sync the KVM MP_STATE into QEMU
 */
static int kvm_arm_sync_mpstate_to_qemu(ARMCPU *cpu)
{
    if (cap_has_mp_state) {
        struct kvm_mp_state mp_state;
        int ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_MP_STATE, &mp_state);
        if (ret) {
            return ret;
        }
        cpu->power_state = (mp_state.mp_state == KVM_MP_STATE_STOPPED) ?
            PSCI_OFF : PSCI_ON;
    }
    return 0;
}

/**
 * kvm_arm_get_virtual_time:
 * @cpu: ARMCPU
 *
 * Gets the VCPU's virtual counter and stores it in the KVM CPU state.
 */
static void kvm_arm_get_virtual_time(ARMCPU *cpu)
{
    int ret;

    if (cpu->kvm_vtime_dirty) {
        return;
    }

    ret = kvm_get_one_reg(CPU(cpu), KVM_REG_ARM_TIMER_CNT, &cpu->kvm_vtime);
    if (ret) {
        error_report("Failed to get KVM_REG_ARM_TIMER_CNT");
        abort();
    }

    cpu->kvm_vtime_dirty = true;
}

/**
 * kvm_arm_put_virtual_time:
 * @cpu: ARMCPU
 *
 * Sets the VCPU's virtual counter to the value stored in the KVM CPU state.
 */
static void kvm_arm_put_virtual_time(ARMCPU *cpu)
{
    int ret;

    if (!cpu->kvm_vtime_dirty) {
        return;
    }

    ret = kvm_set_one_reg(CPU(cpu), KVM_REG_ARM_TIMER_CNT, &cpu->kvm_vtime);
    if (ret) {
        error_report("Failed to set KVM_REG_ARM_TIMER_CNT");
        abort();
    }

    cpu->kvm_vtime_dirty = false;
}

/**
 * kvm_put_vcpu_events:
 * @cpu: ARMCPU
 *
 * Put VCPU related state to kvm.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_put_vcpu_events(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    struct kvm_vcpu_events events;
    int ret;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    memset(&events, 0, sizeof(events));
    events.exception.serror_pending = env->serror.pending;

    /* Inject SError to guest with specified syndrome if host kernel
     * supports it, otherwise inject SError without syndrome.
     */
    if (cap_has_inject_serror_esr) {
        events.exception.serror_has_esr = env->serror.has_esr;
        events.exception.serror_esr = env->serror.esr;
    }

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_VCPU_EVENTS, &events);
    if (ret) {
        error_report("failed to put vcpu events");
    }

    return ret;
}

/**
 * kvm_get_vcpu_events:
 * @cpu: ARMCPU
 *
 * Get VCPU related state from kvm.
 *
 * Returns: 0 if success else < 0 error code
 */
static int kvm_get_vcpu_events(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    struct kvm_vcpu_events events;
    int ret;

    if (!kvm_has_vcpu_events()) {
        return 0;
    }

    memset(&events, 0, sizeof(events));
    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_VCPU_EVENTS, &events);
    if (ret) {
        error_report("failed to get vcpu events");
        return ret;
    }

    env->serror.pending = events.exception.serror_pending;
    env->serror.has_esr = events.exception.serror_has_esr;
    env->serror.esr = events.exception.serror_esr;

    return 0;
}

#define ARM64_REG_ESR_EL1 ARM64_SYS_REG(3, 0, 5, 2, 0)
#define ARM64_REG_TCR_EL1 ARM64_SYS_REG(3, 0, 2, 0, 2)

/*
 * ESR_EL1
 * ISS encoding
 * AARCH64: DFSC,   bits [5:0]
 * AARCH32:
 *      TTBCR.EAE == 0
 *          FS[4]   - DFSR[10]
 *          FS[3:0] - DFSR[3:0]
 *      TTBCR.EAE == 1
 *          FS, bits [5:0]
 */
#define ESR_DFSC(aarch64, lpae, v)        \
    ((aarch64 || (lpae)) ? ((v) & 0x3F)   \
               : (((v) >> 6) | ((v) & 0x1F)))

#define ESR_DFSC_EXTABT(aarch64, lpae) \
    ((aarch64) ? 0x10 : (lpae) ? 0x10 : 0x8)

/**
 * kvm_arm_verify_ext_dabt_pending:
 * @cpu: ARMCPU
 *
 * Verify the fault status code wrt the Ext DABT injection
 *
 * Returns: true if the fault status code is as expected, false otherwise
 */
static bool kvm_arm_verify_ext_dabt_pending(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    uint64_t dfsr_val;

    if (!kvm_get_one_reg(cs, ARM64_REG_ESR_EL1, &dfsr_val)) {
        CPUARMState *env = &cpu->env;
        int aarch64_mode = arm_feature(env, ARM_FEATURE_AARCH64);
        int lpae = 0;

        if (!aarch64_mode) {
            uint64_t ttbcr;

            if (!kvm_get_one_reg(cs, ARM64_REG_TCR_EL1, &ttbcr)) {
                lpae = arm_feature(env, ARM_FEATURE_LPAE)
                        && (ttbcr & TTBCR_EAE);
            }
        }
        /*
         * The verification here is based on the DFSC bits
         * of the ESR_EL1 reg only
         */
         return (ESR_DFSC(aarch64_mode, lpae, dfsr_val) ==
                ESR_DFSC_EXTABT(aarch64_mode, lpae));
    }
    return false;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (unlikely(env->ext_dabt_raised)) {
        /*
         * Verifying that the ext DABT has been properly injected,
         * otherwise risking indefinitely re-running the faulting instruction
         * Covering a very narrow case for kernels 5.5..5.5.4
         * when injected abort was misconfigured to be
         * an IMPLEMENTATION DEFINED exception (for 32-bit EL1)
         */
        if (!arm_feature(env, ARM_FEATURE_AARCH64) &&
            unlikely(!kvm_arm_verify_ext_dabt_pending(cpu))) {

            error_report("Data abort exception with no valid ISS generated by "
                   "guest memory access. KVM unable to emulate faulting "
                   "instruction. Failed to inject an external data abort "
                   "into the guest.");
            abort();
       }
       /* Clear the status */
       env->ext_dabt_raised = 0;
    }
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    ARMCPU *cpu;
    uint32_t switched_level;

    if (kvm_irqchip_in_kernel()) {
        /*
         * We only need to sync timer states with user-space interrupt
         * controllers, so return early and save cycles if we don't.
         */
        return MEMTXATTRS_UNSPECIFIED;
    }

    cpu = ARM_CPU(cs);

    /* Synchronize our shadowed in-kernel device irq lines with the kvm ones */
    if (run->s.regs.device_irq_level != cpu->device_irq_level) {
        switched_level = cpu->device_irq_level ^ run->s.regs.device_irq_level;

        bql_lock();

        if (switched_level & KVM_ARM_DEV_EL1_VTIMER) {
            qemu_set_irq(cpu->gt_timer_outputs[GTIMER_VIRT],
                         !!(run->s.regs.device_irq_level &
                            KVM_ARM_DEV_EL1_VTIMER));
            switched_level &= ~KVM_ARM_DEV_EL1_VTIMER;
        }

        if (switched_level & KVM_ARM_DEV_EL1_PTIMER) {
            qemu_set_irq(cpu->gt_timer_outputs[GTIMER_PHYS],
                         !!(run->s.regs.device_irq_level &
                            KVM_ARM_DEV_EL1_PTIMER));
            switched_level &= ~KVM_ARM_DEV_EL1_PTIMER;
        }

        if (switched_level & KVM_ARM_DEV_PMU) {
            qemu_set_irq(cpu->pmu_interrupt,
                         !!(run->s.regs.device_irq_level & KVM_ARM_DEV_PMU));
            switched_level &= ~KVM_ARM_DEV_PMU;
        }

        if (switched_level) {
            qemu_log_mask(LOG_UNIMP, "%s: unhandled in-kernel device IRQ %x\n",
                          __func__, switched_level);
        }

        /* We also mark unknown levels as processed to not waste cycles */
        cpu->device_irq_level = run->s.regs.device_irq_level;
        bql_unlock();
    }

    return MEMTXATTRS_UNSPECIFIED;
}

static void kvm_arm_vm_state_change(void *opaque, bool running, RunState state)
{
    ARMCPU *cpu = opaque;

    if (running) {
        if (cpu->kvm_adjvtime) {
            kvm_arm_put_virtual_time(cpu);
        }
    } else {
        if (cpu->kvm_adjvtime) {
            kvm_arm_get_virtual_time(cpu);
        }
    }
}

/**
 * kvm_arm_handle_dabt_nisv:
 * @cpu: ARMCPU
 * @esr_iss: ISS encoding (limited) for the exception from Data Abort
 *           ISV bit set to '0b0' -> no valid instruction syndrome
 * @fault_ipa: faulting address for the synchronous data abort
 *
 * Returns: 0 if the exception has been handled, < 0 otherwise
 */
static int kvm_arm_handle_dabt_nisv(ARMCPU *cpu, uint64_t esr_iss,
                                    uint64_t fault_ipa)
{
    CPUARMState *env = &cpu->env;
    /*
     * Request KVM to inject the external data abort into the guest
     */
    if (cap_has_inject_ext_dabt) {
        struct kvm_vcpu_events events = { };
        /*
         * The external data abort event will be handled immediately by KVM
         * using the address fault that triggered the exit on given VCPU.
         * Requesting injection of the external data abort does not rely
         * on any other VCPU state. Therefore, in this particular case, the VCPU
         * synchronization can be exceptionally skipped.
         */
        events.exception.ext_dabt_pending = 1;
        /* KVM_CAP_ARM_INJECT_EXT_DABT implies KVM_CAP_VCPU_EVENTS */
        if (!kvm_vcpu_ioctl(CPU(cpu), KVM_SET_VCPU_EVENTS, &events)) {
            env->ext_dabt_raised = 1;
            return 0;
        }
    } else {
        error_report("Data abort exception triggered by guest memory access "
                     "at physical address: 0x"  TARGET_FMT_lx,
                     (target_ulong)fault_ipa);
        error_printf("KVM unable to emulate faulting instruction.\n");
    }
    return -1;
}

/**
 * kvm_arm_handle_debug:
 * @cpu: ARMCPU
 * @debug_exit: debug part of the KVM exit structure
 *
 * Returns: TRUE if the debug exception was handled.
 *
 * See v8 ARM ARM D7.2.27 ESR_ELx, Exception Syndrome Register
 *
 * To minimise translating between kernel and user-space the kernel
 * ABI just provides user-space with the full exception syndrome
 * register value to be decoded in QEMU.
 */
static bool kvm_arm_handle_debug(ARMCPU *cpu,
                                 struct kvm_debug_exit_arch *debug_exit)
{
    int hsr_ec = syn_get_ec(debug_exit->hsr);
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    /* Ensure PC is synchronised */
    kvm_cpu_synchronize_state(cs);

    switch (hsr_ec) {
    case EC_SOFTWARESTEP:
        if (cs->singlestep_enabled) {
            return true;
        } else {
            /*
             * The kernel should have suppressed the guest's ability to
             * single step at this point so something has gone wrong.
             */
            error_report("%s: guest single-step while debugging unsupported"
                         " (%"PRIx64", %"PRIx32")",
                         __func__, env->pc, debug_exit->hsr);
            return false;
        }
        break;
    case EC_AA64_BKPT:
        if (kvm_find_sw_breakpoint(cs, env->pc)) {
            return true;
        }
        break;
    case EC_BREAKPOINT:
        if (find_hw_breakpoint(cs, env->pc)) {
            return true;
        }
        break;
    case EC_WATCHPOINT:
    {
        CPUWatchpoint *wp = find_hw_watchpoint(cs, debug_exit->far);
        if (wp) {
            cs->watchpoint_hit = wp;
            return true;
        }
        break;
    }
    default:
        error_report("%s: unhandled debug exit (%"PRIx32", %"PRIx64")",
                     __func__, debug_exit->hsr, env->pc);
    }

    /* If we are not handling the debug exception it must belong to
     * the guest. Let's re-use the existing TCG interrupt code to set
     * everything up properly.
     */
    cs->exception_index = EXCP_BKPT;
    env->exception.syndrome = debug_exit->hsr;
    env->exception.vaddress = debug_exit->far;
    env->exception.target_el = 1;
    bql_lock();
    arm_cpu_do_interrupt(cs);
    bql_unlock();

    return false;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    ARMCPU *cpu = ARM_CPU(cs);
    int ret = 0;

    switch (run->exit_reason) {
    case KVM_EXIT_DEBUG:
        if (kvm_arm_handle_debug(cpu, &run->debug.arch)) {
            ret = EXCP_DEBUG;
        } /* otherwise return to guest */
        break;
    case KVM_EXIT_ARM_NISV:
        /* External DABT with no valid iss to decode */
        ret = kvm_arm_handle_dabt_nisv(cpu, run->arm_nisv.esr_iss,
                                       run->arm_nisv.fault_ipa);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: un-handled exit reason %d\n",
                      __func__, run->exit_reason);
        break;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return 0;
}

/**
 * kvm_arm_hw_debug_active:
 * @cpu: ARMCPU
 *
 * Return: TRUE if any hardware breakpoints in use.
 */
static bool kvm_arm_hw_debug_active(ARMCPU *cpu)
{
    return ((cur_hw_wps > 0) || (cur_hw_bps > 0));
}

/**
 * kvm_arm_copy_hw_debug_data:
 * @ptr: kvm_guest_debug_arch structure
 *
 * Copy the architecture specific debug registers into the
 * kvm_guest_debug ioctl structure.
 */
static void kvm_arm_copy_hw_debug_data(struct kvm_guest_debug_arch *ptr)
{
    int i;
    memset(ptr, 0, sizeof(struct kvm_guest_debug_arch));

    for (i = 0; i < max_hw_wps; i++) {
        HWWatchpoint *wp = get_hw_wp(i);
        ptr->dbg_wcr[i] = wp->wcr;
        ptr->dbg_wvr[i] = wp->wvr;
    }
    for (i = 0; i < max_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        ptr->dbg_bcr[i] = bp->bcr;
        ptr->dbg_bvr[i] = bp->bvr;
    }
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    if (kvm_sw_breakpoints_active(cs)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
    }
    if (kvm_arm_hw_debug_active(ARM_CPU(cs))) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW;
        kvm_arm_copy_hw_debug_data(&dbg->arch);
    }
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

int kvm_arch_irqchip_create(KVMState *s)
{
    if (kvm_kernel_irqchip_split()) {
        error_report("-machine kernel_irqchip=split is not supported on ARM.");
        exit(1);
    }

    /* If we can create the VGIC using the newer device control API, we
     * let the device do this when it initializes itself, otherwise we
     * fall back to the old API */
    return kvm_check_extension(s, KVM_CAP_DEVICE_CTRL);
}

int kvm_arm_vgic_probe(void)
{
    int val = 0;

    if (kvm_create_device(kvm_state,
                          KVM_DEV_TYPE_ARM_VGIC_V3, true) == 0) {
        val |= KVM_ARM_VGIC_V3;
    }
    if (kvm_create_device(kvm_state,
                          KVM_DEV_TYPE_ARM_VGIC_V2, true) == 0) {
        val |= KVM_ARM_VGIC_V2;
    }
    return val;
}

int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level)
{
    int kvm_irq = (irqtype << KVM_ARM_IRQ_TYPE_SHIFT) | irq;
    int cpu_idx1 = cpu % 256;
    int cpu_idx2 = cpu / 256;

    kvm_irq |= (cpu_idx1 << KVM_ARM_IRQ_VCPU_SHIFT) |
               (cpu_idx2 << KVM_ARM_IRQ_VCPU2_SHIFT);

    return kvm_set_irq(kvm_state, kvm_irq, !!level);
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    AddressSpace *as = pci_device_iommu_address_space(dev);
    hwaddr xlat, len, doorbell_gpa;
    MemoryRegionSection mrs;
    MemoryRegion *mr;

    if (as == &address_space_memory) {
        return 0;
    }

    /* MSI doorbell address is translated by an IOMMU */

    RCU_READ_LOCK_GUARD();

    mr = address_space_translate(as, address, &xlat, &len, true,
                                 MEMTXATTRS_UNSPECIFIED);

    if (!mr) {
        return 1;
    }

    mrs = memory_region_find(mr, xlat, 1);

    if (!mrs.mr) {
        return 1;
    }

    doorbell_gpa = mrs.offset_within_address_space;
    memory_region_unref(mrs.mr);

    route->u.msi.address_lo = doorbell_gpa;
    route->u.msi.address_hi = doorbell_gpa >> 32;

    trace_kvm_arm_fixup_msi_route(address, doorbell_gpa);

    return 0;
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    return (data - 32) & 0xffff;
}

static void kvm_arch_get_eager_split_size(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint64_t value = s->kvm_eager_split_size;

    visit_type_size(v, name, &value, errp);
}

static void kvm_arch_set_eager_split_size(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint64_t value;

    if (s->fd != -1) {
        error_setg(errp, "Unable to set early-split-size after KVM has been initialized");
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    if (value && !is_power_of_2(value)) {
        error_setg(errp, "early-split-size must be a power of two");
        return;
    }

    s->kvm_eager_split_size = value;
}

void kvm_arch_accel_class_init(ObjectClass *oc)
{
    object_class_property_add(oc, "eager-split-size", "size",
                              kvm_arch_get_eager_split_size,
                              kvm_arch_set_eager_split_size, NULL, NULL);

    object_class_property_set_description(oc, "eager-split-size",
        "Eager Page Split chunk size for hugepages. (default: 0, disabled)");
}

int kvm_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return insert_hw_breakpoint(addr);
        break;
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return insert_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}

int kvm_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return delete_hw_breakpoint(addr);
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return delete_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    if (cur_hw_wps > 0) {
        g_array_remove_range(hw_watchpoints, 0, cur_hw_wps);
    }
    if (cur_hw_bps > 0) {
        g_array_remove_range(hw_breakpoints, 0, cur_hw_bps);
    }
}

static bool kvm_arm_set_device_attr(ARMCPU *cpu, struct kvm_device_attr *attr,
                                    const char *name)
{
    int err;

    err = kvm_vcpu_ioctl(CPU(cpu), KVM_HAS_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_HAS_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    err = kvm_vcpu_ioctl(CPU(cpu), KVM_SET_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_SET_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    return true;
}

void kvm_arm_pmu_init(ARMCPU *cpu)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .attr = KVM_ARM_VCPU_PMU_V3_INIT,
    };

    if (!cpu->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cpu, &attr, "PMU")) {
        error_report("failed to init PMU");
        abort();
    }
}

void kvm_arm_pmu_set_irq(ARMCPU *cpu, int irq)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .addr = (intptr_t)&irq,
        .attr = KVM_ARM_VCPU_PMU_V3_IRQ,
    };

    if (!cpu->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cpu, &attr, "PMU")) {
        error_report("failed to set irq for PMU");
        abort();
    }
}

void kvm_arm_pvtime_init(ARMCPU *cpu, uint64_t ipa)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PVTIME_CTRL,
        .attr = KVM_ARM_VCPU_PVTIME_IPA,
        .addr = (uint64_t)&ipa,
    };

    if (cpu->kvm_steal_time == ON_OFF_AUTO_OFF) {
        return;
    }
    if (!kvm_arm_set_device_attr(cpu, &attr, "PVTIME IPA")) {
        error_report("failed to init PVTIME IPA");
        abort();
    }
}

void kvm_arm_steal_time_finalize(ARMCPU *cpu, Error **errp)
{
    bool has_steal_time = kvm_check_extension(kvm_state, KVM_CAP_STEAL_TIME);

    if (cpu->kvm_steal_time == ON_OFF_AUTO_AUTO) {
        if (!has_steal_time || !arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
            cpu->kvm_steal_time = ON_OFF_AUTO_OFF;
        } else {
            cpu->kvm_steal_time = ON_OFF_AUTO_ON;
        }
    } else if (cpu->kvm_steal_time == ON_OFF_AUTO_ON) {
        if (!has_steal_time) {
            error_setg(errp, "'kvm-steal-time' cannot be enabled "
                             "on this host");
            return;
        } else if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
            /*
             * DEN0057A chapter 2 says "This specification only covers
             * systems in which the Execution state of the hypervisor
             * as well as EL1 of virtual machines is AArch64.". And,
             * to ensure that, the smc/hvc calls are only specified as
             * smc64/hvc64.
             */
            error_setg(errp, "'kvm-steal-time' cannot be enabled "
                             "for AArch32 guests");
            return;
        }
    }
}

bool kvm_arm_aarch32_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_EL1_32BIT);
}

bool kvm_arm_sve_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_SVE);
}

bool kvm_arm_mte_supported(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_MTE);
}

QEMU_BUILD_BUG_ON(KVM_ARM64_SVE_VQ_MIN != 1);

uint32_t kvm_arm_sve_get_vls(ARMCPU *cpu)
{
    /* Only call this function if kvm_arm_sve_supported() returns true. */
    static uint64_t vls[KVM_ARM64_SVE_VLS_WORDS];
    static bool probed;
    uint32_t vq = 0;
    int i;

    /*
     * KVM ensures all host CPUs support the same set of vector lengths.
     * So we only need to create the scratch VCPUs once and then cache
     * the results.
     */
    if (!probed) {
        struct kvm_vcpu_init init = {
            .target = -1,
            .features[0] = (1 << KVM_ARM_VCPU_SVE),
        };
        struct kvm_one_reg reg = {
            .id = KVM_REG_ARM64_SVE_VLS,
            .addr = (uint64_t)&vls[0],
        };
        int fdarray[3], ret;

        probed = true;

        if (!kvm_arm_create_scratch_host_vcpu(NULL, fdarray, &init)) {
            error_report("failed to create scratch VCPU with SVE enabled");
            abort();
        }
        ret = ioctl(fdarray[2], KVM_GET_ONE_REG, &reg);
        kvm_arm_destroy_scratch_host_vcpu(fdarray);
        if (ret) {
            error_report("failed to get KVM_REG_ARM64_SVE_VLS: %s",
                         strerror(errno));
            abort();
        }

        for (i = KVM_ARM64_SVE_VLS_WORDS - 1; i >= 0; --i) {
            if (vls[i]) {
                vq = 64 - clz64(vls[i]) + i * 64;
                break;
            }
        }
        if (vq > ARM_MAX_VQ) {
            warn_report("KVM supports vector lengths larger than "
                        "QEMU can enable");
            vls[0] &= MAKE_64BIT_MASK(0, ARM_MAX_VQ);
        }
    }

    return vls[0];
}

static int kvm_arm_sve_set_vls(ARMCPU *cpu)
{
    uint64_t vls[KVM_ARM64_SVE_VLS_WORDS] = { cpu->sve_vq.map };

    assert(cpu->sve_max_vq <= KVM_ARM64_SVE_VQ_MAX);

    return kvm_set_one_reg(CPU(cpu), KVM_REG_ARM64_SVE_VLS, &vls[0]);
}

#define ARM_CPU_ID_MPIDR       3, 0, 0, 0, 5

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret;
    uint64_t mpidr;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t psciver;

    if (cpu->kvm_target == QEMU_KVM_ARM_TARGET_NONE ||
        !object_dynamic_cast(OBJECT(cpu), TYPE_AARCH64_CPU)) {
        error_report("KVM is not supported for this guest CPU type");
        return -EINVAL;
    }

    qemu_add_vm_change_state_handler(kvm_arm_vm_state_change, cpu);

    /* Determine init features for this CPU */
    memset(cpu->kvm_init_features, 0, sizeof(cpu->kvm_init_features));
    if (cs->start_powered_off) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_POWER_OFF;
    }
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PSCI_0_2)) {
        cpu->psci_version = QEMU_PSCI_VERSION_0_2;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    }
    if (!arm_feature(env, ARM_FEATURE_AARCH64)) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_EL1_32BIT;
    }
    if (cpu->has_pmu) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
    }
    if (cpu_isar_feature(aa64_sve, cpu)) {
        assert(kvm_arm_sve_supported());
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_SVE;
    }
    if (cpu_isar_feature(aa64_pauth, cpu)) {
        cpu->kvm_init_features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS |
                                      1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
    }

    /* Do KVM_ARM_VCPU_INIT ioctl */
    ret = kvm_arm_vcpu_init(cpu);
    if (ret) {
        return ret;
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arm_sve_set_vls(cpu);
        if (ret) {
            return ret;
        }
        ret = kvm_arm_vcpu_finalize(cpu, KVM_ARM_VCPU_SVE);
        if (ret) {
            return ret;
        }
    }

    /*
     * KVM reports the exact PSCI version it is implementing via a
     * special sysreg. If it is present, use its contents to determine
     * what to report to the guest in the dtb (it is the PSCI version,
     * in the same 15-bits major 16-bits minor format that PSCI_VERSION
     * returns).
     */
    if (!kvm_get_one_reg(cs, KVM_REG_ARM_PSCI_VERSION, &psciver)) {
        cpu->psci_version = psciver;
    }

    /*
     * When KVM is in use, PSCI is emulated in-kernel and not by qemu.
     * Currently KVM has its own idea about MPIDR assignment, so we
     * override our defaults with what we get from KVM.
     */
    ret = kvm_get_one_reg(cs, ARM64_SYS_REG(ARM_CPU_ID_MPIDR), &mpidr);
    if (ret) {
        return ret;
    }
    cpu->mp_affinity = mpidr & ARM64_AFFINITY_MASK;

    return kvm_arm_init_cpreg_list(cpu);
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

/* Callers must hold the iothread mutex lock */
static void kvm_inject_arm_sea(CPUState *c)
{
    ARMCPU *cpu = ARM_CPU(c);
    CPUARMState *env = &cpu->env;
    uint32_t esr;
    bool same_el;

    c->exception_index = EXCP_DATA_ABORT;
    env->exception.target_el = 1;

    /*
     * Set the DFSC to synchronous external abort and set FnV to not valid,
     * this will tell guest the FAR_ELx is UNKNOWN for this abort.
     */
    same_el = arm_current_el(env) == env->exception.target_el;
    esr = syn_data_abort_no_iss(same_el, 1, 0, 0, 0, 0, 0x10);

    env->exception.syndrome = esr;

    arm_cpu_do_interrupt(c);
}

#define AARCH64_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CORE_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U128 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define AARCH64_SIMD_CTRL_REG(x)   (KVM_REG_ARM64 | KVM_REG_SIZE_U32 | \
                 KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

static int kvm_arch_put_fpsimd(CPUState *cs)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    int i, ret;

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
#if HOST_BIG_ENDIAN
        uint64_t fp_val[2] = { q[1], q[0] };
        ret = kvm_set_one_reg(cs, AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]),
                                                        fp_val);
#else
        ret = kvm_set_one_reg(cs, AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]), q);
#endif
        if (ret) {
            return ret;
        }
    }

    return 0;
}

/*
 * KVM SVE registers come in slices where ZREGs have a slice size of 2048 bits
 * and PREGS and the FFR have a slice size of 256 bits. However we simply hard
 * code the slice index to zero for now as it's unlikely we'll need more than
 * one slice for quite some time.
 */
static int kvm_arch_put_sve(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp[ARM_MAX_VQ * 2];
    uint64_t *r;
    int n, ret;

    for (n = 0; n < KVM_ARM64_SVE_NUM_ZREGS; ++n) {
        r = sve_bswap64(tmp, &env->vfp.zregs[n].d[0], cpu->sve_max_vq * 2);
        ret = kvm_set_one_reg(cs, KVM_REG_ARM64_SVE_ZREG(n, 0), r);
        if (ret) {
            return ret;
        }
    }

    for (n = 0; n < KVM_ARM64_SVE_NUM_PREGS; ++n) {
        r = sve_bswap64(tmp, r = &env->vfp.pregs[n].p[0],
                        DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
        ret = kvm_set_one_reg(cs, KVM_REG_ARM64_SVE_PREG(n, 0), r);
        if (ret) {
            return ret;
        }
    }

    r = sve_bswap64(tmp, &env->vfp.pregs[FFR_PRED_NUM].p[0],
                    DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
    ret = kvm_set_one_reg(cs, KVM_REG_ARM64_SVE_FFR(0), r);
    if (ret) {
        return ret;
    }

    return 0;
}

int kvm_arch_put_registers(CPUState *cs, int level, Error **errp)
{
    uint64_t val;
    uint32_t fpr;
    int i, ret;
    unsigned int el;

    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /* If we are in AArch32 mode then we need to copy the AArch32 regs to the
     * AArch64 registers before pushing them out to 64-bit KVM.
     */
    if (!is_a64(env)) {
        aarch64_sync_32_to_64(env);
    }

    for (i = 0; i < 31; i++) {
        ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(regs.regs[i]),
                              &env->xregs[i]);
        if (ret) {
            return ret;
        }
    }

    /* KVM puts SP_EL0 in regs.sp and SP_EL1 in regs.sp_el1. On the
     * QEMU side we keep the current SP in xregs[31] as well.
     */
    aarch64_save_sp(env, 1);

    ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(regs.sp), &env->sp_el[0]);
    if (ret) {
        return ret;
    }

    ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(sp_el1), &env->sp_el[1]);
    if (ret) {
        return ret;
    }

    /* Note that KVM thinks pstate is 64 bit but we use a uint32_t */
    if (is_a64(env)) {
        val = pstate_read(env);
    } else {
        val = cpsr_read(env);
    }
    ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(regs.pstate), &val);
    if (ret) {
        return ret;
    }

    ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(regs.pc), &env->pc);
    if (ret) {
        return ret;
    }

    ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(elr_el1), &env->elr_el[1]);
    if (ret) {
        return ret;
    }

    /* Saved Program State Registers
     *
     * Before we restore from the banked_spsr[] array we need to
     * ensure that any modifications to env->spsr are correctly
     * reflected in the banks.
     */
    el = arm_current_el(env);
    if (el > 0 && !is_a64(env)) {
        i = bank_number(env->uncached_cpsr & CPSR_M);
        env->banked_spsr[i] = env->spsr;
    }

    /* KVM 0-4 map to QEMU banks 1-5 */
    for (i = 0; i < KVM_NR_SPSR; i++) {
        ret = kvm_set_one_reg(cs, AARCH64_CORE_REG(spsr[i]),
                              &env->banked_spsr[i + 1]);
        if (ret) {
            return ret;
        }
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arch_put_sve(cs);
    } else {
        ret = kvm_arch_put_fpsimd(cs);
    }
    if (ret) {
        return ret;
    }

    fpr = vfp_get_fpsr(env);
    ret = kvm_set_one_reg(cs, AARCH64_SIMD_CTRL_REG(fp_regs.fpsr), &fpr);
    if (ret) {
        return ret;
    }

    fpr = vfp_get_fpcr(env);
    ret = kvm_set_one_reg(cs, AARCH64_SIMD_CTRL_REG(fp_regs.fpcr), &fpr);
    if (ret) {
        return ret;
    }

    write_cpustate_to_list(cpu, true);

    if (!write_list_to_kvmstate(cpu, level)) {
        return -EINVAL;
    }

   /*
    * Setting VCPU events should be triggered after syncing the registers
    * to avoid overwriting potential changes made by KVM upon calling
    * KVM_SET_VCPU_EVENTS ioctl
    */
    ret = kvm_put_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    return kvm_arm_sync_mpstate_to_kvm(cpu);
}

static int kvm_arch_get_fpsimd(CPUState *cs)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    int i, ret;

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
        ret = kvm_get_one_reg(cs, AARCH64_SIMD_CORE_REG(fp_regs.vregs[i]), q);
        if (ret) {
            return ret;
        } else {
#if HOST_BIG_ENDIAN
            uint64_t t;
            t = q[0], q[0] = q[1], q[1] = t;
#endif
        }
    }

    return 0;
}

/*
 * KVM SVE registers come in slices where ZREGs have a slice size of 2048 bits
 * and PREGS and the FFR have a slice size of 256 bits. However we simply hard
 * code the slice index to zero for now as it's unlikely we'll need more than
 * one slice for quite some time.
 */
static int kvm_arch_get_sve(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t *r;
    int n, ret;

    for (n = 0; n < KVM_ARM64_SVE_NUM_ZREGS; ++n) {
        r = &env->vfp.zregs[n].d[0];
        ret = kvm_get_one_reg(cs, KVM_REG_ARM64_SVE_ZREG(n, 0), r);
        if (ret) {
            return ret;
        }
        sve_bswap64(r, r, cpu->sve_max_vq * 2);
    }

    for (n = 0; n < KVM_ARM64_SVE_NUM_PREGS; ++n) {
        r = &env->vfp.pregs[n].p[0];
        ret = kvm_get_one_reg(cs, KVM_REG_ARM64_SVE_PREG(n, 0), r);
        if (ret) {
            return ret;
        }
        sve_bswap64(r, r, DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));
    }

    r = &env->vfp.pregs[FFR_PRED_NUM].p[0];
    ret = kvm_get_one_reg(cs, KVM_REG_ARM64_SVE_FFR(0), r);
    if (ret) {
        return ret;
    }
    sve_bswap64(r, r, DIV_ROUND_UP(cpu->sve_max_vq * 2, 8));

    return 0;
}

int kvm_arch_get_registers(CPUState *cs, Error **errp)
{
    uint64_t val;
    unsigned int el;
    uint32_t fpr;
    int i, ret;

    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    for (i = 0; i < 31; i++) {
        ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(regs.regs[i]),
                              &env->xregs[i]);
        if (ret) {
            return ret;
        }
    }

    ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(regs.sp), &env->sp_el[0]);
    if (ret) {
        return ret;
    }

    ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(sp_el1), &env->sp_el[1]);
    if (ret) {
        return ret;
    }

    ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(regs.pstate), &val);
    if (ret) {
        return ret;
    }

    env->aarch64 = ((val & PSTATE_nRW) == 0);
    if (is_a64(env)) {
        pstate_write(env, val);
    } else {
        cpsr_write(env, val, 0xffffffff, CPSRWriteRaw);
    }

    /* KVM puts SP_EL0 in regs.sp and SP_EL1 in regs.sp_el1. On the
     * QEMU side we keep the current SP in xregs[31] as well.
     */
    aarch64_restore_sp(env, 1);

    ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(regs.pc), &env->pc);
    if (ret) {
        return ret;
    }

    /* If we are in AArch32 mode then we need to sync the AArch32 regs with the
     * incoming AArch64 regs received from 64-bit KVM.
     * We must perform this after all of the registers have been acquired from
     * the kernel.
     */
    if (!is_a64(env)) {
        aarch64_sync_64_to_32(env);
    }

    ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(elr_el1), &env->elr_el[1]);
    if (ret) {
        return ret;
    }

    /* Fetch the SPSR registers
     *
     * KVM SPSRs 0-4 map to QEMU banks 1-5
     */
    for (i = 0; i < KVM_NR_SPSR; i++) {
        ret = kvm_get_one_reg(cs, AARCH64_CORE_REG(spsr[i]),
                              &env->banked_spsr[i + 1]);
        if (ret) {
            return ret;
        }
    }

    el = arm_current_el(env);
    if (el > 0 && !is_a64(env)) {
        i = bank_number(env->uncached_cpsr & CPSR_M);
        env->spsr = env->banked_spsr[i];
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arch_get_sve(cs);
    } else {
        ret = kvm_arch_get_fpsimd(cs);
    }
    if (ret) {
        return ret;
    }

    ret = kvm_get_one_reg(cs, AARCH64_SIMD_CTRL_REG(fp_regs.fpsr), &fpr);
    if (ret) {
        return ret;
    }
    vfp_set_fpsr(env, fpr);

    ret = kvm_get_one_reg(cs, AARCH64_SIMD_CTRL_REG(fp_regs.fpcr), &fpr);
    if (ret) {
        return ret;
    }
    vfp_set_fpcr(env, fpr);

    ret = kvm_get_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    if (!write_kvmstate_to_list(cpu)) {
        return -EINVAL;
    }
    /* Note that it's OK to have registers which aren't in CPUState,
     * so we can ignore a failure return here.
     */
    write_list_to_cpustate(cpu);

    ret = kvm_arm_sync_mpstate_to_qemu(cpu);

    /* TODO: other registers */
    return ret;
}

void kvm_arch_on_sigbus_vcpu(CPUState *c, int code, void *addr)
{
    ram_addr_t ram_addr;
    hwaddr paddr;

    assert(code == BUS_MCEERR_AR || code == BUS_MCEERR_AO);

    if (acpi_ghes_present() && addr) {
        ram_addr = qemu_ram_addr_from_host(addr);
        if (ram_addr != RAM_ADDR_INVALID &&
            kvm_physical_memory_addr_from_host(c->kvm_state, addr, &paddr)) {
            kvm_hwpoison_page_add(ram_addr);
            /*
             * If this is a BUS_MCEERR_AR, we know we have been called
             * synchronously from the vCPU thread, so we can easily
             * synchronize the state and inject an error.
             *
             * TODO: we currently don't tell the guest at all about
             * BUS_MCEERR_AO. In that case we might either be being
             * called synchronously from the vCPU thread, or a bit
             * later from the main thread, so doing the injection of
             * the error would be more complicated.
             */
            if (code == BUS_MCEERR_AR) {
                kvm_cpu_synchronize_state(c);
                if (!acpi_ghes_memory_errors(ACPI_HEST_SRC_ID_SEA, paddr)) {
                    kvm_inject_arm_sea(c);
                } else {
                    error_report("failed to record the error");
                    abort();
                }
            }
            return;
        }
        if (code == BUS_MCEERR_AO) {
            error_report("Hardware memory error at addr %p for memory used by "
                "QEMU itself instead of guest system!", addr);
        }
    }

    if (code == BUS_MCEERR_AR) {
        error_report("Hardware memory error!");
        exit(1);
    }
}

/* C6.6.29 BRK instruction */
static const uint32_t brk_insn = 0xd4200000;

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk_insn, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    static uint32_t brk;

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&brk, 4, 0) ||
        brk != brk_insn ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

void kvm_arm_enable_mte(Object *cpuobj, Error **errp)
{
    static bool tried_to_enable;
    static bool succeeded_to_enable;
    Error *mte_migration_blocker = NULL;
    ARMCPU *cpu = ARM_CPU(cpuobj);
    int ret;

    if (!tried_to_enable) {
        /*
         * MTE on KVM is enabled on a per-VM basis (and retrying doesn't make
         * sense), and we only want a single migration blocker as well.
         */
        tried_to_enable = true;

        ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_ARM_MTE, 0);
        if (ret) {
            error_setg_errno(errp, -ret, "Failed to enable KVM_CAP_ARM_MTE");
            return;
        }

        /* TODO: Add migration support with MTE enabled */
        error_setg(&mte_migration_blocker,
                   "Live migration disabled due to MTE enabled");
        if (migrate_add_blocker(&mte_migration_blocker, errp)) {
            error_free(mte_migration_blocker);
            return;
        }

        succeeded_to_enable = true;
    }

    if (succeeded_to_enable) {
        cpu->kvm_mte = true;
    }
}
