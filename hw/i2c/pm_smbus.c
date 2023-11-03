/*
 * PC SMBus implementation
 * split from acpi.c
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
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
#include "hw/boards.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/i2c/smbus_master.h"
#include "migration/vmstate.h"
#include "trace.h"

#define SMBHSTSTS       0x00
#define SMBHSTCNT       0x02
#define SMBHSTCMD       0x03
#define SMBHSTADD       0x04
#define SMBHSTDAT0      0x05
#define SMBHSTDAT1      0x06
#define SMBBLKDAT       0x07
#define SMBAUXCTL       0x0d

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
#define PROT_I2C_BLOCK_READ 6

#define AUX_PEC       (1 << 0)
#define AUX_BLK       (1 << 1)
#define AUX_MASK      0x3

static void smb_transaction(PMSMBus *s)
{
    uint8_t prot = (s->smb_ctl >> 2) & 0x07;
    uint8_t read = s->smb_addr & 0x01;
    uint8_t cmd = s->smb_cmd;
    uint8_t addr = s->smb_addr >> 1;
    I2CBus *bus = s->smbus;
    int ret;

    trace_smbus_transaction(addr, prot);
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
    case PROT_I2C_BLOCK_READ:
        /* According to the Linux i2c-i801 driver:
         *   NB: page 240 of ICH5 datasheet shows that the R/#W
         *   bit should be cleared here, even when reading.
         *   However if SPD Write Disable is set (Lynx Point and later),
         *   the read will fail if we don't set the R/#W bit.
         * So at least Linux may or may not set the read bit here.
         * So just ignore the read bit for this command.
         */
        if (i2c_start_send(bus, addr)) {
            goto error;
        }
        ret = i2c_send(bus, s->smb_data1);
        if (ret) {
            goto error;
        }
        if (i2c_start_recv(bus, addr)) {
            goto error;
        }
        s->in_i2c_block_read = true;
        s->smb_blkdata = i2c_recv(s->smbus);
        s->op_done = false;
        s->smb_stat |= STS_HOST_BUSY | STS_BYTE_DONE;
        goto out;

    case PROT_BLOCK_DATA:
        if (read) {
            ret = smbus_read_block(bus, addr, cmd, s->smb_data,
                                   sizeof(s->smb_data), !s->i2c_enable,
                                   !s->i2c_enable);
            if (ret < 0) {
                goto error;
            }
            s->smb_index = 0;
            s->op_done = false;
            if (s->smb_auxctl & AUX_BLK) {
                s->smb_stat |= STS_INTR;
            } else {
                s->smb_blkdata = s->smb_data[0];
                s->smb_stat |= STS_HOST_BUSY | STS_BYTE_DONE;
            }
            s->smb_data0 = ret;
            goto out;
        } else {
            if (s->smb_auxctl & AUX_BLK) {
                if (s->smb_index != s->smb_data0) {
                    s->smb_index = 0;
                    goto error;
                }
                /* Data is already all written to the queue, just do
                   the operation. */
                s->smb_index = 0;
                ret = smbus_write_block(bus, addr, cmd, s->smb_data,
                                        s->smb_data0, !s->i2c_enable);
                if (ret < 0) {
                    goto error;
                }
                s->op_done = true;
                s->smb_stat |= STS_INTR;
                s->smb_stat &= ~STS_HOST_BUSY;
            } else {
                s->op_done = false;
                s->smb_stat |= STS_HOST_BUSY | STS_BYTE_DONE;
                s->smb_data[0] = s->smb_blkdata;
                s->smb_index = 0;
            }
            goto out;
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
    s->smb_stat |= STS_INTR;
out:
    return;

error:
    s->smb_stat |= STS_DEV_ERR;
    return;
}

