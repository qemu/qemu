/*
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
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/sensor/tmp105.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "hw/registerfields.h"
#include "trace.h"

FIELD(CONFIG, SHUTDOWN_MODE,        0, 1)
FIELD(CONFIG, THERMOSTAT_MODE,      1, 1)
FIELD(CONFIG, POLARITY,             2, 1)
FIELD(CONFIG, FAULT_QUEUE,          3, 2)
FIELD(CONFIG, CONVERTER_RESOLUTION, 5, 2)
FIELD(CONFIG, ONE_SHOT,             7, 1)

static void tmp105_interrupt_update(TMP105State *s)
{
    qemu_set_irq(s->pin, s->alarm ^ FIELD_EX8(~s->config, CONFIG, POLARITY));
}

static void tmp105_alarm_update(TMP105State *s, bool one_shot)
{
    if (FIELD_EX8(s->config, CONFIG, SHUTDOWN_MODE) && !one_shot) {
        return;
    }

    if (FIELD_EX8(s->config, CONFIG, THERMOSTAT_MODE)) {
        /*
         * TM == 1 : Interrupt mode. We signal Alert when the
         * temperature rises above T_high, and expect the guest to clear
         * it (eg by reading a device register).
         */
        if (s->detect_falling) {
            if (s->temperature < s->limit[0]) {
                s->alarm = 1;
                s->detect_falling = false;
            }
        } else {
            if (s->temperature >= s->limit[1]) {
                s->alarm = 1;
                s->detect_falling = true;
            }
        }
    } else {
        /*
         * TM == 0 : Comparator mode. We signal Alert when the temperature
         * rises above T_high, and stop signalling it when the temperature
         * falls below T_low.
         */
        if (s->detect_falling) {
            if (s->temperature < s->limit[0]) {
                s->alarm = 0;
                s->detect_falling = false;
            }
        } else {
            if (s->temperature >= s->limit[1]) {
                s->alarm = 1;
                s->detect_falling = true;
            }
        }
    }

    tmp105_interrupt_update(s);
}

static void tmp105_get_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP105State *s = TMP105(obj);
    int64_t value = s->temperature * 1000 / 256;

    visit_type_int(v, name, &value, errp);
}

/*
 * Units are 0.001 centigrades relative to 0 C.  s->temperature is 8.8
 * fixed point, so units are 1/256 centigrades.  A simple ratio will do.
 */
static void tmp105_set_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    TMP105State *s = TMP105(obj);
    int64_t temp;

    if (!visit_type_int(v, name, &temp, errp)) {
        return;
    }
    if (temp >= 128000 || temp < -128000) {
        error_setg(errp, "value %" PRId64 ".%03" PRIu64 " C is out of range",
                   temp / 1000, temp % 1000);
        return;
    }

    s->temperature = (int16_t) (temp * 256 / 1000);

    tmp105_alarm_update(s, false);
}

static const int tmp105_faultq[4] = { 1, 2, 4, 6 };

static void tmp105_read(TMP105State *s)
{
    s->len = 0;

    if (FIELD_EX8(s->config, CONFIG, THERMOSTAT_MODE)) {
        s->alarm = 0;
        tmp105_interrupt_update(s);
    }

    switch (s->pointer & 3) {
    case TMP105_REG_TEMPERATURE:
        s->buf[s->len++] = (((uint16_t) s->temperature) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature) >> 0) &
                (0xf0 << (FIELD_EX8(~s->config, CONFIG, CONVERTER_RESOLUTION)));
        break;

    case TMP105_REG_CONFIG:
        s->buf[s->len++] = s->config;
        break;

    case TMP105_REG_T_LOW:
        s->buf[s->len++] = ((uint16_t) s->limit[0]) >> 8;
        s->buf[s->len++] = ((uint16_t) s->limit[0]) >> 0;
        break;

    case TMP105_REG_T_HIGH:
        s->buf[s->len++] = ((uint16_t) s->limit[1]) >> 8;
        s->buf[s->len++] = ((uint16_t) s->limit[1]) >> 0;
        break;
    }

    trace_tmp105_read(s->i2c.address, s->pointer);
}

