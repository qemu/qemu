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

/* Voltage conversion */
static void test_voltage_conversion(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    qmp_adc128d818_set("ain0", 1280);
    g_test_message("Injected ain0 = 1280 mV");
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Read ch0: raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain1", 2560);
    g_test_message("Injected ain1 = 2560 mV");
    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("Read ch1: raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0xFFF0);

    qmp_adc128d818_set("ain2", 0);
    g_test_message("Injected ain2 = 0 mV");
    reading = i2c_get16(dev, REG_CH_READING_BASE + 2u);
    g_test_message("Read ch2: raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0x0000);
}

/* Temperature conversion (mode 0, channel 7 = temperature) */
static void
test_temperature_conversion(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    qmp_adc128d818_set("temperature", 25000);
    g_test_message("Injected temperature = 25000 mC (25.0 deg C)");
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("Read ch7: raw 0x%04x -> %d mC", reading,
             (int16_t)(reading & 0xFF80u) * 500 / 128);
    g_assert_cmphex(reading, ==, 0x1900);
}

/* Channels with distinct voltages */
static void test_all_channels(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    static const uint16_t ain_mv[NUM_CHANNELS] = {
        0, 320, 640, 960, 1280, 1920, 2240, 2560
    };
    static const uint16_t expect[NUM_CHANNELS] = {
        0x0000, 0x2000, 0x4000, 0x6000, 0x8000, 0xC000, 0xE000, 0xFFF0
    };
    uint16_t reading;
    unsigned ch;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_1);

    for (ch = 0u; ch < NUM_CHANNELS; ch++) {
        char name[8];
        snprintf(name, sizeof(name), "ain%u", ch);
        qmp_adc128d818_set(name, ain_mv[ch]);
    }

    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    for (ch = 0u; ch < NUM_CHANNELS; ch++) {
        reading = i2c_get16(dev, REG_CH_READING_BASE + ch);
        g_test_message("ch%u: ain %u mV -> raw 0x%04x (expect 0x%04x)",
                 ch, ain_mv[ch], reading, expect[ch]);
        g_assert_cmphex(reading, ==, expect[ch]);
    }
}

/* Voltage conversion edge cases */
static void test_voltage_edges(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_1);

    qmp_adc128d818_set("ain0", 3000);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Over-range 3000 mV: raw 0x%04x (expect 0xFFF0)", reading);
    g_assert_cmphex(reading, ==, 0xFFF0);

    qmp_adc128d818_set("ain1", 1);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("1 mV: raw 0x%04x (expect 0x0010)", reading);
    g_assert_cmphex(reading, ==, 0x0010);

    qmp_adc128d818_set("ain2", 640);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 2u);
    g_test_message("640 mV (quarter): raw 0x%04x (expect 0x4000)", reading);
    g_assert_cmphex(reading, ==, 0x4000);

    qmp_adc128d818_set("ain3", 1920);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 3u);
    g_test_message("1920 mV (3/4): raw 0x%04x (expect 0xC000)", reading);
    g_assert_cmphex(reading, ==, 0xC000);
}

