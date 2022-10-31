/*
 * QEMU MOS6522 VIA emulation
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2018 Mark Cave-Ayland
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

#include "qemu/osdep.h"
#include "hw/input/adb.h"
#include "hw/irq.h"
#include "hw/misc/mos6522.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/type-helpers.h"
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"


static const char *mos6522_reg_names[MOS6522_NUM_REGS] = {
    "ORB", "ORA", "DDRB", "DDRA", "T1CL", "T1CH", "T1LL", "T1LH",
    "T2CL", "T2CH", "SR", "ACR", "PCR", "IFR", "IER", "ANH"
};

/* XXX: implement all timer modes */

static void mos6522_timer1_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t current_time);
static void mos6522_timer2_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t current_time);

static void mos6522_update_irq(MOS6522State *s)
{
    if (s->ifr & s->ier) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void mos6522_set_irq(void *opaque, int n, int level)
{
    MOS6522State *s = MOS6522(opaque);
    int last_level = !!(s->last_irq_levels & (1 << n));
    uint8_t last_ifr = s->ifr;
    bool positive_edge = true;
    int ctrl;

    /*
     * SR_INT is managed by mos6522 instances and cleared upon SR
     * read. It is only the external CA1/2 and CB1/2 lines that
     * are edge-triggered and latched in IFR
     */
    if (n != SR_INT_BIT && level == last_level) {
        return;
    }

    /* Detect negative edge trigger */
    if (last_level == 1 && level == 0) {
        positive_edge = false;
    }

    switch (n) {
    case CA2_INT_BIT:
        ctrl = (s->pcr & CA2_CTRL_MASK) >> CA2_CTRL_SHIFT;
        if ((positive_edge && (ctrl & C2_POS)) ||
             (!positive_edge && !(ctrl & C2_POS))) {
            s->ifr |= 1 << n;
        }
        break;
    case CA1_INT_BIT:
        ctrl = (s->pcr & CA1_CTRL_MASK) >> CA1_CTRL_SHIFT;
        if ((positive_edge && (ctrl & C1_POS)) ||
             (!positive_edge && !(ctrl & C1_POS))) {
            s->ifr |= 1 << n;
        }
        break;
    case SR_INT_BIT:
        s->ifr |= 1 << n;
        break;
    case CB2_INT_BIT:
        ctrl = (s->pcr & CB2_CTRL_MASK) >> CB2_CTRL_SHIFT;
        if ((positive_edge && (ctrl & C2_POS)) ||
             (!positive_edge && !(ctrl & C2_POS))) {
            s->ifr |= 1 << n;
        }
        break;
    case CB1_INT_BIT:
        ctrl = (s->pcr & CB1_CTRL_MASK) >> CB1_CTRL_SHIFT;
        if ((positive_edge && (ctrl & C1_POS)) ||
             (!positive_edge && !(ctrl & C1_POS))) {
            s->ifr |= 1 << n;
        }
        break;
    }

    if (s->ifr != last_ifr) {
        mos6522_update_irq(s);
    }

    if (level) {
        s->last_irq_levels |= 1 << n;
    } else {
        s->last_irq_levels &= ~(1 << n);
    }
}

static uint64_t get_counter_value(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(s);

    if (ti->index == 0) {
        return mdc->get_timer1_counter_value(s, ti);
    } else {
        return mdc->get_timer2_counter_value(s, ti);
    }
}

static uint64_t get_load_time(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(s);

    if (ti->index == 0) {
        return mdc->get_timer1_load_time(s, ti);
    } else {
        return mdc->get_timer2_load_time(s, ti);
    }
}

static unsigned int get_counter(MOS6522State *s, MOS6522Timer *ti)
{
    int64_t d;
    unsigned int counter;

    d = get_counter_value(s, ti);

    if (ti->index == 0) {
        /* the timer goes down from latch to -1 (period of latch + 2) */
        if (d <= (ti->counter_value + 1)) {
            counter = (ti->counter_value - d) & 0xffff;
        } else {
            counter = (d - (ti->counter_value + 1)) % (ti->latch + 2);
            counter = (ti->latch - counter) & 0xffff;
        }
    } else {
        counter = (ti->counter_value - d) & 0xffff;
    }
    return counter;
}

static void set_counter(MOS6522State *s, MOS6522Timer *ti, unsigned int val)
{
    trace_mos6522_set_counter(1 + ti->index, val);
    ti->load_time = get_load_time(s, ti);
    ti->counter_value = val;
    if (ti->index == 0) {
        mos6522_timer1_update(s, ti, ti->load_time);
    } else {
        mos6522_timer2_update(s, ti, ti->load_time);
    }
}

static int64_t get_next_irq_time(MOS6522State *s, MOS6522Timer *ti,
                                 int64_t current_time)
{
    int64_t d, next_time;
    unsigned int counter;

    if (ti->frequency == 0) {
        return INT64_MAX;
    }

    /* current counter value */
    d = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - ti->load_time,
                 ti->frequency, NANOSECONDS_PER_SECOND);

    /* the timer goes down from latch to -1 (period of latch + 2) */
    if (d <= (ti->counter_value + 1)) {
        counter = (ti->counter_value - d) & 0xffff;
    } else {
        counter = (d - (ti->counter_value + 1)) % (ti->latch + 2);
        counter = (ti->latch - counter) & 0xffff;
    }

    /* Note: we consider the irq is raised on 0 */
    if (counter == 0xffff) {
        next_time = d + ti->latch + 1;
    } else if (counter == 0) {
        next_time = d + ti->latch + 2;
    } else {
        next_time = d + counter;
    }
    trace_mos6522_get_next_irq_time(ti->latch, d, next_time - d);
    next_time = muldiv64(next_time, NANOSECONDS_PER_SECOND, ti->frequency) +
                         ti->load_time;

    if (next_time <= current_time) {
        next_time = current_time + 1;
    }
    return next_time;
}

