/*
 * Renesas 8bit timer
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/timer/renesas_tmr.h"
#include "migration/vmstate.h"

REG8(TCR, 0)
  FIELD(TCR, CCLR,  3, 2)
  FIELD(TCR, OVIE,  5, 1)
  FIELD(TCR, CMIEA, 6, 1)
  FIELD(TCR, CMIEB, 7, 1)
REG8(TCSR, 2)
  FIELD(TCSR, OSA,  0, 2)
  FIELD(TCSR, OSB,  2, 2)
  FIELD(TCSR, ADTE, 4, 2)
REG8(TCORA, 4)
REG8(TCORB, 6)
REG8(TCNT, 8)
REG8(TCCR, 10)
  FIELD(TCCR, CKS,   0, 3)
  FIELD(TCCR, CSS,   3, 2)
  FIELD(TCCR, TMRIS, 7, 1)

#define CSS_EXTERNAL  0x00
#define CSS_INTERNAL  0x01
#define CSS_INVALID   0x02
#define CSS_CASCADING 0x03
#define CCLR_A    0x01
#define CCLR_B    0x02

static const int clkdiv[] = {0, 1, 2, 8, 32, 64, 1024, 8192};

static uint8_t concat_reg(uint8_t *reg)
{
    return (reg[0] << 8) | reg[1];
}

static void update_events(RTMRState *tmr, int ch)
{
    uint16_t diff[TMR_NR_EVENTS], min;
    int64_t next_time;
    int i, event;

    if (tmr->tccr[ch] == 0) {
        return;
    }
    if (FIELD_EX8(tmr->tccr[ch], TCCR, CSS) == 0) {
        /* external clock mode */
        /* event not happened */
        return;
    }
    if (FIELD_EX8(tmr->tccr[0], TCCR, CSS) == CSS_CASCADING) {
        /* cascading mode */
        if (ch == 1) {
            tmr->next[ch] = none;
            return;
        }
        diff[cmia] = concat_reg(tmr->tcora) - concat_reg(tmr->tcnt);
        diff[cmib] = concat_reg(tmr->tcorb) - concat_reg(tmr->tcnt);
        diff[ovi] = 0x10000 - concat_reg(tmr->tcnt);
    } else {
        /* separate mode */
        diff[cmia] = tmr->tcora[ch] - tmr->tcnt[ch];
        diff[cmib] = tmr->tcorb[ch] - tmr->tcnt[ch];
        diff[ovi] = 0x100 - tmr->tcnt[ch];
    }
    /* Search for the most recently occurring event. */
    for (event = 0, min = diff[0], i = 1; i < none; i++) {
        if (min > diff[i]) {
            event = i;
            min = diff[i];
        }
    }
    tmr->next[ch] = event;
    next_time = diff[event];
    next_time *= clkdiv[FIELD_EX8(tmr->tccr[ch], TCCR, CKS)];
    next_time *= NANOSECONDS_PER_SECOND;
    next_time /= tmr->input_freq;
    next_time += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(&tmr->timer[ch], next_time);
}

static int elapsed_time(RTMRState *tmr, int ch, int64_t delta)
{
    int divrate = clkdiv[FIELD_EX8(tmr->tccr[ch], TCCR, CKS)];
    int et;

    tmr->div_round[ch] += delta;
    if (divrate > 0) {
        et = tmr->div_round[ch] / divrate;
        tmr->div_round[ch] %= divrate;
    } else {
        /* disable clock. so no update */
        et = 0;
    }
    return et;
}

