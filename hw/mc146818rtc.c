/*
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "pc.h"
#include "apic.h"
#include "isa.h"
#include "mc146818rtc.h"

//#define DEBUG_CMOS
//#define DEBUG_COALESCED

#ifdef DEBUG_CMOS
# define CMOS_DPRINTF(format, ...)      printf(format, ## __VA_ARGS__)
#else
# define CMOS_DPRINTF(format, ...)      do { } while (0)
#endif

#ifdef DEBUG_COALESCED
# define DPRINTF_C(format, ...)      printf(format, ## __VA_ARGS__)
#else
# define DPRINTF_C(format, ...)      do { } while (0)
#endif

#define RTC_REINJECT_ON_ACK_COUNT 20

#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

#define REG_A_UIP 0x80

#define REG_B_SET  0x80
#define REG_B_PIE  0x40
#define REG_B_AIE  0x20
#define REG_B_UIE  0x10
#define REG_B_SQWE 0x08
#define REG_B_DM   0x04
#define REG_B_24H  0x02

#define REG_C_UF   0x10
#define REG_C_IRQF 0x80
#define REG_C_PF   0x40
#define REG_C_AF   0x20

typedef struct RTCState {
    ISADevice dev;
    MemoryRegion io;
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    struct tm current_tm;
    int32_t base_year;
    qemu_irq irq;
    qemu_irq sqw_irq;
    int it_shift;
    /* periodic timer */
    QEMUTimer *periodic_timer;
    int64_t next_periodic_time;
    /* second update */
    int64_t next_second_time;
    uint16_t irq_reinject_on_ack_count;
    uint32_t irq_coalesced;
    uint32_t period;
    QEMUTimer *coalesced_timer;
    QEMUTimer *second_timer;
    QEMUTimer *second_timer2;
    Notifier clock_reset_notifier;
} RTCState;

static void rtc_set_time(RTCState *s);
static void rtc_copy_date(RTCState *s);

#ifdef TARGET_I386
static void rtc_coalesced_timer_update(RTCState *s)
{
    if (s->irq_coalesced == 0) {
        qemu_del_timer(s->coalesced_timer);
    } else {
        /* divide each RTC interval to 2 - 8 smaller intervals */
        int c = MIN(s->irq_coalesced, 7) + 1; 
        int64_t next_clock = qemu_get_clock_ns(rtc_clock) +
            muldiv64(s->period / c, get_ticks_per_sec(), 32768);
        qemu_mod_timer(s->coalesced_timer, next_clock);
    }
}

static void rtc_coalesced_timer(void *opaque)
{
    RTCState *s = opaque;

    if (s->irq_coalesced != 0) {
        apic_reset_irq_delivered();
        s->cmos_data[RTC_REG_C] |= 0xc0;
        DPRINTF_C("cmos: injecting from timer\n");
        qemu_irq_raise(s->irq);
        if (apic_get_irq_delivered()) {
            s->irq_coalesced--;
            DPRINTF_C("cmos: coalesced irqs decreased to %d\n",
                      s->irq_coalesced);
        }
    }

    rtc_coalesced_timer_update(s);
}
#endif

static void rtc_timer_update(RTCState *s, int64_t current_time)
{
    int period_code, period;
    int64_t cur_clock, next_irq_clock;

    period_code = s->cmos_data[RTC_REG_A] & 0x0f;
    if (period_code != 0
        && ((s->cmos_data[RTC_REG_B] & REG_B_PIE)
            || ((s->cmos_data[RTC_REG_B] & REG_B_SQWE) && s->sqw_irq))) {
        if (period_code <= 2)
            period_code += 7;
        /* period in 32 Khz cycles */
        period = 1 << (period_code - 1);
#ifdef TARGET_I386
        if (period != s->period) {
            s->irq_coalesced = (s->irq_coalesced * s->period) / period;
            DPRINTF_C("cmos: coalesced irqs scaled to %d\n", s->irq_coalesced);
        }
        s->period = period;
#endif
        /* compute 32 khz clock */
        cur_clock = muldiv64(current_time, 32768, get_ticks_per_sec());
        next_irq_clock = (cur_clock & ~(period - 1)) + period;
        s->next_periodic_time =
            muldiv64(next_irq_clock, get_ticks_per_sec(), 32768) + 1;
        qemu_mod_timer(s->periodic_timer, s->next_periodic_time);
    } else {
#ifdef TARGET_I386
        s->irq_coalesced = 0;
#endif
        qemu_del_timer(s->periodic_timer);
    }
}

