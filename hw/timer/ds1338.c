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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "qemu/bcd.h"
#include "qemu/module.h"

/* Size of NVRAM including both the user-accessible area and the
 * secondary register area.
 */
#define NVRAM_SIZE 64

/* Flags definitions */
#define SECONDS_CH 0x80
#define HOURS_12   0x40
#define HOURS_PM   0x20
#define CTRL_OSF   0x20

#define TYPE_DS1338 "ds1338"
#define DS1338(obj) OBJECT_CHECK(DS1338State, (obj), TYPE_DS1338)

typedef struct DS1338State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t nvram[NVRAM_SIZE];
    int32_t ptr;
    bool addr_byte;
} DS1338State;

static const VMStateDescription vmstate_ds1338 = {
    .name = "ds1338",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DS1338State),
        VMSTATE_INT64(offset, DS1338State),
        VMSTATE_UINT8_V(wday_offset, DS1338State, 2),
        VMSTATE_UINT8_ARRAY(nvram, DS1338State, NVRAM_SIZE),
        VMSTATE_INT32(ptr, DS1338State),
        VMSTATE_BOOL(addr_byte, DS1338State),
        VMSTATE_END_OF_LIST()
    }
};

static void capture_current_time(DS1338State *s)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    struct tm now;
    qemu_get_timedate(&now, s->offset);
    s->nvram[0] = to_bcd(now.tm_sec);
    s->nvram[1] = to_bcd(now.tm_min);
    if (s->nvram[2] & HOURS_12) {
        int tmp = now.tm_hour;
        if (tmp % 12 == 0) {
            tmp += 12;
        }
        if (tmp <= 12) {
            s->nvram[2] = HOURS_12 | to_bcd(tmp);
        } else {
            s->nvram[2] = HOURS_12 | HOURS_PM | to_bcd(tmp - 12);
        }
    } else {
        s->nvram[2] = to_bcd(now.tm_hour);
    }
    s->nvram[3] = (now.tm_wday + s->wday_offset) % 7 + 1;
    s->nvram[4] = to_bcd(now.tm_mday);
    s->nvram[5] = to_bcd(now.tm_mon + 1);
    s->nvram[6] = to_bcd(now.tm_year - 100);
}

static void inc_regptr(DS1338State *s)
{
    /* The register pointer wraps around after 0x3F; wraparound
     * causes the current time/date to be retransferred into
     * the secondary registers.
     */
    s->ptr = (s->ptr + 1) & (NVRAM_SIZE - 1);
    if (!s->ptr) {
        capture_current_time(s);
    }
}

static int ds1338_event(I2CSlave *i2c, enum i2c_event event)
{
    DS1338State *s = DS1338(i2c);

    switch (event) {
    case I2C_START_RECV:
        /* In h/w, capture happens on any START condition, not just a
         * START_RECV, but there is no need to actually capture on
         * START_SEND, because the guest can't get at that data
         * without going through a START_RECV which would overwrite it.
         */
        capture_current_time(s);
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t ds1338_recv(I2CSlave *i2c)
{
    DS1338State *s = DS1338(i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];
    inc_regptr(s);
    return res;
}

static int ds1338_send(I2CSlave *i2c, uint8_t data)
{
    DS1338State *s = DS1338(i2c);

    if (s->addr_byte) {
        s->ptr = data & (NVRAM_SIZE - 1);
        s->addr_byte = false;
        return 0;
    }
    if (s->ptr < 7) {
        /* Time register. */
        struct tm now;
        qemu_get_timedate(&now, s->offset);
        switch(s->ptr) {
        case 0:
            /* TODO: Implement CH (stop) bit.  */
            now.tm_sec = from_bcd(data & 0x7f);
            break;
        case 1:
            now.tm_min = from_bcd(data & 0x7f);
            break;
        case 2:
            if (data & HOURS_12) {
                int tmp = from_bcd(data & (HOURS_PM - 1));
                if (data & HOURS_PM) {
                    tmp += 12;
                }
                if (tmp % 12 == 0) {
                    tmp -= 12;
                }
                now.tm_hour = tmp;
            } else {
                now.tm_hour = from_bcd(data & (HOURS_12 - 1));
            }
            break;
        case 3:
            {
                /* The day field is supposed to contain a value in
                   the range 1-7. Otherwise behavior is undefined.
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
    } else if (s->ptr == 7) {
        /* Control register. */

        /* Ensure bits 2, 3 and 6 will read back as zero. */
        data &= 0xB3;

        /* Attempting to write the OSF flag to logic 1 leaves the
           value unchanged. */
        data = (data & ~CTRL_OSF) | (data & s->nvram[s->ptr] & CTRL_OSF);

        s->nvram[s->ptr] = data;
    } else {
        s->nvram[s->ptr] = data;
    }
    inc_regptr(s);
    return 0;
}

static void ds1338_reset(DeviceState *dev)
{
    DS1338State *s = DS1338(dev);

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->nvram, 0, NVRAM_SIZE);
    s->ptr = 0;
    s->addr_byte = false;
}

static void ds1338_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = ds1338_event;
    k->recv = ds1338_recv;
    k->send = ds1338_send;
    dc->reset = ds1338_reset;
    dc->vmsd = &vmstate_ds1338;
}

static const TypeInfo ds1338_info = {
    .name          = TYPE_DS1338,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS1338State),
    .class_init    = ds1338_class_init,
};

static void ds1338_register_types(void)
{
    type_register_static(&ds1338_info);
}

type_init(ds1338_register_types)
