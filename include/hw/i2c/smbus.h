#ifndef QEMU_SMBUS_H
#define QEMU_SMBUS_H

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

#include "hw/i2c/i2c.h"

#define TYPE_SMBUS_DEVICE "smbus-device"
#define SMBUS_DEVICE(obj) \
     OBJECT_CHECK(SMBusDevice, (obj), TYPE_SMBUS_DEVICE)
#define SMBUS_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SMBusDeviceClass, (klass), TYPE_SMBUS_DEVICE)
#define SMBUS_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SMBusDeviceClass, (obj), TYPE_SMBUS_DEVICE)

typedef struct SMBusDeviceClass
{
    I2CSlaveClass parent_class;
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
} SMBusDeviceClass;

struct SMBusDevice {
    /* The SMBus protocol is implemented on top of I2C.  */
    I2CSlave i2c;

    /* Remaining fields for internal use only.  */
    int mode;
    int data_len;
    uint8_t data_buf[34]; /* command + len + 32 bytes of data.  */
    uint8_t command;
};

/* Master device commands.  */
int smbus_quick_command(I2CBus *bus, uint8_t addr, int read);
int smbus_receive_byte(I2CBus *bus, uint8_t addr);
int smbus_send_byte(I2CBus *bus, uint8_t addr, uint8_t data);
int smbus_read_byte(I2CBus *bus, uint8_t addr, uint8_t command);
int smbus_write_byte(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t data);
int smbus_read_word(I2CBus *bus, uint8_t addr, uint8_t command);
int smbus_write_word(I2CBus *bus, uint8_t addr, uint8_t command, uint16_t data);

/*
 * Do a block transfer from an I2C device.  If recv_len is set, then the
 * first received byte is a length field and is used to know how much data
 * to receive.  Otherwise receive "len" bytes.  If send_cmd is set, send
 * the command byte first before receiving the data.
 */
int smbus_read_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                     int len, bool recv_len, bool send_cmd);

/*
 * Do a block transfer to an I2C device.  If send_len is set, send the
 * "len" value before the data.
 */
int smbus_write_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                      int len, bool send_len);

void smbus_eeprom_init_one(I2CBus *smbus, uint8_t address, uint8_t *eeprom_buf);
void smbus_eeprom_init(I2CBus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int size);

#endif
