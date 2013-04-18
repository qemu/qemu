/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"

typedef struct {
    SysBusDevice busdev;
    DeviceState *gic;
    MemoryRegion container;
} RealViewGICState;

static void realview_gic_set_irq(void *opaque, int irq, int level)
{
    RealViewGICState *s = (RealViewGICState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static int realview_gic_init(SysBusDevice *dev)
{
    RealViewGICState *s = FROM_SYSBUS(RealViewGICState, dev);
    SysBusDevice *busdev;
    /* The GICs on the RealView boards have a fixed nonconfigurable
     * number of interrupt lines, so we don't need to expose this as
     * a qdev property.
     */
    int numirq = 96;

    s->gic = qdev_create(NULL, "arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", 1);
    qdev_prop_set_uint32(s->gic, "num-irq", numirq);
    qdev_init_nofail(s->gic);
    busdev = SYS_BUS_DEVICE(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, busdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, realview_gic_set_irq, numirq - 32);

    memory_region_init(&s->container, "realview-gic-container", 0x2000);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(busdev, 1));
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(busdev, 0));
    sysbus_init_mmio(dev, &s->container);
    return 0;
}

static void realview_gic_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = realview_gic_init;
}

static const TypeInfo realview_gic_info = {
    .name          = "realview_gic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RealViewGICState),
    .class_init    = realview_gic_class_init,
};

static void realview_gic_register_types(void)
{
    type_register_static(&realview_gic_info);
}

type_init(realview_gic_register_types)
