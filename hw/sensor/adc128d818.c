/*
 * Texas Instruments ADC128D818 12-bit 8-channel ADC with I2C interface
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "hw/sensor/adc128d818.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "trace.h"


/* Register addresses */
#define REG_CONFIG              0x00
#define REG_INT_STATUS          0x01
#define REG_INT_MASK            0x03
#define REG_CONV_RATE           0x07
#define REG_CH_DISABLE          0x08
#define REG_ONE_SHOT            0x09
#define REG_DEEP_SHUTDOWN       0x0a
#define REG_ADV_CONFIG          0x0b
#define REG_BUSY_STATUS         0x0c

/* Channel Reading Registers (16-bit, read-only) */
#define REG_CH_READING_BASE     0x20
#define REG_CH_READING_LAST     0x27

/* Limit Registers (8-bit, read/write) */
#define REG_LIMIT_BASE          0x2a
#define REG_LIMIT_LAST          0x39

/* ID Registers (read-only) */
#define REG_MANUFACTURER_ID     0x3e
#define REG_REVISION_ID         0x3f

/* Configuration Register (0x00) bitfields */
#define CONFIG_START            BIT(0)
#define CONFIG_INT_ENABLE       BIT(1)
#define CONFIG_INT_CLEAR        BIT(3)
#define CONFIG_INITIALIZATION   BIT(7)
#define CONFIG_WR_MASK \
    (CONFIG_START | CONFIG_INT_ENABLE | CONFIG_INT_CLEAR)

/* Advanced Configuration Register (0x0B) bitfields */
#define ADV_CONFIG_EXT_REF_EN   BIT(0)
#define ADV_CONFIG_MODE_SHIFT   1
#define ADV_CONFIG_MODE_MASK    (0x3 << ADV_CONFIG_MODE_SHIFT)
#define ADV_CONFIG_WR_MASK \
    (ADV_CONFIG_EXT_REF_EN | ADV_CONFIG_MODE_MASK)

/* Busy Status Register (0x0C) bitfields */
#define BUSY_STATUS_NOT_READY   BIT(1)

/* Conversion Rate Register (0x07) bitfields */
#define CONV_RATE_MASK          0x01

/* Deep Shutdown Register (0x0A) bitfields */
#define DEEP_SHUTDOWN_EN        0x01

/* Device constants */
#define ADC128D818_NUM_CHANNELS         8
#define ADC128D818_NUM_REGS             0x40

#define ADC128D818_INTERNAL_VREF_MV     2560
#define ADC128D818_MAX_VDD_MV           5500
#define ADC128D818_MANUFACTURER_ID_VAL  0x01
#define ADC128D818_REVISION_ID_VAL      0x09

/* ADC resolution */
#define ADC128D818_ADC_RESOLUTION       4096
#define ADC128D818_ADC_MAX              4095

/* Temperature: 0.5 deg C per LSb = 500 milli-degrees per LSb */
#define ADC128D818_TEMP_LSB_MC          500
#define ADC128D818_TEMP_RAW_MIN         (-256)
#define ADC128D818_TEMP_RAW_MAX         255


OBJECT_DECLARE_SIMPLE_TYPE(ADC128D818State, ADC128D818)

struct ADC128D818State {
    I2CSlave parent_obj;

    qemu_irq irq;

    uint8_t len;
    uint8_t pointer;
    uint8_t rx_byte;

    uint8_t regs[ADC128D818_NUM_REGS];
    uint16_t channel[ADC128D818_NUM_CHANNELS];

    int16_t ain[ADC128D818_NUM_CHANNELS]; /* mV */
    int32_t temperature; /* milli-degrees Celsius */
    uint16_t ext_vref; /* mV, 0 means not connected */
    bool temp_alarm; /* temperature high-limit alarm latched */

    char *description;
};

static uint16_t adc128d818_get_vref(const ADC128D818State *s)
{
    if (s->regs[REG_ADV_CONFIG] & ADV_CONFIG_EXT_REF_EN) {
        if (s->ext_vref > 0u) {
            return s->ext_vref;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: external VREF selected but not"
                      " connected, falling back to internal\n",
                      __func__, s->description);
    }

    return ADC128D818_INTERNAL_VREF_MV;
}

static uint8_t adc128d818_get_mode(const ADC128D818State *s)
{
    return (s->regs[REG_ADV_CONFIG] & ADV_CONFIG_MODE_MASK) >>
           ADV_CONFIG_MODE_SHIFT;
}

