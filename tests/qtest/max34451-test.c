/*
 * QTests for the MAX34451 device
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/bitops.h"

#define TEST_ID "max34451-test"
#define TEST_ADDR (0x4e)

#define MAX34451_MFR_MODE               0xD1
#define MAX34451_MFR_VOUT_PEAK          0xD4
#define MAX34451_MFR_IOUT_PEAK          0xD5
#define MAX34451_MFR_TEMPERATURE_PEAK   0xD6
#define MAX34451_MFR_VOUT_MIN           0xD7

#define DEFAULT_VOUT                    0
#define DEFAULT_UV_LIMIT                0
#define DEFAULT_TEMPERATURE             2500
#define DEFAULT_SCALE                   0x7FFF
#define DEFAULT_OV_LIMIT                0x7FFF
#define DEFAULT_OC_LIMIT                0x7FFF
#define DEFAULT_OT_LIMIT                0x7FFF
#define DEFAULT_VMIN                    0x7FFF
#define DEFAULT_TON_FAULT_LIMIT         0xFFFF
#define DEFAULT_CHANNEL_CONFIG          0x20
#define DEFAULT_TEXT                    0x20

#define MAX34451_NUM_PWR_DEVICES        16
#define MAX34451_NUM_TEMP_DEVICES       5


static uint16_t qmp_max34451_get(const char *id, const char *property)
{
    QDict *response;
    uint16_t ret;
    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': %s } }", id, property);
    g_assert(qdict_haskey(response, "return"));
    ret = qnum_get_uint(qobject_to(QNum, qdict_get(response, "return")));
    qobject_unref(response);
    return ret;
}

static void qmp_max34451_set(const char *id,
                             const char *property,
                             uint16_t value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': %s, 'value': %u } }",
                   id, property, value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

/* PMBus commands are little endian vs i2c_set16 in i2c.h which is big endian */
static uint16_t max34451_i2c_get16(QI2CDevice *i2cdev, uint8_t reg)
{
    uint8_t resp[2];
    i2c_read_block(i2cdev, reg, resp, sizeof(resp));
    return (resp[1] << 8) | resp[0];
}

/* PMBus commands are little endian vs i2c_set16 in i2c.h which is big endian */
static void max34451_i2c_set16(QI2CDevice *i2cdev, uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = value & 255;
    data[1] = value >> 8;
    i2c_write_block(i2cdev, reg, data, sizeof(data));
}

/* Test default values */
static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;

    /* Default temperatures and temperature fault limits */
    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        path = g_strdup_printf("temperature[%d]", i);
        value = qmp_max34451_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, DEFAULT_TEMPERATURE);
        g_free(path);

        /* Temperature sensors start on page 16 */
        i2c_set8(i2cdev, PMBUS_PAGE, i + 16);
        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_TEMPERATURE);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_OT_FAULT_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_OT_LIMIT);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_OT_WARN_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_OT_LIMIT);
    }

    /* Default voltages and fault limits */
    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        path = g_strdup_printf("vout[%d]", i);
        value = qmp_max34451_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, DEFAULT_VOUT);
        g_free(path);

        i2c_set8(i2cdev, PMBUS_PAGE, i);
        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_VOUT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_VOUT);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_OV_LIMIT);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_OV_LIMIT);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_UV_LIMIT);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_UV_FAULT_LIMIT);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_UV_LIMIT);

        i2c_value = max34451_i2c_get16(i2cdev, MAX34451_MFR_VOUT_MIN);
        g_assert_cmpuint(i2c_value, ==, DEFAULT_VMIN);
    }

    i2c_value = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
    g_assert_cmphex(i2c_value, ==, 0x40); /* DIRECT mode */

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, 0x11); /* Rev 1.1 */
}

