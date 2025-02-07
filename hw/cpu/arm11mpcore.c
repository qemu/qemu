/*
 * ARM11MPCore internal peripheral emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
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

#define ARM11MPCORE_NUM_GIC_PRIORITY_BITS    4

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

    qdev_prop_set_uint32(scudev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }

    qdev_prop_set_uint32(gicdev, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(gicdev, "num-irq", s->num_irq);
    qdev_prop_set_uint32(gicdev, "num-priority-bits",
                         ARM11MPCORE_NUM_GIC_PRIORITY_BITS);


    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(&s->gic));

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, mpcore_priv_set_irq, s->num_irq - 32);

    qdev_prop_set_uint32(mptimerdev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mptimer), errp)) {
        return;
    }

    qdev_prop_set_uint32(wdtimerdev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdtimer), errp)) {
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

    object_initialize_child(obj, "scu", &s->scu, TYPE_ARM11_SCU);

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);
    /* Request the legacy 11MPCore GIC behaviour: */
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 0);

    object_initialize_child(obj, "mptimer", &s->mptimer, TYPE_ARM_MPTIMER);

    object_initialize_child(obj, "wdtimer", &s->wdtimer, TYPE_ARM_MPTIMER);
}

static const Property mpcore_priv_properties[] = {
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
};

static void mpcore_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mpcore_priv_realize;
    device_class_set_props(dc, mpcore_priv_properties);
}

static const TypeInfo arm11mp_types[] = {
    {
        .name           = TYPE_ARM11MPCORE_PRIV,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(ARM11MPCorePriveState),
        .instance_init  = mpcore_priv_initfn,
        .class_init     = mpcore_priv_class_init,
    },
};

DEFINE_TYPES(arm11mp_types)
