/*
 * QEMU PowerPC PowerNV Emulation of some ADU behaviour
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_ADU_H
#define PPC_PNV_ADU_H

#include "hw/ppc/pnv.h"
#include "hw/qdev-core.h"

#define TYPE_PNV_ADU "pnv-adu"

OBJECT_DECLARE_TYPE(PnvADU, PnvADUClass, PNV_ADU)

struct PnvADU {
    DeviceState xd;

    MemoryRegion xscom_regs;
};

#endif /* PPC_PNV_ADU_H */
