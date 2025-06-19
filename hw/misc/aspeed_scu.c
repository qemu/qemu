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
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "qemu/module.h"
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
#define CHIP_ID0             TO_REG(0x150)
#define CHIP_ID1             TO_REG(0x154)
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

#define AST2600_PROT_KEY          TO_REG(0x00)
#define AST2600_PROT_KEY2         TO_REG(0x10)
#define AST2600_SILICON_REV       TO_REG(0x04)
#define AST2600_SILICON_REV2      TO_REG(0x14)
#define AST2600_SYS_RST_CTRL      TO_REG(0x40)
#define AST2600_SYS_RST_CTRL_CLR  TO_REG(0x44)
#define AST2600_SYS_RST_CTRL2     TO_REG(0x50)
#define AST2600_SYS_RST_CTRL2_CLR TO_REG(0x54)
#define AST2600_CLK_STOP_CTRL     TO_REG(0x80)
#define AST2600_CLK_STOP_CTRL_CLR TO_REG(0x84)
#define AST2600_CLK_STOP_CTRL2     TO_REG(0x90)
#define AST2600_CLK_STOP_CTRL2_CLR TO_REG(0x94)
#define AST2600_DEBUG_CTRL        TO_REG(0xC8)
#define AST2600_DEBUG_CTRL2       TO_REG(0xD8)
#define AST2600_SDRAM_HANDSHAKE   TO_REG(0x100)
#define AST2600_HPLL_PARAM        TO_REG(0x200)
#define AST2600_HPLL_EXT          TO_REG(0x204)
#define AST2600_APLL_PARAM        TO_REG(0x210)
#define AST2600_APLL_EXT          TO_REG(0x214)
#define AST2600_MPLL_PARAM        TO_REG(0x220)
#define AST2600_MPLL_EXT          TO_REG(0x224)
#define AST2600_EPLL_PARAM        TO_REG(0x240)
#define AST2600_EPLL_EXT          TO_REG(0x244)
#define AST2600_DPLL_PARAM        TO_REG(0x260)
#define AST2600_DPLL_EXT          TO_REG(0x264)
#define AST2600_CLK_SEL           TO_REG(0x300)
#define AST2600_CLK_SEL2          TO_REG(0x304)
#define AST2600_CLK_SEL3          TO_REG(0x308)
#define AST2600_CLK_SEL4          TO_REG(0x310)
#define AST2600_CLK_SEL5          TO_REG(0x314)
#define AST2600_UARTCLK           TO_REG(0x338)
#define AST2600_HUARTCLK          TO_REG(0x33C)
#define AST2600_HW_STRAP1         TO_REG(0x500)
#define AST2600_HW_STRAP1_CLR     TO_REG(0x504)
#define AST2600_HW_STRAP1_PROT    TO_REG(0x508)
#define AST2600_HW_STRAP2         TO_REG(0x510)
#define AST2600_HW_STRAP2_CLR     TO_REG(0x514)
#define AST2600_HW_STRAP2_PROT    TO_REG(0x518)
#define AST2600_RNG_CTRL          TO_REG(0x524)
#define AST2600_RNG_DATA          TO_REG(0x540)
#define AST2600_CHIP_ID0          TO_REG(0x5B0)
#define AST2600_CHIP_ID1          TO_REG(0x5B4)

#define AST2600_CLK TO_REG(0x40)

#define AST2700_SILICON_REV       TO_REG(0x00)
#define AST2700_HW_STRAP1         TO_REG(0x10)
#define AST2700_HW_STRAP1_CLR     TO_REG(0x14)
#define AST2700_HW_STRAP1_LOCK    TO_REG(0x20)
#define AST2700_HW_STRAP1_SEC1    TO_REG(0x24)
#define AST2700_HW_STRAP1_SEC2    TO_REG(0x28)
#define AST2700_HW_STRAP1_SEC3    TO_REG(0x2C)

