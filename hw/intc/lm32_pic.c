/*
 *  LatticeMico32 CPU interrupt controller logic.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "monitor/monitor.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "hw/lm32/lm32_pic.h"
#include "hw/intc/intc.h"

#define TYPE_LM32_PIC "lm32-pic"
#define LM32_PIC(obj) OBJECT_CHECK(LM32PicState, (obj), TYPE_LM32_PIC)

struct LM32PicState {
    SysBusDevice parent_obj;

    qemu_irq parent_irq;
    uint32_t im;        /* interrupt mask */
    uint32_t ip;        /* interrupt pending */
    uint32_t irq_state;

    /* statistics */
    uint64_t stats_irq_count[32];
};
typedef struct LM32PicState LM32PicState;

static void update_irq(LM32PicState *s)
{
    s->ip |= s->irq_state;

    if (s->ip & s->im) {
        trace_lm32_pic_raise_irq();
        qemu_irq_raise(s->parent_irq);
    } else {
        trace_lm32_pic_lower_irq();
        qemu_irq_lower(s->parent_irq);
    }
}

static void irq_handler(void *opaque, int irq, int level)
{
    LM32PicState *s = opaque;

    assert(irq < 32);
    trace_lm32_pic_interrupt(irq, level);

    if (level) {
        s->irq_state |= (1 << irq);
        s->stats_irq_count[irq]++;
    } else {
        s->irq_state &= ~(1 << irq);
    }

    update_irq(s);
}

void lm32_pic_set_im(DeviceState *d, uint32_t im)
{
    LM32PicState *s = LM32_PIC(d);

    trace_lm32_pic_set_im(im);
    s->im = im;

    update_irq(s);
}

void lm32_pic_set_ip(DeviceState *d, uint32_t ip)
{
    LM32PicState *s = LM32_PIC(d);

    trace_lm32_pic_set_ip(ip);

    /* ack interrupt */
    s->ip &= ~ip;

    update_irq(s);
}

uint32_t lm32_pic_get_im(DeviceState *d)
{
    LM32PicState *s = LM32_PIC(d);

    trace_lm32_pic_get_im(s->im);
    return s->im;
}

uint32_t lm32_pic_get_ip(DeviceState *d)
{
    LM32PicState *s = LM32_PIC(d);

    trace_lm32_pic_get_ip(s->ip);
    return s->ip;
}

static void pic_reset(DeviceState *d)
{
    LM32PicState *s = LM32_PIC(d);
    int i;

    s->im = 0;
    s->ip = 0;
    s->irq_state = 0;
    for (i = 0; i < 32; i++) {
        s->stats_irq_count[i] = 0;
    }
}

static bool lm32_get_statistics(InterruptStatsProvider *obj,
                                uint64_t **irq_counts, unsigned int *nb_irqs)
{
    LM32PicState *s = LM32_PIC(obj);
    *irq_counts = s->stats_irq_count;
    *nb_irqs = ARRAY_SIZE(s->stats_irq_count);
    return true;
}

static void lm32_print_info(InterruptStatsProvider *obj, Monitor *mon)
{
    LM32PicState *s = LM32_PIC(obj);
    monitor_printf(mon, "lm32-pic: im=%08x ip=%08x irq_state=%08x\n",
            s->im, s->ip, s->irq_state);
}

static void lm32_pic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    LM32PicState *s = LM32_PIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, irq_handler, 32);
    sysbus_init_irq(sbd, &s->parent_irq);
}

static const VMStateDescription vmstate_lm32_pic = {
    .name = "lm32-pic",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(im, LM32PicState),
        VMSTATE_UINT32(ip, LM32PicState),
        VMSTATE_UINT32(irq_state, LM32PicState),
        VMSTATE_UINT64_ARRAY(stats_irq_count, LM32PicState, 32),
        VMSTATE_END_OF_LIST()
    }
};

static void lm32_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(klass);

    dc->reset = pic_reset;
    dc->vmsd = &vmstate_lm32_pic;
    ic->get_statistics = lm32_get_statistics;
    ic->print_info = lm32_print_info;
}

static const TypeInfo lm32_pic_info = {
    .name          = TYPE_LM32_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LM32PicState),
    .instance_init = lm32_pic_init,
    .class_init    = lm32_pic_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_INTERRUPT_STATS_PROVIDER },
        { }
    },
};

static void lm32_pic_register_types(void)
{
    type_register_static(&lm32_pic_info);
}

type_init(lm32_pic_register_types)
