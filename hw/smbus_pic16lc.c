/*
 * QEMU SMBus PIC16LC System Monitor
 *
 * Copyright (c) 2011 espes
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


#define PIC16LC_REG_VER                 0x01
#define PIC16LC_REG_POWER               0x02
#define         PIC16LC_REG_POWER_RESET         0x01
#define         PIC16LC_REG_POWER_CYCLE         0x40
#define         PIC16LC_REG_POWER_SHUTDOWN      0x80
#define PIC16LC_REG_TRAYSTATE           0x03
#define PIC16LC_REG_AVPACK              0x04
#define         PIC16LC_REG_AVPACK_SCART        0x00
#define         PIC16LC_REG_AVPACK_HDTV         0x01
#define         PIC16LC_REG_AVPACK_VGA_SOG      0x02
#define         PIC16LC_REG_AVPACK_SVIDEO       0x04
#define         PIC16LC_REG_AVPACK_COMPOSITE    0x06
#define         PIC16LC_REG_AVPACK_VGA          0x07
#define PIC16LC_REG_FANMODE             0x05
#define PIC16LC_REG_FANSPEED            0x06
#define PIC16LC_REG_LEDMODE             0x07
#define PIC16LC_REG_LEDSEQ              0x08
#define PIC16LC_REG_CPUTEMP             0x09
#define PIC16LC_REG_BOARDTEMP           0x0a
#define PIC16LC_REG_TRAYEJECT           0x0c
#define PIC16LC_REG_INTACK		0x0d
#define PIC16LC_REG_INTSTATUS           0x11
#define		PIC16LC_REG_INTSTATUS_POWER		0x01
#define		PIC16LC_REG_INTSTATUS_TRAYCLOSED	0x02
#define		PIC16LC_REG_INTSTATUS_TRAYOPENING	0x04
#define		PIC16LC_REG_INTSTATUS_AVPACK_PLUG	0x08
#define		PIC16LC_REG_INTSTATUS_AVPACK_UNPLUG	0x10
#define		PIC16LC_REG_INTSTATUS_EJECT_BUTTON	0x20
#define		PIC16LC_REG_INTSTATUS_TRAYCLOSING	0x40
#define PIC16LC_REG_RESETONEJECT        0x19
#define PIC16LC_REG_INTEN               0x1a

static const char* pic_version_string = "P01";


#define DEBUG

typedef struct SMBusPIC16LCDevice {
    SMBusDevice smbusdev;
    int versionStringIndex;
} SMBusPIC16LCDevice;

static void pic_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("pic_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static void pic_send_byte(SMBusDevice *dev, uint8_t val)
{
    SMBusPIC16LCDevice *pic = (SMBusPIC16LCDevice *) dev;
#ifdef DEBUG
    printf("pic_send_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
}

static uint8_t pic_receive_byte(SMBusDevice *dev)
{
    SMBusPIC16LCDevice *pic = (SMBusPIC16LCDevice *) dev;
#ifdef DEBUG
    printf("pic_receive_byte: addr=0x%02x\n",
           dev->i2c.address);
#endif
    return 0;
}

static void pic_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusPIC16LCDevice *pic = (SMBusPIC16LCDevice *) dev;
#ifdef DEBUG
    printf("pic_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif

    switch(cmd) {
        case PIC16LC_REG_VER:
            //pic version string reset
            pic->versionStringIndex = buf[0];
            break;
        default:
            break;
    }
}

static uint8_t pic_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusPIC16LCDevice *pic = (SMBusPIC16LCDevice *) dev;
    #ifdef DEBUG
        printf("pic_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
               dev->i2c.address, cmd, n);
    #endif
    
    switch(cmd) {
        case PIC16LC_REG_VER:
            return pic_version_string[
                pic->versionStringIndex++%(sizeof(pic_version_string)-1)];
        case PIC16LC_REG_AVPACK:
            //pretend to ave a composite av pack plugged in
            return PIC16LC_REG_AVPACK_COMPOSITE;
        default:
            break;
    }
    
    return 0;
}

static int smbus_pic_init(SMBusDevice *dev)
{
    SMBusPIC16LCDevice *pic = (SMBusPIC16LCDevice *)dev;
    
    pic->versionStringIndex = 0;
    return 0;
}


static void smbus_pic_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    sc->init = smbus_pic_init;
    sc->quick_cmd = pic_quick_cmd;
    sc->send_byte = pic_send_byte;
    sc->receive_byte = pic_receive_byte;
    sc->write_data = pic_write_data;
    sc->read_data = pic_read_data;
}

static TypeInfo smbus_pic_info = {
    .name = "smbus-pic16lc",
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusPIC16LCDevice),
    .class_init = smbus_pic_class_initfn,
};



static void smbus_pic_register_devices(void)
{
    type_register_static(&smbus_pic_info);
}

type_init(smbus_pic_register_devices)


void smbus_pic16lc_init(i2c_bus *smbus, int address)
{
    DeviceState *pic;
    pic = qdev_create((BusState *)smbus, "smbus-pic16lc");
    qdev_prop_set_uint8(pic, "address", address);
    qdev_init_nofail(pic);
}
