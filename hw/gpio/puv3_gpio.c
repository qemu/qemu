/*
 * GPIO device simulation in PKUnity SoC
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#include "hw/hw.h"
#include "hw/sysbus.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"

#define TYPE_PUV3_GPIO "puv3_gpio"
#define PUV3_GPIO(obj) OBJECT_CHECK(PUV3GPIOState, (obj), TYPE_PUV3_GPIO)

typedef struct PUV3GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq[9];

    uint32_t reg_GPLR;
    uint32_t reg_GPDR;
    uint32_t reg_GPIR;
} PUV3GPIOState;

static uint64_t puv3_gpio_read(void *opaque, hwaddr offset,
        unsigned size)
{
    PUV3GPIOState *s = opaque;
    uint32_t ret = 0;

    switch (offset) {
    case 0x00:
        ret = s->reg_GPLR;
        break;
    case 0x04:
        ret = s->reg_GPDR;
        break;
    case 0x20:
        ret = s->reg_GPIR;
        break;
    default:
        DPRINTF("Bad offset 0x%x\n", offset);
    }
    DPRINTF("offset 0x%x, value 0x%x\n", offset, ret);

    return ret;
}

static void puv3_gpio_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    PUV3GPIOState *s = opaque;

    DPRINTF("offset 0x%x, value 0x%x\n", offset, value);
    switch (offset) {
    case 0x04:
        s->reg_GPDR = value;
        break;
    case 0x08:
        if (s->reg_GPDR & value) {
            s->reg_GPLR |= value;
        } else {
            DPRINTF("Write gpio input port error!");
        }
        break;
    case 0x0c:
        if (s->reg_GPDR & value) {
            s->reg_GPLR &= ~value;
        } else {
            DPRINTF("Write gpio input port error!");
        }
        break;
    case 0x10: /* GRER */
    case 0x14: /* GFER */
    case 0x18: /* GEDR */
        break;
    case 0x20: /* GPIR */
        s->reg_GPIR = value;
        break;
    default:
        DPRINTF("Bad offset 0x%x\n", offset);
    }
}

static const MemoryRegionOps puv3_gpio_ops = {
    .read = puv3_gpio_read,
    .write = puv3_gpio_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int puv3_gpio_init(SysBusDevice *dev)
{
    PUV3GPIOState *s = PUV3_GPIO(dev);

    s->reg_GPLR = 0;
    s->reg_GPDR = 0;

    /* FIXME: these irqs not handled yet */
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW0]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW1]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW2]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW3]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW4]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW5]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW6]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOLOW7]);
    sysbus_init_irq(dev, &s->irq[PUV3_IRQS_GPIOHIGH]);

    memory_region_init_io(&s->iomem, OBJECT(s), &puv3_gpio_ops, s, "puv3_gpio",
            PUV3_REGS_OFFSET);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static void puv3_gpio_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = puv3_gpio_init;
}

static const TypeInfo puv3_gpio_info = {
    .name = TYPE_PUV3_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PUV3GPIOState),
    .class_init = puv3_gpio_class_init,
};

static void puv3_gpio_register_type(void)
{
    type_register_static(&puv3_gpio_info);
}

type_init(puv3_gpio_register_type)
