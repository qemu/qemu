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

void i2c_send(QI2CDevice *i2cdev, const uint8_t *buf, uint16_t len)
{
    i2cdev->bus->send(i2cdev->bus, i2cdev->addr, buf, len);
}

void i2c_recv(QI2CDevice *i2cdev, uint8_t *buf, uint16_t len)
{
    i2cdev->bus->recv(i2cdev->bus, i2cdev->addr, buf, len);
}

void i2c_read_block(QI2CDevice *i2cdev, uint8_t reg,
                    uint8_t *buf, uint16_t len)
{
    i2c_send(i2cdev, &reg, 1);
    i2c_recv(i2cdev, buf, len);
}

void i2c_write_block(QI2CDevice *i2cdev, uint8_t reg,
                     const uint8_t *buf, uint16_t len)
{
    uint8_t *cmd = g_malloc(len + 1);
    cmd[0] = reg;
    memcpy(&cmd[1], buf, len);
    i2c_send(i2cdev, cmd, len + 1);
    g_free(cmd);
}

uint8_t i2c_get8(QI2CDevice *i2cdev, uint8_t reg)
{
    uint8_t resp[1];
    i2c_read_block(i2cdev, reg, resp, sizeof(resp));
    return resp[0];
}

uint16_t i2c_get16(QI2CDevice *i2cdev, uint8_t reg)
{
    uint8_t resp[2];
    i2c_read_block(i2cdev, reg, resp, sizeof(resp));
    return (resp[0] << 8) | resp[1];
}

void i2c_set8(QI2CDevice *i2cdev, uint8_t reg, uint8_t value)
{
    i2c_write_block(i2cdev, reg, &value, 1);
}

void i2c_set16(QI2CDevice *i2cdev, uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = value >> 8;
    data[1] = value & 255;
    i2c_write_block(i2cdev, reg, data, sizeof(data));
}

void *i2c_device_create(void *i2c_bus, QGuestAllocator *alloc, void *addr)
{
    QI2CDevice *i2cdev = g_new0(QI2CDevice, 1);

    i2cdev->bus = i2c_bus;
    if (addr) {
        i2cdev->addr = ((QI2CAddress *)addr)->addr;
    }
    return &i2cdev->obj;
}

void add_qi2c_address(QOSGraphEdgeOptions *opts, QI2CAddress *addr)
{
    g_assert(addr);

    opts->arg = addr;
    opts->size_arg = sizeof(QI2CAddress);
}
