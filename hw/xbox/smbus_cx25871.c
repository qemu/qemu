/*
 * QEMU SMBus Conexant CX25871 Video Encoder
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus.h"

typedef struct SMBusCX25871Device {
    SMBusDevice smbusdev;

    uint8_t registers[256];
} SMBusCX25871Device;

//#define DEBUG

static void cx_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("cx_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static void cx_send_byte(SMBusDevice *dev, uint8_t val)
{
#ifdef DEBUG
    printf("cx_send_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
}

static uint8_t cx_receive_byte(SMBusDevice *dev)
{
#ifdef DEBUG
    printf("cx_receive_byte: addr=0x%02x\n",
           dev->i2c.address);
#endif
    return 0;
}

static void cx_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusCX25871Device *cx = (SMBusCX25871Device *) dev;
#ifdef DEBUG
    printf("cx_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif

    memcpy(cx->registers+cmd, buf, MIN(len, 256-cmd));
}

static uint8_t cx_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusCX25871Device *cx = (SMBusCX25871Device *) dev;
    #ifdef DEBUG
        printf("cx_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
               dev->i2c.address, cmd, n);
    #endif
    
    return cx->registers[cmd];
}

static int smbus_cx_init(SMBusDevice *dev)
{
    return 0;
}

static void smbus_cx25871_class_initfn(ObjectClass *klass, void *data)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    sc->init = smbus_cx_init;
    sc->quick_cmd = cx_quick_cmd;
    sc->send_byte = cx_send_byte;
    sc->receive_byte = cx_receive_byte;
    sc->write_data = cx_write_data;
    sc->read_data = cx_read_data;
}

static TypeInfo smbus_cx25871_info = {
    .name = "smbus-cx25871",
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusCX25871Device),
    .class_init = smbus_cx25871_class_initfn,
};


static void smbus_cx25871_register_devices(void)
{
    type_register_static(&smbus_cx25871_info);
}

type_init(smbus_cx25871_register_devices)


void smbus_cx25871_init(i2c_bus *smbus, int address)
{
    DeviceState *cx;
    cx = qdev_create((BusState *)smbus, "smbus-cx25871");
    qdev_prop_set_uint8(cx, "address", address);
    qdev_init_nofail(cx);
}
