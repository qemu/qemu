/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX7 GPR IP block emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Bare minimum emulation code needed to support being able to shut
 * down linux guest gracefully.
 */

#include "qemu/osdep.h"
#include "hw/misc/imx7_gpr.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "trace.h"

enum IMX7GPRRegisters {
    IOMUXC_GPR0  = 0x00,
    IOMUXC_GPR1  = 0x04,
    IOMUXC_GPR2  = 0x08,
    IOMUXC_GPR3  = 0x0c,
    IOMUXC_GPR4  = 0x10,
    IOMUXC_GPR5  = 0x14,
    IOMUXC_GPR6  = 0x18,
    IOMUXC_GPR7  = 0x1c,
    IOMUXC_GPR8  = 0x20,
    IOMUXC_GPR9  = 0x24,
    IOMUXC_GPR10 = 0x28,
    IOMUXC_GPR11 = 0x2c,
    IOMUXC_GPR12 = 0x30,
    IOMUXC_GPR13 = 0x34,
    IOMUXC_GPR14 = 0x38,
    IOMUXC_GPR15 = 0x3c,
    IOMUXC_GPR16 = 0x40,
    IOMUXC_GPR17 = 0x44,
    IOMUXC_GPR18 = 0x48,
    IOMUXC_GPR19 = 0x4c,
    IOMUXC_GPR20 = 0x50,
    IOMUXC_GPR21 = 0x54,
    IOMUXC_GPR22 = 0x58,
};

#define IMX7D_GPR1_IRQ_MASK                 BIT(12)
#define IMX7D_GPR1_ENET1_TX_CLK_SEL_MASK    BIT(13)
#define IMX7D_GPR1_ENET2_TX_CLK_SEL_MASK    BIT(14)
#define IMX7D_GPR1_ENET_TX_CLK_SEL_MASK     (0x3 << 13)
#define IMX7D_GPR1_ENET1_CLK_DIR_MASK       BIT(17)
#define IMX7D_GPR1_ENET2_CLK_DIR_MASK       BIT(18)
#define IMX7D_GPR1_ENET_CLK_DIR_MASK        (0x3 << 17)

#define IMX7D_GPR5_CSI_MUX_CONTROL_MIPI     BIT(4)
#define IMX7D_GPR12_PCIE_PHY_REFCLK_SEL     BIT(5)
#define IMX7D_GPR22_PCIE_PHY_PLL_LOCKED     BIT(31)


static uint64_t imx7_gpr_read(void *opaque, hwaddr offset, unsigned size)
{
    trace_imx7_gpr_read(offset);

    if (offset == IOMUXC_GPR22) {
        return IMX7D_GPR22_PCIE_PHY_PLL_LOCKED;
    }

    return 0;
}

static void imx7_gpr_write(void *opaque, hwaddr offset,
                           uint64_t v, unsigned size)
{
    trace_imx7_gpr_write(offset, v);
}

static const struct MemoryRegionOps imx7_gpr_ops = {
    .read = imx7_gpr_read,
    .write = imx7_gpr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the
         * real device but in practice there is no reason for a guest
         * to access this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx7_gpr_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX7GPRState *s = IMX7_GPR(obj);

    memory_region_init_io(&s->mmio, obj, &imx7_gpr_ops, s,
                          TYPE_IMX7_GPR, 64 * 1024);
    sysbus_init_mmio(sd, &s->mmio);
}

static void imx7_gpr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc  = "i.MX7 General Purpose Registers Module";
}

static const TypeInfo imx7_gpr_info = {
    .name          = TYPE_IMX7_GPR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX7GPRState),
    .instance_init = imx7_gpr_init,
    .class_init    = imx7_gpr_class_init,
};

static void imx7_gpr_register_type(void)
{
    type_register_static(&imx7_gpr_info);
}
type_init(imx7_gpr_register_type)
