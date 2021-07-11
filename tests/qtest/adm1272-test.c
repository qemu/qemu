/*
 * QTests for the ADM1272 hotswap controller
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include "hw/i2c/pmbus_device.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/bitops.h"

#define TEST_ID "adm1272-test"
#define TEST_ADDR (0x10)

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

static const PMBusCoefficients adm1272_coefficients[] = {
    [0] = { 6770, 0, -2 },       /* voltage, vrange 60V */
    [1] = { 4062, 0, -2 },       /* voltage, vrange 100V */
    [2] = { 1326, 20480, -1 },   /* current, vsense range 15mV */
    [3] = { 663, 20480, -1 },    /* current, vsense range 30mV */
    [4] = { 3512, 0, -2 },       /* power, vrange 60V, irange 15mV */
    [5] = { 21071, 0, -3 },      /* power, vrange 100V, irange 15mV */
    [6] = { 17561, 0, -3 },      /* power, vrange 60V, irange 30mV */
    [7] = { 10535, 0, -3 },      /* power, vrange 100V, irange 30mV */
    [8] = { 42, 31871, -1 },     /* temperature */
};

uint16_t pmbus_data2direct_mode(PMBusCoefficients c, uint32_t value)
{
    /* R is usually negative to fit large readings into 16 bits */
    uint16_t y = (c.m * value + c.b) * pow(10, c.R);
    return y;
}

uint32_t pmbus_direct_mode2data(PMBusCoefficients c, uint16_t value)
{
    /* X = (Y * 10^-R - b) / m */
    uint32_t x = (value / pow(10, c.R) - c.b) / c.m;
    return x;
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

static uint16_t qmp_adm1272_get(const char *id, const char *property)
{
    QDict *response;
    uint64_t ret;

    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': %s } }", id, property);
    g_assert(qdict_haskey(response, "return"));
    ret = qnum_get_uint(qobject_to(QNum, qdict_get(response, "return")));
    qobject_unref(response);
    return ret;
}

static void qmp_adm1272_set(const char *id,
                            const char *property,
                            uint16_t value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': %s, 'value': %u } }", id, property, value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

/* PMBus commands are little endian vs i2c_set16 in i2c.h which is big endian */
static uint16_t adm1272_i2c_get16(QI2CDevice *i2cdev, uint8_t reg)
{
    uint8_t resp[2];
    i2c_read_block(i2cdev, reg, resp, sizeof(resp));
    return (resp[1] << 8) | resp[0];
}

/* PMBus commands are little endian vs i2c_set16 in i2c.h which is big endian */
static void adm1272_i2c_set16(QI2CDevice *i2cdev, uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = value & 255;
    data[1] = value >> 8;
    i2c_write_block(i2cdev, reg, data, sizeof(data));
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    int16_t err;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    value = qmp_adm1272_get(TEST_ID, "vout");
    err = ADM1272_VOLT_DEFAULT - value;
    g_assert_cmpuint(abs(err), <, ADM1272_VOLT_DEFAULT / 20);

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ADM1272_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
    g_assert_cmphex(i2c_value, ==, ADM1272_DIRECT_MODE);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_HIGH_LIMIT_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_IOUT_OC_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_HIGH_LIMIT_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_HIGH_LIMIT_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_HIGH_LIMIT_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VIN_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_HIGH_LIMIT_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VIN_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_PIN_OP_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ADM1272_PIN_OP_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ADM1272_PMBUS_REVISION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, ADM1272_MFR_PMON_CONTROL);
    g_assert_cmphex(i2c_value, ==, ADM1272_PMON_CONTROL_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_PMON_CONFIG);
    g_assert_cmphex(i2c_value, ==, ADM1272_PMON_CONFIG_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_DEVICE_CONFIG);
    g_assert_cmphex(i2c_value, ==, ADM1272_DEVICE_CONFIG_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_HYSTERESIS_HIGH);
    g_assert_cmphex(i2c_value, ==, ADM1272_HYSTERESIS_HIGH_DEFAULT);

    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_STRT_UP_IOUT_LIM);
    g_assert_cmphex(i2c_value, ==, ADM1272_STRT_UP_IOUT_LIM_DEFAULT);
}

/* test qmp access */
static void test_tx_rx(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value, value, i2c_voltage, i2c_pwr, lossy_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* converting to direct mode is lossy - we generate the same loss here */
    lossy_value =
        adm1272_direct_to_millivolts(adm1272_millivolts_to_direct(1000));
    qmp_adm1272_set(TEST_ID, "vin", 1000);
    value = qmp_adm1272_get(TEST_ID, "vin");
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VIN);
    i2c_voltage = adm1272_direct_to_millivolts(i2c_value);
    g_assert_cmpuint(value, ==, i2c_voltage);
    g_assert_cmpuint(i2c_voltage, ==, lossy_value);

    lossy_value =
        adm1272_direct_to_millivolts(adm1272_millivolts_to_direct(1500));
    qmp_adm1272_set(TEST_ID, "vout", 1500);
    value = qmp_adm1272_get(TEST_ID, "vout");
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    i2c_voltage = adm1272_direct_to_millivolts(i2c_value);
    g_assert_cmpuint(value, ==, i2c_voltage);
    g_assert_cmpuint(i2c_voltage, ==, lossy_value);

    lossy_value =
        adm1272_direct_to_milliamps(adm1272_milliamps_to_direct(1600));
    qmp_adm1272_set(TEST_ID, "iout", 1600);
    value = qmp_adm1272_get(TEST_ID, "iout");
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    i2c_value = adm1272_direct_to_milliamps(i2c_value);
    g_assert_cmphex(value, ==, i2c_value);
    g_assert_cmphex(i2c_value, ==, lossy_value);

    lossy_value =
        adm1272_direct_to_watts(adm1272_watts_to_direct(320));
    qmp_adm1272_set(TEST_ID, "pin", 320);
    value = qmp_adm1272_get(TEST_ID, "pin");
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_PIN);
    i2c_pwr = adm1272_direct_to_watts(i2c_value);
    g_assert_cmphex(value, ==, i2c_pwr);
    g_assert_cmphex(i2c_pwr, ==, lossy_value);
}

