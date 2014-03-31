/*
 * QTest testcase for the TMP105 temperature sensor
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>

#include "libqtest.h"
#include "libqos/i2c.h"
#include "hw/misc/tmp105_regs.h"

#define OMAP2_I2C_1_BASE 0x48070000

#define TMP105_TEST_ID   "tmp105-test"
#define TMP105_TEST_ADDR 0x49

static I2CAdapter *i2c;

static uint16_t tmp105_get16(I2CAdapter *i2c, uint8_t addr, uint8_t reg)
{
    uint8_t resp[2];
    i2c_send(i2c, addr, &reg, 1);
    i2c_recv(i2c, addr, resp, 2);
    return (resp[0] << 8) | resp[1];
}

static void tmp105_set8(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
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

static void tmp105_set16(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                         uint16_t value)
{
    uint8_t cmd[3];
    uint8_t resp[2];

    cmd[0] = reg;
    cmd[1] = value >> 8;
    cmd[2] = value & 255;
    i2c_send(i2c, addr, cmd, 3);
    i2c_recv(i2c, addr, resp, 2);
    g_assert_cmphex(resp[0], ==, cmd[1]);
    g_assert_cmphex(resp[1], ==, cmd[2]);
}


static void send_and_receive(void)
{
    uint16_t value;

    value = tmp105_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmpuint(value, ==, 0);

    /* reset */
    tmp105_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0);

    tmp105_set16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_LOW, 0x1234);
    tmp105_set16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_HIGH, 0x4231);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-machine n800 "
                    "-device tmp105,bus=i2c-bus.0,id=" TMP105_TEST_ID
                    ",address=0x49");
    i2c = omap_i2c_create(OMAP2_I2C_1_BASE);

    qtest_add_func("/tmp105/tx-rx", send_and_receive);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }
    g_free(i2c);

    return ret;
}
