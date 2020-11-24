/*
 * MAXIM DS3231 I2C RTC
 *
 * Implementation is based from ds1338.c
 *
 * Copyright (c) 2020 Nanosonics Ltd.
 * Written by Nigel Po
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qemu/bcd.h"
#include "qemu/module.h"
#include "qemu/error-report.h"

/*
 * RTC register addresses
 */
typedef enum {
    SECONDS_REGISTER,
    MINUTES_REGISTER,
    HOURS_REGISTER,
    DAY_REGISTER,
    DATE_REGISTER,
    MONTH_REGISTER,
    YEAR_REGISTER,
    ALARM_1_SECONDS_REGISTER,
    ALARM_1_MINUTES_REGISTER,
    ALARM_1_HOURS_REGISTER,
    ALARM_1_DAY_DATE_REGISTER,
    ALARM_2_MINUTES_REGISTER,
    ALARM_2_HOUR_REGISTER,
    ALARM_2_DAY_DATE_REGISTER,
    CONTROL_REGISTER,
    STATUS_REGISTER,
    AGING_OFFSET_REGISTER,
    TEMPERATURE_MSB_REGISTER,
    TEMPERATURE_LSB_REGISTER,
    NUM_REGISTERS
} DS3231Registers;

/*
 * Register masks
 */
#define SECONDS_REG_MASK              0x7f    /* Seconds Bits           */
#define MINUTES_REG_MASK              0x7f    /* Minutes Bits           */
#define HOURS_REG_12HR_MASK           0x1f    /* 12Hr Bits              */
#define HOURS_REG_24HR_MASK           0x3f    /* 24Hr Bits              */
#define DAY_REG_MASK                  0x07    /* Day Bits               */
#define DATE_REG_MASK                 0x3f    /* Date Bits              */
#define MONTH_REG_MASK                0x1f    /* Month Bits             */
#define YEAR_REG_MASK                 0xff    /* Year Bits             */

/*
 * Hours register bits
 */
#define HR_REG_PM_BIT           0x20    /* AM (0) / PM (1) or 20Hours   */
#define HR_REG_12_BIT           0x40    /* 12 (1) or 24 (0) Hour Mode   */

/*
 * Offset
 */
#define DAY_OFFSET              1
#define MONTH_OFFSET            1
#define YEAR_OFFSET             100
#define DAYS_OF_A_WEEK          7
#define HOURS_12                12

#define TYPE_DS3231 "ds3231"
#define DS3231(obj) OBJECT_CHECK(DS3231State, (obj), TYPE_DS3231)

typedef struct DS3231State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t registers[NUM_REGISTERS];
    int32_t ptr;
    bool addr_byte;
} DS3231State;

static const VMStateDescription vmstate_ds3231 = {
    .name = TYPE_DS3231,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DS3231State),
        VMSTATE_INT64(offset, DS3231State),
        VMSTATE_UINT8_V(wday_offset, DS3231State, 2),
        VMSTATE_UINT8_ARRAY(registers, DS3231State, NUM_REGISTERS),
        VMSTATE_INT32(ptr, DS3231State),
        VMSTATE_BOOL(addr_byte, DS3231State),
        VMSTATE_END_OF_LIST()
    }
};

static void capture_current_time(DS3231State *s)
{
    struct tm now;
    qemu_get_timedate(&now, s->offset);

    s->registers[SECONDS_REGISTER] = to_bcd(now.tm_sec);
    s->registers[MINUTES_REGISTER] = to_bcd(now.tm_min);

    if (s->registers[HOURS_REGISTER] & HR_REG_12_BIT) {
        int tmp = now.tm_hour;
        if (tmp % HOURS_12 == 0) {
            tmp += HOURS_12;
        }
        if (tmp <= HOURS_12) {
            s->registers[HOURS_REGISTER] = HR_REG_12_BIT | to_bcd(tmp);
        } else {
            s->registers[HOURS_REGISTER] = HR_REG_12_BIT | HR_REG_PM_BIT | to_bcd(tmp - HOURS_12);
        }
    } else {
        s->registers[HOURS_REGISTER] = to_bcd(now.tm_hour);
    }

    s->registers[DAY_REGISTER] = (now.tm_wday + s->wday_offset) % DAYS_OF_A_WEEK + DAY_OFFSET;
    s->registers[DATE_REGISTER] = to_bcd(now.tm_mday);
    s->registers[MONTH_REGISTER] = to_bcd(now.tm_mon + MONTH_OFFSET);
    s->registers[YEAR_REGISTER] = to_bcd(now.tm_year - YEAR_OFFSET);
}

