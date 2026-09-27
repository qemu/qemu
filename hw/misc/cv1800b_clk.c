/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sophgo CV1800B Clock Controller
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/cv1800b_clk.h"

#define REG_BYTE_WIDTH    (sizeof(uint32_t))
#define REG_CLK_BYP_0     (0x030 / REG_BYTE_WIDTH)
#define REG_CLK_BYP_1     (0x034 / REG_BYTE_WIDTH)

static uint64_t cv1800b_clk_read(void *opaque, hwaddr addr, unsigned int size)
{
    CV1800BClkState *s = opaque;
    uint32_t val = 0;

    if (addr < CV1800B_CLK_IOSIZE) {
        val = s->regs[addr / REG_BYTE_WIDTH];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }

    return val;
}

static void cv1800b_clk_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    CV1800BClkState *s = opaque;

    if (addr < CV1800B_CLK_IOSIZE) {
        s->regs[addr / REG_BYTE_WIDTH] = val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps cv1800b_clk_ops = {
    .read = cv1800b_clk_read,
    .write = cv1800b_clk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cv1800b_clk_reset_hold(Object *obj, ResetType type)
{
    CV1800BClkState *s = CV1800B_CLK(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * TODO: Implement proper PLL state machines.
     * For now, use POR default to bypass PLLs and boot via 25MHz XTAL.
     */
    s->regs[REG_CLK_BYP_0] = 0xFFFFFFFF;
    s->regs[REG_CLK_BYP_1] = 0xFFFFFFFF;
}

static void cv1800b_clk_init(Object *obj)
{
    CV1800BClkState *s = CV1800B_CLK(obj);

    memory_region_init_io(&s->iomem, obj, &cv1800b_clk_ops, s,
                            TYPE_CV1800B_CLK, CV1800B_CLK_IOSIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void cv1800b_clk_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = cv1800b_clk_reset_hold;
}

static const TypeInfo cv1800b_clk_info = {
    .name = TYPE_CV1800B_CLK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CV1800BClkState),
    .instance_init = cv1800b_clk_init,
    .class_init = cv1800b_clk_class_init,
};

static void cv1800b_clk_register_types(void)
{
    type_register_static(&cv1800b_clk_info);
}

type_init(cv1800b_clk_register_types)
