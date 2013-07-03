/*
 * KVM in-kernel PIC (i8259) support
 *
 * Copyright (c) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka          <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 */
#include "hw/isa/i8259_internal.h"
#include "hw/i386/apic_internal.h"
#include "sysemu/kvm.h"

static void kvm_pic_get(PICCommonState *s)
{
    struct kvm_irqchip chip;
    struct kvm_pic_state *kpic;
    int ret;

    chip.chip_id = s->master ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE;
    ret = kvm_vm_ioctl(kvm_state, KVM_GET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_IRQCHIP failed: %s\n", strerror(ret));
        abort();
    }

    kpic = &chip.chip.pic;

    s->last_irr = kpic->last_irr;
    s->irr = kpic->irr;
    s->imr = kpic->imr;
    s->isr = kpic->isr;
    s->priority_add = kpic->priority_add;
    s->irq_base = kpic->irq_base;
    s->read_reg_select = kpic->read_reg_select;
    s->poll = kpic->poll;
    s->special_mask = kpic->special_mask;
    s->init_state = kpic->init_state;
    s->auto_eoi = kpic->auto_eoi;
    s->rotate_on_auto_eoi = kpic->rotate_on_auto_eoi;
    s->special_fully_nested_mode = kpic->special_fully_nested_mode;
    s->init4 = kpic->init4;
    s->elcr = kpic->elcr;
    s->elcr_mask = kpic->elcr_mask;
}

static void kvm_pic_put(PICCommonState *s)
{
    struct kvm_irqchip chip;
    struct kvm_pic_state *kpic;
    int ret;

    chip.chip_id = s->master ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE;

    kpic = &chip.chip.pic;

    kpic->last_irr = s->last_irr;
    kpic->irr = s->irr;
    kpic->imr = s->imr;
    kpic->isr = s->isr;
    kpic->priority_add = s->priority_add;
    kpic->irq_base = s->irq_base;
    kpic->read_reg_select = s->read_reg_select;
    kpic->poll = s->poll;
    kpic->special_mask = s->special_mask;
    kpic->init_state = s->init_state;
    kpic->auto_eoi = s->auto_eoi;
    kpic->rotate_on_auto_eoi = s->rotate_on_auto_eoi;
    kpic->special_fully_nested_mode = s->special_fully_nested_mode;
    kpic->init4 = s->init4;
    kpic->elcr = s->elcr;
    kpic->elcr_mask = s->elcr_mask;

    ret = kvm_vm_ioctl(kvm_state, KVM_SET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_IRQCHIP failed: %s\n", strerror(ret));
        abort();
    }
}

static void kvm_pic_reset(DeviceState *dev)
{
    PICCommonState *s = PIC_COMMON(dev);

    s->elcr = 0;
    pic_reset_common(s);

    kvm_pic_put(s);
}

static void kvm_pic_set_irq(void *opaque, int irq, int level)
{
    int delivered;

    delivered = kvm_set_irq(kvm_state, irq, level);
    apic_report_irq_delivered(delivered);
}

static void kvm_pic_init(PICCommonState *s)
{
    memory_region_init_reservation(&s->base_io, "kvm-pic", 2);
    memory_region_init_reservation(&s->elcr_io, "kvm-elcr", 1);
}

qemu_irq *kvm_i8259_init(ISABus *bus)
{
    i8259_init_chip("kvm-i8259", bus, true);
    i8259_init_chip("kvm-i8259", bus, false);

    return qemu_allocate_irqs(kvm_pic_set_irq, NULL, ISA_NUM_IRQS);
}

static void kvm_i8259_class_init(ObjectClass *klass, void *data)
{
    PICCommonClass *k = PIC_COMMON_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset     = kvm_pic_reset;
    k->init       = kvm_pic_init;
    k->pre_save   = kvm_pic_get;
    k->post_load  = kvm_pic_put;
}

static const TypeInfo kvm_i8259_info = {
    .name  = "kvm-i8259",
    .parent = TYPE_PIC_COMMON,
    .instance_size = sizeof(PICCommonState),
    .class_init = kvm_i8259_class_init,
};

static void kvm_pic_register_types(void)
{
    type_register_static(&kvm_i8259_info);
}

type_init(kvm_pic_register_types)
