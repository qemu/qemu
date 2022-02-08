/*
 * QTest testcase for the LSM303DLHC I2C magnetometer
 *
 * Copyright (C) 2021 Linaro Ltd.
 * Written by Kevin Townsend <kevin.townsend@linaro.org>
 *
 * Based on: https://www.st.com/resource/en/datasheet/lsm303dlhc.pdf
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qapi/qmp/qdict.h"

#define LSM303DLHC_MAG_TEST_ID        "lsm303dlhc_mag-test"
#define LSM303DLHC_MAG_REG_CRA        0x00
#define LSM303DLHC_MAG_REG_CRB        0x01
#define LSM303DLHC_MAG_REG_OUT_X_H    0x03
#define LSM303DLHC_MAG_REG_OUT_Z_H    0x05
#define LSM303DLHC_MAG_REG_OUT_Y_H    0x07
#define LSM303DLHC_MAG_REG_IRC        0x0C
#define LSM303DLHC_MAG_REG_TEMP_OUT_H 0x31

static int qmp_lsm303dlhc_mag_get_property(const char *id, const char *prop)
{
    QDict *response;
    int ret;

    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': %s } }", id, prop);
    g_assert(qdict_haskey(response, "return"));
    ret = qdict_get_int(response, "return");
    qobject_unref(response);
    return ret;
}

static void qmp_lsm303dlhc_mag_set_property(const char *id, const char *prop,
                                            int value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': %s, 'value': %d } }", id, prop, value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void send_and_receive(void *obj, void *data, QGuestAllocator *alloc)
{
    int64_t value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* Check default value for CRB */
    g_assert_cmphex(i2c_get8(i2cdev, LSM303DLHC_MAG_REG_CRB), ==, 0x20);

    /* Set x to 1.0 gauss and verify the value */
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID, "mag-x", 100000);
    value = qmp_lsm303dlhc_mag_get_property(
        LSM303DLHC_MAG_TEST_ID, "mag-x");
    g_assert_cmpint(value, ==, 100000);

    /* Set y to 1.5 gauss and verify the value */
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID, "mag-y", 150000);
    value = qmp_lsm303dlhc_mag_get_property(
        LSM303DLHC_MAG_TEST_ID, "mag-y");
    g_assert_cmpint(value, ==, 150000);

    /* Set z to 0.5 gauss and verify the value */
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID, "mag-z", 50000);
    value = qmp_lsm303dlhc_mag_get_property(
        LSM303DLHC_MAG_TEST_ID, "mag-z");
    g_assert_cmpint(value, ==, 50000);

    /* Set temperature to 23.6 C and verify the value */
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID,
        "temperature", 23600);
    value = qmp_lsm303dlhc_mag_get_property(
        LSM303DLHC_MAG_TEST_ID, "temperature");
    /* Should return 23.5 C due to 0.125°C steps. */
    g_assert_cmpint(value, ==, 23500);

    /* Read raw x axis registers (1 gauss = 1100 at +/-1.3 g gain) */
    value = i2c_get16(i2cdev, LSM303DLHC_MAG_REG_OUT_X_H);
    g_assert_cmphex(value, ==, 1100);

    /* Read raw y axis registers (1.5 gauss = 1650 at +/- 1.3 g gain = ) */
    value = i2c_get16(i2cdev, LSM303DLHC_MAG_REG_OUT_Y_H);
    g_assert_cmphex(value, ==, 1650);

    /* Read raw z axis registers (0.5 gauss = 490 at +/- 1.3 g gain = ) */
    value = i2c_get16(i2cdev, LSM303DLHC_MAG_REG_OUT_Z_H);
    g_assert_cmphex(value, ==, 490);

    /* Read raw temperature registers with temp disabled (CRA = 0x10) */
    value = i2c_get16(i2cdev, LSM303DLHC_MAG_REG_TEMP_OUT_H);
    g_assert_cmphex(value, ==, 0);

    /* Enable temperature reads (CRA = 0x90) */
    i2c_set8(i2cdev, LSM303DLHC_MAG_REG_CRA, 0x90);

    /* Read raw temp registers (23.5 C = 188 at 1 lsb = 0.125 C) */
    value = i2c_get16(i2cdev, LSM303DLHC_MAG_REG_TEMP_OUT_H);
    g_assert_cmphex(value, ==, 188);
}

static void reg_wraparound(void *obj, void *data, QGuestAllocator *alloc)
{
    uint8_t value[4];
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* Set x to 1.0 gauss, and y to 1.5 gauss for known test values */
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID, "mag-x", 100000);
    qmp_lsm303dlhc_mag_set_property(LSM303DLHC_MAG_TEST_ID, "mag-y", 150000);

    /* Check that requesting 4 bytes starting at Y_H wraps around to X_L */
    i2c_read_block(i2cdev, LSM303DLHC_MAG_REG_OUT_Y_H, value, 4);
    /* 1.5 gauss = 1650 lsb = 0x672 */
    g_assert_cmphex(value[0], ==, 0x06);
    g_assert_cmphex(value[1], ==, 0x72);
    /* 1.0 gauss = 1100 lsb = 0x44C */
    g_assert_cmphex(value[2], ==, 0x04);
    g_assert_cmphex(value[3], ==, 0x4C);

    /* Check that requesting LSM303DLHC_MAG_REG_IRC wraps around to CRA */
    i2c_read_block(i2cdev, LSM303DLHC_MAG_REG_IRC, value, 2);
    /* Default value for IRC = 0x33 */
    g_assert_cmphex(value[0], ==, 0x33);
    /* Default value for CRA = 0x10 */
    g_assert_cmphex(value[1], ==, 0x10);
}

static void lsm303dlhc_mag_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" LSM303DLHC_MAG_TEST_ID ",address=0x1e"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { 0x1E });

    qos_node_create_driver("lsm303dlhc_mag", i2c_device_create);
    qos_node_consumes("lsm303dlhc_mag", "i2c-bus", &opts);

    qos_add_test("tx-rx", "lsm303dlhc_mag", send_and_receive, NULL);
    qos_add_test("regwrap", "lsm303dlhc_mag", reg_wraparound, NULL);
}
libqos_init(lsm303dlhc_mag_register_nodes);
