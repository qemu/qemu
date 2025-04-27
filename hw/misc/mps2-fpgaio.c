/*
 * ARM MPS2 AN505 FPGAIO emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the "FPGA system control and I/O" block found
 * in the AN505 FPGA image for the MPS2 devboard.
 * It is documented in AN505:
 * https://developer.arm.com/documentation/dai0505/latest/
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/misc/mps2-fpgaio.h"
#include "hw/misc/led.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"

REG32(LED0, 0)
REG32(DBGCTRL, 4)
REG32(BUTTON, 8)
REG32(CLK1HZ, 0x10)
REG32(CLK100HZ, 0x14)
REG32(COUNTER, 0x18)
REG32(PRESCALE, 0x1c)
REG32(PSCNTR, 0x20)
REG32(SWITCH, 0x28)
REG32(MISC, 0x4c)

static uint32_t counter_from_tickoff(int64_t now, int64_t tick_offset, int frq)
{
    return muldiv64(now - tick_offset, frq, NANOSECONDS_PER_SECOND);
}

static int64_t tickoff_from_counter(int64_t now, uint32_t count, int frq)
{
    return now - muldiv64(count, NANOSECONDS_PER_SECOND, frq);
}

static void resync_counter(MPS2FPGAIO *s)
{
    /*
     * Update s->counter and s->pscntr to their true current values
     * by calculating how many times PSCNTR has ticked since the
     * last time we did a resync.
     */
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t elapsed = now - s->pscntr_sync_ticks;

    /*
     * Round elapsed down to a whole number of PSCNTR ticks, so we don't
     * lose time if we do multiple resyncs in a single tick.
     */
    uint64_t ticks = muldiv64(elapsed, s->prescale_clk, NANOSECONDS_PER_SECOND);

    /*
     * Work out what PSCNTR and COUNTER have moved to. We assume that
     * PSCNTR reloads from PRESCALE one tick-period after it hits zero,
     * and that COUNTER increments at the same moment.
     */
    if (ticks == 0) {
        /* We haven't ticked since the last time we were asked */
        return;
    } else if (ticks < s->pscntr) {
        /* We haven't yet reached zero, just reduce the PSCNTR */
        s->pscntr -= ticks;
    } else {
        if (s->prescale == 0) {
            /*
             * If the reload value is zero then the PSCNTR will stick
             * at zero once it reaches it, and so we will increment
             * COUNTER every tick after that.
             */
            s->counter += ticks - s->pscntr;
            s->pscntr = 0;
        } else {
            /*
             * This is the complicated bit. This ASCII art diagram gives an
             * example with PRESCALE==5 PSCNTR==7:
             *
             * ticks  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
             * PSCNTR 7  6  5  4  3  2  1  0  5  4  3  2  1  0  5
             * cinc                           1                 2
             * y            0  1  2  3  4  5  6  7  8  9 10 11 12
             * x            0  1  2  3  4  5  0  1  2  3  4  5  0
             *
             * where x = y % (s->prescale + 1)
             * and so PSCNTR = s->prescale - x
             * and COUNTER is incremented by y / (s->prescale + 1)
             *
             * The case where PSCNTR < PRESCALE works out the same,
             * though we must be careful to calculate y as 64-bit unsigned
             * for all parts of the expression.
             * y < 0 is not possible because that implies ticks < s->pscntr.
             */
            uint64_t y = ticks - s->pscntr + s->prescale;
            s->pscntr = s->prescale - (y % (s->prescale + 1));
            s->counter += y / (s->prescale + 1);
        }
    }

    /*
     * Only advance the sync time to the timestamp of the last PSCNTR tick,
     * not all the way to 'now', so we don't lose time if we do multiple
     * resyncs in a single tick.
     */
    s->pscntr_sync_ticks += muldiv64(ticks, NANOSECONDS_PER_SECOND,
                                     s->prescale_clk);
}

static uint64_t mps2_fpgaio_read(void *opaque, hwaddr offset, unsigned size)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(opaque);
    uint64_t r;
    int64_t now;

    switch (offset) {
    case A_LED0:
        r = s->led0;
        break;
    case A_DBGCTRL:
        if (!s->has_dbgctrl) {
            goto bad_offset;
        }
        r = s->dbgctrl;
        break;
    case A_BUTTON:
        /* User-pressable board buttons. We don't model that, so just return
         * zeroes.
         */
        r = 0;
        break;
    case A_PRESCALE:
        r = s->prescale;
        break;
    case A_MISC:
        r = s->misc;
        break;
    case A_CLK1HZ:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        r = counter_from_tickoff(now, s->clk1hz_tick_offset, 1);
        break;
    case A_CLK100HZ:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        r = counter_from_tickoff(now, s->clk100hz_tick_offset, 100);
        break;
    case A_COUNTER:
        resync_counter(s);
        r = s->counter;
        break;
    case A_PSCNTR:
        resync_counter(s);
        r = s->pscntr;
        break;
    case A_SWITCH:
        if (!s->has_switches) {
            goto bad_offset;
        }
        /* User-togglable board switches. We don't model that, so report 0. */
        r = 0;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 FPGAIO read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }

    trace_mps2_fpgaio_read(offset, r, size);
    return r;
}

