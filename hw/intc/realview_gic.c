/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/intc/realview_gic.h"

static void realview_gic_set_irq(void *opaque, int irq, int level)
{
    RealViewGICState *s = (RealViewGICState *)opaque;

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void realview_gic_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    RealViewGICState *s = REALVIEW_GIC(dev);
    SysBusDevice *busdev;
    Error *err = NULL;
    /* The GICs on the RealView boards have a fixed nonconfigurable
     * number of interrupt lines, so we don't need to expose this as
     * a qdev property.
     */
    int numirq = 96;

    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", numirq);
    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    busdev = SYS_BUS_DEVICE(&s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, busdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, realview_gic_set_irq, numirq - 32);

    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(busdev, 1));
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(busdev, 0));
}

static void realview_gic_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RealViewGICState *s = REALVIEW_GIC(obj);

    memory_region_init(&s->container, OBJECT(s),
                       "realview-gic-container", 0x2000);
    sysbus_init_mmio(sbd, &s->container);

    sysbus_init_child_obj(obj, "gic", &s->gic, sizeof(s->gic), TYPE_ARM_GIC);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", 1);
}

static void realview_gic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = realview_gic_realize;
}

static const TypeInfo realview_gic_info = {
    .name          = TYPE_REALVIEW_GIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RealViewGICState),
    .instance_init = realview_gic_init,
    .class_init    = realview_gic_class_init,
};

static void realview_gic_register_types(void)
{
    type_register_static(&realview_gic_info);
}

type_init(realview_gic_register_types)
