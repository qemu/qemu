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

#include <stdint.h>

#define TYPE_ASPEED_LPC "aspeed.lpc"
#define ASPEED_LPC(obj) OBJECT_CHECK(AspeedLPCState, (obj), TYPE_ASPEED_LPC)

#define ASPEED_LPC_NR_REGS      (0x260 >> 2)

enum aspeed_lpc_subdevice {
    aspeed_lpc_kcs_1 = 0,
    aspeed_lpc_kcs_2,
    aspeed_lpc_kcs_3,
    aspeed_lpc_kcs_4,
    aspeed_lpc_ibt,
};

#define ASPEED_LPC_NR_SUBDEVS   5

typedef struct AspeedLPCState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    qemu_irq subdevice_irqs[ASPEED_LPC_NR_SUBDEVS];
    uint32_t subdevice_irqs_pending;

    uint32_t regs[ASPEED_LPC_NR_REGS];
    uint32_t hicr7;
} AspeedLPCState;

#endif /* ASPEED_LPC_H */
