/*
 * QEMU PowerPC PowerNV XSCOM bus definitions
 *
 * Copyright (c) 2016, IBM Corporation.
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

#ifndef PPC_PNV_XSCOM_H
#define PPC_PNV_XSCOM_H

#include "qom/object.h"

typedef struct PnvXScomInterface {
    Object parent;
} PnvXScomInterface;

#define TYPE_PNV_XSCOM_INTERFACE "pnv-xscom-interface"
#define PNV_XSCOM_INTERFACE(obj) \
     OBJECT_CHECK(PnvXScomInterface, (obj), TYPE_PNV_XSCOM_INTERFACE)
#define PNV_XSCOM_INTERFACE_CLASS(klass)                \
    OBJECT_CLASS_CHECK(PnvXScomInterfaceClass, (klass), \
                       TYPE_PNV_XSCOM_INTERFACE)
#define PNV_XSCOM_INTERFACE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvXScomInterfaceClass, (obj), TYPE_PNV_XSCOM_INTERFACE)

typedef struct PnvXScomInterfaceClass {
    InterfaceClass parent;
    int (*dt_xscom)(PnvXScomInterface *dev, void *fdt, int offset);
} PnvXScomInterfaceClass;

/*
 * Layout of the XSCOM PCB addresses of EX core 1 (POWER 8)
 *
 *   GPIO        0x1100xxxx
 *   SCOM        0x1101xxxx
 *   OHA         0x1102xxxx
 *   CLOCK CTL   0x1103xxxx
 *   FIR         0x1104xxxx
 *   THERM       0x1105xxxx
 *   <reserved>  0x1106xxxx
 *               ..
 *               0x110Exxxx
 *   PCB SLAVE   0x110Fxxxx
 */

#define PNV_XSCOM_EX_CORE_BASE    0x10000000ull

#define PNV_XSCOM_EX_BASE(core) \
    (PNV_XSCOM_EX_CORE_BASE | ((uint64_t)(core) << 24))
#define PNV_XSCOM_EX_SIZE         0x100000

#define PNV_XSCOM_LPC_BASE        0xb0020
#define PNV_XSCOM_LPC_SIZE        0x4

#define PNV_XSCOM_PSIHB_BASE      0x2010900
#define PNV_XSCOM_PSIHB_SIZE      0x20

#define PNV_XSCOM_OCC_BASE        0x0066000
#define PNV_XSCOM_OCC_SIZE        0x6000

#define PNV9_XSCOM_EC_BASE(core) \
    ((uint64_t)(((core) & 0x1F) + 0x20) << 24)
#define PNV9_XSCOM_EC_SIZE        0x100000

#define PNV9_XSCOM_EQ_BASE(core) \
    ((uint64_t)(((core) & 0x1C) + 0x40) << 22)
#define PNV9_XSCOM_EQ_SIZE        0x100000

#define PNV9_XSCOM_OCC_BASE       PNV_XSCOM_OCC_BASE
#define PNV9_XSCOM_OCC_SIZE       0x8000

#define PNV9_XSCOM_PSIHB_BASE     0x5012900
#define PNV9_XSCOM_PSIHB_SIZE     0x100

#define PNV9_XSCOM_XIVE_BASE      0x5013000
#define PNV9_XSCOM_XIVE_SIZE      0x300

extern void pnv_xscom_realize(PnvChip *chip, uint64_t size, Error **errp);
extern int pnv_dt_xscom(PnvChip *chip, void *fdt, int offset);

extern void pnv_xscom_add_subregion(PnvChip *chip, hwaddr offset,
                                    MemoryRegion *mr);
extern void pnv_xscom_region_init(MemoryRegion *mr,
                                  struct Object *owner,
                                  const MemoryRegionOps *ops,
                                  void *opaque,
                                  const char *name,
                                  uint64_t size);

#endif /* PPC_PNV_XSCOM_H */
