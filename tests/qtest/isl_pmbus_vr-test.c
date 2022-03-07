/*
 * QTests for the ISL_PMBUS digital voltage regulators
 *
 * Copyright 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include <math.h>
#include "hw/i2c/pmbus_device.h"
#include "hw/sensor/isl_pmbus_vr.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/bitops.h"

#define TEST_ID "isl_pmbus_vr-test"
#define TEST_ADDR (0x43)

static uint16_t qmp_isl_pmbus_vr_get(const char *id, const char *property)
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

static void qmp_isl_pmbus_vr_set(const char *id,
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
static uint16_t isl_pmbus_vr_i2c_get16(QI2CDevice *i2cdev, uint8_t reg)
{
    uint8_t resp[2];
    i2c_read_block(i2cdev, reg, resp, sizeof(resp));
    return (resp[1] << 8) | resp[0];
}

/* PMBus commands are little endian vs i2c_set16 in i2c.h which is big endian */
static void isl_pmbus_vr_i2c_set16(QI2CDevice *i2cdev, uint8_t reg,
                                   uint16_t value)
{
    uint8_t data[2];

    data[0] = value & 255;
    data[1] = value >> 8;
    i2c_write_block(i2cdev, reg, data, sizeof(data));
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    value = qmp_isl_pmbus_vr_get(TEST_ID, "vout[0]");
    g_assert_cmpuint(value, ==, ISL_READ_VOUT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    g_assert_cmpuint(i2c_value, ==, ISL_READ_IOUT_DEFAULT);

    value = qmp_isl_pmbus_vr_get(TEST_ID, "pout[0]");
    g_assert_cmpuint(value, ==, ISL_READ_POUT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VIN);
    g_assert_cmpuint(i2c_value, ==, ISL_READ_VIN_DEFAULT);

    value = qmp_isl_pmbus_vr_get(TEST_ID, "iin[0]");
    g_assert_cmpuint(value, ==, ISL_READ_IIN_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_PIN);
    g_assert_cmpuint(i2c_value, ==, ISL_READ_PIN_DEFAULT);

    value = qmp_isl_pmbus_vr_get(TEST_ID, "temp1[0]");
    g_assert_cmpuint(value, ==, ISL_READ_TEMP_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_2);
    g_assert_cmpuint(i2c_value, ==, ISL_READ_TEMP_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_CAPABILITY);
    g_assert_cmphex(i2c_value, ==, ISL_CAPABILITY_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ISL_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_ON_OFF_CONFIG);
    g_assert_cmphex(i2c_value, ==, ISL_ON_OFF_CONFIG_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MODE_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_COMMAND);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_COMMAND_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MAX);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MAX_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_HIGH);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MARGIN_HIGH_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_LOW);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MARGIN_LOW_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_TRANSITION_RATE);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_TRANSITION_RATE_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_OV_FAULT_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_OT_FAULT_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_OT_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VIN_OV_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VIN_UV_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_IIN_OC_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_IIN_OC_FAULT_LIMIT_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ISL_REVISION_DEFAULT);
}

static void raa228000_test_defaults(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    value = qmp_isl_pmbus_vr_get(TEST_ID, "vout[0]");
    g_assert_cmpuint(value, ==, 0);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    g_assert_cmpuint(i2c_value, ==, 0);

    value = qmp_isl_pmbus_vr_get(TEST_ID, "pout[0]");
    g_assert_cmpuint(value, ==, 0);

    i2c_value = i2c_get8(i2cdev, PMBUS_CAPABILITY);
    g_assert_cmphex(i2c_value, ==, ISL_CAPABILITY_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ISL_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_ON_OFF_CONFIG);
    g_assert_cmphex(i2c_value, ==, ISL_ON_OFF_CONFIG_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MODE_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_COMMAND);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_COMMAND_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MAX);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MAX_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_HIGH);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MARGIN_HIGH_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_LOW);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_MARGIN_LOW_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_TRANSITION_RATE);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_TRANSITION_RATE_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VOUT_OV_FAULT_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_OT_FAULT_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_OT_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_OV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VIN_OV_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_UV_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_VIN_UV_WARN_LIMIT_DEFAULT);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_IIN_OC_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, ISL_IIN_OC_FAULT_LIMIT_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ISL_REVISION_DEFAULT);
}

/* test qmp access */
static void test_tx_rx(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value, value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    qmp_isl_pmbus_vr_set(TEST_ID, "vin[0]", 200);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "vin[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VIN);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "vout[0]", 2500);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "vout[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "iin[0]", 300);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "iin[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IIN);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "iout[0]", 310);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "iout[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "pin[0]", 100);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "pin[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_PIN);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "pout[0]", 95);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "pout[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_POUT);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "temp1[0]", 26);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "temp1[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "temp2[0]", 27);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "temp2[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_2);
    g_assert_cmpuint(value, ==, i2c_value);

    qmp_isl_pmbus_vr_set(TEST_ID, "temp3[0]", 28);
    value = qmp_isl_pmbus_vr_get(TEST_ID, "temp3[0]");
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_3);
    g_assert_cmpuint(value, ==, i2c_value);

}