static void rtc_periodic_timer(void *opaque)
{
    RTCState *s = opaque;

    rtc_timer_update(s, s->next_periodic_time);
    if (s->cmos_data[RTC_REG_B] & REG_B_PIE) {
        s->cmos_data[RTC_REG_C] |= 0xc0;
#ifdef TARGET_I386
        if(rtc_td_hack) {
            if (s->irq_reinject_on_ack_count >= RTC_REINJECT_ON_ACK_COUNT)
                s->irq_reinject_on_ack_count = 0;		
            apic_reset_irq_delivered();
            qemu_irq_raise(s->irq);
            if (!apic_get_irq_delivered()) {
                s->irq_coalesced++;
                rtc_coalesced_timer_update(s);
                DPRINTF_C("cmos: coalesced irqs increased to %d\n",
                          s->irq_coalesced);
            }
        } else
#endif
        qemu_irq_raise(s->irq);
    }
    if (s->cmos_data[RTC_REG_B] & REG_B_SQWE) {
        /* Not square wave at all but we don't want 2048Hz interrupts!
           Must be seen as a pulse.  */
        qemu_irq_raise(s->sqw_irq);
    }
}

static void cmos_ioport_write(void *opaque, uint32_t addr, uint32_t data)
{
    RTCState *s = opaque;

    if ((addr & 1) == 0) {
        s->cmos_index = data & 0x7f;
    } else {
        CMOS_DPRINTF("cmos: write index=0x%02x val=0x%02x\n",
                     s->cmos_index, data);
        switch(s->cmos_index) {
        case RTC_SECONDS_ALARM:
        case RTC_MINUTES_ALARM:
        case RTC_HOURS_ALARM:
            s->cmos_data[s->cmos_index] = data;
            break;
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            s->cmos_data[s->cmos_index] = data;
            /* if in set mode, do not update the time */
            if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
                rtc_set_time(s);
            }
            break;
        case RTC_REG_A:
            /* UIP bit is read only */
            s->cmos_data[RTC_REG_A] = (data & ~REG_A_UIP) |
                (s->cmos_data[RTC_REG_A] & REG_A_UIP);
            rtc_timer_update(s, qemu_get_clock_ns(rtc_clock));
            break;
        case RTC_REG_B:
            if (data & REG_B_SET) {
                /* set mode: reset UIP mode */
                s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
                data &= ~REG_B_UIE;
            } else {
                /* if disabling set mode, update the time */
                if (s->cmos_data[RTC_REG_B] & REG_B_SET) {
                    rtc_set_time(s);
                }
            }
            if (((s->cmos_data[RTC_REG_B] ^ data) & (REG_B_DM | REG_B_24H)) &&
                !(data & REG_B_SET)) {
                /* If the time format has changed and not in set mode,
                   update the registers immediately. */
                s->cmos_data[RTC_REG_B] = data;
                rtc_copy_date(s);
            } else {
                s->cmos_data[RTC_REG_B] = data;
            }
            rtc_timer_update(s, qemu_get_clock_ns(rtc_clock));
            break;
        case RTC_REG_C:
        case RTC_REG_D:
            /* cannot write to them */
            break;
        default:
            s->cmos_data[s->cmos_index] = data;
            break;
        }
    }
}

static inline int rtc_to_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & REG_B_DM) {
        return a;
    } else {
        return ((a / 10) << 4) | (a % 10);
    }
}

static inline int rtc_from_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & REG_B_DM) {
        return a;
    } else {
        return ((a >> 4) * 10) + (a & 0x0f);
    }
}

static void rtc_set_time(RTCState *s)
{
    struct tm *tm = &s->current_tm;

    tm->tm_sec = rtc_from_bcd(s, s->cmos_data[RTC_SECONDS]);
    tm->tm_min = rtc_from_bcd(s, s->cmos_data[RTC_MINUTES]);
    tm->tm_hour = rtc_from_bcd(s, s->cmos_data[RTC_HOURS] & 0x7f);
    if (!(s->cmos_data[RTC_REG_B] & REG_B_24H) &&
        (s->cmos_data[RTC_HOURS] & 0x80)) {
        tm->tm_hour += 12;
    }
    tm->tm_wday = rtc_from_bcd(s, s->cmos_data[RTC_DAY_OF_WEEK]) - 1;
    tm->tm_mday = rtc_from_bcd(s, s->cmos_data[RTC_DAY_OF_MONTH]);
    tm->tm_mon = rtc_from_bcd(s, s->cmos_data[RTC_MONTH]) - 1;
    tm->tm_year = rtc_from_bcd(s, s->cmos_data[RTC_YEAR]) + s->base_year - 1900;

    rtc_change_mon_event(tm);
}