/* Temperature conversion edge cases */
static void test_temperature_edges(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    qmp_adc128d818_set("temperature", 0);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("0 C: raw 0x%04x (expect 0x0000)", reading);
    g_assert_cmphex(reading, ==, 0x0000);

    qmp_adc128d818_set("temperature", -25000);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("-25 C: raw 0x%04x (expect 0xE700)", reading);
    g_assert_cmphex(reading, ==, 0xE700);

    qmp_adc128d818_set("temperature", 127500);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("+127.5 C: raw 0x%04x (expect 0x7F80)", reading);
    g_assert_cmphex(reading, ==, 0x7F80);

    qmp_adc128d818_set("temperature", -128000);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("-128 C: raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("temperature", 200000);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("200 C (clamped): raw 0x%04x (expect 0x7F80)", reading);
    g_assert_cmphex(reading, ==, 0x7F80);

    qmp_adc128d818_set("temperature", -200000);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("-200 C (clamped): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* External voltage reference */
static void test_ext_vref(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_1);

    qmp_adc128d818_set("ext-vref-mv", 4096);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_EXT_REF_EN | ADV_CONFIG_MODE_1);

    qmp_adc128d818_set("ain0", 1000);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("1000 mV / 4096 mV VREF: raw 0x%04x (expect 0x3E80)",
                   reading);
    g_assert_cmphex(reading, ==, 0x3E80);

    qmp_adc128d818_set("ain1", 2048);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("2048 mV / 4096 mV VREF: raw 0x%04x (expect 0x8000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* Interrupt status set on limit violation; persists while fault remains */
static void test_interrupt_status(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint8_t status;

    i2c_set8(dev, REG_LIMIT_BASE, 0x10);
    g_test_message("Set ch0 high limit = 0x10");

    qmp_adc128d818_set("ain0", 2560);
    g_test_message("Injected ain0 = 2560 mV (exceeds limit)");

    i2c_set8(dev, REG_CONFIG, CONFIG_START | CONFIG_INT_ENABLE);

    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS = 0x%02x (expect bit 0 set)", status);
    g_assert_cmphex(status & 0x01u, ==, 0x01);

    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS after re-read = 0x%02x (expect bit 0 still set)",
             status);
    g_assert_cmphex(status & 0x01u, ==, 0x01);

    qmp_adc128d818_set("ain0", 80);
    g_test_message("Injected ain0 = 80 mV (within limit)");
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS after fault cleared = 0x%02x "
                   "(expect bit 0 clear)", status);
    g_assert_cmphex(status & 0x01u, ==, 0x00);
}

/* INT_CLEAR stops the round-robin monitoring loop */
static void test_int_clear(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint8_t status;

    i2c_set8(dev, REG_LIMIT_BASE, 0x10);
    qmp_adc128d818_set("ain0", 2560);

    i2c_set8(dev, REG_CONFIG,
             CONFIG_START | CONFIG_INT_ENABLE | CONFIG_INT_CLEAR);
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS with INT_CLEAR set = 0x%02x (expect 0x00)",
                   status);
    g_assert_cmphex(status, ==, 0x00);

    i2c_set8(dev, REG_CONFIG, CONFIG_START | CONFIG_INT_ENABLE);
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS after INT_CLEAR cleared = 0x%02x (expect bit 0)",
             status);
    g_assert_cmphex(status & 0x01u, ==, 0x01);
}

/* Low-limit interrupt triggers correctly */
static void test_low_limit(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint8_t status;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    i2c_set8(dev, REG_LIMIT_BASE + 3u, 0x80);
    g_test_message("Set ch1 low limit = 0x80");

    i2c_set8(dev, REG_LIMIT_BASE + 5u, 0x80);
    g_test_message("Set ch2 low limit = 0x80");

    qmp_adc128d818_set("ain1", 640);
    qmp_adc128d818_set("ain2", 1280);
    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START | CONFIG_INT_ENABLE);

    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("INT_STATUS = 0x%02x (expect bits 1 and 2 set)", status);
    g_assert_cmphex(status & 0x02u, ==, 0x02);
    g_assert_cmphex(status & 0x04u, ==, 0x04);

    g_assert_cmphex(status & 0x01u, ==, 0x00);
}

/* Temperature high-limit interrupt with hysteresis */
static void test_temp_hysteresis(void *obj, void *data,
                                 QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint8_t status;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    i2c_set8(dev, REG_LIMIT_BASE + 7u * 2u, 0x32);
    i2c_set8(dev, REG_LIMIT_BASE + 7u * 2u + 1u, 0x28);

    qmp_adc128d818_set("temperature", 25000);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("25 C: INT_STATUS = 0x%02x (temp bit expect clear)", status);
    g_assert_cmphex(status & 0x80u, ==, 0x00);

    qmp_adc128d818_set("temperature", 55000);
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("55 C: INT_STATUS = 0x%02x (temp bit expect set)", status);
    g_assert_cmphex(status & 0x80u, ==, 0x80);

    qmp_adc128d818_set("temperature", 45000);
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("45 C (hysteresis): INT_STATUS = 0x%02x "
                   "(temp bit expect set)", status);
    g_assert_cmphex(status & 0x80u, ==, 0x80);

    qmp_adc128d818_set("temperature", 35000);
    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("35 C: INT_STATUS = 0x%02x (temp bit expect clear)", status);
    g_assert_cmphex(status & 0x80u, ==, 0x00);
}