static bool adc128d818_is_temp_channel(const ADC128D818State *s, unsigned ch)
{
    if (ch != 7u) {
        return false;
    }

    return adc128d818_get_mode(s) != 1u;
}

static bool adc128d818_is_reserved_channel(const ADC128D818State *s,
                                           unsigned ch)
{
    switch (adc128d818_get_mode(s)) {
    case 2u:
        return ch >= 4u && ch <= 6u;
    case 3u:
        return ch == 6u;
    default:
        return false;
    }
}

static int16_t adc128d818_channel_voltage(const ADC128D818State *s, unsigned ch)
{
    switch (adc128d818_get_mode(s)) {
    case 2u:
        switch (ch) {
        case 0u:
            return (int16_t)(s->ain[0] - s->ain[1]);
        case 1u:
            return (int16_t)(s->ain[3] - s->ain[2]);
        case 2u:
            return (int16_t)(s->ain[4] - s->ain[5]);
        case 3u:
            return (int16_t)(s->ain[7] - s->ain[6]);
        default:
            return 0;
        }
    case 3u:
        switch (ch) {
        case 4u:
            return (int16_t)(s->ain[4] - s->ain[5]);
        case 5u:
            return (int16_t)(s->ain[7] - s->ain[6]);
        default:
            return s->ain[ch];
        }
    default:
        return s->ain[ch];
    }
}

static void adc128d818_update_irq(ADC128D818State *s)
{
    uint8_t cfg = s->regs[REG_CONFIG];
    uint8_t active;
    bool level;

    active = s->regs[REG_INT_STATUS] & ~s->regs[REG_INT_MASK];

    /* INT pin is active-low */
    level = !((cfg & CONFIG_INT_ENABLE) && !(cfg & CONFIG_INT_CLEAR) &&
              (active != 0u));

    trace_adc128d818_irq(s->description, level);
    qemu_set_irq(s->irq, level);
}

static bool adc128d818_monitoring_active(const ADC128D818State *s)
{
    if (s->regs[REG_DEEP_SHUTDOWN] & DEEP_SHUTDOWN_EN) {
        return false;
    }
    if (!(s->regs[REG_CONFIG] & CONFIG_START)) {
        return false;
    }
    if (s->regs[REG_CONFIG] & CONFIG_INT_CLEAR) {
        return false;
    }

    return true;
}

static void adc128d818_check_limits(ADC128D818State *s)
{
    uint8_t disabled = s->regs[REG_CH_DISABLE];
    uint8_t int_status = 0u;

    for (unsigned ch = 0u; ch < ADC128D818_NUM_CHANNELS; ch++) {
        if ((disabled & (1u << ch)) ||
            adc128d818_is_reserved_channel(s, ch)) {
            continue;
        }

        if (adc128d818_is_temp_channel(s, ch)) {
            int raw = s->temperature / ADC128D818_TEMP_LSB_MC;
            int thot;
            int thyst;

            raw = MAX(ADC128D818_TEMP_RAW_MIN,
                      MIN(ADC128D818_TEMP_RAW_MAX, raw));
            thot = (int)(int8_t)s->regs[REG_LIMIT_BASE + ch * 2u] * 2;
            thyst = (int)(int8_t)s->regs[REG_LIMIT_BASE + ch * 2u + 1u] * 2;

            if (raw > thot) {
                s->temp_alarm = true;
            } else if (raw <= thyst) {
                s->temp_alarm = false;
            }
            if (s->temp_alarm) {
                int_status |= (1u << ch);
            }
        } else {
            uint8_t msb = (uint8_t)(s->channel[ch] >> 8u);
            uint8_t high_lim = s->regs[REG_LIMIT_BASE + ch * 2u];
            uint8_t low_lim = s->regs[REG_LIMIT_BASE + ch * 2u + 1u];

            if (msb > high_lim || msb <= low_lim) {
                int_status |= (1u << ch);
            }
        }
    }

    s->regs[REG_INT_STATUS] = int_status;
    adc128d818_update_irq(s);
}

