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

typedef struct PnvChip PnvChip;

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
