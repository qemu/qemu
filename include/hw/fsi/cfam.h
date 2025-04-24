/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Common FRU Access Macro
 */
#ifndef FSI_CFAM_H
#define FSI_CFAM_H

#include "system/memory.h"

#include "hw/fsi/fsi.h"
#include "hw/fsi/lbus.h"

#define TYPE_FSI_CFAM "cfam"
#define FSI_CFAM(obj) OBJECT_CHECK(FSICFAMState, (obj), TYPE_FSI_CFAM)

/* P9-ism */
#define CFAM_CONFIG_NR_REGS 0x28

typedef struct FSICFAMState {
    /* < private > */
    FSISlaveState parent;

    /* CFAM config address space */
    MemoryRegion config_iomem;

    MemoryRegion mr;

    FSILBus lbus;
    FSIScratchPad scratchpad;
} FSICFAMState;

#endif /* FSI_CFAM_H */
