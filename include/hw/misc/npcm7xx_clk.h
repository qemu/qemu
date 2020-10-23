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
#include "hw/sysbus.h"

/*
 * The reference clock frequency for the timer modules, and the SECCNT and
 * CNTR25M registers in this module, is always 25 MHz.
 */
#define NPCM7XX_TIMER_REF_HZ            (25000000)

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_CLK_NR_REGS             (0x70 / sizeof(uint32_t))

#define NPCM7XX_WATCHDOG_RESET_GPIO_IN "npcm7xx-clk-watchdog-reset-gpio-in"

typedef struct NPCM7xxCLKState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[NPCM7XX_CLK_NR_REGS];

    /* Time reference for SECCNT and CNTR25M, initialized by power on reset */
    int64_t ref_ns;
} NPCM7xxCLKState;

#define TYPE_NPCM7XX_CLK "npcm7xx-clk"
#define NPCM7XX_CLK(obj) OBJECT_CHECK(NPCM7xxCLKState, (obj), TYPE_NPCM7XX_CLK)

#endif /* NPCM7XX_CLK_H */