static void mos6522_timer1_update(MOS6522State *s, MOS6522Timer *ti,
                                 int64_t current_time)
{
    if (!ti->timer) {
        return;
    }
    ti->next_irq_time = get_next_irq_time(s, ti, current_time);
    if ((s->ier & T1_INT) == 0 || (s->acr & T1MODE) != T1MODE_CONT) {
        timer_del(ti->timer);
    } else {
        timer_mod(ti->timer, ti->next_irq_time);
    }
}

static void mos6522_timer2_update(MOS6522State *s, MOS6522Timer *ti,
                                 int64_t current_time)
{
    if (!ti->timer) {
        return;
    }
    ti->next_irq_time = get_next_irq_time(s, ti, current_time);
    if ((s->ier & T2_INT) == 0) {
        timer_del(ti->timer);
    } else {
        timer_mod(ti->timer, ti->next_irq_time);
    }
}

static void mos6522_timer1(void *opaque)
{
    MOS6522State *s = opaque;
    MOS6522Timer *ti = &s->timers[0];

    mos6522_timer1_update(s, ti, ti->next_irq_time);
    s->ifr |= T1_INT;
    mos6522_update_irq(s);
}

static void mos6522_timer2(void *opaque)
{
    MOS6522State *s = opaque;
    MOS6522Timer *ti = &s->timers[1];

    mos6522_timer2_update(s, ti, ti->next_irq_time);
    s->ifr |= T2_INT;
    mos6522_update_irq(s);
}

static uint64_t mos6522_get_counter_value(MOS6522State *s, MOS6522Timer *ti)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - ti->load_time,
                    ti->frequency, NANOSECONDS_PER_SECOND);
}

static uint64_t mos6522_get_load_time(MOS6522State *s, MOS6522Timer *ti)
{
    uint64_t load_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    return load_time;
}

static void mos6522_portA_write(MOS6522State *s)
{
    qemu_log_mask(LOG_UNIMP, "portA_write unimplemented\n");
}

static void mos6522_portB_write(MOS6522State *s)
{
    qemu_log_mask(LOG_UNIMP, "portB_write unimplemented\n");
}

