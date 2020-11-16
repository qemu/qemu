/*
 * QEMU PowerPC PowerNV Emulation of a few HOMER related registers
 *
 * Copyright (c) 2019, IBM Corporation.
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

#ifndef PPC_PNV_HOMER_H
#define PPC_PNV_HOMER_H

#include "hw/ppc/pnv.h"
#include "qom/object.h"

#define TYPE_PNV_HOMER "pnv-homer"
OBJECT_DECLARE_TYPE(PnvHomer, PnvHomerClass,
                    PNV_HOMER)
#define TYPE_PNV8_HOMER TYPE_PNV_HOMER "-POWER8"
DECLARE_INSTANCE_CHECKER(PnvHomer, PNV8_HOMER,
                         TYPE_PNV8_HOMER)
#define TYPE_PNV9_HOMER TYPE_PNV_HOMER "-POWER9"
DECLARE_INSTANCE_CHECKER(PnvHomer, PNV9_HOMER,
                         TYPE_PNV9_HOMER)

struct PnvHomer {
    DeviceState parent;

    struct PnvChip *chip;
    MemoryRegion pba_regs;
    MemoryRegion regs;
};


struct PnvHomerClass {
    DeviceClass parent_class;

    int pba_size;
    const MemoryRegionOps *pba_ops;
    int homer_size;
    const MemoryRegionOps *homer_ops;

    hwaddr core_max_base;
};

#endif /* PPC_PNV_HOMER_H */
