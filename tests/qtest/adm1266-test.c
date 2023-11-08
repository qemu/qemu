/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * Copyright 2022 Google LLC
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

#define TEST_ID "adm1266-test"
#define TEST_ADDR (0x12)

#define ADM1266_BLACKBOX_CONFIG                 0xD3
#define ADM1266_PDIO_CONFIG                     0xD4
#define ADM1266_READ_STATE                      0xD9
#define ADM1266_READ_BLACKBOX                   0xDE
#define ADM1266_SET_RTC                         0xDF
#define ADM1266_GPIO_SYNC_CONFIGURATION         0xE1
#define ADM1266_BLACKBOX_INFORMATION            0xE6
#define ADM1266_PDIO_STATUS                     0xE9
#define ADM1266_GPIO_STATUS                     0xEA

/* Defaults */
#define ADM1266_OPERATION_DEFAULT               0x80
#define ADM1266_CAPABILITY_DEFAULT              0xA0
#define ADM1266_CAPABILITY_NO_PEC               0x20
#define ADM1266_PMBUS_REVISION_DEFAULT          0x22
#define ADM1266_MFR_ID_DEFAULT                  "ADI"
#define ADM1266_MFR_ID_DEFAULT_LEN              32
#define ADM1266_MFR_MODEL_DEFAULT               "ADM1266-A1"
#define ADM1266_MFR_MODEL_DEFAULT_LEN           32
#define ADM1266_MFR_REVISION_DEFAULT            "25"
#define ADM1266_MFR_REVISION_DEFAULT_LEN        8
#define TEST_STRING_A                           "a sample"
#define TEST_STRING_B                           "b sample"
#define TEST_STRING_C                           "rev c"

static void compare_string(QI2CDevice *i2cdev, uint8_t reg,
                           const char *test_str)
{
    uint8_t len = i2c_get8(i2cdev, reg);
    char i2c_str[SMBUS_DATA_MAX_LEN] = {0};

    i2c_read_block(i2cdev, reg, (uint8_t *)i2c_str, len);
    g_assert_cmpstr(i2c_str, ==, test_str);
}

static void write_and_compare_string(QI2CDevice *i2cdev, uint8_t reg,
                                     const char *test_str, uint8_t len)
{
    char buf[SMBUS_DATA_MAX_LEN] = {0};
    buf[0] = len;
    strncpy(buf + 1, test_str, len);
    i2c_write_block(i2cdev, reg, (uint8_t *)buf, len + 1);
    compare_string(i2cdev, reg, test_str);
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ADM1266_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ADM1266_PMBUS_REVISION_DEFAULT);

    compare_string(i2cdev, PMBUS_MFR_ID, ADM1266_MFR_ID_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_MODEL, ADM1266_MFR_MODEL_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_REVISION, ADM1266_MFR_REVISION_DEFAULT);
}

/* test r/w registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* empty strings */
    i2c_set8(i2cdev, PMBUS_MFR_ID, 0);
    compare_string(i2cdev, PMBUS_MFR_ID, "");

    i2c_set8(i2cdev, PMBUS_MFR_MODEL, 0);
    compare_string(i2cdev, PMBUS_MFR_MODEL, "");

    i2c_set8(i2cdev, PMBUS_MFR_REVISION, 0);
    compare_string(i2cdev, PMBUS_MFR_REVISION, "");

    /* test strings */
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_A,
                             sizeof(TEST_STRING_A));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_B,
                             sizeof(TEST_STRING_B));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_C,
                             sizeof(TEST_STRING_C));
}

static void adm1266_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x12"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("adm1266", i2c_device_create);
    qos_node_consumes("adm1266", "i2c-bus", &opts);

    qos_add_test("test_defaults", "adm1266", test_defaults, NULL);
    qos_add_test("test_rw_regs", "adm1266", test_rw_regs, NULL);
}

libqos_init(adm1266_register_nodes);
