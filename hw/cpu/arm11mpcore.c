/*
 * ARM11MPCore internal peripheral emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/cpu/arm11mpcore.h"
#include "hw/intc/realview_gic.h"


static void mpcore_priv_set_irq(void *opaque, int irq, int level)
{
    ARM11MPCorePriveState *s = (ARM11MPCorePriveState *)opaque;

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void mpcore_priv_map_setup(ARM11MPCorePriveState *s)
{
    int i;
    SysBusDevice *scubusdev = SYS_BUS_DEVICE(&s->scu);
    DeviceState *gicdev = DEVICE(&s->gic);
    SysBusDevice *gicbusdev = SYS_BUS_DEVICE(&s->gic);
    SysBusDevice *timerbusdev = SYS_BUS_DEVICE(&s->mptimer);
    SysBusDevice *wdtbusdev = SYS_BUS_DEVICE(&s->wdtimer);

    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(scubusdev, 0));
    /* GIC CPU interfaces: "current CPU" at 0x100, then specific CPUs
     * at 0x200, 0x300...
     */
    for (i = 0; i < (s->num_cpu + 1); i++) {
        hwaddr offset = 0x100 + (i * 0x100);
        memory_region_add_subregion(&s->container, offset,
                                    sysbus_mmio_get_region(gicbusdev, i + 1));
    }
    /* Add the regions for timer and watchdog for "current CPU" and
     * for each specific CPU.
     */
    for (i = 0; i < (s->num_cpu + 1); i++) {
        /* Timers at 0x600, 0x700, ...; watchdogs at 0x620, 0x720, ... */
        hwaddr offset = 0x600 + i * 0x100;
        memory_region_add_subregion(&s->container, offset,
                                    sysbus_mmio_get_region(timerbusdev, i));
        memory_region_add_subregion(&s->container, offset + 0x20,
                                    sysbus_mmio_get_region(wdtbusdev, i));
    }
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(gicbusdev, 0));
    /* Wire up the interrupt from each watchdog and timer.
     * For each core the timer is PPI 29 and the watchdog PPI 30.
     */
    for (i = 0; i < s->num_cpu; i++) {
        int ppibase = (s->num_irq - 32) + i * 32;
        sysbus_connect_irq(timerbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 29));
        sysbus_connect_irq(wdtbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 30));
    }
}

static void mpcore_priv_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    ARM11MPCorePriveState *s = ARM11MPCORE_PRIV(dev);
    DeviceState *scudev = DEVICE(&s->scu);
    DeviceState *gicdev = DEVICE(&s->gic);
    DeviceState *mptimerdev = DEVICE(&s->mptimer);
    DeviceState *wdtimerdev = DEVICE(&s->wdtimer);
    Error *err = NULL;

    qdev_prop_set_uint32(scudev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->scu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    qdev_prop_set_uint32(gicdev, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(gicdev, "num-irq", s->num_irq);
    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(&s->gic));

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, mpcore_priv_set_irq, s->num_irq - 32);

    qdev_prop_set_uint32(mptimerdev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->mptimer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    qdev_prop_set_uint32(wdtimerdev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->wdtimer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    mpcore_priv_map_setup(s);
}

static void mpcore_priv_initfn(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARM11MPCorePriveState *s = ARM11MPCORE_PRIV(obj);

    memory_region_init(&s->container, OBJECT(s),
                       "mpcore-priv-container", 0x2000);
    sysbus_init_mmio(sbd, &s->container);

    object_initialize(&s->scu, sizeof(s->scu), TYPE_ARM11_SCU);
    qdev_set_parent_bus(DEVICE(&s->scu), sysbus_get_default());

    object_initialize(&s->gic, sizeof(s->gic), TYPE_ARM_GIC);
    qdev_set_parent_bus(DEVICE(&s->gic), sysbus_get_default());
    /* Request the legacy 11MPCore GIC behaviour: */
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 0);

    object_initialize(&s->mptimer, sizeof(s->mptimer), TYPE_ARM_MPTIMER);
    qdev_set_parent_bus(DEVICE(&s->mptimer), sysbus_get_default());

    object_initialize(&s->wdtimer, sizeof(s->wdtimer), TYPE_ARM_MPTIMER);
    qdev_set_parent_bus(DEVICE(&s->wdtimer), sysbus_get_default());
}

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

    object_initialize(&s->priv, sizeof(s->priv), TYPE_ARM11MPCORE_PRIV);
    qdev_set_parent_bus(DEVICE(&s->priv), sysbus_get_default());
    privbusdev = SYS_BUS_DEVICE(&s->priv);
    sysbus_init_mmio(sbd, sysbus_mmio_get_region(privbusdev, 0));

    for (i = 0; i < 4; i++) {
        object_initialize(&s->gic[i], sizeof(s->gic[i]), TYPE_REALVIEW_GIC);
        qdev_set_parent_bus(DEVICE(&s->gic[i]), sysbus_get_default());
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

static Property mpcore_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", ARM11MPCorePriveState, num_cpu, 1),
    /* The ARM11 MPCORE TRM says the on-chip controller may have
     * anything from 0 to 224 external interrupt IRQ lines (with another
     * 32 internal). We default to 32+32, which is the number provided by
     * the ARM11 MPCore test chip in the Realview Versatile Express
     * coretile. Other boards may differ and should set this property
     * appropriately. Some Linux kernels may not boot if the hardware
     * has more IRQ lines than the kernel expects.
     */
    DEFINE_PROP_UINT32("num-irq", ARM11MPCorePriveState, num_irq, 64),
    DEFINE_PROP_END_OF_LIST(),
};

static void mpcore_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mpcore_priv_realize;
    dc->props = mpcore_priv_properties;
}

static const TypeInfo mpcore_priv_info = {
    .name          = TYPE_ARM11MPCORE_PRIV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARM11MPCorePriveState),
    .instance_init = mpcore_priv_initfn,
    .class_init    = mpcore_priv_class_init,
};

static void arm11mpcore_register_types(void)
{
    type_register_static(&mpcore_rirq_info);
    type_register_static(&mpcore_priv_info);
}

type_init(arm11mpcore_register_types)
