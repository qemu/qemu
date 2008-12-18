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
#include "isa.h"
#include "hpet_emul.h"

//#define DEBUG_CMOS

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

#define REG_B_SET 0x80
#define REG_B_PIE 0x40
#define REG_B_AIE 0x20
#define REG_B_UIE 0x10

struct RTCState {
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    struct tm current_tm;
    qemu_irq irq;
    int it_shift;
    /* periodic timer */
    QEMUTimer *periodic_timer;
    int64_t next_periodic_time;
    /* second update */
    int64_t next_second_time;
    QEMUTimer *second_timer;
    QEMUTimer *second_timer2;
};

static void rtc_irq_raise(qemu_irq irq) {
    /* When HPET is operating in legacy mode, RTC interrupts are disabled
     * We block qemu_irq_raise, but not qemu_irq_lower, in case legacy
     * mode is established while interrupt is raised. We want it to
     * be lowered in any case
     */
#if defined TARGET_I386 || defined TARGET_X86_64
    if (!hpet_in_legacy_mode())
#endif
        qemu_irq_raise(irq);
}

static void rtc_set_time(RTCState *s);
static void rtc_copy_date(RTCState *s);

static void rtc_timer_update(RTCState *s, int64_t current_time)
{
    int period_code, period;
    int64_t cur_clock, next_irq_clock;

    period_code = s->cmos_data[RTC_REG_A] & 0x0f;
#if defined TARGET_I386 || defined TARGET_X86_64
    /* disable periodic timer if hpet is in legacy mode, since interrupts are
     * disabled anyway.
     */
    if (period_code != 0 && (s->cmos_data[RTC_REG_B] & REG_B_PIE) && !hpet_in_legacy_mode()) {
#else
    if (period_code != 0 && (s->cmos_data[RTC_REG_B] & REG_B_PIE)) {
#endif
        if (period_code <= 2)
            period_code += 7;
        /* period in 32 Khz cycles */
        period = 1 << (period_code - 1);
        /* compute 32 khz clock */
        cur_clock = muldiv64(current_time, 32768, ticks_per_sec);
        next_irq_clock = (cur_clock & ~(period - 1)) + period;
        s->next_periodic_time = muldiv64(next_irq_clock, ticks_per_sec, 32768) + 1;
        qemu_mod_timer(s->periodic_timer, s->next_periodic_time);
    } else {
        qemu_del_timer(s->periodic_timer);
    }
}

static void rtc_periodic_timer(void *opaque)
{
    RTCState *s = opaque;

    rtc_timer_update(s, s->next_periodic_time);
    s->cmos_data[RTC_REG_C] |= 0xc0;
    rtc_irq_raise(s->irq);
}

static void cmos_ioport_write(void *opaque, uint32_t addr, uint32_t data)
{
    RTCState *s = opaque;

    if ((addr & 1) == 0) {
        s->cmos_index = data & 0x7f;
    } else {
#ifdef DEBUG_CMOS
        printf("cmos: write index=0x%02x val=0x%02x\n",
               s->cmos_index, data);
#endif
        switch(s->cmos_index) {
        case RTC_SECONDS_ALARM:
        case RTC_MINUTES_ALARM:
        case RTC_HOURS_ALARM:
            /* XXX: not supported */
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
            rtc_timer_update(s, qemu_get_clock(vm_clock));
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
            s->cmos_data[RTC_REG_B] = data;
            rtc_timer_update(s, qemu_get_clock(vm_clock));
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

static inline int to_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & 0x04) {
        return a;
    } else {
        return ((a / 10) << 4) | (a % 10);
    }
}

static inline int from_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & 0x04) {
        return a;
    } else {
        return ((a >> 4) * 10) + (a & 0x0f);
    }
}

static void rtc_set_time(RTCState *s)
{
    struct tm *tm = &s->current_tm;

    tm->tm_sec = from_bcd(s, s->cmos_data[RTC_SECONDS]);
    tm->tm_min = from_bcd(s, s->cmos_data[RTC_MINUTES]);
    tm->tm_hour = from_bcd(s, s->cmos_data[RTC_HOURS] & 0x7f);
    if (!(s->cmos_data[RTC_REG_B] & 0x02) &&
        (s->cmos_data[RTC_HOURS] & 0x80)) {
        tm->tm_hour += 12;
    }
    tm->tm_wday = from_bcd(s, s->cmos_data[RTC_DAY_OF_WEEK]);
    tm->tm_mday = from_bcd(s, s->cmos_data[RTC_DAY_OF_MONTH]);
    tm->tm_mon = from_bcd(s, s->cmos_data[RTC_MONTH]) - 1;
    tm->tm_year = from_bcd(s, s->cmos_data[RTC_YEAR]) + 100;
}

