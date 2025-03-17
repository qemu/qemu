/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_PNOR_H
#define PPC_PNV_PNOR_H

#include "hw/sysbus.h"

/*
 * PNOR offset on the LPC FW address space. For now this should be 0 because
 * skiboot 7.1 has a bug where IDSEL > 0 (LPC FW address > 256MB) access is
 * not performed correctly.
 */
#define PNOR_SPI_OFFSET         0x00000000UL

#define TYPE_PNV_PNOR  "pnv-pnor"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPnor, PNV_PNOR)

struct PnvPnor {
    SysBusDevice   parent_obj;

    BlockBackend   *blk;

    uint8_t        *storage;
    uint32_t       lpc_address; /* Offset within LPC FW space */
    int64_t        size;
    MemoryRegion   mmio;
};

#endif /* PPC_PNV_PNOR_H */