static void rtc_copy_date(RTCState *s)
{
    const struct tm *tm = &s->current_tm;
    int year;

    s->cmos_data[RTC_SECONDS] = rtc_to_bcd(s, tm->tm_sec);
    s->cmos_data[RTC_MINUTES] = rtc_to_bcd(s, tm->tm_min);
    if (s->cmos_data[RTC_REG_B] & REG_B_24H) {
        /* 24 hour format */
        s->cmos_data[RTC_HOURS] = rtc_to_bcd(s, tm->tm_hour);
    } else {
        /* 12 hour format */
        s->cmos_data[RTC_HOURS] = rtc_to_bcd(s, tm->tm_hour % 12);
        if (tm->tm_hour >= 12)
            s->cmos_data[RTC_HOURS] |= 0x80;
    }
    s->cmos_data[RTC_DAY_OF_WEEK] = rtc_to_bcd(s, tm->tm_wday + 1);
    s->cmos_data[RTC_DAY_OF_MONTH] = rtc_to_bcd(s, tm->tm_mday);
    s->cmos_data[RTC_MONTH] = rtc_to_bcd(s, tm->tm_mon + 1);
    year = (tm->tm_year - s->base_year) % 100;
    if (year < 0)
        year += 100;
    s->cmos_data[RTC_YEAR] = rtc_to_bcd(s, year);
}

/* month is between 0 and 11. */
static int get_days_in_month(int month, int year)
{
    static const int days_tab[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int d;
    if ((unsigned )month >= 12)
        return 31;
    d = days_tab[month];
    if (month == 1) {
        if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0))
            d++;
    }
    return d;
}

/* update 'tm' to the next second */
static void rtc_next_second(struct tm *tm)
{
    int days_in_month;

    tm->tm_sec++;
    if ((unsigned)tm->tm_sec >= 60) {
        tm->tm_sec = 0;
        tm->tm_min++;
        if ((unsigned)tm->tm_min >= 60) {
            tm->tm_min = 0;
            tm->tm_hour++;
            if ((unsigned)tm->tm_hour >= 24) {
                tm->tm_hour = 0;
                /* next day */
                tm->tm_wday++;
                if ((unsigned)tm->tm_wday >= 7)
                    tm->tm_wday = 0;
                days_in_month = get_days_in_month(tm->tm_mon,
                                                  tm->tm_year + 1900);
                tm->tm_mday++;
                if (tm->tm_mday < 1) {
                    tm->tm_mday = 1;
                } else if (tm->tm_mday > days_in_month) {
                    tm->tm_mday = 1;
                    tm->tm_mon++;
                    if (tm->tm_mon >= 12) {
                        tm->tm_mon = 0;
                        tm->tm_year++;
                    }
                }
            }
        }
    }
}


static void rtc_update_second(void *opaque)
{
    RTCState *s = opaque;
    int64_t delay;

    /* if the oscillator is not in normal operation, we do not update */
    if ((s->cmos_data[RTC_REG_A] & 0x70) != 0x20) {
        s->next_second_time += get_ticks_per_sec();
        qemu_mod_timer(s->second_timer, s->next_second_time);
    } else {
        rtc_next_second(&s->current_tm);

        if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
            /* update in progress bit */
            s->cmos_data[RTC_REG_A] |= REG_A_UIP;
        }
        /* should be 244 us = 8 / 32768 seconds, but currently the
           timers do not have the necessary resolution. */
        delay = (get_ticks_per_sec() * 1) / 100;
        if (delay < 1)
            delay = 1;
        qemu_mod_timer(s->second_timer2,
                       s->next_second_time + delay);
    }
}

