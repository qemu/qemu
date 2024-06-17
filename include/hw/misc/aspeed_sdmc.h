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
#include "qom/object.h"

#define TYPE_ASPEED_SDMC "aspeed.sdmc"
OBJECT_DECLARE_TYPE(AspeedSDMCState, AspeedSDMCClass, ASPEED_SDMC)
#define TYPE_ASPEED_2400_SDMC TYPE_ASPEED_SDMC "-ast2400"
#define TYPE_ASPEED_2500_SDMC TYPE_ASPEED_SDMC "-ast2500"
#define TYPE_ASPEED_2600_SDMC TYPE_ASPEED_SDMC "-ast2600"
#define TYPE_ASPEED_2700_SDMC TYPE_ASPEED_SDMC "-ast2700"

/*
 * SDMC has 174 documented registers. In addition the u-boot device tree
 * describes the following regions:
 *  - PHY status regs at offset 0x400, length 0x200
 *  - PHY setting regs at offset 0x100, length 0x300
 *
 * There are two sets of MRS (Mode Registers) configuration in ast2600 memory
 * system: one is in the SDRAM MC (memory controller) which is used in run
 * time, and the other is in the DDR-PHY IP which is used during DDR-PHY
 * training.
 */
#define ASPEED_SDMC_NR_REGS (0x1000 >> 2)

struct AspeedSDMCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SDMC_NR_REGS];
    uint64_t ram_size;
    uint64_t max_ram_size;
    bool unlocked;
};


struct AspeedSDMCClass {
    SysBusDeviceClass parent_class;

    uint64_t max_ram_size;
    const uint64_t *valid_ram_sizes;
    uint32_t (*compute_conf)(AspeedSDMCState *s, uint32_t data);
    void (*write)(AspeedSDMCState *s, uint32_t reg, uint32_t data);
    bool is_bus64bit;
};

#endif /* ASPEED_SDMC_H */
