/*
 * AVR 16-bit timer
 *
 * Copyright (c) 2018 University of Kent
 * Author: Ed Robbins
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
 * Driver for 16 bit timers on 8 bit AVR devices.
 * Note:
 * ATmega640/V-1280/V-1281/V-2560/V-2561/V timers 1, 3, 4 and 5 are 16 bit
 */

/*
 * XXX TODO: Power Reduction Register support
 *           prescaler pause support
 *           PWM modes, GPIO, output capture pins, input compare pin
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/avr_timer16.h"
#include "trace.h"

/* Register offsets */
#define T16_CRA     0x0
#define T16_CRB     0x1
#define T16_CRC     0x2
#define T16_CNTL    0x4
#define T16_CNTH    0x5
#define T16_ICRL    0x6
#define T16_ICRH    0x7
#define T16_OCRAL   0x8
#define T16_OCRAH   0x9
#define T16_OCRBL   0xa
#define T16_OCRBH   0xb
#define T16_OCRCL   0xc
#define T16_OCRCH   0xd

/* Field masks */
#define T16_CRA_WGM01   0x3
#define T16_CRA_COMC    0xc
#define T16_CRA_COMB    0x30
#define T16_CRA_COMA    0xc0
#define T16_CRA_OC_CONF \
    (T16_CRA_COMA | T16_CRA_COMB | T16_CRA_COMC)

#define T16_CRB_CS      0x7
#define T16_CRB_WGM23   0x18
#define T16_CRB_ICES    0x40
#define T16_CRB_ICNC    0x80

#define T16_CRC_FOCC    0x20
#define T16_CRC_FOCB    0x40
#define T16_CRC_FOCA    0x80

/* Fields masks both TIMSK and TIFR (interrupt mask/flag registers) */
#define T16_INT_TOV    0x1 /* Timer overflow */
#define T16_INT_OCA    0x2 /* Output compare A */
#define T16_INT_OCB    0x4 /* Output compare B */
#define T16_INT_OCC    0x8 /* Output compare C */
#define T16_INT_IC     0x20 /* Input capture */

/* Clock source values */
#define T16_CLKSRC_STOPPED     0
#define T16_CLKSRC_DIV1        1
#define T16_CLKSRC_DIV8        2
#define T16_CLKSRC_DIV64       3
#define T16_CLKSRC_DIV256      4
#define T16_CLKSRC_DIV1024     5
#define T16_CLKSRC_EXT_FALLING 6
#define T16_CLKSRC_EXT_RISING  7

/* Timer mode values (not including PWM modes) */
#define T16_MODE_NORMAL     0
#define T16_MODE_CTC_OCRA   4
#define T16_MODE_CTC_ICR    12

/* Accessors */
#define CLKSRC(t16) (t16->crb & T16_CRB_CS)
#define MODE(t16)   (((t16->crb & T16_CRB_WGM23) >> 1) | \
                     (t16->cra & T16_CRA_WGM01))
#define CNT(t16)    VAL16(t16->cntl, t16->cnth)
#define OCRA(t16)   VAL16(t16->ocral, t16->ocrah)
#define OCRB(t16)   VAL16(t16->ocrbl, t16->ocrbh)
#define OCRC(t16)   VAL16(t16->ocrcl, t16->ocrch)
#define ICR(t16)    VAL16(t16->icrl, t16->icrh)

/* Helper macros */
#define VAL16(l, h) ((h << 8) | l)
#define DB_PRINT(fmt, args...) /* Nothing */

static inline int64_t avr_timer16_ns_to_ticks(AVRTimer16State *t16, int64_t t)
{
    if (t16->period_ns == 0) {
        return 0;
    }
    return t / t16->period_ns;
}

static void avr_timer16_update_cnt(AVRTimer16State *t16)
{
    uint16_t cnt;
    cnt = avr_timer16_ns_to_ticks(t16, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                                       t16->reset_time_ns);
    t16->cntl = (uint8_t)(cnt & 0xff);
    t16->cnth = (uint8_t)((cnt & 0xff00) >> 8);
}

static inline void avr_timer16_recalc_reset_time(AVRTimer16State *t16)
{
    t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                         CNT(t16) * t16->period_ns;
}

