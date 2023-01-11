/*
 * Bit-Bang i2c emulation extracted from
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "trace.h"


/* bitbang_i2c_state enum to name */
static const char * const sname[] = {
#define NAME(e) [e] = stringify(e)
    NAME(STOPPED),
    [SENDING_BIT7] = "SENDING_BIT7 (START)",
    NAME(SENDING_BIT6),
    NAME(SENDING_BIT5),
    NAME(SENDING_BIT4),
    NAME(SENDING_BIT3),
    NAME(SENDING_BIT2),
    NAME(SENDING_BIT1),
    NAME(SENDING_BIT0),
    NAME(WAITING_FOR_ACK),
    [RECEIVING_BIT7] = "RECEIVING_BIT7 (ACK)",
    NAME(RECEIVING_BIT6),
    NAME(RECEIVING_BIT5),
    NAME(RECEIVING_BIT4),
    NAME(RECEIVING_BIT3),
    NAME(RECEIVING_BIT2),
    NAME(RECEIVING_BIT1),
    NAME(RECEIVING_BIT0),
    NAME(SENDING_ACK),
    NAME(SENT_NACK)
#undef NAME
};

static void bitbang_i2c_set_state(bitbang_i2c_interface *i2c,
                                  bitbang_i2c_state state)
{
    trace_bitbang_i2c_state(sname[i2c->state], sname[state]);
    i2c->state = state;
}

static void bitbang_i2c_enter_stop(bitbang_i2c_interface *i2c)
{
    if (i2c->current_addr >= 0)
        i2c_end_transfer(i2c->bus);
    i2c->current_addr = -1;
    bitbang_i2c_set_state(i2c, STOPPED);
}

/* Set device data pin.  */
static int bitbang_i2c_ret(bitbang_i2c_interface *i2c, int level)
{
    trace_bitbang_i2c_data(i2c->last_clock, i2c->last_data,
                           i2c->device_out, level);
    i2c->device_out = level;

    return level & i2c->last_data;
}

/* Leave device data pin unodified.  */
static int bitbang_i2c_nop(bitbang_i2c_interface *i2c)
{
    return bitbang_i2c_ret(i2c, i2c->device_out);
}

/* Returns data line level.  */
int bitbang_i2c_set(bitbang_i2c_interface *i2c, int line, int level)
{
    int data;

    if (level != 0 && level != 1) {
        abort();
    }

    if (line == BITBANG_I2C_SDA) {
        if (level == i2c->last_data) {
            return bitbang_i2c_nop(i2c);
        }
        i2c->last_data = level;
        if (i2c->last_clock == 0) {
            return bitbang_i2c_nop(i2c);
        }
        if (level == 0) {
            /* START condition.  */
            bitbang_i2c_set_state(i2c, SENDING_BIT7);
            i2c->current_addr = -1;
        } else {
            /* STOP condition.  */
            bitbang_i2c_enter_stop(i2c);
        }
        return bitbang_i2c_ret(i2c, 1);
    }

    data = i2c->last_data;
    if (i2c->last_clock == level) {
        return bitbang_i2c_nop(i2c);
    }
    i2c->last_clock = level;
    if (level == 0) {
        /* State is set/read at the start of the clock pulse.
           release the data line at the end.  */
        return bitbang_i2c_ret(i2c, 1);
    }
    switch (i2c->state) {
    case STOPPED:
    case SENT_NACK:
        return bitbang_i2c_ret(i2c, 1);

    case SENDING_BIT7 ... SENDING_BIT0:
        i2c->buffer = (i2c->buffer << 1) | data;
        /* will end up in WAITING_FOR_ACK */
        bitbang_i2c_set_state(i2c, i2c->state + 1);
        return bitbang_i2c_ret(i2c, 1);

    case WAITING_FOR_ACK:
    {
        int ret;

        if (i2c->current_addr < 0) {
            i2c->current_addr = i2c->buffer;
            trace_bitbang_i2c_addr(i2c->current_addr);
            ret = i2c_start_transfer(i2c->bus, i2c->current_addr >> 1,
                                     i2c->current_addr & 1);
        } else {
            trace_bitbang_i2c_send(i2c->buffer);
            ret = i2c_send(i2c->bus, i2c->buffer);
        }
        if (ret) {
            /* NACK (either addressing a nonexistent device, or the
             * device we were sending to decided to NACK us).
             */
            bitbang_i2c_set_state(i2c, SENT_NACK);
            bitbang_i2c_enter_stop(i2c);
            return bitbang_i2c_ret(i2c, 1);
        }
        if (i2c->current_addr & 1) {
            bitbang_i2c_set_state(i2c, RECEIVING_BIT7);
        } else {
            bitbang_i2c_set_state(i2c, SENDING_BIT7);
        }
        return bitbang_i2c_ret(i2c, 0);
    }
    case RECEIVING_BIT7:
        i2c->buffer = i2c_recv(i2c->bus);
        trace_bitbang_i2c_recv(i2c->buffer);
        /* Fall through... */
    case RECEIVING_BIT6 ... RECEIVING_BIT0:
        data = i2c->buffer >> 7;
        /* will end up in SENDING_ACK */
        bitbang_i2c_set_state(i2c, i2c->state + 1);
        i2c->buffer <<= 1;
        return bitbang_i2c_ret(i2c, data);

    case SENDING_ACK:
        if (data != 0) {
            bitbang_i2c_set_state(i2c, SENT_NACK);
            i2c_nack(i2c->bus);
        } else {
            bitbang_i2c_set_state(i2c, RECEIVING_BIT7);
        }
        return bitbang_i2c_ret(i2c, 1);
    }
    abort();
}

void bitbang_i2c_init(bitbang_i2c_interface *s, I2CBus *bus)
{
    s->bus = bus;
    s->last_data = 1;
    s->last_clock = 1;
    s->device_out = 1;
}

/* GPIO interface.  */

OBJECT_DECLARE_SIMPLE_TYPE(GPIOI2CState, GPIO_I2C)

struct GPIOI2CState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    bitbang_i2c_interface bitbang;
    int last_level;
    qemu_irq out;
};

static void bitbang_i2c_gpio_set(void *opaque, int irq, int level)
{
    GPIOI2CState *s = opaque;

    level = bitbang_i2c_set(&s->bitbang, irq, level);
    if (level != s->last_level) {
        s->last_level = level;
        qemu_set_irq(s->out, level);
    }
}

static void gpio_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    GPIOI2CState *s = GPIO_I2C(obj);
    I2CBus *bus;

    bus = i2c_init_bus(dev, "i2c");
    bitbang_i2c_init(&s->bitbang, bus);

    qdev_init_gpio_in(dev, bitbang_i2c_gpio_set, 2);
    qdev_init_gpio_out(dev, &s->out, 1);
}

static void gpio_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Virtual GPIO to I2C bridge";
}

static const TypeInfo gpio_i2c_info = {
    .name          = TYPE_GPIO_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GPIOI2CState),
    .instance_init = gpio_i2c_init,
    .class_init    = gpio_i2c_class_init,
};

static void bitbang_i2c_register_types(void)
{
    type_register_static(&gpio_i2c_info);
}

type_init(bitbang_i2c_register_types)
