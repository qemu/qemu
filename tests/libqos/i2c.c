/*
 * QTest I2C driver
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
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

void i2c_read_block(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                       uint8_t *buf, uint16_t len)
{
    i2c_send(i2c, addr, &reg, 1);
    i2c_recv(i2c, addr, buf, len);
}

void i2c_write_block(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                     const uint8_t *buf, uint16_t len)
{
    uint8_t *cmd = g_malloc(len + 1);
    cmd[0] = reg;
    memcpy(&cmd[1], buf, len);
    i2c_send(i2c, addr, cmd, len + 1);
    g_free(cmd);
}

uint8_t i2c_get8(I2CAdapter *i2c, uint8_t addr, uint8_t reg)
{
    uint8_t resp[1];
    i2c_read_block(i2c, addr, reg, resp, sizeof(resp));
    return resp[0];
}

uint16_t i2c_get16(I2CAdapter *i2c, uint8_t addr, uint8_t reg)
{
    uint8_t resp[2];
    i2c_read_block(i2c, addr, reg, resp, sizeof(resp));
    return (resp[0] << 8) | resp[1];
}

void i2c_set8(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
              uint8_t value)
{
    i2c_write_block(i2c, addr, reg, &value, 1);
}

void i2c_set16(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
               uint16_t value)
{
    uint8_t data[2];

    data[0] = value >> 8;
    data[1] = value & 255;
    i2c_write_block(i2c, addr, reg, data, sizeof(data));
}
