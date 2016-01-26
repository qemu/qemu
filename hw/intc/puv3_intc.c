/*
 * INTC device simulation in PKUnity SoC
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"

#define TYPE_PUV3_INTC "puv3_intc"
#define PUV3_INTC(obj) OBJECT_CHECK(PUV3INTCState, (obj), TYPE_PUV3_INTC)

typedef struct PUV3INTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq parent_irq;

    uint32_t reg_ICMR;
    uint32_t reg_ICPR;
} PUV3INTCState;

/* Update interrupt status after enabled or pending bits have been changed.  */
static void puv3_intc_update(PUV3INTCState *s)
{
    if (s->reg_ICMR & s->reg_ICPR) {
        qemu_irq_raise(s->parent_irq);
    } else {
        qemu_irq_lower(s->parent_irq);
    }
}

/* Process a change in an external INTC input. */
static void puv3_intc_handler(void *opaque, int irq, int level)
{
    PUV3INTCState *s = opaque;

    DPRINTF("irq 0x%x, level 0x%x\n", irq, level);
    if (level) {
        s->reg_ICPR |= (1 << irq);
    } else {
        s->reg_ICPR &= ~(1 << irq);
    }
    puv3_intc_update(s);
}

static uint64_t puv3_intc_read(void *opaque, hwaddr offset,
        unsigned size)
{
    PUV3INTCState *s = opaque;
    uint32_t ret = 0;

    switch (offset) {
    case 0x04: /* INTC_ICMR */
        ret = s->reg_ICMR;
        break;
    case 0x0c: /* INTC_ICIP */
        ret = s->reg_ICPR; /* the same value with ICPR */
        break;
    default:
        DPRINTF("Bad offset %x\n", (int)offset);
    }
    DPRINTF("offset 0x%x, value 0x%x\n", offset, ret);
    return ret;
}

static void puv3_intc_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    PUV3INTCState *s = opaque;

    DPRINTF("offset 0x%x, value 0x%x\n", offset, value);
    switch (offset) {
    case 0x00: /* INTC_ICLR */
    case 0x14: /* INTC_ICCR */
        break;
    case 0x04: /* INTC_ICMR */
        s->reg_ICMR = value;
        break;
    default:
        DPRINTF("Bad offset 0x%x\n", (int)offset);
        return;
    }
    puv3_intc_update(s);
}

static const MemoryRegionOps puv3_intc_ops = {
    .read = puv3_intc_read,
    .write = puv3_intc_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int puv3_intc_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    PUV3INTCState *s = PUV3_INTC(dev);

    qdev_init_gpio_in(dev, puv3_intc_handler, PUV3_IRQS_NR);
    sysbus_init_irq(sbd, &s->parent_irq);

    s->reg_ICMR = 0;
    s->reg_ICPR = 0;

    memory_region_init_io(&s->iomem, OBJECT(s), &puv3_intc_ops, s, "puv3_intc",
                          PUV3_REGS_OFFSET);
    sysbus_init_mmio(sbd, &s->iomem);

    return 0;
}

static void puv3_intc_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = puv3_intc_init;
}

static const TypeInfo puv3_intc_info = {
    .name = TYPE_PUV3_INTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PUV3INTCState),
    .class_init = puv3_intc_class_init,
};

static void puv3_intc_register_type(void)
{
    type_register_static(&puv3_intc_info);
}

type_init(puv3_intc_register_type)
