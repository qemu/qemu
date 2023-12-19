/*
 * ARM implementation of KVM hooks, 64 bit specific code
 *
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/ptrace.h>

#include <linux/elf.h>
#include <linux/kvm.h>

#include "qapi/error.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "exec/gdbstub.h"
#include "sysemu/runstate.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "kvm_arm.h"
#include "internals.h"
#include "cpu-features.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ghes.h"


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

static bool kvm_arm_set_device_attr(CPUState *cs, struct kvm_device_attr *attr,
                                    const char *name)
{
    int err;

    err = kvm_vcpu_ioctl(cs, KVM_HAS_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_HAS_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    err = kvm_vcpu_ioctl(cs, KVM_SET_DEVICE_ATTR, attr);
    if (err != 0) {
        error_report("%s: KVM_SET_DEVICE_ATTR: %s", name, strerror(-err));
        return false;
    }

    return true;
}

void kvm_arm_pmu_init(CPUState *cs)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .attr = KVM_ARM_VCPU_PMU_V3_INIT,
    };

    if (!ARM_CPU(cs)->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PMU")) {
        error_report("failed to init PMU");
        abort();
    }
}

void kvm_arm_pmu_set_irq(CPUState *cs, int irq)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PMU_V3_CTRL,
        .addr = (intptr_t)&irq,
        .attr = KVM_ARM_VCPU_PMU_V3_IRQ,
    };

    if (!ARM_CPU(cs)->has_pmu) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PMU")) {
        error_report("failed to set irq for PMU");
        abort();
    }
}

void kvm_arm_pvtime_init(CPUState *cs, uint64_t ipa)
{
    struct kvm_device_attr attr = {
        .group = KVM_ARM_VCPU_PVTIME_CTRL,
        .attr = KVM_ARM_VCPU_PVTIME_IPA,
        .addr = (uint64_t)&ipa,
    };

    if (ARM_CPU(cs)->kvm_steal_time == ON_OFF_AUTO_OFF) {
        return;
    }
    if (!kvm_arm_set_device_attr(cs, &attr, "PVTIME IPA")) {
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

QEMU_BUILD_BUG_ON(KVM_ARM64_SVE_VQ_MIN != 1);

uint32_t kvm_arm_sve_get_vls(CPUState *cs)
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

static int kvm_arm_sve_set_vls(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    uint64_t vls[KVM_ARM64_SVE_VLS_WORDS] = { cpu->sve_vq.map };

    assert(cpu->sve_max_vq <= KVM_ARM64_SVE_VQ_MAX);

    return kvm_set_one_reg(cs, KVM_REG_ARM64_SVE_VLS, &vls[0]);
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

    qemu_add_vm_change_state_handler(kvm_arm_vm_state_change, cs);

    /* Determine init features for this CPU */
    memset(cpu->kvm_init_features, 0, sizeof(cpu->kvm_init_features));
    if (cs->start_powered_off) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_POWER_OFF;
    }
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PSCI_0_2)) {
        cpu->psci_version = QEMU_PSCI_VERSION_0_2;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    }
    if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_EL1_32BIT;
    }
    if (!kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PMU_V3)) {
        cpu->has_pmu = false;
    }
    if (cpu->has_pmu) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
    } else {
        env->features &= ~(1ULL << ARM_FEATURE_PMU);
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
    ret = kvm_arm_vcpu_init(cs);
    if (ret) {
        return ret;
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        ret = kvm_arm_sve_set_vls(cs);
        if (ret) {
            return ret;
        }
        ret = kvm_arm_vcpu_finalize(cs, KVM_ARM_VCPU_SVE);
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

    /* Check whether user space can specify guest syndrome value */
    kvm_arm_init_serror_injection(cs);

    return kvm_arm_init_cpreg_list(cpu);
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx)
{
    /* Return true if the regidx is a register we should synchronize
     * via the cpreg_tuples array (ie is not a core or sve reg that
     * we sync by hand in kvm_arch_get/put_registers())
     */
    switch (regidx & KVM_REG_ARM_COPROC_MASK) {
    case KVM_REG_ARM_CORE:
    case KVM_REG_ARM64_SVE:
        return false;
    default:
        return true;
    }
}

typedef struct CPRegStateLevel {
    uint64_t regidx;
    int level;
} CPRegStateLevel;

/* All system registers not listed in the following table are assumed to be
 * of the level KVM_PUT_RUNTIME_STATE. If a register should be written less
 * often, you must add it to this table with a state of either
 * KVM_PUT_RESET_STATE or KVM_PUT_FULL_STATE.
 */
static const CPRegStateLevel non_runtime_cpregs[] = {
    { KVM_REG_ARM_TIMER_CNT, KVM_PUT_FULL_STATE },
    { KVM_REG_ARM_PTIMER_CNT, KVM_PUT_FULL_STATE },
};

int kvm_arm_cpreg_level(uint64_t regidx)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(non_runtime_cpregs); i++) {
        const CPRegStateLevel *l = &non_runtime_cpregs[i];
        if (l->regidx == regidx) {
            return l->level;
        }
    }

    return KVM_PUT_RUNTIME_STATE;
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

int kvm_arch_put_registers(CPUState *cs, int level)
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

    kvm_arm_sync_mpstate_to_kvm(cpu);

    return ret;
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

int kvm_arch_get_registers(CPUState *cs)
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

    kvm_arm_sync_mpstate_to_qemu(cpu);

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
                if (!acpi_ghes_record_errors(ACPI_HEST_SRC_ID_SEA, paddr)) {
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
