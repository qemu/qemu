/*
 * Ricoh RS5C372, R222x I2C RTC
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * Based on hw/rtc/ds1338.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "hw/resettable.h"
#include "migration/vmstate.h"
#include "qemu/bcd.h"
#include "qom/object.h"
#include "system/rtc.h"
#include "trace.h"

#define NVRAM_SIZE 0x10

/* Flags definitions */
#define SECONDS_CH 0x80
#define HOURS_PM   0x20
#define CTRL2_24   0x20

#define TYPE_RS5C372 "rs5c372"
OBJECT_DECLARE_SIMPLE_TYPE(RS5C372State, RS5C372)

struct RS5C372State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t nvram[NVRAM_SIZE];
    uint8_t ptr;
    uint8_t tx_format;
    bool addr_byte;
};

static void capture_current_time(RS5C372State *s)
{
    /*
     * Capture the current time into the secondary registers which will be
     * actually read by the data transfer operation.
     */
    struct tm now;
    qemu_get_timedate(&now, s->offset);
    s->nvram[0] = to_bcd(now.tm_sec);
    s->nvram[1] = to_bcd(now.tm_min);
    if (s->nvram[0xf] & CTRL2_24) {
        s->nvram[2] = to_bcd(now.tm_hour);
    } else {
        int tmp = now.tm_hour;
        if (tmp % 12 == 0) {
            tmp += 12;
        }
        if (tmp <= 12) {
            s->nvram[2] = to_bcd(tmp);
        } else {
            s->nvram[2] = HOURS_PM | to_bcd(tmp - 12);
        }
    }
    s->nvram[3] = (now.tm_wday + s->wday_offset) % 7 + 1;
    s->nvram[4] = to_bcd(now.tm_mday);
    s->nvram[5] = to_bcd(now.tm_mon + 1);
    s->nvram[6] = to_bcd(now.tm_year - 100);
}

static void inc_regptr(RS5C372State *s)
{
    s->ptr = (s->ptr + 1) & (NVRAM_SIZE - 1);
}

static int rs5c372_event(I2CSlave *i2c, enum i2c_event event)
{
    RS5C372State *s = RS5C372(i2c);

    switch (event) {
    case I2C_START_RECV:
        /*
         * In h/w, capture happens on any START condition, not just a
         * START_RECV, but there is no need to actually capture on
         * START_SEND, because the guest can't get at that data
         * without going through a START_RECV which would overwrite it.
         */
        capture_current_time(s);
        s->ptr = 0xf;
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t rs5c372_recv(I2CSlave *i2c)
{
    RS5C372State *s = RS5C372(i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];

    trace_rs5c372_recv(s->ptr, res);

    inc_regptr(s);
    return res;
}

static int rs5c372_send(I2CSlave *i2c, uint8_t data)
{
    RS5C372State *s = RS5C372(i2c);

    if (s->addr_byte) {
        s->ptr = data >> 4;
        s->tx_format = data & 0xf;
        s->addr_byte = false;
        return 0;
    }

    trace_rs5c372_send(s->ptr, data);

    if (s->ptr < 7) {
        /* Time register. */
        struct tm now;
        qemu_get_timedate(&now, s->offset);
        switch (s->ptr) {
        case 0:
            now.tm_sec = from_bcd(data & 0x7f);
            break;
        case 1:
            now.tm_min = from_bcd(data & 0x7f);
            break;
        case 2:
            if (s->nvram[0xf] & CTRL2_24) {
                now.tm_hour = from_bcd(data & 0x3f);
            } else {
                int tmp = from_bcd(data & (HOURS_PM - 1));
                if (data & HOURS_PM) {
                    tmp += 12;
                }
                if (tmp % 12 == 0) {
                    tmp -= 12;
                }
                now.tm_hour = tmp;
            }
            break;
        case 3:
            {
                /*
                 * The day field is supposed to contain a value in the range
                 * 1-7. Otherwise behavior is undefined.
                 */
                int user_wday = (data & 7) - 1;
                s->wday_offset = (user_wday - now.tm_wday + 7) % 7;
            }
            break;
        case 4:
            now.tm_mday = from_bcd(data & 0x3f);
            break;
        case 5:
            now.tm_mon = from_bcd(data & 0x1f) - 1;
            break;
        case 6:
            now.tm_year = from_bcd(data) + 100;
            break;
        }
        s->offset = qemu_timedate_diff(&now);
    } else {
        s->nvram[s->ptr] = data;
    }
    inc_regptr(s);
    return 0;
}

static void rs5c372_reset_hold(Object *obj, ResetType type)
{
    RS5C372State *s = RS5C372(obj);

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->nvram, 0, NVRAM_SIZE);
    s->ptr = 0;
    s->addr_byte = false;
}

static const VMStateDescription rs5c372_vmstate = {
    .name = "rs5c372",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, RS5C372State),
        VMSTATE_INT64(offset, RS5C372State),
        VMSTATE_UINT8_V(wday_offset, RS5C372State, 2),
        VMSTATE_UINT8_ARRAY(nvram, RS5C372State, NVRAM_SIZE),
        VMSTATE_UINT8(ptr, RS5C372State),
        VMSTATE_UINT8(tx_format, RS5C372State),
        VMSTATE_BOOL(addr_byte, RS5C372State),
        VMSTATE_END_OF_LIST()
    }
};

static void rs5c372_init(Object *obj)
{
    qdev_prop_set_uint8(DEVICE(obj), "address", 0x32);
}

static void rs5c372_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->event = rs5c372_event;
    k->recv = rs5c372_recv;
    k->send = rs5c372_send;
    dc->vmsd = &rs5c372_vmstate;
    rc->phases.hold = rs5c372_reset_hold;
}

static const TypeInfo rs5c372_types[] = {
    {
        .name          = TYPE_RS5C372,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(RS5C372State),
        .instance_init = rs5c372_init,
        .class_init    = rs5c372_class_init,
    },
};

DEFINE_TYPES(rs5c372_types)