static void adc128d818_convert(ADC128D818State *s)
{
    uint8_t disabled;
    uint16_t vref;

    disabled = s->regs[REG_CH_DISABLE];
    vref = adc128d818_get_vref(s);

    for (unsigned ch = 0u; ch < ADC128D818_NUM_CHANNELS; ch++) {
        if ((disabled & (1u << ch)) ||
            adc128d818_is_reserved_channel(s, ch)) {
            continue;
        }

        if (adc128d818_is_temp_channel(s, ch)) {
            int32_t raw = s->temperature / ADC128D818_TEMP_LSB_MC;

            raw =
                MAX(ADC128D818_TEMP_RAW_MIN, MIN(ADC128D818_TEMP_RAW_MAX, raw));
            s->channel[ch] = (uint16_t)(((unsigned)raw & 0x1FFu) << 7u);
        } else {
            int16_t vin = adc128d818_channel_voltage(s, ch);
            int32_t dout;

            dout = vin * (int32_t)ADC128D818_ADC_RESOLUTION / vref;
            dout = MAX(0, MIN((int32_t)ADC128D818_ADC_MAX, dout));
            s->channel[ch] = (uint16_t)(dout << 4u);
        }

        trace_adc128d818_convert(s->description, ch, s->channel[ch]);
    }

    s->regs[REG_BUSY_STATUS] &= ~BUSY_STATUS_NOT_READY;

    adc128d818_check_limits(s);
}

static uint8_t adc128d818_read_channel(ADC128D818State *s, unsigned ch)
{
    uint8_t val;

    if (s->rx_byte == 0u) {
        val = (uint8_t)(s->channel[ch] >> 8u);
        trace_adc128d818_read_channel(s->description, ch, s->channel[ch]);
    } else {
        val = (uint8_t)(s->channel[ch] & 0xFFu);
    }
    s->rx_byte ^= 1u;

    return val;
}

static uint8_t adc128d818_read_reg(ADC128D818State *s, uint8_t reg)
{
    uint8_t val;

    switch (reg) {
    case REG_INT_STATUS:
        val = s->regs[REG_INT_STATUS];
        s->regs[REG_INT_STATUS] = 0x00u;
        if (adc128d818_monitoring_active(s)) {
            adc128d818_check_limits(s);
        } else {
            adc128d818_update_irq(s);
        }
        trace_adc128d818_read(s->description, reg, val);
        return val;
    case REG_CONFIG:
    case REG_INT_MASK:
    case REG_CONV_RATE:
    case REG_CH_DISABLE:
    case REG_ONE_SHOT:
    case REG_DEEP_SHUTDOWN:
    case REG_ADV_CONFIG:
    case REG_BUSY_STATUS:
    case REG_LIMIT_BASE ... REG_LIMIT_LAST:
    case REG_MANUFACTURER_ID:
    case REG_REVISION_ID:
        trace_adc128d818_read(s->description, reg, s->regs[reg]);
        return s->regs[reg];
    case REG_CH_READING_BASE ... REG_CH_READING_LAST:
        return adc128d818_read_channel(s, reg - REG_CH_READING_BASE);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: read from undefined register 0x%02x\n",
                      __func__, s->description, reg);
        return 0x00u;
    }
}

static void adc128d818_write_reg(ADC128D818State *s, uint8_t reg, uint8_t val);

static void adc128d818_reset_regs(ADC128D818State *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->channel, 0, sizeof(s->channel));
    s->temp_alarm = false;

    s->regs[REG_CONFIG] = 0x08u;
    s->regs[REG_BUSY_STATUS] = 0x02u;
    s->regs[REG_MANUFACTURER_ID] = ADC128D818_MANUFACTURER_ID_VAL;
    s->regs[REG_REVISION_ID] = ADC128D818_REVISION_ID_VAL;

    for (unsigned ch = 0u; ch < ADC128D818_NUM_CHANNELS; ch++) {
        s->regs[REG_LIMIT_BASE + ch * 2u] = 0xFFu;
    }

    s->pointer = 0x00u;
    s->len = 0u;
    s->rx_byte = 0u;

    adc128d818_update_irq(s);
}

