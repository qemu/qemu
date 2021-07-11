/*
 * SMSC EMC141X temperature sensor.
 *
 * Copyright (c) 2020 Bytedance Corporation
 * Written by John Wang <wangzhiqiang.bj@bytedance.com>
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
#include "hw/sensor/emc141x_regs.h"

#define SENSORS_COUNT_MAX    4

struct EMC141XState {
    I2CSlave parent_obj;
    struct {
        uint8_t raw_temp_min;
        uint8_t raw_temp_current;
        uint8_t raw_temp_max;
    } sensor[SENSORS_COUNT_MAX];
    uint8_t len;
    uint8_t data;
    uint8_t pointer;
};

struct EMC141XClass {
    I2CSlaveClass parent_class;
    uint8_t model;
    unsigned sensors_count;
};

#define TYPE_EMC141X "emc141x"
OBJECT_DECLARE_TYPE(EMC141XState, EMC141XClass, EMC141X)

static void emc141x_get_temperature(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    EMC141XState *s = EMC141X(obj);
    EMC141XClass *sc = EMC141X_GET_CLASS(s);
    int64_t value;
    unsigned tempid;

    if (sscanf(name, "temperature%u", &tempid) != 1) {
        error_setg(errp, "error reading %s: %s", name, g_strerror(errno));
        return;
    }

    if (tempid >= sc->sensors_count) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    value = s->sensor[tempid].raw_temp_current * 1000;

    visit_type_int(v, name, &value, errp);
}

static void emc141x_set_temperature(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    EMC141XState *s = EMC141X(obj);
    EMC141XClass *sc = EMC141X_GET_CLASS(s);
    int64_t temp;
    unsigned tempid;

    if (!visit_type_int(v, name, &temp, errp)) {
        return;
    }

    if (sscanf(name, "temperature%u", &tempid) != 1) {
        error_setg(errp, "error reading %s: %s", name, g_strerror(errno));
        return;
    }

    if (tempid >= sc->sensors_count) {
        error_setg(errp, "error reading %s", name);
        return;
    }

    s->sensor[tempid].raw_temp_current = temp / 1000;
}

static void emc141x_read(EMC141XState *s)
{
    EMC141XClass *sc = EMC141X_GET_CLASS(s);
    switch (s->pointer) {
    case EMC141X_DEVICE_ID:
        s->data = sc->model;
        break;
    case EMC141X_MANUFACTURER_ID:
        s->data = MANUFACTURER_ID;
        break;
    case EMC141X_REVISION:
        s->data = REVISION;
        break;
    case EMC141X_TEMP_HIGH0:
        s->data = s->sensor[0].raw_temp_current;
        break;
    case EMC141X_TEMP_HIGH1:
        s->data = s->sensor[1].raw_temp_current;
        break;
    case EMC141X_TEMP_HIGH2:
        s->data = s->sensor[2].raw_temp_current;
        break;
    case EMC141X_TEMP_HIGH3:
        s->data = s->sensor[3].raw_temp_current;
        break;
    case EMC141X_TEMP_MAX_HIGH0:
        s->data = s->sensor[0].raw_temp_max;
        break;
    case EMC141X_TEMP_MAX_HIGH1:
        s->data = s->sensor[1].raw_temp_max;
        break;
    case EMC141X_TEMP_MAX_HIGH2:
        s->data = s->sensor[2].raw_temp_max;
        break;
    case EMC141X_TEMP_MAX_HIGH3:
        s->data = s->sensor[3].raw_temp_max;
        break;
    case EMC141X_TEMP_MIN_HIGH0:
        s->data = s->sensor[0].raw_temp_min;
        break;
    case EMC141X_TEMP_MIN_HIGH1:
        s->data = s->sensor[1].raw_temp_min;
        break;
    case EMC141X_TEMP_MIN_HIGH2:
        s->data = s->sensor[2].raw_temp_min;
        break;
    case EMC141X_TEMP_MIN_HIGH3:
        s->data = s->sensor[3].raw_temp_min;
        break;
    default:
        s->data = 0;
    }
}

static void emc141x_write(EMC141XState *s)
{
    switch (s->pointer) {
    case EMC141X_TEMP_MAX_HIGH0:
        s->sensor[0].raw_temp_max = s->data;
        break;
    case EMC141X_TEMP_MAX_HIGH1:
        s->sensor[1].raw_temp_max = s->data;
        break;
    case EMC141X_TEMP_MAX_HIGH2:
        s->sensor[2].raw_temp_max = s->data;
        break;
    case EMC141X_TEMP_MAX_HIGH3:
        s->sensor[3].raw_temp_max = s->data;
        break;
    case EMC141X_TEMP_MIN_HIGH0:
        s->sensor[0].raw_temp_min = s->data;
        break;
    case EMC141X_TEMP_MIN_HIGH1:
        s->sensor[1].raw_temp_min = s->data;
        break;
    case EMC141X_TEMP_MIN_HIGH2:
        s->sensor[2].raw_temp_min = s->data;
        break;
    case EMC141X_TEMP_MIN_HIGH3:
        s->sensor[3].raw_temp_min = s->data;
        break;
    default:
        s->data = 0;
    }
}

static uint8_t emc141x_rx(I2CSlave *i2c)
{
    EMC141XState *s = EMC141X(i2c);

    if (s->len == 0) {
        s->len++;
        return s->data;
    } else {
        return 0xff;
    }
}

static int emc141x_tx(I2CSlave *i2c, uint8_t data)
{
    EMC141XState *s = EMC141X(i2c);

    if (s->len == 0) {
        /* first byte is the reg pointer */
        s->pointer = data;
        s->len++;
    } else if (s->len == 1) {
        s->data = data;
        emc141x_write(s);
    }

    return 0;
}