static void rtc_update_second2(void *opaque)
{
    RTCState *s = opaque;

    if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
        rtc_copy_date(s);
    }

    /* check alarm */
    if (s->cmos_data[RTC_REG_B] & REG_B_AIE) {
        if (((s->cmos_data[RTC_SECONDS_ALARM] & 0xc0) == 0xc0 ||
             rtc_from_bcd(s, s->cmos_data[RTC_SECONDS_ALARM]) == s->current_tm.tm_sec) &&
            ((s->cmos_data[RTC_MINUTES_ALARM] & 0xc0) == 0xc0 ||
             rtc_from_bcd(s, s->cmos_data[RTC_MINUTES_ALARM]) == s->current_tm.tm_min) &&
            ((s->cmos_data[RTC_HOURS_ALARM] & 0xc0) == 0xc0 ||
             rtc_from_bcd(s, s->cmos_data[RTC_HOURS_ALARM]) == s->current_tm.tm_hour)) {

            s->cmos_data[RTC_REG_C] |= 0xa0;
            qemu_irq_raise(s->irq);
        }
    }

    /* update ended interrupt */
    s->cmos_data[RTC_REG_C] |= REG_C_UF;
    if (s->cmos_data[RTC_REG_B] & REG_B_UIE) {
        s->cmos_data[RTC_REG_C] |= REG_C_IRQF;
        qemu_irq_raise(s->irq);
    }

    /* clear update in progress bit */
    s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;

    s->next_second_time += get_ticks_per_sec();
    qemu_mod_timer(s->second_timer, s->next_second_time);
}

static uint32_t cmos_ioport_read(void *opaque, uint32_t addr)
{
    RTCState *s = opaque;
    int ret;
    if ((addr & 1) == 0) {
        return 0xff;
    } else {
        switch(s->cmos_index) {
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_A:
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_C:
            ret = s->cmos_data[s->cmos_index];
            qemu_irq_lower(s->irq);
#ifdef TARGET_I386
            if(s->irq_coalesced &&
                    s->irq_reinject_on_ack_count < RTC_REINJECT_ON_ACK_COUNT) {
                s->irq_reinject_on_ack_count++;
                apic_reset_irq_delivered();
                DPRINTF_C("cmos: injecting on ack\n");
                qemu_irq_raise(s->irq);
                if (apic_get_irq_delivered()) {
                    s->irq_coalesced--;
                    DPRINTF_C("cmos: coalesced irqs decreased to %d\n",
                              s->irq_coalesced);
                }
                break;
            }
#endif

            s->cmos_data[RTC_REG_C] = 0x00;
            break;
        default:
            ret = s->cmos_data[s->cmos_index];
            break;
        }
        CMOS_DPRINTF("cmos: read index=0x%02x val=0x%02x\n",
                     s->cmos_index, ret);
        return ret;
    }
}

void rtc_set_memory(ISADevice *dev, int addr, int val)
{
    RTCState *s = DO_UPCAST(RTCState, dev, dev);
    if (addr >= 0 && addr <= 127)
        s->cmos_data[addr] = val;
}

void rtc_set_date(ISADevice *dev, const struct tm *tm)
{
    RTCState *s = DO_UPCAST(RTCState, dev, dev);
    s->current_tm = *tm;
    rtc_copy_date(s);
}

/* PC cmos mappings */
#define REG_IBM_CENTURY_BYTE        0x32
#define REG_IBM_PS2_CENTURY_BYTE    0x37

static void rtc_set_date_from_host(ISADevice *dev)
{
    RTCState *s = DO_UPCAST(RTCState, dev, dev);
    struct tm tm;
    int val;

    /* set the CMOS date */
    qemu_get_timedate(&tm, 0);
    rtc_set_date(dev, &tm);

    val = rtc_to_bcd(s, (tm.tm_year / 100) + 19);
    rtc_set_memory(dev, REG_IBM_CENTURY_BYTE, val);
    rtc_set_memory(dev, REG_IBM_PS2_CENTURY_BYTE, val);
}

static int rtc_post_load(void *opaque, int version_id)
{
#ifdef TARGET_I386
    RTCState *s = opaque;

    if (version_id >= 2) {
        if (rtc_td_hack) {
            rtc_coalesced_timer_update(s);
        }
    }
#endif
    return 0;
}

