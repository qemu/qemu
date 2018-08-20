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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/i2c/smbus.h"

#define SMBHSTSTS       0x00
#define SMBHSTCNT       0x02
#define SMBHSTCMD       0x03
#define SMBHSTADD       0x04
#define SMBHSTDAT0      0x05
#define SMBHSTDAT1      0x06
#define SMBBLKDAT       0x07

#define STS_HOST_BUSY   (1 << 0)
#define STS_INTR        (1 << 1)
#define STS_DEV_ERR     (1 << 2)
#define STS_BUS_ERR     (1 << 3)
#define STS_FAILED      (1 << 4)
#define STS_SMBALERT    (1 << 5)
#define STS_INUSE_STS   (1 << 6)
#define STS_BYTE_DONE   (1 << 7)
/* Signs of successfully transaction end :
*  ByteDoneStatus = 1 (STS_BYTE_DONE) and INTR = 1 (STS_INTR )
*/

#define CTL_INTREN      (1 << 0)
#define CTL_KILL        (1 << 1)
#define CTL_LAST_BYTE   (1 << 5)
#define CTL_START       (1 << 6)
#define CTL_PEC_EN      (1 << 7)
#define CTL_RETURN_MASK 0x1f

#define PROT_QUICK          0
#define PROT_BYTE           1
#define PROT_BYTE_DATA      2
#define PROT_WORD_DATA      3
#define PROT_PROC_CALL      4
#define PROT_BLOCK_DATA     5
#define PROT_I2C_BLOCK_DATA 6

/*#define DEBUG*/

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
    I2CBus *bus = s->smbus;
    int ret;

    assert(s->smb_stat & STS_HOST_BUSY);
    s->smb_stat &= ~STS_HOST_BUSY;

    SMBUS_DPRINTF("SMBus trans addr=0x%02x prot=0x%02x\n", addr, prot);
    /* Transaction isn't exec if STS_DEV_ERR bit set */
    if ((s->smb_stat & STS_DEV_ERR) != 0)  {
        goto error;
    }

    switch(prot) {
    case PROT_QUICK:
        ret = smbus_quick_command(bus, addr, read);
        goto done;
    case PROT_BYTE:
        if (read) {
            ret = smbus_receive_byte(bus, addr);
            goto data8;
        } else {
            ret = smbus_send_byte(bus, addr, cmd);
            goto done;
        }
    case PROT_BYTE_DATA:
        if (read) {
            ret = smbus_read_byte(bus, addr, cmd);
            goto data8;
        } else {
            ret = smbus_write_byte(bus, addr, cmd, s->smb_data0);
            goto done;
        }
        break;
    case PROT_WORD_DATA:
        if (read) {
            ret = smbus_read_word(bus, addr, cmd);
            goto data16;
        } else {
            ret = smbus_write_word(bus, addr, cmd,
                                   (s->smb_data1 << 8) | s->smb_data0);
            goto done;
        }
        break;
    case PROT_I2C_BLOCK_DATA:
        if (read) {
            ret = smbus_read_block(bus, addr, cmd, s->smb_data);
            goto data8;
        } else {
            ret = smbus_write_block(bus, addr, cmd, s->smb_data, s->smb_data0);
            goto done;
        }
        break;
    default:
        goto error;
    }
    abort();

data16:
    if (ret < 0) {
        goto error;
    }
    s->smb_data1 = ret >> 8;
data8:
    if (ret < 0) {
        goto error;
    }
    s->smb_data0 = ret;
done:
    if (ret < 0) {
        goto error;
    }
    s->smb_stat |= STS_BYTE_DONE | STS_INTR;
    return;

error:
    s->smb_stat |= STS_DEV_ERR;
    return;

}

static void smb_transaction_start(PMSMBus *s)
{
    /* Do not execute immediately the command ; it will be
     * executed when guest will read SMB_STAT register */
    s->smb_stat |= STS_HOST_BUSY;
}

static void smb_ioport_writeb(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    PMSMBus *s = opaque;

    SMBUS_DPRINTF("SMB writeb port=0x%04" HWADDR_PRIx
                  " val=0x%02" PRIx64 "\n", addr, val);
    switch(addr) {
    case SMBHSTSTS:
        s->smb_stat = (~(val & 0xff)) & s->smb_stat;
        s->smb_index = 0;
        break;
    case SMBHSTCNT:
        s->smb_ctl = val;
        if (s->smb_ctl & CTL_START) {
            smb_transaction_start(s);
        }
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
        if (s->smb_stat & STS_HOST_BUSY) {
            /* execute command now */
            smb_transaction(s);
        }
        break;
    case SMBHSTCNT:
        s->smb_index = 0;
        val = s->smb_ctl & CTL_RETURN_MASK;
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
    SMBUS_DPRINTF("SMB readb port=0x%04" HWADDR_PRIx " val=0x%02x\n",
                  addr, val);

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
    memory_region_init_io(&smb->io, OBJECT(parent), &pm_smbus_ops, smb,
                          "pm-smbus", 64);
}
