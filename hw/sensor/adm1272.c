/*
 * Analog Devices ADM1272 High Voltage Positive Hot Swap Controller and Digital
 * Power Monitor with PMBus
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_ADM1272 "adm1272"
#define ADM1272(obj) OBJECT_CHECK(ADM1272State, (obj), TYPE_ADM1272)

#define ADM1272_RESTART_TIME            0xCC
#define ADM1272_MFR_PEAK_IOUT           0xD0
#define ADM1272_MFR_PEAK_VIN            0xD1
#define ADM1272_MFR_PEAK_VOUT           0xD2
#define ADM1272_MFR_PMON_CONTROL        0xD3
#define ADM1272_MFR_PMON_CONFIG         0xD4
#define ADM1272_MFR_ALERT1_CONFIG       0xD5
#define ADM1272_MFR_ALERT2_CONFIG       0xD6
#define ADM1272_MFR_PEAK_TEMPERATURE    0xD7
#define ADM1272_MFR_DEVICE_CONFIG       0xD8
#define ADM1272_MFR_POWER_CYCLE         0xD9
#define ADM1272_MFR_PEAK_PIN            0xDA
#define ADM1272_MFR_READ_PIN_EXT        0xDB
#define ADM1272_MFR_READ_EIN_EXT        0xDC

#define ADM1272_HYSTERESIS_LOW          0xF2
#define ADM1272_HYSTERESIS_HIGH         0xF3
#define ADM1272_STATUS_HYSTERESIS       0xF4
#define ADM1272_STATUS_GPIO             0xF5
#define ADM1272_STRT_UP_IOUT_LIM        0xF6

/* Defaults */
#define ADM1272_OPERATION_DEFAULT       0x80
#define ADM1272_CAPABILITY_DEFAULT      0xB0
#define ADM1272_CAPABILITY_NO_PEC       0x30
#define ADM1272_DIRECT_MODE             0x40
#define ADM1272_HIGH_LIMIT_DEFAULT      0x0FFF
#define ADM1272_PIN_OP_DEFAULT          0x7FFF
#define ADM1272_PMBUS_REVISION_DEFAULT  0x22
#define ADM1272_MFR_ID_DEFAULT          "ADI"
#define ADM1272_MODEL_DEFAULT           "ADM1272-A1"
#define ADM1272_MFR_DEFAULT_REVISION    "25"
#define ADM1272_DEFAULT_DATE            "160301"
#define ADM1272_RESTART_TIME_DEFAULT    0x64
#define ADM1272_PMON_CONTROL_DEFAULT    0x1
#define ADM1272_PMON_CONFIG_DEFAULT     0x3F35
#define ADM1272_DEVICE_CONFIG_DEFAULT   0x8
#define ADM1272_HYSTERESIS_HIGH_DEFAULT     0xFFFF
#define ADM1272_STRT_UP_IOUT_LIM_DEFAULT    0x000F
#define ADM1272_VOLT_DEFAULT            12000
#define ADM1272_IOUT_DEFAULT            25000
#define ADM1272_PWR_DEFAULT             300  /* 12V 25A */
#define ADM1272_SHUNT                   300 /* micro-ohms */
#define ADM1272_VOLTAGE_COEFF_DEFAULT   1
#define ADM1272_CURRENT_COEFF_DEFAULT   3
#define ADM1272_PWR_COEFF_DEFAULT       7
#define ADM1272_IOUT_OFFSET             0x5000
#define ADM1272_IOUT_OFFSET             0x5000


typedef struct ADM1272State {
    PMBusDevice parent;

    uint64_t ein_ext;
    uint32_t pin_ext;
    uint8_t restart_time;

    uint16_t peak_vin;
    uint16_t peak_vout;
    uint16_t peak_iout;
    uint16_t peak_temperature;
    uint16_t peak_pin;

    uint8_t pmon_control;
    uint16_t pmon_config;
    uint16_t alert1_config;
    uint16_t alert2_config;
    uint16_t device_config;

    uint16_t hysteresis_low;
    uint16_t hysteresis_high;
    uint8_t status_hysteresis;
    uint8_t status_gpio;

    uint16_t strt_up_iout_lim;

} ADM1272State;

