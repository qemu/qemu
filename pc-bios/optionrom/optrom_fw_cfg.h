/*
 * Common Option ROM Functions for fw_cfg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2015-2019 Red Hat Inc.
 *   Authors:
 *     Marc Marí <marc.mari.barcelo@gmail.com>
 *     Richard W.M. Jones <rjones@redhat.com>
 *     Stefano Garzarella <sgarzare@redhat.com>
 */

#ifndef OPTROM_FW_CFG_H
#define OPTROM_FW_CFG_H

#include "../../include/standard-headers/linux/qemu_fw_cfg.h"

#define BIOS_CFG_IOPORT_CFG     0x510
#define BIOS_CFG_IOPORT_DATA    0x511
#define BIOS_CFG_DMA_ADDR_HIGH  0x514
#define BIOS_CFG_DMA_ADDR_LOW   0x518

static __attribute__((unused))
void bios_cfg_select(uint16_t key)
{
    outw(key, BIOS_CFG_IOPORT_CFG);
}

static __attribute__((unused))
void bios_cfg_read_entry_io(void *buf, uint16_t entry, uint32_t len)
{
    bios_cfg_select(entry);
    insb(BIOS_CFG_IOPORT_DATA, buf, len);
}

/*
 * clang is happy to inline this function, and bloats the
 * ROM.
 */
static __attribute__((__noinline__)) __attribute__((unused))
void bios_cfg_read_entry_dma(void *buf, uint16_t entry, uint32_t len)
{
    struct fw_cfg_dma_access access;
    uint32_t control = (entry << 16) | FW_CFG_DMA_CTL_SELECT
                        | FW_CFG_DMA_CTL_READ;

    access.address = cpu_to_be64((uint64_t)(uint32_t)buf);
    access.length = cpu_to_be32(len);
    access.control = cpu_to_be32(control);

    barrier();

    outl(cpu_to_be32((uint32_t)&access), BIOS_CFG_DMA_ADDR_LOW);

    while (be32_to_cpu(access.control) & ~FW_CFG_DMA_CTL_ERROR) {
        barrier();
    }
}

static __attribute__((unused))
void bios_cfg_read_entry(void *buf, uint16_t entry, uint32_t len,
                         uint32_t version)
{
    if (version & FW_CFG_VERSION_DMA) {
        bios_cfg_read_entry_dma(buf, entry, len);
    } else {
        bios_cfg_read_entry_io(buf, entry, len);
    }
}

static __attribute__((unused))
uint32_t bios_cfg_version(void)
{
    uint32_t version;

    bios_cfg_read_entry_io(&version, FW_CFG_ID, sizeof(version));

    return version;
}

#endif /* OPTROM_FW_CFG_H */
