/*
 * Nuvoton NPCM7xx/8xx System Global Control Registers.
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
#ifndef NPCM_GCR_H
#define NPCM_GCR_H

#include "system/memory.h"
#include "hw/sysbus.h"
#include "qom/object.h"

/*
 * NPCM7XX PWRON STRAP bit fields
 * 12: SPI0 powered by VSBV3 at 1.8V
 * 11: System flash attached to BMC
 * 10: BSP alternative pins.
 * 9:8: Flash UART command route enabled.
 * 7: Security enabled.
 * 6: HI-Z state control.
 * 5: ECC disabled.
 * 4: Reserved
 * 3: JTAG2 enabled.
 * 2:0: CPU and DRAM clock frequency.
 */
#define NPCM7XX_PWRON_STRAP_SPI0F18                 BIT(12)
#define NPCM7XX_PWRON_STRAP_SFAB                    BIT(11)
#define NPCM7XX_PWRON_STRAP_BSPA                    BIT(10)
#define NPCM7XX_PWRON_STRAP_FUP(x)                  ((x) << 8)
#define     FUP_NORM_UART2      3
#define     FUP_PROG_UART3      2
#define     FUP_PROG_UART2      1
#define     FUP_NORM_UART3      0
#define NPCM7XX_PWRON_STRAP_SECEN                   BIT(7)
#define NPCM7XX_PWRON_STRAP_HIZ                     BIT(6)
#define NPCM7XX_PWRON_STRAP_ECC                     BIT(5)
#define NPCM7XX_PWRON_STRAP_RESERVE1                BIT(4)
#define NPCM7XX_PWRON_STRAP_J2EN                    BIT(3)
#define NPCM7XX_PWRON_STRAP_CKFRQ(x)                (x)
#define     CKFRQ_SKIPINIT      0x000
#define     CKFRQ_DEFAULT       0x111

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM_GCR_MAX_NR_REGS NPCM8XX_GCR_NR_REGS
#define NPCM7XX_GCR_NR_REGS (0x148 / sizeof(uint32_t))
#define NPCM8XX_GCR_NR_REGS (0xf80 / sizeof(uint32_t))

typedef struct NPCMGCRState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[NPCM_GCR_MAX_NR_REGS];

    uint32_t reset_pwron;
    uint32_t reset_mdlr;
    uint32_t reset_intcr3;
    uint32_t reset_scrpad_b;
} NPCMGCRState;

typedef struct NPCMGCRClass {
    SysBusDeviceClass parent;

    size_t nr_regs;
    const uint32_t *cold_reset_values;
} NPCMGCRClass;

#define TYPE_NPCM_GCR "npcm-gcr"
#define TYPE_NPCM7XX_GCR "npcm7xx-gcr"
#define TYPE_NPCM8XX_GCR "npcm8xx-gcr"
OBJECT_DECLARE_TYPE(NPCMGCRState, NPCMGCRClass, NPCM_GCR)

#endif /* NPCM_GCR_H */