static const PMBusCoefficients adm1272_coefficients[] = {
    [0] = { 6770, 0, -2 },        /* voltage, vrange 60V */
    [1] = { 4062, 0, -2 },        /* voltage, vrange 100V */
    [2] = { 1326, 20480, -1 },    /* current, vsense range 15mV */
    [3] = { 663, 20480, -1 },     /* current, vsense range 30mV */
    [4] = { 3512, 0, -2 },        /* power, vrange 60V, irange 15mV */
    [5] = { 21071, 0, -3 },       /* power, vrange 100V, irange 15mV */
    [6] = { 17561, 0, -3 },       /* power, vrange 60V, irange 30mV */
    [7] = { 10535, 0, -3 },       /* power, vrange 100V, irange 30mV */
    [8] = { 42, 31871, -1 },      /* temperature */
};

static void adm1272_check_limits(ADM1272State *s)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(s);

    pmbus_check_limits(pmdev);

    if (pmdev->pages[0].read_vout > s->peak_vout) {
        s->peak_vout = pmdev->pages[0].read_vout;
    }

    if (pmdev->pages[0].read_vin > s->peak_vin) {
        s->peak_vin = pmdev->pages[0].read_vin;
    }

    if (pmdev->pages[0].read_iout > s->peak_iout) {
        s->peak_iout = pmdev->pages[0].read_iout;
    }

    if (pmdev->pages[0].read_temperature_1 > s->peak_temperature) {
        s->peak_temperature = pmdev->pages[0].read_temperature_1;
    }

    if (pmdev->pages[0].read_pin > s->peak_pin) {
        s->peak_pin = pmdev->pages[0].read_pin;
    }
}

static uint16_t adm1272_millivolts_to_direct(uint32_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_VOLTAGE_COEFF_DEFAULT];
    c.b = c.b * 1000;
    c.R = c.R - 3;
    return pmbus_data2direct_mode(c, value);
}

static uint32_t adm1272_direct_to_millivolts(uint16_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_VOLTAGE_COEFF_DEFAULT];
    c.b = c.b * 1000;
    c.R = c.R - 3;
    return pmbus_direct_mode2data(c, value);
}

static uint16_t adm1272_milliamps_to_direct(uint32_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_CURRENT_COEFF_DEFAULT];
    /* Y = (m * r_sense * x - b) * 10^R */
    c.m = c.m * ADM1272_SHUNT / 1000; /* micro-ohms */
    c.b = c.b * 1000;
    c.R = c.R - 3;
    return pmbus_data2direct_mode(c, value);
}

static uint32_t adm1272_direct_to_milliamps(uint16_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_CURRENT_COEFF_DEFAULT];
    c.m = c.m * ADM1272_SHUNT / 1000;
    c.b = c.b * 1000;
    c.R = c.R - 3;
    return pmbus_direct_mode2data(c, value);
}

static uint16_t adm1272_watts_to_direct(uint32_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_PWR_COEFF_DEFAULT];
    c.m = c.m * ADM1272_SHUNT / 1000;
    return pmbus_data2direct_mode(c, value);
}

static uint32_t adm1272_direct_to_watts(uint16_t value)
{
    PMBusCoefficients c = adm1272_coefficients[ADM1272_PWR_COEFF_DEFAULT];
    c.m = c.m * ADM1272_SHUNT / 1000;
    return pmbus_direct_mode2data(c, value);
}

