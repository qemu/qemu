/*
 * Allwinner F1 Clock Control Unit emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 * Copyright (C) 2022 froloff
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
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/misc/allwinner-ccu.h"

/* CCU register offsets */
enum {
    REG_PLL_CPU_CTRL         = 0x0000, /* PLL CPU Control */
    REG_PLL_AUDIO            = 0x0008, /* PLL Audio Control */
    REG_PLL_VIDEO            = 0x0010, /* PLL Video Control */
    REG_PLL_VE               = 0x0018, /* PLL VE Control */
    REG_PLL_DDR              = 0x0020, /* PLL DDR Control */
    REG_PLL_PERIPH           = 0x0028, /* PLL Peripherals Control */
    REG_CPU_CLK_SRC          = 0x0050, /* CPU Clock Source */
    REG_AHB_APB_HCLKC_CFG    = 0x0054, /* AHB/APB/HCLKC Configuration */
    REG_BUS_CLK_GATING0      = 0x0060, /* Bus Clock Gating Register 0 */
    REG_BUS_CLK_GATING1      = 0x0064, /* Bus Clock Gating Register 1 */
    REG_BUS_CLK_GATING2      = 0x0068, /* Bus Clock Gating Register 2 */
    REG_SDMMC0_CLK           = 0x0088, /* SDMMC0 Clock */
    REG_SDMMC1_CLK           = 0x008c, /* SDMMC1 Clock */
    REG_DAUDIO_CLK           = 0x00b0, /* DAUDIO Clock */
    REG_OWA_CLK              = 0x00b4, /* OWA Clock */
    REG_CIR_CLK              = 0x00b8, /* CIR Clock */
    REG_USBPHY_CLK           = 0x00cc, /* USBPHY Clock */
    REG_DRAM_GATING          = 0x0100, /* DRAM GATING */
    REG_BE_CLK               = 0x0104, /* BE Clock */
    REG_FE_CLK               = 0x010c, /* FE Clock */
    REG_TCON_CLK             = 0x0118, /* TCON Clock */
    REG_DI_CLK               = 0x011c, /* De-interlacer Clock */
    REG_TVE_CLK              = 0x0120, /* TVE Clock */
    REG_TVD_CLK              = 0x0124, /* TVD Clock */    
    REG_CSI_CLK              = 0x0134, /* CSI Clock */
    REG_VE_CLK               = 0x013c, /* TE Clock */
    REG_AUDIO_CODEC_CLK      = 0x0140, /* AUDIO CODEC Clock */
    REG_AVS_CLK              = 0x0144, /* AVS Clock */    
    REG_PLL_TIME0            = 0x0200, /* PLL Stable Time 0 */
    REG_PLL_TIME1            = 0x0204, /* PLL Stable Time 1 */
    REG_PLL_CPU_BIAS         = 0x0220, /* PLL CPU Bias */
    REG_PLL_AUDIO_BIAS       = 0x0224, /* PLL Audio Bias */
    REG_PLL_VIDEO_BIAS       = 0x0228, /* PLL Video Bias */
    REG_PLL_VE_BIAS          = 0x022C, /* PLL VE Bias */
    REG_PLL_DDR_BIAS         = 0x0230, /* PLL DDR Bias */
    REG_PLL_PERIPH_BIAS      = 0x0234, /* PLL Peripherals Bias */
    REG_PLL_CPU_TUNING       = 0x0250, /* PLL CPU Tuning */
    REG_PLL_DDR_TUNING       = 0x0260, /* PLL DDR Tuning */   
    REG_PLL_AUDIO_PAT_CTRL   = 0x0284, /* PLL AUDIO Pattern Control */
    REG_PLL_VIDEO_PAT_CTRL   = 0x0288, /* PLL VIDEO Pattern Control */
    REG_PLL_DDR_PAT_CTRL     = 0x0290, /* PLL DDR Pattern Control */
    REG_BUS_SOFT_RST0        = 0x02c0, /* Bus Software Reset Register 0 */
    REG_BUS_SOFT_RST1        = 0x02c4, /* Bus Software Reset Register 1 */
    REG_BUS_SOFT_RST2        = 0x02d0, /* Bus Software Reset Register 2 */
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* CCU register flags */
enum {
    REG_DRAM_CFG_UPDATE      = (1 << 20),
};

enum {
    REG_PLL_ENABLE           = (1 << 31),
    REG_PLL_LOCK             = (1 << 28),
};


/* CCU register reset values */
enum {
    REG_PLL_CPU_CTRL_RST     = 0x00001000,
    REG_PLL_AUDIO_RST        = 0x00005514,
    REG_PLL_VIDEO_RST        = 0x03006207,
    REG_PLL_VE_RST           = 0x03004570,
    REG_PLL_DDR_RST          = 0x00001811,
    REG_PLL_PERIPH_RST       = 0x00041801,
    REG_CPU_CLK_SRC_RST      = 0x00010000,
    REG_AHB_APB_HCLKC_CFG_RST= 0x00011010,
    REG_BUS_CLK_GATING0_RST  = 0x00000000,
    REG_BUS_CLK_GATING1_RST  = 0x00000000,
    REG_BUS_CLK_GATING2_RST  = 0x00000000,
    REG_SDMMC0_CLK_RST       = 0x00000000,
    REG_SDMMC1_CLK_RST       = 0x00000000,
    REG_DRAM_CFG_RST         = 0x00000000,
    REG_DAUDIO_CLK_RST       = 0x00000000,
    REG_OWA_CLK_RST          = 0x00010000,
    REG_CIR_CLK_RST          = 0x00000000,
    REG_USBPHY_CLK_RST       = 0x00000000,
    REG_DRAM_GATING_RST      = 0x00000000,
    REG_BE_CLK_RST           = 0x00000000,
    REG_FE_CLK_RST           = 0x00000000,
    REG_TCON_CLK_RST         = 0x00000000,
    REG_DI_CLK_RST           = 0x00000000,
    REG_TVE_CLK_RST          = 0x00000000,
    REG_PLL_CPU_BIAS_RST     = 0x08100200,
    REG_PLL_AUDIO_BIAS_RST   = 0x10100000,
    REG_PLL_VIDEO_BIAS_RST   = 0x10100000,
    REG_PLL_VE_BIAS_RST      = 0x10100000,
    REG_PLL_DDR_BIAS_RST     = 0x81104000,
    REG_PLL_PERIPH_BIAS_RST  = 0x10100010,
    REG_PLL_CPU_TUNING_RST   = 0x02404000,
    REG_PLL_DDR_TUNING_RST   = 0x02404000,
};

static uint64_t allwinner_f1_ccu_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwClockCtlState *s = AW_CCU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case 0x308 ... AW_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_f1_ccu_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwClockCtlState *s = AW_CCU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_PLL_DDR:      /* DRAM Configuration */
        val &= ~REG_DRAM_CFG_UPDATE;
        // fall through
    case REG_PLL_CPU_CTRL: /* PLL CPU Control */
    case REG_PLL_AUDIO:    /* PLL Audio Control */
    case REG_PLL_VIDEO:    /* PLL Video Control */
    case REG_PLL_VE:       /* PLL VE Control */
    case REG_PLL_PERIPH:   /* PLL Peripherals 0 Control */
    case REG_SDMMC0_CLK:
        if (val & REG_PLL_ENABLE) {
            val |= REG_PLL_LOCK;
        }
        break;
    case 0x308 ... AW_CCU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_f1_ccu_ops = {
    .read = allwinner_f1_ccu_read,
    .write = allwinner_f1_ccu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_f1_ccu_reset(DeviceState *dev)
{
    AwClockCtlState *s = AW_CCU(dev);

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_PLL_CPU_CTRL)] = REG_PLL_CPU_CTRL_RST;
    s->regs[REG_INDEX(REG_PLL_AUDIO)] = REG_PLL_AUDIO_RST;
    s->regs[REG_INDEX(REG_PLL_VIDEO)] = REG_PLL_VIDEO_RST;
    s->regs[REG_INDEX(REG_PLL_VE)] = REG_PLL_VE_RST;
    s->regs[REG_INDEX(REG_PLL_DDR)] = REG_PLL_DDR_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH)] = REG_PLL_PERIPH_RST;
    s->regs[REG_INDEX(REG_CPU_CLK_SRC)] = REG_CPU_CLK_SRC_RST;
    s->regs[REG_INDEX(REG_AHB_APB_HCLKC_CFG)] = REG_AHB_APB_HCLKC_CFG_RST;
    s->regs[REG_INDEX(REG_DRAM_GATING)] = REG_DRAM_GATING_RST;
    s->regs[REG_INDEX(REG_PLL_CPU_BIAS)] = REG_PLL_CPU_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_AUDIO_BIAS)] = REG_PLL_AUDIO_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_VIDEO_BIAS)] = REG_PLL_VIDEO_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_VE_BIAS)] = REG_PLL_VE_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_DDR_BIAS)] = REG_PLL_DDR_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_PERIPH_BIAS)] = REG_PLL_PERIPH_BIAS_RST;
    s->regs[REG_INDEX(REG_PLL_CPU_TUNING)] = REG_PLL_CPU_TUNING_RST;
    s->regs[REG_INDEX(REG_PLL_DDR_TUNING)] = REG_PLL_DDR_TUNING_RST;
}

static void allwinner_f1_ccu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwClockCtlState *s = AW_CCU(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_f1_ccu_ops, s,
                          TYPE_AW_F1_CCU, AW_CCU_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_f1_ccu_vmstate = {
    .name = "allwinner-f1-ccu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwClockCtlState, AW_CCU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_f1_ccu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_f1_ccu_reset;
    dc->vmsd = &allwinner_f1_ccu_vmstate;
}

static const TypeInfo allwinner_f1_ccu_info = {
    .name          = TYPE_AW_F1_CCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_f1_ccu_init,
    .instance_size = sizeof(AwClockCtlState),
    .class_init    = allwinner_f1_ccu_class_init,
};

static void allwinner_f1_ccu_register(void)
{
    type_register_static(&allwinner_f1_ccu_info);
}

type_init(allwinner_f1_ccu_register)
