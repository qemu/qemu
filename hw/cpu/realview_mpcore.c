/*
 * RealView ARM11MPCore internal peripheral emulation
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 * Written by Paul Brook and Andreas Färber
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/cpu/arm11mpcore.h"
#include "hw/intc/realview_gic.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#define TYPE_REALVIEW_MPCORE_RIRQ "realview_mpcore"
#define REALVIEW_MPCORE_RIRQ(obj) \
    OBJECT_CHECK(mpcore_rirq_state, (obj), TYPE_REALVIEW_MPCORE_RIRQ)

/* Dummy PIC to route IRQ lines.  The baseboard has 4 independent IRQ
   controllers.  The output of these, plus some of the raw input lines
   are fed into a single SMP-aware interrupt controller on the CPU.  */
typedef struct {
    SysBusDevice parent_obj;

    qemu_irq cpuic[32];
    qemu_irq rvic[4][64];
    uint32_t num_cpu;

    ARM11MPCorePriveState priv;
    RealViewGICState gic[4];
} mpcore_rirq_state;

/* Map baseboard IRQs onto CPU IRQ lines.  */
static const int mpcore_irq_map[32] = {
    -1, -1, -1, -1,  1,  2, -1, -1,
    -1, -1,  6, -1,  4,  5, -1, -1,
    -1, 14, 15,  0,  7,  8, -1, -1,
    -1, -1, -1, -1,  9,  3, -1, -1,
};

static void mpcore_rirq_set_irq(void *opaque, int irq, int level)
{
    mpcore_rirq_state *s = (mpcore_rirq_state *)opaque;
    int i;

    for (i = 0; i < 4; i++) {
        qemu_set_irq(s->rvic[i][irq], level);
    }
    if (irq < 32) {
        irq = mpcore_irq_map[irq];
        if (irq >= 0) {
            qemu_set_irq(s->cpuic[irq], level);
        }
    }
}

static void realview_mpcore_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    mpcore_rirq_state *s = REALVIEW_MPCORE_RIRQ(dev);
    DeviceState *priv = DEVICE(&s->priv);
    DeviceState *gic;
    SysBusDevice *gicbusdev;
    Error *err = NULL;
    int n;
    int i;

    qdev_prop_set_uint32(priv, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->priv), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(&s->priv));
    for (i = 0; i < 32; i++) {
        s->cpuic[i] = qdev_get_gpio_in(priv, i);
    }
    /* ??? IRQ routing is hardcoded to "normal" mode.  */
    for (n = 0; n < 4; n++) {
        object_property_set_bool(OBJECT(&s->gic[n]), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        gic = DEVICE(&s->gic[n]);
        gicbusdev = SYS_BUS_DEVICE(&s->gic[n]);
        sysbus_mmio_map(gicbusdev, 0, 0x10040000 + n * 0x10000);
        sysbus_connect_irq(gicbusdev, 0, s->cpuic[10 + n]);
        for (i = 0; i < 64; i++) {
            s->rvic[n][i] = qdev_get_gpio_in(gic, i);
        }
    }
    qdev_init_gpio_in(dev, mpcore_rirq_set_irq, 64);
}

static void mpcore_rirq_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    mpcore_rirq_state *s = REALVIEW_MPCORE_RIRQ(obj);
    SysBusDevice *privbusdev;
    int i;

    sysbus_init_child_obj(obj, "a11priv", &s->priv, sizeof(s->priv),
                          TYPE_ARM11MPCORE_PRIV);
    privbusdev = SYS_BUS_DEVICE(&s->priv);
    sysbus_init_mmio(sbd, sysbus_mmio_get_region(privbusdev, 0));

    for (i = 0; i < 4; i++) {
        sysbus_init_child_obj(obj, "gic[*]", &s->gic[i], sizeof(s->gic[i]),
                              TYPE_REALVIEW_GIC);
    }
}

static Property mpcore_rirq_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", mpcore_rirq_state, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void mpcore_rirq_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = realview_mpcore_realize;
    dc->props = mpcore_rirq_properties;
}

static const TypeInfo mpcore_rirq_info = {
    .name          = TYPE_REALVIEW_MPCORE_RIRQ,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mpcore_rirq_state),
    .instance_init = mpcore_rirq_init,
    .class_init    = mpcore_rirq_class_init,
};

static void realview_mpcore_register_types(void)
{
    type_register_static(&mpcore_rirq_info);
}

type_init(realview_mpcore_register_types)
