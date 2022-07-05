/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongarch LS7A Real Time Clock emulation
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "include/hw/register.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/unimp.h"
#include "sysemu/rtc.h"
#include "hw/registerfields.h"

#define SYS_TOYTRIM        0x20
#define SYS_TOYWRITE0      0x24
#define SYS_TOYWRITE1      0x28
#define SYS_TOYREAD0       0x2C
#define SYS_TOYREAD1       0x30
#define SYS_TOYMATCH0      0x34
#define SYS_TOYMATCH1      0x38
#define SYS_TOYMATCH2      0x3C
#define SYS_RTCCTRL        0x40
#define SYS_RTCTRIM        0x60
#define SYS_RTCWRTIE0      0x64
#define SYS_RTCREAD0       0x68
#define SYS_RTCMATCH0      0x6C
#define SYS_RTCMATCH1      0x70
#define SYS_RTCMATCH2      0x74

#define LS7A_RTC_FREQ     32768
#define TIMER_NUMS        3
/*
 * Shift bits and filed mask
 */

FIELD(TOY, MON, 26, 6)
FIELD(TOY, DAY, 21, 5)
FIELD(TOY, HOUR, 16, 5)
FIELD(TOY, MIN, 10, 6)
FIELD(TOY, SEC, 4, 6)
FIELD(TOY, MSEC, 0, 4)

FIELD(TOY_MATCH, YEAR, 26, 6)
FIELD(TOY_MATCH, MON, 22, 4)
FIELD(TOY_MATCH, DAY, 17, 5)
FIELD(TOY_MATCH, HOUR, 12, 5)
FIELD(TOY_MATCH, MIN, 6, 6)
FIELD(TOY_MATCH, SEC, 0, 6)

FIELD(RTC_CTRL, RTCEN, 13, 1)
FIELD(RTC_CTRL, TOYEN, 11, 1)
FIELD(RTC_CTRL, EO, 8, 1)

#define TYPE_LS7A_RTC "ls7a_rtc"
OBJECT_DECLARE_SIMPLE_TYPE(LS7ARtcState, LS7A_RTC)

struct LS7ARtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    /*
     * Needed to preserve the tick_count across migration, even if the
     * absolute value of the rtc_clock is different on the source and
     * destination.
     */
    int64_t offset_toy;
    int64_t offset_rtc;
    int64_t data;
    int tidx;
    uint32_t toymatch[3];
    uint32_t toytrim;
    uint32_t cntrctl;
    uint32_t rtctrim;
    uint32_t rtccount;
    uint32_t rtcmatch[3];
    QEMUTimer *toy_timer[TIMER_NUMS];
    QEMUTimer *rtc_timer[TIMER_NUMS];
    qemu_irq irq;
};

/* switch nanoseconds time to rtc ticks */
static uint64_t ls7a_rtc_ticks(void)
{
    return qemu_clock_get_ns(rtc_clock) * LS7A_RTC_FREQ / NANOSECONDS_PER_SECOND;
}

/* switch rtc ticks to nanoseconds */
static uint64_t ticks_to_ns(uint64_t ticks)
{
    return ticks * NANOSECONDS_PER_SECOND / LS7A_RTC_FREQ;
}

static bool toy_enabled(LS7ARtcState *s)
{
    return FIELD_EX32(s->cntrctl, RTC_CTRL, TOYEN) &&
           FIELD_EX32(s->cntrctl, RTC_CTRL, EO);
}

static bool rtc_enabled(LS7ARtcState *s)
{
    return FIELD_EX32(s->cntrctl, RTC_CTRL, RTCEN) &&
           FIELD_EX32(s->cntrctl, RTC_CTRL, EO);
}

/* parse struct tm to toy value */
static uint64_t toy_time_to_val_mon(const struct tm *tm)
{
    uint64_t val = 0;

    val = FIELD_DP32(val, TOY, MON, tm->tm_mon + 1);
    val = FIELD_DP32(val, TOY, DAY, tm->tm_mday);
    val = FIELD_DP32(val, TOY, HOUR, tm->tm_hour);
    val = FIELD_DP32(val, TOY, MIN, tm->tm_min);
    val = FIELD_DP32(val, TOY, SEC, tm->tm_sec);
    return val;
}

static void toymatch_val_to_time(LS7ARtcState *s, uint64_t val, struct tm *tm)
{
    qemu_get_timedate(tm, s->offset_toy);
    tm->tm_sec = FIELD_EX32(val, TOY_MATCH, SEC);
    tm->tm_min = FIELD_EX32(val, TOY_MATCH, MIN);
    tm->tm_hour = FIELD_EX32(val, TOY_MATCH, HOUR);
    tm->tm_mday = FIELD_EX32(val, TOY_MATCH, DAY);
    tm->tm_mon = FIELD_EX32(val, TOY_MATCH, MON) - 1;
    tm->tm_year += (FIELD_EX32(val, TOY_MATCH, YEAR) - (tm->tm_year & 0x3f));
}

