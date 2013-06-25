/*
 * ARM implementation of KVM hooks
 *
 * Copyright Christoffer Dall 2009-2010
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "cpu.h"
#include "hw/arm/arm.h"

/* Check that cpu.h's idea of coprocessor fields matches KVM's */
#if (CP_REG_SIZE_SHIFT != KVM_REG_SIZE_SHIFT) || \
    (CP_REG_SIZE_MASK != KVM_REG_SIZE_MASK) ||   \
    (CP_REG_SIZE_U32 != KVM_REG_SIZE_U32) || \
    (CP_REG_SIZE_U64 != KVM_REG_SIZE_U64) || \
    (CP_REG_ARM != KVM_REG_ARM)
#error mismatch between cpu.h and KVM header definitions
#endif

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

int kvm_arch_init(KVMState *s)
{
    /* For ARM interrupt delivery is always asynchronous,
     * whether we are using an in-kernel VGIC or not.
     */
    kvm_async_interrupts_allowed = true;
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

static bool reg_syncs_via_tuple_list(uint64_t regidx)
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

static int compare_u64(const void *a, const void *b)
{
    return *(uint64_t *)a - *(uint64_t *)b;
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    struct kvm_vcpu_init init;
    int i, ret, arraylen;
    uint64_t v;
    struct kvm_one_reg r;
    struct kvm_reg_list rl;
    struct kvm_reg_list *rlp;
    ARMCPU *cpu = ARM_CPU(cs);

    init.target = KVM_ARM_TARGET_CORTEX_A15;
    memset(init.features, 0, sizeof(init.features));
    ret = kvm_vcpu_ioctl(cs, KVM_ARM_VCPU_INIT, &init);
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

    /* Populate the cpreg list based on the kernel's idea
     * of what registers exist (and throw away the TCG-created list).
     */
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
        if (!reg_syncs_via_tuple_list(rlp->reg[i])) {
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
        if (!reg_syncs_via_tuple_list(regidx)) {
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

    /* Save a copy of the initial register values so that we can
     * feed it back to the kernel on VCPU reset.
     */
    cpu->cpreg_reset_values = g_memdup(cpu->cpreg_values,
                                       cpu->cpreg_array_len *
                                       sizeof(cpu->cpreg_values[0]));

out:
    g_free(rlp);
    return ret;
}

/* We track all the KVM devices which need their memory addresses
 * passing to the kernel in a list of these structures.
 * When board init is complete we run through the list and
 * tell the kernel the base addresses of the memory regions.
 * We use a MemoryListener to track mapping and unmapping of
 * the regions during board creation, so the board models don't
 * need to do anything special for the KVM case.
 */
typedef struct KVMDevice {
    struct kvm_arm_device_addr kda;
    MemoryRegion *mr;
    QSLIST_ENTRY(KVMDevice) entries;
} KVMDevice;

static QSLIST_HEAD(kvm_devices_head, KVMDevice) kvm_devices_head;

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
    .region_add = kvm_arm_devlistener_add,
    .region_del = kvm_arm_devlistener_del,
};

static void kvm_arm_machine_init_done(Notifier *notifier, void *data)
{
    KVMDevice *kd, *tkd;

    memory_listener_unregister(&devlistener);
    QSLIST_FOREACH_SAFE(kd, &kvm_devices_head, entries, tkd) {
        if (kd->kda.addr != -1) {
            if (kvm_vm_ioctl(kvm_state, KVM_ARM_SET_DEVICE_ADDR,
                             &kd->kda) < 0) {
                fprintf(stderr, "KVM_ARM_SET_DEVICE_ADDRESS failed: %s\n",
                        strerror(errno));
                abort();
            }
        }
        g_free(kd);
    }
}

static Notifier notify = {
    .notify = kvm_arm_machine_init_done,
};

void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid)
{
    KVMDevice *kd;

    if (!kvm_irqchip_in_kernel()) {
        return;
    }

    if (QSLIST_EMPTY(&kvm_devices_head)) {
        memory_listener_register(&devlistener, NULL);
        qemu_add_machine_init_done_notifier(&notify);
    }
    kd = g_new0(KVMDevice, 1);
    kd->mr = mr;
    kd->kda.id = devid;
    kd->kda.addr = -1;
    QSLIST_INSERT_HEAD(&kvm_devices_head, kd, entries);
}

bool write_kvmstate_to_list(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        struct kvm_one_reg r;
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        r.id = regidx;

        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            r.addr = (uintptr_t)&v32;
            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
            if (!ret) {
                cpu->cpreg_values[i] = v32;
            }
            break;
        case KVM_REG_SIZE_U64:
            r.addr = (uintptr_t)(cpu->cpreg_values + i);
            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
            break;
        default:
            abort();
        }
        if (ret) {
            ok = false;
        }
    }
    return ok;
}