/* Channel disable prevents conversion */
static void test_channel_disable(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CH_DISABLE, 0x01);
    g_test_message("Disabled channel 0");

    qmp_adc128d818_set("ain0", 1280);
    g_test_message("Injected ain0 = 1280 mV (disabled)");
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Read ch0 (disabled): raw 0x%04x", reading);
    g_assert_cmphex(reading, ==, 0x0000);

    qmp_adc128d818_set("ain1", 1280);
    g_test_message("Injected ain1 = 1280 mV (enabled)");
    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("Read ch1 (enabled): raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* One-shot conversion in shutdown mode */
static void test_one_shot(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    qmp_adc128d818_set("ain0", 1280);
    g_test_message("Injected ain0 = 1280 mV (device stopped)");

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Read ch0 before one-shot: raw 0x%04x", reading);
    g_assert_cmphex(reading, ==, 0x0000);

    g_assert_cmphex(i2c_get8(dev, REG_ONE_SHOT), ==, 0x00);

    i2c_set8(dev, REG_ONE_SHOT, 0x00);
    g_test_message("Triggered one-shot conversion with value 0x00");

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Read ch0 after one-shot: raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* Mode 1 makes channel 7 a voltage input instead of temperature */
static void test_mode_selection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_1);
    g_test_message("Set mode 1 (all voltage channels)");

    qmp_adc128d818_set("ain7", 1280);
    qmp_adc128d818_set("temperature", 50000);
    g_test_message("Injected ain7 = 1280 mV, temperature = 50000 mC");

    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("Read ch7 (mode 1): raw 0x%04x -> %u mV", reading,
             (reading >> 4u) * INTERNAL_VREF_MV / 4096u);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* Mode 2 - 4 pseudo-differential pairs */
static void test_mode2_diff(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_2);
    g_test_message("Set mode 2 (4 pseudo-differential pairs)");

    qmp_adc128d818_set("ain0", 2000);
    qmp_adc128d818_set("ain1", 720);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Pair 0 (IN0-IN1): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain3", 1920);
    qmp_adc128d818_set("ain2", 640);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("Pair 1 (IN3-IN2): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain4", 1500);
    qmp_adc128d818_set("ain5", 220);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 2u);
    g_test_message("Pair 2 (IN4-IN5): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain7", 2560);
    qmp_adc128d818_set("ain6", 1280);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 3u);
    g_test_message("Pair 3 (IN7-IN6): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 4u);
    g_test_message("Reserved ch4: raw 0x%04x (expect 0x0000)", reading);
    g_assert_cmphex(reading, ==, 0x0000);

    qmp_adc128d818_set("ain0", 500);
    qmp_adc128d818_set("ain1", 1000);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Pair 0 negative dV: raw 0x%04x (expect 0x0000)", reading);
    g_assert_cmphex(reading, ==, 0x0000);
}

/* Mode 3 - 4 single-ended + 2 pseudo-differential pairs */
static void test_mode3_mixed(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_3);
    g_test_message("Set mode 3 (4 single-ended + 2 differential)");

    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Ch0 single-ended: raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain4", 1500);
    qmp_adc128d818_set("ain5", 220);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 4u);
    g_test_message("Ch4 diff (IN4-IN5): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain7", 2560);
    qmp_adc128d818_set("ain6", 1280);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 5u);
    g_test_message("Ch5 diff (IN7-IN6): raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 6u);
    g_test_message("Reserved ch6: raw 0x%04x (expect 0x0000)", reading);
    g_assert_cmphex(reading, ==, 0x0000);

    qmp_adc128d818_set("temperature", 25000);

    reading = i2c_get16(dev, REG_CH_READING_BASE + 7u);
    g_test_message("Ch7 temperature: raw 0x%04x (expect 0x1900)", reading);
    g_assert_cmphex(reading, ==, 0x1900);
}