static void rtc_copy_date(RTCState *s)
{
    const struct tm *tm = &s->current_tm;

    s->cmos_data[RTC_SECONDS] = to_bcd(s, tm->tm_sec);
    s->cmos_data[RTC_MINUTES] = to_bcd(s, tm->tm_min);
    if (s->cmos_data[RTC_REG_B] & 0x02) {
        /* 24 hour format */
        s->cmos_data[RTC_HOURS] = to_bcd(s, tm->tm_hour);
    } else {
        /* 12 hour format */
        s->cmos_data[RTC_HOURS] = to_bcd(s, tm->tm_hour % 12);
        if (tm->tm_hour >= 12)
            s->cmos_data[RTC_HOURS] |= 0x80;
    }
    s->cmos_data[RTC_DAY_OF_WEEK] = to_bcd(s, tm->tm_wday);
    s->cmos_data[RTC_DAY_OF_MONTH] = to_bcd(s, tm->tm_mday);
    s->cmos_data[RTC_MONTH] = to_bcd(s, tm->tm_mon + 1);
    s->cmos_data[RTC_YEAR] = to_bcd(s, tm->tm_year % 100);
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
        s->next_second_time += ticks_per_sec;
        qemu_mod_timer(s->second_timer, s->next_second_time);
    } else {
        rtc_next_second(&s->current_tm);

        if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
            /* update in progress bit */
            s->cmos_data[RTC_REG_A] |= REG_A_UIP;
        }
        /* should be 244 us = 8 / 32768 seconds, but currently the
           timers do not have the necessary resolution. */
        delay = (ticks_per_sec * 1) / 100;
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
             s->cmos_data[RTC_SECONDS_ALARM] == s->current_tm.tm_sec) &&
            ((s->cmos_data[RTC_MINUTES_ALARM] & 0xc0) == 0xc0 ||
             s->cmos_data[RTC_MINUTES_ALARM] == s->current_tm.tm_mon) &&
            ((s->cmos_data[RTC_HOURS_ALARM] & 0xc0) == 0xc0 ||
             s->cmos_data[RTC_HOURS_ALARM] == s->current_tm.tm_hour)) {

            s->cmos_data[RTC_REG_C] |= 0xa0;
            rtc_irq_raise(s->irq);
        }
    }

    /* update ended interrupt */
    if (s->cmos_data[RTC_REG_B] & REG_B_UIE) {
        s->cmos_data[RTC_REG_C] |= 0x90;
        rtc_irq_raise(s->irq);
    }

    /* clear update in progress bit */
    s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;

    s->next_second_time += ticks_per_sec;
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
            s->cmos_data[RTC_REG_C] = 0x00;
            break;
        default:
            ret = s->cmos_data[s->cmos_index];
            break;
        }
#ifdef DEBUG_CMOS
        printf("cmos: read index=0x%02x val=0x%02x\n",
               s->cmos_index, ret);
#endif
        return ret;
    }
}

void rtc_set_memory(RTCState *s, int addr, int val)
{
    if (addr >= 0 && addr <= 127)
        s->cmos_data[addr] = val;
}

void rtc_set_date(RTCState *s, const struct tm *tm)
{
    s->current_tm = *tm;
    rtc_copy_date(s);
}

/* PC cmos mappings */
#define REG_IBM_CENTURY_BYTE        0x32
#define REG_IBM_PS2_CENTURY_BYTE    0x37

static void rtc_set_date_from_host(RTCState *s)
{
    struct tm tm;
    int val;

    /* set the CMOS date */
    qemu_get_timedate(&tm, 0);
    rtc_set_date(s, &tm);

    val = to_bcd(s, (tm.tm_year / 100) + 19);
    rtc_set_memory(s, REG_IBM_CENTURY_BYTE, val);
    rtc_set_memory(s, REG_IBM_PS2_CENTURY_BYTE, val);
}

static void rtc_save(QEMUFile *f, void *opaque)
{
    RTCState *s = opaque;

    qemu_put_buffer(f, s->cmos_data, 128);
    qemu_put_8s(f, &s->cmos_index);

    qemu_put_be32(f, s->current_tm.tm_sec);
    qemu_put_be32(f, s->current_tm.tm_min);
    qemu_put_be32(f, s->current_tm.tm_hour);
    qemu_put_be32(f, s->current_tm.tm_wday);
    qemu_put_be32(f, s->current_tm.tm_mday);
    qemu_put_be32(f, s->current_tm.tm_mon);
    qemu_put_be32(f, s->current_tm.tm_year);

    qemu_put_timer(f, s->periodic_timer);
    qemu_put_be64(f, s->next_periodic_time);

    qemu_put_be64(f, s->next_second_time);
    qemu_put_timer(f, s->second_timer);
    qemu_put_timer(f, s->second_timer2);
}

