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

static void pca9555_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x20"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { PCA9555_TEST_ADDR });

    qos_node_create_driver("pca9555", i2c_device_create);
    qos_node_consumes("pca9555", "i2c-bus", &opts);

    qos_add_test("reset-defaults", "pca9555", test_reset_defaults, NULL);
}

libqos_init(pca9555_register_nodes);
