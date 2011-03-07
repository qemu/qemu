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

#include <assert.h>

#include "hw.h"
#include "pc.h"
#include "monitor.h"
#include "sysbus.h"
#include "trace.h"
#include "lm32_pic.h"

struct LM32PicState {
    SysBusDevice busdev;
    qemu_irq parent_irq;
    uint32_t im;        /* interrupt mask */
    uint32_t ip;        /* interrupt pending */
    uint32_t irq_state;

    /* statistics */
    uint32_t stats_irq_count[32];
};
typedef struct LM32PicState LM32PicState;

static LM32PicState *pic;
void pic_info(Monitor *mon)
{
    if (pic == NULL) {
        return;
    }

    monitor_printf(mon, "lm32-pic: im=%08x ip=%08x irq_state=%08x\n",
            pic->im, pic->ip, pic->irq_state);
}

void irq_info(Monitor *mon)
{
    int i;
    uint32_t count;

    if (pic == NULL) {
        return;
    }

    monitor_printf(mon, "IRQ statistics:\n");
    for (i = 0; i < 32; i++) {
        count = pic->stats_irq_count[i];
        if (count > 0) {
            monitor_printf(mon, "%2d: %u\n", i, count);
        }
    }
}

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
    LM32PicState *s = container_of(d, LM32PicState, busdev.qdev);

    trace_lm32_pic_set_im(im);
    s->im = im;

    update_irq(s);
}

void lm32_pic_set_ip(DeviceState *d, uint32_t ip)
{
    LM32PicState *s = container_of(d, LM32PicState, busdev.qdev);

    trace_lm32_pic_set_ip(ip);

    /* ack interrupt */
    s->ip &= ~ip;

    update_irq(s);
}

uint32_t lm32_pic_get_im(DeviceState *d)
{
    LM32PicState *s = container_of(d, LM32PicState, busdev.qdev);

    trace_lm32_pic_get_im(s->im);
    return s->im;
}

uint32_t lm32_pic_get_ip(DeviceState *d)
{
    LM32PicState *s = container_of(d, LM32PicState, busdev.qdev);

    trace_lm32_pic_get_ip(s->ip);
    return s->ip;
}

static void pic_reset(DeviceState *d)
{
    LM32PicState *s = container_of(d, LM32PicState, busdev.qdev);
    int i;

    s->im = 0;
    s->ip = 0;
    s->irq_state = 0;
    for (i = 0; i < 32; i++) {
        s->stats_irq_count[i] = 0;
    }
}

static int lm32_pic_init(SysBusDevice *dev)
{
    LM32PicState *s = FROM_SYSBUS(typeof(*s), dev);

    qdev_init_gpio_in(&dev->qdev, irq_handler, 32);
    sysbus_init_irq(dev, &s->parent_irq);

    pic = s;

    return 0;
}

static const VMStateDescription vmstate_lm32_pic = {
    .name = "lm32-pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(im, LM32PicState),
        VMSTATE_UINT32(ip, LM32PicState),
        VMSTATE_UINT32(irq_state, LM32PicState),
        VMSTATE_UINT32_ARRAY(stats_irq_count, LM32PicState, 32),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo lm32_pic_info = {
    .init = lm32_pic_init,
    .qdev.name  = "lm32-pic",
    .qdev.size  = sizeof(LM32PicState),
    .qdev.vmsd  = &vmstate_lm32_pic,
    .qdev.reset = pic_reset,
};

static void lm32_pic_register(void)
{
    sysbus_register_withprop(&lm32_pic_info);
}

device_init(lm32_pic_register)
