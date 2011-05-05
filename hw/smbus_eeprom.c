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
    void *data;
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
    uint8_t *data = eeprom->data;
    uint8_t val = data[eeprom->offset++];
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

static int smbus_eeprom_initfn(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *)dev;

    eeprom->offset = 0;
    return 0;
}

static SMBusDeviceInfo smbus_eeprom_info = {
    .i2c.qdev.name = "smbus-eeprom",
    .i2c.qdev.size = sizeof(SMBusEEPROMDevice),
    .i2c.qdev.props = (Property[]) {
        DEFINE_PROP_PTR("data", SMBusEEPROMDevice, data),
        DEFINE_PROP_END_OF_LIST(),
    },
    .init = smbus_eeprom_initfn,
    .quick_cmd = eeprom_quick_cmd,
    .send_byte = eeprom_send_byte,
    .receive_byte = eeprom_receive_byte,
    .write_data = eeprom_write_data,
    .read_data = eeprom_read_data
};

static void smbus_eeprom_register_devices(void)
{
    smbus_register_device(&smbus_eeprom_info);
}

device_init(smbus_eeprom_register_devices)

void smbus_eeprom_init(i2c_bus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int eeprom_spd_size)
{
    int i;
    uint8_t *eeprom_buf = qemu_mallocz(8 * 256); /* XXX: make this persistent */
    if (eeprom_spd_size > 0) {
        memcpy(eeprom_buf, eeprom_spd, eeprom_spd_size);
    }

    for (i = 0; i < nb_eeprom; i++) {
        DeviceState *eeprom;
        eeprom = qdev_create((BusState *)smbus, "smbus-eeprom");
        qdev_prop_set_uint8(eeprom, "address", 0x50 + i);
        qdev_prop_set_ptr(eeprom, "data", eeprom_buf + (i * 256));
        qdev_init_nofail(eeprom);
    }
}
