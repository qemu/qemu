/*
 * QTest I2C driver
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "libqos/i2c.h"
#include "libqtest.h"

void i2c_send(I2CAdapter *i2c, uint8_t addr,
              const uint8_t *buf, uint16_t len)
{
    i2c->send(i2c, addr, buf, len);
}

void i2c_recv(I2CAdapter *i2c, uint8_t addr,
              uint8_t *buf, uint16_t len)
{
    i2c->recv(i2c, addr, buf, len);
}