static void avr_timer16_clock_reset(AVRTimer16State *t16)
{
    t16->cntl = 0;
    t16->cnth = 0;
    t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void avr_timer16_clksrc_update(AVRTimer16State *t16)
{
    uint16_t divider = 0;
    switch (CLKSRC(t16)) {
    case T16_CLKSRC_EXT_FALLING:
    case T16_CLKSRC_EXT_RISING:
        qemu_log_mask(LOG_UNIMP, "%s: external clock source unsupported\n",
                      __func__);
        break;
    case T16_CLKSRC_STOPPED:
        break;
    case T16_CLKSRC_DIV1:
        divider = 1;
        break;
    case T16_CLKSRC_DIV8:
        divider = 8;
        break;
    case T16_CLKSRC_DIV64:
        divider = 64;
        break;
    case T16_CLKSRC_DIV256:
        divider = 256;
        break;
    case T16_CLKSRC_DIV1024:
        divider = 1024;
        break;
    default:
        break;
    }
    if (divider) {
        t16->freq_hz = t16->cpu_freq_hz / divider;
        t16->period_ns = NANOSECONDS_PER_SECOND / t16->freq_hz;
        trace_avr_timer16_clksrc_update(t16->freq_hz, t16->period_ns,
                                        (uint64_t)(1e6 / t16->freq_hz));
    }
}

static void avr_timer16_set_alarm(AVRTimer16State *t16)
{
    if (CLKSRC(t16) == T16_CLKSRC_EXT_FALLING ||
        CLKSRC(t16) == T16_CLKSRC_EXT_RISING ||
        CLKSRC(t16) == T16_CLKSRC_STOPPED) {
        /* Timer is disabled or set to external clock source (unsupported) */
        return;
    }

    uint64_t alarm_offset = 0xffff;
    enum NextInterrupt next_interrupt = OVERFLOW;

    switch (MODE(t16)) {
    case T16_MODE_NORMAL:
        /* Normal mode */
        if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16) &&
            (t16->imsk & T16_INT_OCA)) {
            alarm_offset = OCRA(t16);
            next_interrupt = COMPA;
        }
        break;
    case T16_MODE_CTC_OCRA:
        /* CTC mode, top = ocra */
        if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16)) {
            alarm_offset = OCRA(t16);
            next_interrupt = COMPA;
        }
       break;
    case T16_MODE_CTC_ICR:
        /* CTC mode, top = icr */
        if (ICR(t16) < alarm_offset && ICR(t16) > CNT(t16)) {
            alarm_offset = ICR(t16);
            next_interrupt = CAPT;
        }
        if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16) &&
            (t16->imsk & T16_INT_OCA)) {
            alarm_offset = OCRA(t16);
            next_interrupt = COMPA;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: pwm modes are unsupported\n",
                      __func__);
        return;
    }
    if (OCRB(t16) < alarm_offset && OCRB(t16) > CNT(t16) &&
        (t16->imsk & T16_INT_OCB)) {
        alarm_offset = OCRB(t16);
        next_interrupt = COMPB;
    }
    if (OCRC(t16) < alarm_offset && OCRB(t16) > CNT(t16) &&
        (t16->imsk & T16_INT_OCC)) {
        alarm_offset = OCRB(t16);
        next_interrupt = COMPC;
    }
    alarm_offset -= CNT(t16);

    t16->next_interrupt = next_interrupt;
    uint64_t alarm_ns =
        t16->reset_time_ns + ((CNT(t16) + alarm_offset) * t16->period_ns);
    timer_mod(t16->timer, alarm_ns);

    trace_avr_timer16_next_alarm(alarm_offset * t16->period_ns);
}

static void avr_timer16_interrupt(void *opaque)
{
    AVRTimer16State *t16 = opaque;
    uint8_t mode = MODE(t16);

    avr_timer16_update_cnt(t16);

    if (CLKSRC(t16) == T16_CLKSRC_EXT_FALLING ||
        CLKSRC(t16) == T16_CLKSRC_EXT_RISING ||
        CLKSRC(t16) == T16_CLKSRC_STOPPED) {
        /* Timer is disabled or set to external clock source (unsupported) */
        return;
    }

    trace_avr_timer16_interrupt_count(CNT(t16));

    /* Counter overflow */
    if (t16->next_interrupt == OVERFLOW) {
        trace_avr_timer16_interrupt_overflow("counter 0xffff");
        avr_timer16_clock_reset(t16);
        if (t16->imsk & T16_INT_TOV) {
            t16->ifr |= T16_INT_TOV;
            qemu_set_irq(t16->ovf_irq, 1);
        }
    }
    /* Check for ocra overflow in CTC mode */
    if (mode == T16_MODE_CTC_OCRA && t16->next_interrupt == COMPA) {
        trace_avr_timer16_interrupt_overflow("CTC OCRA");
        avr_timer16_clock_reset(t16);
    }
    /* Check for icr overflow in CTC mode */
    if (mode == T16_MODE_CTC_ICR && t16->next_interrupt == CAPT) {
        trace_avr_timer16_interrupt_overflow("CTC ICR");
        avr_timer16_clock_reset(t16);
        if (t16->imsk & T16_INT_IC) {
            t16->ifr |= T16_INT_IC;
            qemu_set_irq(t16->capt_irq, 1);
        }
    }
    /* Check for output compare interrupts */
    if (t16->imsk & T16_INT_OCA && t16->next_interrupt == COMPA) {
        t16->ifr |= T16_INT_OCA;
        qemu_set_irq(t16->compa_irq, 1);
    }
    if (t16->imsk & T16_INT_OCB && t16->next_interrupt == COMPB) {
        t16->ifr |= T16_INT_OCB;
        qemu_set_irq(t16->compb_irq, 1);
    }
    if (t16->imsk & T16_INT_OCC && t16->next_interrupt == COMPC) {
        t16->ifr |= T16_INT_OCC;
        qemu_set_irq(t16->compc_irq, 1);
    }
    avr_timer16_set_alarm(t16);
}

