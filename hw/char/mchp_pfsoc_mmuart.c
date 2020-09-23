/*
 * Microchip PolarFire SoC MMUART emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "chardev/char.h"
#include "exec/address-spaces.h"
#include "hw/char/mchp_pfsoc_mmuart.h"

static uint64_t mchp_pfsoc_mmuart_read(void *opaque, hwaddr addr, unsigned size)
{
    MchpPfSoCMMUartState *s = opaque;

    if (addr >= MCHP_PFSOC_MMUART_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    return s->reg[addr / sizeof(uint32_t)];
}

static void mchp_pfsoc_mmuart_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    MchpPfSoCMMUartState *s = opaque;
    uint32_t val32 = (uint32_t)value;

    if (addr >= MCHP_PFSOC_MMUART_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%" HWADDR_PRIx
                      " v=0x%x\n", __func__, addr, val32);
        return;
    }

    s->reg[addr / sizeof(uint32_t)] = val32;
}

static const MemoryRegionOps mchp_pfsoc_mmuart_ops = {
    .read = mchp_pfsoc_mmuart_read,
    .write = mchp_pfsoc_mmuart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

MchpPfSoCMMUartState *mchp_pfsoc_mmuart_create(MemoryRegion *sysmem,
    hwaddr base, qemu_irq irq, Chardev *chr)
{
    MchpPfSoCMMUartState *s;

    s = g_new0(MchpPfSoCMMUartState, 1);

    memory_region_init_io(&s->iomem, NULL, &mchp_pfsoc_mmuart_ops, s,
                          "mchp.pfsoc.mmuart", 0x1000);

    s->base = base;
    s->irq = irq;

    s->serial = serial_mm_init(sysmem, base, 2, irq, 399193, chr,
                               DEVICE_LITTLE_ENDIAN);

    memory_region_add_subregion(sysmem, base + 0x20, &s->iomem);

    return s;
}
