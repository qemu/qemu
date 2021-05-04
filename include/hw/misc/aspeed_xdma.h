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
#define TYPE_ASPEED_2400_XDMA TYPE_ASPEED_XDMA "-ast2400"
#define TYPE_ASPEED_2500_XDMA TYPE_ASPEED_XDMA "-ast2500"
#define TYPE_ASPEED_2600_XDMA TYPE_ASPEED_XDMA "-ast2600"
OBJECT_DECLARE_TYPE(AspeedXDMAState, AspeedXDMAClass, ASPEED_XDMA)

#define ASPEED_XDMA_NUM_REGS (ASPEED_XDMA_REG_SIZE / sizeof(uint32_t))
#define ASPEED_XDMA_REG_SIZE 0x7C

struct AspeedXDMAState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    char bmc_cmdq_readp_set;
    uint32_t regs[ASPEED_XDMA_NUM_REGS];
};

struct AspeedXDMAClass {
    SysBusDeviceClass parent_class;

    uint8_t cmdq_endp;
    uint8_t cmdq_wrp;
    uint8_t cmdq_rdp;
    uint8_t intr_ctrl;
    uint32_t intr_ctrl_mask;
    uint8_t intr_status;
    uint32_t intr_complete;
};

#endif /* ASPEED_XDMA_H */
