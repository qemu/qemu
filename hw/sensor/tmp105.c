/*
 * Texas Instruments TMP105/TMP75/TMP175/LM75B temperature sensor.
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
 *
 * Limitations:
 * - The SMBus Alert Response Address protocol and the general-call commands
 *   are not implemented.
 * - The over-limit comparison uses the full 8.8 fixed-point temperature and a
 *   ">=" boundary for every variant. The LM75B's 9-bit comparison quantisation
 *   and strict-exceed boundary are therefore only approximated.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-type-infos.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "hw/sensor/tmp105.h"
#include "hw/sensor/tmp105_regs.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "trace.h"

OBJECT_DECLARE_TYPE(TMP105State, TMP105Class, TMP105)

struct TMP105State {
    /*< private >*/
    I2CSlave parent_obj;
    /*< public >*/

    uint8_t len;
    uint8_t buf[2];
    qemu_irq pin;

    uint8_t pointer;
    uint8_t config;
    int16_t temperature;
    int16_t limit[2];
    uint8_t faults;
    uint8_t fault_count;
    uint8_t alarm;
    /*
     * The TMP105 initially looks for a temperature rising above T_high;
     * once this is detected, the condition it looks for next is the
     * temperature falling below T_low. This flag is false when initially
     * looking for T_high, true when looking for T_low.
     */
    bool detect_falling;
};

/*
 * Per-device-model parameters. The TMP105, TMP75, TMP175 and LM75B share the
 * same register map and control semantics; they differ only in a handful of
 * details captured here.
 *
 * @faultq: fault-queue length table selected by Config bits
 * @config_wmask: writable Config bits. The LM75B has no resolution (R1:R0) or
 * one-shot (OS) bits.
 * @fixed_res: converter resolution field pinned by the device, or -1 when it is
 * software-selectable.
 * @limit_lsb_mask: low-byte mask applied to the T_LOW/T_HIGH limit registers.
 */
