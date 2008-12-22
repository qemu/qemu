/*
 * MAX7310 8-port GPIO expansion chip.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This file is licensed under GNU GPL.
 */

#include "hw.h"
#include "i2c.h"

struct max7310_s {
    i2c_slave i2c;
    int i2c_command_byte;
    int len;

    uint8_t level;
    uint8_t direction;
    uint8_t polarity;
    uint8_t status;
    uint8_t command;
    qemu_irq handler[8];
    qemu_irq *gpio_in;
};

void max7310_reset(i2c_slave *i2c)
{
    struct max7310_s *s = (struct max7310_s *) i2c;
    s->level &= s->direction;
    s->direction = 0xff;
    s->polarity = 0xf0;
    s->status = 0x01;
    s->command = 0x00;
}

static int max7310_rx(i2c_slave *i2c)
{
    struct max7310_s *s = (struct max7310_s *) i2c;

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

static int max7310_tx(i2c_slave *i2c, uint8_t data)
{
    struct max7310_s *s = (struct max7310_s *) i2c;
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
            line = ffs(diff) - 1;
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

static void max7310_event(i2c_slave *i2c, enum i2c_event event)
{
    struct max7310_s *s = (struct max7310_s *) i2c;
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

static void max7310_save(QEMUFile *f, void *opaque)
{
    struct max7310_s *s = (struct max7310_s *) opaque;

    qemu_put_be32(f, s->i2c_command_byte);
    qemu_put_be32(f, s->len);

    qemu_put_8s(f, &s->level);
    qemu_put_8s(f, &s->direction);
    qemu_put_8s(f, &s->polarity);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->command);

    i2c_slave_save(f, &s->i2c);
}

static int max7310_load(QEMUFile *f, void *opaque, int version_id)
{
    struct max7310_s *s = (struct max7310_s *) opaque;

    s->i2c_command_byte = qemu_get_be32(f);
    s->len = qemu_get_be32(f);

    qemu_get_8s(f, &s->level);
    qemu_get_8s(f, &s->direction);
    qemu_get_8s(f, &s->polarity);
    qemu_get_8s(f, &s->status);
    qemu_get_8s(f, &s->command);

    i2c_slave_load(f, &s->i2c);
    return 0;
}

static void max7310_gpio_set(void *opaque, int line, int level)
{
    struct max7310_s *s = (struct max7310_s *) opaque;
    if (line >= ARRAY_SIZE(s->handler) || line  < 0)
        hw_error("bad GPIO line");

    if (level)
        s->level |= s->direction & (1 << line);
    else
        s->level &= ~(s->direction & (1 << line));
}

/* MAX7310 is SMBus-compatible (can be used with only SMBus protocols),
 * but also accepts sequences that are not SMBus so return an I2C device.  */
struct i2c_slave *max7310_init(i2c_bus *bus)
{
    struct max7310_s *s = (struct max7310_s *)
            i2c_slave_init(bus, 0, sizeof(struct max7310_s));
    s->i2c.event = max7310_event;
    s->i2c.recv = max7310_rx;
    s->i2c.send = max7310_tx;
    s->gpio_in = qemu_allocate_irqs(max7310_gpio_set, s,
                    ARRAY_SIZE(s->handler));

    max7310_reset(&s->i2c);

    register_savevm("max7310", -1, 0, max7310_save, max7310_load, s);

    return &s->i2c;
}

qemu_irq *max7310_gpio_in_get(i2c_slave *i2c)
{
    struct max7310_s *s = (struct max7310_s *) i2c;
    return s->gpio_in;
}

void max7310_gpio_out_set(i2c_slave *i2c, int line, qemu_irq handler)
{
    struct max7310_s *s = (struct max7310_s *) i2c;
    if (line >= ARRAY_SIZE(s->handler) || line  < 0)
        hw_error("bad GPIO line");

    s->handler[line] = handler;
}
