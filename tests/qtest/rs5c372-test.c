/*
 * QTest testcase for the RS5C372 RTC
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * Based on ds1338-test.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "libqos/i2c.h"

#define RS5C372_ADDR 0x32

static void rs5c372_read_date(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = obj;

    uint8_t resp[0x10];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);

    i2c_read_block(i2cdev, 0, resp, sizeof(resp));

    /* check retrieved time against local time */
    g_assert_cmpuint(from_bcd(resp[5]), == , utc->tm_mday);
    g_assert_cmpuint(from_bcd(resp[6]), == , 1 + utc->tm_mon);
    g_assert_cmpuint(2000 + from_bcd(resp[7]), == , 1900 + utc->tm_year);
}

static void rs5c372_register_nodes(void)
{
    QOSGraphEdgeOptions opts = { };
    add_qi2c_address(&opts, &(QI2CAddress) { RS5C372_ADDR });

    qos_node_create_driver("rs5c372", i2c_device_create);
    qos_node_consumes("rs5c372", "i2c-bus", &opts);
    qos_add_test("read_date", "rs5c372", rs5c372_read_date, NULL);
}

libqos_init(rs5c372_register_nodes);
