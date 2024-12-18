/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

static void loongarch_pic_common_realize(DeviceState *dev, Error **errp)
{
    LoongArchPICCommonState *s = LOONGARCH_PCH_PIC(dev);

    if (!s->irq_num || s->irq_num  > VIRT_PCH_PIC_IRQ_NUM) {
        error_setg(errp, "Invalid 'pic_irq_num'");
        return;
    }
}

static const Property loongarch_pic_common_properties[] = {
    DEFINE_PROP_UINT32("pch_pic_irq_num", LoongArchPICCommonState, irq_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_loongarch_pic_common = {
    .name = "loongarch_pch_pic",
    .version_id = 1,
    .minimum_version_id = 1,
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
