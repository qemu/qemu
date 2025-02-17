/*
 * Allwinner H3 Clock Control Unit emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
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
#include "hw/misc/allwinner-h3-ccu.h"

/* CCU register offsets */
enum {
    REG_PLL_CPUX             = 0x0000, /* PLL CPUX Control */
    REG_PLL_AUDIO            = 0x0008, /* PLL Audio Control */
    REG_PLL_VIDEO            = 0x0010, /* PLL Video Control */
    REG_PLL_VE               = 0x0018, /* PLL VE Control */
    REG_PLL_DDR              = 0x0020, /* PLL DDR Control */
    REG_PLL_PERIPH0          = 0x0028, /* PLL Peripherals 0 Control */
    REG_PLL_GPU              = 0x0038, /* PLL GPU Control */
    REG_PLL_PERIPH1          = 0x0044, /* PLL Peripherals 1 Control */
    REG_PLL_DE               = 0x0048, /* PLL Display Engine Control */
    REG_CPUX_AXI             = 0x0050, /* CPUX/AXI Configuration */
    REG_APB1                 = 0x0054, /* ARM Peripheral Bus 1 Config */
    REG_APB2                 = 0x0058, /* ARM Peripheral Bus 2 Config */
    REG_DRAM_CFG             = 0x00F4, /* DRAM Configuration */
    REG_MBUS                 = 0x00FC, /* MBUS Reset */
    REG_PLL_TIME0            = 0x0200, /* PLL Stable Time 0 */
    REG_PLL_TIME1            = 0x0204, /* PLL Stable Time 1 */
    REG_PLL_CPUX_BIAS        = 0x0220, /* PLL CPUX Bias */
    REG_PLL_AUDIO_BIAS       = 0x0224, /* PLL Audio Bias */
    REG_PLL_VIDEO_BIAS       = 0x0228, /* PLL Video Bias */
    REG_PLL_VE_BIAS          = 0x022C, /* PLL VE Bias */
    REG_PLL_DDR_BIAS         = 0x0230, /* PLL DDR Bias */
    REG_PLL_PERIPH0_BIAS     = 0x0234, /* PLL Peripherals 0 Bias */
    REG_PLL_GPU_BIAS         = 0x023C, /* PLL GPU Bias */
    REG_PLL_PERIPH1_BIAS     = 0x0244, /* PLL Peripherals 1 Bias */
    REG_PLL_DE_BIAS          = 0x0248, /* PLL Display Engine Bias */
    REG_PLL_CPUX_TUNING      = 0x0250, /* PLL CPUX Tuning */
    REG_PLL_DDR_TUNING       = 0x0260, /* PLL DDR Tuning */
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* CCU register flags */
enum {
    REG_DRAM_CFG_UPDATE      = (1 << 16),
};

enum {
    REG_PLL_ENABLE           = (1 << 31),
    REG_PLL_LOCK             = (1 << 28),
};


/* CCU register reset values */
enum {
    REG_PLL_CPUX_RST         = 0x00001000,
    REG_PLL_AUDIO_RST        = 0x00035514,
    REG_PLL_VIDEO_RST        = 0x03006207,
    REG_PLL_VE_RST           = 0x03006207,
    REG_PLL_DDR_RST          = 0x00001000,
    REG_PLL_PERIPH0_RST      = 0x00041811,
    REG_PLL_GPU_RST          = 0x03006207,
    REG_PLL_PERIPH1_RST      = 0x00041811,
    REG_PLL_DE_RST           = 0x03006207,
    REG_CPUX_AXI_RST         = 0x00010000,
    REG_APB1_RST             = 0x00001010,
    REG_APB2_RST             = 0x01000000,
    REG_DRAM_CFG_RST         = 0x00000000,
    REG_MBUS_RST             = 0x80000000,
    REG_PLL_TIME0_RST        = 0x000000FF,
    REG_PLL_TIME1_RST        = 0x000000FF,
    REG_PLL_CPUX_BIAS_RST    = 0x08100200,
    REG_PLL_AUDIO_BIAS_RST   = 0x10100000,
    REG_PLL_VIDEO_BIAS_RST   = 0x10100000,
    REG_PLL_VE_BIAS_RST      = 0x10100000,
    REG_PLL_DDR_BIAS_RST     = 0x81104000,
    REG_PLL_PERIPH0_BIAS_RST = 0x10100010,
    REG_PLL_GPU_BIAS_RST     = 0x10100000,
    REG_PLL_PERIPH1_BIAS_RST = 0x10100010,
    REG_PLL_DE_BIAS_RST      = 0x10100000,
    REG_PLL_CPUX_TUNING_RST  = 0x0A101000,
    REG_PLL_DDR_TUNING_RST   = 0x14880000,
};

static uint64_t allwinner_h3_ccu_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwH3ClockCtlState *s = AW_H3_CCU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case 0x308 ... AW_H3_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_h3_ccu_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwH3ClockCtlState *s = AW_H3_CCU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_DRAM_CFG:    /* DRAM Configuration */
        val &= ~REG_DRAM_CFG_UPDATE;
        break;
    case REG_PLL_CPUX:    /* PLL CPUX Control */
    case REG_PLL_AUDIO:   /* PLL Audio Control */
    case REG_PLL_VIDEO:   /* PLL Video Control */
    case REG_PLL_VE:      /* PLL VE Control */
    case REG_PLL_DDR:     /* PLL DDR Control */
    case REG_PLL_PERIPH0: /* PLL Peripherals 0 Control */
    case REG_PLL_GPU:     /* PLL GPU Control */
    case REG_PLL_PERIPH1: /* PLL Peripherals 1 Control */
    case REG_PLL_DE:      /* PLL Display Engine Control */
        if (val & REG_PLL_ENABLE) {
            val |= REG_PLL_LOCK;
        }
        break;
    case 0x308 ... AW_H3_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_h3_ccu_ops = {
    .read = allwinner_h3_ccu_read,
    .write = allwinner_h3_ccu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_h3_ccu_reset(DeviceState *dev)
{
    AwH3ClockCtlState *s = AW_H3_CCU(dev);

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_PLL_CPUX)] = REG_PLL_CPUX_RST;
    s->regs[REG_INDEX(REG_PLL_AUDIO)] = REG_PLL_AUDIO_RST;
    s->regs[REG_INDEX(REG_PLL_VIDEO)] = REG_PLL_VIDEO_RST;
    s->regs[REG_INDEX(REG_PLL_VE)] = REG_PLL_VE_RST;
    s->regs[REG_INDEX(REG_PLL_DDR)] = REG_PLL_DDR_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH0)] = REG_PLL_PERIPH0_RST;
    s->regs[REG_INDEX(REG_PLL_GPU)] = REG_PLL_GPU_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH1)] = REG_PLL_PERIPH1_RST;
    s->regs[REG_INDEX(REG_PLL_DE)] = REG_PLL_DE_RST;
    s->regs[REG_INDEX(REG_CPUX_AXI)] = REG_CPUX_AXI_RST;
    s->regs[REG_INDEX(REG_APB1)] = REG_APB1_RST;
    s->regs[REG_INDEX(REG_APB2)] = REG_APB2_RST;
    s->regs[REG_INDEX(REG_DRAM_CFG)] = REG_DRAM_CFG_RST;
    s->regs[REG_INDEX(REG_MBUS)] = REG_MBUS_RST;
    s->regs[REG_INDEX(REG_PLL_TIME0)] = REG_PLL_TIME0_RST;
    s->regs[REG_INDEX(REG_PLL_TIME1)] = REG_PLL_TIME1_RST;
    s->regs[REG_INDEX(REG_PLL_CPUX_BIAS)] = REG_PLL_CPUX_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_AUDIO_BIAS)] = REG_PLL_AUDIO_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_VIDEO_BIAS)] = REG_PLL_VIDEO_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_VE_BIAS)] = REG_PLL_VE_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_DDR_BIAS)] = REG_PLL_DDR_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH0_BIAS)] = REG_PLL_PERIPH0_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_GPU_BIAS)] = REG_PLL_GPU_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH1_BIAS)] = REG_PLL_PERIPH1_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_DE_BIAS)] = REG_PLL_DE_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_CPUX_TUNING)] = REG_PLL_CPUX_TUNING_RST;
    s->regs[REG_INDEX(REG_PLL_DDR_TUNING)] = REG_PLL_DDR_TUNING_RST;
}

static void allwinner_h3_ccu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwH3ClockCtlState *s = AW_H3_CCU(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_h3_ccu_ops, s,
                          TYPE_AW_H3_CCU, AW_H3_CCU_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_h3_ccu_vmstate = {
    .name = "allwinner-h3-ccu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwH3ClockCtlState, AW_H3_CCU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_h3_ccu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, allwinner_h3_ccu_reset);
    dc->vmsd = &allwinner_h3_ccu_vmstate;
}

static const TypeInfo allwinner_h3_ccu_info = {
    .name          = TYPE_AW_H3_CCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_h3_ccu_init,
    .instance_size = sizeof(AwH3ClockCtlState),
    .class_init    = allwinner_h3_ccu_class_init,
};

static void allwinner_h3_ccu_register(void)
{
    type_register_static(&allwinner_h3_ccu_info);
}

type_init(allwinner_h3_ccu_register)
