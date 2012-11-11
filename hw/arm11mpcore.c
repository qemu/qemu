/*
 * ARM11MPCore internal peripheral emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "qemu-timer.h"

/* MPCore private memory region.  */

typedef struct mpcore_priv_state {
    SysBusDevice busdev;
    uint32_t scu_control;
    int iomemtype;
    uint32_t old_timer_status[8];
    uint32_t num_cpu;
    MemoryRegion iomem;
    MemoryRegion container;
    DeviceState *mptimer;
    DeviceState *gic;
    uint32_t num_irq;
} mpcore_priv_state;

/* Per-CPU private memory mapped IO.  */

static uint64_t mpcore_scu_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    int id;
    /* SCU */
    switch (offset) {
    case 0x00: /* Control.  */
        return s->scu_control;
    case 0x04: /* Configuration.  */
        id = ((1 << s->num_cpu) - 1) << 4;
        return id | (s->num_cpu - 1);
    case 0x08: /* CPU status.  */
        return 0;
    case 0x0c: /* Invalidate all.  */
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpcore_priv_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void mpcore_scu_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    /* SCU */
    switch (offset) {
    case 0: /* Control register.  */
        s->scu_control = value & 1;
        break;
    case 0x0c: /* Invalidate all.  */
        /* This is a no-op as cache is not emulated.  */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpcore_priv_read: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps mpcore_scu_ops = {
    .read = mpcore_scu_read,
    .write = mpcore_scu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mpcore_priv_set_irq(void *opaque, int irq, int level)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static void mpcore_priv_map_setup(mpcore_priv_state *s)
{
    int i;
    SysBusDevice *gicbusdev = sysbus_from_qdev(s->gic);
    SysBusDevice *busdev = sysbus_from_qdev(s->mptimer);
    memory_region_init(&s->container, "mpcode-priv-container", 0x2000);
    memory_region_init_io(&s->iomem, &mpcore_scu_ops, s, "mpcore-scu", 0x100);
    memory_region_add_subregion(&s->container, 0, &s->iomem);
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
    for (i = 0; i < (s->num_cpu + 1) * 2; i++) {
        /* Timers at 0x600, 0x700, ...; watchdogs at 0x620, 0x720, ... */
        hwaddr offset = 0x600 + (i >> 1) * 0x100 + (i & 1) * 0x20;
        memory_region_add_subregion(&s->container, offset,
                                    sysbus_mmio_get_region(busdev, i));
    }
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(gicbusdev, 0));
    /* Wire up the interrupt from each watchdog and timer.
     * For each core the timer is PPI 29 and the watchdog PPI 30.
     */
    for (i = 0; i < s->num_cpu; i++) {
        int ppibase = (s->num_irq - 32) + i * 32;
        sysbus_connect_irq(busdev, i * 2,
                           qdev_get_gpio_in(s->gic, ppibase + 29));
        sysbus_connect_irq(busdev, i * 2 + 1,
                           qdev_get_gpio_in(s->gic, ppibase + 30));
    }
}

static int mpcore_priv_init(SysBusDevice *dev)
{
    mpcore_priv_state *s = FROM_SYSBUS(mpcore_priv_state, dev);

    s->gic = qdev_create(NULL, "arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(s->gic, "num-irq", s->num_irq);
    /* Request the legacy 11MPCore GIC behaviour: */
    qdev_prop_set_uint32(s->gic, "revision", 0);
    qdev_init_nofail(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, sysbus_from_qdev(s->gic));

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, mpcore_priv_set_irq, s->num_irq - 32);

    s->mptimer = qdev_create(NULL, "arm_mptimer");
    qdev_prop_set_uint32(s->mptimer, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->mptimer);
    mpcore_priv_map_setup(s);
    sysbus_init_mmio(dev, &s->container);
    return 0;
}

/* Dummy PIC to route IRQ lines.  The baseboard has 4 independent IRQ
   controllers.  The output of these, plus some of the raw input lines
   are fed into a single SMP-aware interrupt controller on the CPU.  */
typedef struct {
    SysBusDevice busdev;
    SysBusDevice *priv;
    qemu_irq cpuic[32];
    qemu_irq rvic[4][64];
    uint32_t num_cpu;
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

static int realview_mpcore_init(SysBusDevice *dev)
{
    mpcore_rirq_state *s = FROM_SYSBUS(mpcore_rirq_state, dev);
    DeviceState *gic;
    DeviceState *priv;
    int n;
    int i;

    priv = qdev_create(NULL, "arm11mpcore_priv");
    qdev_prop_set_uint32(priv, "num-cpu", s->num_cpu);
    qdev_init_nofail(priv);
    s->priv = sysbus_from_qdev(priv);
    sysbus_pass_irq(dev, s->priv);
    for (i = 0; i < 32; i++) {
        s->cpuic[i] = qdev_get_gpio_in(priv, i);
    }
    /* ??? IRQ routing is hardcoded to "normal" mode.  */
    for (n = 0; n < 4; n++) {
        gic = sysbus_create_simple("realview_gic", 0x10040000 + n * 0x10000,
                                   s->cpuic[10 + n]);
        for (i = 0; i < 64; i++) {
            s->rvic[n][i] = qdev_get_gpio_in(gic, i);
        }
    }
    qdev_init_gpio_in(&dev->qdev, mpcore_rirq_set_irq, 64);
    sysbus_init_mmio(dev, sysbus_mmio_get_region(s->priv, 0));
    return 0;
}

static Property mpcore_rirq_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", mpcore_rirq_state, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void mpcore_rirq_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = realview_mpcore_init;
    dc->props = mpcore_rirq_properties;
}

static TypeInfo mpcore_rirq_info = {
    .name          = "realview_mpcore",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mpcore_rirq_state),
    .class_init    = mpcore_rirq_class_init,
};

static Property mpcore_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", mpcore_priv_state, num_cpu, 1),
    /* The ARM11 MPCORE TRM says the on-chip controller may have
     * anything from 0 to 224 external interrupt IRQ lines (with another
     * 32 internal). We default to 32+32, which is the number provided by
     * the ARM11 MPCore test chip in the Realview Versatile Express
     * coretile. Other boards may differ and should set this property
     * appropriately. Some Linux kernels may not boot if the hardware
     * has more IRQ lines than the kernel expects.
     */
    DEFINE_PROP_UINT32("num-irq", mpcore_priv_state, num_irq, 64),
    DEFINE_PROP_END_OF_LIST(),
};

static void mpcore_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = mpcore_priv_init;
    dc->props = mpcore_priv_properties;
}

static TypeInfo mpcore_priv_info = {
    .name          = "arm11mpcore_priv",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mpcore_priv_state),
    .class_init    = mpcore_priv_class_init,
};

static void arm11mpcore_register_types(void)
{
    type_register_static(&mpcore_rirq_info);
    type_register_static(&mpcore_priv_info);
}

type_init(arm11mpcore_register_types)
