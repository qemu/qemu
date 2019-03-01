/*
 * QEMU SMBus host (master) emulation.
 *
 * This code emulates SMBus transactions from the master point of view,
 * it runs the individual I2C transaction to do the SMBus protocol
 * over I2C.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_master.h"

/* Master device commands.  */
int smbus_quick_command(I2CBus *bus, uint8_t addr, int read)
{
    if (i2c_start_transfer(bus, addr, read)) {
        return -1;
    }
    i2c_end_transfer(bus);
    return 0;
}

int smbus_receive_byte(I2CBus *bus, uint8_t addr)
{
    uint8_t data;

    if (i2c_start_transfer(bus, addr, 1)) {
        return -1;
    }
    data = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

int smbus_send_byte(I2CBus *bus, uint8_t addr, uint8_t data)
{
    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, data);
    i2c_end_transfer(bus);
    return 0;
}

int smbus_read_byte(I2CBus *bus, uint8_t addr, uint8_t command)
{
    uint8_t data;
    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, command);
    if (i2c_start_transfer(bus, addr, 1)) {
        i2c_end_transfer(bus);
        return -1;
    }
    data = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

int smbus_write_byte(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t data)
{
    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, command);
    i2c_send(bus, data);
    i2c_end_transfer(bus);
    return 0;
}

int smbus_read_word(I2CBus *bus, uint8_t addr, uint8_t command)
{
    uint16_t data;
    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, command);
    if (i2c_start_transfer(bus, addr, 1)) {
        i2c_end_transfer(bus);
        return -1;
    }
    data = i2c_recv(bus);
    data |= i2c_recv(bus) << 8;
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

int smbus_write_word(I2CBus *bus, uint8_t addr, uint8_t command, uint16_t data)
{
    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, command);
    i2c_send(bus, data & 0xff);
    i2c_send(bus, data >> 8);
    i2c_end_transfer(bus);
    return 0;
}

int smbus_read_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                     int len, bool recv_len, bool send_cmd)
{
    int rlen;
    int i;

    if (send_cmd) {
        if (i2c_start_transfer(bus, addr, 0)) {
            return -1;
        }
        i2c_send(bus, command);
    }
    if (i2c_start_transfer(bus, addr, 1)) {
        if (send_cmd) {
            i2c_end_transfer(bus);
        }
        return -1;
    }
    if (recv_len) {
        rlen = i2c_recv(bus);
    } else {
        rlen = len;
    }
    if (rlen > len) {
        rlen = 0;
    }
    for (i = 0; i < rlen; i++) {
        data[i] = i2c_recv(bus);
    }
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return rlen;
}

int smbus_write_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                      int len, bool send_len)
{
    int i;

    if (len > 32) {
        len = 32;
    }

    if (i2c_start_transfer(bus, addr, 0)) {
        return -1;
    }
    i2c_send(bus, command);
    if (send_len) {
        i2c_send(bus, len);
    }
    for (i = 0; i < len; i++) {
        i2c_send(bus, data[i]);
    }
    i2c_end_transfer(bus);
    return 0;
}
