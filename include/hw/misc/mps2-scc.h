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
#include "hw/misc/led.h"
#include "qom/object.h"

#define TYPE_MPS2_SCC "mps2-scc"
OBJECT_DECLARE_SIMPLE_TYPE(MPS2SCC, MPS2_SCC)

struct MPS2SCC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    LEDState *led[8];

    uint32_t cfg0;
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t cfg4;
    uint32_t cfg5;
    uint32_t cfg6;
    uint32_t cfgdata_rtn;
    uint32_t cfgdata_out;
    uint32_t cfgctrl;
    uint32_t cfgstat;
    uint32_t dll;
    uint32_t aid;
    uint32_t id;
    uint32_t num_oscclk;
    uint32_t *oscclk;
    uint32_t *oscclk_reset;
};

#endif
