/*
 * Nuvoton NPCM7xx Memory Controller stub
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
#ifndef NPCM7XX_MC_H
#define NPCM7XX_MC_H

#include "exec/memory.h"
#include "hw/sysbus.h"

/**
 * struct NPCM7xxMCState - Device state for the memory controller.
 * @parent: System bus device.
 * @mmio: Memory region through which registers are accessed.
 */
typedef struct NPCM7xxMCState {
    SysBusDevice parent;

    MemoryRegion mmio;
} NPCM7xxMCState;

#define TYPE_NPCM7XX_MC "npcm7xx-mc"
#define NPCM7XX_MC(obj) OBJECT_CHECK(NPCM7xxMCState, (obj), TYPE_NPCM7XX_MC)

#endif /* NPCM7XX_MC_H */
