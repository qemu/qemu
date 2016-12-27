/*
 * ASPEED System Control Unit
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "trace.h"

#define TO_REG(offset) ((offset) >> 2)

#define PROT_KEY             TO_REG(0x00)
#define SYS_RST_CTRL         TO_REG(0x04)
#define CLK_SEL              TO_REG(0x08)
#define CLK_STOP_CTRL        TO_REG(0x0C)
#define FREQ_CNTR_CTRL       TO_REG(0x10)
#define FREQ_CNTR_EVAL       TO_REG(0x14)
#define IRQ_CTRL             TO_REG(0x18)
#define D2PLL_PARAM          TO_REG(0x1C)
#define MPLL_PARAM           TO_REG(0x20)
#define HPLL_PARAM           TO_REG(0x24)
#define FREQ_CNTR_RANGE      TO_REG(0x28)
#define MISC_CTRL1           TO_REG(0x2C)
#define PCI_CTRL1            TO_REG(0x30)
#define PCI_CTRL2            TO_REG(0x34)
#define PCI_CTRL3            TO_REG(0x38)
#define SYS_RST_STATUS       TO_REG(0x3C)
#define SOC_SCRATCH1         TO_REG(0x40)
#define SOC_SCRATCH2         TO_REG(0x44)
#define MAC_CLK_DELAY        TO_REG(0x48)
#define MISC_CTRL2           TO_REG(0x4C)
#define VGA_SCRATCH1         TO_REG(0x50)
#define VGA_SCRATCH2         TO_REG(0x54)
#define VGA_SCRATCH3         TO_REG(0x58)
#define VGA_SCRATCH4         TO_REG(0x5C)
#define VGA_SCRATCH5         TO_REG(0x60)
#define VGA_SCRATCH6         TO_REG(0x64)
#define VGA_SCRATCH7         TO_REG(0x68)
#define VGA_SCRATCH8         TO_REG(0x6C)
#define HW_STRAP1            TO_REG(0x70)
#define RNG_CTRL             TO_REG(0x74)
#define RNG_DATA             TO_REG(0x78)
#define SILICON_REV          TO_REG(0x7C)
#define PINMUX_CTRL1         TO_REG(0x80)
#define PINMUX_CTRL2         TO_REG(0x84)
#define PINMUX_CTRL3         TO_REG(0x88)
#define PINMUX_CTRL4         TO_REG(0x8C)
#define PINMUX_CTRL5         TO_REG(0x90)
#define PINMUX_CTRL6         TO_REG(0x94)
#define WDT_RST_CTRL         TO_REG(0x9C)
#define PINMUX_CTRL7         TO_REG(0xA0)
#define PINMUX_CTRL8         TO_REG(0xA4)
#define PINMUX_CTRL9         TO_REG(0xA8)
#define WAKEUP_EN            TO_REG(0xC0)
#define WAKEUP_CTRL          TO_REG(0xC4)
#define HW_STRAP2            TO_REG(0xD0)
#define FREE_CNTR4           TO_REG(0xE0)
#define FREE_CNTR4_EXT       TO_REG(0xE4)
#define CPU2_CTRL            TO_REG(0x100)
#define CPU2_BASE_SEG1       TO_REG(0x104)
#define CPU2_BASE_SEG2       TO_REG(0x108)
#define CPU2_BASE_SEG3       TO_REG(0x10C)
#define CPU2_BASE_SEG4       TO_REG(0x110)
#define CPU2_BASE_SEG5       TO_REG(0x114)
#define CPU2_CACHE_CTRL      TO_REG(0x118)
#define UART_HPLL_CLK        TO_REG(0x160)
#define PCIE_CTRL            TO_REG(0x180)
#define BMC_MMIO_CTRL        TO_REG(0x184)
#define RELOC_DECODE_BASE1   TO_REG(0x188)
#define RELOC_DECODE_BASE2   TO_REG(0x18C)
#define MAILBOX_DECODE_BASE  TO_REG(0x190)
#define SRAM_DECODE_BASE1    TO_REG(0x194)
#define SRAM_DECODE_BASE2    TO_REG(0x198)
#define BMC_REV              TO_REG(0x19C)
#define BMC_DEV_ID           TO_REG(0x1A4)

#define PROT_KEY_UNLOCK 0x1688A8A8
#define SCU_IO_REGION_SIZE 0x1000

static const uint32_t ast2400_a0_resets[ASPEED_SCU_NR_REGS] = {
     [SYS_RST_CTRL]    = 0xFFCFFEDCU,
     [CLK_SEL]         = 0xF3F40000U,
     [CLK_STOP_CTRL]   = 0x19FC3E8BU,
     [D2PLL_PARAM]     = 0x00026108U,
     [MPLL_PARAM]      = 0x00030291U,
     [HPLL_PARAM]      = 0x00000291U,
     [MISC_CTRL1]      = 0x00000010U,
     [PCI_CTRL1]       = 0x20001A03U,
     [PCI_CTRL2]       = 0x20001A03U,
     [PCI_CTRL3]       = 0x04000030U,
     [SYS_RST_STATUS]  = 0x00000001U,
     [SOC_SCRATCH1]    = 0x000000C0U, /* SoC completed DRAM init */
     [MISC_CTRL2]      = 0x00000023U,
     [RNG_CTRL]        = 0x0000000EU,
     [PINMUX_CTRL2]    = 0x0000F000U,
     [PINMUX_CTRL3]    = 0x01000000U,
     [PINMUX_CTRL4]    = 0x000000FFU,
     [PINMUX_CTRL5]    = 0x0000A000U,
     [WDT_RST_CTRL]    = 0x003FFFF3U,
     [PINMUX_CTRL8]    = 0xFFFF0000U,
     [PINMUX_CTRL9]    = 0x000FFFFFU,
     [FREE_CNTR4]      = 0x000000FFU,
     [FREE_CNTR4_EXT]  = 0x000000FFU,
     [CPU2_BASE_SEG1]  = 0x80000000U,
     [CPU2_BASE_SEG4]  = 0x1E600000U,
     [CPU2_BASE_SEG5]  = 0xC0000000U,
     [UART_HPLL_CLK]   = 0x00001903U,
     [PCIE_CTRL]       = 0x0000007BU,
     [BMC_DEV_ID]      = 0x00002402U
};

