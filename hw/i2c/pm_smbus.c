/*
 * PC SMBus implementation
 * splitted from acpi.c
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/i2c/smbus.h"

/* no save/load? */

#define SMBHSTSTS       0x00
#define SMBHSTCNT       0x02
#define SMBHSTCMD       0x03
#define SMBHSTADD       0x04
#define SMBHSTDAT0      0x05
#define SMBHSTDAT1      0x06
#define SMBBLKDAT       0x07

//#define DEBUG

#ifdef DEBUG
# define SMBUS_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define SMBUS_DPRINTF(format, ...)     do { } while (0)
#endif


static void smb_transaction(PMSMBus *s)
{
    uint8_t prot = (s->smb_ctl >> 2) & 0x07;
    uint8_t read = s->smb_addr & 0x01;
    uint8_t cmd = s->smb_cmd;
    uint8_t addr = s->smb_addr >> 1;
    i2c_bus *bus = s->smbus;

    SMBUS_DPRINTF("SMBus trans addr=0x%02x prot=0x%02x\n", addr, prot);
    switch(prot) {
    case 0x0:
        smbus_quick_command(bus, addr, read);
        break;
    case 0x1:
        if (read) {
            s->smb_data0 = smbus_receive_byte(bus, addr);
        } else {
            smbus_send_byte(bus, addr, cmd);
        }
        break;
    case 0x2:
        if (read) {
            s->smb_data0 = smbus_read_byte(bus, addr, cmd);
        } else {
            smbus_write_byte(bus, addr, cmd, s->smb_data0);
        }
        break;
    case 0x3:
        if (read) {
            uint16_t val;
            val = smbus_read_word(bus, addr, cmd);
            s->smb_data0 = val;
            s->smb_data1 = val >> 8;
        } else {
            smbus_write_word(bus, addr, cmd, (s->smb_data1 << 8) | s->smb_data0);
        }
        break;
    case 0x5:
        if (read) {
            s->smb_data0 = smbus_read_block(bus, addr, cmd, s->smb_data);
        } else {
            smbus_write_block(bus, addr, cmd, s->smb_data, s->smb_data0);
        }
        break;
    default:
        goto error;
    }
    return;

  error:
    s->smb_stat |= 0x04;
}

static void smb_ioport_writeb(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    PMSMBus *s = opaque;

    SMBUS_DPRINTF("SMB writeb port=0x%04x val=0x%02x\n", addr, val);
    switch(addr) {
    case SMBHSTSTS:
        s->smb_stat = 0;
        s->smb_index = 0;
        break;
    case SMBHSTCNT:
        s->smb_ctl = val;
        if (val & 0x40)
            smb_transaction(s);
        break;
    case SMBHSTCMD:
        s->smb_cmd = val;
        break;
    case SMBHSTADD:
        s->smb_addr = val;
        break;
    case SMBHSTDAT0:
        s->smb_data0 = val;
        break;
    case SMBHSTDAT1:
        s->smb_data1 = val;
        break;
    case SMBBLKDAT:
        s->smb_data[s->smb_index++] = val;
        if (s->smb_index > 31)
            s->smb_index = 0;
        break;
    default:
        break;
    }
}

static uint64_t smb_ioport_readb(void *opaque, hwaddr addr, unsigned width)
{
    PMSMBus *s = opaque;
    uint32_t val;

    switch(addr) {
    case SMBHSTSTS:
        val = s->smb_stat;
        break;
    case SMBHSTCNT:
        s->smb_index = 0;
        val = s->smb_ctl & 0x1f;
        break;
    case SMBHSTCMD:
        val = s->smb_cmd;
        break;
    case SMBHSTADD:
        val = s->smb_addr;
        break;
    case SMBHSTDAT0:
        val = s->smb_data0;
        break;
    case SMBHSTDAT1:
        val = s->smb_data1;
        break;
    case SMBBLKDAT:
        val = s->smb_data[s->smb_index++];
        if (s->smb_index > 31)
            s->smb_index = 0;
        break;
    default:
        val = 0;
        break;
    }
    SMBUS_DPRINTF("SMB readb port=0x%04x val=0x%02x\n", addr, val);
    return val;
}

static const MemoryRegionOps pm_smbus_ops = {
    .read = smb_ioport_readb,
    .write = smb_ioport_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void pm_smbus_init(DeviceState *parent, PMSMBus *smb)
{
    smb->smbus = i2c_init_bus(parent, "i2c");
    memory_region_init_io(&smb->io, &pm_smbus_ops, smb, "pm-smbus", 64);
}