static uint16_t read_tcnt(RTMRState *tmr, unsigned size, int ch)
{
    int64_t delta, now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int elapsed, ovf = 0;
    uint16_t tcnt[2];
    uint32_t ret;

    delta = (now - tmr->tick) * NANOSECONDS_PER_SECOND / tmr->input_freq;
    if (delta > 0) {
        tmr->tick = now;

        switch (FIELD_EX8(tmr->tccr[1], TCCR, CSS)) {
        case CSS_INTERNAL:
            /* timer1 count update */
            elapsed = elapsed_time(tmr, 1, delta);
            if (elapsed >= 0x100) {
                ovf = elapsed >> 8;
            }
            tcnt[1] = tmr->tcnt[1] + (elapsed & 0xff);
            break;
        case CSS_INVALID: /* guest error to have set this */
        case CSS_EXTERNAL: /* QEMU doesn't implement these */
        case CSS_CASCADING:
            tcnt[1] = tmr->tcnt[1];
            break;
        default:
            g_assert_not_reached();
        }
        switch (FIELD_EX8(tmr->tccr[0], TCCR, CSS)) {
        case CSS_INTERNAL:
            elapsed = elapsed_time(tmr, 0, delta);
            tcnt[0] = tmr->tcnt[0] + elapsed;
            break;
        case CSS_CASCADING:
            tcnt[0] = tmr->tcnt[0] + ovf;
            break;
        case CSS_INVALID: /* guest error to have set this */
        case CSS_EXTERNAL: /* QEMU doesn't implement this */
            tcnt[0] = tmr->tcnt[0];
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        tcnt[0] = tmr->tcnt[0];
        tcnt[1] = tmr->tcnt[1];
    }
    if (size == 1) {
        return tcnt[ch];
    } else {
        ret = 0;
        ret = deposit32(ret, 0, 8, tcnt[1]);
        ret = deposit32(ret, 8, 8, tcnt[0]);
        return ret;
    }
}

static uint8_t read_tccr(uint8_t r)
{
    uint8_t tccr = 0;
    tccr = FIELD_DP8(tccr, TCCR, TMRIS,
                     FIELD_EX8(r, TCCR, TMRIS));
    tccr = FIELD_DP8(tccr, TCCR, CSS,
                     FIELD_EX8(r, TCCR, CSS));
    tccr = FIELD_DP8(tccr, TCCR, CKS,
                     FIELD_EX8(r, TCCR, CKS));
    return tccr;
}

static uint64_t tmr_read(void *opaque, hwaddr addr, unsigned size)
{
    RTMRState *tmr = opaque;
    int ch = addr & 1;
    uint64_t ret;

    if (size == 2 && (ch != 0 || addr == A_TCR || addr == A_TCSR)) {
        qemu_log_mask(LOG_GUEST_ERROR, "renesas_tmr: Invalid read size 0x%"
                                       HWADDR_PRIX "\n",
                      addr);
        return UINT64_MAX;
    }
    switch (addr & 0x0e) {
    case A_TCR:
        ret = 0;
        ret = FIELD_DP8(ret, TCR, CCLR,
                        FIELD_EX8(tmr->tcr[ch], TCR, CCLR));
        ret = FIELD_DP8(ret, TCR, OVIE,
                        FIELD_EX8(tmr->tcr[ch], TCR, OVIE));
        ret = FIELD_DP8(ret, TCR, CMIEA,
                        FIELD_EX8(tmr->tcr[ch], TCR, CMIEA));
        ret = FIELD_DP8(ret, TCR, CMIEB,
                        FIELD_EX8(tmr->tcr[ch], TCR, CMIEB));
        return ret;
    case A_TCSR:
        ret = 0;
        ret = FIELD_DP8(ret, TCSR, OSA,
                        FIELD_EX8(tmr->tcsr[ch], TCSR, OSA));
        ret = FIELD_DP8(ret, TCSR, OSB,
                        FIELD_EX8(tmr->tcsr[ch], TCSR, OSB));
        switch (ch) {
        case 0:
            ret = FIELD_DP8(ret, TCSR, ADTE,
                            FIELD_EX8(tmr->tcsr[ch], TCSR, ADTE));
            break;
        case 1: /* CH1 ADTE unimplement always 1 */
            ret = FIELD_DP8(ret, TCSR, ADTE, 1);
            break;
        }
        return ret;
    case A_TCORA:
        if (size == 1) {
            return tmr->tcora[ch];
        } else if (ch == 0) {
            return concat_reg(tmr->tcora);
        }
        /* fall through */
    case A_TCORB:
        if (size == 1) {
            return tmr->tcorb[ch];
        } else {
            return concat_reg(tmr->tcorb);
        }
    case A_TCNT:
        return read_tcnt(tmr, size, ch);
    case A_TCCR:
        if (size == 1) {
            return read_tccr(tmr->tccr[ch]);
        } else {
            return read_tccr(tmr->tccr[0]) << 8 | read_tccr(tmr->tccr[1]);
        }
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_tmr: Register 0x%" HWADDR_PRIX
                                 " not implemented\n",
                      addr);
        break;
    }
    return UINT64_MAX;
}