bool write_list_to_kvmstate(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        struct kvm_one_reg r;
        uint64_t regidx = cpu->cpreg_indexes[i];
        uint32_t v32;
        int ret;

        r.id = regidx;
        switch (regidx & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            v32 = cpu->cpreg_values[i];
            r.addr = (uintptr_t)&v32;
            break;
        case KVM_REG_SIZE_U64:
            r.addr = (uintptr_t)(cpu->cpreg_values + i);
            break;
        default:
            abort();
        }
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
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
    COREREG(usr_regs.uregs[13], banked_r13[0]),
    COREREG(usr_regs.uregs[14], banked_r14[0]),
    /* R13, R14, SPSR for SVC, ABT, UND, IRQ banks */
    COREREG(svc_regs[0], banked_r13[1]),
    COREREG(svc_regs[1], banked_r14[1]),
    COREREG(svc_regs[2], banked_spsr[1]),
    COREREG(abt_regs[0], banked_r13[2]),
    COREREG(abt_regs[1], banked_r14[2]),
    COREREG(abt_regs[2], banked_spsr[2]),
    COREREG(und_regs[0], banked_r13[3]),
    COREREG(und_regs[1], banked_r14[3]),
    COREREG(und_regs[2], banked_spsr[3]),
    COREREG(irq_regs[0], banked_r13[4]),
    COREREG(irq_regs[1], banked_r14[4]),
    COREREG(irq_regs[2], banked_spsr[4]),
    /* R8_fiq .. R14_fiq and SPSR_fiq */
    COREREG(fiq_regs[0], fiq_regs[0]),
    COREREG(fiq_regs[1], fiq_regs[1]),
    COREREG(fiq_regs[2], fiq_regs[2]),
    COREREG(fiq_regs[3], fiq_regs[3]),
    COREREG(fiq_regs[4], fiq_regs[4]),
    COREREG(fiq_regs[5], banked_r13[5]),
    COREREG(fiq_regs[6], banked_r14[5]),
    COREREG(fiq_regs[7], banked_spsr[5]),
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
    env->banked_r14[bn] = env->regs[14];
    env->banked_spsr[bn] = env->spsr;

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
        r.addr = (uintptr_t)(&env->vfp.regs[i]);
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
    if (!write_list_to_kvmstate(cpu)) {
        return EINVAL;
    }

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
    cpsr_write(env, cpsr, 0xffffffff);

    /* Make sure the current mode regs are properly set */
    mode = env->uncached_cpsr & CPSR_M;
    bn = bank_number(mode);
    if (mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    } else {
        memcpy(env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    }
    env->regs[13] = env->banked_r13[bn];
    env->regs[14] = env->banked_r14[bn];
    env->spsr = env->banked_spsr[bn];

    /* VFP registers */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | KVM_REG_ARM_VFP;
    for (i = 0; i < 32; i++) {
        r.addr = (uintptr_t)(&env->vfp.regs[i]);
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

    if (!write_kvmstate_to_list(cpu)) {
        return EINVAL;
    }
    /* Note that it's OK to have registers which aren't in CPUState,
     * so we can ignore a failure return here.
     */
    write_list_to_cpustate(cpu);

    return 0;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    return 0;
}

void kvm_arch_reset_vcpu(CPUState *cs)
{
    /* Feed the kernel back its initial register state */
    ARMCPU *cpu = ARM_CPU(cs);

    memmove(cpu->cpreg_values, cpu->cpreg_reset_values,
            cpu->cpreg_array_len * sizeof(cpu->cpreg_values[0]));

    if (!write_list_to_kvmstate(cpu)) {
        abort();
    }
}

bool kvm_arch_stop_on_emulation_error(CPUState *cs)
{
    return true;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return 0;
}

int kvm_arch_on_sigbus_vcpu(CPUState *cs, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs,
                                  struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
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

int kvm_arch_remove_sw_breakpoint(CPUState *cs,
                                  struct kvm_sw_breakpoint *bp)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
    return -EINVAL;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}