uint64_t mos6522_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522State *s = opaque;
    uint32_t val;
    int ctrl;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (now >= s->timers[0].next_irq_time) {
        mos6522_timer1_update(s, &s->timers[0], now);
        s->ifr |= T1_INT;
    }
    if (now >= s->timers[1].next_irq_time) {
        mos6522_timer2_update(s, &s->timers[1], now);
        s->ifr |= T2_INT;
    }
    switch (addr) {
    case VIA_REG_B:
        val = s->b;
        ctrl = (s->pcr & CB2_CTRL_MASK) >> CB2_CTRL_SHIFT;
        if (!(ctrl & C2_IND)) {
            s->ifr &= ~CB2_INT;
        }
        s->ifr &= ~CB1_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_A:
       qemu_log_mask(LOG_UNIMP, "Read access to register A with handshake");
       /* fall through */
    case VIA_REG_ANH:
        val = s->a;
        ctrl = (s->pcr & CA2_CTRL_MASK) >> CA2_CTRL_SHIFT;
        if (!(ctrl & C2_IND)) {
            s->ifr &= ~CA2_INT;
        }
        s->ifr &= ~CA1_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_DIRB:
        val = s->dirb;
        break;
    case VIA_REG_DIRA:
        val = s->dira;
        break;
    case VIA_REG_T1CL:
        val = get_counter(s, &s->timers[0]) & 0xff;
        s->ifr &= ~T1_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_T1CH:
        val = get_counter(s, &s->timers[0]) >> 8;
        mos6522_update_irq(s);
        break;
    case VIA_REG_T1LL:
        val = s->timers[0].latch & 0xff;
        break;
    case VIA_REG_T1LH:
        /* XXX: check this */
        val = (s->timers[0].latch >> 8) & 0xff;
        break;
    case VIA_REG_T2CL:
        val = get_counter(s, &s->timers[1]) & 0xff;
        s->ifr &= ~T2_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_T2CH:
        val = get_counter(s, &s->timers[1]) >> 8;
        break;
    case VIA_REG_SR:
        val = s->sr;
        s->ifr &= ~SR_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_ACR:
        val = s->acr;
        break;
    case VIA_REG_PCR:
        val = s->pcr;
        break;
    case VIA_REG_IFR:
        val = s->ifr;
        if (s->ifr & s->ier) {
            val |= 0x80;
        }
        break;
    case VIA_REG_IER:
        val = s->ier | 0x80;
        break;
    default:
        g_assert_not_reached();
    }

    if (addr != VIA_REG_IFR || val != 0) {
        trace_mos6522_read(addr, mos6522_reg_names[addr], val);
    }

    return val;
}