static const VMStateDescription vmstate_rtc = {
    .name = "mc146818rtc",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = rtc_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_BUFFER(cmos_data, RTCState),
        VMSTATE_UINT8(cmos_index, RTCState),
        VMSTATE_INT32(current_tm.tm_sec, RTCState),
        VMSTATE_INT32(current_tm.tm_min, RTCState),
        VMSTATE_INT32(current_tm.tm_hour, RTCState),
        VMSTATE_INT32(current_tm.tm_wday, RTCState),
        VMSTATE_INT32(current_tm.tm_mday, RTCState),
        VMSTATE_INT32(current_tm.tm_mon, RTCState),
        VMSTATE_INT32(current_tm.tm_year, RTCState),
        VMSTATE_TIMER(periodic_timer, RTCState),
        VMSTATE_INT64(next_periodic_time, RTCState),
        VMSTATE_INT64(next_second_time, RTCState),
        VMSTATE_TIMER(second_timer, RTCState),
        VMSTATE_TIMER(second_timer2, RTCState),
        VMSTATE_UINT32_V(irq_coalesced, RTCState, 2),
        VMSTATE_UINT32_V(period, RTCState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void rtc_notify_clock_reset(Notifier *notifier, void *data)
{
    RTCState *s = container_of(notifier, RTCState, clock_reset_notifier);
    int64_t now = *(int64_t *)data;

    rtc_set_date_from_host(&s->dev);
    s->next_second_time = now + (get_ticks_per_sec() * 99) / 100;
    qemu_mod_timer(s->second_timer2, s->next_second_time);
    rtc_timer_update(s, now);
#ifdef TARGET_I386
    if (rtc_td_hack) {
        rtc_coalesced_timer_update(s);
    }
#endif
}

static void rtc_reset(void *opaque)
{
    RTCState *s = opaque;

    s->cmos_data[RTC_REG_B] &= ~(REG_B_PIE | REG_B_AIE | REG_B_SQWE);
    s->cmos_data[RTC_REG_C] &= ~(REG_C_UF | REG_C_IRQF | REG_C_PF | REG_C_AF);

    qemu_irq_lower(s->irq);

#ifdef TARGET_I386
    if (rtc_td_hack)
	    s->irq_coalesced = 0;
#endif
}

static const MemoryRegionPortio cmos_portio[] = {
    {0, 2, 1, .read = cmos_ioport_read, .write = cmos_ioport_write },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionOps cmos_ops = {
    .old_portio = cmos_portio
};

static int rtc_initfn(ISADevice *dev)
{
    RTCState *s = DO_UPCAST(RTCState, dev, dev);
    int base = 0x70;

    s->cmos_data[RTC_REG_A] = 0x26;
    s->cmos_data[RTC_REG_B] = 0x02;
    s->cmos_data[RTC_REG_C] = 0x00;
    s->cmos_data[RTC_REG_D] = 0x80;

    rtc_set_date_from_host(dev);

    s->periodic_timer = qemu_new_timer_ns(rtc_clock, rtc_periodic_timer, s);
#ifdef TARGET_I386
    if (rtc_td_hack)
        s->coalesced_timer =
            qemu_new_timer_ns(rtc_clock, rtc_coalesced_timer, s);
#endif
    s->second_timer = qemu_new_timer_ns(rtc_clock, rtc_update_second, s);
    s->second_timer2 = qemu_new_timer_ns(rtc_clock, rtc_update_second2, s);

    s->clock_reset_notifier.notify = rtc_notify_clock_reset;
    qemu_register_clock_reset_notifier(rtc_clock, &s->clock_reset_notifier);

    s->next_second_time =
        qemu_get_clock_ns(rtc_clock) + (get_ticks_per_sec() * 99) / 100;
    qemu_mod_timer(s->second_timer2, s->next_second_time);

    memory_region_init_io(&s->io, &cmos_ops, s, "rtc", 2);
    isa_register_ioport(dev, &s->io, base);

    qdev_set_legacy_instance_id(&dev->qdev, base, 2);
    qemu_register_reset(rtc_reset, s);
    return 0;
}

ISADevice *rtc_init(int base_year, qemu_irq intercept_irq)
{
    ISADevice *dev;
    RTCState *s;

    dev = isa_create("mc146818rtc");
    s = DO_UPCAST(RTCState, dev, dev);
    qdev_prop_set_int32(&dev->qdev, "base_year", base_year);
    qdev_init_nofail(&dev->qdev);
    if (intercept_irq) {
        s->irq = intercept_irq;
    } else {
        isa_init_irq(dev, &s->irq, RTC_ISA_IRQ);
    }
    return dev;
}

static ISADeviceInfo mc146818rtc_info = {
    .qdev.name     = "mc146818rtc",
    .qdev.size     = sizeof(RTCState),
    .qdev.no_user  = 1,
    .qdev.vmsd     = &vmstate_rtc,
    .init          = rtc_initfn,
    .qdev.props    = (Property[]) {
        DEFINE_PROP_INT32("base_year", RTCState, base_year, 1980),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void mc146818rtc_register(void)
{
    isa_qdev_register(&mc146818rtc_info);
}
device_init(mc146818rtc_register)
