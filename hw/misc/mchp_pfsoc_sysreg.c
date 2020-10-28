/*
 * Microchip PolarFire SoC SYSREG module emulation
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
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/mchp_pfsoc_sysreg.h"

#define ENVM_CR         0xb8

static uint64_t mchp_pfsoc_sysreg_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    uint32_t val = 0;

    switch (offset) {
    case ENVM_CR:
        /* Indicate the eNVM is running at the configured divider rate */
        val = BIT(6);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        break;
    }

    return val;
}

static void mchp_pfsoc_sysreg_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %d, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_sysreg_ops = {
    .read = mchp_pfsoc_sysreg_read,
    .write = mchp_pfsoc_sysreg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mchp_pfsoc_sysreg_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCSysregState *s = MCHP_PFSOC_SYSREG(dev);

    memory_region_init_io(&s->sysreg, OBJECT(dev),
                          &mchp_pfsoc_sysreg_ops, s,
                          "mchp.pfsoc.sysreg",
                          MCHP_PFSOC_SYSREG_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sysreg);
}

static void mchp_pfsoc_sysreg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip PolarFire SoC SYSREG module";
    dc->realize = mchp_pfsoc_sysreg_realize;
}

static const TypeInfo mchp_pfsoc_sysreg_info = {
    .name          = TYPE_MCHP_PFSOC_SYSREG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCSysregState),
    .class_init    = mchp_pfsoc_sysreg_class_init,
};

static void mchp_pfsoc_sysreg_register_types(void)
{
    type_register_static(&mchp_pfsoc_sysreg_info);
}

type_init(mchp_pfsoc_sysreg_register_types)
