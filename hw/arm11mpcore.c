/*
 * ARM11MPCore internal peripheral emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/* ??? The MPCore TRM says the on-chip controller has 224 external IRQ lines
   (+ 32 internal).  However my test chip only exposes/reports 32.
   More importantly Linux falls over if more than 32 are present!  */
#define GIC_NIRQ 64
#include "mpcore.c"

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

static void mpcore_rirq_map(SysBusDevice *dev, target_phys_addr_t base)
{
    mpcore_rirq_state *s = FROM_SYSBUS(mpcore_rirq_state, dev);
    sysbus_mmio_map(s->priv, 0, base);
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
    sysbus_init_mmio_cb(dev, 0x2000, mpcore_rirq_map);
    return 0;
}

static SysBusDeviceInfo mpcore_rirq_info = {
    .init = realview_mpcore_init,
    .qdev.name  = "realview_mpcore",
    .qdev.size  = sizeof(mpcore_rirq_state),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("num-cpu", mpcore_rirq_state, num_cpu, 1),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static SysBusDeviceInfo mpcore_priv_info = {
    .init = mpcore_priv_init,
    .qdev.name  = "arm11mpcore_priv",
    .qdev.size  = sizeof(mpcore_priv_state),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("num-cpu", mpcore_priv_state, num_cpu, 1),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void arm11mpcore_register_devices(void)
{
    sysbus_register_withprop(&mpcore_rirq_info);
    sysbus_register_withprop(&mpcore_priv_info);
}

device_init(arm11mpcore_register_devices)
