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

#include "qemu/osdep.h"
#include "hw/isa/i8259_internal.h"
#include "hw/intc/i8259.h"
#include "qemu/module.h"
#include "hw/intc/kvm_irqcount.h"
#include "hw/core/irq.h"
#include "system/kvm.h"
#include "qom/object.h"

#define TYPE_KVM_I8259 "kvm-i8259"
OBJECT_DECLARE_TYPE(KVMI8259State, KVMI8259Class, KVM_I8259)

struct KVMI8259State {
    I8259CommonState parent_obj;
};

/**
 * KVMI8259Class:
 * @parent_realize: The parent's realizefn.
 */
struct KVMI8259Class {
    I8259CommonClass parent_class;

    DeviceRealize parent_realize;
};

static void kvm_i8259_get(I8259CommonState *cs)
{
    struct kvm_irqchip chip;
    struct kvm_pic_state *kpic;
    int ret;

    chip.chip_id = cs->master ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE;
    ret = kvm_vm_ioctl(kvm_state, KVM_GET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_IRQCHIP failed: %s\n", strerror(-ret));
        abort();
    }

    kpic = &chip.chip.pic;

    cs->last_irr = kpic->last_irr;
    cs->irr = kpic->irr;
    cs->imr = kpic->imr;
    cs->isr = kpic->isr;
    cs->priority_add = kpic->priority_add;
    cs->irq_base = kpic->irq_base;
    cs->read_reg_select = kpic->read_reg_select;
    cs->poll = kpic->poll;
    cs->special_mask = kpic->special_mask;
    cs->init_state = kpic->init_state;
    cs->auto_eoi = kpic->auto_eoi;
    cs->rotate_on_auto_eoi = kpic->rotate_on_auto_eoi;
    cs->special_fully_nested_mode = kpic->special_fully_nested_mode;
    cs->init4 = kpic->init4;
    cs->elcr = kpic->elcr;
    cs->elcr_mask = kpic->elcr_mask;
}

static void kvm_i8259_put(I8259CommonState *cs)
{
    struct kvm_irqchip chip;
    struct kvm_pic_state *kpic;
    int ret;

    chip.chip_id = cs->master ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE;

    kpic = &chip.chip.pic;

    kpic->last_irr = cs->last_irr;
    kpic->irr = cs->irr;
    kpic->imr = cs->imr;
    kpic->isr = cs->isr;
    kpic->priority_add = cs->priority_add;
    kpic->irq_base = cs->irq_base;
    kpic->read_reg_select = cs->read_reg_select;
    kpic->poll = cs->poll;
    kpic->special_mask = cs->special_mask;
    kpic->init_state = cs->init_state;
    kpic->auto_eoi = cs->auto_eoi;
    kpic->rotate_on_auto_eoi = cs->rotate_on_auto_eoi;
    kpic->special_fully_nested_mode = cs->special_fully_nested_mode;
    kpic->init4 = cs->init4;
    kpic->elcr = cs->elcr;
    kpic->elcr_mask = cs->elcr_mask;

    ret = kvm_vm_ioctl(kvm_state, KVM_SET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_SET_IRQCHIP failed: %s\n", strerror(-ret));
        abort();
    }
}

static void kvm_i8259_reset(DeviceState *dev)
{
    I8259CommonState *cs = I8259_COMMON(dev);

    cs->elcr = 0;
    i8259_common_reset(cs);

    kvm_i8259_put(cs);
}

static void kvm_pic_set_irq(void *opaque, int irq, int level)
{
    int delivered;

    i8259_stat_update_irq(irq, level);
    delivered = kvm_set_irq(kvm_state, irq, level);
    kvm_report_irq_delivered(delivered);
}

static void kvm_i8259_realize(DeviceState *dev, Error **errp)
{
    I8259CommonState *cs = I8259_COMMON(dev);
    KVMI8259Class *kpc = KVM_I8259_GET_CLASS(dev);

    memory_region_init_io(&cs->base_io, OBJECT(dev), NULL, NULL, "kvm-pic", 2);
    memory_region_init_io(&cs->elcr_io, OBJECT(dev), NULL, NULL, "kvm-elcr", 1);

    kpc->parent_realize(dev, errp);
}

qemu_irq *kvm_i8259_init(ISABus *bus)
{
    i8259_init_chip(TYPE_KVM_I8259, bus, true);
    i8259_init_chip(TYPE_KVM_I8259, bus, false);

    return qemu_allocate_irqs(kvm_pic_set_irq, NULL, ISA_NUM_IRQS);
}

static void kvm_i8259_class_init(ObjectClass *klass, const void *data)
{
    KVMI8259Class *kpc = KVM_I8259_CLASS(klass);
    I8259CommonClass *k = I8259_COMMON_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, kvm_i8259_reset);
    device_class_set_parent_realize(dc, kvm_i8259_realize,
                                    &kpc->parent_realize);
    k->pre_save   = kvm_i8259_get;
    k->post_load  = kvm_i8259_put;
}

static const TypeInfo kvm_i8259_info = {
    .name = TYPE_KVM_I8259,
    .parent = TYPE_I8259_COMMON,
    .instance_size = sizeof(KVMI8259State),
    .class_init = kvm_i8259_class_init,
    .class_size = sizeof(KVMI8259Class),
};

static void kvm_i8259_register_types(void)
{
    type_register_static(&kvm_i8259_info);
}

type_init(kvm_i8259_register_types)
