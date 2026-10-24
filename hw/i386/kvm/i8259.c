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
#include "hw/core/qdev-properties.h"
#include "system/kvm.h"
#include "qapi/error.h"
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

#define TYPE_KVM_I8259_PIC "kvm-i8259-pic"

struct KVMI8259PICState {
    DeviceState parent_obj;

    ISABus *isabus;
    KVMI8259State i8259[2];

    int irq_level[ISA_NUM_IRQS];
    uint64_t irq_count[ISA_NUM_IRQS];
};

OBJECT_DECLARE_SIMPLE_TYPE(KVMI8259PICState, KVM_I8259_PIC)


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

static bool kvm_i8259_pic_get_statistics(InterruptStatsProvider *obj,
                                         uint64_t **irq_counts,
                                         unsigned int *nb_irqs)
{
    KVMI8259PICState *s = KVM_I8259_PIC(obj);

    *irq_counts = s->irq_count;
    *nb_irqs = ARRAY_SIZE(s->irq_count);

    return true;
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
    KVMI8259PICState *s = opaque;
    int delivered;

    i8259_stat_update_irq(s->irq_count, s->irq_level, irq, level);
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
    qemu_irq *irq_set;
    DeviceState *dev;
    int i;

    irq_set = g_new0(qemu_irq, ISA_NUM_IRQS);

    dev = qdev_new(TYPE_KVM_I8259_PIC);
    object_property_set_link(OBJECT(dev), "bus", OBJECT(bus), &error_fatal);
    qdev_realize_and_unref(dev, NULL, &error_fatal);

    for (i = 0 ; i < ISA_NUM_IRQS; i++) {
        irq_set[i] = qdev_get_gpio_in(dev, i);
    }

    return irq_set;
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


static void kvm_i8259_pic_init(Object *obj)
{
    KVMI8259PICState *s = KVM_I8259_PIC(obj);

    object_initialize_child(obj, "primary", &s->i8259[0], TYPE_KVM_I8259);
    object_initialize_child(obj, "secondary", &s->i8259[1], TYPE_KVM_I8259);

    qdev_init_gpio_in(DEVICE(obj), kvm_pic_set_irq, ISA_NUM_IRQS);
}

static void kvm_i8259_pic_realize(DeviceState *dev, Error **errp)
{
    KVMI8259PICState *s = KVM_I8259_PIC(dev);
    DeviceState *pri_dev, *sec_dev;

    /* Primary */
    pri_dev = DEVICE(&s->i8259[0]);
    qdev_prop_set_uint32(pri_dev, "iobase", 0x20);
    qdev_prop_set_uint32(pri_dev, "elcr_addr", 0x4d0);
    qdev_prop_set_uint8(pri_dev, "elcr_mask", 0xf8);
    qdev_prop_set_bit(pri_dev, "master", 1);
    if (!isa_realize(ISA_DEVICE(pri_dev), s->isabus, errp)) {
        return;
    }

    /* Secondary */
    sec_dev = DEVICE(&s->i8259[1]);
    qdev_prop_set_uint32(sec_dev, "iobase", 0xa0);
    qdev_prop_set_uint32(sec_dev, "elcr_addr", 0x4d1);
    qdev_prop_set_uint8(sec_dev, "elcr_mask", 0xde);
    if (!isa_realize(ISA_DEVICE(sec_dev), s->isabus, errp)) {
        return;
    }
}

static const Property kvm_i8259_pic_properties[] = {
    DEFINE_PROP_LINK("bus", KVMI8259PICState, isabus, TYPE_ISA_BUS,
                     ISABus *),
};

static void kvm_i8259_pic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(klass);

    dc->realize = kvm_i8259_pic_realize;
    device_class_set_props(dc, kvm_i8259_pic_properties);
    ic->get_statistics = kvm_i8259_pic_get_statistics;
    ic->print_info = i8259_pic_print_info;
    /*
     * Reason: must be wired to the ISA bus via the "bus" property
     */
    dc->user_creatable = false;
}

static const TypeInfo kvm_i8259_type_infos[] = {
    {
        .name = TYPE_KVM_I8259,
        .parent = TYPE_I8259_COMMON,
        .instance_size = sizeof(KVMI8259State),
        .class_init = kvm_i8259_class_init,
        .class_size = sizeof(KVMI8259Class),
    },
    {
        .name = TYPE_KVM_I8259_PIC,
        .parent = TYPE_DEVICE,
        .class_init = kvm_i8259_pic_class_init,
        .instance_init = kvm_i8259_pic_init,
        .instance_size = sizeof(KVMI8259PICState),
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_INTERRUPT_STATS_PROVIDER },
            { }
        },
    },
};

DEFINE_TYPES(kvm_i8259_type_infos)
