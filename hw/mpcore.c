/*
 * ARM MPCore internal peripheral emulation (common code).
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "qemu-timer.h"

#define NCPU 4

static inline int
gic_get_current_cpu(void)
{
  return cpu_single_env->cpu_index;
}

#include "arm_gic.c"

/* MPCore private memory region.  */

typedef struct mpcore_priv_state {
    gic_state gic;
    uint32_t scu_control;
    int iomemtype;
    uint32_t old_timer_status[8];
    uint32_t num_cpu;
    qemu_irq *timer_irq;
    MemoryRegion iomem;
    MemoryRegion container;
    DeviceState *mptimer;
} mpcore_priv_state;

/* Per-CPU private memory mapped IO.  */

static uint64_t mpcore_priv_read(void *opaque, target_phys_addr_t offset,
                                 unsigned size)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    int id;
    offset &= 0xfff;
    if (offset < 0x100) {
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
            goto bad_reg;
        }
    } else if (offset < 0x600) {
        /* Interrupt controller.  */
        if (offset < 0x200) {
            id = gic_get_current_cpu();
        } else {
            id = (offset - 0x200) >> 8;
            if (id >= s->num_cpu) {
                return 0;
            }
        }
        return gic_cpu_read(&s->gic, id, offset & 0xff);
    }
bad_reg:
    hw_error("mpcore_priv_read: Bad offset %x\n", (int)offset);
    return 0;
}

static void mpcore_priv_write(void *opaque, target_phys_addr_t offset,
                              uint64_t value, unsigned size)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    int id;
    offset &= 0xfff;
    if (offset < 0x100) {
        /* SCU */
        switch (offset) {
        case 0: /* Control register.  */
            s->scu_control = value & 1;
            break;
        case 0x0c: /* Invalidate all.  */
            /* This is a no-op as cache is not emulated.  */
            break;
        default:
            goto bad_reg;
        }
    } else if (offset < 0x600) {
        /* Interrupt controller.  */
        if (offset < 0x200) {
            id = gic_get_current_cpu();
        } else {
            id = (offset - 0x200) >> 8;
        }
        if (id < s->num_cpu) {
            gic_cpu_write(&s->gic, id, offset & 0xff, value);
        }
    }
    return;
bad_reg:
    hw_error("mpcore_priv_read: Bad offset %x\n", (int)offset);
}

static const MemoryRegionOps mpcore_priv_ops = {
    .read = mpcore_priv_read,
    .write = mpcore_priv_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mpcore_timer_irq_handler(void *opaque, int irq, int level)
{
    mpcore_priv_state *s = (mpcore_priv_state *)opaque;
    if (level && !s->old_timer_status[irq]) {
        gic_set_pending_private(&s->gic, irq >> 1, 29 + (irq & 1));
    }
    s->old_timer_status[irq] = level;
}

static void mpcore_priv_map_setup(mpcore_priv_state *s)
{
    int i;
    SysBusDevice *busdev = sysbus_from_qdev(s->mptimer);
    memory_region_init(&s->container, "mpcode-priv-container", 0x2000);
    memory_region_init_io(&s->iomem, &mpcore_priv_ops, s, "mpcode-priv",
                          0x1000);
    memory_region_add_subregion(&s->container, 0, &s->iomem);
    /* Add the regions for timer and watchdog for "current CPU" and
     * for each specific CPU.
     */
    s->timer_irq = qemu_allocate_irqs(mpcore_timer_irq_handler,
                                      s, (s->num_cpu + 1) * 2);
    for (i = 0; i < (s->num_cpu + 1) * 2; i++) {
        /* Timers at 0x600, 0x700, ...; watchdogs at 0x620, 0x720, ... */
        target_phys_addr_t offset = 0x600 + (i >> 1) * 0x100 + (i & 1) * 0x20;
        memory_region_add_subregion(&s->container, offset,
                                    sysbus_mmio_get_region(busdev, i));
    }
    memory_region_add_subregion(&s->container, 0x1000, &s->gic.iomem);
    /* Wire up the interrupt from each watchdog and timer. */
    for (i = 0; i < s->num_cpu * 2; i++) {
        sysbus_connect_irq(busdev, i, s->timer_irq[i]);
    }
}

static int mpcore_priv_init(SysBusDevice *dev)
{
    mpcore_priv_state *s = FROM_SYSBUSGIC(mpcore_priv_state, dev);

    gic_init(&s->gic, s->num_cpu);
    s->mptimer = qdev_create(NULL, "arm_mptimer");
    qdev_prop_set_uint32(s->mptimer, "num-cpu", s->num_cpu);
    qdev_init_nofail(s->mptimer);
    mpcore_priv_map_setup(s);
    sysbus_init_mmio(dev, &s->container);
    return 0;
}
