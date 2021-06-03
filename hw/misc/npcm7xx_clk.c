/*
 * Nuvoton NPCM7xx Clock Control Registers.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "hw/misc/npcm7xx_clk.h"
#include "hw/timer/npcm7xx_timer.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "trace.h"
#include "sysemu/watchdog.h"

/*
 * The reference clock hz, and the SECCNT and CNTR25M registers in this module,
 * is always 25 MHz.
 */
#define NPCM7XX_CLOCK_REF_HZ            (25000000)

/* Register Field Definitions */
#define NPCM7XX_CLK_WDRCR_CA9C  BIT(0) /* Cortex-A9 Cores */

#define PLLCON_LOKI     BIT(31)
#define PLLCON_LOKS     BIT(30)
#define PLLCON_PWDEN    BIT(12)
#define PLLCON_FBDV(con) extract32((con), 16, 12)
#define PLLCON_OTDV2(con) extract32((con), 13, 3)
#define PLLCON_OTDV1(con) extract32((con), 8, 3)
#define PLLCON_INDV(con) extract32((con), 0, 6)

enum NPCM7xxCLKRegisters {
    NPCM7XX_CLK_CLKEN1,
    NPCM7XX_CLK_CLKSEL,
    NPCM7XX_CLK_CLKDIV1,
    NPCM7XX_CLK_PLLCON0,
    NPCM7XX_CLK_PLLCON1,
    NPCM7XX_CLK_SWRSTR,
    NPCM7XX_CLK_IPSRST1         = 0x20 / sizeof(uint32_t),
    NPCM7XX_CLK_IPSRST2,
    NPCM7XX_CLK_CLKEN2,
    NPCM7XX_CLK_CLKDIV2,
    NPCM7XX_CLK_CLKEN3,
    NPCM7XX_CLK_IPSRST3,
    NPCM7XX_CLK_WD0RCR,
    NPCM7XX_CLK_WD1RCR,
    NPCM7XX_CLK_WD2RCR,
    NPCM7XX_CLK_SWRSTC1,
    NPCM7XX_CLK_SWRSTC2,
    NPCM7XX_CLK_SWRSTC3,
    NPCM7XX_CLK_SWRSTC4,
    NPCM7XX_CLK_PLLCON2,
    NPCM7XX_CLK_CLKDIV3,
    NPCM7XX_CLK_CORSTC,
    NPCM7XX_CLK_PLLCONG,
    NPCM7XX_CLK_AHBCKFI,
    NPCM7XX_CLK_SECCNT,
    NPCM7XX_CLK_CNTR25M,
    NPCM7XX_CLK_REGS_END,
};

/*
 * These reset values were taken from version 0.91 of the NPCM750R data sheet.
 *
 * All are loaded on power-up reset. CLKENx and SWRSTR should also be loaded on
 * core domain reset, but this reset type is not yet supported by QEMU.
 */
static const uint32_t cold_reset_values[NPCM7XX_CLK_NR_REGS] = {
    [NPCM7XX_CLK_CLKEN1]        = 0xffffffff,
    [NPCM7XX_CLK_CLKSEL]        = 0x004aaaaa,
    [NPCM7XX_CLK_CLKDIV1]       = 0x5413f855,
    [NPCM7XX_CLK_PLLCON0]       = 0x00222101 | PLLCON_LOKI,
    [NPCM7XX_CLK_PLLCON1]       = 0x00202101 | PLLCON_LOKI,
    [NPCM7XX_CLK_IPSRST1]       = 0x00001000,
    [NPCM7XX_CLK_IPSRST2]       = 0x80000000,
    [NPCM7XX_CLK_CLKEN2]        = 0xffffffff,
    [NPCM7XX_CLK_CLKDIV2]       = 0xaa4f8f9f,
    [NPCM7XX_CLK_CLKEN3]        = 0xffffffff,
    [NPCM7XX_CLK_IPSRST3]       = 0x03000000,
    [NPCM7XX_CLK_WD0RCR]        = 0xffffffff,
    [NPCM7XX_CLK_WD1RCR]        = 0xffffffff,
    [NPCM7XX_CLK_WD2RCR]        = 0xffffffff,
    [NPCM7XX_CLK_SWRSTC1]       = 0x00000003,
    [NPCM7XX_CLK_PLLCON2]       = 0x00c02105 | PLLCON_LOKI,
    [NPCM7XX_CLK_CORSTC]        = 0x04000003,
    [NPCM7XX_CLK_PLLCONG]       = 0x01228606 | PLLCON_LOKI,
    [NPCM7XX_CLK_AHBCKFI]       = 0x000000c8,
};