static void adm1272_exit_reset(Object *obj)
{
    ADM1272State *s = ADM1272(obj);
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->page = 0;
    pmdev->pages[0].operation = ADM1272_OPERATION_DEFAULT;


    pmdev->capability = ADM1272_CAPABILITY_NO_PEC;
    pmdev->pages[0].revision = ADM1272_PMBUS_REVISION_DEFAULT;
    pmdev->pages[0].vout_mode = ADM1272_DIRECT_MODE;
    pmdev->pages[0].vout_ov_warn_limit = ADM1272_HIGH_LIMIT_DEFAULT;
    pmdev->pages[0].vout_uv_warn_limit = 0;
    pmdev->pages[0].iout_oc_warn_limit = ADM1272_HIGH_LIMIT_DEFAULT;
    pmdev->pages[0].ot_fault_limit = ADM1272_HIGH_LIMIT_DEFAULT;
    pmdev->pages[0].ot_warn_limit = ADM1272_HIGH_LIMIT_DEFAULT;
    pmdev->pages[0].vin_ov_warn_limit = ADM1272_HIGH_LIMIT_DEFAULT;
    pmdev->pages[0].vin_uv_warn_limit = 0;
    pmdev->pages[0].pin_op_warn_limit = ADM1272_PIN_OP_DEFAULT;

    pmdev->pages[0].status_word = 0;
    pmdev->pages[0].status_vout = 0;
    pmdev->pages[0].status_iout = 0;
    pmdev->pages[0].status_input = 0;
    pmdev->pages[0].status_temperature = 0;
    pmdev->pages[0].status_mfr_specific = 0;

    pmdev->pages[0].read_vin
        = adm1272_millivolts_to_direct(ADM1272_VOLT_DEFAULT);
    pmdev->pages[0].read_vout
        = adm1272_millivolts_to_direct(ADM1272_VOLT_DEFAULT);
    pmdev->pages[0].read_iout
        = adm1272_milliamps_to_direct(ADM1272_IOUT_DEFAULT);
    pmdev->pages[0].read_temperature_1 = 0;
    pmdev->pages[0].read_pin = adm1272_watts_to_direct(ADM1272_PWR_DEFAULT);
    pmdev->pages[0].revision = ADM1272_PMBUS_REVISION_DEFAULT;
    pmdev->pages[0].mfr_id = ADM1272_MFR_ID_DEFAULT;
    pmdev->pages[0].mfr_model = ADM1272_MODEL_DEFAULT;
    pmdev->pages[0].mfr_revision = ADM1272_MFR_DEFAULT_REVISION;
    pmdev->pages[0].mfr_date = ADM1272_DEFAULT_DATE;

    s->pin_ext = 0;
    s->ein_ext = 0;
    s->restart_time = ADM1272_RESTART_TIME_DEFAULT;

    s->peak_vin = 0;
    s->peak_vout = 0;
    s->peak_iout = 0;
    s->peak_temperature = 0;
    s->peak_pin = 0;

    s->pmon_control = ADM1272_PMON_CONTROL_DEFAULT;
    s->pmon_config = ADM1272_PMON_CONFIG_DEFAULT;
    s->alert1_config = 0;
    s->alert2_config = 0;
    s->device_config = ADM1272_DEVICE_CONFIG_DEFAULT;

    s->hysteresis_low = 0;
    s->hysteresis_high = ADM1272_HYSTERESIS_HIGH_DEFAULT;
    s->status_hysteresis = 0;
    s->status_gpio = 0;

    s->strt_up_iout_lim = ADM1272_STRT_UP_IOUT_LIM_DEFAULT;
}

