/*
 * GPIO key
 *
 * Copyright (c) 2016 Linaro Limited
 *
 * Author: Shannon Zhao <shannon.zhao@linaro.org>
 *
 * Emulate a (human) keypress -- when the key is triggered by
 * setting the incoming gpio line, the outbound irq line is
 * raised for 100ms before being dropped again.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_GPIOKEY "gpio-key"
#define GPIOKEY(obj) OBJECT_CHECK(GPIOKEYState, (obj), TYPE_GPIOKEY)
#define GPIO_KEY_LATENCY 100 /* 100ms */

typedef struct GPIOKEYState {
    SysBusDevice parent_obj;

    QEMUTimer *timer;
    qemu_irq irq;
} GPIOKEYState;

static const VMStateDescription vmstate_gpio_key = {
    .name = "gpio-key",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, GPIOKEYState),
        VMSTATE_END_OF_LIST()
    }
};

static void gpio_key_reset(DeviceState *dev)
{
    GPIOKEYState *s = GPIOKEY(dev);

    timer_del(s->timer);
}

static void gpio_key_timer_expired(void *opaque)
{
    GPIOKEYState *s = (GPIOKEYState *)opaque;

    qemu_set_irq(s->irq, 0);
    timer_del(s->timer);
}

static void gpio_key_set_irq(void *opaque, int irq, int level)
{
    GPIOKEYState *s = (GPIOKEYState *)opaque;

    qemu_set_irq(s->irq, 1);
    timer_mod(s->timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + GPIO_KEY_LATENCY);
}

static void gpio_key_realize(DeviceState *dev, Error **errp)
{
    GPIOKEYState *s = GPIOKEY(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(dev, gpio_key_set_irq, 1);
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, gpio_key_timer_expired, s);
}

static void gpio_key_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gpio_key_realize;
    dc->vmsd = &vmstate_gpio_key;
    dc->reset = &gpio_key_reset;
}

static const TypeInfo gpio_key_info = {
    .name          = TYPE_GPIOKEY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GPIOKEYState),
    .class_init    = gpio_key_class_init,
};

static void gpio_key_register_types(void)
{
    type_register_static(&gpio_key_info);
}

type_init(gpio_key_register_types)
