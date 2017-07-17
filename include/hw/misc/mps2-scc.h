/*
 * ARM MPS2 SCC emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef MPS2_SCC_H
#define MPS2_SCC_H

#include "hw/sysbus.h"

#define TYPE_MPS2_SCC "mps2-scc"
#define MPS2_SCC(obj) OBJECT_CHECK(MPS2SCC, (obj), TYPE_MPS2_SCC)

#define NUM_OSCCLK 3

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t cfg0;
    uint32_t cfg1;
    uint32_t cfg4;
    uint32_t cfgdata_rtn;
    uint32_t cfgdata_out;
    uint32_t cfgctrl;
    uint32_t cfgstat;
    uint32_t dll;
    uint32_t aid;
    uint32_t id;
    uint32_t oscclk[NUM_OSCCLK];
    uint32_t oscclk_reset[NUM_OSCCLK];
} MPS2SCC;

#endif
