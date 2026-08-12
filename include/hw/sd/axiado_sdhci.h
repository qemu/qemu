/*
 * Axiado SD Host Controller
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/sd/sdhci.h"
#include "qom/object.h"

#define TYPE_AXIADO_SDHCI "axiado-sdhci"
OBJECT_DECLARE_SIMPLE_TYPE(AxiadoSDHCIState, AXIADO_SDHCI)

typedef struct AxiadoSDHCIState {
    SysBusDevice parent;

    SDHCIState sdhci;
    MemoryRegion emmc_phy;
    BusState *sd_bus;
} AxiadoSDHCIState;
