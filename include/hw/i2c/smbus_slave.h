/*
 * QEMU SMBus device (slave) API
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

#ifndef HW_SMBUS_SLAVE_H
#define HW_SMBUS_SLAVE_H

#include "hw/i2c/i2c.h"

#define TYPE_SMBUS_DEVICE "smbus-device"
#define SMBUS_DEVICE(obj) \
     OBJECT_CHECK(SMBusDevice, (obj), TYPE_SMBUS_DEVICE)
#define SMBUS_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SMBusDeviceClass, (klass), TYPE_SMBUS_DEVICE)
#define SMBUS_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SMBusDeviceClass, (obj), TYPE_SMBUS_DEVICE)

typedef struct SMBusDevice SMBusDevice;

typedef struct SMBusDeviceClass
{
    I2CSlaveClass parent_class;

    /*
     * An operation with no data, special in SMBus.
     * This may be NULL, quick commands are ignore in that case.
     */
    void (*quick_cmd)(SMBusDevice *dev, uint8_t read);

    /*
     * We can't distinguish between a word write and a block write with
     * length 1, so pass the whole data block including the length byte
     * (if present).  The device is responsible figuring out what type of
     * command this is.
     * This may be NULL if no data is written to the device.  Writes
     * will be ignore in that case.
     */
    int (*write_data)(SMBusDevice *dev, uint8_t *buf, uint8_t len);

    /*
     * Likewise we can't distinguish between different reads, or even know
     * the length of the read until the read is complete, so read data a
     * byte at a time.  The device is responsible for adding the length
     * byte on block reads.  This call cannot fail, it should return
     * something, preferably 0xff if nothing is available.
     * This may be NULL if no data is read from the device.  Reads will
     * return 0xff in that case.
     */
    uint8_t (*receive_byte)(SMBusDevice *dev);
} SMBusDeviceClass;

#define SMBUS_DATA_MAX_LEN 34  /* command + len + 32 bytes of data.  */

struct SMBusDevice {
    /* The SMBus protocol is implemented on top of I2C.  */
    I2CSlave i2c;

    /* Remaining fields for internal use only.  */
    int32_t mode;
    int32_t data_len;
    uint8_t data_buf[SMBUS_DATA_MAX_LEN];
};

extern const VMStateDescription vmstate_smbus_device;

#define VMSTATE_SMBUS_DEVICE(_field, _state) {                       \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SMBusDevice),                               \
    .vmsd       = &vmstate_smbus_device,                             \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SMBusDevice), \
}

/*
 * Users should call this in their .needed functions to know if the
 * SMBus slave data needs to be transferred.
 */
bool smbus_vmstate_needed(SMBusDevice *dev);

#endif