static int emc141x_event(I2CSlave *i2c, enum i2c_event event)
{
    EMC141XState *s = EMC141X(i2c);

    if (event == I2C_START_RECV) {
        emc141x_read(s);
    }

    s->len = 0;
    return 0;
}

static const VMStateDescription vmstate_emc141x = {
    .name = "EMC141X",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, EMC141XState),
        VMSTATE_UINT8(data, EMC141XState),
        VMSTATE_UINT8(pointer, EMC141XState),
        VMSTATE_I2C_SLAVE(parent_obj, EMC141XState),
        VMSTATE_END_OF_LIST()
    }
};

static void emc141x_reset(DeviceState *dev)
{
    EMC141XState *s = EMC141X(dev);
    int i;

    for (i = 0; i < SENSORS_COUNT_MAX; i++) {
        s->sensor[i].raw_temp_max = 0x55;
    }
    s->pointer = 0;
    s->len = 0;
}

static void emc141x_initfn(Object *obj)
{
    object_property_add(obj, "temperature0", "int",
                        emc141x_get_temperature,
                        emc141x_set_temperature, NULL, NULL);
    object_property_add(obj, "temperature1", "int",
                        emc141x_get_temperature,
                        emc141x_set_temperature, NULL, NULL);
    object_property_add(obj, "temperature2", "int",
                        emc141x_get_temperature,
                        emc141x_set_temperature, NULL, NULL);
    object_property_add(obj, "temperature3", "int",
                        emc141x_get_temperature,
                        emc141x_set_temperature, NULL, NULL);
}

static void emc141x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = emc141x_reset;
    k->event = emc141x_event;
    k->recv = emc141x_rx;
    k->send = emc141x_tx;
    dc->vmsd = &vmstate_emc141x;
}

static void emc1413_class_init(ObjectClass *klass, void *data)
{
    EMC141XClass *ec = EMC141X_CLASS(klass);

    emc141x_class_init(klass, data);
    ec->model = EMC1413_DEVICE_ID;
    ec->sensors_count = 3;
}

static void emc1414_class_init(ObjectClass *klass, void *data)
{
    EMC141XClass *ec = EMC141X_CLASS(klass);

    emc141x_class_init(klass, data);
    ec->model = EMC1414_DEVICE_ID;
    ec->sensors_count = 4;
}

static const TypeInfo emc141x_info = {
    .name          = TYPE_EMC141X,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(EMC141XState),
    .class_size    = sizeof(EMC141XClass),
    .instance_init = emc141x_initfn,
    .abstract      = true,
};

static const TypeInfo emc1413_info = {
    .name          = "emc1413",
    .parent        = TYPE_EMC141X,
    .class_init    = emc1413_class_init,
};

static const TypeInfo emc1414_info = {
    .name          = "emc1414",
    .parent        = TYPE_EMC141X,
    .class_init    = emc1414_class_init,
};

static void emc141x_register_types(void)
{
    type_register_static(&emc141x_info);
    type_register_static(&emc1413_info);
    type_register_static(&emc1414_info);
}

type_init(emc141x_register_types)
