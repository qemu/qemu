/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef _PPC_PNV_PNOR_H
#define _PPC_PNV_PNOR_H

#define TYPE_PNV_PNOR  "pnv-pnor"
#define PNV_PNOR(obj)  OBJECT_CHECK(PnvPnor, (obj), TYPE_PNV_PNOR)

typedef struct PnvPnor {
    SysBusDevice   parent_obj;

    BlockBackend   *blk;

    uint8_t        *storage;
    uint32_t       size;
    MemoryRegion   mmio;
} PnvPnor;

#endif /* _PPC_PNV_PNOR_H */
