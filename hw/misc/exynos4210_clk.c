/*
 *  Exynos4210 Clock Controller Emulation
 *
 *  Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_EXYNOS4210_CLK             "exynos4210.clk"
OBJECT_DECLARE_SIMPLE_TYPE(Exynos4210ClkState, EXYNOS4210_CLK)

#define CLK_PLL_LOCKED                  BIT(29)

#define EXYNOS4210_CLK_REGS_MEM_SIZE    0x15104

typedef struct Exynos4210Reg {
    const char   *name; /* for debug only */
    uint32_t     offset;
    uint32_t     reset_value;
} Exynos4210Reg;

/* Clock controller register base: 0x10030000 */
static const Exynos4210Reg exynos4210_clk_regs[] = {
    {"EPLL_LOCK",                     0xc010, 0x00000fff},
    {"VPLL_LOCK",                     0xc020, 0x00000fff},
    {"EPLL_CON0",                     0xc110, 0x00300301 | CLK_PLL_LOCKED},
    {"EPLL_CON1",                     0xc114, 0x00000000},
    {"VPLL_CON0",                     0xc120, 0x00240201 | CLK_PLL_LOCKED},
    {"VPLL_CON1",                     0xc124, 0x66010464},
    {"APLL_LOCK",                    0x14000, 0x00000fff},
    {"MPLL_LOCK",                    0x14004, 0x00000fff},
    {"APLL_CON0",                    0x14100, 0x00c80601 | CLK_PLL_LOCKED},
    {"APLL_CON1",                    0x14104, 0x0000001c},
    {"MPLL_CON0",                    0x14108, 0x00c80601 | CLK_PLL_LOCKED},
    {"MPLL_CON1",                    0x1410c, 0x0000001c},
};

#define EXYNOS4210_REGS_NUM       ARRAY_SIZE(exynos4210_clk_regs)

struct Exynos4210ClkState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg[EXYNOS4210_REGS_NUM];
};

static uint64_t exynos4210_clk_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    const Exynos4210ClkState *s = (Exynos4210ClkState *)opaque;
    const Exynos4210Reg *regs = exynos4210_clk_regs;
    unsigned int i;

    for (i = 0; i < EXYNOS4210_REGS_NUM; i++) {
        if (regs->offset == offset) {
            return s->reg[i];
        }
        regs++;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%04x\n",
                  __func__, (uint32_t)offset);
    return 0;
}

static void exynos4210_clk_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    Exynos4210ClkState *s = (Exynos4210ClkState *)opaque;
    const Exynos4210Reg *regs = exynos4210_clk_regs;
    unsigned int i;

    for (i = 0; i < EXYNOS4210_REGS_NUM; i++) {
        if (regs->offset == offset) {
            s->reg[i] = val;
            return;
        }
        regs++;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n",
                  __func__, (uint32_t)offset);
}

static const MemoryRegionOps exynos4210_clk_ops = {
    .read = exynos4210_clk_read,
    .write = exynos4210_clk_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};

static void exynos4210_clk_reset(DeviceState *dev)
{
    Exynos4210ClkState *s = EXYNOS4210_CLK(dev);
    unsigned int i;

    /* Set default values for registers */
    for (i = 0; i < EXYNOS4210_REGS_NUM; i++) {
        s->reg[i] = exynos4210_clk_regs[i].reset_value;
    }
}

static void exynos4210_clk_init(Object *obj)
{
    Exynos4210ClkState *s = EXYNOS4210_CLK(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    /* memory mapping */
    memory_region_init_io(&s->iomem, obj, &exynos4210_clk_ops, s,
                          TYPE_EXYNOS4210_CLK, EXYNOS4210_CLK_REGS_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static const VMStateDescription exynos4210_clk_vmstate = {
    .name = TYPE_EXYNOS4210_CLK,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Exynos4210ClkState, EXYNOS4210_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4210_clk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_clk_reset;
    dc->vmsd = &exynos4210_clk_vmstate;
}

static const TypeInfo exynos4210_clk_info = {
    .name          = TYPE_EXYNOS4210_CLK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210ClkState),
    .instance_init = exynos4210_clk_init,
    .class_init    = exynos4210_clk_class_init,
};

static void exynos4210_clk_register(void)
{
    qemu_log_mask(LOG_GUEST_ERROR, "Clock init\n");
    type_register_static(&exynos4210_clk_info);
}

type_init(exynos4210_clk_register)
