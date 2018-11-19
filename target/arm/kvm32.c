/*
 * ARM implementation of KVM hooks, 32 bit specific code.
 *
 * Copyright Christoffer Dall 2009-2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "internals.h"
#include "hw/arm/arm.h"
#include "qemu/log.h"

static inline void set_feature(uint64_t *features, int feature)
{
    *features |= 1ULL << feature;
}

static int read_sys_reg32(int fd, uint32_t *pret, uint64_t id)
{
    struct kvm_one_reg idreg = { .id = id, .addr = (uintptr_t)pret };

    assert((id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U32);
    return ioctl(fd, KVM_GET_ONE_REG, &idreg);
}

bool kvm_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    /* Identify the feature bits corresponding to the host CPU, and
     * fill out the ARMHostCPUClass fields accordingly. To do this
     * we have to create a scratch VM, create a single CPU inside it,
     * and then query that CPU for the relevant ID registers.
     */
    int err = 0, fdarray[3];
    uint32_t midr, id_pfr0;
    uint64_t features = 0;

    /* Old kernels may not know about the PREFERRED_TARGET ioctl: however
     * we know these will only support creating one kind of guest CPU,
     * which is its preferred CPU type.
     */
    static const uint32_t cpus_to_try[] = {
        QEMU_KVM_ARM_TARGET_CORTEX_A15,
        QEMU_KVM_ARM_TARGET_NONE
    };
    struct kvm_vcpu_init init;

    if (!kvm_arm_create_scratch_host_vcpu(cpus_to_try, fdarray, &init)) {
        return false;
    }

    ahcf->target = init.target;

    /* This is not strictly blessed by the device tree binding docs yet,
     * but in practice the kernel does not care about this string so
     * there is no point maintaining an KVM_ARM_TARGET_* -> string table.
     */
    ahcf->dtb_compatible = "arm,arm-v7";

    err |= read_sys_reg32(fdarray[2], &midr, ARM_CP15_REG32(0, 0, 0, 0));
    err |= read_sys_reg32(fdarray[2], &id_pfr0, ARM_CP15_REG32(0, 0, 1, 0));

    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar0,
                          ARM_CP15_REG32(0, 0, 2, 0));
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar1,
                          ARM_CP15_REG32(0, 0, 2, 1));
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar2,
                          ARM_CP15_REG32(0, 0, 2, 2));
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar3,
                          ARM_CP15_REG32(0, 0, 2, 3));
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar4,
                          ARM_CP15_REG32(0, 0, 2, 4));
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.id_isar5,
                          ARM_CP15_REG32(0, 0, 2, 5));
    if (read_sys_reg32(fdarray[2], &ahcf->isar.id_isar6,
                       ARM_CP15_REG32(0, 0, 2, 7))) {
        /*
         * Older kernels don't support reading ID_ISAR6. This register was
         * only introduced in ARMv8, so we can assume that it is zero on a
         * CPU that a kernel this old is running on.
         */
        ahcf->isar.id_isar6 = 0;
    }

    err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr0,
                          KVM_REG_ARM | KVM_REG_SIZE_U32 |
                          KVM_REG_ARM_VFP | KVM_REG_ARM_VFP_MVFR0);
    err |= read_sys_reg32(fdarray[2], &ahcf->isar.mvfr1,
                          KVM_REG_ARM | KVM_REG_SIZE_U32 |
                          KVM_REG_ARM_VFP | KVM_REG_ARM_VFP_MVFR1);
    /*
     * FIXME: There is not yet a way to read MVFR2.
     * Fortunately there is not yet anything in there that affects migration.
     */

    kvm_arm_destroy_scratch_host_vcpu(fdarray);

    if (err < 0) {
        return false;
    }

    /* Now we've retrieved all the register information we can
     * set the feature bits based on the ID register fields.
     * We can assume any KVM supporting CPU is at least a v7
     * with VFPv3, virtualization extensions, and the generic
     * timers; this in turn implies most of the other feature
     * bits, but a few must be tested.
     */
    set_feature(&features, ARM_FEATURE_V7VE);
    set_feature(&features, ARM_FEATURE_VFP3);
    set_feature(&features, ARM_FEATURE_GENERIC_TIMER);

    if (extract32(id_pfr0, 12, 4) == 1) {
        set_feature(&features, ARM_FEATURE_THUMB2EE);
    }
    if (extract32(ahcf->isar.mvfr1, 20, 4) == 1) {
        set_feature(&features, ARM_FEATURE_VFP_FP16);
    }
    if (extract32(ahcf->isar.mvfr1, 12, 4) == 1) {
        set_feature(&features, ARM_FEATURE_NEON);
    }
    if (extract32(ahcf->isar.mvfr1, 28, 4) == 1) {
        /* FMAC support implies VFPv4 */
        set_feature(&features, ARM_FEATURE_VFP4);
    }

    ahcf->features = features;

    return true;
}

bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx)
{
    /* Return true if the regidx is a register we should synchronize
     * via the cpreg_tuples array (ie is not a core reg we sync by
     * hand in kvm_arch_get/put_registers())
     */
    switch (regidx & KVM_REG_ARM_COPROC_MASK) {
    case KVM_REG_ARM_CORE:
    case KVM_REG_ARM_VFP:
        return false;
    default:
        return true;
    }
}

typedef struct CPRegStateLevel {
    uint64_t regidx;
    int level;
} CPRegStateLevel;

/* All coprocessor registers not listed in the following table are assumed to
 * be of the level KVM_PUT_RUNTIME_STATE. If a register should be written less
 * often, you must add it to this table with a state of either
 * KVM_PUT_RESET_STATE or KVM_PUT_FULL_STATE.
 */
static const CPRegStateLevel non_runtime_cpregs[] = {
    { KVM_REG_ARM_TIMER_CNT, KVM_PUT_FULL_STATE },
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

#define ARM_CPU_ID_MPIDR       0, 0, 0, 5

int kvm_arch_init_vcpu(CPUState *cs)
{
    int ret;
    uint64_t v;
    uint32_t mpidr;
    struct kvm_one_reg r;
    ARMCPU *cpu = ARM_CPU(cs);

    if (cpu->kvm_target == QEMU_KVM_ARM_TARGET_NONE) {
        fprintf(stderr, "KVM is not supported for this guest CPU type\n");
        return -EINVAL;
    }

    /* Determine init features for this CPU */
    memset(cpu->kvm_init_features, 0, sizeof(cpu->kvm_init_features));
    if (cpu->start_powered_off) {
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_POWER_OFF;
    }
    if (kvm_check_extension(cs->kvm_state, KVM_CAP_ARM_PSCI_0_2)) {
        cpu->psci_version = 2;
        cpu->kvm_init_features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    }

    /* Do KVM_ARM_VCPU_INIT ioctl */
    ret = kvm_arm_vcpu_init(cs);
    if (ret) {
        return ret;
    }

    /* Query the kernel to make sure it supports 32 VFP
     * registers: QEMU's "cortex-a15" CPU is always a
     * VFP-D32 core. The simplest way to do this is just
     * to attempt to read register d31.
     */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | KVM_REG_ARM_VFP | 31;
    r.addr = (uintptr_t)(&v);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
    if (ret == -ENOENT) {
        return -EINVAL;
    }

    /*
     * When KVM is in use, PSCI is emulated in-kernel and not by qemu.
     * Currently KVM has its own idea about MPIDR assignment, so we
     * override our defaults with what we get from KVM.
     */
    ret = kvm_get_one_reg(cs, ARM_CP15_REG32(ARM_CPU_ID_MPIDR), &mpidr);
    if (ret) {
        return ret;
    }
    cpu->mp_affinity = mpidr & ARM32_AFFINITY_MASK;

    /* Check whether userspace can specify guest syndrome value */
    kvm_arm_init_serror_injection(cs);

    return kvm_arm_init_cpreg_list(cpu);
}

typedef struct Reg {
    uint64_t id;
    int offset;
} Reg;

#define COREREG(KERNELNAME, QEMUFIELD)                       \
    {                                                        \
        KVM_REG_ARM | KVM_REG_SIZE_U32 |                     \
        KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(KERNELNAME), \
        offsetof(CPUARMState, QEMUFIELD)                     \
    }

#define VFPSYSREG(R)                                       \
    {                                                      \
        KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_VFP | \
        KVM_REG_ARM_VFP_##R,                               \
        offsetof(CPUARMState, vfp.xregs[ARM_VFP_##R])      \
    }

/* Like COREREG, but handle fields which are in a uint64_t in CPUARMState. */
#define COREREG64(KERNELNAME, QEMUFIELD)                     \
    {                                                        \
        KVM_REG_ARM | KVM_REG_SIZE_U32 |                     \
        KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(KERNELNAME), \
        offsetoflow32(CPUARMState, QEMUFIELD)                \
    }

static const Reg regs[] = {
    /* R0_usr .. R14_usr */
    COREREG(usr_regs.uregs[0], regs[0]),
    COREREG(usr_regs.uregs[1], regs[1]),
    COREREG(usr_regs.uregs[2], regs[2]),
    COREREG(usr_regs.uregs[3], regs[3]),
    COREREG(usr_regs.uregs[4], regs[4]),
    COREREG(usr_regs.uregs[5], regs[5]),
    COREREG(usr_regs.uregs[6], regs[6]),
    COREREG(usr_regs.uregs[7], regs[7]),
    COREREG(usr_regs.uregs[8], usr_regs[0]),
    COREREG(usr_regs.uregs[9], usr_regs[1]),
    COREREG(usr_regs.uregs[10], usr_regs[2]),
    COREREG(usr_regs.uregs[11], usr_regs[3]),
    COREREG(usr_regs.uregs[12], usr_regs[4]),
    COREREG(usr_regs.uregs[13], banked_r13[BANK_USRSYS]),
    COREREG(usr_regs.uregs[14], banked_r14[BANK_USRSYS]),
    /* R13, R14, SPSR for SVC, ABT, UND, IRQ banks */
    COREREG(svc_regs[0], banked_r13[BANK_SVC]),
    COREREG(svc_regs[1], banked_r14[BANK_SVC]),
    COREREG64(svc_regs[2], banked_spsr[BANK_SVC]),
    COREREG(abt_regs[0], banked_r13[BANK_ABT]),
    COREREG(abt_regs[1], banked_r14[BANK_ABT]),
    COREREG64(abt_regs[2], banked_spsr[BANK_ABT]),
    COREREG(und_regs[0], banked_r13[BANK_UND]),
    COREREG(und_regs[1], banked_r14[BANK_UND]),
    COREREG64(und_regs[2], banked_spsr[BANK_UND]),
    COREREG(irq_regs[0], banked_r13[BANK_IRQ]),
    COREREG(irq_regs[1], banked_r14[BANK_IRQ]),
    COREREG64(irq_regs[2], banked_spsr[BANK_IRQ]),
    /* R8_fiq .. R14_fiq and SPSR_fiq */
    COREREG(fiq_regs[0], fiq_regs[0]),
    COREREG(fiq_regs[1], fiq_regs[1]),
    COREREG(fiq_regs[2], fiq_regs[2]),
    COREREG(fiq_regs[3], fiq_regs[3]),
    COREREG(fiq_regs[4], fiq_regs[4]),
    COREREG(fiq_regs[5], banked_r13[BANK_FIQ]),
    COREREG(fiq_regs[6], banked_r14[BANK_FIQ]),
    COREREG64(fiq_regs[7], banked_spsr[BANK_FIQ]),
    /* R15 */
    COREREG(usr_regs.uregs[15], regs[15]),
    /* VFP system registers */
    VFPSYSREG(FPSID),
    VFPSYSREG(MVFR1),
    VFPSYSREG(MVFR0),
    VFPSYSREG(FPEXC),
    VFPSYSREG(FPINST),
    VFPSYSREG(FPINST2),
};

int kvm_arch_put_registers(CPUState *cs, int level)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    struct kvm_one_reg r;
    int mode, bn;
    int ret, i;
    uint32_t cpsr, fpscr;

    /* Make sure the banked regs are properly set */
    mode = env->uncached_cpsr & CPSR_M;
    bn = bank_number(mode);
    if (mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->fiq_regs, env->regs + 8, 5 * sizeof(uint32_t));
    } else {
        memcpy(env->usr_regs, env->regs + 8, 5 * sizeof(uint32_t));
    }
    env->banked_r13[bn] = env->regs[13];
    env->banked_spsr[bn] = env->spsr;
    env->banked_r14[r14_bank_number(mode)] = env->regs[14];

    /* Now we can safely copy stuff down to the kernel */
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        r.id = regs[i].id;
        r.addr = (uintptr_t)(env) + regs[i].offset;
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
        if (ret) {
            return ret;
        }
    }

    /* Special cases which aren't a single CPUARMState field */
    cpsr = cpsr_read(env);
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U32 |
        KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(usr_regs.ARM_cpsr);
    r.addr = (uintptr_t)(&cpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
    if (ret) {
        return ret;
    }

    /* VFP registers */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | KVM_REG_ARM_VFP;
    for (i = 0; i < 32; i++) {
        r.addr = (uintptr_t)aa32_vfp_dreg(env, i);
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
        if (ret) {
            return ret;
        }
        r.id++;
    }

    r.id = KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_VFP |
        KVM_REG_ARM_VFP_FPSCR;
    fpscr = vfp_get_fpscr(env);
    r.addr = (uintptr_t)&fpscr;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
    if (ret) {
        return ret;
    }

    ret = kvm_put_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    /* Note that we do not call write_cpustate_to_list()
     * here, so we are only writing the tuple list back to
     * KVM. This is safe because nothing can change the
     * CPUARMState cp15 fields (in particular gdb accesses cannot)
     * and so there are no changes to sync. In fact syncing would
     * be wrong at this point: for a constant register where TCG and
     * KVM disagree about its value, the preceding write_list_to_cpustate()
     * would not have had any effect on the CPUARMState value (since the
     * register is read-only), and a write_cpustate_to_list() here would
     * then try to write the TCG value back into KVM -- this would either
     * fail or incorrectly change the value the guest sees.
     *
     * If we ever want to allow the user to modify cp15 registers via
     * the gdb stub, we would need to be more clever here (for instance
     * tracking the set of registers kvm_arch_get_registers() successfully
     * managed to update the CPUARMState with, and only allowing those
     * to be written back up into the kernel).
     */
    if (!write_list_to_kvmstate(cpu, level)) {
        return EINVAL;
    }

    kvm_arm_sync_mpstate_to_kvm(cpu);

    return ret;
}

int kvm_arch_get_registers(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    struct kvm_one_reg r;
    int mode, bn;
    int ret, i;
    uint32_t cpsr, fpscr;

    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        r.id = regs[i].id;
        r.addr = (uintptr_t)(env) + regs[i].offset;
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
        if (ret) {
            return ret;
        }
    }

    /* Special cases which aren't a single CPUARMState field */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U32 |
        KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(usr_regs.ARM_cpsr);
    r.addr = (uintptr_t)(&cpsr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
    if (ret) {
        return ret;
    }
    cpsr_write(env, cpsr, 0xffffffff, CPSRWriteRaw);

    /* Make sure the current mode regs are properly set */
    mode = env->uncached_cpsr & CPSR_M;
    bn = bank_number(mode);
    if (mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    } else {
        memcpy(env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    }
    env->regs[13] = env->banked_r13[bn];
    env->spsr = env->banked_spsr[bn];
    env->regs[14] = env->banked_r14[r14_bank_number(mode)];

    /* VFP registers */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | KVM_REG_ARM_VFP;
    for (i = 0; i < 32; i++) {
        r.addr = (uintptr_t)aa32_vfp_dreg(env, i);
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
        if (ret) {
            return ret;
        }
        r.id++;
    }

    r.id = KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_VFP |
        KVM_REG_ARM_VFP_FPSCR;
    r.addr = (uintptr_t)&fpscr;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
    if (ret) {
        return ret;
    }
    vfp_set_fpscr(env, fpscr);

    ret = kvm_get_vcpu_events(cpu);
    if (ret) {
        return ret;
    }

    if (!write_kvmstate_to_list(cpu)) {
        return EINVAL;
    }
    /* Note that it's OK to have registers which aren't in CPUState,
     * so we can ignore a failure return here.
     */
    write_list_to_cpustate(cpu);

    kvm_arm_sync_mpstate_to_qemu(cpu);

    return 0;
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: guest debug not yet implemented\n", __func__);
    return -EINVAL;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: guest debug not yet implemented\n", __func__);
    return -EINVAL;
}

bool kvm_arm_handle_debug(CPUState *cs, struct kvm_debug_exit_arch *debug_exit)
{
    qemu_log_mask(LOG_UNIMP, "%s: guest debug not yet implemented\n", __func__);
    return false;
}

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

void kvm_arm_copy_hw_debug_data(struct kvm_guest_debug_arch *ptr)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

bool kvm_arm_hw_debug_active(CPUState *cs)
{
    return false;
}

void kvm_arm_pmu_set_irq(CPUState *cs, int irq)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

void kvm_arm_pmu_init(CPUState *cs)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}
