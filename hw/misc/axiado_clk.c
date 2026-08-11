/*
 * Axiado Clock Control
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/axiado_clk.h"
#include "qemu/log.h"

#define CLKRST_CPU_PLL_POSTDIV_OFFSET   0x0C
#define CLKRST_CPU_PLL_STS_OFFSET       0x14

static uint64_t pll_read(void *opaque, hwaddr offset, unsigned size)
{
    switch (offset) {
    case CLKRST_CPU_PLL_POSTDIV_OFFSET:
        return 0x20891b;
    case CLKRST_CPU_PLL_STS_OFFSET:
        return 0x01;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "Register 0x%" HWADDR_PRIx " not implemented\n", offset);
        break;
    }
    return 0x00;
}

static void pll_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "Register 0x%" HWADDR_PRIx " not implemented\n", offset);
}

static const MemoryRegionOps pll_ops = {
    .read = pll_read,
    .write = pll_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void ax3000_clk_init(Object *obj)
{
    Ax3000ClkState *s = AX3000_CLK(obj);

    memory_region_init_io(&s->pll_ctrl, obj, &pll_ops, s,
                          TYPE_AX3000_CLK, AX3000_CLK_PLL_CTRL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->pll_ctrl);
}

static void ax3000_clk_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Axiado AX3000 Clock Control";
}

static const TypeInfo ax3000_clk_info = {
    .parent             = TYPE_SYS_BUS_DEVICE,
    .name               = TYPE_AX3000_CLK,
    .instance_size      = sizeof(Ax3000ClkState),
    .instance_init      = ax3000_clk_init,
    .class_init         = ax3000_clk_class_init,
};

static void axiado_clk_register_type(void)
{
    type_register_static(&ax3000_clk_info);
}
type_init(axiado_clk_register_type);
