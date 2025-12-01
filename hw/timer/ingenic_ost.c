/*
 * Ingenic XBurst2 Operating System Timer (OST)
 *
 * Copyright (C) 2024 OpenSensor
 *
 * This implements the OST timer used in Ingenic T41/XBurst2 SoCs.
 * The OST provides:
 *  - Global OST: 64-bit free-running counter for clocksource
 *  - Core OST: Per-CPU timer for clock events
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#define TYPE_INGENIC_OST "ingenic-ost"
#define INGENIC_OST(obj) OBJECT_CHECK(IngenicOSTState, (obj), TYPE_INGENIC_OST)

/* Global OST registers (at base + 0x0) */
#define G_OSTCCR    0x00    /* Clock Control Register */
#define G_OSTER     0x04    /* Enable Register */
#define G_OSTCR     0x08    /* Clear Register */
#define G_OSTCNTH   0x0C    /* Counter High 32 bits */
#define G_OSTCNTL   0x10    /* Counter Low 32 bits */
#define G_OSTCNTB   0x14    /* Counter Buffer */

/* Core OST registers (at base + 0x10000 + cpu*0x100) */
#define OSTCCR      0x00    /* Clock Control Register */
#define OSTER       0x04    /* Enable Register */
#define OSTCR       0x08    /* Clear Register */
#define OSTFR       0x0C    /* Flag Register */
#define OSTMR       0x10    /* Mask Register */
#define OSTDFR      0x14    /* Data Full Register (compare value) */
#define OSTCNT      0x18    /* Counter */

/* Timer frequency: 24MHz / 1 = 24MHz (configurable via OSTCCR) */
#define OST_FREQ    24000000

typedef struct IngenicOSTState {
    SysBusDevice parent_obj;

    MemoryRegion global_iomem;
    MemoryRegion core_iomem;
    qemu_irq irq;

    /* Global OST state */
    uint32_t g_ostccr;
    uint32_t g_oster;
    uint64_t g_counter_offset;  /* Offset from virtual clock */

    /* Core OST state (per-CPU, we support 2 CPUs) */
    uint32_t core_oster[2];
    uint32_t core_ostfr[2];
    uint32_t core_ostmr[2];
    uint32_t core_ostdfr[2];
    uint64_t core_counter_start[2];

    QEMUTimer *core_timer[2];
    uint32_t freq;
} IngenicOSTState;

/* Get the current global counter value */
static uint64_t ingenic_ost_get_global_count(IngenicOSTState *s)
{
    if (!(s->g_oster & 1)) {
        return 0;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* Convert ns to timer ticks */
    return s->g_counter_offset + muldiv64(now, s->freq, NANOSECONDS_PER_SECOND);
}

/* Get the current core counter value */
static uint32_t ingenic_ost_get_core_count(IngenicOSTState *s, int cpu)
{
    if (!(s->core_oster[cpu] & 1)) {
        return 0;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t elapsed = now - s->core_counter_start[cpu];
    return (uint32_t)muldiv64(elapsed, s->freq, NANOSECONDS_PER_SECOND);
}

static void ingenic_ost_update_irq(IngenicOSTState *s, int cpu)
{
    /* IRQ is raised if flag is set and not masked */
    if ((s->core_ostfr[cpu] & 1) && !(s->core_ostmr[cpu] & 1)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void ingenic_ost_core_timer_cb(void *opaque)
{
    IngenicOSTState *s = INGENIC_OST(opaque);
    int cpu = 0;  /* For now, handle CPU 0 only */

    qemu_log_mask(LOG_GUEST_ERROR, "OST: timer cb, oster=%d, ostfr=%d, ostmr=%d, ostdfr=%d\n",
                  s->core_oster[cpu], s->core_ostfr[cpu], s->core_ostmr[cpu], s->core_ostdfr[cpu]);

    if (s->core_oster[cpu] & 1) {
        /* Set the interrupt flag */
        s->core_ostfr[cpu] |= 1;
        ingenic_ost_update_irq(s, cpu);

        /* Restart the counter from 0 for periodic mode */
        s->core_counter_start[cpu] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        /* Schedule next interrupt */
        if (s->core_ostdfr[cpu] > 0) {
            int64_t next = s->core_counter_start[cpu] +
                muldiv64(s->core_ostdfr[cpu], NANOSECONDS_PER_SECOND, s->freq);
            timer_mod(s->core_timer[cpu], next);
        }
    }
}

static uint64_t ingenic_ost_global_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    IngenicOSTState *s = INGENIC_OST(opaque);
    uint64_t count;

    switch (offset) {
    case G_OSTCCR:
        return s->g_ostccr;
    case G_OSTER:
        return s->g_oster;
    case G_OSTCR:
        return 0;
    case G_OSTCNTH:
        count = ingenic_ost_get_global_count(s);
        return (count >> 32) & 0xFFFFFFFF;
    case G_OSTCNTL:
        count = ingenic_ost_get_global_count(s);
        return count & 0xFFFFFFFF;
    case G_OSTCNTB:
        count = ingenic_ost_get_global_count(s);
        return (count >> 32) & 0xFFFFFFFF;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_ost: global read 0x%"HWADDR_PRIx"\n",
                      offset);
        return 0;
    }
}

static void ingenic_ost_global_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    IngenicOSTState *s = INGENIC_OST(opaque);

    switch (offset) {
    case G_OSTCCR:
        s->g_ostccr = value;
        break;
    case G_OSTER:
        s->g_oster = value & 1;
        if (s->g_oster) {
            /* Timer started, record the offset */
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->g_counter_offset = -muldiv64(now, s->freq, NANOSECONDS_PER_SECOND);
        }
        break;
    case G_OSTCR:
        if (value & 1) {
            /* Clear the counter */
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->g_counter_offset = -muldiv64(now, s->freq, NANOSECONDS_PER_SECOND);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_ost: global write 0x%"HWADDR_PRIx
                      " = 0x%"PRIx64"\n", offset, value);
        break;
    }
}

static uint64_t ingenic_ost_core_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    IngenicOSTState *s = INGENIC_OST(opaque);
    int cpu = (offset >> 8) & 1;  /* CPU 0 or 1 based on address */
    hwaddr reg = offset & 0xFF;

    switch (reg) {
    case OSTCCR:
        return 0;  /* Clock control */
    case OSTER:
        return s->core_oster[cpu];
    case OSTCR:
        return 0;
    case OSTFR:
        return s->core_ostfr[cpu];
    case OSTMR:
        return s->core_ostmr[cpu];
    case OSTDFR:
        return s->core_ostdfr[cpu];
    case OSTCNT:
        return ingenic_ost_get_core_count(s, cpu);
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_ost: core read cpu%d reg 0x%"
                      HWADDR_PRIx"\n", cpu, reg);
        return 0;
    }
}

