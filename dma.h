/*
 * DMA helper functions
 *
 * Copyright (c) 2009 Red Hat
 *
 * This work is licensed under the terms of the GNU General Public License
 * (GNU GPL), version 2 or later.
 */

#ifndef DMA_H
#define DMA_H

#include <stdio.h>
//#include "cpu.h"
#include "hw/hw.h"
#include "block.h"

typedef struct ScatterGatherEntry ScatterGatherEntry;

#if defined(TARGET_PHYS_ADDR_BITS)
typedef target_phys_addr_t dma_addr_t;

#define DMA_ADDR_FMT TARGET_FMT_plx

typedef enum {
    DMA_DIRECTION_TO_DEVICE = 0,
    DMA_DIRECTION_FROM_DEVICE = 1,
} DMADirection;

struct ScatterGatherEntry {
    dma_addr_t base;
    dma_addr_t len;
};

struct QEMUSGList {
    ScatterGatherEntry *sg;
    int nsg;
    int nalloc;
    dma_addr_t size;
};

void qemu_sglist_init(QEMUSGList *qsg, int alloc_hint);
void qemu_sglist_add(QEMUSGList *qsg, dma_addr_t base, dma_addr_t len);
void qemu_sglist_destroy(QEMUSGList *qsg);
#endif

typedef BlockDriverAIOCB *DMAIOFunc(BlockDriverState *bs, int64_t sector_num,
                                 QEMUIOVector *iov, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque);

BlockDriverAIOCB *dma_bdrv_io(BlockDriverState *bs,
                              QEMUSGList *sg, uint64_t sector_num,
                              DMAIOFunc *io_func, BlockDriverCompletionFunc *cb,
                              void *opaque, bool to_dev);
BlockDriverAIOCB *dma_bdrv_read(BlockDriverState *bs,
                                QEMUSGList *sg, uint64_t sector,
                                BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *dma_bdrv_write(BlockDriverState *bs,
                                 QEMUSGList *sg, uint64_t sector,
                                 BlockDriverCompletionFunc *cb, void *opaque);
#endif
