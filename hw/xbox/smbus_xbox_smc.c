/*
 * QEMU SMBus Xbox System Management Controller
 *
 * Copyright (c) 2011 espes
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
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"

/*
 * Hardware is a PIC16LC
 * http://www.xbox-linux.org/wiki/PIC
 */

#define SMC_REG_VER                 0x01
#define SMC_REG_POWER               0x02
#define     SMC_REG_POWER_RESET         0x01
#define     SMC_REG_POWER_CYCLE         0x40
#define     SMC_REG_POWER_SHUTDOWN      0x80
#define SMC_REG_TRAYSTATE           0x03
#define SMC_REG_AVPACK              0x04
#define     SMC_REG_AVPACK_SCART        0x00
#define     SMC_REG_AVPACK_HDTV         0x01
#define     SMC_REG_AVPACK_VGA_SOG      0x02
#define     SMC_REG_AVPACK_SVIDEO       0x04
#define     SMC_REG_AVPACK_COMPOSITE    0x06
#define     SMC_REG_AVPACK_VGA          0x07
#define SMC_REG_FANMODE             0x05
#define SMC_REG_FANSPEED            0x06
#define SMC_REG_LEDMODE             0x07
#define SMC_REG_LEDSEQ              0x08
#define SMC_REG_CPUTEMP             0x09
#define SMC_REG_BOARDTEMP           0x0a
#define SMC_REG_TRAYEJECT           0x0c
#define SMC_REG_INTACK              0x0d
#define SMC_REG_INTSTATUS           0x11
#define     SMC_REG_INTSTATUS_POWER         0x01
#define     SMC_REG_INTSTATUS_TRAYCLOSED    0x02
#define     SMC_REG_INTSTATUS_TRAYOPENING   0x04
#define     SMC_REG_INTSTATUS_AVPACK_PLUG   0x08
#define     SMC_REG_INTSTATUS_AVPACK_UNPLUG 0x10
#define     SMC_REG_INTSTATUS_EJECT_BUTTON  0x20
#define     SMC_REG_INTSTATUS_TRAYCLOSING   0x40
#define SMC_REG_RESETONEJECT        0x19
#define SMC_REG_INTEN               0x1a
#define SMC_REG_SCRATCH             0x1b
#define     SMC_REG_SCRATCH_SHORT_ANIMATION 0x04

static const char* smc_version_string = "P01";


//#define DEBUG

typedef struct SMBusSMCDevice {
    SMBusDevice smbusdev;
    int version_string_index;
    uint8_t scratch_reg;
} SMBusSMCDevice;

static void smc_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("smc_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static void smc_send_byte(SMBusDevice *dev, uint8_t val)
{
#ifdef DEBUG
    printf("smc_send_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
}

static uint8_t smc_receive_byte(SMBusDevice *dev)
{
#ifdef DEBUG
    printf("smc_receive_byte: addr=0x%02x\n",
           dev->i2c.address);
#endif
    return 0;
}

static void smc_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *) dev;
#ifdef DEBUG
    printf("smc_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);
#endif

    switch(cmd) {
    case SMC_REG_VER:
        /* version string reset */
        smc->version_string_index = buf[0];
        break;

    case SMC_REG_POWER:
        if (buf[0] & (SMC_REG_POWER_RESET | SMC_REG_POWER_CYCLE))
            qemu_system_reset_request();
        else if (buf[0] & SMC_REG_POWER_SHUTDOWN)
            qemu_system_shutdown_request();
        break;

    case SMC_REG_SCRATCH:
        smc->scratch_reg = buf[0];
        break;

    /* challenge response
     * (http://www.xbox-linux.org/wiki/PIC_Challenge_Handshake_Sequence) */
    case 0x20:
        break;
    case 0x21:
        break;

    default:
        break;
    }
}

static uint8_t smc_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *) dev;
    #ifdef DEBUG
        printf("smc_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
               dev->i2c.address, cmd, n);
    #endif

    switch(cmd) {
    case SMC_REG_VER:
        return smc_version_string[
            smc->version_string_index++%(sizeof(smc_version_string)-1)];

    case SMC_REG_AVPACK:
        /* pretend to have a composite av pack plugged in */
        return SMC_REG_AVPACK_COMPOSITE;

    case SMC_REG_SCRATCH:
        return smc->scratch_reg;

    /* challenge request:
     * must be non-0 */
    case 0x1c:
        return 0x52;
    case 0x1d:
        return 0x72;
    case 0x1e:
        return 0xea;
    case 0x1f:
        return 0x46;

    default:
        break;
    }

    return 0;
}

static int smbus_smc_init(SMBusDevice *dev)
{
    QemuOpts *opts;
    SMBusSMCDevice *smc = (SMBusSMCDevice *)dev;

    smc->version_string_index = 0;
    smc->scratch_reg = 0;

    opts = qemu_opts_find(qemu_find_opts("machine"), NULL);
    if (opts && qemu_opt_get_bool(opts, "short_animation", 0)) {
        smc->scratch_reg = SMC_REG_SCRATCH_SHORT_ANIMATION;
    }

    return 0;
}


static void smbus_smc_class_initfn(ObjectClass *klass, void *data)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    sc->init = smbus_smc_init;
    sc->quick_cmd = smc_quick_cmd;
    sc->send_byte = smc_send_byte;
    sc->receive_byte = smc_receive_byte;
    sc->write_data = smc_write_data;
    sc->read_data = smc_read_data;
}

static TypeInfo smbus_smc_info = {
    .name = "smbus-xbox-smc",
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusSMCDevice),
    .class_init = smbus_smc_class_initfn,
};



static void smbus_smc_register_devices(void)
{
    type_register_static(&smbus_smc_info);
}

type_init(smbus_smc_register_devices)


void smbus_xbox_smc_init(i2c_bus *smbus, int address)
{
    DeviceState *smc;
    smc = qdev_create((BusState *)smbus, "smbus-xbox-smc");
    qdev_prop_set_uint8(smc, "address", address);
    qdev_init_nofail(smc);
}