/* test r/w registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    adm1272_i2c_set16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT, 0xABCD);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xABCD);

    adm1272_i2c_set16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT, 0xCDEF);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xCDEF);

    adm1272_i2c_set16(i2cdev, PMBUS_IOUT_OC_WARN_LIMIT, 0x1234);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_IOUT_OC_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x1234);

    adm1272_i2c_set16(i2cdev, PMBUS_OT_FAULT_LIMIT, 0x5678);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x5678);

    adm1272_i2c_set16(i2cdev, PMBUS_OT_WARN_LIMIT, 0xABDC);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xABDC);

    adm1272_i2c_set16(i2cdev, PMBUS_VIN_OV_WARN_LIMIT, 0xCDEF);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VIN_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xCDEF);

    adm1272_i2c_set16(i2cdev, PMBUS_VIN_UV_WARN_LIMIT, 0x2345);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_VIN_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x2345);

    i2c_set8(i2cdev, ADM1272_RESTART_TIME, 0xF8);
    i2c_value = i2c_get8(i2cdev, ADM1272_RESTART_TIME);
    g_assert_cmphex(i2c_value, ==, 0xF8);

    i2c_set8(i2cdev, ADM1272_MFR_PMON_CONTROL, 0);
    i2c_value = i2c_get8(i2cdev, ADM1272_MFR_PMON_CONTROL);
    g_assert_cmpuint(i2c_value, ==, 0);

    adm1272_i2c_set16(i2cdev, ADM1272_MFR_PMON_CONFIG, 0xDEF0);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_PMON_CONFIG);
    g_assert_cmphex(i2c_value, ==, 0xDEF0);

    adm1272_i2c_set16(i2cdev, ADM1272_MFR_ALERT1_CONFIG, 0x0123);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_ALERT1_CONFIG);
    g_assert_cmphex(i2c_value, ==, 0x0123);

    adm1272_i2c_set16(i2cdev, ADM1272_MFR_ALERT2_CONFIG, 0x9876);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_ALERT2_CONFIG);
    g_assert_cmphex(i2c_value, ==, 0x9876);

    adm1272_i2c_set16(i2cdev, ADM1272_MFR_DEVICE_CONFIG, 0x3456);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_MFR_DEVICE_CONFIG);
    g_assert_cmphex(i2c_value, ==, 0x3456);

    adm1272_i2c_set16(i2cdev, ADM1272_HYSTERESIS_LOW, 0xCABA);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_HYSTERESIS_LOW);
    g_assert_cmphex(i2c_value, ==, 0xCABA);

    adm1272_i2c_set16(i2cdev, ADM1272_HYSTERESIS_HIGH, 0x6789);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_HYSTERESIS_HIGH);
    g_assert_cmphex(i2c_value, ==, 0x6789);

    adm1272_i2c_set16(i2cdev, ADM1272_STRT_UP_IOUT_LIM, 0x9876);
    i2c_value = adm1272_i2c_get16(i2cdev, ADM1272_STRT_UP_IOUT_LIM);
    g_assert_cmphex(i2c_value, ==, 0x9876);

    adm1272_i2c_set16(i2cdev, PMBUS_OPERATION, 0xA);
    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, 0xA);
}

/* test read-only registers */
static void test_ro_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_init_value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_init_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VIN);
    adm1272_i2c_set16(i2cdev, PMBUS_READ_VIN, 0xBEEF);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VIN);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    adm1272_i2c_set16(i2cdev, PMBUS_READ_VOUT, 0x1234);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    adm1272_i2c_set16(i2cdev, PMBUS_READ_IOUT, 0x6547);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    adm1272_i2c_set16(i2cdev, PMBUS_READ_TEMPERATURE_1, 0x1597);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_PIN);
    adm1272_i2c_set16(i2cdev, PMBUS_READ_PIN, 0xDEAD);
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_READ_PIN);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);
}

/* test voltage fault handling */
static void test_voltage_faults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    uint8_t i2c_byte;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    adm1272_i2c_set16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT,
                      adm1272_millivolts_to_direct(5000));
    qmp_adm1272_set(TEST_ID, "vout", 5100);

    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_value & PB_STATUS_VOUT) != 0);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_OV_WARN) != 0);

    qmp_adm1272_set(TEST_ID, "vout", 4500);
    i2c_set8(i2cdev, PMBUS_CLEAR_FAULTS, 0);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_OV_WARN) == 0);

    adm1272_i2c_set16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT,
                      adm1272_millivolts_to_direct(4600));
    i2c_value = adm1272_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_value & PB_STATUS_VOUT) != 0);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_UV_WARN) != 0);

}

static void adm1272_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x10"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("adm1272", i2c_device_create);
    qos_node_consumes("adm1272", "i2c-bus", &opts);

    qos_add_test("test_defaults", "adm1272", test_defaults, NULL);
    qos_add_test("test_tx_rx", "adm1272", test_tx_rx, NULL);
    qos_add_test("test_rw_regs", "adm1272", test_rw_regs, NULL);
    qos_add_test("test_ro_regs", "adm1272", test_ro_regs, NULL);
    qos_add_test("test_ov_faults", "adm1272", test_voltage_faults, NULL);
}
libqos_init(adm1272_register_nodes);
