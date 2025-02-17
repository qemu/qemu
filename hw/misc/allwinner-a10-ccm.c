/*
 * Allwinner A10 Clock Control Module emulation
 *
 * Copyright (C) 2022 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from Allwinner H3 CCU,
 *  by Niek Linnenbank.
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
#include "hw/misc/allwinner-a10-ccm.h"

/* CCM register offsets */
enum {
    REG_PLL1_CFG             = 0x0000, /* PLL1 Control */
    REG_PLL1_TUN             = 0x0004, /* PLL1 Tuning */
    REG_PLL2_CFG             = 0x0008, /* PLL2 Control */
    REG_PLL2_TUN             = 0x000C, /* PLL2 Tuning */
    REG_PLL3_CFG             = 0x0010, /* PLL3 Control */
    REG_PLL4_CFG             = 0x0018, /* PLL4 Control */
    REG_PLL5_CFG             = 0x0020, /* PLL5 Control */
    REG_PLL5_TUN             = 0x0024, /* PLL5 Tuning */
    REG_PLL6_CFG             = 0x0028, /* PLL6 Control */
    REG_PLL6_TUN             = 0x002C, /* PLL6 Tuning */
    REG_PLL7_CFG             = 0x0030, /* PLL7 Control */
    REG_PLL1_TUN2            = 0x0038, /* PLL1 Tuning2 */
    REG_PLL5_TUN2            = 0x003C, /* PLL5 Tuning2 */
    REG_PLL8_CFG             = 0x0040, /* PLL8 Control */
    REG_OSC24M_CFG           = 0x0050, /* OSC24M Control */
    REG_CPU_AHB_APB0_CFG     = 0x0054, /* CPU, AHB and APB0 Divide Ratio */
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* CCM register reset values */
enum {
    REG_PLL1_CFG_RST         = 0x21005000,
    REG_PLL1_TUN_RST         = 0x0A101000,
    REG_PLL2_CFG_RST         = 0x08100010,
    REG_PLL2_TUN_RST         = 0x00000000,
    REG_PLL3_CFG_RST         = 0x0010D063,
    REG_PLL4_CFG_RST         = 0x21009911,
    REG_PLL5_CFG_RST         = 0x11049280,
    REG_PLL5_TUN_RST         = 0x14888000,
    REG_PLL6_CFG_RST         = 0x21009911,
    REG_PLL6_TUN_RST         = 0x00000000,
    REG_PLL7_CFG_RST         = 0x0010D063,
    REG_PLL1_TUN2_RST        = 0x00000000,
    REG_PLL5_TUN2_RST        = 0x00000000,
    REG_PLL8_CFG_RST         = 0x21009911,
    REG_OSC24M_CFG_RST       = 0x00138013,
    REG_CPU_AHB_APB0_CFG_RST = 0x00010010,
};

static uint64_t allwinner_a10_ccm_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwA10ClockCtlState *s = AW_A10_CCM(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_PLL1_CFG:
    case REG_PLL1_TUN:
    case REG_PLL2_CFG:
    case REG_PLL2_TUN:
    case REG_PLL3_CFG:
    case REG_PLL4_CFG:
    case REG_PLL5_CFG:
    case REG_PLL5_TUN:
    case REG_PLL6_CFG:
    case REG_PLL6_TUN:
    case REG_PLL7_CFG:
    case REG_PLL1_TUN2:
    case REG_PLL5_TUN2:
    case REG_PLL8_CFG:
    case REG_OSC24M_CFG:
    case REG_CPU_AHB_APB0_CFG:
        break;
    case 0x158 ... AW_A10_CCM_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void allwinner_a10_ccm_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10ClockCtlState *s = AW_A10_CCM(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_PLL1_CFG:
    case REG_PLL1_TUN:
    case REG_PLL2_CFG:
    case REG_PLL2_TUN:
    case REG_PLL3_CFG:
    case REG_PLL4_CFG:
    case REG_PLL5_CFG:
    case REG_PLL5_TUN:
    case REG_PLL6_CFG:
    case REG_PLL6_TUN:
    case REG_PLL7_CFG:
    case REG_PLL1_TUN2:
    case REG_PLL5_TUN2:
    case REG_PLL8_CFG:
    case REG_OSC24M_CFG:
    case REG_CPU_AHB_APB0_CFG:
        break;
    case 0x158 ... AW_A10_CCM_IOSIZE:
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

static const MemoryRegionOps allwinner_a10_ccm_ops = {
    .read = allwinner_a10_ccm_read,
    .write = allwinner_a10_ccm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_a10_ccm_reset_enter(Object *obj, ResetType type)
{
    AwA10ClockCtlState *s = AW_A10_CCM(obj);

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_PLL1_CFG)] = REG_PLL1_CFG_RST;
    s->regs[REG_INDEX(REG_PLL1_TUN)] = REG_PLL1_TUN_RST;
    s->regs[REG_INDEX(REG_PLL2_CFG)] = REG_PLL2_CFG_RST;
    s->regs[REG_INDEX(REG_PLL2_TUN)] = REG_PLL2_TUN_RST;
    s->regs[REG_INDEX(REG_PLL3_CFG)] = REG_PLL3_CFG_RST;
    s->regs[REG_INDEX(REG_PLL4_CFG)] = REG_PLL4_CFG_RST;
    s->regs[REG_INDEX(REG_PLL5_CFG)] = REG_PLL5_CFG_RST;
    s->regs[REG_INDEX(REG_PLL5_TUN)] = REG_PLL5_TUN_RST;
    s->regs[REG_INDEX(REG_PLL6_CFG)] = REG_PLL6_CFG_RST;
    s->regs[REG_INDEX(REG_PLL6_TUN)] = REG_PLL6_TUN_RST;
    s->regs[REG_INDEX(REG_PLL7_CFG)] = REG_PLL7_CFG_RST;
    s->regs[REG_INDEX(REG_PLL1_TUN2)] = REG_PLL1_TUN2_RST;
    s->regs[REG_INDEX(REG_PLL5_TUN2)] = REG_PLL5_TUN2_RST;
    s->regs[REG_INDEX(REG_PLL8_CFG)] = REG_PLL8_CFG_RST;
    s->regs[REG_INDEX(REG_OSC24M_CFG)] = REG_OSC24M_CFG_RST;
    s->regs[REG_INDEX(REG_CPU_AHB_APB0_CFG)] = REG_CPU_AHB_APB0_CFG_RST;
}

static void allwinner_a10_ccm_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwA10ClockCtlState *s = AW_A10_CCM(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_ccm_ops, s,
                          TYPE_AW_A10_CCM, AW_A10_CCM_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_a10_ccm_vmstate = {
    .name = "allwinner-a10-ccm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10ClockCtlState, AW_A10_CCM_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_a10_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_a10_ccm_reset_enter;
    dc->vmsd = &allwinner_a10_ccm_vmstate;
}

static const TypeInfo allwinner_a10_ccm_info = {
    .name          = TYPE_AW_A10_CCM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_ccm_init,
    .instance_size = sizeof(AwA10ClockCtlState),
    .class_init    = allwinner_a10_ccm_class_init,
};

static void allwinner_a10_ccm_register(void)
{
    type_register_static(&allwinner_a10_ccm_info);
}

type_init(allwinner_a10_ccm_register)