static void inc_regptr(DS3231State *s)
{
    s->ptr = (s->ptr + 1) % NUM_REGISTERS;
    if (!s->ptr) {
        capture_current_time(s);
    }
}

static int ds3231_event(I2CSlave *i2c, enum i2c_event event)
{
    DS3231State *s = DS3231(i2c);

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

static uint8_t ds3231_recv(I2CSlave *i2c)
{
    DS3231State *s = DS3231(i2c);
    uint8_t res;

    res  = s->registers[s->ptr];
    inc_regptr(s);
    return res;
}

static int ds3231_send(I2CSlave *i2c, uint8_t data)
{
    DS3231State *s = DS3231(i2c);

    if (s->addr_byte) {
        s->ptr = data;
        if (s->ptr >= NUM_REGISTERS) {
            s->ptr = 0;
            error_report("%s: Invalid register address (%d) received. Forcing to address %d.\n", __func__, data, s->ptr);
        }

        s->addr_byte = false;
        return 0;
    }

    if (s->ptr <= YEAR_REGISTER) {
        /* Time related registers */
        struct tm now;
        qemu_get_timedate(&now, s->offset);

        switch(s->ptr) {
        case SECONDS_REGISTER:
            now.tm_sec = from_bcd(data & SECONDS_REG_MASK);
            break;
        case MINUTES_REGISTER:
            now.tm_min = from_bcd(data & MINUTES_REG_MASK);
            break;
        case HOURS_REGISTER:
            if (data & HR_REG_12_BIT) {
                int tmp = from_bcd(data & HOURS_REG_12HR_MASK);
                if (data & HR_REG_PM_BIT) {
                    tmp += HOURS_12;
                }
                if (tmp % HOURS_12 == 0) {
                    tmp -= HOURS_12;
                }
                now.tm_hour = tmp;
                s->registers[HOURS_REGISTER] |= HR_REG_12_BIT;
            } else {
                now.tm_hour = from_bcd(data & HOURS_REG_24HR_MASK);
                s->registers[HOURS_REGISTER] &= ~HR_REG_12_BIT;
            }
            break;
        case DAY_REGISTER:
            {
                /* The day field is supposed to contain a value in
                   the range 1-7. Otherwise behavior is undefined.
                 */
                int user_wday = (data & DAY_REG_MASK) - DAY_OFFSET;
                s->wday_offset = (user_wday - now.tm_wday + DAYS_OF_A_WEEK) % DAYS_OF_A_WEEK;
            }
            break;
        case DATE_REGISTER:
            now.tm_mday = from_bcd(data & DATE_REG_MASK);
            break;
        case MONTH_REGISTER:
            now.tm_mon = from_bcd(data & MONTH_REG_MASK) - MONTH_OFFSET;
            break;
        case YEAR_REGISTER:
            now.tm_year = from_bcd(data) + YEAR_OFFSET;
            break;
        }

        s->offset = qemu_timedate_diff(&now);
    } else if (s->ptr == STATUS_REGISTER) {
        /* Ensure bits 7 (OSF Bit), 6, 5 and 4 will read back as zero.
           OSF Bit is not settable */
        s->registers[STATUS_REGISTER] = data & 0x0f;
    } else {
        s->registers[s->ptr] = data;
    }

    inc_regptr(s);
    return 0;
}

static void ds3231_reset(DeviceState *dev)
{
    DS3231State *s = DS3231(dev);

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->registers, 0, NUM_REGISTERS);
    s->ptr = 0;
    s->addr_byte = false;
}

static void ds3231_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = ds3231_event;
    k->recv = ds3231_recv;
    k->send = ds3231_send;
    dc->reset = ds3231_reset;
    dc->vmsd = &vmstate_ds3231;
}

static const TypeInfo ds3231_info = {
    .name          = TYPE_DS3231,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS3231State),
    .class_init    = ds3231_class_init,
};

static void ds3231_register_types(void)
{
    type_register_static(&ds3231_info);
}

type_init(ds3231_register_types)
