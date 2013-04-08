/*
 * ARM Generic Interrupt Controller using KVM in-kernel support
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "gic_internal.h"

#define TYPE_KVM_ARM_GIC "kvm-arm-gic"
#define KVM_ARM_GIC(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_KVM_ARM_GIC)
#define KVM_ARM_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(KVMARMGICClass, (klass), TYPE_KVM_ARM_GIC)
#define KVM_ARM_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(KVMARMGICClass, (obj), TYPE_KVM_ARM_GIC)

typedef struct KVMARMGICClass {
    ARMGICCommonClass parent_class;
    DeviceRealize parent_realize;
    void (*parent_reset)(DeviceState *dev);
} KVMARMGICClass;

static void kvm_arm_gic_set_irq(void *opaque, int irq, int level)
{
    /* Meaning of the 'irq' parameter:
     *  [0..N-1] : external interrupts
     *  [N..N+31] : PPI (internal) interrupts for CPU 0
     *  [N+32..N+63] : PPI (internal interrupts for CPU 1
     *  ...
     * Convert this to the kernel's desired encoding, which
     * has separate fields in the irq number for type,
     * CPU number and interrupt number.
     */
    GICState *s = (GICState *)opaque;
    int kvm_irq, irqtype, cpu;

    if (irq < (s->num_irq - GIC_INTERNAL)) {
        /* External interrupt. The kernel numbers these like the GIC
         * hardware, with external interrupt IDs starting after the
         * internal ones.
         */
        irqtype = KVM_ARM_IRQ_TYPE_SPI;
        cpu = 0;
        irq += GIC_INTERNAL;
    } else {
        /* Internal interrupt: decode into (cpu, interrupt id) */
        irqtype = KVM_ARM_IRQ_TYPE_PPI;
        irq -= (s->num_irq - GIC_INTERNAL);
        cpu = irq / GIC_INTERNAL;
        irq %= GIC_INTERNAL;
    }
    kvm_irq = (irqtype << KVM_ARM_IRQ_TYPE_SHIFT)
        | (cpu << KVM_ARM_IRQ_VCPU_SHIFT) | irq;

    kvm_set_irq(kvm_state, kvm_irq, !!level);
}

static void kvm_arm_gic_put(GICState *s)
{
    /* TODO: there isn't currently a kernel interface to set the GIC state */
}

static void kvm_arm_gic_get(GICState *s)
{
    /* TODO: there isn't currently a kernel interface to get the GIC state */
}

static void kvm_arm_gic_reset(DeviceState *dev)
{
    GICState *s = ARM_GIC_COMMON(dev);
    KVMARMGICClass *kgc = KVM_ARM_GIC_GET_CLASS(s);

    kgc->parent_reset(dev);
    kvm_arm_gic_put(s);
}

static void kvm_arm_gic_realize(DeviceState *dev, Error **errp)
{
    int i;
    GICState *s = KVM_ARM_GIC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    KVMARMGICClass *kgc = KVM_ARM_GIC_GET_CLASS(s);

    kgc->parent_realize(dev, errp);
    if (error_is_set(errp)) {
        return;
    }

    i = s->num_irq - GIC_INTERNAL;
    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] SPIs
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    i += (GIC_INTERNAL * s->num_cpu);
    qdev_init_gpio_in(dev, kvm_arm_gic_set_irq, i);
    /* We never use our outbound IRQ lines but provide them so that
     * we maintain the same interface as the non-KVM GIC.
     */
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    /* Distributor */
    memory_region_init_reservation(&s->iomem, "kvm-gic_dist", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    kvm_arm_register_device(&s->iomem,
                            (KVM_ARM_DEVICE_VGIC_V2 << KVM_ARM_DEVICE_ID_SHIFT)
                            | KVM_VGIC_V2_ADDR_TYPE_DIST);
    /* CPU interface for current core. Unlike arm_gic, we don't
     * provide the "interface for core #N" memory regions, because
     * cores with a VGIC don't have those.
     */
    memory_region_init_reservation(&s->cpuiomem[0], "kvm-gic_cpu", 0x1000);
    sysbus_init_mmio(sbd, &s->cpuiomem[0]);
    kvm_arm_register_device(&s->cpuiomem[0],
                            (KVM_ARM_DEVICE_VGIC_V2 << KVM_ARM_DEVICE_ID_SHIFT)
                            | KVM_VGIC_V2_ADDR_TYPE_CPU);
}

static void kvm_arm_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICCommonClass *agcc = ARM_GIC_COMMON_CLASS(klass);
    KVMARMGICClass *kgc = KVM_ARM_GIC_CLASS(klass);

    agcc->pre_save = kvm_arm_gic_get;
    agcc->post_load = kvm_arm_gic_put;
    kgc->parent_realize = dc->realize;
    kgc->parent_reset = dc->reset;
    dc->realize = kvm_arm_gic_realize;
    dc->reset = kvm_arm_gic_reset;
    dc->no_user = 1;
}

static const TypeInfo kvm_arm_gic_info = {
    .name = TYPE_KVM_ARM_GIC,
    .parent = TYPE_ARM_GIC_COMMON,
    .instance_size = sizeof(GICState),
    .class_init = kvm_arm_gic_class_init,
    .class_size = sizeof(KVMARMGICClass),
};

static void kvm_arm_gic_register_types(void)
{
    type_register_static(&kvm_arm_gic_info);
}

type_init(kvm_arm_gic_register_types)