/* The number of watchdogs that can trigger a reset. */
#define NPCM7XX_NR_WATCHDOGS    (3)

/* Clock converter functions */

#define TYPE_NPCM7XX_CLOCK_PLL "npcm7xx-clock-pll"
#define NPCM7XX_CLOCK_PLL(obj) OBJECT_CHECK(NPCM7xxClockPLLState, \
        (obj), TYPE_NPCM7XX_CLOCK_PLL)
#define TYPE_NPCM7XX_CLOCK_SEL "npcm7xx-clock-sel"
#define NPCM7XX_CLOCK_SEL(obj) OBJECT_CHECK(NPCM7xxClockSELState, \
        (obj), TYPE_NPCM7XX_CLOCK_SEL)
#define TYPE_NPCM7XX_CLOCK_DIVIDER "npcm7xx-clock-divider"
#define NPCM7XX_CLOCK_DIVIDER(obj) OBJECT_CHECK(NPCM7xxClockDividerState, \
        (obj), TYPE_NPCM7XX_CLOCK_DIVIDER)

static void npcm7xx_clk_update_pll(void *opaque)
{
    NPCM7xxClockPLLState *s = opaque;
    uint32_t con = s->clk->regs[s->reg];
    uint64_t freq;

    /* The PLL is grounded if it is not locked yet. */
    if (con & PLLCON_LOKI) {
        freq = clock_get_hz(s->clock_in);
        freq *= PLLCON_FBDV(con);
        freq /= PLLCON_INDV(con) * PLLCON_OTDV1(con) * PLLCON_OTDV2(con);
    } else {
        freq = 0;
    }

    clock_update_hz(s->clock_out, freq);
}

static void npcm7xx_clk_update_sel(void *opaque)
{
    NPCM7xxClockSELState *s = opaque;
    uint32_t index = extract32(s->clk->regs[NPCM7XX_CLK_CLKSEL], s->offset,
            s->len);

    if (index >= s->input_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: SEL index: %u out of range\n",
                      __func__, index);
        index = 0;
    }
    clock_update_hz(s->clock_out, clock_get_hz(s->clock_in[index]));
}

static void npcm7xx_clk_update_divider(void *opaque)
{
    NPCM7xxClockDividerState *s = opaque;
    uint32_t freq;

    freq = s->divide(s);
    clock_update_hz(s->clock_out, freq);
}

static uint32_t divide_by_constant(NPCM7xxClockDividerState *s)
{
    return clock_get_hz(s->clock_in) / s->divisor;
}

static uint32_t divide_by_reg_divisor(NPCM7xxClockDividerState *s)
{
    return clock_get_hz(s->clock_in) /
            (extract32(s->clk->regs[s->reg], s->offset, s->len) + 1);
}

static uint32_t divide_by_reg_divisor_times_2(NPCM7xxClockDividerState *s)
{
    return divide_by_reg_divisor(s) / 2;
}

static uint32_t shift_by_reg_divisor(NPCM7xxClockDividerState *s)
{
    return clock_get_hz(s->clock_in) >>
        extract32(s->clk->regs[s->reg], s->offset, s->len);
}

static NPCM7xxClockPLL find_pll_by_reg(enum NPCM7xxCLKRegisters reg)
{
    switch (reg) {
    case NPCM7XX_CLK_PLLCON0:
        return NPCM7XX_CLOCK_PLL0;
    case NPCM7XX_CLK_PLLCON1:
        return NPCM7XX_CLOCK_PLL1;
    case NPCM7XX_CLK_PLLCON2:
        return NPCM7XX_CLOCK_PLL2;
    case NPCM7XX_CLK_PLLCONG:
        return NPCM7XX_CLOCK_PLLG;
    default:
        g_assert_not_reached();
    }
}

static void npcm7xx_clk_update_all_plls(NPCM7xxCLKState *clk)
{
    int i;

    for (i = 0; i < NPCM7XX_CLOCK_NR_PLLS; ++i) {
        npcm7xx_clk_update_pll(&clk->plls[i]);
    }
}

static void npcm7xx_clk_update_all_sels(NPCM7xxCLKState *clk)
{
    int i;

    for (i = 0; i < NPCM7XX_CLOCK_NR_SELS; ++i) {
        npcm7xx_clk_update_sel(&clk->sels[i]);
    }
}

