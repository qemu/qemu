/*
 * M41T80 serial rtc emulation
 *
 * Copyright (c) 2018 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/bcd.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"
#include "system/rtc.h"

#define TYPE_M41T80 "m41t80"
OBJECT_DECLARE_SIMPLE_TYPE(M41t80State, M41T80)

struct M41t80State {
    I2CSlave parent_obj;
    int8_t addr;
};

static void m41t80_realize(DeviceState *dev, Error **errp)
{
    M41t80State *s = M41T80(dev);

    s->addr = -1;
}

static int m41t80_send(I2CSlave *i2c, uint8_t data)
{
    M41t80State *s = M41T80(i2c);

    if (s->addr < 0) {
        s->addr = data;
    } else {
        s->addr++;
    }
    return 0;
}

static uint8_t m41t80_recv(I2CSlave *i2c)
{
    M41t80State *s = M41T80(i2c);
    struct tm now;
    int64_t rt;

    if (s->addr < 0) {
        s->addr = 0;
    }
    if (s->addr >= 1 && s->addr <= 7) {
        qemu_get_timedate(&now, -1);
    }
    switch (s->addr++) {
    case 0:
        rt = g_get_real_time();
        return to_bcd((rt % G_USEC_PER_SEC) / 10000);
    case 1:
        return to_bcd(now.tm_sec);
    case 2:
        return to_bcd(now.tm_min);
    case 3:
        return to_bcd(now.tm_hour);
    case 4:
        return to_bcd(now.tm_wday);
    case 5:
        return to_bcd(now.tm_mday);
    case 6:
        return to_bcd(now.tm_mon + 1);
    case 7:
        return to_bcd(now.tm_year % 100);
    case 8 ... 19:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented register: %d\n",
                      __func__, s->addr - 1);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid register: %d\n",
                      __func__, s->addr - 1);
        return 0;
    }
}

static int m41t80_event(I2CSlave *i2c, enum i2c_event event)
{
    M41t80State *s = M41T80(i2c);

    if (event == I2C_START_SEND) {
        s->addr = -1;
    }
    return 0;
}

static void m41t80_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->realize = m41t80_realize;
    sc->send = m41t80_send;
    sc->recv = m41t80_recv;
    sc->event = m41t80_event;
}

static const TypeInfo m41t80_info = {
    .name          = TYPE_M41T80,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(M41t80State),
    .class_init    = m41t80_class_init,
};

static void m41t80_register_types(void)
{
    type_register_static(&m41t80_info);
}

type_init(m41t80_register_types)
