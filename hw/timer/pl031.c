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
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

//#define DEBUG_PL031

#ifdef DEBUG_PL031
#define DPRINTF(fmt, ...) \
do { printf("pl031: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define RTC_DR      0x00    /* Data read register */
#define RTC_MR      0x04    /* Match register */
#define RTC_LR      0x08    /* Data load register */
#define RTC_CR      0x0c    /* Control register */
#define RTC_IMSC    0x10    /* Interrupt mask and set register */
#define RTC_RIS     0x14    /* Raw interrupt status register */
#define RTC_MIS     0x18    /* Masked interrupt status register */
#define RTC_ICR     0x1c    /* Interrupt clear register */

#define TYPE_PL031 "pl031"
#define PL031(obj) OBJECT_CHECK(PL031State, (obj), TYPE_PL031)

typedef struct PL031State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    /* Needed to preserve the tick_count across migration, even if the
     * absolute value of the rtc_clock is different on the source and
     * destination.
     */
    uint32_t tick_offset_vmstate;
    uint32_t tick_offset;

    uint32_t mr;
    uint32_t lr;
    uint32_t cr;
    uint32_t im;
    uint32_t is;
} PL031State;

static const unsigned char pl031_id[] = {
    0x31, 0x10, 0x14, 0x00,         /* Device ID        */
    0x0d, 0xf0, 0x05, 0xb1          /* Cell ID      */
};

static void pl031_update(PL031State *s)
{
    qemu_set_irq(s->irq, s->is & s->im);
}

static void pl031_interrupt(void * opaque)
{
    PL031State *s = (PL031State *)opaque;

    s->is = 1;
    DPRINTF("Alarm raised\n");
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
    DPRINTF("Alarm set in %ud ticks\n", ticks);
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

    if (offset >= 0xfe0  &&  offset < 0x1000)
        return pl031_id[(offset - 0xfe0) >> 2];

    switch (offset) {
    case RTC_DR:
        return pl031_get_count(s);
    case RTC_MR:
        return s->mr;
    case RTC_IMSC:
        return s->im;
    case RTC_RIS:
        return s->is;
    case RTC_LR:
        return s->lr;
    case RTC_CR:
        /* RTC is permanently enabled.  */
        return 1;
    case RTC_MIS:
        return s->is & s->im;
    case RTC_ICR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031: read of write-only register at offset 0x%x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl031_read: Bad offset 0x%x\n", (int)offset);
        break;
    }

    return 0;
}

static void pl031_write(void * opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL031State *s = (PL031State *)opaque;


    switch (offset) {
    case RTC_LR:
        s->tick_offset += value - pl031_get_count(s);
        pl031_set_alarm(s);
        break;
    case RTC_MR:
        s->mr = value;
        pl031_set_alarm(s);
        break;
    case RTC_IMSC:
        s->im = value & 1;
        DPRINTF("Interrupt mask %d\n", s->im);
        pl031_update(s);
        break;
    case RTC_ICR:
        /* The PL031 documentation (DDI0224B) states that the interrupt is
           cleared when bit 0 of the written value is set.  However the
           arm926e documentation (DDI0287B) states that the interrupt is
           cleared when any value is written.  */
        DPRINTF("Interrupt cleared");
        s->is = 0;
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

static void pl031_pre_save(void *opaque)
{
    PL031State *s = opaque;

    /* tick_offset is base_time - rtc_clock base time.  Instead, we want to
     * store the base time relative to the QEMU_CLOCK_VIRTUAL for backwards-compatibility.  */
    int64_t delta = qemu_clock_get_ns(rtc_clock) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tick_offset_vmstate = s->tick_offset + delta / NANOSECONDS_PER_SECOND;
}

static int pl031_post_load(void *opaque, int version_id)
{
    PL031State *s = opaque;

    int64_t delta = qemu_clock_get_ns(rtc_clock) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tick_offset = s->tick_offset_vmstate - delta / NANOSECONDS_PER_SECOND;
    pl031_set_alarm(s);
    return 0;
}

static const VMStateDescription vmstate_pl031 = {
    .name = "pl031",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = pl031_pre_save,
    .post_load = pl031_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tick_offset_vmstate, PL031State),
        VMSTATE_UINT32(mr, PL031State),
        VMSTATE_UINT32(lr, PL031State),
        VMSTATE_UINT32(cr, PL031State),
        VMSTATE_UINT32(im, PL031State),
        VMSTATE_UINT32(is, PL031State),
        VMSTATE_END_OF_LIST()
    }
};

static void pl031_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pl031;
}

static const TypeInfo pl031_info = {
    .name          = TYPE_PL031,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL031State),
    .instance_init = pl031_init,
    .class_init    = pl031_class_init,
};

static void pl031_register_types(void)
{
    type_register_static(&pl031_info);
}

type_init(pl031_register_types)
