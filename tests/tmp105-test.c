/*
 * QTest testcase for the TMP105 temperature sensor
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "libqtest.h"
#include "libqos/i2c.h"
#include "hw/misc/tmp105_regs.h"

#include <glib.h>

#define OMAP2_I2C_1_BASE 0x48070000

#define N8X0_ADDR 0x48

static I2CAdapter *i2c;
static uint8_t addr;

static void send_and_receive(void)
{
    uint8_t cmd[3];
    uint8_t resp[2];

    cmd[0] = TMP105_REG_TEMPERATURE;
    i2c_send(i2c, addr, cmd, 1);
    i2c_recv(i2c, addr, resp, 2);
    g_assert_cmpuint(((uint16_t)resp[0] << 8) | resp[1], ==, 0);

    cmd[0] = TMP105_REG_CONFIG;
    cmd[1] = 0x0; /* matches the reset value */
    i2c_send(i2c, addr, cmd, 2);
    i2c_recv(i2c, addr, resp, 1);
    g_assert_cmphex(resp[0], ==, cmd[1]);

    cmd[0] = TMP105_REG_T_LOW;
    cmd[1] = 0x12;
    cmd[2] = 0x34;
    i2c_send(i2c, addr, cmd, 3);
    i2c_recv(i2c, addr, resp, 2);
    g_assert_cmphex(resp[0], ==, cmd[1]);
    g_assert_cmphex(resp[1], ==, cmd[2]);

    cmd[0] = TMP105_REG_T_HIGH;
    cmd[1] = 0x42;
    cmd[2] = 0x31;
    i2c_send(i2c, addr, cmd, 3);
    i2c_recv(i2c, addr, resp, 2);
    g_assert_cmphex(resp[0], ==, cmd[1]);
    g_assert_cmphex(resp[1], ==, cmd[2]);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-machine n800");
    i2c = omap_i2c_create(OMAP2_I2C_1_BASE);
    addr = N8X0_ADDR;

    qtest_add_func("/tmp105/tx-rx", send_and_receive);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }
    g_free(i2c);

    return ret;
}