static void toymatch_write(LS7ARtcState *s, uint64_t val, int num)
{
    int64_t now, expire_time;
    struct tm tm = {};

    /* it do not support write when toy disabled */
    if (toy_enabled(s)) {
        s->toymatch[num] = val;
        /* calculate expire time */
        now = qemu_clock_get_ms(rtc_clock);
        toymatch_val_to_time(s, val, &tm);
        expire_time = now + (qemu_timedate_diff(&tm) - s->offset_toy) * 1000;
        timer_mod(s->toy_timer[num], expire_time);
    }
}

static void rtcmatch_write(LS7ARtcState *s, uint64_t val, int num)
{
    uint64_t expire_ns;

    /* it do not support write when toy disabled */
    if (rtc_enabled(s)) {
        s->rtcmatch[num] = val;
        /* calculate expire time */
        expire_ns = ticks_to_ns(val) - ticks_to_ns(s->offset_rtc);
        timer_mod_ns(s->rtc_timer[num], expire_ns);
    }
}

static void ls7a_toy_stop(LS7ARtcState *s)
{
    int i;

    /* delete timers, and when re-enabled, recalculate expire time */
    for (i = 0; i < TIMER_NUMS; i++) {
        timer_del(s->toy_timer[i]);
    }
}

static void ls7a_rtc_stop(LS7ARtcState *s)
{
    int i;

    /* delete timers, and when re-enabled, recalculate expire time */
    for (i = 0; i < TIMER_NUMS; i++) {
        timer_del(s->rtc_timer[i]);
    }
}

static void ls7a_toy_start(LS7ARtcState *s)
{
    int i;
    uint64_t expire_time, now;
    struct tm tm = {};

    now = qemu_clock_get_ms(rtc_clock);

    /* recalculate expire time and enable timer */
    for (i = 0; i < TIMER_NUMS; i++) {
        toymatch_val_to_time(s, s->toymatch[i], &tm);
        expire_time = now + (qemu_timedate_diff(&tm) - s->offset_toy) * 1000;
        timer_mod(s->toy_timer[i], expire_time);
    }
}

static void ls7a_rtc_start(LS7ARtcState *s)
{
    int i;
    uint64_t expire_time;

    /* recalculate expire time and enable timer */
    for (i = 0; i < TIMER_NUMS; i++) {
        expire_time = ticks_to_ns(s->rtcmatch[i]) - ticks_to_ns(s->offset_rtc);
        timer_mod_ns(s->rtc_timer[i], expire_time);
    }
}