static void ingenic_ost_core_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    IngenicOSTState *s = INGENIC_OST(opaque);
    int cpu = (offset >> 8) & 1;
    hwaddr reg = offset & 0xFF;

    switch (reg) {
    case OSTCCR:
        /* Clock control - ignore for now */
        break;
    case OSTER:
        s->core_oster[cpu] = value & 1;
        qemu_log_mask(LOG_GUEST_ERROR, "OST: cpu%d OSTER write %d, ostdfr=%d\n",
                      cpu, (int)(value & 1), s->core_ostdfr[cpu]);
        if (value & 1) {
            /* Start the timer */
            s->core_counter_start[cpu] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            if (s->core_ostdfr[cpu] > 0) {
                int64_t next = s->core_counter_start[cpu] +
                    muldiv64(s->core_ostdfr[cpu], NANOSECONDS_PER_SECOND, s->freq);
                qemu_log_mask(LOG_GUEST_ERROR, "OST: cpu%d timer armed, next in %lld ns\n",
                              cpu, (long long)(next - s->core_counter_start[cpu]));
                timer_mod(s->core_timer[cpu], next);
            }
        } else {
            /* Stop the timer */
            timer_del(s->core_timer[cpu]);
        }
        break;
    case OSTCR:
        if (value & 1) {
            /* Clear the counter */
            s->core_counter_start[cpu] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        break;
    case OSTFR:
        /* Writing 0 clears the flag */
        if (value == 0) {
            s->core_ostfr[cpu] = 0;
            ingenic_ost_update_irq(s, cpu);
        }
        break;
    case OSTMR:
        s->core_ostmr[cpu] = value & 1;
        ingenic_ost_update_irq(s, cpu);
        break;
    case OSTDFR:
        s->core_ostdfr[cpu] = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_ost: core write cpu%d reg 0x%"
                      HWADDR_PRIx" = 0x%"PRIx64"\n", cpu, reg, value);
        break;
    }
}

static const MemoryRegionOps ingenic_ost_global_ops = {
    .read = ingenic_ost_global_read,
    .write = ingenic_ost_global_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps ingenic_ost_core_ops = {
    .read = ingenic_ost_core_read,
    .write = ingenic_ost_core_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ingenic_ost_realize(DeviceState *dev, Error **errp)
{
    IngenicOSTState *s = INGENIC_OST(dev);

    s->core_timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     ingenic_ost_core_timer_cb, s);
    s->core_timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     ingenic_ost_core_timer_cb, s);
}

static void ingenic_ost_init(Object *obj)
{
    IngenicOSTState *s = INGENIC_OST(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->global_iomem, obj, &ingenic_ost_global_ops, s,
                          "ingenic-ost-global", 0x10000);
    memory_region_init_io(&s->core_iomem, obj, &ingenic_ost_core_ops, s,
                          "ingenic-ost-core", 0x10000);

    sysbus_init_mmio(sbd, &s->global_iomem);
    sysbus_init_mmio(sbd, &s->core_iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->freq = OST_FREQ;
    s->g_oster = 1;  /* Start enabled by default */
}

static void ingenic_ost_reset(DeviceState *dev)
{
    IngenicOSTState *s = INGENIC_OST(dev);
    int i;

    s->g_ostccr = 0;
    s->g_oster = 1;  /* Start enabled */
    s->g_counter_offset = 0;

    for (i = 0; i < 2; i++) {
        s->core_oster[i] = 0;
        s->core_ostfr[i] = 0;
        s->core_ostmr[i] = 0;
        s->core_ostdfr[i] = 0;
        s->core_counter_start[i] = 0;
        if (s->core_timer[i]) {
            timer_del(s->core_timer[i]);
        }
    }
}

static const Property ingenic_ost_properties[] = {
    DEFINE_PROP_UINT32("freq", IngenicOSTState, freq, OST_FREQ),
};

static void ingenic_ost_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_ost_realize;
    device_class_set_legacy_reset(dc, ingenic_ost_reset);
    device_class_set_props(dc, ingenic_ost_properties);
}

static const TypeInfo ingenic_ost_info = {
    .name          = TYPE_INGENIC_OST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicOSTState),
    .instance_init = ingenic_ost_init,
    .class_init    = ingenic_ost_class_init,
};

static void ingenic_ost_register_types(void)
{
    type_register_static(&ingenic_ost_info);
}

type_init(ingenic_ost_register_types)

