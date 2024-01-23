/*
 * QEMU PowerPC nest pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_NEST_CHIPLET_PERVASIVE_H
#define PPC_PNV_NEST_CHIPLET_PERVASIVE_H

#define TYPE_PNV_NEST_CHIPLET_PERVASIVE "pnv-nest-chiplet-pervasive"
#define PNV_NEST_CHIPLET_PERVASIVE(obj) OBJECT_CHECK(PnvNestChipletPervasive, (obj), TYPE_PNV_NEST_CHIPLET_PERVASIVE)

typedef struct PnvPervasiveCtrlRegs {
#define PNV_CPLT_CTRL_SIZE 6
    uint64_t cplt_ctrl[PNV_CPLT_CTRL_SIZE];
    uint64_t cplt_cfg0;
    uint64_t cplt_cfg1;
    uint64_t cplt_stat0;
    uint64_t cplt_mask0;
    uint64_t ctrl_protect_mode;
    uint64_t ctrl_atomic_lock;
} PnvPervasiveCtrlRegs;

typedef struct PnvNestChipletPervasive {
    DeviceState             parent;
    MemoryRegion            xscom_ctrl_regs_mr;
    PnvPervasiveCtrlRegs    control_regs;
} PnvNestChipletPervasive;

#endif /*PPC_PNV_NEST_CHIPLET_PERVASIVE_H */