static void adc128d818_write_reg(ADC128D818State *s, uint8_t reg, uint8_t val)
{
    trace_adc128d818_write(s->description, reg, val);

    switch (reg) {
    case REG_CONFIG:
        if (val & CONFIG_INITIALIZATION) {
            trace_adc128d818_reset(s->description, "reg");
            adc128d818_reset_regs(s);
            break;
        }
        s->regs[REG_CONFIG] = val & CONFIG_WR_MASK;
        if ((val & CONFIG_START) && !(val & CONFIG_INT_CLEAR) &&
            !(s->regs[REG_DEEP_SHUTDOWN] & DEEP_SHUTDOWN_EN)) {
            adc128d818_convert(s);
        }
        adc128d818_update_irq(s);
        break;
    case REG_INT_MASK:
        s->regs[REG_INT_MASK] = val;
        adc128d818_update_irq(s);
        break;
    case REG_CONV_RATE:
        if (s->regs[REG_CONFIG] & CONFIG_START) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: %s: CONV_RATE written while running\n",
                          __func__, s->description);
            break;
        }
        s->regs[REG_CONV_RATE] = val & CONV_RATE_MASK;
        break;
    case REG_CH_DISABLE:
        if (s->regs[REG_CONFIG] & CONFIG_START) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: %s: CH_DISABLE written while running\n",
                          __func__, s->description);
            break;
        }
        s->regs[REG_CH_DISABLE] = val;
        memset(s->channel, 0, sizeof(s->channel));
        s->regs[REG_INT_STATUS] = 0x00u;
        s->temp_alarm = false;
        adc128d818_update_irq(s);
        break;
    case REG_ONE_SHOT:
        if (!(s->regs[REG_CONFIG] & CONFIG_START)) {
            adc128d818_convert(s);
        }
        break;
    case REG_DEEP_SHUTDOWN:
        if ((val & DEEP_SHUTDOWN_EN) && (s->regs[REG_CONFIG] & CONFIG_START)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: %s: DEEP_SHUTDOWN set while running\n",
                          __func__, s->description);
            break;
        }
        s->regs[REG_DEEP_SHUTDOWN] = val & DEEP_SHUTDOWN_EN;
        break;
    case REG_ADV_CONFIG:
        if (s->regs[REG_CONFIG] & CONFIG_START) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: %s: ADV_CONFIG written while running\n",
                          __func__, s->description);
            break;
        }
        s->regs[REG_ADV_CONFIG] = val & ADV_CONFIG_WR_MASK;
        memset(s->channel, 0, sizeof(s->channel));
        s->regs[REG_INT_STATUS] = 0x00u;
        s->temp_alarm = false;
        adc128d818_update_irq(s);
        break;
    case REG_LIMIT_BASE ... REG_LIMIT_LAST:
        s->regs[reg] = val;
        break;
    case REG_INT_STATUS:
    case REG_BUSY_STATUS:
    case REG_MANUFACTURER_ID:
    case REG_REVISION_ID:
    case REG_CH_READING_BASE ... REG_CH_READING_LAST:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: write to read-only register 0x%02x\n",
                      __func__, s->description, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: write to undefined register 0x%02x\n",
                      __func__, s->description, reg);
        break;
    }
}

static uint8_t adc128d818_recv(I2CSlave *i2c)
{
    ADC128D818State *s = ADC128D818(i2c);

    return adc128d818_read_reg(s, s->pointer);
}

static int adc128d818_send(I2CSlave *i2c, uint8_t data)
{
    ADC128D818State *s = ADC128D818(i2c);

    if (s->len == 0u) {
        s->pointer = data;
        s->len++;
    } else {
        adc128d818_write_reg(s, s->pointer, data);
    }

    return 0;
}

static int adc128d818_event(I2CSlave *i2c, enum i2c_event event)
{
    ADC128D818State *s = ADC128D818(i2c);

    s->len = 0u;
    s->rx_byte = 0u;

    return 0;
}

static void adc128d818_get_ain(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value;
    int ch_num;
    int rc;

    rc = sscanf(name, "ain%d", &ch_num);
    if (rc != 1 || ch_num < 0 || ch_num >= (int)ADC128D818_NUM_CHANNELS) {
        error_setg(errp, "%s: %s: invalid channel '%s'", __func__,
                   s->description, name);
        return;
    }

    value = s->ain[ch_num];
    visit_type_int(v, name, &value, errp);
}

static void adc128d818_set_ain(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value;
    int ch_num;
    int rc;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    rc = sscanf(name, "ain%d", &ch_num);
    if (rc != 1 || ch_num < 0 || ch_num >= (int)ADC128D818_NUM_CHANNELS) {
        error_setg(errp, "%s: %s: invalid channel '%s'", __func__,
                   s->description, name);
        return;
    }

    if (value < INT16_MIN || value > INT16_MAX) {
        error_setg(errp, "%s: %s: value %" PRId64 " out of range for '%s'",
                   __func__, s->description, value, name);
        return;
    }

    s->ain[ch_num] = (int16_t)value;

    if (adc128d818_monitoring_active(s)) {
        adc128d818_convert(s);
    }
}

