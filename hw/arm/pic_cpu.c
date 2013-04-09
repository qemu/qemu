/*
 * Generic ARM Programmable Interrupt Controller support.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL
 */

#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "sysemu/kvm.h"

/* Input 0 is IRQ and input 1 is FIQ.  */
static void arm_pic_cpu_handler(void *opaque, int irq, int level)
{
    ARMCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    switch (irq) {
    case ARM_PIC_CPU_IRQ:
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        break;
    case ARM_PIC_CPU_FIQ:
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_FIQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_FIQ);
        }
        break;
    default:
        hw_error("arm_pic_cpu_handler: Bad interrupt line %d\n", irq);
    }
}

static void kvm_arm_pic_cpu_handler(void *opaque, int irq, int level)
{
#ifdef CONFIG_KVM
    ARMCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    int kvm_irq = KVM_ARM_IRQ_TYPE_CPU << KVM_ARM_IRQ_TYPE_SHIFT;

    switch (irq) {
    case ARM_PIC_CPU_IRQ:
        kvm_irq |= KVM_ARM_IRQ_CPU_IRQ;
        break;
    case ARM_PIC_CPU_FIQ:
        kvm_irq |= KVM_ARM_IRQ_CPU_FIQ;
        break;
    default:
        hw_error("kvm_arm_pic_cpu_handler: Bad interrupt line %d\n", irq);
    }
    kvm_irq |= cs->cpu_index << KVM_ARM_IRQ_VCPU_SHIFT;
    kvm_set_irq(kvm_state, kvm_irq, level ? 1 : 0);
#endif
}

qemu_irq *arm_pic_init_cpu(ARMCPU *cpu)
{
    if (kvm_enabled()) {
        return qemu_allocate_irqs(kvm_arm_pic_cpu_handler, cpu, 2);
    }
    return qemu_allocate_irqs(arm_pic_cpu_handler, cpu, 2);
}
