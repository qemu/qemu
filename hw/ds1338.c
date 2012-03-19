/*
 * MAXIM DS1338 I2C RTC+NVRAM
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "i2c.h"

typedef struct {
    I2CSlave i2c;
    time_t offset;
    struct tm now;
    uint8_t nvram[56];
    int ptr;
    int addr_byte;
} DS1338State;

static void ds1338_event(I2CSlave *i2c, enum i2c_event event)
{
    DS1338State *s = FROM_I2C_SLAVE(DS1338State, i2c);

    switch (event) {
    case I2C_START_RECV:
        qemu_get_timedate(&s->now, s->offset);
        s->nvram[0] = to_bcd(s->now.tm_sec);
        s->nvram[1] = to_bcd(s->now.tm_min);
        if (s->nvram[2] & 0x40) {
            s->nvram[2] = (to_bcd((s->now.tm_hour % 12)) + 1) | 0x40;
            if (s->now.tm_hour >= 12) {
                s->nvram[2] |= 0x20;
            }
        } else {
            s->nvram[2] = to_bcd(s->now.tm_hour);
        }
        s->nvram[3] = to_bcd(s->now.tm_wday) + 1;
        s->nvram[4] = to_bcd(s->now.tm_mday);
        s->nvram[5] = to_bcd(s->now.tm_mon) + 1;
        s->nvram[6] = to_bcd(s->now.tm_year - 100);
        break;
    case I2C_START_SEND:
        s->addr_byte = 1;
        break;
    default:
        break;
    }
}

static int ds1338_recv(I2CSlave *i2c)
{
    DS1338State *s = FROM_I2C_SLAVE(DS1338State, i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];
    s->ptr = (s->ptr + 1) & 0xff;
    return res;
}

static int ds1338_send(I2CSlave *i2c, uint8_t data)
{
    DS1338State *s = FROM_I2C_SLAVE(DS1338State, i2c);
    if (s->addr_byte) {
        s->ptr = data;
        s->addr_byte = 0;
        return 0;
    }
    s->nvram[s->ptr - 8] = data;
    if (data < 8) {
        qemu_get_timedate(&s->now, s->offset);
        switch(data) {
        case 0:
            /* TODO: Implement CH (stop) bit.  */
            s->now.tm_sec = from_bcd(data & 0x7f);
            break;
        case 1:
            s->now.tm_min = from_bcd(data & 0x7f);
            break;
        case 2:
            if (data & 0x40) {
                if (data & 0x20) {
                    data = from_bcd(data & 0x4f) + 11;
                } else {
                    data = from_bcd(data & 0x1f) - 1;
                }
            } else {
                data = from_bcd(data);
            }
            s->now.tm_hour = data;
            break;
        case 3:
            s->now.tm_wday = from_bcd(data & 7) - 1;
            break;
        case 4:
            s->now.tm_mday = from_bcd(data & 0x3f);
            break;
        case 5:
            s->now.tm_mon = from_bcd(data & 0x1f) - 1;
            break;
        case 6:
            s->now.tm_year = from_bcd(data) + 100;
            break;
        case 7:
            /* Control register. Currently ignored.  */
            break;
        }
        s->offset = qemu_timedate_diff(&s->now);
    }
    s->ptr = (s->ptr + 1) & 0xff;
    return 0;
}

static int ds1338_init(I2CSlave *i2c)
{
    return 0;
}

static void ds1338_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = ds1338_init;
    k->event = ds1338_event;
    k->recv = ds1338_recv;
    k->send = ds1338_send;
}

static TypeInfo ds1338_info = {
    .name          = "ds1338",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS1338State),
    .class_init    = ds1338_class_init,
};

static void ds1338_register_types(void)
{
    type_register_static(&ds1338_info);
}

type_init(ds1338_register_types)
