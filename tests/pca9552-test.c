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
#include "libqos/i2c.h"
#include "hw/misc/pca9552_regs.h"

#define PCA9552_TEST_ID   "pca9552-test"
#define PCA9552_TEST_ADDR 0x60

static I2CAdapter *i2c;

static uint8_t pca9552_get8(I2CAdapter *i2c, uint8_t addr, uint8_t reg)
{
    uint8_t resp[1];
    i2c_send(i2c, addr, &reg, 1);
    i2c_recv(i2c, addr, resp, 1);
    return resp[0];
}

static void pca9552_set8(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                         uint8_t value)
{
    uint8_t cmd[2];
    uint8_t resp[1];

    cmd[0] = reg;
    cmd[1] = value;
    i2c_send(i2c, addr, cmd, 2);
    i2c_recv(i2c, addr, resp, 1);
    g_assert_cmphex(resp[0], ==, cmd[1]);
}

static void receive_autoinc(void)
{
    uint8_t resp;
    uint8_t reg = PCA9552_LS0 | PCA9552_AUTOINC;

    i2c_send(i2c, PCA9552_TEST_ADDR, &reg, 1);

    /* PCA9552_LS0 */
    i2c_recv(i2c, PCA9552_TEST_ADDR, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);

    /* PCA9552_LS1 */
    i2c_recv(i2c, PCA9552_TEST_ADDR, &resp, 1);
    g_assert_cmphex(resp, ==, 0x55);

    /* PCA9552_LS2 */
    i2c_recv(i2c, PCA9552_TEST_ADDR, &resp, 1);
    g_assert_cmphex(resp, ==, 0x55);

    /* PCA9552_LS3 */
    i2c_recv(i2c, PCA9552_TEST_ADDR, &resp, 1);
    g_assert_cmphex(resp, ==, 0x54);
}

static void send_and_receive(void)
{
    uint8_t value;

    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_LS0);
    g_assert_cmphex(value, ==, 0x55);

    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_INPUT0);
    g_assert_cmphex(value, ==, 0x0);

    /* Switch on LED 0 */
    pca9552_set8(i2c, PCA9552_TEST_ADDR, PCA9552_LS0, 0x54);
    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_LS0);
    g_assert_cmphex(value, ==, 0x54);

    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_INPUT0);
    g_assert_cmphex(value, ==, 0x01);

    /* Switch on LED 12 */
    pca9552_set8(i2c, PCA9552_TEST_ADDR, PCA9552_LS3, 0x54);
    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_LS3);
    g_assert_cmphex(value, ==, 0x54);

    value = pca9552_get8(i2c, PCA9552_TEST_ADDR, PCA9552_INPUT1);
    g_assert_cmphex(value, ==, 0x10);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-machine n800 "
                    "-device pca9552,bus=i2c-bus.0,id=" PCA9552_TEST_ID
                    ",address=0x60");
    i2c = omap_i2c_create(s, OMAP2_I2C_1_BASE);

    qtest_add_func("/pca9552/tx-rx", send_and_receive);
    qtest_add_func("/pca9552/rx-autoinc", receive_autoinc);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }
    g_free(i2c);

    return ret;
}