/* Test setting temperature */
static void test_temperature(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;

    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        path = g_strdup_printf("temperature[%d]", i);
        qmp_max34451_set(TEST_ID, path, 0xBE00 + i);
        value = qmp_max34451_get(TEST_ID, path);
        g_assert_cmphex(value, ==, 0xBE00 + i);
        g_free(path);
    }

    /* compare qmp read with i2c read separately */
    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        /* temperature[0] is on page 16 */
        i2c_set8(i2cdev, PMBUS_PAGE, i + 16);
        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
        g_assert_cmphex(i2c_value, ==, 0xBE00 + i);

        i2c_value = max34451_i2c_get16(i2cdev, MAX34451_MFR_TEMPERATURE_PEAK);
        g_assert_cmphex(i2c_value, ==, 0xBE00 + i);
    }
}

/* Test setting voltage */
static void test_voltage(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value, i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;

    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        path = g_strdup_printf("vout[%d]", i);
        qmp_max34451_set(TEST_ID, path, 3000 + i);
        value = qmp_max34451_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, 3000 + i);
        g_free(path);
    }

    /* compare qmp read with i2c read separately */
    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        i2c_set8(i2cdev, PMBUS_PAGE, i);
        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_VOUT);
        g_assert_cmpuint(i2c_value, ==, 3000 + i);

        i2c_value = max34451_i2c_get16(i2cdev, MAX34451_MFR_VOUT_PEAK);
        g_assert_cmpuint(i2c_value, ==, 3000 + i);

        i2c_value = max34451_i2c_get16(i2cdev, MAX34451_MFR_VOUT_MIN);
        g_assert_cmpuint(i2c_value, ==, 3000 + i);
    }
}

/* Test setting some read/write registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, PMBUS_PAGE, 11);
    i2c_value = i2c_get8(i2cdev, PMBUS_PAGE);
    g_assert_cmpuint(i2c_value, ==, 11);

    i2c_set8(i2cdev, PMBUS_OPERATION, 1);
    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmpuint(i2c_value, ==, 1);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_MARGIN_HIGH, 5000);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_HIGH);
    g_assert_cmpuint(i2c_value, ==, 5000);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_MARGIN_LOW, 4000);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_MARGIN_LOW);
    g_assert_cmpuint(i2c_value, ==, 4000);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT, 5500);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT);
    g_assert_cmpuint(i2c_value, ==, 5500);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT, 5600);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_OV_WARN_LIMIT);
    g_assert_cmpuint(i2c_value, ==, 5600);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_UV_FAULT_LIMIT, 5700);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_UV_FAULT_LIMIT);
    g_assert_cmpuint(i2c_value, ==, 5700);

    max34451_i2c_set16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT, 5800);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_VOUT_UV_WARN_LIMIT);
    g_assert_cmpuint(i2c_value, ==, 5800);

    max34451_i2c_set16(i2cdev, PMBUS_POWER_GOOD_ON, 5900);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_POWER_GOOD_ON);
    g_assert_cmpuint(i2c_value, ==, 5900);

    max34451_i2c_set16(i2cdev, PMBUS_POWER_GOOD_OFF, 6100);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_POWER_GOOD_OFF);
    g_assert_cmpuint(i2c_value, ==, 6100);
}

/* Test that Read only registers can't be written */
static void test_ro_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value, i2c_init_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, PMBUS_PAGE, 1); /* move to page 1 */
    i2c_init_value = i2c_get8(i2cdev, PMBUS_CAPABILITY);
    i2c_set8(i2cdev, PMBUS_CAPABILITY, 0xF9);
    i2c_value = i2c_get8(i2cdev, PMBUS_CAPABILITY);
    g_assert_cmpuint(i2c_init_value, ==, i2c_value);

    i2c_init_value = max34451_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    max34451_i2c_set16(i2cdev, PMBUS_READ_VOUT, 0xDEAD);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_VOUT);
    g_assert_cmpuint(i2c_init_value, ==, i2c_value);
    g_assert_cmphex(i2c_value, !=, 0xDEAD);

    i2c_set8(i2cdev, PMBUS_PAGE, 16); /* move to page 16 */
    i2c_init_value = max34451_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    max34451_i2c_set16(i2cdev, PMBUS_READ_TEMPERATURE_1, 0xABBA);
    i2c_value = max34451_i2c_get16(i2cdev, PMBUS_READ_TEMPERATURE_1);
    g_assert_cmpuint(i2c_init_value, ==, i2c_value);
    g_assert_cmphex(i2c_value, !=, 0xABBA);
}