static uint8_t adm1272_read_byte(PMBusDevice *pmdev)
{
    ADM1272State *s = ADM1272(pmdev);

    switch (pmdev->code) {
    case ADM1272_RESTART_TIME:
        pmbus_send8(pmdev, s->restart_time);
        break;

    case ADM1272_MFR_PEAK_IOUT:
        pmbus_send16(pmdev, s->peak_iout);
        break;

    case ADM1272_MFR_PEAK_VIN:
        pmbus_send16(pmdev, s->peak_vin);
        break;

    case ADM1272_MFR_PEAK_VOUT:
        pmbus_send16(pmdev, s->peak_vout);
        break;

    case ADM1272_MFR_PMON_CONTROL:
        pmbus_send8(pmdev, s->pmon_control);
        break;

    case ADM1272_MFR_PMON_CONFIG:
        pmbus_send16(pmdev, s->pmon_config);
        break;

    case ADM1272_MFR_ALERT1_CONFIG:
        pmbus_send16(pmdev, s->alert1_config);
        break;

    case ADM1272_MFR_ALERT2_CONFIG:
        pmbus_send16(pmdev, s->alert2_config);
        break;

    case ADM1272_MFR_PEAK_TEMPERATURE:
        pmbus_send16(pmdev, s->peak_temperature);
        break;

    case ADM1272_MFR_DEVICE_CONFIG:
        pmbus_send16(pmdev, s->device_config);
        break;

    case ADM1272_MFR_PEAK_PIN:
        pmbus_send16(pmdev, s->peak_pin);
        break;

    case ADM1272_MFR_READ_PIN_EXT:
        pmbus_send32(pmdev, s->pin_ext);
        break;

    case ADM1272_MFR_READ_EIN_EXT:
        pmbus_send64(pmdev, s->ein_ext);
        break;

    case ADM1272_HYSTERESIS_LOW:
        pmbus_send16(pmdev, s->hysteresis_low);
        break;

    case ADM1272_HYSTERESIS_HIGH:
        pmbus_send16(pmdev, s->hysteresis_high);
        break;

    case ADM1272_STATUS_HYSTERESIS:
        pmbus_send16(pmdev, s->status_hysteresis);
        break;

    case ADM1272_STATUS_GPIO:
        pmbus_send16(pmdev, s->status_gpio);
        break;

    case ADM1272_STRT_UP_IOUT_LIM:
        pmbus_send16(pmdev, s->strt_up_iout_lim);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading from unsupported register: 0x%02x\n",
                      __func__, pmdev->code);
        return 0xFF;
        break;
    }

    return 0;
}

