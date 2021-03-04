/*
 * Aspeed SD Host Controller
 * Eddie James <eajames@linux.ibm.com>
 *
 * Copyright (C) 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_SDHCI_H
#define ASPEED_SDHCI_H

#include "hw/sd/sdhci.h"
#include "qom/object.h"

#define TYPE_ASPEED_SDHCI "aspeed.sdhci"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedSDHCIState, ASPEED_SDHCI)

#define ASPEED_SDHCI_CAPABILITIES 0x01E80080
#define ASPEED_SDHCI_NUM_SLOTS    2
#define ASPEED_SDHCI_NUM_REGS     (ASPEED_SDHCI_REG_SIZE / sizeof(uint32_t))
#define ASPEED_SDHCI_REG_SIZE     0x100

struct AspeedSDHCIState {
    SysBusDevice parent;

    SDHCIState slots[ASPEED_SDHCI_NUM_SLOTS];
    uint8_t num_slots;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_SDHCI_NUM_REGS];
};

#endif /* ASPEED_SDHCI_H */