struct TMP105Class {
    I2CSlaveClass parent_class;
    const uint8_t *faultq;
    uint8_t config_wmask;
    int8_t fixed_res;
    uint8_t limit_lsb_mask;
    bool tm_change_clears_alert;
};

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
    bool fault;

    if (FIELD_EX8(s->config, CONFIG, SHUTDOWN_MODE) && !one_shot) {
        return;
    }

    /*
     * A fault is a conversion that lies outside the limit currently being
     * watched: above T_high while looking for the alarm to trip, below T_low
     * afterwards.
     */
    if (s->detect_falling) {
        fault = s->temperature < s->limit[0];
    } else {
        fault = s->temperature >= s->limit[1];
    }

    if (!fault) {
        s->fault_count = 0;
    } else if (++s->fault_count >= s->faults) {
        s->fault_count = 0;
        if (s->detect_falling) {
            /*
             * Temperature fell back below T_low. In comparator mode (TM == 0)
             * the alarm is released; in interrupt mode (TM == 1) it is
             * asserted again and the guest is expected to clear it by reading
             * a register.
             */
            s->alarm = FIELD_EX8(s->config, CONFIG, THERMOSTAT_MODE);
            s->detect_falling = false;
        } else {
            /* Temperature rose to or above T_high: assert the alarm. */
            s->alarm = 1;
            s->detect_falling = true;
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

static void tmp105_read(TMP105State *s)
{
    const TMP105Class *tc = TMP105_GET_CLASS(s);
    int res;

    s->len = 0;

    if (FIELD_EX8(s->config, CONFIG, THERMOSTAT_MODE)) {
        s->alarm = 0;
        tmp105_interrupt_update(s);
    }

    switch (s->pointer & 3) {
    case TMP105_REG_TEMPERATURE:
        res = tc->fixed_res >= 0 ? tc->fixed_res :
              FIELD_EX8(s->config, CONFIG, CONVERTER_RESOLUTION);
        s->buf[s->len++] = (((uint16_t) s->temperature) >> 8);
        s->buf[s->len++] = (((uint16_t) s->temperature) >> 0) &
                (0xf0 << (3 - res));
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

    trace_tmp105_read(s->parent_obj.address, s->pointer);
}

static void tmp105_write(TMP105State *s)
{
    const TMP105Class *tc = TMP105_GET_CLASS(s);
    uint8_t config, one_shot;
    bool waking;

    trace_tmp105_write(s->parent_obj.address, s->pointer);

    switch (s->pointer & 3) {
    case TMP105_REG_TEMPERATURE:
        break;

    case TMP105_REG_CONFIG:
        config = s->buf[0] & tc->config_wmask;
        if (FIELD_EX8(config & ~s->config, CONFIG, SHUTDOWN_MODE)) {
            trace_tmp105_write_shutdown(s->parent_obj.address);
            if (FIELD_EX8(config, CONFIG, THERMOSTAT_MODE)) {
                s->alarm = 0;
            }
        }
        if (tc->tm_change_clears_alert &&
            FIELD_EX8(config ^ s->config, CONFIG, THERMOSTAT_MODE)) {
            s->alarm = 0;
            s->fault_count = 0;
            s->detect_falling = false;
        }
        waking = FIELD_EX8(s->config & ~config, CONFIG, SHUTDOWN_MODE);
        one_shot = FIELD_EX8(config, CONFIG, ONE_SHOT);
        s->config = FIELD_DP8(config, CONFIG, ONE_SHOT, 0);
        s->faults = tc->faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];
        if (one_shot && FIELD_EX8(s->config, CONFIG, SHUTDOWN_MODE)) {
            tmp105_alarm_update(s, true);
        } else if (waking) {
            tmp105_alarm_update(s, false);
        } else {
            tmp105_interrupt_update(s);
        }
        break;

    case TMP105_REG_T_LOW:
    case TMP105_REG_T_HIGH:
        if (s->len >= 3) {
            s->limit[s->pointer & 1] = (int16_t)
                    ((((uint16_t) s->buf[0]) << 8) |
                     (s->buf[1] & tc->limit_lsb_mask));
        }
        tmp105_interrupt_update(s);
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
    const TMP105Class *tc = TMP105_GET_CLASS(s);

    s->faults = tc->faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];

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

static bool fault_count_needed(void *opaque)
{
    const TMP105State *s = opaque;

    return s->fault_count != 0;
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

static const VMStateDescription vmstate_tmp105_fault_count = {
    .name = "TMP105/fault-count",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fault_count_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(fault_count, TMP105State),
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
        VMSTATE_I2C_SLAVE(parent_obj, TMP105State),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_tmp105_detect_falling,
        &vmstate_tmp105_fault_count,
        NULL
    }
};

static void tmp105_reset_hold(Object *obj, ResetType type)
{
    TMP105State *s = TMP105(obj);
    const TMP105Class *tc = TMP105_GET_CLASS(s);

    s->temperature = 0;
    s->pointer = 0;
    s->config = 0;
    s->faults = tc->faultq[FIELD_EX8(s->config, CONFIG, FAULT_QUEUE)];
    s->fault_count = 0;
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
}

static void tmp105_initfn(Object *obj)
{
    object_property_add(obj, "temperature", "int", &int_type_info,
                        tmp105_get_temperature,
                        tmp105_set_temperature, NULL, NULL);
    object_property_set_description(obj, "temperature",
                                    "Temperature, in millidegrees Celsius");
}

/*
 * Fault-queue length selected by Config F1:F0. Row 0 (1/2/4/6) is used by the
 * TMP105, TMP175 and LM75B; row 1 (1/2/3/4) by the TMP75.
 */
static const uint8_t tmp105_faultq[][4] = {
    { 1, 2, 4, 6 },
    { 1, 2, 3, 4 },
};

/* The F1:F0 field must never index past a fault-queue table row. */
QEMU_BUILD_BUG_ON((1 << R_CONFIG_FAULT_QUEUE_LENGTH) - 1 >=
                  ARRAY_SIZE(tmp105_faultq[0]));

static void tmp105_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    TMP105Class *tc = TMP105_CLASS(klass);

    dc->realize = tmp105_realize;
    k->event = tmp105_event;
    k->recv = tmp105_rx;
    k->send = tmp105_tx;
    rc->phases.hold = tmp105_reset_hold;
    dc->vmsd = &vmstate_tmp105;

    tc->faultq = tmp105_faultq[0];
    tc->config_wmask = 0xff;
    tc->fixed_res = -1;
    tc->limit_lsb_mask = 0xf0;
    tc->tm_change_clears_alert = false;
}

static void tmp175_class_init(ObjectClass *klass, const void *data)
{
    TMP105Class *tc = TMP105_CLASS(klass);

    tc->faultq = tmp105_faultq[0];
    tc->config_wmask = 0xff;
    tc->fixed_res = -1;
    tc->limit_lsb_mask = 0xf0;
    tc->tm_change_clears_alert = false;
}

static void tmp75_class_init(ObjectClass *klass, const void *data)
{
    TMP105Class *tc = TMP105_CLASS(klass);

    tc->faultq = tmp105_faultq[1];
    tc->config_wmask = 0xff;
    tc->fixed_res = -1;
    tc->limit_lsb_mask = 0xf0;
    tc->tm_change_clears_alert = true;
}

static void lm75b_class_init(ObjectClass *klass, const void *data)
{
    TMP105Class *tc = TMP105_CLASS(klass);

    tc->faultq = tmp105_faultq[0];
    tc->config_wmask = 0x1f;
    tc->fixed_res = 2;
    tc->limit_lsb_mask = 0x80;
    tc->tm_change_clears_alert = false;
}

static const TypeInfo tmp105_types[] = {
    {
        .name          = TYPE_TMP105,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(TMP105State),
        .class_size    = sizeof(TMP105Class),
        .instance_init = tmp105_initfn,
        .class_init    = tmp105_class_init,
    },
    {
        .name          = TYPE_TMP175,
        .parent        = TYPE_TMP105,
        .class_init    = tmp175_class_init,
    },
    {
        .name          = TYPE_TMP75,
        .parent        = TYPE_TMP105,
        .class_init    = tmp75_class_init,
    },
    {
        .name          = TYPE_LM75B,
        .parent        = TYPE_TMP105,
        .class_init    = lm75b_class_init,
    },
};

DEFINE_TYPES(tmp105_types)
