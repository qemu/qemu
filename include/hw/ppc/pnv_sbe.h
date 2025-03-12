/*
 * QEMU PowerPC PowerNV Emulation of some SBE behaviour
 *
 * Copyright (c) 2022, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_SBE_H
#define PPC_PNV_SBE_H

#include "system/memory.h"
#include "hw/qdev-core.h"

#define TYPE_PNV_SBE "pnv-sbe"
OBJECT_DECLARE_TYPE(PnvSBE, PnvSBEClass, PNV_SBE)
#define TYPE_PNV9_SBE TYPE_PNV_SBE "-POWER9"
DECLARE_INSTANCE_CHECKER(PnvSBE, PNV9_SBE, TYPE_PNV9_SBE)
#define TYPE_PNV10_SBE TYPE_PNV_SBE "-POWER10"
DECLARE_INSTANCE_CHECKER(PnvSBE, PNV10_SBE, TYPE_PNV10_SBE)

struct PnvSBE {
    DeviceState xd;

    uint64_t mbox[8];
    uint64_t sbe_doorbell;
    uint64_t host_doorbell;

    qemu_irq psi_irq;
    QEMUTimer *timer;

    MemoryRegion xscom_mbox_regs;
    MemoryRegion xscom_ctrl_regs;
};

struct PnvSBEClass {
    DeviceClass parent_class;

    int xscom_ctrl_size;
    int xscom_mbox_size;
    const MemoryRegionOps *xscom_ctrl_ops;
    const MemoryRegionOps *xscom_mbox_ops;
};

#endif /* PPC_PNV_SBE_H */
