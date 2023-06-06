/*
 * Allwinner R40 SRAM controller emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/misc/allwinner-sramc.h"
#include "trace.h"

/*
 * register offsets
 * https://linux-sunxi.org/SRAM_Controller_Register_Guide
 */
enum {
    REG_SRAM_CTL1_CFG               = 0x04, /* SRAM Control register 1 */
    REG_SRAM_VER                    = 0x24, /* SRAM Version register */
    REG_SRAM_R40_SOFT_ENTRY_REG0    = 0xbc,
};

/* REG_SRAMC_VERSION bit defines */
#define SRAM_VER_READ_ENABLE            (1 << 15)
#define SRAM_VER_VERSION_SHIFT          16
#define SRAM_VERSION_SUN8I_R40          0x1701

static uint64_t allwinner_sramc_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    AwSRAMCState *s = AW_SRAMC(opaque);
    AwSRAMCClass *sc = AW_SRAMC_GET_CLASS(s);
    uint64_t val = 0;

    switch (offset) {
    case REG_SRAM_CTL1_CFG:
        val = s->sram_ctl1;
        break;
    case REG_SRAM_VER:
        /* bit15: lock bit, set this bit before reading this register */
        if (s->sram_ver & SRAM_VER_READ_ENABLE) {
            val = SRAM_VER_READ_ENABLE |
                    (sc->sram_version_code << SRAM_VER_VERSION_SHIFT);
        }
        break;
    case REG_SRAM_R40_SOFT_ENTRY_REG0:
        val = s->sram_soft_entry_reg0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_sramc_read(offset, val);

    return val;
}

static void allwinner_sramc_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    AwSRAMCState *s = AW_SRAMC(opaque);

    trace_allwinner_sramc_write(offset, val);

    switch (offset) {
    case REG_SRAM_CTL1_CFG:
        s->sram_ctl1 = val;
        break;
    case REG_SRAM_VER:
        /* Only the READ_ENABLE bit is writeable */
        s->sram_ver = val & SRAM_VER_READ_ENABLE;
        break;
    case REG_SRAM_R40_SOFT_ENTRY_REG0:
        s->sram_soft_entry_reg0 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_sramc_ops = {
    .read = allwinner_sramc_read,
    .write = allwinner_sramc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const VMStateDescription allwinner_sramc_vmstate = {
    .name = "allwinner-sramc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sram_ver, AwSRAMCState),
        VMSTATE_UINT32(sram_soft_entry_reg0, AwSRAMCState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_sramc_reset(DeviceState *dev)
{
    AwSRAMCState *s = AW_SRAMC(dev);
    AwSRAMCClass *sc = AW_SRAMC_GET_CLASS(s);

    switch (sc->sram_version_code) {
    case SRAM_VERSION_SUN8I_R40:
        s->sram_ctl1 = 0x1300;
        break;
    }
}

static void allwinner_sramc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_sramc_reset;
    dc->vmsd = &allwinner_sramc_vmstate;
}

static void allwinner_sramc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwSRAMCState *s = AW_SRAMC(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_sramc_ops, s,
                           TYPE_AW_SRAMC, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo allwinner_sramc_info = {
    .name          = TYPE_AW_SRAMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_sramc_init,
    .instance_size = sizeof(AwSRAMCState),
    .class_init    = allwinner_sramc_class_init,
};

static void allwinner_r40_sramc_class_init(ObjectClass *klass, void *data)
{
    AwSRAMCClass *sc = AW_SRAMC_CLASS(klass);

    sc->sram_version_code = SRAM_VERSION_SUN8I_R40;
}

static const TypeInfo allwinner_r40_sramc_info = {
    .name          = TYPE_AW_SRAMC_SUN8I_R40,
    .parent        = TYPE_AW_SRAMC,
    .class_init    = allwinner_r40_sramc_class_init,
};

static void allwinner_sramc_register(void)
{
    type_register_static(&allwinner_sramc_info);
    type_register_static(&allwinner_r40_sramc_info);
}

type_init(allwinner_sramc_register)
