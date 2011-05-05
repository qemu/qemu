/*
 * QEMU SMBus API
 *
 * Copyright (c) 2007 Arastra, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "i2c.h"

struct SMBusDevice {
    /* The SMBus protocol is implemented on top of I2C.  */
    i2c_slave i2c;

    /* Remaining fields for internal use only.  */
    int mode;
    int data_len;
    uint8_t data_buf[34]; /* command + len + 32 bytes of data.  */
    uint8_t command;
};

typedef struct {
    I2CSlaveInfo i2c;
    int (*init)(SMBusDevice *dev);
    void (*quick_cmd)(SMBusDevice *dev, uint8_t read);
    void (*send_byte)(SMBusDevice *dev, uint8_t val);
    uint8_t (*receive_byte)(SMBusDevice *dev);
    /* We can't distinguish between a word write and a block write with
       length 1, so pass the whole data block including the length byte
       (if present).  The device is responsible figuring out what type of
       command  this is.  */
    void (*write_data)(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len);
    /* Likewise we can't distinguish between different reads, or even know
       the length of the read until the read is complete, so read data a
       byte at a time.  The device is responsible for adding the length
       byte on block reads.  */
    uint8_t (*read_data)(SMBusDevice *dev, uint8_t cmd, int n);
} SMBusDeviceInfo;

void smbus_register_device(SMBusDeviceInfo *info);

/* Master device commands.  */
void smbus_quick_command(i2c_bus *bus, uint8_t addr, int read);
uint8_t smbus_receive_byte(i2c_bus *bus, uint8_t addr);
void smbus_send_byte(i2c_bus *bus, uint8_t addr, uint8_t data);
uint8_t smbus_read_byte(i2c_bus *bus, uint8_t addr, uint8_t command);
void smbus_write_byte(i2c_bus *bus, uint8_t addr, uint8_t command, uint8_t data);
uint16_t smbus_read_word(i2c_bus *bus, uint8_t addr, uint8_t command);
void smbus_write_word(i2c_bus *bus, uint8_t addr, uint8_t command, uint16_t data);
int smbus_read_block(i2c_bus *bus, uint8_t addr, uint8_t command, uint8_t *data);
void smbus_write_block(i2c_bus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                       int len);

void smbus_eeprom_init(i2c_bus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int size);
