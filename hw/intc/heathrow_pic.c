/*
 * Heathrow PIC support (OldWorld PowerMac)
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/intc/heathrow_pic.h"
#include "hw/irq.h"
#include "trace.h"

static inline int heathrow_check_irq(HeathrowPICState *pic)
{
    return (pic->events | (pic->levels & pic->level_triggered)) & pic->mask;
}

/* update the CPU irq state */
static void heathrow_update_irq(HeathrowState *s)
{
    if (heathrow_check_irq(&s->pics[0]) ||
            heathrow_check_irq(&s->pics[1])) {
        qemu_irq_raise(s->irqs[0]);
    } else {
        qemu_irq_lower(s->irqs[0]);
    }
}

static void heathrow_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    HeathrowState *s = opaque;
    HeathrowPICState *pic;
    unsigned int n;

    n = ((addr & 0xfff) - 0x10) >> 4;
    trace_heathrow_write(addr, n, value);
    if (n >= 2)
        return;
    pic = &s->pics[n];
    switch(addr & 0xf) {
    case 0x04:
        pic->mask = value;
        heathrow_update_irq(s);
        break;
    case 0x08:
        /* do not reset level triggered IRQs */
        value &= ~pic->level_triggered;
        pic->events &= ~value;
        heathrow_update_irq(s);
        break;
    default:
        break;
    }
}

static uint64_t heathrow_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    HeathrowState *s = opaque;
    HeathrowPICState *pic;
    unsigned int n;
    uint32_t value;

    n = ((addr & 0xfff) - 0x10) >> 4;
    if (n >= 2) {
        value = 0;
    } else {
        pic = &s->pics[n];
        switch(addr & 0xf) {
        case 0x0:
            value = pic->events;
            break;
        case 0x4:
            value = pic->mask;
            break;
        case 0xc:
            value = pic->levels;
            break;
        default:
            value = 0;
            break;
        }
    }
    trace_heathrow_read(addr, n, value);
    return value;
}

static const MemoryRegionOps heathrow_ops = {
    .read = heathrow_read,
    .write = heathrow_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void heathrow_set_irq(void *opaque, int num, int level)
{
    HeathrowState *s = opaque;
    HeathrowPICState *pic;
    unsigned int irq_bit;
    int last_level;

    pic = &s->pics[1 - (num >> 5)];
    irq_bit = 1 << (num & 0x1f);
    last_level = (pic->levels & irq_bit) ? 1 : 0;

    if (level) {
        pic->events |= irq_bit & ~pic->level_triggered;
        pic->levels |= irq_bit;
    } else {
        pic->levels &= ~irq_bit;
    }

    if (last_level != level) {
        trace_heathrow_set_irq(num, level);
    }

    heathrow_update_irq(s);
}

static const VMStateDescription vmstate_heathrow_pic_one = {
    .name = "heathrow_pic_one",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(events, HeathrowPICState),
        VMSTATE_UINT32(mask, HeathrowPICState),
        VMSTATE_UINT32(levels, HeathrowPICState),
        VMSTATE_UINT32(level_triggered, HeathrowPICState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_heathrow = {
    .name = "heathrow_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(pics, HeathrowState, 2, 1,
                             vmstate_heathrow_pic_one, HeathrowPICState),
        VMSTATE_END_OF_LIST()
    }
};

static void heathrow_reset(DeviceState *d)
{
    HeathrowState *s = HEATHROW(d);

    s->pics[0].level_triggered = 0;
    s->pics[1].level_triggered = 0x1ff00000;
}

static void heathrow_init(Object *obj)
{
    HeathrowState *s = HEATHROW(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* only 1 CPU */
    qdev_init_gpio_out(DEVICE(obj), s->irqs, 1);

    qdev_init_gpio_in(DEVICE(obj), heathrow_set_irq, HEATHROW_NUM_IRQS);

    memory_region_init_io(&s->mem, OBJECT(s), &heathrow_ops, s,
                          "heathrow-pic", 0x1000);
    sysbus_init_mmio(sbd, &s->mem);
}

static void heathrow_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = heathrow_reset;
    dc->vmsd = &vmstate_heathrow;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo heathrow_type_info = {
    .name = TYPE_HEATHROW,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HeathrowState),
    .instance_init = heathrow_init,
    .class_init = heathrow_class_init,
};

static void heathrow_register_types(void)
{
    type_register_static(&heathrow_type_info);
}

type_init(heathrow_register_types)
