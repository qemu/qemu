/*
 * Texas Instruments TMP421 temperature sensor.
 *
 * Copyright (c) 2016 IBM Corporation.
 *
 * Largely inspired by :
 *
 * Texas Instruments TMP105 temperature sensor.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
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
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qom/object.h"

/* Manufacturer / Device ID's */
#define TMP421_MANUFACTURER_ID          0x55
#define TMP421_DEVICE_ID                0x21
#define TMP422_DEVICE_ID                0x22
#define TMP423_DEVICE_ID                0x23

typedef struct DeviceInfo {
    int model;
    const char *name;
} DeviceInfo;

static const DeviceInfo devices[] = {
    { TMP421_DEVICE_ID, "tmp421" },
    { TMP422_DEVICE_ID, "tmp422" },
    { TMP423_DEVICE_ID, "tmp423" },
};

struct TMP421State {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    int16_t temperature[4];

    uint8_t status;
    uint8_t config[2];
    uint8_t rate;

    uint8_t len;
    uint8_t buf[2];
    uint8_t pointer;

};

struct TMP421Class {
    I2CSlaveClass parent_class;
    const DeviceInfo *dev;
};

#define TYPE_TMP421 "tmp421-generic"
OBJECT_DECLARE_TYPE(TMP421State, TMP421Class, TMP421)


/* the TMP421 registers */
#define TMP421_STATUS_REG               0x08
#define    TMP421_STATUS_BUSY             (1 << 7)
#define TMP421_CONFIG_REG_1             0x09
#define    TMP421_CONFIG_RANGE            (1 << 2)
#define    TMP421_CONFIG_SHUTDOWN         (1 << 6)
#define TMP421_CONFIG_REG_2             0x0A
#define    TMP421_CONFIG_RC               (1 << 2)
#define    TMP421_CONFIG_LEN              (1 << 3)
#define    TMP421_CONFIG_REN              (1 << 4)
#define    TMP421_CONFIG_REN2             (1 << 5)
#define    TMP421_CONFIG_REN3             (1 << 6)

#define TMP421_CONVERSION_RATE_REG      0x0B
#define TMP421_ONE_SHOT                 0x0F

#define TMP421_RESET                    0xFC
#define TMP421_MANUFACTURER_ID_REG      0xFE
#define TMP421_DEVICE_ID_REG            0xFF

#define TMP421_TEMP_MSB0                0x00
#define TMP421_TEMP_MSB1                0x01
#define TMP421_TEMP_MSB2                0x02
#define TMP421_TEMP_MSB3                0x03
#define TMP421_TEMP_LSB0                0x10
#define TMP421_TEMP_LSB1                0x11
#define TMP421_TEMP_LSB2                0x12
#define TMP421_TEMP_LSB3                0x13

static const int32_t mins[2] = { -40000, -55000 };
static const int32_t maxs[2] = { 127000, 150000 };

static void tmp421_get_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP421State *s = TMP421(obj);
    bool ext_range = (s->config[0] & TMP421_CONFIG_RANGE);
    int offset = ext_range * 64 * 256;
    int64_t value;
    int tempid;

    if (sscanf(name, "temperature%d", &tempid) != 1) {
        error_setg(errp, "error reading %s: %s", name, g_strerror(errno));
        return;
    }

    if (tempid >= 4 || tempid < 0) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    value = ((s->temperature[tempid] - offset) * 1000 + 128) / 256;

    visit_type_int(v, name, &value, errp);
}

/* Units are 0.001 centigrades relative to 0 C.  s->temperature is 8.8
 * fixed point, so units are 1/256 centigrades.  A simple ratio will do.
 */
static void tmp421_set_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP421State *s = TMP421(obj);
    int64_t temp;
    bool ext_range = (s->config[0] & TMP421_CONFIG_RANGE);
    int offset = ext_range * 64 * 256;
    int tempid;

    if (!visit_type_int(v, name, &temp, errp)) {
        return;
    }

    if (temp >= maxs[ext_range] || temp < mins[ext_range]) {
        error_setg(errp, "value %" PRId64 ".%03" PRIu64 " C is out of range",
                   temp / 1000, temp % 1000);
        return;
    }

    if (sscanf(name, "temperature%d", &tempid) != 1) {
        error_setg(errp, "error reading %s: %s", name, g_strerror(errno));
        return;
    }

    if (tempid >= 4 || tempid < 0) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    s->temperature[tempid] = (int16_t) ((temp * 256 - 128) / 1000) + offset;
}