static void adc128d818_get_temperature(
    Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value = s->temperature;

    visit_type_int(v, name, &value, errp);
}

static void adc128d818_set_temperature(
    Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    if (value < INT32_MIN || value > INT32_MAX) {
        error_setg(errp, "%s: %s: value %" PRId64 " out of range", __func__,
                   s->description, value);
        return;
    }

    s->temperature = (int32_t)value;

    if (adc128d818_monitoring_active(s)) {
        adc128d818_convert(s);
    }
}

static const VMStateDescription adc128d818_vmstate = {
    .name = "ADC128D818",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, ADC128D818State),
        VMSTATE_UINT8(pointer, ADC128D818State),
        VMSTATE_UINT8(rx_byte, ADC128D818State),
        VMSTATE_UINT8_ARRAY(regs, ADC128D818State,
                            ADC128D818_NUM_REGS),
        VMSTATE_UINT16_ARRAY(channel, ADC128D818State,
                             ADC128D818_NUM_CHANNELS),
        VMSTATE_INT16_ARRAY(ain, ADC128D818State,
                            ADC128D818_NUM_CHANNELS),
        VMSTATE_INT32(temperature, ADC128D818State),
        VMSTATE_UINT16(ext_vref, ADC128D818State),
        VMSTATE_BOOL(temp_alarm, ADC128D818State),
        VMSTATE_I2C_SLAVE(parent_obj, ADC128D818State),
        VMSTATE_END_OF_LIST()
    }
};

static void adc128d818_reset_hold(Object *obj, ResetType type)
{
    ADC128D818State *s = ADC128D818(obj);

    trace_adc128d818_reset(s->description, "hw");
    adc128d818_reset_regs(s);
}

static void adc128d818_get_ext_vref(
    Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value = (int64_t)s->ext_vref;

    visit_type_int(v, name, &value, errp);
}

static void adc128d818_set_ext_vref(
    Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    ADC128D818State *s = ADC128D818(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    if (value < 0 || value > ADC128D818_MAX_VDD_MV) {
        error_setg(errp,
                   "%s: %s: ext-vref-mv %" PRId64 " out of range (0..%u mV)",
                   __func__, s->description, value, ADC128D818_MAX_VDD_MV);
        return;
    }

    s->ext_vref = (uint16_t)value;

    if (adc128d818_monitoring_active(s)) {
        adc128d818_convert(s);
    }
}

static void adc128d818_initfn(Object *obj)
{
    for (unsigned ch = 0u; ch < ADC128D818_NUM_CHANNELS; ch++) {
        char *name = g_strdup_printf("ain%u", ch);

        object_property_add(obj, name, "int", adc128d818_get_ain,
                            adc128d818_set_ain, NULL, NULL);
        g_free(name);
    }

    object_property_add(obj, "temperature", "int", adc128d818_get_temperature,
                        adc128d818_set_temperature, NULL, NULL);
    object_property_add(obj, "ext-vref-mv", "int", adc128d818_get_ext_vref,
                        adc128d818_set_ext_vref, NULL, NULL);
}

static void adc128d818_realize(DeviceState *dev, Error **errp)
{
    ADC128D818State *s = ADC128D818(dev);

    if (!s->description) {
        s->description = g_strdup(object_get_typename(OBJECT(dev)));
    }

    qdev_init_gpio_out(dev, &s->irq, 1u);
}

static const Property adc128d818_properties[] = {
    DEFINE_PROP_STRING("description", ADC128D818State, description),
};

static void adc128d818_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->event = adc128d818_event;
    ic->recv = adc128d818_recv;
    ic->send = adc128d818_send;
    dc->realize = adc128d818_realize;
    rc->phases.hold = adc128d818_reset_hold;
    dc->vmsd = &adc128d818_vmstate;
    device_class_set_props(dc, adc128d818_properties);
}

static const TypeInfo adc128d818_types[] = {
    {
        .name          = TYPE_ADC128D818,
        .parent        = TYPE_I2C_SLAVE,
        .instance_init = adc128d818_initfn,
        .instance_size = sizeof(ADC128D818State),
        .class_init    = adc128d818_class_init,
    },
};

DEFINE_TYPES(adc128d818_types)
