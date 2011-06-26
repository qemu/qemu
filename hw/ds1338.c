/*
 * MAXIM DS1338 I2C RTC+NVRAM
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 */

#include "i2c.h"

typedef struct {
    i2c_slave i2c;
    time_t offset;
    struct tm now;
    uint8_t nvram[56];
    int ptr;
    int addr_byte;
} DS1338State;

static void ds1338_event(i2c_slave *i2c, enum i2c_event event)
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

static int ds1338_recv(i2c_slave *i2c)
{
    DS1338State *s = FROM_I2C_SLAVE(DS1338State, i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];
    s->ptr = (s->ptr + 1) & 0xff;
    return res;
}

static int ds1338_send(i2c_slave *i2c, uint8_t data)
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

static int ds1338_init(i2c_slave *i2c)
{
    return 0;
}

static I2CSlaveInfo ds1338_info = {
    .qdev.name = "ds1338",
    .qdev.size = sizeof(DS1338State),
    .init = ds1338_init,
    .event = ds1338_event,
    .recv = ds1338_recv,
    .send = ds1338_send,
};

static void ds1338_register_devices(void)
{
    i2c_register_slave(&ds1338_info);
}

device_init(ds1338_register_devices)
