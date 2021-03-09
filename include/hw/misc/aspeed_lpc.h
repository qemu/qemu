/*
 *  ASPEED LPC Controller
 *
 *  Copyright (C) 2017-2018 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_LPC_H
#define ASPEED_LPC_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_LPC "aspeed.lpc"
#define ASPEED_LPC(obj) OBJECT_CHECK(AspeedLPCState, (obj), TYPE_ASPEED_LPC)

#define ASPEED_LPC_NR_REGS (0x260 >> 2)

typedef struct AspeedLPCState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_LPC_NR_REGS];
    uint32_t hicr7;
} AspeedLPCState;

#endif /* _ASPEED_LPC_H_ */