/* test over voltage faults */
static void test_ov_faults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    uint8_t i2c_byte;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;
    /* Test ov fault reporting */
    for (int i = 0; i < MAX34451_NUM_PWR_DEVICES; i++) {
        path = g_strdup_printf("vout[%d]", i);
        i2c_set8(i2cdev, PMBUS_PAGE, i);
        max34451_i2c_set16(i2cdev, PMBUS_VOUT_OV_FAULT_LIMIT, 5000);
        qmp_max34451_set(TEST_ID, path, 5100);
        g_free(path);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
        i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_VOUT);
        g_assert_true((i2c_value & PB_STATUS_VOUT) != 0);
        g_assert_true((i2c_byte & PB_STATUS_VOUT_OV_FAULT) != 0);
    }
}

/* test over temperature faults */
static void test_ot_faults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    uint8_t i2c_byte;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;

    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES; i++) {
        path = g_strdup_printf("temperature[%d]", i);
        i2c_set8(i2cdev, PMBUS_PAGE, i + 16);
        max34451_i2c_set16(i2cdev, PMBUS_OT_FAULT_LIMIT, 6000);
        qmp_max34451_set(TEST_ID, path, 6100);
        g_free(path);

        i2c_value = max34451_i2c_get16(i2cdev, PMBUS_STATUS_WORD);
        i2c_byte = i2c_get8(i2cdev, PMBUS_STATUS_TEMPERATURE);
        g_assert_true((i2c_value & PB_STATUS_TEMPERATURE) != 0);
        g_assert_true((i2c_byte & PB_STATUS_OT_FAULT) != 0);
    }
}

#define RAND_ON_OFF_CONFIG  0x12
#define RAND_MFR_MODE       0x3456

/* test writes to all pages */
static void test_all_pages(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, PMBUS_PAGE, PB_ALL_PAGES);
    i2c_set8(i2cdev, PMBUS_ON_OFF_CONFIG, RAND_ON_OFF_CONFIG);
    max34451_i2c_set16(i2cdev, MAX34451_MFR_MODE, RAND_MFR_MODE);

    for (int i = 0; i < MAX34451_NUM_TEMP_DEVICES + MAX34451_NUM_PWR_DEVICES;
         i++) {
        i2c_value = i2c_get8(i2cdev, PMBUS_ON_OFF_CONFIG);
        g_assert_cmphex(i2c_value, ==, RAND_ON_OFF_CONFIG);
        i2c_value = max34451_i2c_get16(i2cdev, MAX34451_MFR_MODE);
        g_assert_cmphex(i2c_value, ==, RAND_MFR_MODE);
    }
}

static void max34451_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x4e"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("max34451", i2c_device_create);
    qos_node_consumes("max34451", "i2c-bus", &opts);

    qos_add_test("test_defaults", "max34451", test_defaults, NULL);
    qos_add_test("test_temperature", "max34451", test_temperature, NULL);
    qos_add_test("test_voltage", "max34451", test_voltage, NULL);
    qos_add_test("test_rw_regs", "max34451", test_rw_regs, NULL);
    qos_add_test("test_ro_regs", "max34451", test_ro_regs, NULL);
    qos_add_test("test_ov_faults", "max34451", test_ov_faults, NULL);
    qos_add_test("test_ot_faults", "max34451", test_ot_faults, NULL);
    qos_add_test("test_all_pages", "max34451", test_all_pages, NULL);
}
libqos_init(max34451_register_nodes);
