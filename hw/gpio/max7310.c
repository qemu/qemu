/*
 * MAX7310 8-port GPIO expansion chip.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This file is licensed under GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"

#define TYPE_MAX7310 "max7310"
#define MAX7310(obj) OBJECT_CHECK(MAX7310State, (obj), TYPE_MAX7310)

typedef struct MAX7310State {
    I2CSlave parent_obj;

    int i2c_command_byte;
    int len;

    uint8_t level;
    uint8_t direction;
    uint8_t polarity;
    uint8_t status;
    uint8_t command;
    qemu_irq handler[8];
    qemu_irq *gpio_in;
} MAX7310State;

static void max7310_reset(DeviceState *dev)
{
    MAX7310State *s = MAX7310(dev);

    s->level &= s->direction;
    s->direction = 0xff;
    s->polarity = 0xf0;
    s->status = 0x01;
    s->command = 0x00;
}

static int max7310_rx(I2CSlave *i2c)
{
    MAX7310State *s = MAX7310(i2c);

    switch (s->command) {
    case 0x00:	/* Input port */
        return s->level ^ s->polarity;
        break;

    case 0x01:	/* Output port */
        return s->level & ~s->direction;
        break;

    case 0x02:	/* Polarity inversion */
        return s->polarity;

    case 0x03:	/* Configuration */
        return s->direction;

    case 0x04:	/* Timeout */
        return s->status;
        break;

    case 0xff:	/* Reserved */
        return 0xff;

    default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __FUNCTION__, s->command);
#endif
        break;
    }
    return 0xff;
}

static int max7310_tx(I2CSlave *i2c, uint8_t data)
{
    MAX7310State *s = MAX7310(i2c);
    uint8_t diff;
    int line;

    if (s->len ++ > 1) {
#ifdef VERBOSE
        printf("%s: message too long (%i bytes)\n", __FUNCTION__, s->len);
#endif
        return 1;
    }

    if (s->i2c_command_byte) {
        s->command = data;
        s->i2c_command_byte = 0;
        return 0;
    }

    switch (s->command) {
    case 0x01:	/* Output port */
        for (diff = (data ^ s->level) & ~s->direction; diff;
                        diff &= ~(1 << line)) {
            line = ctz32(diff);
            if (s->handler[line])
                qemu_set_irq(s->handler[line], (data >> line) & 1);
        }
        s->level = (s->level & s->direction) | (data & ~s->direction);
        break;

    case 0x02:	/* Polarity inversion */
        s->polarity = data;
        break;

    case 0x03:	/* Configuration */
        s->level &= ~(s->direction ^ data);
        s->direction = data;
        break;

    case 0x04:	/* Timeout */
        s->status = data;
        break;

    case 0x00:	/* Input port - ignore writes */
	break;
    default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __FUNCTION__, s->command);
#endif
        return 1;
    }

    return 0;
}

static void max7310_event(I2CSlave *i2c, enum i2c_event event)
{
    MAX7310State *s = MAX7310(i2c);
    s->len = 0;

    switch (event) {
    case I2C_START_SEND:
        s->i2c_command_byte = 1;
        break;
    case I2C_FINISH:
#ifdef VERBOSE
        if (s->len == 1)
            printf("%s: message too short (%i bytes)\n", __FUNCTION__, s->len);
#endif
        break;
    default:
        break;
    }
}

static const VMStateDescription vmstate_max7310 = {
    .name = "max7310",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(i2c_command_byte, MAX7310State),
        VMSTATE_INT32(len, MAX7310State),
        VMSTATE_UINT8(level, MAX7310State),
        VMSTATE_UINT8(direction, MAX7310State),
        VMSTATE_UINT8(polarity, MAX7310State),
        VMSTATE_UINT8(status, MAX7310State),
        VMSTATE_UINT8(command, MAX7310State),
        VMSTATE_I2C_SLAVE(parent_obj, MAX7310State),
        VMSTATE_END_OF_LIST()
    }
};

static void max7310_gpio_set(void *opaque, int line, int level)
{
    MAX7310State *s = (MAX7310State *) opaque;
    if (line >= ARRAY_SIZE(s->handler) || line  < 0)
        hw_error("bad GPIO line");

    if (level)
        s->level |= s->direction & (1 << line);
    else
        s->level &= ~(s->direction & (1 << line));
}

/* MAX7310 is SMBus-compatible (can be used with only SMBus protocols),
 * but also accepts sequences that are not SMBus so return an I2C device.  */
static int max7310_init(I2CSlave *i2c)
{
    MAX7310State *s = MAX7310(i2c);

    qdev_init_gpio_in(&i2c->qdev, max7310_gpio_set, 8);
    qdev_init_gpio_out(&i2c->qdev, s->handler, 8);

    return 0;
}

static void max7310_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = max7310_init;
    k->event = max7310_event;
    k->recv = max7310_rx;
    k->send = max7310_tx;
    dc->reset = max7310_reset;
    dc->vmsd = &vmstate_max7310;
}

static const TypeInfo max7310_info = {
    .name          = TYPE_MAX7310,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MAX7310State),
    .class_init    = max7310_class_init,
};

static void max7310_register_types(void)
{
    type_register_static(&max7310_info);
}

type_init(max7310_register_types)
