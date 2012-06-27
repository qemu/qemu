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
#include "hw/hw.h"
#include "block.h"

typedef struct DMAContext DMAContext;
typedef struct ScatterGatherEntry ScatterGatherEntry;

typedef enum {
    DMA_DIRECTION_TO_DEVICE = 0,
    DMA_DIRECTION_FROM_DEVICE = 1,
} DMADirection;

struct QEMUSGList {
    ScatterGatherEntry *sg;
    int nsg;
    int nalloc;
    size_t size;
};

#if defined(TARGET_PHYS_ADDR_BITS)
typedef target_phys_addr_t dma_addr_t;

#define DMA_ADDR_BITS TARGET_PHYS_ADDR_BITS
#define DMA_ADDR_FMT TARGET_FMT_plx

/* Checks that the given range of addresses is valid for DMA.  This is
 * useful for certain cases, but usually you should just use
 * dma_memory_{read,write}() and check for errors */
static inline bool dma_memory_valid(DMAContext *dma, dma_addr_t addr,
                                    dma_addr_t len, DMADirection dir)
{
    /* Stub version, with no iommu we assume all bus addresses are valid */
    return true;
}

static inline int dma_memory_rw(DMAContext *dma, dma_addr_t addr,
                                void *buf, dma_addr_t len, DMADirection dir)
{
    /* Stub version when we have no iommu support */
    cpu_physical_memory_rw(addr, buf, (target_phys_addr_t)len,
                           dir == DMA_DIRECTION_FROM_DEVICE);
    return 0;
}

static inline int dma_memory_read(DMAContext *dma, dma_addr_t addr,
                                  void *buf, dma_addr_t len)
{
    return dma_memory_rw(dma, addr, buf, len, DMA_DIRECTION_TO_DEVICE);
}

static inline int dma_memory_write(DMAContext *dma, dma_addr_t addr,
                                   const void *buf, dma_addr_t len)
{
    return dma_memory_rw(dma, addr, (void *)buf, len,
                         DMA_DIRECTION_FROM_DEVICE);
}

int dma_memory_set(DMAContext *dma, dma_addr_t addr, uint8_t c, dma_addr_t len);

static inline void *dma_memory_map(DMAContext *dma,
                                   dma_addr_t addr, dma_addr_t *len,
                                   DMADirection dir)
{
    target_phys_addr_t xlen = *len;
    void *p;

    p = cpu_physical_memory_map(addr, &xlen,
                                dir == DMA_DIRECTION_FROM_DEVICE);
    *len = xlen;
    return p;
}

static inline void dma_memory_unmap(DMAContext *dma,
                                    void *buffer, dma_addr_t len,
                                    DMADirection dir, dma_addr_t access_len)
{
    return cpu_physical_memory_unmap(buffer, (target_phys_addr_t)len,
                                     dir == DMA_DIRECTION_FROM_DEVICE,
                                     access_len);
}

#define DEFINE_LDST_DMA(_lname, _sname, _bits, _end) \
    static inline uint##_bits##_t ld##_lname##_##_end##_dma(DMAContext *dma, \
                                                            dma_addr_t addr) \
    {                                                                   \
        uint##_bits##_t val;                                            \
        dma_memory_read(dma, addr, &val, (_bits) / 8);                  \
        return _end##_bits##_to_cpu(val);                               \
    }                                                                   \
    static inline void st##_sname##_##_end##_dma(DMAContext *dma,       \
                                                 dma_addr_t addr,       \
                                                 uint##_bits##_t val)   \
    {                                                                   \
        val = cpu_to_##_end##_bits(val);                                \
        dma_memory_write(dma, addr, &val, (_bits) / 8);                 \
    }

static inline uint8_t ldub_dma(DMAContext *dma, dma_addr_t addr)
{
    uint8_t val;

    dma_memory_read(dma, addr, &val, 1);
    return val;
}

static inline void stb_dma(DMAContext *dma, dma_addr_t addr, uint8_t val)
{
    dma_memory_write(dma, addr, &val, 1);
}

DEFINE_LDST_DMA(uw, w, 16, le);
DEFINE_LDST_DMA(l, l, 32, le);
DEFINE_LDST_DMA(q, q, 64, le);
DEFINE_LDST_DMA(uw, w, 16, be);
DEFINE_LDST_DMA(l, l, 32, be);
DEFINE_LDST_DMA(q, q, 64, be);

#undef DEFINE_LDST_DMA

struct ScatterGatherEntry {
    dma_addr_t base;
    dma_addr_t len;
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
                              void *opaque, DMADirection dir);
BlockDriverAIOCB *dma_bdrv_read(BlockDriverState *bs,
                                QEMUSGList *sg, uint64_t sector,
                                BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *dma_bdrv_write(BlockDriverState *bs,
                                 QEMUSGList *sg, uint64_t sector,
                                 BlockDriverCompletionFunc *cb, void *opaque);
uint64_t dma_buf_read(uint8_t *ptr, int32_t len, QEMUSGList *sg);
uint64_t dma_buf_write(uint8_t *ptr, int32_t len, QEMUSGList *sg);

void dma_acct_start(BlockDriverState *bs, BlockAcctCookie *cookie,
                    QEMUSGList *sg, enum BlockAcctType type);

#endif
