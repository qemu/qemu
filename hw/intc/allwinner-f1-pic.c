/*
 * Allwinner F1 interrupt controller device emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 * Copyright (C) 2022 froloff 
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

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/arm/allwinner-f1.h"
#include "hw/intc/allwinner-f1-pic.h"

static void aw_f1_pic_update(AwF1PICState *s)
{
    uint8_t i;
    int irq = 0, fiq = 0;

    s->vector = 0;

    for (i = 0; i < AW_F1_PIC_REG_NUM; i++) {
        uint32_t value = s->fast_forcing[i] | (s->pending[i] & ~s->mask[i]);
        irq |= value;

        // TODO: check priority
        if (!s->vector) {
            int zeroes;
            zeroes = ctz32(value);
            if (zeroes != 32) {
                s->vector = (i * 32 + zeroes) * 4;
            }
        }
    }
    fiq = irq & 1; 
    irq >>= 1; 

    qemu_set_irq(s->parent_irq, irq != 0);
    qemu_set_irq(s->parent_fiq, fiq != 0);
}

static void aw_f1_pic_set_irq(void *opaque, int irq, int level)
{
    AwF1PICState *s = opaque;

    if (level) {
        if (s->enable[irq / 32] & (1UL << (irq % 32)))
            set_bit(irq % 32, (void *)&s->pending[irq / 32]);
    } else {
        clear_bit(irq % 32, (void *)&s->pending[irq / 32]);
    }
    aw_f1_pic_update(s);
}

static uint64_t aw_f1_pic_read(void *opaque, hwaddr offset, unsigned size)
{
    AwF1PICState *s = opaque;
    uint8_t index = (offset & 0x0c) / 4;

    switch (offset) {
    case AW_F1_PIC_VECTOR:
        return s->vector;
    case AW_F1_PIC_BASE_ADDR:
        return s->base_addr;
    case AW_F1_PIC_INT_CTRL:
        return s->nmi_int_ctrl;
    case AW_F1_PIC_PEND ... AW_F1_PIC_PEND + 4:
        return s->pending[index];
    case AW_F1_PIC_EN ... AW_F1_PIC_EN + 4:
        return s->enable[index];
    case AW_F1_PIC_MASK ... AW_F1_PIC_MASK + 4:
        return s->mask[index];
    case AW_F1_PIC_RESP ... AW_F1_PIC_RESP + 4:
        return s->response[index];
    case AW_F1_PIC_FF ... AW_F1_PIC_FF + 4:
        return s->fast_forcing[index];
    case AW_F1_PIC_PRIO ... AW_F1_PIC_PRIO + 12:
        return s->priority[index];
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return 0;
}

static void aw_f1_pic_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    AwF1PICState *s = opaque;
    uint8_t index = (offset & 0xc) / 4;

    switch (offset) {
    case AW_F1_PIC_BASE_ADDR:
        s->base_addr = value & ~0x3;
        break;
    case AW_F1_PIC_INT_CTRL:
        s->nmi_int_ctrl = value;
        break;
    case AW_F1_PIC_PEND ... AW_F1_PIC_PEND + 4:
        /*
         * The register is read-only; nevertheless, Linux (including
         * the version originally shipped by Allwinner) pretends to
         * write to the register. Just ignore it.
         */
        break;
    case AW_F1_PIC_EN ... AW_F1_PIC_EN + 4:
        s->enable[index] = value;
        break;
    case AW_F1_PIC_MASK ... AW_F1_PIC_MASK + 4:
        s->mask[index] = value;
        break;
    case AW_F1_PIC_RESP ... AW_F1_PIC_RESP + 4:
        s->response[index] &= ~value;
        break;
    case AW_F1_PIC_FF ... AW_F1_PIC_FF + 4:
        s->fast_forcing[index] = s->enable[index] & value;
        s->pending[index] |= s->fast_forcing[index];
        break;
    case AW_F1_PIC_PRIO ... AW_F1_PIC_PRIO + 12:
        s->priority[index] = (uint32_t)value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    aw_f1_pic_update(s);
}

static const MemoryRegionOps aw_f1_pic_ops = {
    .read = aw_f1_pic_read,
    .write = aw_f1_pic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_aw_f1_pic = {
    .name = "f1.pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(vector, AwF1PICState),
        VMSTATE_UINT32(base_addr, AwF1PICState),
        VMSTATE_UINT32(nmi_int_ctrl, AwF1PICState),
        VMSTATE_UINT32_ARRAY(pending, AwF1PICState, AW_F1_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(enable, AwF1PICState, AW_F1_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(mask, AwF1PICState, AW_F1_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(response, AwF1PICState, AW_F1_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(fast_forcing, AwF1PICState, AW_F1_PIC_REG_NUM),
        VMSTATE_UINT32_ARRAY(priority, AwF1PICState, AW_F1_PIC_PRI_REG_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void aw_f1_pic_init(Object *obj)
{
    AwF1PICState *s = AW_F1_PIC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(DEVICE(dev), aw_f1_pic_set_irq, AW_F1_PIC_INT_NR);
    sysbus_init_irq(dev, &s->parent_irq);
    sysbus_init_irq(dev, &s->parent_fiq);
    memory_region_init_io(&s->iomem, OBJECT(s), &aw_f1_pic_ops, s,
                       TYPE_AW_F1_PIC, 0x400);
    sysbus_init_mmio(dev, &s->iomem);
}

static void aw_f1_pic_reset(DeviceState *d)
{
    AwF1PICState *s = AW_F1_PIC(d);    
    AwF1State *soc = AW_F1(s->parent_obj.parent_obj.parent_obj.parent);
    uint8_t i;

    s->vector = 0;
    s->base_addr = 0x00000000;        
    s->nmi_int_ctrl = 0;
    for (i = 0; i < AW_F1_PIC_REG_NUM; i++) {
        s->pending[i] = 0;
        s->enable[i] = 0;
        s->mask[i] = 0;
        s->response[i] = 0;
        s->fast_forcing[i] = 0;
    }
    for (i = 0; i < AW_F1_PIC_PRI_REG_NUM; i++) {
        s->priority[i] = 0;
    }
    cpu_set_pc(CPU(&soc->cpu), s->reset_addr);
}

static void aw_f1_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = aw_f1_pic_reset;
    dc->desc = "allwinner f1 pic";
    dc->vmsd = &vmstate_aw_f1_pic;
 }

static const TypeInfo aw_f1_pic_info = {
    .name = TYPE_AW_F1_PIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AwF1PICState),
    .instance_init = aw_f1_pic_init,
    .class_init = aw_f1_pic_class_init,
};

static void aw_f1_register_types(void)
{
    type_register_static(&aw_f1_pic_info);
}

type_init(aw_f1_register_types);