static void npcm7xx_clk_update_all_dividers(NPCM7xxCLKState *clk)
{
    int i;

    for (i = 0; i < NPCM7XX_CLOCK_NR_DIVIDERS; ++i) {
        npcm7xx_clk_update_divider(&clk->dividers[i]);
    }
}

static void npcm7xx_clk_update_all_clocks(NPCM7xxCLKState *clk)
{
    clock_update_hz(clk->clkref, NPCM7XX_CLOCK_REF_HZ);
    npcm7xx_clk_update_all_plls(clk);
    npcm7xx_clk_update_all_sels(clk);
    npcm7xx_clk_update_all_dividers(clk);
}

/* Types of clock sources. */
typedef enum ClockSrcType {
    CLKSRC_REF,
    CLKSRC_PLL,
    CLKSRC_SEL,
    CLKSRC_DIV,
} ClockSrcType;

typedef struct PLLInitInfo {
    const char *name;
    ClockSrcType src_type;
    int src_index;
    int reg;
    const char *public_name;
} PLLInitInfo;

typedef struct SELInitInfo {
    const char *name;
    uint8_t input_size;
    ClockSrcType src_type[NPCM7XX_CLK_SEL_MAX_INPUT];
    int src_index[NPCM7XX_CLK_SEL_MAX_INPUT];
    int offset;
    int len;
    const char *public_name;
} SELInitInfo;

typedef struct DividerInitInfo {
    const char *name;
    ClockSrcType src_type;
    int src_index;
    uint32_t (*divide)(NPCM7xxClockDividerState *s);
    int reg; /* not used when type == CONSTANT */
    int offset; /* not used when type == CONSTANT */
    int len; /* not used when type == CONSTANT */
    int divisor; /* used only when type == CONSTANT */
    const char *public_name;
} DividerInitInfo;

static const PLLInitInfo pll_init_info_list[] = {
    [NPCM7XX_CLOCK_PLL0] = {
        .name = "pll0",
        .src_type = CLKSRC_REF,
        .reg = NPCM7XX_CLK_PLLCON0,
    },
    [NPCM7XX_CLOCK_PLL1] = {
        .name = "pll1",
        .src_type = CLKSRC_REF,
        .reg = NPCM7XX_CLK_PLLCON1,
    },
    [NPCM7XX_CLOCK_PLL2] = {
        .name = "pll2",
        .src_type = CLKSRC_REF,
        .reg = NPCM7XX_CLK_PLLCON2,
    },
    [NPCM7XX_CLOCK_PLLG] = {
        .name = "pllg",
        .src_type = CLKSRC_REF,
        .reg = NPCM7XX_CLK_PLLCONG,
    },
};

static const SELInitInfo sel_init_info_list[] = {
    [NPCM7XX_CLOCK_PIXCKSEL] = {
        .name = "pixcksel",
        .input_size = 2,
        .src_type = {CLKSRC_PLL, CLKSRC_REF},
        .src_index = {NPCM7XX_CLOCK_PLLG, 0},
        .offset = 5,
        .len = 1,
        .public_name = "pixel-clock",
    },
    [NPCM7XX_CLOCK_MCCKSEL] = {
        .name = "mccksel",
        .input_size = 4,
        .src_type = {CLKSRC_DIV, CLKSRC_REF, CLKSRC_REF,
            /*MCBPCK, shouldn't be used in normal operation*/
            CLKSRC_REF},
        .src_index = {NPCM7XX_CLOCK_PLL1D2, 0, 0, 0},
        .offset = 12,
        .len = 2,
        .public_name = "mc-phy-clock",
    },
    [NPCM7XX_CLOCK_CPUCKSEL] = {
        .name = "cpucksel",
        .input_size = 4,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF,
            /*SYSBPCK, shouldn't be used in normal operation*/
            CLKSRC_REF},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0, 0},
        .offset = 0,
        .len = 2,
        .public_name = "system-clock",
    },
    [NPCM7XX_CLOCK_CLKOUTSEL] = {
        .name = "clkoutsel",
        .input_size = 5,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF,
            CLKSRC_PLL, CLKSRC_DIV},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0,
            NPCM7XX_CLOCK_PLLG, NPCM7XX_CLOCK_PLL2D2},
        .offset = 18,
        .len = 3,
        .public_name = "tock",
    },
    [NPCM7XX_CLOCK_UARTCKSEL] = {
        .name = "uartcksel",
        .input_size = 4,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF, CLKSRC_DIV},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0,
            NPCM7XX_CLOCK_PLL2D2},
        .offset = 8,
        .len = 2,
    },
    [NPCM7XX_CLOCK_TIMCKSEL] = {
        .name = "timcksel",
        .input_size = 4,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF, CLKSRC_DIV},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0,
            NPCM7XX_CLOCK_PLL2D2},
        .offset = 14,
        .len = 2,
    },
    [NPCM7XX_CLOCK_SDCKSEL] = {
        .name = "sdcksel",
        .input_size = 4,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF, CLKSRC_DIV},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0,
            NPCM7XX_CLOCK_PLL2D2},
        .offset = 6,
        .len = 2,
    },
    [NPCM7XX_CLOCK_GFXMSEL] = {
        .name = "gfxmksel",
        .input_size = 2,
        .src_type = {CLKSRC_REF, CLKSRC_PLL},
        .src_index = {0, NPCM7XX_CLOCK_PLL2},
        .offset = 21,
        .len = 1,
    },
    [NPCM7XX_CLOCK_SUCKSEL] = {
        .name = "sucksel",
        .input_size = 4,
        .src_type = {CLKSRC_PLL, CLKSRC_DIV, CLKSRC_REF, CLKSRC_DIV},
        .src_index = {NPCM7XX_CLOCK_PLL0, NPCM7XX_CLOCK_PLL1D2, 0,
            NPCM7XX_CLOCK_PLL2D2},
        .offset = 10,
        .len = 2,
    },
};