static void avr_timer16_reset(DeviceState *dev)
{
    AVRTimer16State *t16 = AVR_TIMER16(dev);

    avr_timer16_clock_reset(t16);
    avr_timer16_clksrc_update(t16);
    avr_timer16_set_alarm(t16);

    qemu_set_irq(t16->capt_irq, 0);
    qemu_set_irq(t16->compa_irq, 0);
    qemu_set_irq(t16->compb_irq, 0);
    qemu_set_irq(t16->compc_irq, 0);
    qemu_set_irq(t16->ovf_irq, 0);
}

static uint64_t avr_timer16_read(void *opaque, hwaddr offset, unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    uint8_t retval = 0;

    switch (offset) {
    case T16_CRA:
        retval = t16->cra;
        break;
    case T16_CRB:
        retval = t16->crb;
        break;
    case T16_CRC:
        retval = t16->crc;
        break;
    case T16_CNTL:
        avr_timer16_update_cnt(t16);
        t16->rtmp = t16->cnth;
        retval = t16->cntl;
        break;
    case T16_CNTH:
        retval = t16->rtmp;
        break;
    case T16_ICRL:
        /*
         * The timer copies cnt to icr when the input capture pin changes
         * state or when the analog comparator has a match. We don't
         * emulate this behaviour. We do support it's use for defining a
         * TOP value in T16_MODE_CTC_ICR
         */
        t16->rtmp = t16->icrh;
        retval = t16->icrl;
        break;
    case T16_ICRH:
        retval = t16->rtmp;
        break;
    case T16_OCRAL:
        retval = t16->ocral;
        break;
    case T16_OCRAH:
        retval = t16->ocrah;
        break;
    case T16_OCRBL:
        retval = t16->ocrbl;
        break;
    case T16_OCRBH:
        retval = t16->ocrbh;
        break;
    case T16_OCRCL:
        retval = t16->ocrcl;
        break;
    case T16_OCRCH:
        retval = t16->ocrch;
        break;
    default:
        break;
    }
    trace_avr_timer16_read(offset, retval);

    return (uint64_t)retval;
}

static void avr_timer16_write(void *opaque, hwaddr offset,
                              uint64_t val64, unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    uint8_t val8 = (uint8_t)val64;
    uint8_t prev_clk_src = CLKSRC(t16);

    trace_avr_timer16_write(offset, val8);

    switch (offset) {
    case T16_CRA:
        t16->cra = val8;
        if (t16->cra & T16_CRA_OC_CONF) {
            qemu_log_mask(LOG_UNIMP, "%s: output compare pins unsupported\n",
                          __func__);
        }
        break;
    case T16_CRB:
        t16->crb = val8;
        if (t16->crb & T16_CRB_ICNC) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: input capture noise canceller unsupported\n",
                          __func__);
        }
        if (t16->crb & T16_CRB_ICES) {
            qemu_log_mask(LOG_UNIMP, "%s: input capture unsupported\n",
                          __func__);
        }
        if (CLKSRC(t16) != prev_clk_src) {
            avr_timer16_clksrc_update(t16);
            if (prev_clk_src == T16_CLKSRC_STOPPED) {
                t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            }
        }
        break;
    case T16_CRC:
        t16->crc = val8;
        qemu_log_mask(LOG_UNIMP, "%s: output compare pins unsupported\n",
                      __func__);
        break;
    case T16_CNTL:
        /*
         * CNT is the 16-bit counter value, it must be read/written via
         * a temporary register (rtmp) to make the read/write atomic.
         */
        /* ICR also has this behaviour, and shares rtmp */
        /*
         * Writing CNT blocks compare matches for one clock cycle.
         * Writing CNT to TOP or to an OCR value (if in use) will
         * skip the relevant interrupt
         */
        t16->cntl = val8;
        t16->cnth = t16->rtmp;
        avr_timer16_recalc_reset_time(t16);
        break;
    case T16_CNTH:
        t16->rtmp = val8;
        break;
    case T16_ICRL:
        /* ICR can only be written in mode T16_MODE_CTC_ICR */
        if (MODE(t16) == T16_MODE_CTC_ICR) {
            t16->icrl = val8;
            t16->icrh = t16->rtmp;
        }
        break;
    case T16_ICRH:
        if (MODE(t16) == T16_MODE_CTC_ICR) {
            t16->rtmp = val8;
        }
        break;
    case T16_OCRAL:
        /*
         * OCRn cause the relevant output compare flag to be raised, and
         * trigger an interrupt, when CNT is equal to the value here
         */
        t16->ocral = val8;
        break;
    case T16_OCRAH:
        t16->ocrah = val8;
        break;
    case T16_OCRBL:
        t16->ocrbl = val8;
        break;
    case T16_OCRBH:
        t16->ocrbh = val8;
        break;
    case T16_OCRCL:
        t16->ocrcl = val8;
        break;
    case T16_OCRCH:
        t16->ocrch = val8;
        break;
    default:
        break;
    }
    avr_timer16_set_alarm(t16);
}

