/*
 * Nuvoton NPCM7xx General Purpose Input / Output (GPIO)
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef NPCM7XX_GPIO_H
#define NPCM7XX_GPIO_H

#include "system/memory.h"
#include "hw/sysbus.h"

/* Number of pins managed by each controller. */
#define NPCM7XX_GPIO_NR_PINS (32)

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_GPIO_NR_REGS (0x80 / sizeof(uint32_t))

typedef struct NPCM7xxGPIOState {
    SysBusDevice parent;

    /* Properties to be defined by the SoC */
    uint32_t reset_pu;
    uint32_t reset_pd;
    uint32_t reset_osrc;
    uint32_t reset_odsc;

    MemoryRegion mmio;

    qemu_irq irq;
    qemu_irq output[NPCM7XX_GPIO_NR_PINS];

    uint32_t pin_level;
    uint32_t ext_level;
    uint32_t ext_driven;

    uint32_t regs[NPCM7XX_GPIO_NR_REGS];
} NPCM7xxGPIOState;

#define TYPE_NPCM7XX_GPIO "npcm7xx-gpio"
#define NPCM7XX_GPIO(obj) \
    OBJECT_CHECK(NPCM7xxGPIOState, (obj), TYPE_NPCM7XX_GPIO)

#endif /* NPCM7XX_GPIO_H */
