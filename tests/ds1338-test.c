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

#define DS1338_ADDR 0x68

static inline uint8_t bcd2bin(uint8_t x)
{
    return ((x) & 0x0f) + ((x) >> 4) * 10;
}

static void send_and_receive(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    uint8_t resp[7];
    time_t now = time(NULL);
    struct tm *tm_ptr = gmtime(&now);

    i2c_read_block(i2cdev, 0, resp, sizeof(resp));

    /* check retrieved time againt local time */
    g_assert_cmpuint(bcd2bin(resp[4]), == , tm_ptr->tm_mday);
    g_assert_cmpuint(bcd2bin(resp[5]), == , 1 + tm_ptr->tm_mon);
    g_assert_cmpuint(2000 + bcd2bin(resp[6]), == , 1900 + tm_ptr->tm_year);
}

static void ds1338_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x68"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { DS1338_ADDR });

    qos_node_create_driver("ds1338", i2c_device_create);
    qos_node_consumes("ds1338", "i2c-bus", &opts);
    qos_add_test("tx-rx", "ds1338", send_and_receive, NULL);
}
libqos_init(ds1338_register_nodes);
