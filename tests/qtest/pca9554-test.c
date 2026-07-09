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

static void pca9554_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x20"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { PCA9554_TEST_ADDR });

    qos_node_create_driver("pca9554", i2c_device_create);
    qos_node_consumes("pca9554", "i2c-bus", &opts);

    qos_add_test("reset-defaults", "pca9554", test_reset_defaults, NULL);
}

libqos_init(pca9554_register_nodes);