static void tmp421_read(TMP421State *s)
{
    TMP421Class *sc = TMP421_GET_CLASS(s);

    s->len = 0;

    switch (s->pointer) {
    case TMP421_MANUFACTURER_ID_REG:
        s->buf[s->len++] = TMP421_MANUFACTURER_ID;
        break;
    case TMP421_DEVICE_ID_REG:
        s->buf[s->len++] = sc->dev->model;
        break;
    case TMP421_CONFIG_REG_1:
        s->buf[s->len++] = s->config[0];
        break;
    case TMP421_CONFIG_REG_2:
        s->buf[s->len++] = s->config[1];
        break;
    case TMP421_CONVERSION_RATE_REG:
        s->buf[s->len++] = s->rate;
        break;
    case TMP421_STATUS_REG:
        s->buf[s->len++] = s->status;
        break;

        /* FIXME: check for channel enablement in config registers */
    case TMP421_TEMP_MSB0:
        s->buf[s->len++] = (((uint16_t) s->temperature[0]) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature[0]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_MSB1:
        s->buf[s->len++] = (((uint16_t) s->temperature[1]) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature[1]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_MSB2:
        s->buf[s->len++] = (((uint16_t) s->temperature[2]) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature[2]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_MSB3:
        s->buf[s->len++] = (((uint16_t) s->temperature[3]) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature[3]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_LSB0:
        s->buf[s->len++] = (((uint16_t) s->temperature[0]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_LSB1:
        s->buf[s->len++] = (((uint16_t) s->temperature[1]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_LSB2:
        s->buf[s->len++] = (((uint16_t) s->temperature[2]) >> 0) & 0xf0;
        break;
    case TMP421_TEMP_LSB3:
        s->buf[s->len++] = (((uint16_t) s->temperature[3]) >> 0) & 0xf0;
        break;
    }
}

static void tmp421_reset(I2CSlave *i2c);

static void tmp421_write(TMP421State *s)
{
    switch (s->pointer) {
    case TMP421_CONVERSION_RATE_REG:
        s->rate = s->buf[0];
        break;
    case TMP421_CONFIG_REG_1:
        s->config[0] = s->buf[0];
        break;
    case TMP421_CONFIG_REG_2:
        s->config[1] = s->buf[0];
        break;
    case TMP421_RESET:
        tmp421_reset(I2C_SLAVE(s));
        break;
    }
}

static uint8_t tmp421_rx(I2CSlave *i2c)
{
    TMP421State *s = TMP421(i2c);

    if (s->len < 2) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

static int tmp421_tx(I2CSlave *i2c, uint8_t data)
{
    TMP421State *s = TMP421(i2c);

    if (s->len == 0) {
        /* first byte is the register pointer for a read or write
         * operation */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        /* second byte is the data to write. The device only supports
         * one byte writes */
        s->buf[0] = data;
        tmp421_write(s);
    }

    return 0;
}

static int tmp421_event(I2CSlave *i2c, enum i2c_event event)
{
    TMP421State *s = TMP421(i2c);

    if (event == I2C_START_RECV) {
        tmp421_read(s);
    }

    s->len = 0;
    return 0;
}

static const VMStateDescription vmstate_tmp421 = {
    .name = "TMP421",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(len, TMP421State),
        VMSTATE_UINT8_ARRAY(buf, TMP421State, 2),
        VMSTATE_UINT8(pointer, TMP421State),
        VMSTATE_UINT8_ARRAY(config, TMP421State, 2),
        VMSTATE_UINT8(status, TMP421State),
        VMSTATE_UINT8(rate, TMP421State),
        VMSTATE_INT16_ARRAY(temperature, TMP421State, 4),
        VMSTATE_I2C_SLAVE(i2c, TMP421State),
        VMSTATE_END_OF_LIST()
    }
};

static void tmp421_reset(I2CSlave *i2c)
{
    TMP421State *s = TMP421(i2c);
    TMP421Class *sc = TMP421_GET_CLASS(s);

    memset(s->temperature, 0, sizeof(s->temperature));
    s->pointer = 0;

    s->config[0] = 0; /* TMP421_CONFIG_RANGE */

     /* resistance correction and channel enablement */
    switch (sc->dev->model) {
    case TMP421_DEVICE_ID:
        s->config[1] = 0x1c;
        break;
    case TMP422_DEVICE_ID:
        s->config[1] = 0x3c;
        break;
    case TMP423_DEVICE_ID:
        s->config[1] = 0x7c;
        break;
    }

    s->rate = 0x7;       /* 8Hz */
    s->status = 0;
}

static void tmp421_realize(DeviceState *dev, Error **errp)
{
    TMP421State *s = TMP421(dev);

    tmp421_reset(&s->i2c);
}

static void tmp421_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    TMP421Class *sc = TMP421_CLASS(klass);

    dc->realize = tmp421_realize;
    k->event = tmp421_event;
    k->recv = tmp421_rx;
    k->send = tmp421_tx;
    dc->vmsd = &vmstate_tmp421;
    sc->dev = (DeviceInfo *) data;

    object_class_property_add(klass, "temperature0", "int",
                              tmp421_get_temperature,
                              tmp421_set_temperature, NULL, NULL);
    object_class_property_add(klass, "temperature1", "int",
                              tmp421_get_temperature,
                              tmp421_set_temperature, NULL, NULL);
    object_class_property_add(klass, "temperature2", "int",
                              tmp421_get_temperature,
                              tmp421_set_temperature, NULL, NULL);
    object_class_property_add(klass, "temperature3", "int",
                              tmp421_get_temperature,
                              tmp421_set_temperature, NULL, NULL);
}

static const TypeInfo tmp421_info = {
    .name          = TYPE_TMP421,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TMP421State),
    .class_size    = sizeof(TMP421Class),
    .abstract      = true,
};

static void tmp421_register_types(void)
{
    int i;

    type_register_static(&tmp421_info);
    for (i = 0; i < ARRAY_SIZE(devices); ++i) {
        TypeInfo ti = {
            .name       = devices[i].name,
            .parent     = TYPE_TMP421,
            .class_init = tmp421_class_init,
            .class_data = &devices[i],
        };
        type_register_static(&ti);
    }
}

type_init(tmp421_register_types)
