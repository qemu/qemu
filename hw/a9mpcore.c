/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"

/* Configuration for arm_gic.c:
 * number of external IRQ lines, max number of CPUs, how to ID current CPU
 */
#define GIC_NIRQ 96
#define NCPU 4

static inline int
gic_get_current_cpu(void)
{
  return cpu_single_env->cpu_index;
}

#include "arm_gic.c"

/* A9MP private memory region.  */

typedef struct a9mp_priv_state {
    gic_state gic;
    uint32_t scu_control;
    uint32_t old_timer_status[8];
    uint32_t num_cpu;
    qemu_irq *timer_irq;
    MemoryRegion scu_iomem;
    MemoryRegion ptimer_iomem;
    MemoryRegion container;
    DeviceState *mptimer;
} a9mp_priv_state;

static uint64_t a9_scu_read(void *opaque, target_phys_addr_t offset,
                            unsigned size)
{
    a9mp_priv_state *s = (a9mp_priv_state *)opaque;
    switch (offset) {
    case 0x00: /* Control */
        return s->scu_control;
    case 0x04: /* Configuration */
        return (((1 << s->num_cpu) - 1) << 4) | (s->num_cpu - 1);
    case 0x08: /* CPU Power Status */
        return 0;
    case 0x0c: /* Invalidate All Registers In Secure State */
        return 0;
    case 0x40: /* Filtering Start Address Register */
    case 0x44: /* Filtering End Address Register */
        /* RAZ/WI, like an implementation with only one AXI master */
        return 0;
    case 0x50: /* SCU Access Control Register */
    case 0x54: /* SCU Non-secure Access Control Register */
        /* unimplemented, fall through */
    default:
        return 0;
    }
}

static void a9_scu_write(void *opaque, target_phys_addr_t offset,
                         uint64_t value, unsigned size)
{
    a9mp_priv_state *s = (a9mp_priv_state *)opaque;
    switch (offset) {
    case 0x00: /* Control */
        s->scu_control = value & 1;
        break;
    case 0x4: /* Configuration: RO */
        break;
    case 0x0c: /* Invalidate All Registers In Secure State */
        /* no-op as we do not implement caches */
        break;
    case 0x40: /* Filtering Start Address Register */
    case 0x44: /* Filtering End Address Register */
        /* RAZ/WI, like an implementation with only one AXI master */
        break;
    case 0x8: /* CPU Power Status */
    case 0x50: /* SCU Access Control Register */
    case 0x54: /* SCU Non-secure Access Control Register */
        /* unimplemented, fall through */
    default:
        break;
    }
}

static const MemoryRegionOps a9_scu_ops = {
    .read = a9_scu_read,
    .write = a9_scu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void a9mpcore_timer_irq_handler(void *opaque, int irq, int level)
{
    a9mp_priv_state *s = (a9mp_priv_state *)opaque;
    if (level && !s->old_timer_status[irq]) {
        gic_set_pending_private(&s->gic, irq >> 1, 29 + (irq & 1));
    }
    s->old_timer_status[irq] = level;
}

static void a9mp_priv_reset(DeviceState *dev)
{
    a9mp_priv_state *s = FROM_SYSBUSGIC(a9mp_priv_state, sysbus_from_qdev(dev));
    int i;
    s->scu_control = 0;
    for (i = 0; i < ARRAY_SIZE(s->old_timer_status); i++) {
        s->old_timer_status[i] = 0;
    }
}

static int a9mp_priv_init(SysBusDevice *dev)
{
    a9mp_priv_state *s = FROM_SYSBUSGIC(a9mp_priv_state, dev);
    SysBusDevice *busdev;
    int i;

    if (s->num_cpu > NCPU) {
        hw_error("a9mp_priv_init: num-cpu may not be more than %d\n", NCPU);
    }

    gic_init(&s->gic, s->num_cpu);

    s->mptimer = qdev_create(NULL, "arm_mptimer");
    qdev_prop_set_uint32(s->mptimer, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->mptimer);
    busdev = sysbus_from_qdev(s->mptimer);

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
    memory_region_init_io(&s->scu_iomem, &a9_scu_ops, s, "a9mp-scu", 0x100);
    memory_region_add_subregion(&s->container, 0, &s->scu_iomem);
    /* GIC CPU interface */
    memory_region_add_subregion(&s->container, 0x100, &s->gic.cpuiomem[0]);
    /* Note that the A9 exposes only the "timer/watchdog for this core"
     * memory region, not the "timer/watchdog for core X" ones 11MPcore has.
     */
    memory_region_add_subregion(&s->container, 0x600,
                                sysbus_mmio_get_region(busdev, 0));
    memory_region_add_subregion(&s->container, 0x620,
                                sysbus_mmio_get_region(busdev, 1));
    memory_region_add_subregion(&s->container, 0x1000, &s->gic.iomem);

    sysbus_init_mmio(dev, &s->container);

    /* Wire up the interrupt from each watchdog and timer. */
    s->timer_irq = qemu_allocate_irqs(a9mpcore_timer_irq_handler,
                                      s, (s->num_cpu + 1) * 2);
    for (i = 0; i < s->num_cpu * 2; i++) {
        sysbus_connect_irq(busdev, i, s->timer_irq[i]);
    }
    return 0;
}

static const VMStateDescription vmstate_a9mp_priv = {
    .name = "a9mpcore_priv",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(scu_control, a9mp_priv_state),
        VMSTATE_UINT32_ARRAY(old_timer_status, a9mp_priv_state, 8),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo a9mp_priv_info = {
    .init = a9mp_priv_init,
    .qdev.name  = "a9mpcore_priv",
    .qdev.size  = sizeof(a9mp_priv_state),
    .qdev.vmsd = &vmstate_a9mp_priv,
    .qdev.reset = a9mp_priv_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("num-cpu", a9mp_priv_state, num_cpu, 1),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void a9mp_register_devices(void)
{
    sysbus_register_withprop(&a9mp_priv_info);
}

device_init(a9mp_register_devices)
