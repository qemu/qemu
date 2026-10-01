/*
 * PRCM (Clock and Reset Controller) in Tenstorrent Atlantis SoC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Tenstorrent
 */

#ifndef TT_ATLANTIS_PRCM_H
#define TT_ATLANTIS_PRCM_H

#include "hw/core/sysbus.h"

#define TYPE_TT_ATLANTIS_PRCM "tt.atlantis.prcm"
#define TYPE_TT_ATLANTIS_PRCM_RCPU "tt.atlantis.prcm.rcpu"
#define TYPE_TT_ATLANTIS_PRCM_HSIO "tt.atlantis.prcm.hsio"
#define TYPE_TT_ATLANTIS_PRCM_PCIE "tt.atlantis.prcm.pcie"
#define TYPE_TT_ATLANTIS_PRCM_MM "tt.atlantis.prcm.mm"
#define TYPE_TT_ATLANTIS_PRCM_DDRC0 "tt.atlantis.prcm.ddrc0"
#define TYPE_TT_ATLANTIS_PRCM_DDRC1 "tt.atlantis.prcm.ddrc1"
OBJECT_DECLARE_TYPE(TTAtlantisPRCMState, TTAtlantisPRCMClass,
                    TT_ATLANTIS_PRCM);


enum {
    TT_ATLANTIS_PRCM_DOMAIN_RCPU = 0,
    TT_ATLANTIS_PRCM_DOMAIN_HSIO = 1,
    TT_ATLANTIS_PRCM_DOMAIN_PCIE = 2,
    TT_ATLANTIS_PRCM_DOMAIN_MM = 3,
    TT_ATLANTIS_PRCM_DOMAIN_DDRC0 = 4,
    TT_ATLANTIS_PRCM_DOMAIN_DDRC1 = 5,
    TT_ATLANTIS_PRCM_DOMAIN_COUNT = 6
};

struct TTAtlantisPRCMState {
    SysBusDevice parent;

    MemoryRegion mmio;

    uint32_t *regs;
};

struct TTAtlantisPRCMClass {
    SysBusDeviceClass parent_class;

    uint32_t domain;
    uint32_t regs_size;
    const MemoryRegionOps *ops;
};

#endif
