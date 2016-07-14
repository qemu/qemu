/*
 * ASPEED System Control Unit
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef ASPEED_SCU_H
#define ASPEED_SCU_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_SCU "aspeed.scu"
#define ASPEED_SCU(obj) OBJECT_CHECK(AspeedSCUState, (obj), TYPE_ASPEED_SCU)

#define ASPEED_SCU_NR_REGS (0x1A8 >> 2)

typedef struct AspeedSCUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SCU_NR_REGS];
    uint32_t silicon_rev;
    uint32_t hw_strap1;
    uint32_t hw_strap2;
} AspeedSCUState;

#define AST2400_A0_SILICON_REV   0x02000303U
#define AST2500_A0_SILICON_REV   0x04000303U

extern bool is_supported_silicon_rev(uint32_t silicon_rev);

#endif /* ASPEED_SCU_H */
