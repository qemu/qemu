/*
 * QEMU 8259 interrupt controller emulation
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

#include "qemu/osdep.h"
#include "hw/intc/i8259.h"
#include "hw/core/irq.h"
#include "hw/isa/isa.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "hw/isa/i8259_internal.h"
#include "trace.h"
#include "qom/object.h"

/*#define DEBUG_IRQ_LATENCY*/

#define TYPE_I8259 "isa-i8259"
OBJECT_DECLARE_TYPE(I8259State, I8259Class, I8259)

struct I8259State {
    I8259CommonState parent_obj;
};

/**
 * I8259Class:
 * @parent_realize: The parent's realizefn.
 */
struct I8259Class {
    I8259CommonClass parent_class;

    DeviceRealize parent_realize;
};

#ifdef DEBUG_IRQ_LATENCY
static int64_t irq_time[16];
#endif
I8259State *isa_pic;
static I8259CommonState *slave_pic;

/* return the highest priority found in mask (highest = smallest
   number). Return 8 if no irq */
static int get_priority(I8259CommonState *cs, int mask)
{
    int priority;

    if (mask == 0) {
        return 8;
    }
    priority = 0;
    while ((mask & (1 << ((priority + cs->priority_add) & 7))) == 0) {
        priority++;
    }
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(I8259CommonState *cs)
{
    int mask, cur_priority, priority;

    mask = cs->irr & ~cs->imr;
    priority = get_priority(cs, mask);
    if (priority == 8) {
        return -1;
    }
    /* compute current priority. If special fully nested mode on the
       master, the IRQ coming from the slave is not taken into account
       for the priority computation. */
    mask = cs->isr;
    if (cs->special_mask) {
        mask &= ~cs->imr;
    }
    if (cs->special_fully_nested_mode && cs->master) {
        mask &= ~(1 << 2);
    }
    cur_priority = get_priority(cs, mask);
    if (priority < cur_priority) {
        /* higher priority found: an irq should be generated */
        return (priority + cs->priority_add) & 7;
    } else {
        return -1;
    }
}

/* Update INT output. Must be called every time the output may have changed. */
static void pic_update_irq(I8259CommonState *cs)
{
    int irq;

    irq = pic_get_irq(cs);
    if (irq >= 0) {
        trace_pic_update_irq(cs->master, cs->imr, cs->irr, cs->priority_add);
        qemu_irq_raise(cs->int_out[0]);
    } else {
        qemu_irq_lower(cs->int_out[0]);
    }
}

/* set irq level. If an edge is detected, then the IRR is set to 1 */
static void pic_set_irq(void *opaque, int irq, int level)
{
    I8259CommonState *cs = opaque;
    int mask = 1 << irq;
    int irq_index = cs->master ? irq : irq + 8;

    trace_pic_set_irq(cs->master, irq, level);
    i8259_stat_update_irq(irq_index, level);

#ifdef DEBUG_IRQ_LATENCY
    if (level) {
        irq_time[irq_index] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
#endif

    if (cs->ltim || (cs->elcr & mask)) {
        /* level triggered */
        if (level) {
            cs->irr |= mask;
            cs->last_irr |= mask;
        } else {
            cs->irr &= ~mask;
            cs->last_irr &= ~mask;
        }
    } else {
        /* edge triggered */
        if (level) {
            if ((cs->last_irr & mask) == 0) {
                cs->irr |= mask;
            }
            cs->last_irr |= mask;
        } else {
            cs->last_irr &= ~mask;
        }
    }
    pic_update_irq(cs);
}

/* acknowledge interrupt 'irq' */
static void pic_intack(I8259CommonState *cs, int irq)
{
    if (cs->auto_eoi) {
        if (cs->rotate_on_auto_eoi) {
            cs->priority_add = (irq + 1) & 7;
        }
    } else {
        cs->isr |= (1 << irq);
    }
    /* We don't clear a level sensitive interrupt here */
    if (!cs->ltim && !(cs->elcr & (1 << irq))) {
        cs->irr &= ~(1 << irq);
    }
    pic_update_irq(cs);
}

int pic_read_irq(I8259State *s)
{
    I8259CommonState *cs = I8259_COMMON(s);
    int irq, intno;

    irq = pic_get_irq(cs);
    if (irq >= 0) {
        int irq2;

        if (irq == 2) {
            irq2 = pic_get_irq(slave_pic);
            if (irq2 >= 0) {
                pic_intack(slave_pic, irq2);
            } else {
                /* spurious IRQ on slave controller */
                irq2 = 7;
            }
            intno = slave_pic->irq_base + irq2;
            pic_intack(cs, irq);
            irq = irq2 + 8;
        } else {
            intno = cs->irq_base + irq;
            pic_intack(cs, irq);
        }
    } else {
        /* spurious IRQ on host controller */
        irq = 7;
        intno = cs->irq_base + irq;
    }

#ifdef DEBUG_IRQ_LATENCY
    printf("IRQ%d latency=%0.3fus\n",
           irq,
           (double)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                    irq_time[irq]) * 1000000.0 / NANOSECONDS_PER_SECOND);
#endif

    trace_pic_interrupt(irq, intno);
    return intno;
}

static void pic_init_reset(I8259CommonState *cs)
{
    i8259_common_reset(cs);
    pic_update_irq(cs);
}

static void pic_reset(DeviceState *dev)
{
    I8259CommonState *cs = I8259_COMMON(dev);

    cs->elcr = 0;
    cs->ltim = 0;
    pic_init_reset(cs);
}

static void pic_ioport_write(void *opaque, hwaddr addr64,
                             uint64_t val64, unsigned size)
{
    I8259CommonState *cs = opaque;
    uint32_t addr = addr64;
    uint32_t val = val64;
    int priority, cmd, irq;

    trace_pic_ioport_write(cs->master, addr, val);

    if (addr == 0) {
        if (val & 0x10) {
            pic_init_reset(cs);
            cs->init_state = 1;
            cs->init4 = val & 1;
            cs->single_mode = val & 2;
            cs->ltim = val & 8;
        } else if (val & 0x08) {
            if (val & 0x04) {
                cs->poll = 1;
            }
            if (val & 0x02) {
                cs->read_reg_select = val & 1;
            }
            if (val & 0x40) {
                cs->special_mask = (val >> 5) & 1;
            }
        } else {
            cmd = val >> 5;
            switch (cmd) {
            case 0:
            case 4:
                cs->rotate_on_auto_eoi = cmd >> 2;
                break;
            case 1: /* end of interrupt */
            case 5:
                priority = get_priority(cs, cs->isr);
                if (priority != 8) {
                    irq = (priority + cs->priority_add) & 7;
                    cs->isr &= ~(1 << irq);
                    if (cmd == 5) {
                        cs->priority_add = (irq + 1) & 7;
                    }
                    pic_update_irq(cs);
                }
                break;
            case 3:
                irq = val & 7;
                cs->isr &= ~(1 << irq);
                pic_update_irq(cs);
                break;
            case 6:
                cs->priority_add = (val + 1) & 7;
                pic_update_irq(cs);
                break;
            case 7:
                irq = val & 7;
                cs->isr &= ~(1 << irq);
                cs->priority_add = (irq + 1) & 7;
                pic_update_irq(cs);
                break;
            default:
                /* no operation */
                break;
            }
        }
    } else {
        switch (cs->init_state) {
        case 0:
            /* normal mode */
            cs->imr = val;
            pic_update_irq(cs);
            break;
        case 1:
            cs->irq_base = val & 0xf8;
            cs->init_state = cs->single_mode ? (cs->init4 ? 3 : 0) : 2;
            break;
        case 2:
            if (cs->init4) {
                cs->init_state = 3;
            } else {
                cs->init_state = 0;
            }
            break;
        case 3:
            cs->special_fully_nested_mode = (val >> 4) & 1;
            cs->auto_eoi = (val >> 1) & 1;
            cs->init_state = 0;
            break;
        }
    }
}

static uint64_t pic_ioport_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    I8259CommonState *cs = opaque;
    int ret;

    if (cs->poll) {
        ret = pic_get_irq(cs);
        if (ret >= 0) {
            pic_intack(cs, ret);
            ret |= 0x80;
        } else {
            ret = 0;
        }
        cs->poll = 0;
    } else {
        if (addr == 0) {
            if (cs->read_reg_select) {
                ret = cs->isr;
            } else {
                ret = cs->irr;
            }
        } else {
            ret = cs->imr;
        }
    }
    trace_pic_ioport_read(cs->master, addr, ret);
    return ret;
}

