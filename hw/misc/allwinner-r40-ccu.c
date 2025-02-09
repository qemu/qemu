/*
 * Allwinner R40 Clock Control Unit emulation
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
#include "hw/misc/allwinner-r40-ccu.h"

/* CCU register offsets */
enum {
    REG_PLL_CPUX_CTRL           = 0x0000,
    REG_PLL_AUDIO_CTRL          = 0x0008,
    REG_PLL_VIDEO0_CTRL         = 0x0010,
    REG_PLL_VE_CTRL             = 0x0018,
    REG_PLL_DDR0_CTRL           = 0x0020,
    REG_PLL_PERIPH0_CTRL        = 0x0028,
    REG_PLL_PERIPH1_CTRL        = 0x002c,
    REG_PLL_VIDEO1_CTRL         = 0x0030,
    REG_PLL_SATA_CTRL           = 0x0034,
    REG_PLL_GPU_CTRL            = 0x0038,
    REG_PLL_MIPI_CTRL           = 0x0040,
    REG_PLL_DE_CTRL             = 0x0048,
    REG_PLL_DDR1_CTRL           = 0x004c,
    REG_AHB1_APB1_CFG           = 0x0054,
    REG_APB2_CFG                = 0x0058,
    REG_MMC0_CLK                = 0x0088,
    REG_MMC1_CLK                = 0x008c,
    REG_MMC2_CLK                = 0x0090,
    REG_MMC3_CLK                = 0x0094,
    REG_USBPHY_CFG              = 0x00cc,
    REG_PLL_DDR_AUX             = 0x00f0,
    REG_DRAM_CFG                = 0x00f4,
    REG_PLL_DDR1_CFG            = 0x00f8,
    REG_DRAM_CLK_GATING         = 0x0100,
    REG_GMAC_CLK                = 0x0164,
    REG_SYS_32K_CLK             = 0x0310,
    REG_PLL_LOCK_CTRL           = 0x0320,
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* CCU register flags */
enum {
    REG_PLL_ENABLE           = (1 << 31),
    REG_PLL_LOCK             = (1 << 28),
};

static uint64_t allwinner_r40_ccu_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwR40ClockCtlState *s = AW_R40_CCU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case 0x324 ... AW_R40_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_r40_ccu_write(void *opaque, hwaddr offset,
                                    uint64_t val, unsigned size)
{
    AwR40ClockCtlState *s = AW_R40_CCU(opaque);

    switch (offset) {
    case REG_DRAM_CFG:    /* DRAM Configuration(for DDR0) */
        /* bit16: SDRCLK_UPD (SDRCLK configuration 0 update) */
        val &= ~(1 << 16);
        break;
    case REG_PLL_DDR1_CTRL: /* DDR1 Control register */
        /* bit30: SDRPLL_UPD */
        val &= ~(1 << 30);
        if (val & REG_PLL_ENABLE) {
            val |= REG_PLL_LOCK;
        }
        break;
    case REG_PLL_CPUX_CTRL:
    case REG_PLL_AUDIO_CTRL:
    case REG_PLL_VE_CTRL:
    case REG_PLL_VIDEO0_CTRL:
    case REG_PLL_DDR0_CTRL:
    case REG_PLL_PERIPH0_CTRL:
    case REG_PLL_PERIPH1_CTRL:
    case REG_PLL_VIDEO1_CTRL:
    case REG_PLL_SATA_CTRL:
    case REG_PLL_GPU_CTRL:
    case REG_PLL_MIPI_CTRL:
    case REG_PLL_DE_CTRL:
        if (val & REG_PLL_ENABLE) {
            val |= REG_PLL_LOCK;
        }
        break;
    case 0x324 ... AW_R40_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[REG_INDEX(offset)] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_r40_ccu_ops = {
    .read = allwinner_r40_ccu_read,
    .write = allwinner_r40_ccu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_r40_ccu_reset(DeviceState *dev)
{
    AwR40ClockCtlState *s = AW_R40_CCU(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_PLL_CPUX_CTRL)]       = 0x00001000;
    s->regs[REG_INDEX(REG_PLL_AUDIO_CTRL)]      = 0x00035514;
    s->regs[REG_INDEX(REG_PLL_VIDEO0_CTRL)]     = 0x03006207;
    s->regs[REG_INDEX(REG_PLL_VE_CTRL)]         = 0x03006207;
    s->regs[REG_INDEX(REG_PLL_DDR0_CTRL)]       = 0x00001000,
    s->regs[REG_INDEX(REG_PLL_PERIPH0_CTRL)]    = 0x00041811;
    s->regs[REG_INDEX(REG_PLL_PERIPH1_CTRL)]    = 0x00041811;
    s->regs[REG_INDEX(REG_PLL_VIDEO1_CTRL)]     = 0x03006207;
    s->regs[REG_INDEX(REG_PLL_SATA_CTRL)]       = 0x00001811;
    s->regs[REG_INDEX(REG_PLL_GPU_CTRL)]        = 0x03006207;
    s->regs[REG_INDEX(REG_PLL_MIPI_CTRL)]       = 0x00000515;
    s->regs[REG_INDEX(REG_PLL_DE_CTRL)]         = 0x03006207;
    s->regs[REG_INDEX(REG_PLL_DDR1_CTRL)]       = 0x00001800;
    s->regs[REG_INDEX(REG_AHB1_APB1_CFG)]       = 0x00001010;
    s->regs[REG_INDEX(REG_APB2_CFG)]            = 0x01000000;
    s->regs[REG_INDEX(REG_PLL_DDR_AUX)]         = 0x00000001;
    s->regs[REG_INDEX(REG_PLL_DDR1_CFG)]        = 0x0ccca000;
    s->regs[REG_INDEX(REG_SYS_32K_CLK)]         = 0x0000000f;
}

static void allwinner_r40_ccu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwR40ClockCtlState *s = AW_R40_CCU(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_r40_ccu_ops, s,
                          TYPE_AW_R40_CCU, AW_R40_CCU_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_r40_ccu_vmstate = {
    .name = "allwinner-r40-ccu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwR40ClockCtlState, AW_R40_CCU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_r40_ccu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_r40_ccu_reset);
    dc->vmsd = &allwinner_r40_ccu_vmstate;
}

static const TypeInfo allwinner_r40_ccu_info = {
    .name          = TYPE_AW_R40_CCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_r40_ccu_init,
    .instance_size = sizeof(AwR40ClockCtlState),
    .class_init    = allwinner_r40_ccu_class_init,
};

static void allwinner_r40_ccu_register(void)
{
    type_register_static(&allwinner_r40_ccu_info);
}

type_init(allwinner_r40_ccu_register)
