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
#ifndef _PPC_PNV_OCC_H
#define _PPC_PNV_OCC_H

#include "hw/ppc/pnv_psi.h"

#define TYPE_PNV_OCC "pnv-occ"
#define PNV_OCC(obj) OBJECT_CHECK(PnvOCC, (obj), TYPE_PNV_OCC)

typedef struct PnvOCC {
    DeviceState xd;

    /* OCC Misc interrupt */
    uint64_t occmisc;

    PnvPsi *psi;

    MemoryRegion xscom_regs;
} PnvOCC;

#endif /* _PPC_PNV_OCC_H */
