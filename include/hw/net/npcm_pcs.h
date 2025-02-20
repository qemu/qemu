/*
 * Nuvoton NPCM8xx PCS Module
 *
 * Copyright 2022 Google LLC
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

#ifndef NPCM_PCS_H
#define NPCM_PCS_H

#include "hw/sysbus.h"

#define NPCM_PCS_NR_SR_CTLS     (0x12 / sizeof(uint16_t))
#define NPCM_PCS_NR_SR_MIIS     (0x20 / sizeof(uint16_t))
#define NPCM_PCS_NR_SR_TIMS     (0x22 / sizeof(uint16_t))
#define NPCM_PCS_NR_VR_MIIS     (0x1c6 / sizeof(uint16_t))

struct NPCMPCSState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint16_t indirect_access_base;
    uint16_t sr_ctl[NPCM_PCS_NR_SR_CTLS];
    uint16_t sr_mii[NPCM_PCS_NR_SR_MIIS];
    uint16_t sr_tim[NPCM_PCS_NR_SR_TIMS];
    uint16_t vr_mii[NPCM_PCS_NR_VR_MIIS];
};

#define TYPE_NPCM_PCS "npcm-pcs"
OBJECT_DECLARE_SIMPLE_TYPE(NPCMPCSState, NPCM_PCS)

#endif /* NPCM_PCS_H */
