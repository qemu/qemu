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
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

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

static uint64_t get_counter_value(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    if (ti->index == 0) {
        return mdc->get_timer1_counter_value(s, ti);
    } else {
        return mdc->get_timer2_counter_value(s, ti);
    }
}

static uint64_t get_load_time(MOS6522State *s, MOS6522Timer *ti)
{
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

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

static void mos6522_set_sr_int(MOS6522State *s)
{
    trace_mos6522_set_sr_int();
    s->ifr |= SR_INT;
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
        break;
    case VIA_REG_A:
        val = s->a;
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
    case VIA_REG_ANH:
        val = s->anh;
        break;
    }

    if (addr != VIA_REG_IFR || val != 0) {
        trace_mos6522_read(addr, val);
    }

    return val;
}

void mos6522_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MOS6522State *s = opaque;
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    trace_mos6522_write(addr, val);

    switch (addr) {
    case VIA_REG_B:
        s->b = (s->b & ~s->dirb) | (val & s->dirb);
        mdc->portB_write(s);
        break;
    case VIA_REG_A:
        s->a = (s->a & ~s->dira) | (val & s->dira);
        mdc->portA_write(s);
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
    case VIA_REG_ANH:
        s->anh = val;
        break;
    }
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
    .version_id = 0,
    .minimum_version_id = 0,
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
        VMSTATE_UINT8(anh, MOS6522State),
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
    s->anh = 0;

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

    memory_region_init_io(&s->mem, obj, &mos6522_ops, s, "mos6522", 0x10);
    sysbus_init_mmio(sbd, &s->mem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->timers); i++) {
        s->timers[i].index = i;
    }

    s->timers[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, mos6522_timer1, s);
    s->timers[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, mos6522_timer2, s);
}

static Property mos6522_properties[] = {
    DEFINE_PROP_UINT64("frequency", MOS6522State, frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void mos6522_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_CLASS(oc);

    dc->reset = mos6522_reset;
    dc->vmsd = &vmstate_mos6522;
    dc->props = mos6522_properties;
    mdc->parent_reset = dc->reset;
    mdc->set_sr_int = mos6522_set_sr_int;
    mdc->portB_write = mos6522_portB_write;
    mdc->portA_write = mos6522_portA_write;
    mdc->update_irq = mos6522_update_irq;
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
    .abstract = true,
    .class_size = sizeof(MOS6522DeviceClass),
    .class_init = mos6522_class_init,
};

static void mos6522_register_types(void)
{
    type_register_static(&mos6522_type_info);
}

type_init(mos6522_register_types)
