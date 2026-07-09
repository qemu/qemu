/*
 * QTest testcase for the PCA9554/PCA9536 I/O port expanders
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/pca9554_regs.h"
#include "libqos/i2c.h"
#include "libqos/qgraph.h"

#define PCA9554_TEST_ADDR 0x20

/* Verify power-on reset defaults match the PCA9554 datasheet. */
static void test_reset_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    /* All pins are inputs, pulled high, with no polarity inversion. */
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_OUTPUT), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_POLARITY), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_CONFIG), ==, 0xFF);
}

/*
 * A pin configured as output (config=0) drives its OUTPUT register level onto
 * the pin (push-pull), which the INPUT register reflects. A pin configured as
 * input (config=1) floats high through its pull-up.
 */
static void test_output_drives_input(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    /* Low nibble output, high nibble input (pull-up). */
    i2c_set8(dev, PCA9554_CONFIG, 0xF0);
    i2c_set8(dev, PCA9554_OUTPUT, 0xFA);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFA);

    /* All outputs, driven low then high. */
    i2c_set8(dev, PCA9554_CONFIG, 0x00);
    i2c_set8(dev, PCA9554_OUTPUT, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x00);

    i2c_set8(dev, PCA9554_OUTPUT, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFF);
}

/*
 * With all pins configured as inputs the pull-ups make the INPUT register read
 * all ones regardless of the OUTPUT register; switching a pin to output with
 * output=0 drives it low.
 */
static void test_input_pullup(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFF);

    i2c_set8(dev, PCA9554_OUTPUT, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFF);

    i2c_set8(dev, PCA9554_CONFIG, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x00);
}

/*
 * Polarity inversion: reading INPUT returns the XOR of the pin levels and the
 * polarity register.
 */
static void test_polarity_inversion(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xFF);

    i2c_set8(dev, PCA9554_POLARITY, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x00);

    i2c_set8(dev, PCA9554_POLARITY, 0x0F);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xF0);
}

/* Polarity inversion combined with output-driven pins. */
static void test_polarity_with_output(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, PCA9554_CONFIG, 0x00);
    i2c_set8(dev, PCA9554_OUTPUT, 0xA5);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0xA5);

    i2c_set8(dev, PCA9554_POLARITY, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x5A);

    /* Inversion only affects the INPUT read, not the OUTPUT register. */
    g_assert_cmphex(i2c_get8(dev, PCA9554_OUTPUT), ==, 0xA5);
}

/*
 * The PCA9554 has no auto-increment: the command pointer never advances, so
 * multi-byte reads and writes all target the addressed register.
 */
static void test_no_autoincrement(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint8_t buf[2];

    /* Distinct values in adjacent registers. */
    i2c_set8(dev, PCA9554_OUTPUT, 0xAA);
    i2c_set8(dev, PCA9554_POLARITY, 0x33);

    /* Two reads from OUTPUT return OUTPUT twice, not OUTPUT then POLARITY. */
    i2c_read_block(dev, PCA9554_OUTPUT, buf, 2);
    g_assert_cmphex(buf[0], ==, 0xAA);
    g_assert_cmphex(buf[1], ==, 0xAA);

    /* The second written byte overwrites OUTPUT; POLARITY is untouched. */
    buf[0] = 0x12;
    buf[1] = 0x34;
    i2c_write_block(dev, PCA9554_OUTPUT, buf, 2);
    g_assert_cmphex(i2c_get8(dev, PCA9554_OUTPUT), ==, 0x34);
    g_assert_cmphex(i2c_get8(dev, PCA9554_POLARITY), ==, 0x33);
}

/*
 * The PCA9536 shares the PCA9554 register map but only has four pins, so its
 * reset defaults and pin logic are masked to the low nibble.
 */
static void test_pca9536_reset_defaults(void *obj, void *data,
                                        QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x0F);
    g_assert_cmphex(i2c_get8(dev, PCA9554_OUTPUT), ==, 0x0F);
    g_assert_cmphex(i2c_get8(dev, PCA9554_POLARITY), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_CONFIG), ==, 0x0F);
}

/* Only the four low pins are driven; the upper nibble stays low. */
static void test_pca9536_output_drives_input(void *obj, void *data,
                                             QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, PCA9554_CONFIG, 0x00);

    i2c_set8(dev, PCA9554_OUTPUT, 0x0A);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x0A);

    i2c_set8(dev, PCA9554_OUTPUT, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x00);
}

/*
 * The four upper bits address pins that do not exist on the PCA9536, so writes
 * to the register map discard them: the writable registers read back with bits
 * [7:4] cleared, and driving them onto the pins never surfaces in INPUT.
 */
static void test_pca9536_ignores_upper_bits(void *obj, void *data,
                                            QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    /* Bits [7:4] are dropped on write; bits [3:0] survive. */
    i2c_set8(dev, PCA9554_OUTPUT, 0xFA);
    g_assert_cmphex(i2c_get8(dev, PCA9554_OUTPUT), ==, 0x0A);

    i2c_set8(dev, PCA9554_POLARITY, 0xF5);
    g_assert_cmphex(i2c_get8(dev, PCA9554_POLARITY), ==, 0x05);

    i2c_set8(dev, PCA9554_CONFIG, 0xF3);
    g_assert_cmphex(i2c_get8(dev, PCA9554_CONFIG), ==, 0x03);

    /*
     * With all four pins as outputs, driving 0xFF only affects the low
     * nibble.
     */
    i2c_set8(dev, PCA9554_POLARITY, 0x00);
    i2c_set8(dev, PCA9554_CONFIG, 0x00);
    i2c_set8(dev, PCA9554_OUTPUT, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9554_INPUT), ==, 0x0F);
}

static void pca9554_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x20"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { PCA9554_TEST_ADDR });

    qos_node_create_driver("pca9554", i2c_device_create);
    qos_node_consumes("pca9554", "i2c-bus", &opts);

    qos_add_test("reset-defaults", "pca9554", test_reset_defaults, NULL);
    qos_add_test("output-drives-input", "pca9554", test_output_drives_input,
                 NULL);
    qos_add_test("input-pullup", "pca9554", test_input_pullup, NULL);
    qos_add_test("polarity-inversion", "pca9554", test_polarity_inversion,
                 NULL);
    qos_add_test("polarity-with-output", "pca9554", test_polarity_with_output,
                 NULL);
    qos_add_test("no-autoincrement", "pca9554", test_no_autoincrement, NULL);

    qos_node_create_driver("pca9536", i2c_device_create);
    qos_node_consumes("pca9536", "i2c-bus", &opts);

    qos_add_test("reset-defaults", "pca9536", test_pca9536_reset_defaults,
                 NULL);
    qos_add_test("output-drives-input", "pca9536",
                 test_pca9536_output_drives_input, NULL);
    qos_add_test("ignores-upper-bits", "pca9536",
                 test_pca9536_ignores_upper_bits, NULL);
}

libqos_init(pca9554_register_nodes);
