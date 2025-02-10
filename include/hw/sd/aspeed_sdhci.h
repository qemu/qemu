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
#define TYPE_ASPEED_2400_SDHCI TYPE_ASPEED_SDHCI "-ast2400"
#define TYPE_ASPEED_2500_SDHCI TYPE_ASPEED_SDHCI "-ast2500"
#define TYPE_ASPEED_2600_SDHCI TYPE_ASPEED_SDHCI "-ast2600"
#define TYPE_ASPEED_2700_SDHCI TYPE_ASPEED_SDHCI "-ast2700"
OBJECT_DECLARE_TYPE(AspeedSDHCIState, AspeedSDHCIClass, ASPEED_SDHCI)

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

struct AspeedSDHCIClass {
    SysBusDeviceClass parent_class;

    uint64_t capareg;
};

#endif /* ASPEED_SDHCI_H */
