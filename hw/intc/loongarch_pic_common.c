/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_pic_common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

static int loongarch_pic_pre_save(void *opaque)
{
    LoongArchPICCommonState *s = (LoongArchPICCommonState *)opaque;
    LoongArchPICCommonClass *lpcc = LOONGARCH_PIC_COMMON_GET_CLASS(s);

    if (lpcc->pre_save) {
        return lpcc->pre_save(s);
    }

    return 0;
}

static int loongarch_pic_post_load(void *opaque, int version_id)
{
    LoongArchPICCommonState *s = (LoongArchPICCommonState *)opaque;
    LoongArchPICCommonClass *lpcc = LOONGARCH_PIC_COMMON_GET_CLASS(s);

    if (lpcc->post_load) {
        return lpcc->post_load(s, version_id);
    }

    return 0;
}

static void loongarch_pic_common_realize(DeviceState *dev, Error **errp)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(dev);

    if (!s->irq_num || s->irq_num  > VIRT_PCH_PIC_IRQ_NUM) {
        error_setg(errp, "Invalid 'pic_irq_num'");
        return;
    }
}

static void loongarch_pic_common_reset_hold(Object *obj, ResetType type)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(obj);
    int i;

    /*
     * With Loongson 7A1000 user manual
     * Chapter 5.2 "Description of Interrupt-related Registers"
     *
     * Interrupt controller identification register 1
     *   Bit 24-31 Interrupt Controller ID
     * Interrupt controller identification register 2
     *   Bit  0-7  Interrupt Controller version number
     *   Bit 16-23 The number of interrupt sources supported
     */
    s->id.desc.id = PCH_PIC_INT_ID_VAL;
    s->id.desc.version = PCH_PIC_INT_ID_VER;
    s->id.desc.irq_num = s->irq_num - 1;
    s->int_mask = UINT64_MAX;
    s->htmsi_en = 0x0;
    s->intedge  = 0x0;
    s->intclr   = 0x0;
    s->auto_crtl0 = 0x0;
    s->auto_crtl1 = 0x0;
    for (i = 0; i < 64; i++) {
        s->route_entry[i] = 0x1;
        s->htmsi_vector[i] = 0x0;
    }
    s->intirr = 0x0;
    s->intisr = 0x0;
    s->last_intirr = 0x0;
    s->int_polarity = 0x0;
}

static const Property loongarch_pic_common_properties[] = {
    DEFINE_PROP_UINT32("pch_pic_irq_num", LoongArchPICCommonState, irq_num, 0),
};

static const VMStateDescription vmstate_loongarch_pic_common = {
    .name = "loongarch_pch_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save  = loongarch_pic_pre_save,
    .post_load = loongarch_pic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(int_mask, LoongArchPICCommonState),
        VMSTATE_UINT64(htmsi_en, LoongArchPICCommonState),
        VMSTATE_UINT64(intedge, LoongArchPICCommonState),
        VMSTATE_UINT64(intclr, LoongArchPICCommonState),
        VMSTATE_UINT64(auto_crtl0, LoongArchPICCommonState),
        VMSTATE_UINT64(auto_crtl1, LoongArchPICCommonState),
        VMSTATE_UINT8_ARRAY(route_entry, LoongArchPICCommonState, 64),
        VMSTATE_UINT8_ARRAY(htmsi_vector, LoongArchPICCommonState, 64),
        VMSTATE_UINT64(last_intirr, LoongArchPICCommonState),
        VMSTATE_UINT64(intirr, LoongArchPICCommonState),
        VMSTATE_UINT64(intisr, LoongArchPICCommonState),
        VMSTATE_UINT64(int_polarity, LoongArchPICCommonState),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_pic_common_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchPICCommonClass *lpcc = LOONGARCH_PIC_COMMON_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_pic_common_realize,
                                    &lpcc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL,
                                       loongarch_pic_common_reset_hold,
                                       NULL, &lpcc->parent_phases);
    device_class_set_props(dc, loongarch_pic_common_properties);
    dc->vmsd = &vmstate_loongarch_pic_common;
}

static const TypeInfo loongarch_pic_common_types[] = {
    {
        .name               = TYPE_LOONGARCH_PIC_COMMON,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(LoongArchPICCommonState),
        .class_size         = sizeof(LoongArchPICCommonClass),
        .class_init         = loongarch_pic_common_class_init,
        .abstract           = true,
    }
};

DEFINE_TYPES(loongarch_pic_common_types)