static const DividerInitInfo divider_init_info_list[] = {
    [NPCM7XX_CLOCK_PLL1D2] = {
        .name = "pll1d2",
        .src_type = CLKSRC_PLL,
        .src_index = NPCM7XX_CLOCK_PLL1,
        .divide = divide_by_constant,
        .divisor = 2,
    },
    [NPCM7XX_CLOCK_PLL2D2] = {
        .name = "pll2d2",
        .src_type = CLKSRC_PLL,
        .src_index = NPCM7XX_CLOCK_PLL2,
        .divide = divide_by_constant,
        .divisor = 2,
    },
    [NPCM7XX_CLOCK_MC_DIVIDER] = {
        .name = "mc-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_MCCKSEL,
        .divide = divide_by_constant,
        .divisor = 2,
        .public_name = "mc-clock"
    },
    [NPCM7XX_CLOCK_AXI_DIVIDER] = {
        .name = "axi-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_CPUCKSEL,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 0,
        .len = 1,
        .public_name = "clk2"
    },
    [NPCM7XX_CLOCK_AHB_DIVIDER] = {
        .name = "ahb-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AXI_DIVIDER,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 26,
        .len = 2,
        .public_name = "clk4"
    },
    [NPCM7XX_CLOCK_AHB3_DIVIDER] = {
        .name = "ahb3-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 6,
        .len = 5,
        .public_name = "ahb3-spi3-clock"
    },
    [NPCM7XX_CLOCK_SPI0_DIVIDER] = {
        .name = "spi0-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV3,
        .offset = 6,
        .len = 5,
        .public_name = "spi0-clock",
    },
    [NPCM7XX_CLOCK_SPIX_DIVIDER] = {
        .name = "spix-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV3,
        .offset = 1,
        .len = 5,
        .public_name = "spix-clock",
    },
    [NPCM7XX_CLOCK_APB1_DIVIDER] = {
        .name = "apb1-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 24,
        .len = 2,
        .public_name = "apb1-clock",
    },
    [NPCM7XX_CLOCK_APB2_DIVIDER] = {
        .name = "apb2-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 26,
        .len = 2,
        .public_name = "apb2-clock",
    },
    [NPCM7XX_CLOCK_APB3_DIVIDER] = {
        .name = "apb3-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 28,
        .len = 2,
        .public_name = "apb3-clock",
    },
    [NPCM7XX_CLOCK_APB4_DIVIDER] = {
        .name = "apb4-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 30,
        .len = 2,
        .public_name = "apb4-clock",
    },
    [NPCM7XX_CLOCK_APB5_DIVIDER] = {
        .name = "apb5-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_AHB_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 22,
        .len = 2,
        .public_name = "apb5-clock",
    },
    [NPCM7XX_CLOCK_CLKOUT_DIVIDER] = {
        .name = "clkout-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_CLKOUTSEL,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 16,
        .len = 5,
        .public_name = "clkout",
    },
    [NPCM7XX_CLOCK_UART_DIVIDER] = {
        .name = "uart-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_UARTCKSEL,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 16,
        .len = 5,
        .public_name = "uart-clock",
    },
    [NPCM7XX_CLOCK_TIMER_DIVIDER] = {
        .name = "timer-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_TIMCKSEL,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 21,
        .len = 5,
        .public_name = "timer-clock",
    },
    [NPCM7XX_CLOCK_ADC_DIVIDER] = {
        .name = "adc-divider",
        .src_type = CLKSRC_DIV,
        .src_index = NPCM7XX_CLOCK_TIMER_DIVIDER,
        .divide = shift_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 28,
        .len = 3,
        .public_name = "adc-clock",
    },
    [NPCM7XX_CLOCK_MMC_DIVIDER] = {
        .name = "mmc-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_SDCKSEL,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV1,
        .offset = 11,
        .len = 5,
        .public_name = "mmc-clock",
    },
    [NPCM7XX_CLOCK_SDHC_DIVIDER] = {
        .name = "sdhc-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_SDCKSEL,
        .divide = divide_by_reg_divisor_times_2,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 0,
        .len = 4,
        .public_name = "sdhc-clock",
    },
    [NPCM7XX_CLOCK_GFXM_DIVIDER] = {
        .name = "gfxm-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_GFXMSEL,
        .divide = divide_by_constant,
        .divisor = 3,
        .public_name = "gfxm-clock",
    },
    [NPCM7XX_CLOCK_UTMI_DIVIDER] = {
        .name = "utmi-divider",
        .src_type = CLKSRC_SEL,
        .src_index = NPCM7XX_CLOCK_SUCKSEL,
        .divide = divide_by_reg_divisor,
        .reg = NPCM7XX_CLK_CLKDIV2,
        .offset = 8,
        .len = 5,
        .public_name = "utmi-clock",
    },
};

