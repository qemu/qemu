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
#ifndef NPCM7XX_CLK_H
#define NPCM7XX_CLK_H

#include "exec/memory.h"
#include "hw/clock.h"
#include "hw/sysbus.h"

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_CLK_NR_REGS             (0x70 / sizeof(uint32_t))

#define NPCM7XX_WATCHDOG_RESET_GPIO_IN "npcm7xx-clk-watchdog-reset-gpio-in"

/* Maximum amount of clock inputs in a SEL module. */
#define NPCM7XX_CLK_SEL_MAX_INPUT 5

/* PLLs in CLK module. */
typedef enum NPCM7xxClockPLL {
    NPCM7XX_CLOCK_PLL0,
    NPCM7XX_CLOCK_PLL1,
    NPCM7XX_CLOCK_PLL2,
    NPCM7XX_CLOCK_PLLG,
    NPCM7XX_CLOCK_NR_PLLS,
} NPCM7xxClockPLL;

/* SEL/MUX in CLK module. */
typedef enum NPCM7xxClockSEL {
    NPCM7XX_CLOCK_PIXCKSEL,
    NPCM7XX_CLOCK_MCCKSEL,
    NPCM7XX_CLOCK_CPUCKSEL,
    NPCM7XX_CLOCK_CLKOUTSEL,
    NPCM7XX_CLOCK_UARTCKSEL,
    NPCM7XX_CLOCK_TIMCKSEL,
    NPCM7XX_CLOCK_SDCKSEL,
    NPCM7XX_CLOCK_GFXMSEL,
    NPCM7XX_CLOCK_SUCKSEL,
    NPCM7XX_CLOCK_NR_SELS,
} NPCM7xxClockSEL;

/* Dividers in CLK module. */
typedef enum NPCM7xxClockDivider {
    NPCM7XX_CLOCK_PLL1D2, /* PLL1/2 */
    NPCM7XX_CLOCK_PLL2D2, /* PLL2/2 */
    NPCM7XX_CLOCK_MC_DIVIDER,
    NPCM7XX_CLOCK_AXI_DIVIDER,
    NPCM7XX_CLOCK_AHB_DIVIDER,
    NPCM7XX_CLOCK_AHB3_DIVIDER,
    NPCM7XX_CLOCK_SPI0_DIVIDER,
    NPCM7XX_CLOCK_SPIX_DIVIDER,
    NPCM7XX_CLOCK_APB1_DIVIDER,
    NPCM7XX_CLOCK_APB2_DIVIDER,
    NPCM7XX_CLOCK_APB3_DIVIDER,
    NPCM7XX_CLOCK_APB4_DIVIDER,
    NPCM7XX_CLOCK_APB5_DIVIDER,
    NPCM7XX_CLOCK_CLKOUT_DIVIDER,
    NPCM7XX_CLOCK_UART_DIVIDER,
    NPCM7XX_CLOCK_TIMER_DIVIDER,
    NPCM7XX_CLOCK_ADC_DIVIDER,
    NPCM7XX_CLOCK_MMC_DIVIDER,
    NPCM7XX_CLOCK_SDHC_DIVIDER,
    NPCM7XX_CLOCK_GFXM_DIVIDER, /* divide by 3 */
    NPCM7XX_CLOCK_UTMI_DIVIDER,
    NPCM7XX_CLOCK_NR_DIVIDERS,
} NPCM7xxClockConverter;

typedef struct NPCM7xxCLKState NPCM7xxCLKState;

/**
 * struct NPCM7xxClockPLLState - A PLL module in CLK module.
 * @name: The name of the module.
 * @clk: The CLK module that owns this module.
 * @clock_in: The input clock of this module.
 * @clock_out: The output clock of this module.
 * @reg: The control registers for this PLL module.
 */
typedef struct NPCM7xxClockPLLState {
    DeviceState parent;

    const char *name;
    NPCM7xxCLKState *clk;
    Clock *clock_in;
    Clock *clock_out;

    int reg;
} NPCM7xxClockPLLState;

/**
 * struct NPCM7xxClockSELState - A SEL module in CLK module.
 * @name: The name of the module.
 * @clk: The CLK module that owns this module.
 * @input_size: The size of inputs of this module.
 * @clock_in: The input clocks of this module.
 * @clock_out: The output clocks of this module.
 * @offset: The offset of this module in the control register.
 * @len: The length of this module in the control register.
 */
typedef struct NPCM7xxClockSELState {
    DeviceState parent;

    const char *name;
    NPCM7xxCLKState *clk;
    uint8_t input_size;
    Clock *clock_in[NPCM7XX_CLK_SEL_MAX_INPUT];
    Clock *clock_out;

    int offset;
    int len;
} NPCM7xxClockSELState;

/**
 * struct NPCM7xxClockDividerState - A Divider module in CLK module.
 * @name: The name of the module.
 * @clk: The CLK module that owns this module.
 * @clock_in: The input clock of this module.
 * @clock_out: The output clock of this module.
 * @divide: The function the divider uses to divide the input.
 * @reg: The index of the control register that contains the divisor.
 * @offset: The offset of the divisor in the control register.
 * @len: The length of the divisor in the control register.
 * @divisor: The divisor for a constant divisor
 */
typedef struct NPCM7xxClockDividerState {
    DeviceState parent;

    const char *name;
    NPCM7xxCLKState *clk;
    Clock *clock_in;
    Clock *clock_out;

    uint32_t (*divide)(struct NPCM7xxClockDividerState *s);
    union {
        struct {
            int reg;
            int offset;
            int len;
        };
        int divisor;
    };
} NPCM7xxClockDividerState;

struct NPCM7xxCLKState {
    SysBusDevice parent;

    MemoryRegion iomem;

    /* Clock converters */
    NPCM7xxClockPLLState plls[NPCM7XX_CLOCK_NR_PLLS];
    NPCM7xxClockSELState sels[NPCM7XX_CLOCK_NR_SELS];
    NPCM7xxClockDividerState dividers[NPCM7XX_CLOCK_NR_DIVIDERS];

    uint32_t regs[NPCM7XX_CLK_NR_REGS];

    /* Time reference for SECCNT and CNTR25M, initialized by power on reset */
    int64_t ref_ns;

    /* The incoming reference clock. */
    Clock *clkref;
};

#define TYPE_NPCM7XX_CLK "npcm7xx-clk"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxCLKState, NPCM7XX_CLK)

#endif /* NPCM7XX_CLK_H */
