/*
 * QEMU SMBus EEPROM device
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

#include "hw.h"
#include "i2c.h"
#include "smbus.h"

//#define DEBUG

typedef struct SMBusEEPROMDevice {
    SMBusDevice smbusdev;
    uint8_t *data;
    uint8_t offset;
} SMBusEEPROMDevice;

static void eeprom_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("eeprom_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static void eeprom_send_byte(SMBusDevice *dev, uint8_t val)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
#ifdef DEBUG
    printf("eeprom_send_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
    eeprom->offset = val;
}

static uint8_t eeprom_receive_byte(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    uint8_t val = eeprom->data[eeprom->offset++];
#ifdef DEBUG
    printf("eeprom_receive_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
    return val;
}

static void eeprom_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    int n;
#ifdef DEBUG
    printf("eeprom_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif
    /* An page write operation is not a valid SMBus command.
       It is a block write without a length byte.  Fortunately we
       get the full block anyway.  */
    /* TODO: Should this set the current location?  */
    if (cmd + len > 256)
        n = 256 - cmd;
    else
        n = len;
    memcpy(eeprom->data + cmd, buf, n);
    len -= n;
    if (len)
        memcpy(eeprom->data, buf + n, len);
}

static uint8_t eeprom_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    /* If this is the first byte then set the current position.  */
    if (n == 0)
        eeprom->offset = cmd;
    /* As with writes, we implement block reads without the
       SMBus length byte.  */
    return eeprom_receive_byte(dev);
}

static void smbus_eeprom_init(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *)dev;

    /* FIXME: Should be a blob rather than a ptr.  */
    eeprom->data = qdev_get_prop_ptr(&dev->i2c.qdev, "data");
    eeprom->offset = 0;
}

static SMBusDeviceInfo smbus_eeprom_info = {
    .init = smbus_eeprom_init,
    .quick_cmd = eeprom_quick_cmd,
    .send_byte = eeprom_send_byte,
    .receive_byte = eeprom_receive_byte,
    .write_data = eeprom_write_data,
    .read_data = eeprom_read_data
};

static void smbus_eeprom_register_devices(void)
{
    smbus_register_device("smbus-eeprom", sizeof(SMBusEEPROMDevice),
                          &smbus_eeprom_info);
}

device_init(smbus_eeprom_register_devices)
