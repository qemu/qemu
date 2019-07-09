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

    sysbus_init_child_obj(obj, "scu", &s->scu, sizeof(s->scu), TYPE_A9_SCU);

    sysbus_init_child_obj(obj, "gic", &s->gic, sizeof(s->gic), TYPE_ARM_GIC);

    sysbus_init_child_obj(obj, "gtimer", &s->gtimer, sizeof(s->gtimer),
                          TYPE_A9_GTIMER);

    sysbus_init_child_obj(obj, "mptimer", &s->mptimer, sizeof(s->mptimer),
                          TYPE_ARM_MPTIMER);

    sysbus_init_child_obj(obj, "wdt", &s->wdt, sizeof(s->wdt),
                          TYPE_ARM_MPTIMER);
}

static void a9mp_priv_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    A9MPPrivState *s = A9MPCORE_PRIV(dev);
    DeviceState *scudev, *gicdev, *gtimerdev, *mptimerdev, *wdtdev;
    SysBusDevice *scubusdev, *gicbusdev, *gtimerbusdev, *mptimerbusdev,
                 *wdtbusdev;
    Error *err = NULL;
    int i;
    bool has_el3;
    Object *cpuobj;

    scudev = DEVICE(&s->scu);
    qdev_prop_set_uint32(scudev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->scu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    scubusdev = SYS_BUS_DEVICE(&s->scu);

    gicdev = DEVICE(&s->gic);
    qdev_prop_set_uint32(gicdev, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(gicdev, "num-irq", s->num_irq);

    /* Make the GIC's TZ support match the CPUs. We assume that
     * either all the CPUs have TZ, or none do.
     */
    cpuobj = OBJECT(qemu_get_cpu(0));
    has_el3 = object_property_find(cpuobj, "has_el3", NULL) &&
        object_property_get_bool(cpuobj, "has_el3", &error_abort);
    qdev_prop_set_bit(gicdev, "has-security-extensions", has_el3);

    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    gicbusdev = SYS_BUS_DEVICE(&s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, gicbusdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, a9mp_priv_set_irq, s->num_irq - 32);

    gtimerdev = DEVICE(&s->gtimer);
    qdev_prop_set_uint32(gtimerdev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->gtimer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    gtimerbusdev = SYS_BUS_DEVICE(&s->gtimer);

    mptimerdev = DEVICE(&s->mptimer);
    qdev_prop_set_uint32(mptimerdev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->mptimer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    mptimerbusdev = SYS_BUS_DEVICE(&s->mptimer);

    wdtdev = DEVICE(&s->wdt);
    qdev_prop_set_uint32(wdtdev, "num-cpu", s->num_cpu);
    object_property_set_bool(OBJECT(&s->wdt), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
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

static Property a9mp_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", A9MPPrivState, num_cpu, 1),
    /* The Cortex-A9MP may have anything from 0 to 224 external interrupt
     * IRQ lines (with another 32 internal). We default to 64+32, which
     * is the number provided by the Cortex-A9MP test chip in the
     * Realview PBX-A9 and Versatile Express A9 development boards.
     * Other boards may differ and should set this property appropriately.
     */
    DEFINE_PROP_UINT32("num-irq", A9MPPrivState, num_irq, 96),
    DEFINE_PROP_END_OF_LIST(),
};

static void a9mp_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = a9mp_priv_realize;
    dc->props = a9mp_priv_properties;
}

static const TypeInfo a9mp_priv_info = {
    .name          = TYPE_A9MPCORE_PRIV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(A9MPPrivState),
    .instance_init = a9mp_priv_initfn,
    .class_init    = a9mp_priv_class_init,
};

static void a9mp_register_types(void)
{
    type_register_static(&a9mp_priv_info);
}

type_init(a9mp_register_types)
