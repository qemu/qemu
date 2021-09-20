/*
 * ARM AMBA PrimeCell PL031 RTC
 *
 * Copyright (c) 2007 CodeSourcery
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/rtc/pl031.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/qapi-events-misc-target.h"

#define RTC_DR      0x00    /* Data read register */
#define RTC_MR      0x04    /* Match register */
#define RTC_LR      0x08    /* Data load register */
#define RTC_CR      0x0c    /* Control register */
#define RTC_IMSC    0x10    /* Interrupt mask and set register */
#define RTC_RIS     0x14    /* Raw interrupt status register */
#define RTC_MIS     0x18    /* Masked interrupt status register */
#define RTC_ICR     0x1c    /* Interrupt clear register */

static const unsigned char pl031_id[] = {
    0x31, 0x10, 0x14, 0x00,         /* Device ID        */
    0x0d, 0xf0, 0x05, 0xb1          /* Cell ID      */
};

static void pl031_update(PL031State *s)
{
    uint32_t flags = s->is & s->im;

    trace_pl031_irq_state(flags);
    qemu_set_irq(s->irq, flags);
}

static void pl031_interrupt(void * opaque)
{
    PL031State *s = (PL031State *)opaque;

    s->is = 1;
    trace_pl031_alarm_raised();
    pl031_update(s);
}

static uint32_t pl031_get_count(PL031State *s)
{
    int64_t now = qemu_clock_get_ns(rtc_clock);
    return s->tick_offset + now / NANOSECONDS_PER_SECOND;
}

static void pl031_set_alarm(PL031State *s)
{
    uint32_t ticks;

    /* The timer wraps around.  This subtraction also wraps in the same way,
       and gives correct results when alarm < now_ticks.  */
    ticks = s->mr - pl031_get_count(s);
    trace_pl031_set_alarm(ticks);
    if (ticks == 0) {
        timer_del(s->timer);
        pl031_interrupt(s);
    } else {
        int64_t now = qemu_clock_get_ns(rtc_clock);
        timer_mod(s->timer, now + (int64_t)ticks * NANOSECONDS_PER_SECOND);
    }
}

static uint64_t pl031_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL031State *s = (PL031State *)opaque;
    uint64_t r;

    switch (offset) {
    case RTC_DR:
        r = pl031_get_count(s);
        break;
    case RTC_MR:
        r = s->mr;
        break;
    case RTC_IMSC:
        r = s->im;
        break;
    case RTC_RIS:
        r = s->is;
        break;
    case RTC_LR:
        r = s->lr;
        break;
    case RTC_CR:
        /* RTC is permanently enabled.  */
        r = 1;
        break;
    case RTC_MIS:
        r = s->is & s->im;
        break;
    case 0xfe0 ... 0xfff:
        r = pl031_id[(offset - 0xfe0) >> 2];
        break;
    case RTC_ICR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031: read of write-only register at offset 0x%x\n",
                      (int)offset);
        r = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031_read: Bad offset 0x%x\n", (int)offset);
        r = 0;
        break;
    }

    trace_pl031_read(offset, r);
    return r;
}

