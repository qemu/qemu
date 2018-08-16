/*
 * ASPEED SDRAM Memory Controller
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef ASPEED_SDMC_H
#define ASPEED_SDMC_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_SDMC "aspeed.sdmc"
#define ASPEED_SDMC(obj) OBJECT_CHECK(AspeedSDMCState, (obj), TYPE_ASPEED_SDMC)

#define ASPEED_SDMC_NR_REGS (0x174 >> 2)

typedef struct AspeedSDMCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SDMC_NR_REGS];
    uint32_t silicon_rev;
    uint32_t ram_bits;
    uint64_t ram_size;
    uint64_t max_ram_size;
    uint32_t fixed_conf;

} AspeedSDMCState;

#endif /* ASPEED_SDMC_H */