/* Mode change resets channel readings and interrupt status */
static void test_mode_change_reset(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;
    uint8_t status;

    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Before mode change, ch0: raw 0x%04x", reading);
    g_assert_cmphex(reading, !=, 0x0000);

    i2c_set8(dev, REG_CONFIG, 0x00);
    i2c_set8(dev, REG_LIMIT_BASE, 0x10);
    qmp_adc128d818_set("ain0", 2560);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    i2c_set8(dev, REG_CONFIG, 0x00);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_2);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After mode change, ch0: raw 0x%04x (expect 0x0000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x0000);

    status = i2c_get8(dev, REG_INT_STATUS);
    g_test_message("After mode change, INT_STATUS: 0x%02x (expect 0x00)",
                   status);
    g_assert_cmphex(status, ==, 0x00);

    g_assert_cmphex(i2c_get8(dev, REG_LIMIT_BASE), ==, 0x10);
    g_test_message("Limit register preserved after mode change");
}

/* QOM property changes trigger correct differential conversion */
static void test_diff_qom_trigger(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_MODE_2);

    qmp_adc128d818_set("ain0", 0);
    qmp_adc128d818_set("ain1", 0);
    qmp_adc128d818_set("ain2", 0);
    qmp_adc128d818_set("ain3", 0);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    qmp_adc128d818_set("ain0", 2000);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After ain0=2000, ain1=0: pair0 = 0x%04x (expect 0xC800)",
             reading);
    g_assert_cmphex(reading, ==, 0xC800);

    qmp_adc128d818_set("ain1", 720);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After ain1=720: pair0 = 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);

    qmp_adc128d818_set("ain3", 1920);
    qmp_adc128d818_set("ain2", 640);
    reading = i2c_get16(dev, REG_CH_READING_BASE + 1u);
    g_test_message("Pair 1 (IN3-IN2) via QOM: 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* One-shot conversion works in deep shutdown */
static void test_deep_shutdown(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_assert_cmphex(reading, ==, 0x8000);

    i2c_set8(dev, REG_DEEP_SHUTDOWN, 0x01);
    g_test_message("DEEP_SHUTDOWN write while running rejected");
    g_assert_cmphex(i2c_get8(dev, REG_DEEP_SHUTDOWN), ==, 0x00);

    i2c_set8(dev, REG_CONFIG, 0x00);
    i2c_set8(dev, REG_DEEP_SHUTDOWN, 0x01);
    qmp_adc128d818_set("ain0", 0);

    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Deep shutdown, no one-shot: raw 0x%04x (expect 0x8000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x8000);

    i2c_set8(dev, REG_ONE_SHOT, 0x01);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("Deep shutdown one-shot: raw 0x%04x (expect 0x0000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x0000);

    g_assert_cmphex(i2c_get8(dev, REG_DEEP_SHUTDOWN), ==, 0x01);

    i2c_set8(dev, REG_DEEP_SHUTDOWN, 0x00);
    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_ONE_SHOT, 0x01);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After exit shutdown: raw 0x%04x (expect 0x8000)", reading);
    g_assert_cmphex(reading, ==, 0x8000);
}

/* BUSY_STATUS NOT_READY clears after first conversion */
static void test_busy_status(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    g_assert_cmphex(i2c_get8(dev, REG_BUSY_STATUS) & 0x02, ==, 0x02);
    g_test_message("After reset: BUSY_STATUS = 0x%02x (NOT_READY set)",
             i2c_get8(dev, REG_BUSY_STATUS));

    qmp_adc128d818_set("ain0", 0);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);

    g_assert_cmphex(i2c_get8(dev, REG_BUSY_STATUS) & 0x02, ==, 0x00);
    g_test_message("After conversion: BUSY_STATUS = 0x%02x (NOT_READY cleared)",
             i2c_get8(dev, REG_BUSY_STATUS));
}

/* Programming Channel Disable resets channel readings */
static void test_chan_disable_clears(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    g_assert_cmphex(i2c_get16(dev, REG_CH_READING_BASE), ==, 0x8000);

    i2c_set8(dev, REG_CONFIG, 0x00);
    i2c_set8(dev, REG_CH_DISABLE, 0x02);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After CH_DISABLE write: ch0 raw 0x%04x (expect 0x0000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x0000);
}

