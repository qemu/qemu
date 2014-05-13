/*
 * Allwinner A10 interrupt controller device emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "hw/sysbus.h"
#include "hw/devices.h"
#include "sysemu/sysemu.h"
#include "hw/intc/allwinner-a10-pic.h"

static void aw_a10_pic_update(AwA10PICState *s)
{
    uint8_t i;
    int irq = 0, fiq = 0, pending;

    s->vector = 0;

    for (i = 0; i < AW_A10_PIC_REG_NUM; i++) {
        irq |= s->irq_pending[i] & ~s->mask[i];
        fiq |= s->select[i] & s->irq_pending[i] & ~s->mask[i];

        if (!s->vector) {
            pending = ffs(s->irq_pending[i] & ~s->mask[i]);
            if (pending) {
                s->vector = (i * 32 + pending - 1) * 4;
            }
        }
    }

    qemu_set_irq(s->parent_irq, !!irq);
    qemu_set_irq(s->parent_fiq, !!fiq);
}

static void aw_a10_pic_set_irq(void *opaque, int irq, int level)
{
    AwA10PICState *s = opaque;

    if (level) {
        set_bit(irq % 32, (void *)&s->irq_pending[irq / 32]);
    } else {
        clear_bit(irq % 32, (void *)&s->irq_pending[irq / 32]);
    }
    aw_a10_pic_update(s);
}

static uint64_t aw_a10_pic_read(void *opaque, hwaddr offset, unsigned size)
{
    AwA10PICState *s = opaque;
    uint8_t index = (offset & 0xc) / 4;

    switch (offset) {
    case AW_A10_PIC_VECTOR:
        return s->vector;
    case AW_A10_PIC_BASE_ADDR:
        return s->base_addr;
    case AW_A10_PIC_PROTECT:
        return s->protect;
    case AW_A10_PIC_NMI:
        return s->nmi;
    case AW_A10_PIC_IRQ_PENDING ... AW_A10_PIC_IRQ_PENDING + 8:
        return s->irq_pending[index];
    case AW_A10_PIC_FIQ_PENDING ... AW_A10_PIC_FIQ_PENDING + 8:
        return s->fiq_pending[index];
    case AW_A10_PIC_SELECT ... AW_A10_PIC_SELECT + 8:
        return s->select[index];
    case AW_A10_PIC_ENABLE ... AW_A10_PIC_ENABLE + 8:
        return s->enable[index];
    case AW_A10_PIC_MASK ... AW_A10_PIC_MASK + 8:
        return s->mask[index];
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return 0;
}

static void aw_a10_pic_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    AwA10PICState *s = opaque;
    uint8_t index = (offset & 0xc) / 4;

    switch (offset) {
    case AW_A10_PIC_BASE_ADDR:
        s->base_addr = value & ~0x3;
        break;
    case AW_A10_PIC_PROTECT:
        s->protect = value;
        break;
    case AW_A10_PIC_NMI:
        s->nmi = value;
        break;
    case AW_A10_PIC_IRQ_PENDING ... AW_A10_PIC_IRQ_PENDING + 8:
        /*
         * The register is read-only; nevertheless, Linux (including
         * the version originally shipped by Allwinner) pretends to
         * write to the register. Just ignore it.
         */
        break;
    case AW_A10_PIC_FIQ_PENDING ... AW_A10_PIC_FIQ_PENDING + 8:
        s->fiq_pending[index] &= ~value;
        break;
    case AW_A10_PIC_SELECT ... AW_A10_PIC_SELECT + 8:
        s->select[index] = value;
        break;
    case AW_A10_PIC_ENABLE ... AW_A10_PIC_ENABLE + 8:
        s->enable[index] = value;
        break;
    case AW_A10_PIC_MASK ... AW_A10_PIC_MASK + 8:
        s->mask[index] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    aw_a10_pic_update(s);
}

static const MemoryRegionOps aw_a10_pic_ops = {
    .read = aw_a10_pic_read,
    .write = aw_a10_pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_aw_a10_pic = {
    .name = "a10.pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(vector, AwA10PICState),
        VMSTATE_UINT32(base_addr, AwA10PICState),
        VMSTATE_UINT32(protect, AwA10PICState),
        VMSTATE_UINT32(nmi, AwA10PICState),
        VMSTATE_UINT32_ARRAY(irq_pending, AwA10PICState, AW_A10_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(fiq_pending, AwA10PICState, AW_A10_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(enable, AwA10PICState, AW_A10_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(select, AwA10PICState, AW_A10_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(mask, AwA10PICState, AW_A10_PIC_REG_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void aw_a10_pic_init(Object *obj)
{
    AwA10PICState *s = AW_A10_PIC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

     qdev_init_gpio_in(DEVICE(dev), aw_a10_pic_set_irq, AW_A10_PIC_INT_NR);
     sysbus_init_irq(dev, &s->parent_irq);
     sysbus_init_irq(dev, &s->parent_fiq);
     memory_region_init_io(&s->iomem, OBJECT(s), &aw_a10_pic_ops, s,
                           TYPE_AW_A10_PIC, 0x400);
     sysbus_init_mmio(dev, &s->iomem);
}

static void aw_a10_pic_reset(DeviceState *d)
{
    AwA10PICState *s = AW_A10_PIC(d);
    uint8_t i;

    s->base_addr = 0;
    s->protect = 0;
    s->nmi = 0;
    s->vector = 0;
    for (i = 0; i < AW_A10_PIC_REG_NUM; i++) {
        s->irq_pending[i] = 0;
        s->fiq_pending[i] = 0;
        s->select[i] = 0;
        s->enable[i] = 0;
        s->mask[i] = 0;
    }
}

static void aw_a10_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = aw_a10_pic_reset;
    dc->desc = "allwinner a10 pic";
    dc->vmsd = &vmstate_aw_a10_pic;
 }

static const TypeInfo aw_a10_pic_info = {
    .name = TYPE_AW_A10_PIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AwA10PICState),
    .instance_init = aw_a10_pic_init,
    .class_init = aw_a10_pic_class_init,
};

static void aw_a10_register_types(void)
{
    type_register_static(&aw_a10_pic_info);
}

type_init(aw_a10_register_types);