static void pl031_write(void * opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL031State *s = (PL031State *)opaque;

    trace_pl031_write(offset, value);

    switch (offset) {
    case RTC_LR: {
        struct tm tm;

        s->tick_offset += value - pl031_get_count(s);

        qemu_get_timedate(&tm, s->tick_offset);
        qapi_event_send_rtc_change(qemu_timedate_diff(&tm));

        pl031_set_alarm(s);
        break;
    }
    case RTC_MR:
        s->mr = value;
        pl031_set_alarm(s);
        break;
    case RTC_IMSC:
        s->im = value & 1;
        pl031_update(s);
        break;
    case RTC_ICR:
        s->is &= ~value;
        pl031_update(s);
        break;
    case RTC_CR:
        /* Written value is ignored.  */
        break;

    case RTC_DR:
    case RTC_MIS:
    case RTC_RIS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031: write to read-only register at offset 0x%x\n",
                      (int)offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031_write: Bad offset 0x%x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps pl031_ops = {
    .read = pl031_read,
    .write = pl031_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pl031_init(Object *obj)
{
    PL031State *s = PL031(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    struct tm tm;

    memory_region_init_io(&s->iomem, obj, &pl031_ops, s, "pl031", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);
    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm) -
        qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;

    s->timer = timer_new_ns(rtc_clock, pl031_interrupt, s);
}

static void pl031_finalize(Object *obj)
{
    PL031State *s = PL031(obj);

    timer_free(s->timer);
}

static int pl031_pre_save(void *opaque)
{
    PL031State *s = opaque;

    /*
     * The PL031 device model code uses the tick_offset field, which is
     * the offset between what the guest RTC should read and what the
     * QEMU rtc_clock reads:
     *  guest_rtc = rtc_clock + tick_offset
     * and so
     *  tick_offset = guest_rtc - rtc_clock
     *
     * We want to migrate this offset, which sounds straightforward.
     * Unfortunately older versions of QEMU migrated a conversion of this
     * offset into an offset from the vm_clock. (This was in turn an
     * attempt to be compatible with even older QEMU versions, but it
     * has incorrect behaviour if the rtc_clock is not the same as the
     * vm_clock.) So we put the actual tick_offset into a migration
     * subsection, and the backwards-compatible time-relative-to-vm_clock
     * in the main migration state.
     *
     * Calculate base time relative to QEMU_CLOCK_VIRTUAL:
     */
    int64_t delta = qemu_clock_get_ns(rtc_clock) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tick_offset_vmstate = s->tick_offset + delta / NANOSECONDS_PER_SECOND;

    return 0;
}

static int pl031_pre_load(void *opaque)
{
    PL031State *s = opaque;

    s->tick_offset_migrated = false;
    return 0;
}

static int pl031_post_load(void *opaque, int version_id)
{
    PL031State *s = opaque;

    /*
     * If we got the tick_offset subsection, then we can just use
     * the value in that. Otherwise the source is an older QEMU and
     * has given us the offset from the vm_clock; convert it back to
     * an offset from the rtc_clock. This will cause time to incorrectly
     * go backwards compared to the host RTC, but this is unavoidable.
     */

    if (!s->tick_offset_migrated) {
        int64_t delta = qemu_clock_get_ns(rtc_clock) -
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->tick_offset = s->tick_offset_vmstate -
            delta / NANOSECONDS_PER_SECOND;
    }
    pl031_set_alarm(s);
    return 0;
}

static int pl031_tick_offset_post_load(void *opaque, int version_id)
{
    PL031State *s = opaque;

    s->tick_offset_migrated = true;
    return 0;
}

static bool pl031_tick_offset_needed(void *opaque)
{
    PL031State *s = opaque;

    return s->migrate_tick_offset;
}

static const VMStateDescription vmstate_pl031_tick_offset = {
    .name = "pl031/tick-offset",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pl031_tick_offset_needed,
    .post_load = pl031_tick_offset_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tick_offset, PL031State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pl031 = {
    .name = "pl031",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = pl031_pre_save,
    .pre_load = pl031_pre_load,
    .post_load = pl031_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tick_offset_vmstate, PL031State),
        VMSTATE_UINT32(mr, PL031State),
        VMSTATE_UINT32(lr, PL031State),
        VMSTATE_UINT32(cr, PL031State),
        VMSTATE_UINT32(im, PL031State),
        VMSTATE_UINT32(is, PL031State),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_pl031_tick_offset,
        NULL
    }
};

static Property pl031_properties[] = {
    /*
     * True to correctly migrate the tick offset of the RTC. False to
     * obtain backward migration compatibility with older QEMU versions,
     * at the expense of the guest RTC going backwards compared with the
     * host RTC when the VM is saved/restored if using -rtc host.
     * (Even if set to 'true' older QEMU can migrate forward to newer QEMU;
     * 'false' also permits newer QEMU to migrate to older QEMU.)
     */
    DEFINE_PROP_BOOL("migrate-tick-offset",
                     PL031State, migrate_tick_offset, true),
    DEFINE_PROP_END_OF_LIST()
};

static void pl031_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pl031;
    device_class_set_props(dc, pl031_properties);
}

static const TypeInfo pl031_info = {
    .name          = TYPE_PL031,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL031State),
    .instance_init = pl031_init,
    .instance_finalize = pl031_finalize,
    .class_init    = pl031_class_init,
};

static void pl031_register_types(void)
{
    type_register_static(&pl031_info);
}

type_init(pl031_register_types)