static void npcm7xx_clk_update_pll_cb(void *opaque, ClockEvent event)
{
    npcm7xx_clk_update_pll(opaque);
}

static void npcm7xx_clk_pll_init(Object *obj)
{
    NPCM7xxClockPLLState *pll = NPCM7XX_CLOCK_PLL(obj);

    pll->clock_in = qdev_init_clock_in(DEVICE(pll), "clock-in",
                                       npcm7xx_clk_update_pll_cb, pll,
                                       ClockUpdate);
    pll->clock_out = qdev_init_clock_out(DEVICE(pll), "clock-out");
}

static void npcm7xx_clk_update_sel_cb(void *opaque, ClockEvent event)
{
    npcm7xx_clk_update_sel(opaque);
}

static void npcm7xx_clk_sel_init(Object *obj)
{
    int i;
    NPCM7xxClockSELState *sel = NPCM7XX_CLOCK_SEL(obj);

    for (i = 0; i < NPCM7XX_CLK_SEL_MAX_INPUT; ++i) {
        sel->clock_in[i] = qdev_init_clock_in(DEVICE(sel),
                g_strdup_printf("clock-in[%d]", i),
                npcm7xx_clk_update_sel_cb, sel, ClockUpdate);
    }
    sel->clock_out = qdev_init_clock_out(DEVICE(sel), "clock-out");
}

static void npcm7xx_clk_update_divider_cb(void *opaque, ClockEvent event)
{
    npcm7xx_clk_update_divider(opaque);
}

static void npcm7xx_clk_divider_init(Object *obj)
{
    NPCM7xxClockDividerState *div = NPCM7XX_CLOCK_DIVIDER(obj);

    div->clock_in = qdev_init_clock_in(DEVICE(div), "clock-in",
                                       npcm7xx_clk_update_divider_cb,
                                       div, ClockUpdate);
    div->clock_out = qdev_init_clock_out(DEVICE(div), "clock-out");
}

static void npcm7xx_init_clock_pll(NPCM7xxClockPLLState *pll,
        NPCM7xxCLKState *clk, const PLLInitInfo *init_info)
{
    pll->name = init_info->name;
    pll->clk = clk;
    pll->reg = init_info->reg;
    if (init_info->public_name != NULL) {
        qdev_alias_clock(DEVICE(pll), "clock-out", DEVICE(clk),
                init_info->public_name);
    }
}

static void npcm7xx_init_clock_sel(NPCM7xxClockSELState *sel,
        NPCM7xxCLKState *clk, const SELInitInfo *init_info)
{
    int input_size = init_info->input_size;

    sel->name = init_info->name;
    sel->clk = clk;
    sel->input_size = init_info->input_size;
    g_assert(input_size <= NPCM7XX_CLK_SEL_MAX_INPUT);
    sel->offset = init_info->offset;
    sel->len = init_info->len;
    if (init_info->public_name != NULL) {
        qdev_alias_clock(DEVICE(sel), "clock-out", DEVICE(clk),
                init_info->public_name);
    }
}