static int adm1272_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                              uint8_t len)
{
    ADM1272State *s = ADM1272(pmdev);

    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing empty data\n", __func__);
        return -1;
    }

    pmdev->code = buf[0]; /* PMBus command code */

    if (len == 1) {
        return 0;
    }

    /* Exclude command code from buffer */
    buf++;
    len--;

    switch (pmdev->code) {

    case ADM1272_RESTART_TIME:
        s->restart_time = pmbus_receive8(pmdev);
        break;

    case ADM1272_MFR_PMON_CONTROL:
        s->pmon_control = pmbus_receive8(pmdev);
        break;

    case ADM1272_MFR_PMON_CONFIG:
        s->pmon_config = pmbus_receive16(pmdev);
        break;

    case ADM1272_MFR_ALERT1_CONFIG:
        s->alert1_config = pmbus_receive16(pmdev);
        break;

    case ADM1272_MFR_ALERT2_CONFIG:
        s->alert2_config = pmbus_receive16(pmdev);
        break;

    case ADM1272_MFR_DEVICE_CONFIG:
        s->device_config = pmbus_receive16(pmdev);
        break;

    case ADM1272_MFR_POWER_CYCLE:
        adm1272_exit_reset((Object *)s);
        break;

    case ADM1272_HYSTERESIS_LOW:
        s->hysteresis_low = pmbus_receive16(pmdev);
        break;

    case ADM1272_HYSTERESIS_HIGH:
        s->hysteresis_high = pmbus_receive16(pmdev);
        break;

    case ADM1272_STRT_UP_IOUT_LIM:
        s->strt_up_iout_lim = pmbus_receive16(pmdev);
        adm1272_check_limits(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: writing to unsupported register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0;
}

static void adm1272_get(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t value;

    if (strcmp(name, "vin") == 0 || strcmp(name, "vout") == 0) {
        value = adm1272_direct_to_millivolts(*(uint16_t *)opaque);
    } else if (strcmp(name, "iout") == 0) {
        value = adm1272_direct_to_milliamps(*(uint16_t *)opaque);
    } else if (strcmp(name, "pin") == 0) {
        value = adm1272_direct_to_watts(*(uint16_t *)opaque);
    } else {
        value = *(uint16_t *)opaque;
    }

    visit_type_uint16(v, name, &value, errp);
}

static void adm1272_set(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    ADM1272State *s = ADM1272(obj);
    uint16_t *internal = opaque;
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    if (strcmp(name, "vin") == 0 || strcmp(name, "vout") == 0) {
        *internal = adm1272_millivolts_to_direct(value);
    } else if (strcmp(name, "iout") == 0) {
        *internal = adm1272_milliamps_to_direct(value);
    } else if (strcmp(name, "pin") == 0) {
        *internal = adm1272_watts_to_direct(value);
    } else {
        *internal = value;
    }

    adm1272_check_limits(s);
}

static const VMStateDescription vmstate_adm1272 = {
    .name = "ADM1272",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_PMBUS_DEVICE(parent, ADM1272State),
        VMSTATE_UINT64(ein_ext, ADM1272State),
        VMSTATE_UINT32(pin_ext, ADM1272State),
        VMSTATE_UINT8(restart_time, ADM1272State),

        VMSTATE_UINT16(peak_vin, ADM1272State),
        VMSTATE_UINT16(peak_vout, ADM1272State),
        VMSTATE_UINT16(peak_iout, ADM1272State),
        VMSTATE_UINT16(peak_temperature, ADM1272State),
        VMSTATE_UINT16(peak_pin, ADM1272State),

        VMSTATE_UINT8(pmon_control, ADM1272State),
        VMSTATE_UINT16(pmon_config, ADM1272State),
        VMSTATE_UINT16(alert1_config, ADM1272State),
        VMSTATE_UINT16(alert2_config, ADM1272State),
        VMSTATE_UINT16(device_config, ADM1272State),

        VMSTATE_UINT16(hysteresis_low, ADM1272State),
        VMSTATE_UINT16(hysteresis_high, ADM1272State),
        VMSTATE_UINT8(status_hysteresis, ADM1272State),
        VMSTATE_UINT8(status_gpio, ADM1272State),

        VMSTATE_UINT16(strt_up_iout_lim, ADM1272State),
        VMSTATE_END_OF_LIST()
    }
};

static void adm1272_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags = PB_HAS_VOUT_MODE | PB_HAS_VOUT | PB_HAS_VIN | PB_HAS_IOUT |
                     PB_HAS_PIN | PB_HAS_TEMPERATURE | PB_HAS_MFR_INFO;

    pmbus_page_config(pmdev, 0, flags);

    object_property_add(obj, "vin", "uint16",
                        adm1272_get,
                        adm1272_set, NULL, &pmdev->pages[0].read_vin);

    object_property_add(obj, "vout", "uint16",
                        adm1272_get,
                        adm1272_set, NULL, &pmdev->pages[0].read_vout);

    object_property_add(obj, "iout", "uint16",
                        adm1272_get,
                        adm1272_set, NULL, &pmdev->pages[0].read_iout);

    object_property_add(obj, "pin", "uint16",
                        adm1272_get,
                        adm1272_set, NULL, &pmdev->pages[0].read_pin);

}

static void adm1272_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Analog Devices ADM1272 Hot Swap controller";
    dc->vmsd = &vmstate_adm1272;
    k->write_data = adm1272_write_data;
    k->receive_byte = adm1272_read_byte;
    k->device_num_pages = 1;

    rc->phases.exit = adm1272_exit_reset;
}

static const TypeInfo adm1272_info = {
    .name = TYPE_ADM1272,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ADM1272State),
    .instance_init = adm1272_init,
    .class_init = adm1272_class_init,
};

static void adm1272_register_types(void)
{
    type_register_static(&adm1272_info);
}

type_init(adm1272_register_types)
