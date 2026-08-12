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
#include "hw/gpio/pca9552_regs.h"

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

/* Verify the power-on reset defaults. */
static void test_reset_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* Prescalers, PWM duty cycles and LED selectors (all LEDs off) */
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_PSC0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_PWM0), ==, 0x80);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_PSC1), ==, 0xFF);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_PWM1), ==, 0x80);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_LS0), ==, 0x55);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_LS1), ==, 0x55);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_LS2), ==, 0x55);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_LS3), ==, 0x55);

    /* All LEDs off, so every pin floats high through its pull-up */
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_INPUT0), ==, 0xFF);
    g_assert_cmphex(i2c_get8(i2cdev, PCA9552_INPUT1), ==, 0xFF);
}

/*
 * The PCA9552 only advances the command pointer when the AI bit is set, and
 * it wraps modulo the full 10-register map.
 */
static void test_autoinc_requires_ai_bit(void *obj, void *data,
                                         QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t reg;
    uint8_t resp;

    /*
     * With the AI bit, reading from LS3 (register 9) rolls over to INPUT0
     * (register 0), not to a sibling in a register pair. All LEDs are off
     * after reset so the input ports read 0xFF.
     */
    reg = PCA9552_LS3 | PCA9552_AUTOINC;
    qi2c_send(i2cdev, &reg, 1);
    qi2c_recv(i2cdev, &resp, 1); /* LS3 */
    g_assert_cmphex(resp, ==, 0x55);
    qi2c_recv(i2cdev, &resp, 1); /* wraps to INPUT0 */
    g_assert_cmphex(resp, ==, 0xFF);
    qi2c_recv(i2cdev, &resp, 1); /* INPUT1 */
    g_assert_cmphex(resp, ==, 0xFF);

    /*
     * Without the AI bit the pointer must not advance: repeated reads keep
     * returning the same register.
     */
    i2c_set8(i2cdev, PCA9552_LS0, 0x54);
    reg = PCA9552_LS0;
    qi2c_send(i2cdev, &reg, 1);
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);
    qi2c_recv(i2cdev, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);
}

/*
 * The PCA9552 decodes a 4-bit command and has no register past LS3 (9), so
 * addressing register 0x0A reads back 0xFF.
 */
static void test_command_out_of_range(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(i2cdev, 0x0A), ==, 0xFF);
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
    qos_add_test("reset-defaults", "pca9552", test_reset_defaults, NULL);
    qos_add_test("autoinc-requires-ai-bit", "pca9552",
                 test_autoinc_requires_ai_bit, NULL);
    qos_add_test("command-out-of-range", "pca9552", test_command_out_of_range,
                 NULL);
}

libqos_init(pca9552_register_nodes);