static void tmr_write_count(RTMRState *tmr, int ch, unsigned size,
                            uint8_t *reg, uint64_t val)
{
    if (size == 1) {
        reg[ch] = val;
        update_events(tmr, ch);
    } else {
        reg[0] = extract32(val, 8, 8);
        reg[1] = extract32(val, 0, 8);
        update_events(tmr, 0);
        update_events(tmr, 1);
    }
}

static void tmr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RTMRState *tmr = opaque;
    int ch = addr & 1;

    if (size == 2 && (ch != 0 || addr == A_TCR || addr == A_TCSR)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_tmr: Invalid write size 0x%" HWADDR_PRIX "\n",
                      addr);
        return;
    }
    switch (addr & 0x0e) {
    case A_TCR:
        tmr->tcr[ch] = val;
        break;
    case A_TCSR:
        tmr->tcsr[ch] = val;
        break;
    case A_TCORA:
        tmr_write_count(tmr, ch, size, tmr->tcora, val);
        break;
    case A_TCORB:
        tmr_write_count(tmr, ch, size, tmr->tcorb, val);
        break;
    case A_TCNT:
        tmr_write_count(tmr, ch, size, tmr->tcnt, val);
        break;
    case A_TCCR:
        tmr_write_count(tmr, ch, size, tmr->tccr, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_tmr: Register 0x%" HWADDR_PRIX
                                 " not implemented\n",
                      addr);
        break;
    }
}