static void smb_transaction_start(PMSMBus *s)
{
    if (s->smb_ctl & CTL_INTREN) {
        smb_transaction(s);
        s->start_transaction_on_status_read = false;
    } else {
        /* Do not execute immediately the command; it will be
         * executed when guest will read SMB_STAT register.  This
         * is to work around a bug in AMIBIOS (that is working
         * around another bug in some specific hardware) where
         * it waits for STS_HOST_BUSY to be set before waiting
         * checking for status.  If STS_HOST_BUSY doesn't get
         * set, it gets stuck. */
        s->smb_stat |= STS_HOST_BUSY;
        s->start_transaction_on_status_read = true;
    }
}

static bool
smb_irq_value(PMSMBus *s)
{
    return ((s->smb_stat & ~STS_HOST_BUSY) != 0) && (s->smb_ctl & CTL_INTREN);
}

static bool
smb_byte_by_byte(PMSMBus *s)
{
    if (s->op_done) {
        return false;
    }
    if (s->in_i2c_block_read) {
        return true;
    }
    return !(s->smb_auxctl & AUX_BLK);
}

static void smb_ioport_writeb(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    PMSMBus *s = opaque;
    uint8_t clear_byte_done;

    trace_smbus_ioport_writeb(addr, val);
    switch(addr) {
    case SMBHSTSTS:
        clear_byte_done = s->smb_stat & val & STS_BYTE_DONE;
        s->smb_stat &= ~(val & ~STS_HOST_BUSY);
        if (clear_byte_done && smb_byte_by_byte(s)) {
            uint8_t read = s->smb_addr & 0x01;

            if (s->in_i2c_block_read) {
                /* See comment below PROT_I2C_BLOCK_READ above. */
                read = 1;
            }

            s->smb_index++;
            if (s->smb_index >= PM_SMBUS_MAX_MSG_SIZE) {
                s->smb_index = 0;
            }
            if (!read && s->smb_index == s->smb_data0) {
                uint8_t prot = (s->smb_ctl >> 2) & 0x07;
                uint8_t cmd = s->smb_cmd;
                uint8_t smb_addr = s->smb_addr >> 1;
                int ret;

                if (prot == PROT_I2C_BLOCK_READ) {
                    s->smb_stat |= STS_DEV_ERR;
                    goto out;
                }

                ret = smbus_write_block(s->smbus, smb_addr, cmd, s->smb_data,
                                        s->smb_data0, !s->i2c_enable);
                if (ret < 0) {
                    s->smb_stat |= STS_DEV_ERR;
                    goto out;
                }
                s->op_done = true;
                s->smb_stat |= STS_INTR;
                s->smb_stat &= ~STS_HOST_BUSY;
            } else if (!read) {
                s->smb_data[s->smb_index] = s->smb_blkdata;
                s->smb_stat |= STS_BYTE_DONE;
            } else if (s->smb_ctl & CTL_LAST_BYTE) {
                s->op_done = true;
                if (s->in_i2c_block_read) {
                    s->in_i2c_block_read = false;
                    s->smb_blkdata = i2c_recv(s->smbus);
                    i2c_nack(s->smbus);
                    i2c_end_transfer(s->smbus);
                } else {
                    s->smb_blkdata = s->smb_data[s->smb_index];
                }
                s->smb_index = 0;
                s->smb_stat |= STS_INTR;
                s->smb_stat &= ~STS_HOST_BUSY;
            } else {
                if (s->in_i2c_block_read) {
                    s->smb_blkdata = i2c_recv(s->smbus);
                } else {
                    s->smb_blkdata = s->smb_data[s->smb_index];
                }
                s->smb_stat |= STS_BYTE_DONE;
            }
        }
        break;
    case SMBHSTCNT:
        s->smb_ctl = val & ~CTL_START; /* CTL_START always reads 0 */
        if (val & CTL_START) {
            if (!s->op_done) {
                s->smb_index = 0;
                s->op_done = true;
                if (s->in_i2c_block_read) {
                    s->in_i2c_block_read = false;
                    i2c_end_transfer(s->smbus);
                }
            }
            smb_transaction_start(s);
        }
        if (s->smb_ctl & CTL_KILL) {
            s->op_done = true;
            s->smb_index = 0;
            s->smb_stat |= STS_FAILED;
            s->smb_stat &= ~STS_HOST_BUSY;
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
        if (s->smb_index >= PM_SMBUS_MAX_MSG_SIZE) {
            s->smb_index = 0;
        }
        if (s->smb_auxctl & AUX_BLK) {
            s->smb_data[s->smb_index++] = val;
        } else {
            s->smb_blkdata = val;
        }
        break;
    case SMBAUXCTL:
        s->smb_auxctl = val & AUX_MASK;
        break;
    default:
        break;
    }

 out:
    if (s->set_irq) {
        s->set_irq(s, smb_irq_value(s));
    }
}

static uint64_t smb_ioport_readb(void *opaque, hwaddr addr, unsigned width)
{
    PMSMBus *s = opaque;
    uint32_t val;

    switch(addr) {
    case SMBHSTSTS:
        val = s->smb_stat;
        if (s->start_transaction_on_status_read) {
            /* execute command now */
            s->start_transaction_on_status_read = false;
            s->smb_stat &= ~STS_HOST_BUSY;
            smb_transaction(s);
        }
        break;
    case SMBHSTCNT:
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
        if (s->smb_auxctl & AUX_BLK && !s->in_i2c_block_read) {
            if (s->smb_index >= PM_SMBUS_MAX_MSG_SIZE) {
                s->smb_index = 0;
            }
            val = s->smb_data[s->smb_index++];
            if (!s->op_done && s->smb_index == s->smb_data0) {
                s->op_done = true;
                s->smb_index = 0;
                s->smb_stat &= ~STS_HOST_BUSY;
            }
        } else {
            val = s->smb_blkdata;
        }
        break;
    case SMBAUXCTL:
        val = s->smb_auxctl;
        break;
    default:
        val = 0;
        break;
    }
    trace_smbus_ioport_readb(addr, val);

    if (s->set_irq) {
        s->set_irq(s, smb_irq_value(s));
    }

    return val;
}

static void pm_smbus_reset(PMSMBus *s)
{
    s->op_done = true;
    s->smb_index = 0;
    s->smb_stat = 0;
}

static const MemoryRegionOps pm_smbus_ops = {
    .read = smb_ioport_readb,
    .write = smb_ioport_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

bool pm_smbus_vmstate_needed(void)
{
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());

    return !mc->smbus_no_migration_support;
}

const VMStateDescription pmsmb_vmstate = {
    .name = "pmsmb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(smb_stat, PMSMBus),
        VMSTATE_UINT8(smb_ctl, PMSMBus),
        VMSTATE_UINT8(smb_cmd, PMSMBus),
        VMSTATE_UINT8(smb_addr, PMSMBus),
        VMSTATE_UINT8(smb_data0, PMSMBus),
        VMSTATE_UINT8(smb_data1, PMSMBus),
        VMSTATE_UINT32(smb_index, PMSMBus),
        VMSTATE_UINT8_ARRAY(smb_data, PMSMBus, PM_SMBUS_MAX_MSG_SIZE),
        VMSTATE_UINT8(smb_auxctl, PMSMBus),
        VMSTATE_UINT8(smb_blkdata, PMSMBus),
        VMSTATE_BOOL(i2c_enable, PMSMBus),
        VMSTATE_BOOL(op_done, PMSMBus),
        VMSTATE_BOOL(in_i2c_block_read, PMSMBus),
        VMSTATE_BOOL(start_transaction_on_status_read, PMSMBus),
        VMSTATE_END_OF_LIST()
    }
};

void pm_smbus_init(DeviceState *parent, PMSMBus *smb, bool force_aux_blk)
{
    smb->op_done = true;
    smb->reset = pm_smbus_reset;
    smb->smbus = i2c_init_bus(parent, "i2c");
    if (force_aux_blk) {
        smb->smb_auxctl |= AUX_BLK;
    }
    memory_region_init_io(&smb->io, OBJECT(parent), &pm_smbus_ops, smb,
                          "pm-smbus", 64);
}