#define AST2700_SCU_CLK_SEL_1       TO_REG(0x280)
#define AST2700_SCU_HPLL_PARAM      TO_REG(0x300)
#define AST2700_SCU_HPLL_EXT_PARAM  TO_REG(0x304)
#define AST2700_SCU_DPLL_PARAM      TO_REG(0x308)
#define AST2700_SCU_DPLL_EXT_PARAM  TO_REG(0x30c)
#define AST2700_SCU_MPLL_PARAM      TO_REG(0x310)
#define AST2700_SCU_MPLL_EXT_PARAM  TO_REG(0x314)
#define AST2700_SCU_D1CLK_PARAM     TO_REG(0x320)
#define AST2700_SCU_D2CLK_PARAM     TO_REG(0x330)
#define AST2700_SCU_CRT1CLK_PARAM   TO_REG(0x340)
#define AST2700_SCU_CRT2CLK_PARAM   TO_REG(0x350)
#define AST2700_SCU_MPHYCLK_PARAM   TO_REG(0x360)
#define AST2700_SCU_FREQ_CNTR       TO_REG(0x3b0)
#define AST2700_SCU_CPU_SCRATCH_0   TO_REG(0x780)
#define AST2700_SCU_CPU_SCRATCH_1   TO_REG(0x784)
#define AST2700_SCU_VGA_SCRATCH_0   TO_REG(0x900)

#define AST2700_SCUIO_CLK_STOP_CTL_1    TO_REG(0x240)
#define AST2700_SCUIO_CLK_STOP_CLR_1    TO_REG(0x244)
#define AST2700_SCUIO_CLK_STOP_CTL_2    TO_REG(0x260)
#define AST2700_SCUIO_CLK_STOP_CLR_2    TO_REG(0x264)
#define AST2700_SCUIO_CLK_SEL_1         TO_REG(0x280)
#define AST2700_SCUIO_CLK_SEL_2         TO_REG(0x284)
#define AST2700_SCUIO_HPLL_PARAM        TO_REG(0x300)
#define AST2700_SCUIO_HPLL_EXT_PARAM    TO_REG(0x304)
#define AST2700_SCUIO_APLL_PARAM        TO_REG(0x310)
#define AST2700_SCUIO_APLL_EXT_PARAM    TO_REG(0x314)
#define AST2700_SCUIO_DPLL_PARAM        TO_REG(0x320)
#define AST2700_SCUIO_DPLL_EXT_PARAM    TO_REG(0x324)
#define AST2700_SCUIO_DPLL_PARAM_READ   TO_REG(0x328)
#define AST2700_SCUIO_DPLL_EXT_PARAM_READ TO_REG(0x32c)
#define AST2700_SCUIO_UARTCLK_GEN       TO_REG(0x330)
#define AST2700_SCUIO_HUARTCLK_GEN      TO_REG(0x334)
#define AST2700_SCUIO_CLK_DUTY_MEAS_RST TO_REG(0x388)
#define AST2700_SCUIO_FREQ_CNT_CTL      TO_REG(0x3A0)

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
     [CHIP_ID0]        = 0x1234ABCDU,
     [CHIP_ID1]        = 0x88884444U,
     [UART_HPLL_CLK]   = 0x00001903U,
     [PCIE_CTRL]       = 0x0000007BU,
     [BMC_DEV_ID]      = 0x00002402U
};

static uint32_t aspeed_scu_get_random(void)
{
    uint32_t num;
    qemu_guest_getrandom_nofail(&num, sizeof(num));
    return num;
}

uint32_t aspeed_scu_get_apb_freq(AspeedSCUState *s)
{
    return ASPEED_SCU_GET_CLASS(s)->get_apb(s);
}

static uint32_t aspeed_2400_scu_get_apb_freq(AspeedSCUState *s)
{
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(s);
    uint32_t hpll = asc->calc_hpll(s, s->regs[HPLL_PARAM]);

    return hpll / (SCU_CLK_GET_PCLK_DIV(s->regs[CLK_SEL]) + 1)
        / asc->apb_divider;
}

static uint32_t aspeed_2600_scu_get_apb_freq(AspeedSCUState *s)
{
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(s);
    uint32_t hpll = asc->calc_hpll(s, s->regs[AST2600_HPLL_PARAM]);

    return hpll / (SCU_CLK_GET_PCLK_DIV(s->regs[AST2600_CLK_SEL]) + 1)
        / asc->apb_divider;
}

static uint32_t aspeed_1030_scu_get_apb_freq(AspeedSCUState *s)
{
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(s);
    uint32_t hpll = asc->calc_hpll(s, s->regs[AST2600_HPLL_PARAM]);

    return hpll / (SCU_AST1030_CLK_GET_PCLK_DIV(s->regs[AST2600_CLK_SEL4]) + 1)
        / asc->apb_divider;
}

static uint32_t aspeed_2700_scu_get_apb_freq(AspeedSCUState *s)
{
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(s);
    uint32_t hpll = asc->calc_hpll(s, s->regs[AST2700_SCU_HPLL_PARAM]);

    return hpll / (SCU_CLK_GET_PCLK_DIV(s->regs[AST2700_SCU_CLK_SEL_1]) + 1)
           / asc->apb_divider;
}

