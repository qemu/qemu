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
#include "hw/hw.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/timer/mc146818rtc.h"
#include "qapi/visitor.h"
#include "qapi-event.h"
#include "qmp-commands.h"

#ifdef TARGET_I386
#include "hw/i386/apic.h"
#endif

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

#define NSEC_PER_SEC    1000000000LL
#define SEC_PER_MIN     60
#define MIN_PER_HOUR    60
#define SEC_PER_HOUR    3600
#define HOUR_PER_DAY    24
#define SEC_PER_DAY     86400

#define RTC_REINJECT_ON_ACK_COUNT 20
#define RTC_CLOCK_RATE            32768
#define UIP_HOLD_LENGTH           (8 * NSEC_PER_SEC / 32768)

#define MC146818_RTC(obj) OBJECT_CHECK(RTCState, (obj), TYPE_MC146818_RTC)

typedef struct RTCState {
    ISADevice parent_obj;

    MemoryRegion io;
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    int32_t base_year;
    uint64_t base_rtc;
    uint64_t last_update;
    int64_t offset;
    qemu_irq irq;
    int it_shift;
    /* periodic timer */
    QEMUTimer *periodic_timer;
    int64_t next_periodic_time;
    /* update-ended timer */
    QEMUTimer *update_timer;
    uint64_t next_alarm_time;
    uint16_t irq_reinject_on_ack_count;
    uint32_t irq_coalesced;
    uint32_t period;
    QEMUTimer *coalesced_timer;
    Notifier clock_reset_notifier;
    LostTickPolicy lost_tick_policy;
    Notifier suspend_notifier;
    QLIST_ENTRY(RTCState) link;
} RTCState;

static void rtc_set_time(RTCState *s);
static void rtc_update_time(RTCState *s);
static void rtc_set_cmos(RTCState *s, const struct tm *tm);
static inline int rtc_from_bcd(RTCState *s, int a);
static uint64_t get_next_alarm(RTCState *s);

static inline bool rtc_running(RTCState *s)
{
    return (!(s->cmos_data[RTC_REG_B] & REG_B_SET) &&
            (s->cmos_data[RTC_REG_A] & 0x70) <= 0x20);
}

static uint64_t get_guest_rtc_ns(RTCState *s)
{
    uint64_t guest_rtc;
    uint64_t guest_clock = qemu_clock_get_ns(rtc_clock);

    guest_rtc = s->base_rtc * NSEC_PER_SEC
                 + guest_clock - s->last_update + s->offset;
    return guest_rtc;
}

