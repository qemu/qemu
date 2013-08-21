/*
 * AMD756 SMBus implementation
 *
 * Copyright (C) 2012 espes
 *
 * Based on pm_smbus.c
 * Copyright (c) 2006 Fabrice Bellard
 * Based on Linux drivers/i2c/busses/i2c-amd756.c
 * Copyright (c) 1999-2002 Merlin Hughes <merlin@merlin.org>
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
#include "hw/i386/pc.h"
#include "hw/xbox/amd_smbus.h"
#include "hw/i2c/smbus.h"

/* AMD756 SMBus address offsets */
#define SMB_ADDR_OFFSET     0xE0
#define SMB_IOSIZE      16

#define SMB_GLOBAL_STATUS       0x0
#define SMB_GLOBAL_ENABLE       0x2
#define SMB_HOST_ADDRESS        0x4
#define SMB_HOST_DATA           0x6
#define SMB_HOST_COMMAND        0x8
#define SMB_HOST_BLOCK_DATA     0x9
#define SMB_HAS_DATA            0xA
#define SMB_HAS_DEVICE_ADDRESS  0xC
#define SMB_HAS_HOST_ADDRESS    0xE
#define SMB_SNOOP_ADDRESS       0xF

/* AMD756 constants */
#define AMD756_QUICK        0x00
#define AMD756_BYTE         0x01
#define AMD756_BYTE_DATA    0x02
#define AMD756_WORD_DATA    0x03
#define AMD756_PROCESS_CALL 0x04
#define AMD756_BLOCK_DATA   0x05

/* 
  SMBUS event = I/O 28-29 bit 11
     see E0 for the status bits and enabled in E2
*/
#define GS_ABRT_STS (1 << 0)
#define GS_COL_STS  (1 << 1)
#define GS_PRERR_STS    (1 << 2)
#define GS_HST_STS  (1 << 3)
#define GS_HCYC_STS (1 << 4)
#define GS_TO_STS   (1 << 5)
#define GS_SMB_STS  (1 << 11)

#define GS_CLEAR_STS    (GS_ABRT_STS | GS_COL_STS | GS_PRERR_STS | \
             GS_HCYC_STS | GS_TO_STS )

#define GE_CYC_TYPE_MASK    (7)
#define GE_HOST_STC     (1 << 3)

#define GE_HCYC_EN      (1 << 4)
#define GE_ABORT        (1 << 5)



//#define DEBUG

#ifdef DEBUG
# define SMBUS_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define SMBUS_DPRINTF(format, ...)     do { } while (0)
#endif

static void amd756_smb_transaction(AMD756SMBus *s)
{
    uint8_t prot = s->smb_ctl & GE_CYC_TYPE_MASK;
    uint8_t read = s->smb_addr & 0x01;
    uint8_t cmd = s->smb_cmd;
    uint8_t addr = (s->smb_addr >> 1) & 0x7f;
    i2c_bus *bus = s->smbus;
    
    SMBUS_DPRINTF("SMBus trans addr=0x%02x prot=0x%02x\n", addr, prot);
    switch(prot) {
    case AMD756_QUICK:
        smbus_quick_command(bus, addr, read);
        break;
    case AMD756_BYTE:
        if (read) {
            s->smb_data0 = smbus_receive_byte(bus, addr);
        } else {
            smbus_send_byte(bus, addr, cmd);
        }
        break;
    case AMD756_BYTE_DATA:
        if (read) {
            s->smb_data0 = smbus_read_byte(bus, addr, cmd);
        } else {
            smbus_write_byte(bus, addr, cmd, s->smb_data0);
        }
        break;
    case AMD756_WORD_DATA:
        if (read) {
            uint16_t val;
            val = smbus_read_word(bus, addr, cmd);
            s->smb_data0 = val;
            s->smb_data1 = val >> 8;
        } else {
            smbus_write_word(bus, addr, cmd, s->smb_data0);
        }
        break;
    case AMD756_BLOCK_DATA:
        if (read) {
            s->smb_data0 = smbus_read_block(bus, addr, cmd, s->smb_data);
        } else {
            smbus_write_block(bus, addr, cmd, s->smb_data, s->smb_data0);
        }
        break;
    default:
        goto error;
    }
    
    s->smb_stat |= GS_HCYC_STS;
    
    return;
    
    
    error:
      s->smb_stat |= GS_PRERR_STS;
}

void amd756_smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    AMD756SMBus *s = opaque;
    addr &= 0x3f;
    SMBUS_DPRINTF("SMB writeb port=0x%04x val=0x%02x\n", addr, val);
    switch(addr) {
    case SMB_GLOBAL_STATUS:

        if (s->irq) {
            /* Raise an irq if interrupts are enabled and a new
             * status is being set */
            if ((s->smb_ctl & GE_HCYC_EN)
                && ((val & GS_CLEAR_STS)
                    & (~(s->smb_stat & GS_CLEAR_STS)))) {

                qemu_irq_raise(s->irq);
            } else {
                qemu_irq_lower(s->irq);
            }
        }

        if (val & GS_CLEAR_STS) {
            s->smb_stat = 0;
            s->smb_index = 0;
        } else if (val & GS_HCYC_STS) {
            s->smb_stat = GS_HCYC_STS;
            s->smb_index = 0;
        } else {
            s->smb_stat = GS_HCYC_STS;
            s->smb_index = 0;
        }

        break;
    case SMB_GLOBAL_ENABLE:
        s->smb_ctl = val;
        if (val & GE_ABORT)
            s->smb_stat |= GS_ABRT_STS;
        if (val & GE_HOST_STC) {
            amd756_smb_transaction(s);

            if (s->irq
                && (val & GE_HCYC_EN)
                && (s->smb_stat & GS_CLEAR_STS)) {
                qemu_irq_raise(s->irq);
            }
        }

        break;
    case SMB_HOST_COMMAND:
        s->smb_cmd = val;
        break;
    case SMB_HOST_ADDRESS:
        s->smb_addr = val;
        break;
    case SMB_HOST_DATA:
        s->smb_data0 = val;
        break;
    case SMB_HOST_DATA+1:
        s->smb_data1 = val;
        break;
    case SMB_HOST_BLOCK_DATA:
        s->smb_data[s->smb_index++] = val;
        if (s->smb_index > 31)
            s->smb_index = 0;
        break;
    default:
        break;
    }
}

uint32_t amd756_smb_ioport_readb(void *opaque, uint32_t addr)
{
    AMD756SMBus *s = opaque;
    uint32_t val;

    addr &= 0x3f;
    switch(addr) {
    case SMB_GLOBAL_STATUS:
        val = s->smb_stat;
        break;
    case SMB_GLOBAL_ENABLE:
        //s->smb_index = 0;
        val = s->smb_ctl & 0x1f;
        break;
    case SMB_HOST_COMMAND:
        val = s->smb_cmd;
        break;
    case SMB_HOST_ADDRESS:
        val = s->smb_addr;
        break;
    case SMB_HOST_DATA:
        val = s->smb_data0;
        break;
    case SMB_HOST_DATA+1:
        val = s->smb_data1;
        break;
    case SMB_HOST_BLOCK_DATA:
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

void amd756_smbus_init(DeviceState *parent, AMD756SMBus *smb, qemu_irq irq)
{
    smb->smbus = i2c_init_bus(parent, "i2c");
    smb->smb_stat = 0;

    smb->irq = irq;
}