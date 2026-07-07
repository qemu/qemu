/*
 * QTest testcase for the ADC128D818 ADC
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqos/i2c.h"
#include "libqos/qgraph.h"
#include "libqtest-single.h"
#include "qobject/qdict.h"

#define ADC128D818_TEST_ID      "adc128d818-test"
#define ADC128D818_TEST_ADDR    0x1f

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

/* Limit Registers (8-bit, read/write) */
#define REG_LIMIT_BASE          0x2a

/* ID Registers (read-only) */
#define REG_MANUFACTURER_ID     0x3e
#define REG_REVISION_ID         0x3f

/* Configuration Register (0x00) bitfields */
#define CONFIG_START            BIT(0)
#define CONFIG_INT_ENABLE       BIT(1)
#define CONFIG_INT_CLEAR        BIT(3)
#define CONFIG_INITIALIZATION   BIT(7)

/* Advanced Configuration Register (0x0b) bitfields */
#define ADV_CONFIG_EXT_REF_EN   BIT(0)
#define ADV_CONFIG_MODE_1       (1 << 1)
#define ADV_CONFIG_MODE_2       (2 << 1)
#define ADV_CONFIG_MODE_3       (3 << 1)

/* Number of channels */
#define NUM_CHANNELS            8

/* Internal VREF in mV */
#define INTERNAL_VREF_MV        2560

/* QMP helpers for setting device properties */

static void qmp_adc128d818_set(const char *property, int value)
{
    QDict *resp;

    resp = qmp("{ 'execute': 'qom-set', 'arguments':"
               " { 'path': %s, 'property': %s, 'value': %d } }",
               ADC128D818_TEST_ID, property, value);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static int qmp_adc128d818_get(const char *property)
{
    QDict *resp;
    int ret;

    resp = qmp("{ 'execute': 'qom-get', 'arguments':"
               " { 'path': %s, 'property': %s } }",
               ADC128D818_TEST_ID, property);
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_int(resp, "return");
    qobject_unref(resp);
    return ret;
}

/* Manufacturer and Revision ID registers */
static void test_id_registers(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, REG_MANUFACTURER_ID), ==, 0x01);
    g_assert_cmphex(i2c_get8(dev, REG_REVISION_ID), ==, 0x09);
}

/* Power-on-reset default values */
static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    unsigned ch;

    g_assert_cmphex(i2c_get8(dev, REG_CONFIG), ==, 0x08);
    g_assert_cmphex(i2c_get8(dev, REG_INT_STATUS), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_INT_MASK), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_CONV_RATE), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_CH_DISABLE), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_DEEP_SHUTDOWN), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_ADV_CONFIG), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_BUSY_STATUS), ==, 0x02);

    for (ch = 0u; ch < NUM_CHANNELS; ch++) {
        g_assert_cmphex(i2c_get8(dev, REG_LIMIT_BASE + ch * 2u), ==, 0xFF);
        g_assert_cmphex(i2c_get8(dev, REG_LIMIT_BASE + ch * 2u + 1u), ==, 0x00);
    }
}

/* Software reset via INITIALIZATION bit */
static void test_soft_reset(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, REG_INT_MASK, 0xAA);
    i2c_set8(dev, REG_CH_DISABLE, 0x55);
    i2c_set8(dev, REG_LIMIT_BASE, 0x42);

    g_assert_cmphex(i2c_get8(dev, REG_INT_MASK), ==, 0xAA);
    g_assert_cmphex(i2c_get8(dev, REG_CH_DISABLE), ==, 0x55);
    g_assert_cmphex(i2c_get8(dev, REG_LIMIT_BASE), ==, 0x42);

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    g_assert_cmphex(i2c_get8(dev, REG_CONFIG), ==, 0x08);
    g_assert_cmphex(i2c_get8(dev, REG_INT_MASK), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_CH_DISABLE), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, REG_LIMIT_BASE), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, REG_BUSY_STATUS), ==, 0x02);
}

/* Verify ain property readback via QMP */
static void test_ain_property(void *obj, void *data, QGuestAllocator *alloc)
{
    int value;

    qmp_adc128d818_set("ain3", 1500);
    value = qmp_adc128d818_get("ain3");
    g_test_message("Set ain3 = 1500 mV, readback = %d mV", value);
    g_assert_cmpint(value, ==, 1500);

    qmp_adc128d818_set("temperature", 37500);
    value = qmp_adc128d818_get("temperature");
    g_test_message("Set temperature = 37500 mC, readback = %d mC", value);
    g_assert_cmpint(value, ==, 37500);
}

static void adc128d818_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" ADC128D818_TEST_ID
                             ",address=0x1f"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { ADC128D818_TEST_ADDR });

    qos_node_create_driver("adc128d818", i2c_device_create);
    qos_node_consumes("adc128d818", "i2c-bus", &opts);

    qos_add_test("id-registers", "adc128d818", test_id_registers, NULL);
    qos_add_test("defaults", "adc128d818", test_defaults, NULL);
    qos_add_test("soft-reset", "adc128d818", test_soft_reset, NULL);
    qos_add_test("ain-property", "adc128d818", test_ain_property, NULL);
}
libqos_init(adc128d818_register_nodes);
