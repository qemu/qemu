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
#ifndef _PPC_PNV_XSCOM_H
#define _PPC_PNV_XSCOM_H

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
    int (*populate)(PnvXScomInterface *dev, void *fdt, int offset);
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

#define PNV_XSCOM_EX_CORE_BASE(base, i) (base | (((uint64_t)i) << 24))
#define PNV_XSCOM_EX_CORE_SIZE    0x100000

#define PNV_XSCOM_LPC_BASE        0xb0020
#define PNV_XSCOM_LPC_SIZE        0x4

extern void pnv_xscom_realize(PnvChip *chip, Error **errp);
extern int pnv_xscom_populate(PnvChip *chip, void *fdt, int offset);

extern void pnv_xscom_add_subregion(PnvChip *chip, hwaddr offset,
                                    MemoryRegion *mr);
extern void pnv_xscom_region_init(MemoryRegion *mr,
                                  struct Object *owner,
                                  const MemoryRegionOps *ops,
                                  void *opaque,
                                  const char *name,
                                  uint64_t size);

#endif /* _PPC_PNV_XSCOM_H */
