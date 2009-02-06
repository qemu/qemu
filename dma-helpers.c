/*
 * DMA helper functions
 *
 * Copyright (c) 2009 Red Hat
 *
 * This work is licensed under the terms of the GNU General Public License
 * (GNU GPL), version 2 or later.
 */

#include "dma.h"
#include "block_int.h"

void qemu_sglist_init(QEMUSGList *qsg, int alloc_hint)
{
    qsg->sg = qemu_malloc(alloc_hint * sizeof(ScatterGatherEntry));
    qsg->nsg = 0;
    qsg->nalloc = alloc_hint;
    qsg->size = 0;
}

void qemu_sglist_add(QEMUSGList *qsg, target_phys_addr_t base,
                     target_phys_addr_t len)
{
    if (qsg->nsg == qsg->nalloc) {
        qsg->nalloc = 2 * qsg->nalloc + 1;
        qsg->sg = qemu_realloc(qsg->sg, qsg->nalloc * sizeof(ScatterGatherEntry));
    }
    qsg->sg[qsg->nsg].base = base;
    qsg->sg[qsg->nsg].len = len;
    qsg->size += len;
    ++qsg->nsg;
}

void qemu_sglist_destroy(QEMUSGList *qsg)
{
    qemu_free(qsg->sg);
}

typedef struct {
    BlockDriverState *bs;
    BlockDriverAIOCB *acb;
    QEMUSGList *sg;
    uint64_t sector_num;
    int is_write;
    int sg_cur_index;
    target_phys_addr_t sg_cur_byte;
    QEMUIOVector iov;
    QEMUBH *bh;
} DMABlockState;

static void dma_bdrv_cb(void *opaque, int ret);

static void reschedule_dma(void *opaque)
{
    DMABlockState *dbs = (DMABlockState *)opaque;

    qemu_bh_delete(dbs->bh);
    dbs->bh = NULL;
    dma_bdrv_cb(opaque, 0);
}

static void continue_after_map_failure(void *opaque)
{
    DMABlockState *dbs = (DMABlockState *)opaque;

    dbs->bh = qemu_bh_new(reschedule_dma, dbs);
    qemu_bh_schedule(dbs->bh);
}

static void dma_bdrv_cb(void *opaque, int ret)
{
    DMABlockState *dbs = (DMABlockState *)opaque;
    target_phys_addr_t cur_addr, cur_len;
    void *mem;
    int i;

    dbs->sector_num += dbs->iov.size / 512;
    for (i = 0; i < dbs->iov.niov; ++i) {
        cpu_physical_memory_unmap(dbs->iov.iov[i].iov_base,
                                  dbs->iov.iov[i].iov_len, !dbs->is_write,
                                  dbs->iov.iov[i].iov_len);
    }
    qemu_iovec_reset(&dbs->iov);

    if (dbs->sg_cur_index == dbs->sg->nsg || ret < 0) {
        dbs->acb->cb(dbs->acb->opaque, ret);
        qemu_iovec_destroy(&dbs->iov);
        qemu_aio_release(dbs->acb);
        qemu_free(dbs);
        return;
    }

    while (dbs->sg_cur_index < dbs->sg->nsg) {
        cur_addr = dbs->sg->sg[dbs->sg_cur_index].base + dbs->sg_cur_byte;
        cur_len = dbs->sg->sg[dbs->sg_cur_index].len - dbs->sg_cur_byte;
        mem = cpu_physical_memory_map(cur_addr, &cur_len, !dbs->is_write);
        if (!mem)
            break;
        qemu_iovec_add(&dbs->iov, mem, cur_len);
        dbs->sg_cur_byte += cur_len;
        if (dbs->sg_cur_byte == dbs->sg->sg[dbs->sg_cur_index].len) {
            dbs->sg_cur_byte = 0;
            ++dbs->sg_cur_index;
        }
    }

    if (dbs->iov.size == 0) {
        cpu_register_map_client(dbs, continue_after_map_failure);
        return;
    }

    if (dbs->is_write) {
        bdrv_aio_writev(dbs->bs, dbs->sector_num, &dbs->iov,
                        dbs->iov.size / 512, dma_bdrv_cb, dbs);
    } else {
        bdrv_aio_readv(dbs->bs, dbs->sector_num, &dbs->iov,
                       dbs->iov.size / 512, dma_bdrv_cb, dbs);
    }
}

static BlockDriverAIOCB *dma_bdrv_io(
    BlockDriverState *bs, QEMUSGList *sg, uint64_t sector_num,
    BlockDriverCompletionFunc *cb, void *opaque,
    int is_write)
{
    DMABlockState *dbs = qemu_malloc(sizeof(*dbs));

    dbs->bs = bs;
    dbs->acb = qemu_aio_get(bs, cb, opaque);
    dbs->sg = sg;
    dbs->sector_num = sector_num;
    dbs->sg_cur_index = 0;
    dbs->sg_cur_byte = 0;
    dbs->is_write = is_write;
    dbs->bh = NULL;
    qemu_iovec_init(&dbs->iov, sg->nsg);
    dma_bdrv_cb(dbs, 0);
    return dbs->acb;
}


BlockDriverAIOCB *dma_bdrv_read(BlockDriverState *bs,
                                QEMUSGList *sg, uint64_t sector,
                                void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_bdrv_io(bs, sg, sector, cb, opaque, 0);
}

BlockDriverAIOCB *dma_bdrv_write(BlockDriverState *bs,
                                 QEMUSGList *sg, uint64_t sector,
                                 void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_bdrv_io(bs, sg, sector, cb, opaque, 1);
}