/* test r/w registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_COMMAND, 0x1234);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_COMMAND);
    g_assert_cmphex(i2c_value, ==, 0x1234);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_TRIM, 0x4567);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_TRIM);
    g_assert_cmphex(i2c_value, ==, 0x4567);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_MAX, 0x9876);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MAX);
    g_assert_cmphex(i2c_value, ==, 0x9876);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_MARGIN_HIGH, 0xABCD);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_HIGH);
    g_assert_cmphex(i2c_value, ==, 0xABCD);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_MARGIN_LOW, 0xA1B2);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_LOW);
    g_assert_cmphex(i2c_value, ==, 0xA1B2);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_TRANSITION_RATE, 0xDEF1);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_TRANSITION_RATE);
    g_assert_cmphex(i2c_value, ==, 0xDEF1);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_DROOP, 0x5678);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_DROOP);
    g_assert_cmphex(i2c_value, ==, 0x5678);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_MIN, 0x1234);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_MIN);
    g_assert_cmphex(i2c_value, ==, 0x1234);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT, 0x2345);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x2345);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_UV_FAULT_LIMIT, 0xFA12);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VOUT_UV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xFA12);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_OT_FAULT_LIMIT, 0xF077);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xF077);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_OT_WARN_LIMIT, 0x7137);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x7137);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VIN_OV_FAULT_LIMIT, 0x3456);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_OV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0x3456);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VIN_UV_FAULT_LIMIT, 0xBADA);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_VIN_UV_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xBADA);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_IIN_OC_FAULT_LIMIT, 0xB1B0);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_IIN_OC_FAULT_LIMIT);
    g_assert_cmphex(i2c_value, ==, 0xB1B0);

    i2c_set8(i2cdev, PMBUS_OPERATION, 0xA);
    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, 0xA);

    i2c_set8(i2cdev, PMBUS_ON_OFF_CONFIG, 0x42);
    i2c_value = i2c_get8(i2cdev, PMBUS_ON_OFF_CONFIG);
    g_assert_cmphex(i2c_value, ==, 0x42);
}

/* test that devices with multiple pages can switch between them */
static void test_pages_rw(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, PMBUS_PAGE, 1);
    i2c_value = i2c_get8(i2cdev, PMBUS_PAGE);
    g_assert_cmphex(i2c_value, ==, 1);

    i2c_set8(i2cdev, PMBUS_PAGE, 0);
    i2c_value = i2c_get8(i2cdev, PMBUS_PAGE);
    g_assert_cmphex(i2c_value, ==, 0);
}

/* test read-only registers */
static void test_ro_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_init_value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VIN);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_VIN, 0xBEEF);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VIN);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IIN);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_IIN, 0xB00F);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IIN);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_VOUT, 0x1234);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_IOUT, 0x6547);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_IOUT);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_TEMPERATURE_1, 0x1597);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_2);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_TEMPERATURE_2, 0x1897);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_2);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_3);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_TEMPERATURE_3, 0x1007);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_3);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_PIN);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_PIN, 0xDEAD);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_PIN);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);

    i2c_init_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_POUT);
    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_READ_POUT, 0xD00D);
    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_READ_POUT);
    g_assert_cmphex(i2c_init_value, ==, i2c_value);
}

/* test voltage fault handling */
static void test_voltage_faults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    uint8_t i2c_byte;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT, 5000);
    qmp_isl_pmbus_vr_set(TEST_ID, "vout[0]", 5100);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_value & PB_STATUS_VOUT) != 0);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_OV_WARN) != 0);

    qmp_isl_pmbus_vr_set(TEST_ID, "vout[0]", 4500);
    i2c_set8(i2cdev, PMBUS_CLEAR_FAULTS, 0);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_OV_WARN) == 0);

    isl_pmbus_vr_i2c_set16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT, 4600);

    i2c_value = isl_pmbus_vr_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
    i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
    g_assert_true((i2c_value & PB_STATUS_VOUT) != 0);
    g_assert_true((i2c_byte & PB_STATUS_VOUT_UV_WARN) != 0);

}

static void isl_pmbus_vr_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x43"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("isl69260", i2c_device_create);
    qos_node_consumes("isl69260", "i2c-bus", &opts);

    qos_add_test("test_defaults", "isl69260", test_defaults, NULL);
    qos_add_test("test_tx_rx", "isl69260", test_tx_rx, NULL);
    qos_add_test("test_rw_regs", "isl69260", test_rw_regs, NULL);
    qos_add_test("test_pages_rw", "isl69260", test_pages_rw, NULL);
    qos_add_test("test_ro_regs", "isl69260", test_ro_regs, NULL);
    qos_add_test("test_ov_faults", "isl69260", test_voltage_faults, NULL);

    qos_node_create_driver("raa229004", i2c_device_create);
    qos_node_consumes("raa229004", "i2c-bus", &opts);

    qos_add_test("test_tx_rx", "raa229004", test_tx_rx, NULL);
    qos_add_test("test_rw_regs", "raa229004", test_rw_regs, NULL);
    qos_add_test("test_pages_rw", "raa229004", test_pages_rw, NULL);
    qos_add_test("test_ov_faults", "raa229004", test_voltage_faults, NULL);

    qos_node_create_driver("raa228000", i2c_device_create);
    qos_node_consumes("raa228000", "i2c-bus", &opts);

    qos_add_test("test_defaults", "raa228000", raa228000_test_defaults, NULL);
    qos_add_test("test_tx_rx", "raa228000", test_tx_rx, NULL);
    qos_add_test("test_rw_regs", "raa228000", test_rw_regs, NULL);
    qos_add_test("test_ov_faults", "raa228000", test_voltage_faults, NULL);
}
libqos_init(isl_pmbus_vr_register_nodes);