static void mps2_fpgaio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(opaque);
    int64_t now;

    trace_mps2_fpgaio_write(offset, value, size);

    switch (offset) {
    case A_LED0:
        if (s->num_leds != 0) {
            uint32_t i;

            s->led0 = value & MAKE_64BIT_MASK(0, s->num_leds);
            for (i = 0; i < s->num_leds; i++) {
                led_set_state(s->led[i], extract64(value, i, 1));
            }
        }
        break;
    case A_DBGCTRL:
        if (!s->has_dbgctrl) {
            goto bad_offset;
        }
        qemu_log_mask(LOG_UNIMP,
                      "MPS2 FPGAIO: DBGCTRL unimplemented\n");
        s->dbgctrl = value;
        break;
    case A_PRESCALE:
        resync_counter(s);
        s->prescale = value;
        break;
    case A_MISC:
        /* These are control bits for some of the other devices on the
         * board (SPI, CLCD, etc). We don't implement that yet, so just
         * make the bits read as written.
         */
        qemu_log_mask(LOG_UNIMP,
                      "MPS2 FPGAIO: MISC control bits unimplemented\n");
        s->misc = value;
        break;
    case A_CLK1HZ:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->clk1hz_tick_offset = tickoff_from_counter(now, value, 1);
        break;
    case A_CLK100HZ:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->clk100hz_tick_offset = tickoff_from_counter(now, value, 100);
        break;
    case A_COUNTER:
        resync_counter(s);
        s->counter = value;
        break;
    case A_PSCNTR:
        resync_counter(s);
        s->pscntr = value;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 FPGAIO write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps mps2_fpgaio_ops = {
    .read = mps2_fpgaio_read,
    .write = mps2_fpgaio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mps2_fpgaio_reset(DeviceState *dev)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(dev);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    trace_mps2_fpgaio_reset();
    s->led0 = 0;
    s->prescale = 0;
    s->misc = 0;
    s->clk1hz_tick_offset = tickoff_from_counter(now, 0, 1);
    s->clk100hz_tick_offset = tickoff_from_counter(now, 0, 100);
    s->counter = 0;
    s->pscntr = 0;
    s->pscntr_sync_ticks = now;

    for (size_t i = 0; i < s->num_leds; i++) {
        device_cold_reset(DEVICE(s->led[i]));
    }
}

static void mps2_fpgaio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MPS2FPGAIO *s = MPS2_FPGAIO(obj);

    memory_region_init_io(&s->iomem, obj, &mps2_fpgaio_ops, s,
                          "mps2-fpgaio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void mps2_fpgaio_realize(DeviceState *dev, Error **errp)
{
    MPS2FPGAIO *s = MPS2_FPGAIO(dev);
    uint32_t i;

    if (s->num_leds > MPS2FPGAIO_MAX_LEDS) {
        error_setg(errp, "num-leds cannot be greater than %d",
                   MPS2FPGAIO_MAX_LEDS);
        return;
    }

    for (i = 0; i < s->num_leds; i++) {
        g_autofree char *ledname = g_strdup_printf("USERLED%d", i);
        s->led[i] = led_create_simple(OBJECT(dev), GPIO_POLARITY_ACTIVE_HIGH,
                                      LED_COLOR_GREEN, ledname);
    }
}

static const VMStateDescription mps2_fpgaio_vmstate = {
    .name = "mps2-fpgaio",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(led0, MPS2FPGAIO),
        VMSTATE_UINT32(prescale, MPS2FPGAIO),
        VMSTATE_UINT32(misc, MPS2FPGAIO),
        VMSTATE_UINT32(dbgctrl, MPS2FPGAIO),
        VMSTATE_INT64(clk1hz_tick_offset, MPS2FPGAIO),
        VMSTATE_INT64(clk100hz_tick_offset, MPS2FPGAIO),
        VMSTATE_UINT32(counter, MPS2FPGAIO),
        VMSTATE_UINT32(pscntr, MPS2FPGAIO),
        VMSTATE_INT64(pscntr_sync_ticks, MPS2FPGAIO),
        VMSTATE_END_OF_LIST()
    },
};

static const Property mps2_fpgaio_properties[] = {
    /* Frequency of the prescale counter */
    DEFINE_PROP_UINT32("prescale-clk", MPS2FPGAIO, prescale_clk, 20000000),
    /* Number of LEDs controlled by LED0 register */
    DEFINE_PROP_UINT32("num-leds", MPS2FPGAIO, num_leds, 2),
    DEFINE_PROP_BOOL("has-switches", MPS2FPGAIO, has_switches, false),
    DEFINE_PROP_BOOL("has-dbgctrl", MPS2FPGAIO, has_dbgctrl, false),
};

static void mps2_fpgaio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &mps2_fpgaio_vmstate;
    dc->realize = mps2_fpgaio_realize;
    device_class_set_legacy_reset(dc, mps2_fpgaio_reset);
    device_class_set_props(dc, mps2_fpgaio_properties);
}

static const TypeInfo mps2_fpgaio_info = {
    .name = TYPE_MPS2_FPGAIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPS2FPGAIO),
    .instance_init = mps2_fpgaio_init,
    .class_init = mps2_fpgaio_class_init,
};

static void mps2_fpgaio_register_types(void)
{
    type_register_static(&mps2_fpgaio_info);
}

type_init(mps2_fpgaio_register_types);