/* SCU70 bit 23: 0 24Mhz. bit 11:9: 0b001 AXI:ABH ratio 2:1 */
/* AST2500 revision A1 */

static const uint32_t ast2500_a1_resets[ASPEED_SCU_NR_REGS] = {
     [SYS_RST_CTRL]    = 0xFFCFFEDCU,
     [CLK_SEL]         = 0xF3F40000U,
     [CLK_STOP_CTRL]   = 0x19FC3E8BU,
     [D2PLL_PARAM]     = 0x00026108U,
     [MPLL_PARAM]      = 0x00030291U,
     [HPLL_PARAM]      = 0x93000400U,
     [MISC_CTRL1]      = 0x00000010U,
     [PCI_CTRL1]       = 0x20001A03U,
     [PCI_CTRL2]       = 0x20001A03U,
     [PCI_CTRL3]       = 0x04000030U,
     [SYS_RST_STATUS]  = 0x00000001U,
     [SOC_SCRATCH1]    = 0x000000C0U, /* SoC completed DRAM init */
     [MISC_CTRL2]      = 0x00000023U,
     [RNG_CTRL]        = 0x0000000EU,
     [PINMUX_CTRL2]    = 0x0000F000U,
     [PINMUX_CTRL3]    = 0x03000000U,
     [PINMUX_CTRL4]    = 0x00000000U,
     [PINMUX_CTRL5]    = 0x0000A000U,
     [WDT_RST_CTRL]    = 0x023FFFF3U,
     [PINMUX_CTRL8]    = 0xFFFF0000U,
     [PINMUX_CTRL9]    = 0x000FFFFFU,
     [FREE_CNTR4]      = 0x000000FFU,
     [FREE_CNTR4_EXT]  = 0x000000FFU,
     [CPU2_BASE_SEG1]  = 0x80000000U,
     [CPU2_BASE_SEG4]  = 0x1E600000U,
     [CPU2_BASE_SEG5]  = 0xC0000000U,
     [UART_HPLL_CLK]   = 0x00001903U,
     [PCIE_CTRL]       = 0x0000007BU,
     [BMC_DEV_ID]      = 0x00002402U
};