static void npcm7xx_init_clock_divider(NPCM7xxClockDividerState *div,
        NPCM7xxCLKState *clk, const DividerInitInfo *init_info)
{
    div->name = init_info->name;
    div->clk = clk;

    div->divide = init_info->divide;
    if (div->divide == divide_by_constant) {
        div->divisor = init_info->divisor;
    } else {
        div->reg = init_info->reg;
        div->offset = init_info->offset;
        div->len = init_info->len;
    }
    if (init_info->public_name != NULL) {
        qdev_alias_clock(DEVICE(div), "clock-out", DEVICE(clk),
                init_info->public_name);
    }
}

static Clock *npcm7xx_get_clock(NPCM7xxCLKState *clk, ClockSrcType type,
        int index)
{
    switch (type) {
    case CLKSRC_REF:
        return clk->clkref;
    case CLKSRC_PLL:
        return clk->plls[index].clock_out;
    case CLKSRC_SEL:
        return clk->sels[index].clock_out;
    case CLKSRC_DIV:
        return clk->dividers[index].clock_out;
    default:
        g_assert_not_reached();
    }
}

static void npcm7xx_connect_clocks(NPCM7xxCLKState *clk)
{
    int i, j;
    Clock *src;

    for (i = 0; i < NPCM7XX_CLOCK_NR_PLLS; ++i) {
        src = npcm7xx_get_clock(clk, pll_init_info_list[i].src_type,
                pll_init_info_list[i].src_index);
        clock_set_source(clk->plls[i].clock_in, src);
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_SELS; ++i) {
        for (j = 0; j < sel_init_info_list[i].input_size; ++j) {
            src = npcm7xx_get_clock(clk, sel_init_info_list[i].src_type[j],
                    sel_init_info_list[i].src_index[j]);
            clock_set_source(clk->sels[i].clock_in[j], src);
        }
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_DIVIDERS; ++i) {
        src = npcm7xx_get_clock(clk, divider_init_info_list[i].src_type,
                divider_init_info_list[i].src_index);
        clock_set_source(clk->dividers[i].clock_in, src);
    }
}

static uint64_t npcm7xx_clk_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxCLKState *s = opaque;
    int64_t now_ns;
    uint32_t value = 0;

    if (reg >= NPCM7XX_CLK_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case NPCM7XX_CLK_SWRSTR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is write-only\n",
                      __func__, offset);
        break;

    case NPCM7XX_CLK_SECCNT:
        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        value = (now_ns - s->ref_ns) / NANOSECONDS_PER_SECOND;
        break;

    case NPCM7XX_CLK_CNTR25M:
        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        /*
         * This register counts 25 MHz cycles, updating every 640 ns. It rolls
         * over to zero every second.
         *
         * The 4 LSBs are always zero: (1e9 / 640) << 4 = 25000000.
         */
        value = (((now_ns - s->ref_ns) / 640) << 4) % NPCM7XX_CLOCK_REF_HZ;
        break;

    default:
        value = s->regs[reg];
        break;
    };

    trace_npcm7xx_clk_read(offset, value);

    return value;
}

static void npcm7xx_clk_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxCLKState *s = opaque;
    uint32_t value = v;

    trace_npcm7xx_clk_write(offset, value);

    if (reg >= NPCM7XX_CLK_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return;
    }

    switch (reg) {
    case NPCM7XX_CLK_SWRSTR:
        qemu_log_mask(LOG_UNIMP, "%s: SW reset not implemented: 0x%02x\n",
                      __func__, value);
        value = 0;
        break;

    case NPCM7XX_CLK_PLLCON0:
    case NPCM7XX_CLK_PLLCON1:
    case NPCM7XX_CLK_PLLCON2:
    case NPCM7XX_CLK_PLLCONG:
        if (value & PLLCON_PWDEN) {
            /* Power down -- clear lock and indicate loss of lock */
            value &= ~PLLCON_LOKI;
            value |= PLLCON_LOKS;
        } else {
            /* Normal mode -- assume always locked */
            value |= PLLCON_LOKI;
            /* Keep LOKS unchanged unless cleared by writing 1 */
            if (value & PLLCON_LOKS) {
                value &= ~PLLCON_LOKS;
            } else {
                value |= (value & PLLCON_LOKS);
            }
        }
        /* Only update PLL when it is locked. */
        if (value & PLLCON_LOKI) {
            npcm7xx_clk_update_pll(&s->plls[find_pll_by_reg(reg)]);
        }
        break;

    case NPCM7XX_CLK_CLKSEL:
        npcm7xx_clk_update_all_sels(s);
        break;

    case NPCM7XX_CLK_CLKDIV1:
    case NPCM7XX_CLK_CLKDIV2:
    case NPCM7XX_CLK_CLKDIV3:
        npcm7xx_clk_update_all_dividers(s);
        break;

    case NPCM7XX_CLK_CNTR25M:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is read-only\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = value;
}

