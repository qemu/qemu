/*
 * ASPEED XDMA Controller
 * Eddie James <eajames@linux.ibm.com>
 *
 * Copyright (C) 2019 IBM Corp.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_XDMA_H
#define ASPEED_XDMA_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_XDMA "aspeed.xdma"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedXDMAState, ASPEED_XDMA)

#define ASPEED_XDMA_NUM_REGS (ASPEED_XDMA_REG_SIZE / sizeof(uint32_t))
#define ASPEED_XDMA_REG_SIZE 0x7C

struct AspeedXDMAState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    char bmc_cmdq_readp_set;
    uint32_t regs[ASPEED_XDMA_NUM_REGS];
};

#endif /* ASPEED_XDMA_H */