int pic_get_output(I8259State *s)
{
    I8259CommonState *cs = I8259_COMMON(s);

    return (pic_get_irq(cs) >= 0);
}

static void elcr_ioport_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    I8259CommonState *cs = opaque;
    cs->elcr = val & cs->elcr_mask;
}

static uint64_t elcr_ioport_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    I8259CommonState *cs = opaque;
    return cs->elcr;
}

static const MemoryRegionOps pic_base_ioport_ops = {
    .read = pic_ioport_read,
    .write = pic_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps pic_elcr_ioport_ops = {
    .read = elcr_ioport_read,
    .write = elcr_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pic_realize(DeviceState *dev, Error **errp)
{
    I8259CommonState *cs = I8259_COMMON(dev);
    I8259Class *k = I8259_GET_CLASS(dev);

    memory_region_init_io(&cs->base_io, OBJECT(cs), &pic_base_ioport_ops, cs,
                          "pic", 2);
    memory_region_init_io(&cs->elcr_io, OBJECT(cs), &pic_elcr_ioport_ops, cs,
                          "elcr", 1);

    qdev_init_gpio_out(dev, cs->int_out, ARRAY_SIZE(cs->int_out));
    qdev_init_gpio_in(dev, pic_set_irq, 8);

    k->parent_realize(dev, errp);
}

qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq_in)
{
    qemu_irq *irq_set;
    DeviceState *dev;
    ISADevice *isadev;
    int i;

    irq_set = g_new0(qemu_irq, ISA_NUM_IRQS);

    isadev = i8259_init_chip(TYPE_I8259, bus, true);
    dev = DEVICE(isadev);

    qdev_connect_gpio_out(dev, 0, parent_irq_in);
    for (i = 0 ; i < 8; i++) {
        irq_set[i] = qdev_get_gpio_in(dev, i);
    }

    isa_pic = I8259(dev);

    isadev = i8259_init_chip(TYPE_I8259, bus, false);
    dev = DEVICE(isadev);

    qdev_connect_gpio_out(dev, 0, irq_set[2]);
    for (i = 0 ; i < 8; i++) {
        irq_set[i + 8] = qdev_get_gpio_in(dev, i);
    }

    slave_pic = I8259_COMMON(dev);

    return irq_set;
}

static void i8259_class_init(ObjectClass *klass, const void *data)
{
    I8259Class *k = I8259_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_parent_realize(dc, pic_realize, &k->parent_realize);
    device_class_set_legacy_reset(dc, pic_reset);
}

static const TypeInfo i8259_info = {
    .name       = TYPE_I8259,
    .instance_size = sizeof(I8259State),
    .parent     = TYPE_I8259_COMMON,
    .class_init = i8259_class_init,
    .class_size = sizeof(I8259Class),
};

static void pic_register_types(void)
{
    type_register_static(&i8259_info);
}

type_init(pic_register_types)
