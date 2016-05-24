/*
 * QTest testcase for the DS1338 RTC
 *
 * Copyright (c) 2013 Jean-Christophe Dubois
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/i2c.h"

#define IMX25_I2C_0_BASE 0x43F80000

#define DS1338_ADDR 0x68

static I2CAdapter *i2c;
static uint8_t addr;

static inline uint8_t bcd2bin(uint8_t x)
{
    return ((x) & 0x0f) + ((x) >> 4) * 10;
}

static void send_and_receive(void)
{
    uint8_t cmd[1];
    uint8_t resp[7];
    time_t now = time(NULL);
    struct tm *tm_ptr = gmtime(&now);

    /* reset the index in the RTC memory */
    cmd[0] = 0;
    i2c_send(i2c, addr, cmd, 1);

    /* retrieve the date */
    i2c_recv(i2c, addr, resp, 7);

    /* check retrieved time againt local time */
    g_assert_cmpuint(bcd2bin(resp[4]), == , tm_ptr->tm_mday);
    g_assert_cmpuint(bcd2bin(resp[5]), == , 1 + tm_ptr->tm_mon);
    g_assert_cmpuint(2000 + bcd2bin(resp[6]), == , 1900 + tm_ptr->tm_year);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-display none -machine imx25-pdk");
    i2c = imx_i2c_create(IMX25_I2C_0_BASE);
    addr = DS1338_ADDR;

    qtest_add_func("/ds1338/tx-rx", send_and_receive);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }
    g_free(i2c);

    return ret;
}
