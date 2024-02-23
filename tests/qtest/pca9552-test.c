/*
 * QTest testcase for the PCA9552 LED blinker
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "hw/misc/pca9552_regs.h"

#define PCA9552_TEST_ID   "pca9552-test"
#define PCA9552_TEST_ADDR 0x60

static void pca9552_init(QI2CDevice *i2cdev)
{
    /* Switch on LEDs 0 and 12 */
    i2c_set8(i2cdev, PCA9552_LS0, 0x54);
    i2c_set8(i2cdev, PCA9552_LS3, 0x54);
}

static void receive_autoinc(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t resp;
    uint8_t reg = PCA9552_LS0 | PCA9552_AUTOINC;

    pca9552_init(i2cdev);

    qi2c_send(i2cdev, &reg, 1);

    /* PCA9552_LS0 */
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);

    /* PCA9552_LS1 */
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x55);

    /* PCA9552_LS2 */
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x55);

    /* PCA9552_LS3 */
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);
}

static void send_and_receive(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t value;

    value = i2c_get8(i2cdev, PCA9552_LS0);
    g_assert_cmphex(value, ==, 0x55);

    value = i2c_get8(i2cdev, PCA9552_INPUT0);
    g_assert_cmphex(value, ==, 0xFF);

    pca9552_init(i2cdev);

    value = i2c_get8(i2cdev, PCA9552_LS0);
    g_assert_cmphex(value, ==, 0x54);

    value = i2c_get8(i2cdev, PCA9552_INPUT0);
    g_assert_cmphex(value, ==, 0xFE);

    value = i2c_get8(i2cdev, PCA9552_LS3);
    g_assert_cmphex(value, ==, 0x54);

    value = i2c_get8(i2cdev, PCA9552_INPUT1);
    g_assert_cmphex(value, ==, 0xEF);
}

static void pca9552_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x60"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { 0x60 });

    qos_node_create_driver("pca9552", i2c_device_create);
    qos_node_consumes("pca9552", "i2c-bus", &opts);

    qos_add_test("tx-rx", "pca9552", send_and_receive, NULL);
    qos_add_test("rx-autoinc", "pca9552", receive_autoinc, NULL);
}
libqos_init(pca9552_register_nodes);
