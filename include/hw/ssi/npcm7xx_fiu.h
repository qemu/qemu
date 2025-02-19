/*
 * Nuvoton NPCM7xx Flash Interface Unit (FIU)
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
#ifndef NPCM7XX_FIU_H
#define NPCM7XX_FIU_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_FIU_NR_REGS (0x7c / sizeof(uint32_t))

typedef struct NPCM7xxFIUState NPCM7xxFIUState;

/**
 * struct NPCM7xxFIUFlash - Per-chipselect flash controller state.
 * @direct_access: Memory region for direct flash access.
 * @fiu: Pointer to flash controller shared state.
 */
typedef struct NPCM7xxFIUFlash {
    MemoryRegion direct_access;
    NPCM7xxFIUState *fiu;
} NPCM7xxFIUFlash;

/**
 * NPCM7xxFIUState - Device state for one Flash Interface Unit.
 * @parent: System bus device.
 * @mmio: Memory region for register access.
 * @cs_count: Number of flash chips that may be connected to this module.
 * @active_cs: Currently active chip select, or -1 if no chip is selected.
 * @cs_lines: GPIO lines that may be wired to flash chips.
 * @flash: Array of @cs_count per-flash-chip state objects.
 * @spi: The SPI bus mastered by this controller.
 * @regs: Register contents.
 *
 * Each FIU has a shared bank of registers, and controls up to four chip
 * selects. Each chip select has a dedicated memory region which may be used to
 * read and write the flash connected to that chip select as if it were memory.
 */
struct NPCM7xxFIUState {
    SysBusDevice parent;

    MemoryRegion mmio;

    int32_t cs_count;
    int32_t active_cs;
    qemu_irq *cs_lines;
    uint64_t flash_size;
    NPCM7xxFIUFlash *flash;

    SSIBus *spi;

    uint32_t regs[NPCM7XX_FIU_NR_REGS];
};

#define TYPE_NPCM7XX_FIU "npcm7xx-fiu"
#define NPCM7XX_FIU(obj) OBJECT_CHECK(NPCM7xxFIUState, (obj), TYPE_NPCM7XX_FIU)

#endif /* NPCM7XX_FIU_H */
