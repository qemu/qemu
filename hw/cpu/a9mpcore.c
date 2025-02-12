/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "target/arm/cpu-qom.h"

#define A9_GIC_NUM_PRIORITY_BITS    5

static void a9mp_priv_set_irq(void *opaque, int irq, int level)
{
    A9MPPrivState *s = (A9MPPrivState *)opaque;

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void a9mp_priv_initfn(Object *obj)
{
    A9MPPrivState *s = A9MPCORE_PRIV(obj);

    memory_region_init(&s->container, obj, "a9mp-priv-container", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->container);

    object_initialize_child(obj, "scu", &s->scu, TYPE_A9_SCU);

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);

    object_initialize_child(obj, "gtimer", &s->gtimer, TYPE_A9_GTIMER);

    object_initialize_child(obj, "mptimer", &s->mptimer, TYPE_ARM_MPTIMER);

    object_initialize_child(obj, "wdt", &s->wdt, TYPE_ARM_MPTIMER);
}

static void a9mp_priv_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    A9MPPrivState *s = A9MPCORE_PRIV(dev);
    DeviceState *scudev, *gicdev, *gtimerdev, *mptimerdev, *wdtdev;
    SysBusDevice *scubusdev, *gicbusdev, *gtimerbusdev, *mptimerbusdev,
                 *wdtbusdev;
    int i;
    bool has_el3;
    CPUState *cpu0;
    Object *cpuobj;

    if (s->num_irq < 32 || s->num_irq > 256) {
        error_setg(errp, "Property 'num-irq' must be between 32 and 256");
        return;
    }

    cpu0 = qemu_get_cpu(0);
    cpuobj = OBJECT(cpu0);
    if (strcmp(object_get_typename(cpuobj), ARM_CPU_TYPE_NAME("cortex-a9"))) {
        /* We might allow Cortex-A5 once we model it */
        error_setg(errp,
                   "Cortex-A9MPCore peripheral can only use Cortex-A9 CPU");
        return;
    }

    scudev = DEVICE(&s->scu);
    qdev_prop_set_uint32(scudev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    scubusdev = SYS_BUS_DEVICE(&s->scu);

    gicdev = DEVICE(&s->gic);
    qdev_prop_set_uint32(gicdev, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(gicdev, "num-irq", s->num_irq);
    qdev_prop_set_uint32(gicdev, "num-priority-bits",
                         A9_GIC_NUM_PRIORITY_BITS);

    /* Make the GIC's TZ support match the CPUs. We assume that
     * either all the CPUs have TZ, or none do.
     */
    has_el3 = object_property_find(cpuobj, "has_el3") &&
        object_property_get_bool(cpuobj, "has_el3", &error_abort);
    qdev_prop_set_bit(gicdev, "has-security-extensions", has_el3);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }
    gicbusdev = SYS_BUS_DEVICE(&s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, gicbusdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, a9mp_priv_set_irq, s->num_irq - 32);

    gtimerdev = DEVICE(&s->gtimer);
    qdev_prop_set_uint32(gtimerdev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gtimer), errp)) {
        return;
    }
    gtimerbusdev = SYS_BUS_DEVICE(&s->gtimer);

    mptimerdev = DEVICE(&s->mptimer);
    qdev_prop_set_uint32(mptimerdev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mptimer), errp)) {
        return;
    }
    mptimerbusdev = SYS_BUS_DEVICE(&s->mptimer);

    wdtdev = DEVICE(&s->wdt);
    qdev_prop_set_uint32(wdtdev, "num-cpu", s->num_cpu);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt), errp)) {
        return;
    }
    wdtbusdev = SYS_BUS_DEVICE(&s->wdt);

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x00ff -- Snoop Control Unit
     *  0x0100-0x01ff -- GIC CPU interface
     *  0x0200-0x02ff -- Global Timer
     *  0x0300-0x05ff -- nothing
     *  0x0600-0x06ff -- private timers and watchdogs
     *  0x0700-0x0fff -- nothing
     *  0x1000-0x1fff -- GIC Distributor
     */
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(scubusdev, 0));
    /* GIC CPU interface */
    memory_region_add_subregion(&s->container, 0x100,
                                sysbus_mmio_get_region(gicbusdev, 1));
    memory_region_add_subregion(&s->container, 0x200,
                                sysbus_mmio_get_region(gtimerbusdev, 0));
    /* Note that the A9 exposes only the "timer/watchdog for this core"
     * memory region, not the "timer/watchdog for core X" ones 11MPcore has.
     */
    memory_region_add_subregion(&s->container, 0x600,
                                sysbus_mmio_get_region(mptimerbusdev, 0));
    memory_region_add_subregion(&s->container, 0x620,
                                sysbus_mmio_get_region(wdtbusdev, 0));
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(gicbusdev, 0));

    /* Wire up the interrupt from each watchdog and timer.
     * For each core the global timer is PPI 27, the private
     * timer is PPI 29 and the watchdog PPI 30.
     */
    for (i = 0; i < s->num_cpu; i++) {
        int ppibase = (s->num_irq - 32) + i * 32;
        sysbus_connect_irq(gtimerbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 27));
        sysbus_connect_irq(mptimerbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 29));
        sysbus_connect_irq(wdtbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 30));
    }
}

static const Property a9mp_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", A9MPPrivState, num_cpu, 1),
    /*
     * The Cortex-A9MP may have anything from 0 to 224 external interrupt
     * lines, plus always 32 internal IRQs. This property sets the total
     * of internal + external, so the valid range is from 32 to 256.
     * The board model must set this to whatever the configuration
     * used for the CPU on that board or SoC is.
     */
    DEFINE_PROP_UINT32("num-irq", A9MPPrivState, num_irq, 0),
};

static void a9mp_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = a9mp_priv_realize;
    device_class_set_props(dc, a9mp_priv_properties);
}

static const TypeInfo a9mp_types[] = {
    {
        .name           = TYPE_A9MPCORE_PRIV,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  =  sizeof(A9MPPrivState),
        .instance_init  = a9mp_priv_initfn,
        .class_init     = a9mp_priv_class_init,
    },
};

DEFINE_TYPES(a9mp_types)
