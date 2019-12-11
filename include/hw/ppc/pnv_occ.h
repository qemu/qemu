/*
 * QEMU PowerPC PowerNV Emulation of a few OCC related registers
 *
 * Copyright (c) 2015-2017, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_OCC_H
#define PPC_PNV_OCC_H

#include "hw/ppc/pnv_psi.h"

#define TYPE_PNV_OCC "pnv-occ"
#define PNV_OCC(obj) OBJECT_CHECK(PnvOCC, (obj), TYPE_PNV_OCC)
#define TYPE_PNV8_OCC TYPE_PNV_OCC "-POWER8"
#define PNV8_OCC(obj) OBJECT_CHECK(PnvOCC, (obj), TYPE_PNV8_OCC)
#define TYPE_PNV9_OCC TYPE_PNV_OCC "-POWER9"
#define PNV9_OCC(obj) OBJECT_CHECK(PnvOCC, (obj), TYPE_PNV9_OCC)

#define PNV_OCC_SENSOR_DATA_BLOCK_OFFSET 0x00580000
#define PNV_OCC_SENSOR_DATA_BLOCK_SIZE   0x00025800

typedef struct PnvOCC {
    DeviceState xd;

    /* OCC Misc interrupt */
    uint64_t occmisc;

    PnvPsi *psi;

    MemoryRegion xscom_regs;
    MemoryRegion sram_regs;
} PnvOCC;

#define PNV_OCC_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvOCCClass, (klass), TYPE_PNV_OCC)
#define PNV_OCC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvOCCClass, (obj), TYPE_PNV_OCC)

typedef struct PnvOCCClass {
    DeviceClass parent_class;

    int xscom_size;
    const MemoryRegionOps *xscom_ops;
    int psi_irq;
} PnvOCCClass;

#define PNV_OCC_SENSOR_DATA_BLOCK_BASE(i)                               \
    (PNV_OCC_SENSOR_DATA_BLOCK_OFFSET + (i) * PNV_OCC_SENSOR_DATA_BLOCK_SIZE)

#endif /* PPC_PNV_OCC_H */