#ifdef TARGET_I386
static void rtc_coalesced_timer_update(RTCState *s)
{
    if (s->irq_coalesced == 0) {
        timer_del(s->coalesced_timer);
    } else {
        /* divide each RTC interval to 2 - 8 smaller intervals */
        int c = MIN(s->irq_coalesced, 7) + 1; 
        int64_t next_clock = qemu_clock_get_ns(rtc_clock) +
            muldiv64(s->period / c, get_ticks_per_sec(), RTC_CLOCK_RATE);
        timer_mod(s->coalesced_timer, next_clock);
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

/* handle periodic timer */
static void periodic_timer_update(RTCState *s, int64_t current_time)
{
    int period_code, period;
    int64_t cur_clock, next_irq_clock;

    period_code = s->cmos_data[RTC_REG_A] & 0x0f;
    if (period_code != 0
        && (s->cmos_data[RTC_REG_B] & REG_B_PIE)) {
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
        cur_clock = muldiv64(current_time, RTC_CLOCK_RATE, get_ticks_per_sec());
        next_irq_clock = (cur_clock & ~(period - 1)) + period;
        s->next_periodic_time =
            muldiv64(next_irq_clock, get_ticks_per_sec(), RTC_CLOCK_RATE) + 1;
        timer_mod(s->periodic_timer, s->next_periodic_time);
    } else {
#ifdef TARGET_I386
        s->irq_coalesced = 0;
#endif
        timer_del(s->periodic_timer);
    }
}

static void rtc_periodic_timer(void *opaque)
{
    RTCState *s = opaque;

    periodic_timer_update(s, s->next_periodic_time);
    s->cmos_data[RTC_REG_C] |= REG_C_PF;
    if (s->cmos_data[RTC_REG_B] & REG_B_PIE) {
        s->cmos_data[RTC_REG_C] |= REG_C_IRQF;
#ifdef TARGET_I386
        if (s->lost_tick_policy == LOST_TICK_POLICY_SLEW) {
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
}

/* handle update-ended timer */
static void check_update_timer(RTCState *s)
{
    uint64_t next_update_time;
    uint64_t guest_nsec;
    int next_alarm_sec;

    /* From the data sheet: "Holding the dividers in reset prevents
     * interrupts from operating, while setting the SET bit allows"
     * them to occur.  However, it will prevent an alarm interrupt
     * from occurring, because the time of day is not updated.
     */
    if ((s->cmos_data[RTC_REG_A] & 0x60) == 0x60) {
        timer_del(s->update_timer);
        return;
    }
    if ((s->cmos_data[RTC_REG_C] & REG_C_UF) &&
        (s->cmos_data[RTC_REG_B] & REG_B_SET)) {
        timer_del(s->update_timer);
        return;
    }
    if ((s->cmos_data[RTC_REG_C] & REG_C_UF) &&
        (s->cmos_data[RTC_REG_C] & REG_C_AF)) {
        timer_del(s->update_timer);
        return;
    }

    guest_nsec = get_guest_rtc_ns(s) % NSEC_PER_SEC;
    /* if UF is clear, reprogram to next second */
    next_update_time = qemu_clock_get_ns(rtc_clock)
        + NSEC_PER_SEC - guest_nsec;

    /* Compute time of next alarm.  One second is already accounted
     * for in next_update_time.
     */
    next_alarm_sec = get_next_alarm(s);
    s->next_alarm_time = next_update_time + (next_alarm_sec - 1) * NSEC_PER_SEC;

    if (s->cmos_data[RTC_REG_C] & REG_C_UF) {
        /* UF is set, but AF is clear.  Program the timer to target
         * the alarm time.  */
        next_update_time = s->next_alarm_time;
    }
    if (next_update_time != timer_expire_time_ns(s->update_timer)) {
        timer_mod(s->update_timer, next_update_time);
    }
}

static inline uint8_t convert_hour(RTCState *s, uint8_t hour)
{
    if (!(s->cmos_data[RTC_REG_B] & REG_B_24H)) {
        hour %= 12;
        if (s->cmos_data[RTC_HOURS] & 0x80) {
            hour += 12;
        }
    }
    return hour;
}

static uint64_t get_next_alarm(RTCState *s)
{
    int32_t alarm_sec, alarm_min, alarm_hour, cur_hour, cur_min, cur_sec;
    int32_t hour, min, sec;

    rtc_update_time(s);

    alarm_sec = rtc_from_bcd(s, s->cmos_data[RTC_SECONDS_ALARM]);
    alarm_min = rtc_from_bcd(s, s->cmos_data[RTC_MINUTES_ALARM]);
    alarm_hour = rtc_from_bcd(s, s->cmos_data[RTC_HOURS_ALARM]);
    alarm_hour = alarm_hour == -1 ? -1 : convert_hour(s, alarm_hour);

    cur_sec = rtc_from_bcd(s, s->cmos_data[RTC_SECONDS]);
    cur_min = rtc_from_bcd(s, s->cmos_data[RTC_MINUTES]);
    cur_hour = rtc_from_bcd(s, s->cmos_data[RTC_HOURS]);
    cur_hour = convert_hour(s, cur_hour);

    if (alarm_hour == -1) {
        alarm_hour = cur_hour;
        if (alarm_min == -1) {
            alarm_min = cur_min;
            if (alarm_sec == -1) {
                alarm_sec = cur_sec + 1;
            } else if (cur_sec > alarm_sec) {
                alarm_min++;
            }
        } else if (cur_min == alarm_min) {
            if (alarm_sec == -1) {
                alarm_sec = cur_sec + 1;
            } else {
                if (cur_sec > alarm_sec) {
                    alarm_hour++;
                }
            }
            if (alarm_sec == SEC_PER_MIN) {
                /* wrap to next hour, minutes is not in don't care mode */
                alarm_sec = 0;
                alarm_hour++;
            }
        } else if (cur_min > alarm_min) {
            alarm_hour++;
        }
    } else if (cur_hour == alarm_hour) {
        if (alarm_min == -1) {
            alarm_min = cur_min;
            if (alarm_sec == -1) {
                alarm_sec = cur_sec + 1;
            } else if (cur_sec > alarm_sec) {
                alarm_min++;
            }

            if (alarm_sec == SEC_PER_MIN) {
                alarm_sec = 0;
                alarm_min++;
            }
            /* wrap to next day, hour is not in don't care mode */
            alarm_min %= MIN_PER_HOUR;
        } else if (cur_min == alarm_min) {
            if (alarm_sec == -1) {
                alarm_sec = cur_sec + 1;
            }
            /* wrap to next day, hours+minutes not in don't care mode */
            alarm_sec %= SEC_PER_MIN;
        }
    }

    /* values that are still don't care fire at the next min/sec */
    if (alarm_min == -1) {
        alarm_min = 0;
    }
    if (alarm_sec == -1) {
        alarm_sec = 0;
    }

    /* keep values in range */
    if (alarm_sec == SEC_PER_MIN) {
        alarm_sec = 0;
        alarm_min++;
    }
    if (alarm_min == MIN_PER_HOUR) {
        alarm_min = 0;
        alarm_hour++;
    }
    alarm_hour %= HOUR_PER_DAY;

    hour = alarm_hour - cur_hour;
    min = hour * MIN_PER_HOUR + alarm_min - cur_min;
    sec = min * SEC_PER_MIN + alarm_sec - cur_sec;
    return sec <= 0 ? sec + SEC_PER_DAY : sec;
}

static void rtc_update_timer(void *opaque)
{
    RTCState *s = opaque;
    int32_t irqs = REG_C_UF;
    int32_t new_irqs;

    assert((s->cmos_data[RTC_REG_A] & 0x60) != 0x60);

    /* UIP might have been latched, update time and clear it.  */
    rtc_update_time(s);
    s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;

    if (qemu_clock_get_ns(rtc_clock) >= s->next_alarm_time) {
        irqs |= REG_C_AF;
        if (s->cmos_data[RTC_REG_B] & REG_B_AIE) {
            qemu_system_wakeup_request(QEMU_WAKEUP_REASON_RTC);
        }
    }

    new_irqs = irqs & ~s->cmos_data[RTC_REG_C];
    s->cmos_data[RTC_REG_C] |= irqs;
    if ((new_irqs & s->cmos_data[RTC_REG_B]) != 0) {
        s->cmos_data[RTC_REG_C] |= REG_C_IRQF;
        qemu_irq_raise(s->irq);
    }
    check_update_timer(s);
}

static void cmos_ioport_write(void *opaque, hwaddr addr,
                              uint64_t data, unsigned size)
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
            check_update_timer(s);
            break;
	case RTC_IBM_PS2_CENTURY_BYTE:
            s->cmos_index = RTC_CENTURY;
            /* fall through */
        case RTC_CENTURY:
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            s->cmos_data[s->cmos_index] = data;
            /* if in set mode, do not update the time */
            if (rtc_running(s)) {
                rtc_set_time(s);
                check_update_timer(s);
            }
            break;
        case RTC_REG_A:
            if ((data & 0x60) == 0x60) {
                if (rtc_running(s)) {
                    rtc_update_time(s);
                }
                /* What happens to UIP when divider reset is enabled is
                 * unclear from the datasheet.  Shouldn't matter much
                 * though.
                 */
                s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
            } else if (((s->cmos_data[RTC_REG_A] & 0x60) == 0x60) &&
                    (data & 0x70)  <= 0x20) {
                /* when the divider reset is removed, the first update cycle
                 * begins one-half second later*/
                if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
                    s->offset = 500000000;
                    rtc_set_time(s);
                }
                s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
            }
            /* UIP bit is read only */
            s->cmos_data[RTC_REG_A] = (data & ~REG_A_UIP) |
                (s->cmos_data[RTC_REG_A] & REG_A_UIP);
            periodic_timer_update(s, qemu_clock_get_ns(rtc_clock));
            check_update_timer(s);
            break;
        case RTC_REG_B:
            if (data & REG_B_SET) {
                /* update cmos to when the rtc was stopping */
                if (rtc_running(s)) {
                    rtc_update_time(s);
                }
                /* set mode: reset UIP mode */
                s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
                data &= ~REG_B_UIE;
            } else {
                /* if disabling set mode, update the time */
                if ((s->cmos_data[RTC_REG_B] & REG_B_SET) &&
                    (s->cmos_data[RTC_REG_A] & 0x70) <= 0x20) {
                    s->offset = get_guest_rtc_ns(s) % NSEC_PER_SEC;
                    rtc_set_time(s);
                }
            }
            /* if an interrupt flag is already set when the interrupt
             * becomes enabled, raise an interrupt immediately.  */
            if (data & s->cmos_data[RTC_REG_C] & REG_C_MASK) {
                s->cmos_data[RTC_REG_C] |= REG_C_IRQF;
                qemu_irq_raise(s->irq);
            } else {
                s->cmos_data[RTC_REG_C] &= ~REG_C_IRQF;
                qemu_irq_lower(s->irq);
            }
            s->cmos_data[RTC_REG_B] = data;
            periodic_timer_update(s, qemu_clock_get_ns(rtc_clock));
            check_update_timer(s);
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
    if ((a & 0xc0) == 0xc0) {
        return -1;
    }
    if (s->cmos_data[RTC_REG_B] & REG_B_DM) {
        return a;
    } else {
        return ((a >> 4) * 10) + (a & 0x0f);
    }
}

static void rtc_get_time(RTCState *s, struct tm *tm)
{
    tm->tm_sec = rtc_from_bcd(s, s->cmos_data[RTC_SECONDS]);
    tm->tm_min = rtc_from_bcd(s, s->cmos_data[RTC_MINUTES]);
    tm->tm_hour = rtc_from_bcd(s, s->cmos_data[RTC_HOURS] & 0x7f);
    if (!(s->cmos_data[RTC_REG_B] & REG_B_24H)) {
        tm->tm_hour %= 12;
        if (s->cmos_data[RTC_HOURS] & 0x80) {
            tm->tm_hour += 12;
        }
    }
    tm->tm_wday = rtc_from_bcd(s, s->cmos_data[RTC_DAY_OF_WEEK]) - 1;
    tm->tm_mday = rtc_from_bcd(s, s->cmos_data[RTC_DAY_OF_MONTH]);
    tm->tm_mon = rtc_from_bcd(s, s->cmos_data[RTC_MONTH]) - 1;
    tm->tm_year =
        rtc_from_bcd(s, s->cmos_data[RTC_YEAR]) + s->base_year +
        rtc_from_bcd(s, s->cmos_data[RTC_CENTURY]) * 100 - 1900;
}

static QLIST_HEAD(, RTCState) rtc_devices =
    QLIST_HEAD_INITIALIZER(rtc_devices);

#ifdef TARGET_I386
void qmp_rtc_reset_reinjection(Error **errp)
{
    RTCState *s;

    QLIST_FOREACH(s, &rtc_devices, link) {
        s->irq_coalesced = 0;
    }
}
#endif

static void rtc_set_time(RTCState *s)
{
    struct tm tm;

    rtc_get_time(s, &tm);
    s->base_rtc = mktimegm(&tm);
    s->last_update = qemu_clock_get_ns(rtc_clock);

    qapi_event_send_rtc_change(qemu_timedate_diff(&tm), &error_abort);
}

static void rtc_set_cmos(RTCState *s, const struct tm *tm)
{
    int year;

    s->cmos_data[RTC_SECONDS] = rtc_to_bcd(s, tm->tm_sec);
    s->cmos_data[RTC_MINUTES] = rtc_to_bcd(s, tm->tm_min);
    if (s->cmos_data[RTC_REG_B] & REG_B_24H) {
        /* 24 hour format */
        s->cmos_data[RTC_HOURS] = rtc_to_bcd(s, tm->tm_hour);
    } else {
        /* 12 hour format */
        int h = (tm->tm_hour % 12) ? tm->tm_hour % 12 : 12;
        s->cmos_data[RTC_HOURS] = rtc_to_bcd(s, h);
        if (tm->tm_hour >= 12)
            s->cmos_data[RTC_HOURS] |= 0x80;
    }
    s->cmos_data[RTC_DAY_OF_WEEK] = rtc_to_bcd(s, tm->tm_wday + 1);
    s->cmos_data[RTC_DAY_OF_MONTH] = rtc_to_bcd(s, tm->tm_mday);
    s->cmos_data[RTC_MONTH] = rtc_to_bcd(s, tm->tm_mon + 1);
    year = tm->tm_year + 1900 - s->base_year;
    s->cmos_data[RTC_YEAR] = rtc_to_bcd(s, year % 100);
    s->cmos_data[RTC_CENTURY] = rtc_to_bcd(s, year / 100);
}

static void rtc_update_time(RTCState *s)
{
    struct tm ret;
    time_t guest_sec;
    int64_t guest_nsec;

    guest_nsec = get_guest_rtc_ns(s);
    guest_sec = guest_nsec / NSEC_PER_SEC;
    gmtime_r(&guest_sec, &ret);

    /* Is SET flag of Register B disabled? */
    if ((s->cmos_data[RTC_REG_B] & REG_B_SET) == 0) {
        rtc_set_cmos(s, &ret);
    }
}

static int update_in_progress(RTCState *s)
{
    int64_t guest_nsec;

    if (!rtc_running(s)) {
        return 0;
    }
    if (timer_pending(s->update_timer)) {
        int64_t next_update_time = timer_expire_time_ns(s->update_timer);
        /* Latch UIP until the timer expires.  */
        if (qemu_clock_get_ns(rtc_clock) >=
            (next_update_time - UIP_HOLD_LENGTH)) {
            s->cmos_data[RTC_REG_A] |= REG_A_UIP;
            return 1;
        }
    }

    guest_nsec = get_guest_rtc_ns(s);
    /* UIP bit will be set at last 244us of every second. */
    if ((guest_nsec % NSEC_PER_SEC) >= (NSEC_PER_SEC - UIP_HOLD_LENGTH)) {
        return 1;
    }
    return 0;
}

static uint64_t cmos_ioport_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    RTCState *s = opaque;
    int ret;
    if ((addr & 1) == 0) {
        return 0xff;
    } else {
        switch(s->cmos_index) {
	case RTC_IBM_PS2_CENTURY_BYTE:
            s->cmos_index = RTC_CENTURY;
            /* fall through */
        case RTC_CENTURY:
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            /* if not in set mode, calibrate cmos before
             * reading*/
            if (rtc_running(s)) {
                rtc_update_time(s);
            }
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_A:
            if (update_in_progress(s)) {
                s->cmos_data[s->cmos_index] |= REG_A_UIP;
            } else {
                s->cmos_data[s->cmos_index] &= ~REG_A_UIP;
            }
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_C:
            ret = s->cmos_data[s->cmos_index];
            qemu_irq_lower(s->irq);
            s->cmos_data[RTC_REG_C] = 0x00;
            if (ret & (REG_C_UF | REG_C_AF)) {
                check_update_timer(s);
            }
#ifdef TARGET_I386
            if(s->irq_coalesced &&
                    (s->cmos_data[RTC_REG_B] & REG_B_PIE) &&
                    s->irq_reinject_on_ack_count < RTC_REINJECT_ON_ACK_COUNT) {
                s->irq_reinject_on_ack_count++;
                s->cmos_data[RTC_REG_C] |= REG_C_IRQF | REG_C_PF;
                apic_reset_irq_delivered();
                DPRINTF_C("cmos: injecting on ack\n");
                qemu_irq_raise(s->irq);
                if (apic_get_irq_delivered()) {
                    s->irq_coalesced--;
                    DPRINTF_C("cmos: coalesced irqs decreased to %d\n",
                              s->irq_coalesced);
                }
            }
#endif
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
    RTCState *s = MC146818_RTC(dev);
    if (addr >= 0 && addr <= 127)
        s->cmos_data[addr] = val;
}

int rtc_get_memory(ISADevice *dev, int addr)
{
    RTCState *s = MC146818_RTC(dev);
    assert(addr >= 0 && addr <= 127);
    return s->cmos_data[addr];
}

static void rtc_set_date_from_host(ISADevice *dev)
{
    RTCState *s = MC146818_RTC(dev);
    struct tm tm;

    qemu_get_timedate(&tm, 0);

    s->base_rtc = mktimegm(&tm);
    s->last_update = qemu_clock_get_ns(rtc_clock);
    s->offset = 0;

    /* set the CMOS date */
    rtc_set_cmos(s, &tm);
}

static int rtc_post_load(void *opaque, int version_id)
{
    RTCState *s = opaque;

    if (version_id <= 2) {
        rtc_set_time(s);
        s->offset = 0;
        check_update_timer(s);
    }

#ifdef TARGET_I386
    if (version_id >= 2) {
        if (s->lost_tick_policy == LOST_TICK_POLICY_SLEW) {
            rtc_coalesced_timer_update(s);
        }
    }
#endif
    return 0;
}

static const VMStateDescription vmstate_rtc = {
    .name = "mc146818rtc",
    .version_id = 3,
    .minimum_version_id = 1,
    .post_load = rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(cmos_data, RTCState),
        VMSTATE_UINT8(cmos_index, RTCState),
        VMSTATE_UNUSED(7*4),
        VMSTATE_TIMER(periodic_timer, RTCState),
        VMSTATE_INT64(next_periodic_time, RTCState),
        VMSTATE_UNUSED(3*8),
        VMSTATE_UINT32_V(irq_coalesced, RTCState, 2),
        VMSTATE_UINT32_V(period, RTCState, 2),
        VMSTATE_UINT64_V(base_rtc, RTCState, 3),
        VMSTATE_UINT64_V(last_update, RTCState, 3),
        VMSTATE_INT64_V(offset, RTCState, 3),
        VMSTATE_TIMER_V(update_timer, RTCState, 3),
        VMSTATE_UINT64_V(next_alarm_time, RTCState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void rtc_notify_clock_reset(Notifier *notifier, void *data)
{
    RTCState *s = container_of(notifier, RTCState, clock_reset_notifier);
    int64_t now = *(int64_t *)data;

    rtc_set_date_from_host(ISA_DEVICE(s));
    periodic_timer_update(s, now);
    check_update_timer(s);
#ifdef TARGET_I386
    if (s->lost_tick_policy == LOST_TICK_POLICY_SLEW) {
        rtc_coalesced_timer_update(s);
    }
#endif
}

/* set CMOS shutdown status register (index 0xF) as S3_resume(0xFE)
   BIOS will read it and start S3 resume at POST Entry */
static void rtc_notify_suspend(Notifier *notifier, void *data)
{
    RTCState *s = container_of(notifier, RTCState, suspend_notifier);
    rtc_set_memory(ISA_DEVICE(s), 0xF, 0xFE);
}

static void rtc_reset(void *opaque)
{
    RTCState *s = opaque;

    s->cmos_data[RTC_REG_B] &= ~(REG_B_PIE | REG_B_AIE | REG_B_SQWE);
    s->cmos_data[RTC_REG_C] &= ~(REG_C_UF | REG_C_IRQF | REG_C_PF | REG_C_AF);
    check_update_timer(s);

    qemu_irq_lower(s->irq);

#ifdef TARGET_I386
    if (s->lost_tick_policy == LOST_TICK_POLICY_SLEW) {
        s->irq_coalesced = 0;
    }
#endif
}

static const MemoryRegionOps cmos_ops = {
    .read = cmos_ioport_read,
    .write = cmos_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rtc_get_date(Object *obj, Visitor *v, void *opaque,
                         const char *name, Error **errp)
{
    Error *err = NULL;
    RTCState *s = MC146818_RTC(obj);
    struct tm current_tm;

    rtc_update_time(s);
    rtc_get_time(s, &current_tm);
    visit_start_struct(v, NULL, "struct tm", name, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_int32(v, &current_tm.tm_year, "tm_year", &err);
    if (err) {
        goto out_end;
    }
    visit_type_int32(v, &current_tm.tm_mon, "tm_mon", &err);
    if (err) {
        goto out_end;
    }
    visit_type_int32(v, &current_tm.tm_mday, "tm_mday", &err);
    if (err) {
        goto out_end;
    }
    visit_type_int32(v, &current_tm.tm_hour, "tm_hour", &err);
    if (err) {
        goto out_end;
    }
    visit_type_int32(v, &current_tm.tm_min, "tm_min", &err);
    if (err) {
        goto out_end;
    }
    visit_type_int32(v, &current_tm.tm_sec, "tm_sec", &err);
    if (err) {
        goto out_end;
    }
out_end:
    error_propagate(errp, err);
    err = NULL;
    visit_end_struct(v, errp);
out:
    error_propagate(errp, err);
}

static void rtc_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    RTCState *s = MC146818_RTC(dev);
    int base = 0x70;

    s->cmos_data[RTC_REG_A] = 0x26;
    s->cmos_data[RTC_REG_B] = 0x02;
    s->cmos_data[RTC_REG_C] = 0x00;
    s->cmos_data[RTC_REG_D] = 0x80;

    /* This is for historical reasons.  The default base year qdev property
     * was set to 2000 for most machine types before the century byte was
     * implemented.
     *
     * This if statement means that the century byte will be always 0
     * (at least until 2079...) for base_year = 1980, but will be set
     * correctly for base_year = 2000.
     */
    if (s->base_year == 2000) {
        s->base_year = 0;
    }

    rtc_set_date_from_host(isadev);

#ifdef TARGET_I386
    switch (s->lost_tick_policy) {
    case LOST_TICK_POLICY_SLEW:
        s->coalesced_timer =
            timer_new_ns(rtc_clock, rtc_coalesced_timer, s);
        break;
    case LOST_TICK_POLICY_DISCARD:
        break;
    default:
        error_setg(errp, "Invalid lost tick policy.");
        return;
    }
#endif

    s->periodic_timer = timer_new_ns(rtc_clock, rtc_periodic_timer, s);
    s->update_timer = timer_new_ns(rtc_clock, rtc_update_timer, s);
    check_update_timer(s);

    s->clock_reset_notifier.notify = rtc_notify_clock_reset;
    qemu_clock_register_reset_notifier(QEMU_CLOCK_REALTIME,
                                       &s->clock_reset_notifier);

    s->suspend_notifier.notify = rtc_notify_suspend;
    qemu_register_suspend_notifier(&s->suspend_notifier);

    memory_region_init_io(&s->io, OBJECT(s), &cmos_ops, s, "rtc", 2);
    isa_register_ioport(isadev, &s->io, base);

    qdev_set_legacy_instance_id(dev, base, 3);
    qemu_register_reset(rtc_reset, s);

    object_property_add(OBJECT(s), "date", "struct tm",
                        rtc_get_date, NULL, NULL, s, NULL);
}

ISADevice *rtc_init(ISABus *bus, int base_year, qemu_irq intercept_irq)
{
    DeviceState *dev;
    ISADevice *isadev;
    RTCState *s;

    isadev = isa_create(bus, TYPE_MC146818_RTC);
    dev = DEVICE(isadev);
    s = MC146818_RTC(isadev);
    qdev_prop_set_int32(dev, "base_year", base_year);
    qdev_init_nofail(dev);
    if (intercept_irq) {
        s->irq = intercept_irq;
    } else {
        isa_init_irq(isadev, &s->irq, RTC_ISA_IRQ);
    }
    QLIST_INSERT_HEAD(&rtc_devices, s, link);

    return isadev;
}

static Property mc146818rtc_properties[] = {
    DEFINE_PROP_INT32("base_year", RTCState, base_year, 1980),
    DEFINE_PROP_LOSTTICKPOLICY("lost_tick_policy", RTCState,
                               lost_tick_policy, LOST_TICK_POLICY_DISCARD),
    DEFINE_PROP_END_OF_LIST(),
};

static void rtc_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rtc_realizefn;
    dc->vmsd = &vmstate_rtc;
    dc->props = mc146818rtc_properties;
    /* Reason: needs to be wired up by rtc_init() */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo mc146818rtc_info = {
    .name          = TYPE_MC146818_RTC,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(RTCState),
    .class_init    = rtc_class_initfn,
};

static void mc146818rtc_register_types(void)
{
    type_register_static(&mc146818rtc_info);
}

type_init(mc146818rtc_register_types)