static const MemoryRegionOps tmr_ops = {
    .write = tmr_write,
    .read  = tmr_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void timer_events(RTMRState *tmr, int ch);

static uint16_t issue_event(RTMRState *tmr, int ch, int sz,
                        uint16_t tcnt, uint16_t tcora, uint16_t tcorb)
{
    uint16_t ret = tcnt;

    switch (tmr->next[ch]) {
    case none:
        break;
    case cmia:
        if (tcnt >= tcora) {
            if (FIELD_EX8(tmr->tcr[ch], TCR, CCLR) == CCLR_A) {
                ret = tcnt - tcora;
            }
            if (FIELD_EX8(tmr->tcr[ch], TCR, CMIEA)) {
                qemu_irq_pulse(tmr->cmia[ch]);
            }
            if (sz == 8 && ch == 0 &&
                FIELD_EX8(tmr->tccr[1], TCCR, CSS) == CSS_CASCADING) {
                tmr->tcnt[1]++;
                timer_events(tmr, 1);
            }
        }
        break;
    case cmib:
        if (tcnt >= tcorb) {
            if (FIELD_EX8(tmr->tcr[ch], TCR, CCLR) == CCLR_B) {
                ret = tcnt - tcorb;
            }
            if (FIELD_EX8(tmr->tcr[ch], TCR, CMIEB)) {
                qemu_irq_pulse(tmr->cmib[ch]);
            }
        }
        break;
    case ovi:
        if ((tcnt >= (1 << sz)) && FIELD_EX8(tmr->tcr[ch], TCR, OVIE)) {
            qemu_irq_pulse(tmr->ovi[ch]);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

static void timer_events(RTMRState *tmr, int ch)
{
    uint16_t tcnt;

    tmr->tcnt[ch] = read_tcnt(tmr, 1, ch);
    if (FIELD_EX8(tmr->tccr[0], TCCR, CSS) != CSS_CASCADING) {
        tmr->tcnt[ch] = issue_event(tmr, ch, 8,
                                    tmr->tcnt[ch],
                                    tmr->tcora[ch],
                                    tmr->tcorb[ch]) & 0xff;
    } else {
        if (ch == 1) {
            return;
        }
        tcnt = issue_event(tmr, ch, 16,
                           concat_reg(tmr->tcnt),
                           concat_reg(tmr->tcora),
                           concat_reg(tmr->tcorb));
        tmr->tcnt[0] = (tcnt >> 8) & 0xff;
        tmr->tcnt[1] = tcnt & 0xff;
    }
    update_events(tmr, ch);
}

static void timer_event0(void *opaque)
{
    RTMRState *tmr = opaque;

    timer_events(tmr, 0);
}

static void timer_event1(void *opaque)
{
    RTMRState *tmr = opaque;

    timer_events(tmr, 1);
}

static void rtmr_reset(DeviceState *dev)
{
    RTMRState *tmr = RTMR(dev);
    tmr->tcr[0]   = tmr->tcr[1]   = 0x00;
    tmr->tcsr[0]  = 0x00;
    tmr->tcsr[1]  = 0x10;
    tmr->tcnt[0]  = tmr->tcnt[1]  = 0x00;
    tmr->tcora[0] = tmr->tcora[1] = 0xff;
    tmr->tcorb[0] = tmr->tcorb[1] = 0xff;
    tmr->tccr[0]  = tmr->tccr[1]  = 0x00;
    tmr->next[0]  = tmr->next[1]  = none;
    tmr->tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void rtmr_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RTMRState *tmr = RTMR(obj);
    int i;

    memory_region_init_io(&tmr->memory, OBJECT(tmr), &tmr_ops,
                          tmr, "renesas-tmr", 0x10);
    sysbus_init_mmio(d, &tmr->memory);

    for (i = 0; i < ARRAY_SIZE(tmr->ovi); i++) {
        sysbus_init_irq(d, &tmr->cmia[i]);
        sysbus_init_irq(d, &tmr->cmib[i]);
        sysbus_init_irq(d, &tmr->ovi[i]);
    }
    timer_init_ns(&tmr->timer[0], QEMU_CLOCK_VIRTUAL, timer_event0, tmr);
    timer_init_ns(&tmr->timer[1], QEMU_CLOCK_VIRTUAL, timer_event1, tmr);
}

static const VMStateDescription vmstate_rtmr = {
    .name = "rx-tmr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(tick, RTMRState),
        VMSTATE_UINT8_ARRAY(tcnt, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tcora, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tcorb, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tcr, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tccr, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tcor, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(tcsr, RTMRState, TMR_CH),
        VMSTATE_INT64_ARRAY(div_round, RTMRState, TMR_CH),
        VMSTATE_UINT8_ARRAY(next, RTMRState, TMR_CH),
        VMSTATE_TIMER_ARRAY(timer, RTMRState, TMR_CH),
        VMSTATE_END_OF_LIST()
    }
};

static Property rtmr_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RTMRState, input_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rtmr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rtmr;
    dc->reset = rtmr_reset;
    device_class_set_props(dc, rtmr_properties);
}

static const TypeInfo rtmr_info = {
    .name = TYPE_RENESAS_TMR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RTMRState),
    .instance_init = rtmr_init,
    .class_init = rtmr_class_init,
};

static void rtmr_register_types(void)
{
    type_register_static(&rtmr_info);
}

type_init(rtmr_register_types)
