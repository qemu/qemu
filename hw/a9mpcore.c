/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"

typedef struct A9MPPrivState {
    SysBusDevice busdev;
    uint32_t num_cpu;
    MemoryRegion container;
    DeviceState *mptimer;
    DeviceState *wdt;
    DeviceState *gic;
    DeviceState *scu;
    uint32_t num_irq;
} A9MPPrivState;

static void a9mp_priv_set_irq(void *opaque, int irq, int level)
{
    A9MPPrivState *s = (A9MPPrivState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static int a9mp_priv_init(SysBusDevice *dev)
{
    A9MPPrivState *s = FROM_SYSBUS(A9MPPrivState, dev);
    SysBusDevice *timerbusdev, *wdtbusdev, *gicbusdev, *scubusdev;
    int i;

    s->gic = qdev_create(NULL, "arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(s->gic, "num-irq", s->num_irq);
    qdev_init_nofail(s->gic);
    gicbusdev = SYS_BUS_DEVICE(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, gicbusdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, a9mp_priv_set_irq, s->num_irq - 32);

    s->scu = qdev_create(NULL, "a9-scu");
    qdev_prop_set_uint32(s->scu, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->scu);
    scubusdev = SYS_BUS_DEVICE(s->scu);

    s->mptimer = qdev_create(NULL, "arm_mptimer");
    qdev_prop_set_uint32(s->mptimer, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->mptimer);
    timerbusdev = SYS_BUS_DEVICE(s->mptimer);

    s->wdt = qdev_create(NULL, "arm_mptimer");
    qdev_prop_set_uint32(s->wdt, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->wdt);
    wdtbusdev = SYS_BUS_DEVICE(s->wdt);

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x00ff -- Snoop Control Unit
     *  0x0100-0x01ff -- GIC CPU interface
     *  0x0200-0x02ff -- Global Timer
     *  0x0300-0x05ff -- nothing
     *  0x0600-0x06ff -- private timers and watchdogs
     *  0x0700-0x0fff -- nothing
     *  0x1000-0x1fff -- GIC Distributor
     *
     * We should implement the global timer but don't currently do so.
     */
    memory_region_init(&s->container, "a9mp-priv-container", 0x2000);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(scubusdev, 0));
    /* GIC CPU interface */
    memory_region_add_subregion(&s->container, 0x100,
                                sysbus_mmio_get_region(gicbusdev, 1));
    /* Note that the A9 exposes only the "timer/watchdog for this core"
     * memory region, not the "timer/watchdog for core X" ones 11MPcore has.
     */
    memory_region_add_subregion(&s->container, 0x600,
                                sysbus_mmio_get_region(timerbusdev, 0));
    memory_region_add_subregion(&s->container, 0x620,
                                sysbus_mmio_get_region(wdtbusdev, 0));
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(gicbusdev, 0));

    sysbus_init_mmio(dev, &s->container);

    /* Wire up the interrupt from each watchdog and timer.
     * For each core the timer is PPI 29 and the watchdog PPI 30.
     */
    for (i = 0; i < s->num_cpu; i++) {
        int ppibase = (s->num_irq - 32) + i * 32;
        sysbus_connect_irq(timerbusdev, i,
                           qdev_get_gpio_in(s->gic, ppibase + 29));
        sysbus_connect_irq(wdtbusdev, i,
                           qdev_get_gpio_in(s->gic, ppibase + 30));
    }
    return 0;
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
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = a9mp_priv_init;
    dc->props = a9mp_priv_properties;
}

static const TypeInfo a9mp_priv_info = {
    .name          = "a9mpcore_priv",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(A9MPPrivState),
    .class_init    = a9mp_priv_class_init,
};

static void a9mp_register_types(void)
{
    type_register_static(&a9mp_priv_info);
}

type_init(a9mp_register_types)