void mos6522_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MOS6522State *s = opaque;
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(s);
    int ctrl;

    trace_mos6522_write(addr, mos6522_reg_names[addr], val);

    switch (addr) {
    case VIA_REG_B:
        s->b = (s->b & ~s->dirb) | (val & s->dirb);
        mdc->portB_write(s);
        ctrl = (s->pcr & CB2_CTRL_MASK) >> CB2_CTRL_SHIFT;
        if (!(ctrl & C2_IND)) {
            s->ifr &= ~CB2_INT;
        }
        s->ifr &= ~CB1_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_A:
       qemu_log_mask(LOG_UNIMP, "Write access to register A with handshake");
       /* fall through */
    case VIA_REG_ANH:
        s->a = (s->a & ~s->dira) | (val & s->dira);
        mdc->portA_write(s);
        ctrl = (s->pcr & CA2_CTRL_MASK) >> CA2_CTRL_SHIFT;
        if (!(ctrl & C2_IND)) {
            s->ifr &= ~CA2_INT;
        }
        s->ifr &= ~CA1_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_DIRB:
        s->dirb = val;
        break;
    case VIA_REG_DIRA:
        s->dira = val;
        break;
    case VIA_REG_T1CL:
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        mos6522_timer1_update(s, &s->timers[0],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case VIA_REG_T1CH:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        set_counter(s, &s->timers[0], s->timers[0].latch);
        break;
    case VIA_REG_T1LL:
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        mos6522_timer1_update(s, &s->timers[0],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case VIA_REG_T1LH:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        mos6522_timer1_update(s, &s->timers[0],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case VIA_REG_T2CL:
        s->timers[1].latch = (s->timers[1].latch & 0xff00) | val;
        break;
    case VIA_REG_T2CH:
        /* To ensure T2 generates an interrupt on zero crossing with the
           common timer code, write the value directly from the latch to
           the counter */
        s->timers[1].latch = (s->timers[1].latch & 0xff) | (val << 8);
        s->ifr &= ~T2_INT;
        set_counter(s, &s->timers[1], s->timers[1].latch);
        break;
    case VIA_REG_SR:
        s->sr = val;
        break;
    case VIA_REG_ACR:
        s->acr = val;
        mos6522_timer1_update(s, &s->timers[0],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    case VIA_REG_PCR:
        s->pcr = val;
        break;
    case VIA_REG_IFR:
        /* reset bits */
        s->ifr &= ~val;
        mos6522_update_irq(s);
        break;
    case VIA_REG_IER:
        if (val & IER_SET) {
            /* set bits */
            s->ier |= val & 0x7f;
        } else {
            /* reset bits */
            s->ier &= ~val;
        }
        mos6522_update_irq(s);
        /* if IER is modified starts needed timers */
        mos6522_timer1_update(s, &s->timers[0],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        mos6522_timer2_update(s, &s->timers[1],
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        break;
    default:
        g_assert_not_reached();
    }
}

static int qmp_x_query_via_foreach(Object *obj, void *opaque)
{
    GString *buf = opaque;

    if (object_dynamic_cast(obj, TYPE_MOS6522)) {
        MOS6522State *s = MOS6522(obj);
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint16_t t1counter = get_counter(s, &s->timers[0]);
        uint16_t t2counter = get_counter(s, &s->timers[1]);

        g_string_append_printf(buf, "%s:\n", object_get_typename(obj));

        g_string_append_printf(buf, "  Registers:\n");
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[0], s->b);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[1], s->a);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[2], s->dirb);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[3], s->dira);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[4], t1counter & 0xff);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[5], t1counter >> 8);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[6],
                               s->timers[0].latch & 0xff);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[7],
                               s->timers[0].latch >> 8);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[8], t2counter & 0xff);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[9], t2counter >> 8);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[10], s->sr);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[11], s->acr);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[12], s->pcr);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[13], s->ifr);
        g_string_append_printf(buf, "    %-*s:    0x%x\n", 4,
                               mos6522_reg_names[14], s->ier);

        g_string_append_printf(buf, "  Timers:\n");
        g_string_append_printf(buf, "    Using current time now(ns)=%"PRId64
                                    "\n", now);
        g_string_append_printf(buf, "    T1 freq(hz)=%"PRId64
                               " mode=%s"
                               " counter=0x%x"
                               " latch=0x%x\n"
                               "       load_time(ns)=%"PRId64
                               " next_irq_time(ns)=%"PRId64 "\n",
                               s->timers[0].frequency,
                               ((s->acr & T1MODE) == T1MODE_CONT) ? "continuous"
                                                                  : "one-shot",
                               t1counter,
                               s->timers[0].latch,
                               s->timers[0].load_time,
                               get_next_irq_time(s, &s->timers[0], now));
        g_string_append_printf(buf, "    T2 freq(hz)=%"PRId64
                               " mode=%s"
                               " counter=0x%x"
                               " latch=0x%x\n"
                               "       load_time(ns)=%"PRId64
                               " next_irq_time(ns)=%"PRId64 "\n",
                               s->timers[1].frequency,
                               "one-shot",
                               t2counter,
                               s->timers[1].latch,
                               s->timers[1].load_time,
                               get_next_irq_time(s, &s->timers[1], now));
    }

    return 0;
}

static HumanReadableText *qmp_x_query_via(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    object_child_foreach_recursive(object_get_root(),
                                   qmp_x_query_via_foreach, buf);

    return human_readable_text_from_str(buf);
}

void hmp_info_via(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    g_autoptr(HumanReadableText) info = qmp_x_query_via(&err);

    if (hmp_handle_error(mon, err)) {
        return;
    }
    monitor_puts(mon, info->human_readable_text);
}

