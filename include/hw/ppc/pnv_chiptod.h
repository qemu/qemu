/*
 * QEMU PowerPC PowerNV Emulation of some CHIPTOD behaviour
 *
 * Copyright (c) 2022-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_CHIPTOD_H
#define PPC_PNV_CHIPTOD_H

#include "qom/object.h"

#define TYPE_PNV_CHIPTOD "pnv-chiptod"
OBJECT_DECLARE_TYPE(PnvChipTOD, PnvChipTODClass, PNV_CHIPTOD)
#define TYPE_PNV9_CHIPTOD TYPE_PNV_CHIPTOD "-POWER9"
DECLARE_INSTANCE_CHECKER(PnvChipTOD, PNV9_CHIPTOD, TYPE_PNV9_CHIPTOD)
#define TYPE_PNV10_CHIPTOD TYPE_PNV_CHIPTOD "-POWER10"
DECLARE_INSTANCE_CHECKER(PnvChipTOD, PNV10_CHIPTOD, TYPE_PNV10_CHIPTOD)

enum tod_state {
    tod_error = 0,
    tod_not_set = 7,
    tod_running = 2,
    tod_stopped = 1,
};

typedef struct PnvCore PnvCore;

struct PnvChipTOD {
    DeviceState xd;

    PnvChip *chip;
    MemoryRegion xscom_regs;

    bool primary;
    bool secondary;
    enum tod_state tod_state;
    uint64_t tod_error;
    uint64_t pss_mss_ctrl_reg;
    PnvCore *slave_pc_target;
};

struct PnvChipTODClass {
    DeviceClass parent_class;

    void (*broadcast_ttype)(PnvChipTOD *sender, uint32_t trigger);
    PnvCore *(*tx_ttype_target)(PnvChipTOD *chiptod, uint64_t val);

    int xscom_size;
};

#endif /* PPC_PNV_CHIPTOD_H */