static uint64_t avr_timer16_imsk_read(void *opaque,
                                      hwaddr offset,
                                      unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    trace_avr_timer16_read_imsk(offset ? 0 : t16->imsk);
    if (offset != 0) {
        return 0;
    }
    return t16->imsk;
}

static void avr_timer16_imsk_write(void *opaque, hwaddr offset,
                                   uint64_t val64, unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    trace_avr_timer16_write_imsk(val64);
    if (offset != 0) {
        return;
    }
    t16->imsk = (uint8_t)val64;
}

static uint64_t avr_timer16_ifr_read(void *opaque,
                                     hwaddr offset,
                                     unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    trace_avr_timer16_read_ifr(offset ? 0 : t16->ifr);
    if (offset != 0) {
        return 0;
    }
    return t16->ifr;
}

static void avr_timer16_ifr_write(void *opaque, hwaddr offset,
                                  uint64_t val64, unsigned size)
{
    assert(size == 1);
    AVRTimer16State *t16 = opaque;
    trace_avr_timer16_write_imsk(val64);
    if (offset != 0) {
        return;
    }
    t16->ifr = (uint8_t)val64;
}

static const MemoryRegionOps avr_timer16_ops = {
    .read = avr_timer16_read,
    .write = avr_timer16_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static const MemoryRegionOps avr_timer16_imsk_ops = {
    .read = avr_timer16_imsk_read,
    .write = avr_timer16_imsk_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static const MemoryRegionOps avr_timer16_ifr_ops = {
    .read = avr_timer16_ifr_read,
    .write = avr_timer16_ifr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static Property avr_timer16_properties[] = {
    DEFINE_PROP_UINT8("id", struct AVRTimer16State, id, 0),
    DEFINE_PROP_UINT64("cpu-frequency-hz", struct AVRTimer16State,
                       cpu_freq_hz, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void avr_timer16_pr(void *opaque, int irq, int level)
{
    AVRTimer16State *s = AVR_TIMER16(opaque);

    s->enabled = !level;

    if (!s->enabled) {
        avr_timer16_reset(DEVICE(s));
    }
}

static void avr_timer16_init(Object *obj)
{
    AVRTimer16State *s = AVR_TIMER16(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->capt_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->compa_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->compb_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->compc_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->ovf_irq);

    memory_region_init_io(&s->iomem, obj, &avr_timer16_ops,
                          s, "avr-timer16", 0xe);
    memory_region_init_io(&s->imsk_iomem, obj, &avr_timer16_imsk_ops,
                          s, "avr-timer16-intmask", 0x1);
    memory_region_init_io(&s->ifr_iomem, obj, &avr_timer16_ifr_ops,
                          s, "avr-timer16-intflag", 0x1);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->imsk_iomem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->ifr_iomem);
    qdev_init_gpio_in(DEVICE(s), avr_timer16_pr, 1);
}

static void avr_timer16_realize(DeviceState *dev, Error **errp)
{
    AVRTimer16State *s = AVR_TIMER16(dev);

    if (s->cpu_freq_hz == 0) {
        error_setg(errp, "AVR timer16: cpu-frequency-hz property must be set");
        return;
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, avr_timer16_interrupt, s);
    s->enabled = true;
}

static void avr_timer16_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = avr_timer16_reset;
    dc->realize = avr_timer16_realize;
    device_class_set_props(dc, avr_timer16_properties);
}

static const TypeInfo avr_timer16_info = {
    .name          = TYPE_AVR_TIMER16,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AVRTimer16State),
    .instance_init = avr_timer16_init,
    .class_init    = avr_timer16_class_init,
};

static void avr_timer16_register_types(void)
{
    type_register_static(&avr_timer16_info);
}

type_init(avr_timer16_register_types)
