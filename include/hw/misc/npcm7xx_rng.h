/*
 * Nuvoton NPCM7xx Random Number Generator.
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
#ifndef NPCM7XX_RNG_H
#define NPCM7XX_RNG_H

#include "hw/sysbus.h"

typedef struct NPCM7xxRNGState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint8_t rngcs;
    uint8_t rngd;
    uint8_t rngmode;
} NPCM7xxRNGState;

#define TYPE_NPCM7XX_RNG "npcm7xx-rng"
#define NPCM7XX_RNG(obj) OBJECT_CHECK(NPCM7xxRNGState, (obj), TYPE_NPCM7XX_RNG)

#endif /* NPCM7XX_RNG_H */