/* Perform reset action triggered by a watchdog */
static void npcm7xx_clk_perform_watchdog_reset(void *opaque, int n,
        int level)
{
    NPCM7xxCLKState *clk = NPCM7XX_CLK(opaque);
    uint32_t rcr;

    g_assert(n >= 0 && n <= NPCM7XX_NR_WATCHDOGS);
    rcr = clk->regs[NPCM7XX_CLK_WD0RCR + n];
    if (rcr & NPCM7XX_CLK_WDRCR_CA9C) {
        watchdog_perform_action();
    } else {
        qemu_log_mask(LOG_UNIMP,
                "%s: only CPU reset is implemented. (requested 0x%" PRIx32")\n",
                __func__, rcr);
    }
}

static const struct MemoryRegionOps npcm7xx_clk_ops = {
    .read       = npcm7xx_clk_read,
    .write      = npcm7xx_clk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_clk_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxCLKState *s = NPCM7XX_CLK(obj);

    QEMU_BUILD_BUG_ON(sizeof(s->regs) != sizeof(cold_reset_values));

    switch (type) {
    case RESET_TYPE_COLD:
        memcpy(s->regs, cold_reset_values, sizeof(cold_reset_values));
        s->ref_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        npcm7xx_clk_update_all_clocks(s);
        return;
    }

    /*
     * A small number of registers need to be reset on a core domain reset,
     * but no such reset type exists yet.
     */
    qemu_log_mask(LOG_UNIMP, "%s: reset type %d not implemented.",
                  __func__, type);
}

static void npcm7xx_clk_init_clock_hierarchy(NPCM7xxCLKState *s)
{
    int i;

    s->clkref = qdev_init_clock_in(DEVICE(s), "clkref", NULL, NULL, 0);

    /* First pass: init all converter modules */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(pll_init_info_list) != NPCM7XX_CLOCK_NR_PLLS);
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(sel_init_info_list) != NPCM7XX_CLOCK_NR_SELS);
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(divider_init_info_list)
            != NPCM7XX_CLOCK_NR_DIVIDERS);
    for (i = 0; i < NPCM7XX_CLOCK_NR_PLLS; ++i) {
        object_initialize_child(OBJECT(s), pll_init_info_list[i].name,
                &s->plls[i], TYPE_NPCM7XX_CLOCK_PLL);
        npcm7xx_init_clock_pll(&s->plls[i], s,
                &pll_init_info_list[i]);
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_SELS; ++i) {
        object_initialize_child(OBJECT(s), sel_init_info_list[i].name,
                &s->sels[i], TYPE_NPCM7XX_CLOCK_SEL);
        npcm7xx_init_clock_sel(&s->sels[i], s,
                &sel_init_info_list[i]);
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_DIVIDERS; ++i) {
        object_initialize_child(OBJECT(s), divider_init_info_list[i].name,
                &s->dividers[i], TYPE_NPCM7XX_CLOCK_DIVIDER);
        npcm7xx_init_clock_divider(&s->dividers[i], s,
                &divider_init_info_list[i]);
    }

    /* Second pass: connect converter modules */
    npcm7xx_connect_clocks(s);

    clock_update_hz(s->clkref, NPCM7XX_CLOCK_REF_HZ);
}