static const MemoryRegionOps mos6522_ops = {
    .read = mos6522_read,
    .write = mos6522_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const VMStateDescription vmstate_mos6522_timer = {
    .name = "mos6522_timer",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(latch, MOS6522Timer),
        VMSTATE_UINT16(counter_value, MOS6522Timer),
        VMSTATE_INT64(load_time, MOS6522Timer),
        VMSTATE_INT64(next_irq_time, MOS6522Timer),
        VMSTATE_TIMER_PTR(timer, MOS6522Timer),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_mos6522 = {
    .name = "mos6522",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(a, MOS6522State),
        VMSTATE_UINT8(b, MOS6522State),
        VMSTATE_UINT8(dira, MOS6522State),
        VMSTATE_UINT8(dirb, MOS6522State),
        VMSTATE_UINT8(sr, MOS6522State),
        VMSTATE_UINT8(acr, MOS6522State),
        VMSTATE_UINT8(pcr, MOS6522State),
        VMSTATE_UINT8(ifr, MOS6522State),
        VMSTATE_UINT8(ier, MOS6522State),
        VMSTATE_UINT8(last_irq_levels, MOS6522State),
        VMSTATE_STRUCT_ARRAY(timers, MOS6522State, 2, 0,
                             vmstate_mos6522_timer, MOS6522Timer),
        VMSTATE_END_OF_LIST()
    }
};

static void mos6522_reset(DeviceState *dev)
{
    MOS6522State *s = MOS6522(dev);

    s->b = 0;
    s->a = 0;
    s->dirb = 0xff;
    s->dira = 0;
    s->sr = 0;
    s->acr = 0;
    s->pcr = 0;
    s->ifr = 0;
    s->ier = 0;
    /* s->ier = T1_INT | SR_INT; */

    s->timers[0].frequency = s->frequency;
    s->timers[0].latch = 0xffff;
    set_counter(s, &s->timers[0], 0xffff);
    timer_del(s->timers[0].timer);

    s->timers[1].frequency = s->frequency;
    s->timers[1].latch = 0xffff;
    timer_del(s->timers[1].timer);
}

static void mos6522_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MOS6522State *s = MOS6522(obj);
    int i;

    memory_region_init_io(&s->mem, obj, &mos6522_ops, s, "mos6522",
                          MOS6522_NUM_REGS);
    sysbus_init_mmio(sbd, &s->mem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->timers); i++) {
        s->timers[i].index = i;
    }

    s->timers[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, mos6522_timer1, s);
    s->timers[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, mos6522_timer2, s);

    qdev_init_gpio_in(DEVICE(obj), mos6522_set_irq, VIA_NUM_INTS);
}

static void mos6522_finalize(Object *obj)
{
    MOS6522State *s = MOS6522(obj);

    timer_free(s->timers[0].timer);
    timer_free(s->timers[1].timer);
}

static Property mos6522_properties[] = {
    DEFINE_PROP_UINT64("frequency", MOS6522State, frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void mos6522_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    dc->reset = mos6522_reset;
    dc->vmsd = &vmstate_mos6522;
    device_class_set_props(dc, mos6522_properties);
    mdc->portB_write = mos6522_portB_write;
    mdc->portA_write = mos6522_portA_write;
    mdc->get_timer1_counter_value = mos6522_get_counter_value;
    mdc->get_timer2_counter_value = mos6522_get_counter_value;
    mdc->get_timer1_load_time = mos6522_get_load_time;
    mdc->get_timer2_load_time = mos6522_get_load_time;
}

static const TypeInfo mos6522_type_info = {
    .name = TYPE_MOS6522,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MOS6522State),
    .instance_init = mos6522_init,
    .instance_finalize = mos6522_finalize,
    .abstract = true,
    .class_size = sizeof(MOS6522DeviceClass),
    .class_init = mos6522_class_init,
};

static void mos6522_register_types(void)
{
    type_register_static(&mos6522_type_info);
}

type_init(mos6522_register_types)
