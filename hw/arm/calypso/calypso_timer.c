/*
 * calypso_timer.c — Calypso GP/Watchdog Timer
 *
 * 16-bit down-counter with auto-reload, prescaler, and IRQ.
 * Calypso base clock: 13 MHz. Effective rate = 13 MHz / (prescaler + 1).
 *
 * Register map (16-bit, offsets from base):
 *   0x00  CNTL       Control (bit0=start, bit1=auto-reload, bit2=irq-enable)
 *   0x02  LOAD       Reload value (written before starting)
 *   0x04  READ       Current count (read-only)
 *   0x06  PRESCALER  Clock divider
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "calypso_timer.h"

#define TIMER_CTRL_START   (1 << 0)
#define TIMER_CTRL_RELOAD  (1 << 1)
#define TIMER_CTRL_IRQ_EN  (1 << 2)

#define CALYPSO_BASE_CLK   13000000LL  /* 13 MHz */

static void calypso_timer_tick(void *opaque)
{
    CalypsoTimerState *s = CALYPSO_TIMER(opaque);

    if (!s->running) {
        return;
    }

    s->count--;
    if (s->count == 0) {
        /* Fire IRQ if enabled */
        if (s->ctrl & TIMER_CTRL_IRQ_EN) {
            qemu_irq_pulse(s->irq);
        }
        /* Auto-reload or stop */
        if (s->ctrl & TIMER_CTRL_RELOAD) {
            s->count = s->load;
        } else {
            s->running = false;
            return;
        }
    }

    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->tick_ns);
}

static void calypso_timer_start(CalypsoTimerState *s)
{
    if (s->load == 0) {
        return;
    }
    s->count = s->load;
    s->running = true;
    int64_t freq = CALYPSO_BASE_CLK / (s->prescaler + 1);
    s->tick_ns = NANOSECONDS_PER_SECOND / freq;
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->tick_ns);
}

/* ---- MMIO ---- */

static uint64_t calypso_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoTimerState *s = CALYPSO_TIMER(opaque);

    switch (offset) {
    case 0x00: return s->ctrl;
    case 0x02: return s->load;
    case 0x04: return s->count;
    case 0x06: return s->prescaler;
    default:   return 0;
    }
}

static void calypso_timer_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    CalypsoTimerState *s = CALYPSO_TIMER(opaque);

    switch (offset) {
    case 0x00: /* CNTL */
        s->ctrl = value & 0x07;
        if (value & TIMER_CTRL_START) {
            calypso_timer_start(s);
        } else {
            s->running = false;
            timer_del(s->timer);
        }
        break;
    case 0x02: /* LOAD */
        s->load = value;
        break;
    case 0x06: /* PRESCALER */
        s->prescaler = value;
        break;
    }
}

static const MemoryRegionOps calypso_timer_ops = {
    .read = calypso_timer_read,
    .write = calypso_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* ---- QOM lifecycle ---- */

static void calypso_timer_realize(DeviceState *dev, Error **errp)
{
    CalypsoTimerState *s = CALYPSO_TIMER(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &calypso_timer_ops, s,
                          "calypso-timer", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, calypso_timer_tick, s);
}

static void calypso_timer_reset(DeviceState *dev)
{
    CalypsoTimerState *s = CALYPSO_TIMER(dev);

    s->load = 0;
    s->count = 0;
    s->ctrl = 0;
    s->prescaler = 0;
    s->running = false;
    timer_del(s->timer);
}

static void calypso_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = calypso_timer_realize;
    device_class_set_legacy_reset(dc, calypso_timer_reset);
    dc->desc = "Calypso GP/Watchdog timer";
}

static const TypeInfo calypso_timer_info = {
    .name          = TYPE_CALYPSO_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoTimerState),
    .class_init    = calypso_timer_class_init,
};

static void calypso_timer_register_types(void)
{
    type_register_static(&calypso_timer_info);
}

type_init(calypso_timer_register_types)
