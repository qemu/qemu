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
    qsg->sg = g_malloc(alloc_hint * sizeof(ScatterGatherEntry));
    qsg->nsg = 0;
    qsg->nalloc = alloc_hint;
    qsg->size = 0;
}

void qemu_sglist_add(QEMUSGList *qsg, target_phys_addr_t base,
                     target_phys_addr_t len)
{
    if (qsg->nsg == qsg->nalloc) {
        qsg->nalloc = 2 * qsg->nalloc + 1;
        qsg->sg = g_realloc(qsg->sg, qsg->nalloc * sizeof(ScatterGatherEntry));
    }
    qsg->sg[qsg->nsg].base = base;
    qsg->sg[qsg->nsg].len = len;
    qsg->size += len;
    ++qsg->nsg;
}

void qemu_sglist_destroy(QEMUSGList *qsg)
{
    g_free(qsg->sg);
}

typedef struct {
    BlockDriverAIOCB common;
    BlockDriverState *bs;
    BlockDriverAIOCB *acb;
    QEMUSGList *sg;
    uint64_t sector_num;
    bool to_dev;
    int sg_cur_index;
    target_phys_addr_t sg_cur_byte;
    QEMUIOVector iov;
    QEMUBH *bh;
    DMAIOFunc *io_func;
} DMAAIOCB;

static void dma_bdrv_cb(void *opaque, int ret);

static void reschedule_dma(void *opaque)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;

    qemu_bh_delete(dbs->bh);
    dbs->bh = NULL;
    dma_bdrv_cb(opaque, 0);
}

static void continue_after_map_failure(void *opaque)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;

    dbs->bh = qemu_bh_new(reschedule_dma, dbs);
    qemu_bh_schedule(dbs->bh);
}

static void dma_bdrv_unmap(DMAAIOCB *dbs)
{
    int i;

    for (i = 0; i < dbs->iov.niov; ++i) {
        cpu_physical_memory_unmap(dbs->iov.iov[i].iov_base,
                                  dbs->iov.iov[i].iov_len, !dbs->to_dev,
                                  dbs->iov.iov[i].iov_len);
    }
}

static void dma_bdrv_cb(void *opaque, int ret)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;
    target_phys_addr_t cur_addr, cur_len;
    void *mem;

    dbs->acb = NULL;
    dbs->sector_num += dbs->iov.size / 512;
    dma_bdrv_unmap(dbs);
    qemu_iovec_reset(&dbs->iov);

    if (dbs->sg_cur_index == dbs->sg->nsg || ret < 0) {
        dbs->common.cb(dbs->common.opaque, ret);
        qemu_iovec_destroy(&dbs->iov);
        qemu_aio_release(dbs);
        return;
    }

    while (dbs->sg_cur_index < dbs->sg->nsg) {
        cur_addr = dbs->sg->sg[dbs->sg_cur_index].base + dbs->sg_cur_byte;
        cur_len = dbs->sg->sg[dbs->sg_cur_index].len - dbs->sg_cur_byte;
        mem = cpu_physical_memory_map(cur_addr, &cur_len, !dbs->to_dev);
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

    dbs->acb = dbs->io_func(dbs->bs, dbs->sector_num, &dbs->iov,
                            dbs->iov.size / 512, dma_bdrv_cb, dbs);
    if (!dbs->acb) {
        dma_bdrv_unmap(dbs);
        qemu_iovec_destroy(&dbs->iov);
        return;
    }
}

static void dma_aio_cancel(BlockDriverAIOCB *acb)
{
    DMAAIOCB *dbs = container_of(acb, DMAAIOCB, common);

    if (dbs->acb) {
        bdrv_aio_cancel(dbs->acb);
    }
}

static AIOPool dma_aio_pool = {
    .aiocb_size         = sizeof(DMAAIOCB),
    .cancel             = dma_aio_cancel,
};

BlockDriverAIOCB *dma_bdrv_io(
    BlockDriverState *bs, QEMUSGList *sg, uint64_t sector_num,
    DMAIOFunc *io_func, BlockDriverCompletionFunc *cb,
    void *opaque, bool to_dev)
{
    DMAAIOCB *dbs = qemu_aio_get(&dma_aio_pool, bs, cb, opaque);

    dbs->acb = NULL;
    dbs->bs = bs;
    dbs->sg = sg;
    dbs->sector_num = sector_num;
    dbs->sg_cur_index = 0;
    dbs->sg_cur_byte = 0;
    dbs->to_dev = to_dev;
    dbs->io_func = io_func;
    dbs->bh = NULL;
    qemu_iovec_init(&dbs->iov, sg->nsg);
    dma_bdrv_cb(dbs, 0);
    if (!dbs->acb) {
        qemu_aio_release(dbs);
        return NULL;
    }
    return &dbs->common;
}


BlockDriverAIOCB *dma_bdrv_read(BlockDriverState *bs,
                                QEMUSGList *sg, uint64_t sector,
                                void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_bdrv_io(bs, sg, sector, bdrv_aio_readv, cb, opaque, false);
}

BlockDriverAIOCB *dma_bdrv_write(BlockDriverState *bs,
                                 QEMUSGList *sg, uint64_t sector,
                                 void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_bdrv_io(bs, sg, sector, bdrv_aio_writev, cb, opaque, true);
}
