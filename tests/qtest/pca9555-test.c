/*
 * QTest testcase for the PCA9555 16-bit I/O port expander
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/pca9552_regs.h"
#include "libqos/i2c.h"
#include "libqos/qgraph.h"

#define PCA9555_TEST_ADDR 0x20

/* Verify power-on reset defaults match the PCA9555 datasheet. */
static void test_reset_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT1), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_OUTPUT0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_OUTPUT1), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_POLARITY0), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_POLARITY1), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_CONFIG0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_CONFIG1), ==, 0xFF);
}

/*
 * When a pin is configured as output and driven low (output=0, config=0),
 * the input register should reflect 0 for that pin.
 * When driven high (output=1, config=0), input should reflect 1.
 * When configured as input (config=1), PCA5555 pull-up makes it read 1.
 */
static void test_output_drives_input(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, PCA9535_CONFIG0, 0xF0);
    i2c_set8(dev, PCA9535_OUTPUT0, 0xFA);

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0xFA);

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT1), ==, 0xFF);

    i2c_set8(dev, PCA9535_CONFIG0, 0x00);
    i2c_set8(dev, PCA9535_OUTPUT0, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0x00);

    i2c_set8(dev, PCA9535_OUTPUT0, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0xFF);
}

/*
 * When all pins are inputs (config=0xFF) and no external driver,
 * PCA9555 pull-ups should make the input register read all ones.
 * Switching a pin to output mode with output=0 should drive it low.
 */
static void test_input_pullup(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT1), ==, 0xFF);

    i2c_set8(dev, PCA9535_OUTPUT0, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0xFF);

    i2c_set8(dev, PCA9535_CONFIG0, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0x00);
}

/*
 * Test that both ports are independent: changing port 0 registers
 * should not affect port 1 and vice versa.
 */
static void test_port_independence(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set8(dev, PCA9535_CONFIG0, 0x00);
    i2c_set8(dev, PCA9535_OUTPUT0, 0x00);

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT1), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_CONFIG1), ==, 0xFF);
    g_assert_cmphex(i2c_get8(dev, PCA9535_OUTPUT1), ==, 0xFF);

    i2c_set8(dev, PCA9535_CONFIG1, 0x00);
    i2c_set8(dev, PCA9535_OUTPUT1, 0xAA);

    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT0), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_INPUT1), ==, 0xAA);
    g_assert_cmphex(i2c_get8(dev, PCA9535_OUTPUT0), ==, 0x00);
    g_assert_cmphex(i2c_get8(dev, PCA9535_OUTPUT1), ==, 0xAA);
}

static void pca9555_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x20"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { PCA9555_TEST_ADDR });

    qos_node_create_driver("pca9555", i2c_device_create);
    qos_node_consumes("pca9555", "i2c-bus", &opts);

    qos_add_test("reset-defaults", "pca9555", test_reset_defaults, NULL);
    qos_add_test("output-drives-input", "pca9555", test_output_drives_input,
                 NULL);
    qos_add_test("input-pullup", "pca9555", test_input_pullup, NULL);
    qos_add_test("port-independence", "pca9555", test_port_independence, NULL);
}

libqos_init(pca9555_register_nodes);
