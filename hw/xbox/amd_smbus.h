/*
 * AMD756 SMBus implementation
 *
 * Copyright (C) 2012 espes
 *
 * Based on pm_smbus.c
 * Copyright (c) 2006 Fabrice Bellard
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

#ifndef AMD_SMBUS_H
#define AMD_SMBUS_H

typedef struct AMD756SMBus {
    i2c_bus *smbus;

    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;

    qemu_irq irq;
} AMD756SMBus;

void amd756_smbus_init(DeviceState *parent, AMD756SMBus *smb, qemu_irq irq);
void amd756_smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val);
uint32_t amd756_smb_ioport_readb(void *opaque, uint32_t addr);

#endif /* !AMD_SMBUS_H */