/* Programming Advanced Configuration always resets channel readings */
static void test_adv_config_clears(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t reading;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    qmp_adc128d818_set("ain0", 1280);
    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    g_assert_cmphex(i2c_get16(dev, REG_CH_READING_BASE), ==, 0x8000);

    i2c_set8(dev, REG_CONFIG, 0x00);
    i2c_set8(dev, REG_ADV_CONFIG, 0x00);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After same-mode ADV_CONFIG: ch0 0x%04x (expect 0x0000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x0000);

    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    g_assert_cmphex(i2c_get16(dev, REG_CH_READING_BASE), ==, 0x8000);
    i2c_set8(dev, REG_CONFIG, 0x00);
    qmp_adc128d818_set("ext-vref-mv", 4096);
    i2c_set8(dev, REG_ADV_CONFIG, ADV_CONFIG_EXT_REF_EN);
    reading = i2c_get16(dev, REG_CH_READING_BASE);
    g_test_message("After ext-vref toggle: ch0 0x%04x (expect 0x0000)",
                   reading);
    g_assert_cmphex(reading, ==, 0x0000);
}

/* Conversion Rate register may only be programmed while in shutdown */
static void test_conv_rate(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, REG_CONFIG, CONFIG_INITIALIZATION);

    i2c_set8(dev, REG_CONV_RATE, 0x01);
    g_assert_cmphex(i2c_get8(dev, REG_CONV_RATE), ==, 0x01);

    i2c_set8(dev, REG_CONFIG, CONFIG_START);
    i2c_set8(dev, REG_CONV_RATE, 0x00);
    g_test_message("CONV_RATE while running: 0x%02x (expect unchanged 0x01)",
             i2c_get8(dev, REG_CONV_RATE));
    g_assert_cmphex(i2c_get8(dev, REG_CONV_RATE), ==, 0x01);
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
    qos_add_test("voltage-conversion", "adc128d818", test_voltage_conversion,
                 NULL);
    qos_add_test("temperature-conversion", "adc128d818",
                 test_temperature_conversion, NULL);
    qos_add_test("all-channels", "adc128d818", test_all_channels, NULL);
    qos_add_test("voltage-edges", "adc128d818", test_voltage_edges, NULL);
    qos_add_test("temperature-edges", "adc128d818", test_temperature_edges,
                 NULL);
    qos_add_test("ext-vref", "adc128d818", test_ext_vref, NULL);
    qos_add_test("interrupt-status", "adc128d818", test_interrupt_status, NULL);
    qos_add_test("int-clear", "adc128d818", test_int_clear, NULL);
    qos_add_test("low-limit", "adc128d818", test_low_limit, NULL);
    qos_add_test("temp-hysteresis", "adc128d818", test_temp_hysteresis, NULL);
    qos_add_test("channel-disable", "adc128d818", test_channel_disable, NULL);
    qos_add_test("one-shot", "adc128d818", test_one_shot, NULL);
    qos_add_test("mode-selection", "adc128d818", test_mode_selection, NULL);
    qos_add_test("mode2-diff", "adc128d818", test_mode2_diff, NULL);
    qos_add_test("mode3-mixed", "adc128d818", test_mode3_mixed, NULL);
    qos_add_test("mode-change-reset", "adc128d818", test_mode_change_reset,
                 NULL);
    qos_add_test("diff-qom-trigger", "adc128d818", test_diff_qom_trigger, NULL);
    qos_add_test("deep-shutdown", "adc128d818", test_deep_shutdown, NULL);
    qos_add_test("busy-status", "adc128d818", test_busy_status, NULL);
    qos_add_test("chan-disable-clears", "adc128d818", test_chan_disable_clears,
                 NULL);
    qos_add_test("adv-config-clears", "adc128d818", test_adv_config_clears,
                 NULL);
    qos_add_test("conv-rate", "adc128d818", test_conv_rate, NULL);
}
libqos_init(adc128d818_register_nodes);
