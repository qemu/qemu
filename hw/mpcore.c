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

typedef struct {
    uint32_t count;
    uint32_t load;
    uint32_t control;
    uint32_t status;
    uint32_t old_status;
    int64_t tick;
    QEMUTimer *timer;
    struct mpcore_priv_state *mpcore;
    int id; /* Encodes both timer/watchdog and CPU.  */
} mpcore_timer_state;

typedef struct mpcore_priv_state {
    gic_state gic;
    uint32_t scu_control;
    int iomemtype;
    mpcore_timer_state timer[8];
    uint32_t num_cpu;
    MemoryRegion iomem;
    MemoryRegion container;
} mpcore_priv_state;

/* Per-CPU Timers.  */

static inline void mpcore_timer_update_irq(mpcore_timer_state *s)
{
    if (s->status & ~s->old_status) {
        gic_set_pending_private(&s->mpcore->gic, s->id >> 1, 29 + (s->id & 1));
    }
    s->old_status = s->status;
}

/* Return conversion factor from mpcore timer ticks to qemu timer ticks.  */
static inline uint32_t mpcore_timer_scale(mpcore_timer_state *s)
{
    return (((s->control >> 8) & 0xff) + 1) * 10;
}

static void mpcore_timer_reload(mpcore_timer_state *s, int restart)
{
    if (s->count == 0)
        return;
    if (restart)
        s->tick = qemu_get_clock_ns(vm_clock);
    s->tick += (int64_t)s->count * mpcore_timer_scale(s);
    qemu_mod_timer(s->timer, s->tick);
}

static void mpcore_timer_tick(void *opaque)
{
    mpcore_timer_state *s = (mpcore_timer_state *)opaque;
    s->status = 1;
    if (s->control & 2) {
        s->count = s->load;
        mpcore_timer_reload(s, 0);
    } else {
        s->count = 0;
    }
    mpcore_timer_update_irq(s);
}

static uint32_t mpcore_timer_read(mpcore_timer_state *s, int offset)
{
    int64_t val;
    switch (offset) {
    case 0: /* Load */
        return s->load;
        /* Fall through.  */
    case 4: /* Counter.  */
        if (((s->control & 1) == 0) || (s->count == 0))
            return 0;
        /* Slow and ugly, but hopefully won't happen too often.  */
        val = s->tick - qemu_get_clock_ns(vm_clock);
        val /= mpcore_timer_scale(s);
        if (val < 0)
            val = 0;
        return val;
    case 8: /* Control.  */
        return s->control;
    case 12: /* Interrupt status.  */
        return s->status;
    default:
        return 0;
    }
}

static void mpcore_timer_write(mpcore_timer_state *s, int offset,
                               uint32_t value)
{
    int64_t old;
    switch (offset) {
    case 0: /* Load */
        s->load = value;
        /* Fall through.  */
    case 4: /* Counter.  */
        if ((s->control & 1) && s->count) {
            /* Cancel the previous timer.  */
            qemu_del_timer(s->timer);
        }
        s->count = value;
        if (s->control & 1) {
            mpcore_timer_reload(s, 1);
        }
        break;
    case 8: /* Control.  */
        old = s->control;
        s->control = value;
        if (((old & 1) == 0) && (value & 1)) {
            if (s->count == 0 && (s->control & 2))
                s->count = s->load;
            mpcore_timer_reload(s, 1);
        }
        break;
    case 12: /* Interrupt status.  */
        s->status &= ~value;
        mpcore_timer_update_irq(s);
        break;
    }
}

static void mpcore_timer_init(mpcore_priv_state *mpcore,
                              mpcore_timer_state *s, int id)
{
    s->id = id;
    s->mpcore = mpcore;
    s->timer = qemu_new_timer_ns(vm_clock, mpcore_timer_tick, s);
}


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
    } else if (offset < 0xb00) {
        /* Timers.  */
        if (offset < 0x700) {
            id = gic_get_current_cpu();
        } else {
            id = (offset - 0x700) >> 8;
            if (id >= s->num_cpu) {
                return 0;
            }
        }
        id <<= 1;
        if (offset & 0x20)
          id++;
        return mpcore_timer_read(&s->timer[id], offset & 0xf);
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
    } else if (offset < 0xb00) {
        /* Timers.  */
        if (offset < 0x700) {
            id = gic_get_current_cpu();
        } else {
            id = (offset - 0x700) >> 8;
        }
        if (id < s->num_cpu) {
            id <<= 1;
            if (offset & 0x20)
              id++;
            mpcore_timer_write(&s->timer[id], offset & 0xf, value);
        }
        return;
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

static void mpcore_priv_map_setup(mpcore_priv_state *s)
{
    memory_region_init(&s->container, "mpcode-priv-container", 0x2000);
    memory_region_init_io(&s->iomem, &mpcore_priv_ops, s, "mpcode-priv",
                          0x1000);
    memory_region_add_subregion(&s->container, 0, &s->iomem);
    memory_region_add_subregion(&s->container, 0x1000, &s->gic.iomem);
}

static int mpcore_priv_init(SysBusDevice *dev)
{
    mpcore_priv_state *s = FROM_SYSBUSGIC(mpcore_priv_state, dev);
    int i;

    gic_init(&s->gic, s->num_cpu);
    mpcore_priv_map_setup(s);
    sysbus_init_mmio_region(dev, &s->container);
    for (i = 0; i < s->num_cpu * 2; i++) {
        mpcore_timer_init(s, &s->timer[i], i);
    }
    return 0;
}