static int rtc_load(QEMUFile *f, void *opaque, int version_id)
{
    RTCState *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_buffer(f, s->cmos_data, 128);
    qemu_get_8s(f, &s->cmos_index);

    s->current_tm.tm_sec=qemu_get_be32(f);
    s->current_tm.tm_min=qemu_get_be32(f);
    s->current_tm.tm_hour=qemu_get_be32(f);
    s->current_tm.tm_wday=qemu_get_be32(f);
    s->current_tm.tm_mday=qemu_get_be32(f);
    s->current_tm.tm_mon=qemu_get_be32(f);
    s->current_tm.tm_year=qemu_get_be32(f);

    qemu_get_timer(f, s->periodic_timer);
    s->next_periodic_time=qemu_get_be64(f);

    s->next_second_time=qemu_get_be64(f);
    qemu_get_timer(f, s->second_timer);
    qemu_get_timer(f, s->second_timer2);
    return 0;
}

RTCState *rtc_init(int base, qemu_irq irq)
{
    RTCState *s;

    s = qemu_mallocz(sizeof(RTCState));
    if (!s)
        return NULL;

    s->irq = irq;
    s->cmos_data[RTC_REG_A] = 0x26;
    s->cmos_data[RTC_REG_B] = 0x02;
    s->cmos_data[RTC_REG_C] = 0x00;
    s->cmos_data[RTC_REG_D] = 0x80;

    rtc_set_date_from_host(s);

    s->periodic_timer = qemu_new_timer(vm_clock,
                                       rtc_periodic_timer, s);
    s->second_timer = qemu_new_timer(vm_clock,
                                     rtc_update_second, s);
    s->second_timer2 = qemu_new_timer(vm_clock,
                                      rtc_update_second2, s);

    s->next_second_time = qemu_get_clock(vm_clock) + (ticks_per_sec * 99) / 100;
    qemu_mod_timer(s->second_timer2, s->next_second_time);

    register_ioport_write(base, 2, 1, cmos_ioport_write, s);
    register_ioport_read(base, 2, 1, cmos_ioport_read, s);

    register_savevm("mc146818rtc", base, 1, rtc_save, rtc_load, s);
    return s;
}

/* Memory mapped interface */
static uint32_t cmos_mm_readb (void *opaque, target_phys_addr_t addr)
{
    RTCState *s = opaque;

    return cmos_ioport_read(s, addr >> s->it_shift) & 0xFF;
}

static void cmos_mm_writeb (void *opaque,
                            target_phys_addr_t addr, uint32_t value)
{
    RTCState *s = opaque;

    cmos_ioport_write(s, addr >> s->it_shift, value & 0xFF);
}

static uint32_t cmos_mm_readw (void *opaque, target_phys_addr_t addr)
{
    RTCState *s = opaque;
    uint32_t val;

    val = cmos_ioport_read(s, addr >> s->it_shift) & 0xFFFF;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static void cmos_mm_writew (void *opaque,
                            target_phys_addr_t addr, uint32_t value)
{
    RTCState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap16(value);
#endif
    cmos_ioport_write(s, addr >> s->it_shift, value & 0xFFFF);
}

static uint32_t cmos_mm_readl (void *opaque, target_phys_addr_t addr)
{
    RTCState *s = opaque;
    uint32_t val;

    val = cmos_ioport_read(s, addr >> s->it_shift);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static void cmos_mm_writel (void *opaque,
                            target_phys_addr_t addr, uint32_t value)
{
    RTCState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    cmos_ioport_write(s, addr >> s->it_shift, value);
}

static CPUReadMemoryFunc *rtc_mm_read[] = {
    &cmos_mm_readb,
    &cmos_mm_readw,
    &cmos_mm_readl,
};

static CPUWriteMemoryFunc *rtc_mm_write[] = {
    &cmos_mm_writeb,
    &cmos_mm_writew,
    &cmos_mm_writel,
};

RTCState *rtc_mm_init(target_phys_addr_t base, int it_shift, qemu_irq irq)
{
    RTCState *s;
    int io_memory;

    s = qemu_mallocz(sizeof(RTCState));
    if (!s)
        return NULL;

    s->irq = irq;
    s->cmos_data[RTC_REG_A] = 0x26;
    s->cmos_data[RTC_REG_B] = 0x02;
    s->cmos_data[RTC_REG_C] = 0x00;
    s->cmos_data[RTC_REG_D] = 0x80;

    rtc_set_date_from_host(s);

    s->periodic_timer = qemu_new_timer(vm_clock,
                                       rtc_periodic_timer, s);
    s->second_timer = qemu_new_timer(vm_clock,
                                     rtc_update_second, s);
    s->second_timer2 = qemu_new_timer(vm_clock,
                                      rtc_update_second2, s);

    s->next_second_time = qemu_get_clock(vm_clock) + (ticks_per_sec * 99) / 100;
    qemu_mod_timer(s->second_timer2, s->next_second_time);

    io_memory = cpu_register_io_memory(0, rtc_mm_read, rtc_mm_write, s);
    cpu_register_physical_memory(base, 2 << it_shift, io_memory);

    register_savevm("mc146818rtc", base, 1, rtc_save, rtc_load, s);
    return s;
}