static uint32_t aspeed_2700_scuio_get_apb_freq(AspeedSCUState *s)
{
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(s);
    uint32_t hpll = asc->calc_hpll(s, s->regs[AST2700_SCUIO_HPLL_PARAM]);

    return hpll /
        (SCUIO_AST2700_CLK_GET_PCLK_DIV(s->regs[AST2700_SCUIO_CLK_SEL_1]) + 1)
        / asc->apb_divider;
}

static uint64_t aspeed_scu_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case RNG_DATA:
        /*
         * On hardware, RNG_DATA works regardless of
         * the state of the enable bit in RNG_CTRL
         */
        s->regs[RNG_DATA] = aspeed_scu_get_random();
        break;
    case WAKEUP_EN:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Read of write-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    trace_aspeed_scu_read(offset, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_ast2400_scu_write(void *opaque, hwaddr offset,
                                     uint64_t data, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    if (reg > PROT_KEY && reg < CPU2_BASE_SEG1 &&
            !s->regs[PROT_KEY]) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SCU is locked!\n", __func__);
    }

    trace_aspeed_scu_write(offset, size, data);

    switch (reg) {
    case PROT_KEY:
        s->regs[reg] = (data == ASPEED_SCU_PROT_KEY) ? 1 : 0;
        return;
    case SILICON_REV:
    case FREQ_CNTR_EVAL:
    case VGA_SCRATCH1 ... VGA_SCRATCH8:
    case RNG_DATA:
    case FREE_CNTR4:
    case FREE_CNTR4_EXT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = data;
}

static void aspeed_ast2500_scu_write(void *opaque, hwaddr offset,
                                     uint64_t data, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    if (reg > PROT_KEY && reg < CPU2_BASE_SEG1 &&
            !s->regs[PROT_KEY]) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SCU is locked!\n", __func__);
        return;
    }

    trace_aspeed_scu_write(offset, size, data);

    switch (reg) {
    case PROT_KEY:
        s->regs[reg] = (data == ASPEED_SCU_PROT_KEY) ? 1 : 0;
        return;
    case HW_STRAP1:
        s->regs[HW_STRAP1] |= data;
        return;
    case SILICON_REV:
        s->regs[HW_STRAP1] &= ~data;
        return;
    case FREQ_CNTR_EVAL:
    case VGA_SCRATCH1 ... VGA_SCRATCH8:
    case RNG_DATA:
    case FREE_CNTR4:
    case FREE_CNTR4_EXT:
    case CHIP_ID0:
    case CHIP_ID1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_ast2400_scu_ops = {
    .read = aspeed_scu_read,
    .write = aspeed_ast2400_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps aspeed_ast2500_scu_ops = {
    .read = aspeed_scu_read,
    .write = aspeed_ast2500_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static uint32_t aspeed_scu_get_clkin(AspeedSCUState *s)
{
    if (s->hw_strap1 & SCU_HW_STRAP_CLK_25M_IN ||
        ASPEED_SCU_GET_CLASS(s)->clkin_25Mhz) {
        return 25000000;
    } else if (s->hw_strap1 & SCU_HW_STRAP_CLK_48M_IN) {
        return 48000000;
    } else {
        return 24000000;
    }
}

/*
 * Strapped frequencies for the AST2400 in MHz. They depend on the
 * clkin frequency.
 */
static const uint32_t hpll_ast2400_freqs[][4] = {
    { 384, 360, 336, 408 }, /* 24MHz or 48MHz */
    { 400, 375, 350, 425 }, /* 25MHz */
};

static uint32_t aspeed_2400_scu_calc_hpll(AspeedSCUState *s, uint32_t hpll_reg)
{
    uint8_t freq_select;
    bool clk_25m_in;
    uint32_t clkin = aspeed_scu_get_clkin(s);

    if (hpll_reg & SCU_AST2400_H_PLL_OFF) {
        return 0;
    }

    if (hpll_reg & SCU_AST2400_H_PLL_PROGRAMMED) {
        uint32_t multiplier = 1;

        if (!(hpll_reg & SCU_AST2400_H_PLL_BYPASS_EN)) {
            uint32_t n  = (hpll_reg >> 5) & 0x3f;
            uint32_t od = (hpll_reg >> 4) & 0x1;
            uint32_t d  = hpll_reg & 0xf;

            multiplier = (2 - od) * ((n + 2) / (d + 1));
        }

        return clkin * multiplier;
    }

    /* HW strapping */
    clk_25m_in = !!(s->hw_strap1 & SCU_HW_STRAP_CLK_25M_IN);
    freq_select = SCU_AST2400_HW_STRAP_GET_H_PLL_CLK(s->hw_strap1);

    return hpll_ast2400_freqs[clk_25m_in][freq_select] * 1000000;
}

static uint32_t aspeed_2500_scu_calc_hpll(AspeedSCUState *s, uint32_t hpll_reg)
{
    uint32_t multiplier = 1;
    uint32_t clkin = aspeed_scu_get_clkin(s);

    if (hpll_reg & SCU_H_PLL_OFF) {
        return 0;
    }

    if (!(hpll_reg & SCU_H_PLL_BYPASS_EN)) {
        uint32_t p = (hpll_reg >> 13) & 0x3f;
        uint32_t m = (hpll_reg >> 5) & 0xff;
        uint32_t n = hpll_reg & 0x1f;

        multiplier = ((m + 1) / (n + 1)) / (p + 1);
    }

    return clkin * multiplier;
}

static uint32_t aspeed_2600_scu_calc_hpll(AspeedSCUState *s, uint32_t hpll_reg)
{
    uint32_t multiplier = 1;
    uint32_t clkin = aspeed_scu_get_clkin(s);

    if (hpll_reg & SCU_AST2600_H_PLL_OFF) {
        return 0;
    }

    if (!(hpll_reg & SCU_AST2600_H_PLL_BYPASS_EN)) {
        uint32_t p = (hpll_reg >> 19) & 0xf;
        uint32_t n = (hpll_reg >> 13) & 0x3f;
        uint32_t m = hpll_reg & 0x1fff;

        multiplier = ((m + 1) / (n + 1)) / (p + 1);
    }

    return clkin * multiplier;
}

static void aspeed_scu_reset(DeviceState *dev)
{
    AspeedSCUState *s = ASPEED_SCU(dev);
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(dev);

    memcpy(s->regs, asc->resets, asc->nr_regs * 4);
    s->regs[SILICON_REV] = s->silicon_rev;
    s->regs[HW_STRAP1] = s->hw_strap1;
    s->regs[HW_STRAP2] = s->hw_strap2;
    s->regs[PROT_KEY] = s->hw_prot_key;
}

static uint32_t aspeed_silicon_revs[] = {
    AST2400_A0_SILICON_REV,
    AST2400_A1_SILICON_REV,
    AST2500_A0_SILICON_REV,
    AST2500_A1_SILICON_REV,
    AST2600_A0_SILICON_REV,
    AST2600_A1_SILICON_REV,
    AST2600_A2_SILICON_REV,
    AST2600_A3_SILICON_REV,
    AST1030_A0_SILICON_REV,
    AST1030_A1_SILICON_REV,
    AST2700_A0_SILICON_REV,
    AST2720_A0_SILICON_REV,
    AST2750_A0_SILICON_REV,
    AST2700_A1_SILICON_REV,
    AST2750_A1_SILICON_REV,
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
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(dev);

    if (!is_supported_silicon_rev(s->silicon_rev)) {
        error_setg(errp, "Unknown silicon revision: 0x%" PRIx32,
                s->silicon_rev);
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), asc->ops, s,
                          TYPE_ASPEED_SCU, SCU_IO_REGION_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_scu = {
    .name = "aspeed.scu",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSCUState, ASPEED_AST2600_SCU_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static const Property aspeed_scu_properties[] = {
    DEFINE_PROP_UINT32("silicon-rev", AspeedSCUState, silicon_rev, 0),
    DEFINE_PROP_UINT32("hw-strap1", AspeedSCUState, hw_strap1, 0),
    DEFINE_PROP_UINT32("hw-strap2", AspeedSCUState, hw_strap2, 0),
    DEFINE_PROP_UINT32("hw-prot-key", AspeedSCUState, hw_prot_key, 0),
};

static void aspeed_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_scu_realize;
    device_class_set_legacy_reset(dc, aspeed_scu_reset);
    dc->desc = "ASPEED System Control Unit";
    dc->vmsd = &vmstate_aspeed_scu;
    device_class_set_props(dc, aspeed_scu_properties);
}

static const TypeInfo aspeed_scu_info = {
    .name = TYPE_ASPEED_SCU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_scu_class_init,
    .class_size    = sizeof(AspeedSCUClass),
    .abstract      = true,
};

static void aspeed_2400_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 2400 System Control Unit";
    asc->resets = ast2400_a0_resets;
    asc->calc_hpll = aspeed_2400_scu_calc_hpll;
    asc->get_apb = aspeed_2400_scu_get_apb_freq;
    asc->apb_divider = 2;
    asc->nr_regs = ASPEED_SCU_NR_REGS;
    asc->clkin_25Mhz = false;
    asc->ops = &aspeed_ast2400_scu_ops;
}

static const TypeInfo aspeed_2400_scu_info = {
    .name = TYPE_ASPEED_2400_SCU,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_2400_scu_class_init,
};

static void aspeed_2500_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 2500 System Control Unit";
    asc->resets = ast2500_a1_resets;
    asc->calc_hpll = aspeed_2500_scu_calc_hpll;
    asc->get_apb = aspeed_2400_scu_get_apb_freq;
    asc->apb_divider = 4;
    asc->nr_regs = ASPEED_SCU_NR_REGS;
    asc->clkin_25Mhz = false;
    asc->ops = &aspeed_ast2500_scu_ops;
}

static const TypeInfo aspeed_2500_scu_info = {
    .name = TYPE_ASPEED_2500_SCU,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_2500_scu_class_init,
};

static uint64_t aspeed_ast2600_scu_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_AST2600_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case AST2600_HPLL_EXT:
    case AST2600_EPLL_EXT:
    case AST2600_MPLL_EXT:
        /* PLLs are always "locked" */
        return s->regs[reg] | BIT(31);
    case AST2600_RNG_DATA:
        /*
         * On hardware, RNG_DATA works regardless of the state of the
         * enable bit in RNG_CTRL
         *
         * TODO: Check this is true for ast2600
         */
        s->regs[AST2600_RNG_DATA] = aspeed_scu_get_random();
        break;
    }

    trace_aspeed_scu_read(offset, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_ast2600_scu_write(void *opaque, hwaddr offset,
                                     uint64_t data64, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);
    /* Truncate here so bitwise operations below behave as expected */
    uint32_t data = data64;
    bool prot_data_state = data == ASPEED_SCU_PROT_KEY;
    bool unlocked = s->regs[AST2600_PROT_KEY] && s->regs[AST2600_PROT_KEY2];

    if (reg >= ASPEED_AST2600_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    if ((reg != AST2600_PROT_KEY && reg != AST2600_PROT_KEY2) && !unlocked) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SCU is locked!\n", __func__);
        return;
    }

    trace_aspeed_scu_write(offset, size, data);

    switch (reg) {
    case AST2600_PROT_KEY:
        /*
         * Writing a value to SCU000 will modify both protection
         * registers to each protection register individually.
         */
        s->regs[AST2600_PROT_KEY] = prot_data_state;
        s->regs[AST2600_PROT_KEY2] = prot_data_state;
        return;
    case AST2600_PROT_KEY2:
        s->regs[AST2600_PROT_KEY2] = prot_data_state;
        return;
    case AST2600_HW_STRAP1:
    case AST2600_HW_STRAP2:
        if (s->regs[reg + 2]) {
            return;
        }
        /* fall through */
    case AST2600_SYS_RST_CTRL:
    case AST2600_SYS_RST_CTRL2:
    case AST2600_CLK_STOP_CTRL:
    case AST2600_CLK_STOP_CTRL2:
        /* W1S (Write 1 to set) registers */
        s->regs[reg] |= data;
        return;
    case AST2600_SYS_RST_CTRL_CLR:
    case AST2600_SYS_RST_CTRL2_CLR:
    case AST2600_CLK_STOP_CTRL_CLR:
    case AST2600_CLK_STOP_CTRL2_CLR:
    case AST2600_HW_STRAP1_CLR:
    case AST2600_HW_STRAP2_CLR:
        /*
         * W1C (Write 1 to clear) registers are offset by one address from
         * the data register
         */
        s->regs[reg - 1] &= ~data;
        return;

    case AST2600_RNG_DATA:
    case AST2600_SILICON_REV:
    case AST2600_SILICON_REV2:
    case AST2600_CHIP_ID0:
    case AST2600_CHIP_ID1:
        /* Add read only registers here */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_ast2600_scu_ops = {
    .read = aspeed_ast2600_scu_read,
    .write = aspeed_ast2600_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static const uint32_t ast2600_a3_resets[ASPEED_AST2600_SCU_NR_REGS] = {
    [AST2600_SYS_RST_CTRL]      = 0xF7C3FED8,
    [AST2600_SYS_RST_CTRL2]     = 0x0DFFFFFC,
    [AST2600_CLK_STOP_CTRL]     = 0xFFFF7F8A,
    [AST2600_CLK_STOP_CTRL2]    = 0xFFF0FFF0,
    [AST2600_DEBUG_CTRL]        = 0x00000FFF,
    [AST2600_DEBUG_CTRL2]       = 0x000000FF,
    [AST2600_SDRAM_HANDSHAKE]   = 0x00000000,
    [AST2600_HPLL_PARAM]        = 0x1000408F,
    [AST2600_APLL_PARAM]        = 0x1000405F,
    [AST2600_MPLL_PARAM]        = 0x1008405F,
    [AST2600_EPLL_PARAM]        = 0x1004077F,
    [AST2600_DPLL_PARAM]        = 0x1078405F,
    [AST2600_CLK_SEL]           = 0xF3940000,
    [AST2600_CLK_SEL2]          = 0x00700000,
    [AST2600_CLK_SEL3]          = 0x00000000,
    [AST2600_CLK_SEL4]          = 0xF3F40000,
    [AST2600_CLK_SEL5]          = 0x30000000,
    [AST2600_UARTCLK]           = 0x00014506,
    [AST2600_HUARTCLK]          = 0x000145C0,
    [AST2600_CHIP_ID0]          = 0x1234ABCD,
    [AST2600_CHIP_ID1]          = 0x88884444,
};

static void aspeed_ast2600_scu_reset(DeviceState *dev)
{
    AspeedSCUState *s = ASPEED_SCU(dev);
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(dev);

    memcpy(s->regs, asc->resets, asc->nr_regs * 4);

    /*
     * A0 reports A0 in _REV, but subsequent revisions report A1 regardless
     * of actual revision. QEMU and Linux only support A1 onwards so this is
     * sufficient.
     */
    s->regs[AST2600_SILICON_REV] = AST2600_A3_SILICON_REV;
    s->regs[AST2600_SILICON_REV2] = s->silicon_rev;
    s->regs[AST2600_HW_STRAP1] = s->hw_strap1;
    s->regs[AST2600_HW_STRAP2] = s->hw_strap2;
    s->regs[PROT_KEY] = s->hw_prot_key;
}

static void aspeed_2600_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 2600 System Control Unit";
    device_class_set_legacy_reset(dc, aspeed_ast2600_scu_reset);
    asc->resets = ast2600_a3_resets;
    asc->calc_hpll = aspeed_2600_scu_calc_hpll;
    asc->get_apb = aspeed_2600_scu_get_apb_freq;
    asc->apb_divider = 4;
    asc->nr_regs = ASPEED_AST2600_SCU_NR_REGS;
    asc->clkin_25Mhz = true;
    asc->ops = &aspeed_ast2600_scu_ops;
}

static const TypeInfo aspeed_2600_scu_info = {
    .name = TYPE_ASPEED_2600_SCU,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_2600_scu_class_init,
};

static uint64_t aspeed_ast2700_scu_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_AST2700_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        return 0;
    }

    switch (reg) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unhandled read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_aspeed_ast2700_scu_read(offset, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_ast2700_scu_write(void *opaque, hwaddr offset,
                                     uint64_t data64, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);
    /* Truncate here so bitwise operations below behave as expected */
    uint32_t data = data64;

    if (reg >= ASPEED_AST2700_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        return;
    }

    trace_aspeed_ast2700_scu_write(offset, size, data);

    switch (reg) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unhandled write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_ast2700_scu_ops = {
    .read = aspeed_ast2700_scu_read,
    .write = aspeed_ast2700_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .valid.unaligned = false,
};

static const uint32_t ast2700_a0_resets[ASPEED_AST2700_SCU_NR_REGS] = {
    [AST2700_HW_STRAP1_CLR]         = 0xFFF0FFF0,
    [AST2700_HW_STRAP1_LOCK]        = 0x00000FFF,
    [AST2700_HW_STRAP1_SEC1]        = 0x000000FF,
    [AST2700_HW_STRAP1_SEC2]        = 0x00000000,
    [AST2700_HW_STRAP1_SEC3]        = 0x1000408F,
    [AST2700_SCU_HPLL_PARAM]        = 0x0000009f,
    [AST2700_SCU_HPLL_EXT_PARAM]    = 0x8000004f,
    [AST2700_SCU_DPLL_PARAM]        = 0x0080009f,
    [AST2700_SCU_DPLL_EXT_PARAM]    = 0x8000004f,
    [AST2700_SCU_MPLL_PARAM]        = 0x00000040,
    [AST2700_SCU_MPLL_EXT_PARAM]    = 0x80000000,
    [AST2700_SCU_D1CLK_PARAM]       = 0x00050002,
    [AST2700_SCU_D2CLK_PARAM]       = 0x00050002,
    [AST2700_SCU_CRT1CLK_PARAM]     = 0x00050002,
    [AST2700_SCU_CRT2CLK_PARAM]     = 0x00050002,
    [AST2700_SCU_MPHYCLK_PARAM]     = 0x0000004c,
    [AST2700_SCU_FREQ_CNTR]         = 0x000375eb,
    [AST2700_SCU_CPU_SCRATCH_0]     = 0x00000000,
    [AST2700_SCU_CPU_SCRATCH_1]     = 0x00000004,
    [AST2700_SCU_VGA_SCRATCH_0]     = 0x00000040,
};

static void aspeed_ast2700_scu_reset(DeviceState *dev)
{
    AspeedSCUState *s = ASPEED_SCU(dev);
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(dev);

    memcpy(s->regs, asc->resets, asc->nr_regs * 4);
    s->regs[AST2700_SILICON_REV] = s->silicon_rev;
    s->regs[AST2700_HW_STRAP1] = s->hw_strap1;
}

static void aspeed_2700_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 2700 System Control Unit";
    device_class_set_legacy_reset(dc, aspeed_ast2700_scu_reset);
    asc->resets = ast2700_a0_resets;
    asc->calc_hpll = aspeed_2600_scu_calc_hpll;
    asc->get_apb = aspeed_2700_scu_get_apb_freq;
    asc->apb_divider = 4;
    asc->nr_regs = ASPEED_AST2700_SCU_NR_REGS;
    asc->clkin_25Mhz = true;
    asc->ops = &aspeed_ast2700_scu_ops;
}

static uint64_t aspeed_ast2700_scuio_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);
    if (reg >= ASPEED_AST2700_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        return 0;
    }

    switch (reg) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unhandled read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_aspeed_ast2700_scuio_read(offset, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_ast2700_scuio_write(void *opaque, hwaddr offset,
                                     uint64_t data64, unsigned size)
{
    AspeedSCUState *s = ASPEED_SCU(opaque);
    int reg = TO_REG(offset);
    /* Truncate here so bitwise operations below behave as expected */
    uint32_t data = data64;
    bool updated = false;

    if (reg >= ASPEED_AST2700_SCU_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        return;
    }

    trace_aspeed_ast2700_scuio_write(offset, size, data);

    switch (reg) {
    case AST2700_SCUIO_CLK_STOP_CTL_1:
    case AST2700_SCUIO_CLK_STOP_CTL_2:
        s->regs[reg] |= data;
        updated = true;
        break;
    case AST2700_SCUIO_CLK_STOP_CLR_1:
    case AST2700_SCUIO_CLK_STOP_CLR_2:
        s->regs[reg - 1] ^= data;
        updated = true;
        break;
    case AST2700_SCUIO_FREQ_CNT_CTL:
        s->regs[reg] = deposit32(s->regs[reg], 6, 1, !!(data & BIT(1)));
        updated = true;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unhandled write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    if (!updated) {
        s->regs[reg] = data;
    }
}

static const MemoryRegionOps aspeed_ast2700_scuio_ops = {
    .read = aspeed_ast2700_scuio_read,
    .write = aspeed_ast2700_scuio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .valid.unaligned = false,
};

static const uint32_t ast2700_a0_resets_io[ASPEED_AST2700_SCU_NR_REGS] = {
    [AST2700_HW_STRAP1_CLR]             = 0xFFF0FFF0,
    [AST2700_HW_STRAP1_LOCK]            = 0x00000FFF,
    [AST2700_HW_STRAP1_SEC1]            = 0x000000FF,
    [AST2700_HW_STRAP1_SEC2]            = 0x00000000,
    [AST2700_HW_STRAP1_SEC3]            = 0x1000408F,
    [AST2700_SCUIO_CLK_STOP_CTL_1]      = 0xffff8400,
    [AST2700_SCUIO_CLK_STOP_CTL_2]      = 0x00005f30,
    [AST2700_SCUIO_CLK_SEL_1]           = 0x86900000,
    [AST2700_SCUIO_CLK_SEL_2]           = 0x00400000,
    [AST2700_SCUIO_HPLL_PARAM]          = 0x10000027,
    [AST2700_SCUIO_HPLL_EXT_PARAM]      = 0x80000014,
    [AST2700_SCUIO_APLL_PARAM]          = 0x1000001f,
    [AST2700_SCUIO_APLL_EXT_PARAM]      = 0x8000000f,
    [AST2700_SCUIO_DPLL_PARAM]          = 0x106e42ce,
    [AST2700_SCUIO_DPLL_EXT_PARAM]      = 0x80000167,
    [AST2700_SCUIO_DPLL_PARAM_READ]     = 0x106e42ce,
    [AST2700_SCUIO_DPLL_EXT_PARAM_READ] = 0x80000167,
    [AST2700_SCUIO_UARTCLK_GEN]         = 0x00014506,
    [AST2700_SCUIO_HUARTCLK_GEN]        = 0x000145c0,
    [AST2700_SCUIO_CLK_DUTY_MEAS_RST]   = 0x0c9100d2,
    [AST2700_SCUIO_FREQ_CNT_CTL]        = 0x00000080,
};

static void aspeed_2700_scuio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 2700 System Control Unit I/O";
    device_class_set_legacy_reset(dc, aspeed_ast2700_scu_reset);
    asc->resets = ast2700_a0_resets_io;
    asc->calc_hpll = aspeed_2600_scu_calc_hpll;
    asc->get_apb = aspeed_2700_scuio_get_apb_freq;
    asc->apb_divider = 2;
    asc->nr_regs = ASPEED_AST2700_SCU_NR_REGS;
    asc->clkin_25Mhz = true;
    asc->ops = &aspeed_ast2700_scuio_ops;
}

static const TypeInfo aspeed_2700_scu_info = {
    .name = TYPE_ASPEED_2700_SCU,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_2700_scu_class_init,
};

static const TypeInfo aspeed_2700_scuio_info = {
    .name = TYPE_ASPEED_2700_SCUIO,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_2700_scuio_class_init,
};

static const uint32_t ast1030_a1_resets[ASPEED_AST2600_SCU_NR_REGS] = {
    [AST2600_SYS_RST_CTRL]      = 0xFFC3FED8,
    [AST2600_SYS_RST_CTRL2]     = 0x09FFFFFC,
    [AST2600_CLK_STOP_CTRL]     = 0xFFFF7F8A,
    [AST2600_CLK_STOP_CTRL2]    = 0xFFF0FFF0,
    [AST2600_DEBUG_CTRL2]       = 0x00000000,
    [AST2600_HPLL_PARAM]        = 0x10004077,
    [AST2600_HPLL_EXT]          = 0x00000031,
    [AST2600_CLK_SEL4]          = 0x43F90900,
    [AST2600_CLK_SEL5]          = 0x40000000,
    [AST2600_CHIP_ID0]          = 0xDEADBEEF,
    [AST2600_CHIP_ID1]          = 0x0BADCAFE,
};

static void aspeed_ast1030_scu_reset(DeviceState *dev)
{
    AspeedSCUState *s = ASPEED_SCU(dev);
    AspeedSCUClass *asc = ASPEED_SCU_GET_CLASS(dev);

    memcpy(s->regs, asc->resets, asc->nr_regs * 4);

    s->regs[AST2600_SILICON_REV] = AST1030_A1_SILICON_REV;
    s->regs[AST2600_SILICON_REV2] = s->silicon_rev;
    s->regs[AST2600_HW_STRAP1] = s->hw_strap1;
    s->regs[AST2600_HW_STRAP2] = s->hw_strap2;
    s->regs[PROT_KEY] = s->hw_prot_key;
}

static void aspeed_1030_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSCUClass *asc = ASPEED_SCU_CLASS(klass);

    dc->desc = "ASPEED 1030 System Control Unit";
    device_class_set_legacy_reset(dc, aspeed_ast1030_scu_reset);
    asc->resets = ast1030_a1_resets;
    asc->calc_hpll = aspeed_2600_scu_calc_hpll;
    asc->get_apb = aspeed_1030_scu_get_apb_freq;
    asc->apb_divider = 2;
    asc->nr_regs = ASPEED_AST2600_SCU_NR_REGS;
    asc->clkin_25Mhz = true;
    asc->ops = &aspeed_ast2600_scu_ops;
}

static const TypeInfo aspeed_1030_scu_info = {
    .name = TYPE_ASPEED_1030_SCU,
    .parent = TYPE_ASPEED_SCU,
    .instance_size = sizeof(AspeedSCUState),
    .class_init = aspeed_1030_scu_class_init,
};

static void aspeed_scu_register_types(void)
{
    type_register_static(&aspeed_scu_info);
    type_register_static(&aspeed_2400_scu_info);
    type_register_static(&aspeed_2500_scu_info);
    type_register_static(&aspeed_2600_scu_info);
    type_register_static(&aspeed_1030_scu_info);
    type_register_static(&aspeed_2700_scu_info);
    type_register_static(&aspeed_2700_scuio_info);
}

type_init(aspeed_scu_register_types);