static uint64_t ls7a_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    LS7ARtcState *s = LS7A_RTC(opaque);
    struct tm tm;
    int val = 0;

    switch (addr) {
    case SYS_TOYREAD0:
        if (toy_enabled(s)) {
            qemu_get_timedate(&tm, s->offset_toy);
            val = toy_time_to_val_mon(&tm);
        } else {
            /* return 0 when toy disabled */
            val = 0;
        }
        break;
    case SYS_TOYREAD1:
        if (toy_enabled(s)) {
            qemu_get_timedate(&tm, s->offset_toy);
            val = tm.tm_year;
        } else {
            /* return 0 when toy disabled */
            val = 0;
        }
        break;
    case SYS_TOYMATCH0:
        val = s->toymatch[0];
        break;
    case SYS_TOYMATCH1:
        val = s->toymatch[1];
        break;
    case SYS_TOYMATCH2:
        val = s->toymatch[2];
        break;
    case SYS_RTCCTRL:
        val = s->cntrctl;
        break;
    case SYS_RTCREAD0:
        if (rtc_enabled(s)) {
            val = ls7a_rtc_ticks() + s->offset_rtc;
        } else {
            /* return 0 when rtc disabled */
            val = 0;
        }
        break;
    case SYS_RTCMATCH0:
        val = s->rtcmatch[0];
        break;
    case SYS_RTCMATCH1:
        val = s->rtcmatch[1];
        break;
    case SYS_RTCMATCH2:
        val = s->rtcmatch[2];
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void ls7a_rtc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    int old_toyen, old_rtcen, new_toyen, new_rtcen;
    LS7ARtcState *s = LS7A_RTC(opaque);
    struct tm tm;

    switch (addr) {
    case SYS_TOYWRITE0:
        /* it do not support write when toy disabled */
        if (toy_enabled(s)) {
            qemu_get_timedate(&tm, s->offset_toy);
            tm.tm_sec = FIELD_EX32(val, TOY, SEC);
            tm.tm_min = FIELD_EX32(val, TOY, MIN);
            tm.tm_hour = FIELD_EX32(val, TOY, HOUR);
            tm.tm_mday = FIELD_EX32(val, TOY, DAY);
            tm.tm_mon = FIELD_EX32(val, TOY, MON) - 1;
            s->offset_toy = qemu_timedate_diff(&tm);
        }
    break;
    case SYS_TOYWRITE1:
        if (toy_enabled(s)) {
            qemu_get_timedate(&tm, s->offset_toy);
            tm.tm_year = val;
            s->offset_toy = qemu_timedate_diff(&tm);
        }
        break;
    case SYS_TOYMATCH0:
        toymatch_write(s, val, 0);
        break;
    case SYS_TOYMATCH1:
        toymatch_write(s, val, 1);
        break;
    case SYS_TOYMATCH2:
        toymatch_write(s, val, 2);
        break;
    case SYS_RTCCTRL:
        /* get old ctrl */
        old_toyen = toy_enabled(s);
        old_rtcen = rtc_enabled(s);

        s->cntrctl = val;
        /* get new ctrl */
        new_toyen = toy_enabled(s);
        new_rtcen = rtc_enabled(s);

        /*
         * we do not consider if EO changed, as it always set at most time.
         * toy or rtc enabled should start timer. otherwise, stop timer
         */
        if (old_toyen != new_toyen) {
            if (new_toyen) {
                ls7a_toy_start(s);
            } else {
                ls7a_toy_stop(s);
            }
        }
        if (old_rtcen != new_rtcen) {
            if (new_rtcen) {
                ls7a_rtc_start(s);
            } else {
                ls7a_rtc_stop(s);
            }
        }
        break;
    case SYS_RTCWRTIE0:
        if (rtc_enabled(s)) {
            s->offset_rtc = val - ls7a_rtc_ticks();
        }
        break;
    case SYS_RTCMATCH0:
        rtcmatch_write(s, val, 0);
        break;
    case SYS_RTCMATCH1:
        rtcmatch_write(s, val, 1);
        break;
    case SYS_RTCMATCH2:
        rtcmatch_write(s, val, 2);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ls7a_rtc_ops = {
    .read = ls7a_rtc_read,
    .write = ls7a_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void toy_timer_cb(void *opaque)
{
    LS7ARtcState *s = opaque;

    if (toy_enabled(s)) {
        qemu_irq_raise(s->irq);
    }
}

static void rtc_timer_cb(void *opaque)
{
    LS7ARtcState *s = opaque;

    if (rtc_enabled(s)) {
        qemu_irq_raise(s->irq);
    }
}

static void ls7a_rtc_realize(DeviceState *dev, Error **errp)
{
    int i;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    LS7ARtcState *d = LS7A_RTC(sbd);
    memory_region_init_io(&d->iomem, NULL, &ls7a_rtc_ops,
                         (void *)d, "ls7a_rtc", 0x100);

    sysbus_init_irq(sbd, &d->irq);

    sysbus_init_mmio(sbd, &d->iomem);
    for (i = 0; i < TIMER_NUMS; i++) {
        d->toymatch[i] = 0;
        d->rtcmatch[i] = 0;
        d->toy_timer[i] = timer_new_ms(rtc_clock, toy_timer_cb, d);
        d->rtc_timer[i] = timer_new_ms(rtc_clock, rtc_timer_cb, d);
    }
    d->offset_toy = 0;
    d->offset_rtc = 0;

}

/* delete timer and clear reg when reset */
static void ls7a_rtc_reset(DeviceState *dev)
{
    int i;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    LS7ARtcState *d = LS7A_RTC(sbd);
    for (i = 0; i < TIMER_NUMS; i++) {
        if (toy_enabled(d)) {
            timer_del(d->toy_timer[i]);
        }
        if (rtc_enabled(d)) {
            timer_del(d->rtc_timer[i]);
        }
        d->toymatch[i] = 0;
        d->rtcmatch[i] = 0;
    }
    d->cntrctl = 0;
}

static int ls7a_rtc_pre_save(void *opaque)
{
    LS7ARtcState *s = LS7A_RTC(opaque);

    ls7a_toy_stop(s);
    ls7a_rtc_stop(s);

    return 0;
}

static int ls7a_rtc_post_load(void *opaque, int version_id)
{
    LS7ARtcState *s = LS7A_RTC(opaque);
    if (toy_enabled(s)) {
        ls7a_toy_start(s);
    }

    if (rtc_enabled(s)) {
        ls7a_rtc_start(s);
    }

    return 0;
}

static const VMStateDescription vmstate_ls7a_rtc = {
    .name = "ls7a_rtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = ls7a_rtc_pre_save,
    .post_load = ls7a_rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(offset_toy, LS7ARtcState),
        VMSTATE_INT64(offset_rtc, LS7ARtcState),
        VMSTATE_UINT32_ARRAY(toymatch, LS7ARtcState, TIMER_NUMS),
        VMSTATE_UINT32_ARRAY(rtcmatch, LS7ARtcState, TIMER_NUMS),
        VMSTATE_UINT32(cntrctl, LS7ARtcState),
        VMSTATE_END_OF_LIST()
    }
};

static void ls7a_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->vmsd = &vmstate_ls7a_rtc;
    dc->realize = ls7a_rtc_realize;
    dc->reset = ls7a_rtc_reset;
    dc->desc = "ls7a rtc";
}

static const TypeInfo ls7a_rtc_info = {
    .name          = TYPE_LS7A_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LS7ARtcState),
    .class_init    = ls7a_rtc_class_init,
};

static void ls7a_rtc_register_types(void)
{
    type_register_static(&ls7a_rtc_info);
}

type_init(ls7a_rtc_register_types)
