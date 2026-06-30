/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#ifndef HW_RISCV_TT_ATLANTIS_H
#define HW_RISCV_TT_ATLANTIS_H

#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_TT_ATLANTIS_MACHINE MACHINE_TYPE_NAME("tt-atlantis")
OBJECT_DECLARE_SIMPLE_TYPE(TTAtlantisState, TT_ATLANTIS_MACHINE)

struct TTAtlantisState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    const MemMapEntry *memmap;

    RISCVHartArrayState soc;
    DeviceState *irqchip;

    int fdt_size;
};

enum {
    TT_ATL_UART1_IRQ = 39,
};

enum {
    TT_ATL_ACLINT,
    TT_ATL_BOOTROM,
    TT_ATL_DDR_LO,
    TT_ATL_DDR_HI,
    TT_ATL_MAPLIC,
    TT_ATL_MIMSIC,
    TT_ATL_SAPLIC,
    TT_ATL_SIMSIC,
    TT_ATL_UART1,
};

#endif