static void tmp105_write(TMP105State *s)
{
    trace_tmp105_write(s->i2c.address, s->pointer);

    switch (s->pointer & 3) {
    case TMP105_REG_TEMPERATURE:
        break;

    case TMP105_REG_CONFIG:
        if (FIELD_EX8(s->buf[0] & ~s->config, CONFIG, SHUTDOWN_MODE)) {
            trace_tmp105_write_shutdown(s->i2c.address);
        }
        s->config = FIELD_DP8(s->buf[0], CONFIG, ONE_SHOT, 0);
        s->faults = tmp105_faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];
        tmp105_alarm_update(s, FIELD_EX8(s->buf[0], CONFIG, ONE_SHOT));
        break;

    case TMP105_REG_T_LOW:
    case TMP105_REG_T_HIGH:
        if (s->len >= 3) {
            s->limit[s->pointer & 1] = (int16_t)
                    ((((uint16_t) s->buf[0]) << 8) | (s->buf[1] & 0xf0));
        }
        tmp105_alarm_update(s, false);
        break;
    }
}

static uint8_t tmp105_rx(I2CSlave *i2c)
{
    TMP105State *s = TMP105(i2c);

    if (s->len < 2) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

static int tmp105_tx(I2CSlave *i2c, uint8_t data)
{
    TMP105State *s = TMP105(i2c);

    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else {
        if (s->len <= 2) {
            s->buf[s->len - 1] = data;
        }
        s->len++;
        tmp105_write(s);
    }

    return 0;
}

static int tmp105_event(I2CSlave *i2c, enum i2c_event event)
{
    TMP105State *s = TMP105(i2c);

    if (event == I2C_START_RECV) {
        tmp105_read(s);
    }

    s->len = 0;
    return 0;
}

static int tmp105_post_load(void *opaque, int version_id)
{
    TMP105State *s = opaque;

    s->faults = tmp105_faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];

    tmp105_interrupt_update(s);
    return 0;
}

static bool detect_falling_needed(void *opaque)
{
    TMP105State *s = opaque;

    /*
     * We only need to migrate the detect_falling bool if it's set;
     * for migration from older machines we assume that it is false
     * (ie temperature is not out of range).
     */
    return s->detect_falling;
}

static const VMStateDescription vmstate_tmp105_detect_falling = {
    .name = "TMP105/detect-falling",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = detect_falling_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(detect_falling, TMP105State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tmp105 = {
    .name = "TMP105",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = tmp105_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(len, TMP105State),
        VMSTATE_UINT8_ARRAY(buf, TMP105State, 2),
        VMSTATE_UINT8(pointer, TMP105State),
        VMSTATE_UINT8(config, TMP105State),
        VMSTATE_INT16(temperature, TMP105State),
        VMSTATE_INT16_ARRAY(limit, TMP105State, 2),
        VMSTATE_UINT8(alarm, TMP105State),
        VMSTATE_I2C_SLAVE(i2c, TMP105State),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_tmp105_detect_falling,
        NULL
    }
};

static void tmp105_reset(I2CSlave *i2c)
{
    TMP105State *s = TMP105(i2c);

    s->temperature = 0;
    s->pointer = 0;
    s->config = 0;
    s->faults = tmp105_faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];
    s->alarm = 0;
    s->detect_falling = false;

    s->limit[0] = 0x4b00; /* T_LOW, 75 degrees C */
    s->limit[1] = 0x5000; /* T_HIGH, 80 degrees C */

    tmp105_interrupt_update(s);
}

static void tmp105_realize(DeviceState *dev, Error **errp)
{
    I2CSlave *i2c = I2C_SLAVE(dev);
    TMP105State *s = TMP105(i2c);

    qdev_init_gpio_out(&i2c->qdev, &s->pin, 1);

    tmp105_reset(&s->i2c);
}

static void tmp105_initfn(Object *obj)
{
    object_property_add(obj, "temperature", "int",
                        tmp105_get_temperature,
                        tmp105_set_temperature, NULL, NULL);
}

static void tmp105_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = tmp105_realize;
    k->event = tmp105_event;
    k->recv = tmp105_rx;
    k->send = tmp105_tx;
    dc->vmsd = &vmstate_tmp105;
}

static const TypeInfo tmp105_info = {
    .name          = TYPE_TMP105,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TMP105State),
    .instance_init = tmp105_initfn,
    .class_init    = tmp105_class_init,
};

static void tmp105_register_types(void)
{
    type_register_static(&tmp105_info);
}

type_init(tmp105_register_types)
