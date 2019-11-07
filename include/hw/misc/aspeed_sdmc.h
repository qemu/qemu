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
#define TYPE_ASPEED_2400_SDMC TYPE_ASPEED_SDMC "-ast2400"
#define TYPE_ASPEED_2500_SDMC TYPE_ASPEED_SDMC "-ast2500"
#define TYPE_ASPEED_2600_SDMC TYPE_ASPEED_SDMC "-ast2600"

#define ASPEED_SDMC_NR_REGS (0x174 >> 2)

typedef struct AspeedSDMCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SDMC_NR_REGS];
    uint64_t ram_size;
    uint64_t max_ram_size;
} AspeedSDMCState;

#define ASPEED_SDMC_CLASS(klass) \
     OBJECT_CLASS_CHECK(AspeedSDMCClass, (klass), TYPE_ASPEED_SDMC)
#define ASPEED_SDMC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AspeedSDMCClass, (obj), TYPE_ASPEED_SDMC)

typedef struct AspeedSDMCClass {
    SysBusDeviceClass parent_class;

    uint64_t max_ram_size;
    uint32_t (*compute_conf)(AspeedSDMCState *s, uint32_t data);
    void (*write)(AspeedSDMCState *s, uint32_t reg, uint32_t data);
} AspeedSDMCClass;

#endif /* ASPEED_SDMC_H */