static void npcm7xx_clk_init(Object *obj)
{
    NPCM7xxCLKState *s = NPCM7XX_CLK(obj);

    memory_region_init_io(&s->iomem, obj, &npcm7xx_clk_ops, s,
                          TYPE_NPCM7XX_CLK, 4 * KiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static int npcm7xx_clk_post_load(void *opaque, int version_id)
{
    if (version_id >= 1) {
        NPCM7xxCLKState *clk = opaque;

        npcm7xx_clk_update_all_clocks(clk);
    }

    return 0;
}

static void npcm7xx_clk_realize(DeviceState *dev, Error **errp)
{
    int i;
    NPCM7xxCLKState *s = NPCM7XX_CLK(dev);

    qdev_init_gpio_in_named(DEVICE(s), npcm7xx_clk_perform_watchdog_reset,
            NPCM7XX_WATCHDOG_RESET_GPIO_IN, NPCM7XX_NR_WATCHDOGS);
    npcm7xx_clk_init_clock_hierarchy(s);

    /* Realize child devices */
    for (i = 0; i < NPCM7XX_CLOCK_NR_PLLS; ++i) {
        if (!qdev_realize(DEVICE(&s->plls[i]), NULL, errp)) {
            return;
        }
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_SELS; ++i) {
        if (!qdev_realize(DEVICE(&s->sels[i]), NULL, errp)) {
            return;
        }
    }
    for (i = 0; i < NPCM7XX_CLOCK_NR_DIVIDERS; ++i) {
        if (!qdev_realize(DEVICE(&s->dividers[i]), NULL, errp)) {
            return;
        }
    }
}

static const VMStateDescription vmstate_npcm7xx_clk_pll = {
    .name = "npcm7xx-clock-pll",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =  (VMStateField[]) {
        VMSTATE_CLOCK(clock_in, NPCM7xxClockPLLState),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_npcm7xx_clk_sel = {
    .name = "npcm7xx-clock-sel",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =  (VMStateField[]) {
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(clock_in, NPCM7xxClockSELState,
                NPCM7XX_CLK_SEL_MAX_INPUT, 0, vmstate_clock, Clock),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_npcm7xx_clk_divider = {
    .name = "npcm7xx-clock-divider",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =  (VMStateField[]) {
        VMSTATE_CLOCK(clock_in, NPCM7xxClockDividerState),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_npcm7xx_clk = {
    .name = "npcm7xx-clk",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = npcm7xx_clk_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxCLKState, NPCM7XX_CLK_NR_REGS),
        VMSTATE_INT64(ref_ns, NPCM7xxCLKState),
        VMSTATE_CLOCK(clkref, NPCM7xxCLKState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_clk_pll_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx Clock PLL Module";
    dc->vmsd = &vmstate_npcm7xx_clk_pll;
}

static void npcm7xx_clk_sel_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx Clock SEL Module";
    dc->vmsd = &vmstate_npcm7xx_clk_sel;
}

static void npcm7xx_clk_divider_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx Clock Divider Module";
    dc->vmsd = &vmstate_npcm7xx_clk_divider;
}

static void npcm7xx_clk_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM7XX_CLK_REGS_END > NPCM7XX_CLK_NR_REGS);

    dc->desc = "NPCM7xx Clock Control Registers";
    dc->vmsd = &vmstate_npcm7xx_clk;
    dc->realize = npcm7xx_clk_realize;
    rc->phases.enter = npcm7xx_clk_enter_reset;
}

static const TypeInfo npcm7xx_clk_pll_info = {
    .name               = TYPE_NPCM7XX_CLOCK_PLL,
    .parent             = TYPE_DEVICE,
    .instance_size      = sizeof(NPCM7xxClockPLLState),
    .instance_init      = npcm7xx_clk_pll_init,
    .class_init         = npcm7xx_clk_pll_class_init,
};

static const TypeInfo npcm7xx_clk_sel_info = {
    .name               = TYPE_NPCM7XX_CLOCK_SEL,
    .parent             = TYPE_DEVICE,
    .instance_size      = sizeof(NPCM7xxClockSELState),
    .instance_init      = npcm7xx_clk_sel_init,
    .class_init         = npcm7xx_clk_sel_class_init,
};

static const TypeInfo npcm7xx_clk_divider_info = {
    .name               = TYPE_NPCM7XX_CLOCK_DIVIDER,
    .parent             = TYPE_DEVICE,
    .instance_size      = sizeof(NPCM7xxClockDividerState),
    .instance_init      = npcm7xx_clk_divider_init,
    .class_init         = npcm7xx_clk_divider_class_init,
};

static const TypeInfo npcm7xx_clk_info = {
    .name               = TYPE_NPCM7XX_CLK,
    .parent             = TYPE_SYS_BUS_DEVICE,
    .instance_size      = sizeof(NPCM7xxCLKState),
    .instance_init      = npcm7xx_clk_init,
    .class_init         = npcm7xx_clk_class_init,
};

static void npcm7xx_clk_register_type(void)
{
    type_register_static(&npcm7xx_clk_pll_info);
    type_register_static(&npcm7xx_clk_sel_info);
    type_register_static(&npcm7xx_clk_divider_info);
    type_register_static(&npcm7xx_clk_info);
}
type_init(npcm7xx_clk_register_type);
