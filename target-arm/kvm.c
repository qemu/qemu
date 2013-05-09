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

int kvm_arch_init_vcpu(CPUState *cs)
{
    struct kvm_vcpu_init init;
    int ret;
    uint64_t v;
    struct kvm_one_reg r;

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

#define CP15REG(CRN, CRM, OPC1, OPC2, QEMUFIELD) \
    {                                            \
        KVM_REG_ARM | KVM_REG_SIZE_U32 |         \
        (15 << KVM_REG_ARM_COPROC_SHIFT) |       \
        ((CRN) << KVM_REG_ARM_32_CRN_SHIFT) |    \
        ((CRM) << KVM_REG_ARM_CRM_SHIFT) |       \
        ((OPC1) << KVM_REG_ARM_OPC1_SHIFT) |     \
        ((OPC2) << KVM_REG_ARM_32_OPC2_SHIFT),   \
        offsetof(CPUARMState, QEMUFIELD)         \
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
    /* A non-comprehensive set of cp15 registers.
     * TODO: drive this from the cp_regs hashtable instead.
     */
    CP15REG(1, 0, 0, 0, cp15.c1_sys), /* SCTLR */
    CP15REG(2, 0, 0, 2, cp15.c2_control), /* TTBCR */
    CP15REG(3, 0, 0, 0, cp15.c3), /* DACR */
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
    uint64_t ttbr;

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

    /* TTBR0: cp15 crm=2 opc1=0 */
    ttbr = ((uint64_t)env->cp15.c2_base0_hi << 32) | env->cp15.c2_base0;
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | (15 << KVM_REG_ARM_COPROC_SHIFT) |
        (2 << KVM_REG_ARM_CRM_SHIFT) | (0 << KVM_REG_ARM_OPC1_SHIFT);
    r.addr = (uintptr_t)(&ttbr);
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &r);
    if (ret) {
        return ret;
    }

    /* TTBR1: cp15 crm=2 opc1=1 */
    ttbr = ((uint64_t)env->cp15.c2_base1_hi << 32) | env->cp15.c2_base1;
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | (15 << KVM_REG_ARM_COPROC_SHIFT) |
        (2 << KVM_REG_ARM_CRM_SHIFT) | (1 << KVM_REG_ARM_OPC1_SHIFT);
    r.addr = (uintptr_t)(&ttbr);
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
    uint64_t ttbr;

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

    /* TTBR0: cp15 crm=2 opc1=0 */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | (15 << KVM_REG_ARM_COPROC_SHIFT) |
        (2 << KVM_REG_ARM_CRM_SHIFT) | (0 << KVM_REG_ARM_OPC1_SHIFT);
    r.addr = (uintptr_t)(&ttbr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
    if (ret) {
        return ret;
    }
    env->cp15.c2_base0_hi = ttbr >> 32;
    env->cp15.c2_base0 = ttbr;

    /* TTBR1: cp15 crm=2 opc1=1 */
    r.id = KVM_REG_ARM | KVM_REG_SIZE_U64 | (15 << KVM_REG_ARM_COPROC_SHIFT) |
        (2 << KVM_REG_ARM_CRM_SHIFT) | (1 << KVM_REG_ARM_OPC1_SHIFT);
    r.addr = (uintptr_t)(&ttbr);
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &r);
    if (ret) {
        return ret;
    }
    env->cp15.c2_base1_hi = ttbr >> 32;
    env->cp15.c2_base1 = ttbr;

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

    /* The main GET_ONE_REG loop above set c2_control, but we need to
     * update some extra cached precomputed values too.
     * When this is driven from the cp_regs hashtable then this ugliness
     * can disappear because we'll use the access function which sets
     * these values automatically.
     */
    env->cp15.c2_mask = ~(0xffffffffu >> env->cp15.c2_control);
    env->cp15.c2_base_mask = ~(0x3fffu >> env->cp15.c2_control);

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
