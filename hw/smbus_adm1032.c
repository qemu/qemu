/*
 * QEMU SMBus ADM1032 Temperature Monitor
 *
 * Copyright (c) 2012 espes
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

#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus.h"

#define DEBUG

static uint8_t tm_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    #ifdef DEBUG
        printf("tm_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
               dev->i2c.address, cmd, n);
    #endif
    
    switch (cmd) {
        case 0x0:
        case 0x1:
            return 50;
        default:
            break;
    }

    return 0;
}

static int tm_init(SMBusDevice *dev)
{
    return 0;
}


static void smbus_adm1032_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    sc->init = tm_init;
    sc->read_data = tm_read_data;
}

static TypeInfo smbus_adm1032_info = {
    .name = "smbus-adm1032",
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusDevice),
    .class_init = smbus_adm1032_class_initfn,
};


static void smbus_adm1032_register_devices(void)
{
    type_register_static(&smbus_adm1032_info);
}

type_init(smbus_adm1032_register_devices)


void smbus_adm1032_init(i2c_bus *smbus, int address)
{
    DeviceState *tm;
    tm = qdev_create((BusState *)smbus, "smbus-adm1032");
    qdev_prop_set_uint8(tm, "address", address);
    qdev_init_nofail(tm);
}