static uint64_t aspeed_scu_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case WAKEUP_EN:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Read of write-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    return s->regs[reg];
}

static void aspeed_scu_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    if (reg > PROT_KEY && reg < CPU2_BASE_SEG1 &&
            s->regs[PROT_KEY] != PROT_KEY_UNLOCK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SCU is locked!\n", __func__);
        return;
    }

    trace_aspeed_scu_write(offset, size, data);

    switch (reg) {
    case FREQ_CNTR_EVAL:
    case VGA_SCRATCH1 ... VGA_SCRATCH8:
    case RNG_DATA:
    case SILICON_REV:
    case FREE_CNTR4:
    case FREE_CNTR4_EXT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_scu_ops = {
    .read = aspeed_scu_read,
    .write = aspeed_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_scu_reset(DeviceState *dev)
{
    AspeedSCUState *s = ASPEED_SCU(dev);
    const uint32_t *reset;

    switch (s->silicon_rev) {
    case AST2400_A0_SILICON_REV:
    case AST2400_A1_SILICON_REV:
        reset = ast2400_a0_resets;
        break;
    case AST2500_A0_SILICON_REV:
    case AST2500_A1_SILICON_REV:
        reset = ast2500_a1_resets;
        break;
    default:
        g_assert_not_reached();
    }

    memcpy(s->regs, reset, sizeof(s->regs));
    s->regs[SILICON_REV] = s->silicon_rev;
    s->regs[HW_STRAP1] = s->hw_strap1;
    s->regs[HW_STRAP2] = s->hw_strap2;
}

static uint32_t aspeed_silicon_revs[] = {
    AST2400_A0_SILICON_REV,
    AST2400_A1_SILICON_REV,
    AST2500_A0_SILICON_REV,
    AST2500_A1_SILICON_REV,
};

bool is_supported_silicon_rev(uint32_t silicon_rev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(aspeed_silicon_revs); i++) {
        if (silicon_rev == aspeed_silicon_revs[i]) {
            return true;
        }
    }

    return false;
}

static void aspeed_scu_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSCUState *s = ASPEED_SCU(dev);

    if (!is_supported_silicon_rev(s->silicon_rev)) {
        error_setg(errp, "Unknown silicon revision: 0x%" PRIx32,
                s->silicon_rev);
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_scu_ops, s,
                          TYPE_ASPEED_SCU, SCU_IO_REGION_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_scu = {
    .name = "aspeed.scu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSCUState, ASPEED_SCU_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static Property aspeed_scu_properties[] = {
    DEFINE_PROP_UINT32("silicon-rev", AspeedSCUState, silicon_rev, 0),
    DEFINE_PROP_UINT32("hw-strap1", AspeedSCUState, hw_strap1, 0),
    DEFINE_PROP_UINT32("hw-strap2", AspeedSCUState, hw_strap2, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_scu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_scu_realize;
    dc->reset = aspeed_scu_reset;
    dc->desc = "ASPEED System Control Unit";
    dc->vmsd = &vmstate_aspeed_scu;
    dc->props = aspeed_scu_properties;
}

static const TypeInfo aspeed_scu_info = {
    .name = TYPE_ASPEED_SCU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_scu_class_init,
};

static void aspeed_scu_register_types(void)
{
    type_register_static(&aspeed_scu_info);
}

type_init(aspeed_scu_register_types);
