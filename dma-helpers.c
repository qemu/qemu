/*
 * DMA helper functions
 *
 * Copyright (c) 2009 Red Hat
 *
 * This work is licensed under the terms of the GNU General Public License
 * (GNU GPL), version 2 or later.
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "sysemu/dma.h"
#include "trace.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"

/* #define DEBUG_IOMMU */

int dma_memory_set(AddressSpace *as, dma_addr_t addr, uint8_t c, dma_addr_t len)
{
    dma_barrier(as, DMA_DIRECTION_FROM_DEVICE);

#define FILLBUF_SIZE 512
    uint8_t fillbuf[FILLBUF_SIZE];
    int l;
    bool error = false;

    memset(fillbuf, c, FILLBUF_SIZE);
    while (len > 0) {
        l = len < FILLBUF_SIZE ? len : FILLBUF_SIZE;
        error |= address_space_rw(as, addr, MEMTXATTRS_UNSPECIFIED,
                                  fillbuf, l, true);
        len -= l;
        addr += l;
    }

    return error;
}

void qemu_sglist_init(QEMUSGList *qsg, DeviceState *dev, int alloc_hint,
                      AddressSpace *as)
{
    qsg->sg = g_malloc(alloc_hint * sizeof(ScatterGatherEntry));
    qsg->nsg = 0;
    qsg->nalloc = alloc_hint;
    qsg->size = 0;
    qsg->as = as;
    qsg->dev = dev;
    object_ref(OBJECT(dev));
}

void qemu_sglist_add(QEMUSGList *qsg, dma_addr_t base, dma_addr_t len)
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
    object_unref(OBJECT(qsg->dev));
    g_free(qsg->sg);
    memset(qsg, 0, sizeof(*qsg));
}

typedef struct {
    BlockAIOCB common;
    AioContext *ctx;
    BlockAIOCB *acb;
    QEMUSGList *sg;
    uint64_t offset;
    DMADirection dir;
    int sg_cur_index;
    dma_addr_t sg_cur_byte;
    QEMUIOVector iov;
    QEMUBH *bh;
    DMAIOFunc *io_func;
    void *io_func_opaque;
} DMAAIOCB;

static void dma_blk_cb(void *opaque, int ret);

static void reschedule_dma(void *opaque)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;

    qemu_bh_delete(dbs->bh);
    dbs->bh = NULL;
    dma_blk_cb(dbs, 0);
}

static void dma_blk_unmap(DMAAIOCB *dbs)
{
    int i;

    for (i = 0; i < dbs->iov.niov; ++i) {
        dma_memory_unmap(dbs->sg->as, dbs->iov.iov[i].iov_base,
                         dbs->iov.iov[i].iov_len, dbs->dir,
                         dbs->iov.iov[i].iov_len);
    }
    qemu_iovec_reset(&dbs->iov);
}

static void dma_complete(DMAAIOCB *dbs, int ret)
{
    trace_dma_complete(dbs, ret, dbs->common.cb);

    dma_blk_unmap(dbs);
    if (dbs->common.cb) {
        dbs->common.cb(dbs->common.opaque, ret);
    }
    qemu_iovec_destroy(&dbs->iov);
    if (dbs->bh) {
        qemu_bh_delete(dbs->bh);
        dbs->bh = NULL;
    }
    qemu_aio_unref(dbs);
}

static void dma_blk_cb(void *opaque, int ret)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;
    dma_addr_t cur_addr, cur_len;
    void *mem;

    trace_dma_blk_cb(dbs, ret);

    dbs->acb = NULL;
    dbs->offset += dbs->iov.size;

    if (dbs->sg_cur_index == dbs->sg->nsg || ret < 0) {
        dma_complete(dbs, ret);
        return;
    }
    dma_blk_unmap(dbs);

    while (dbs->sg_cur_index < dbs->sg->nsg) {
        cur_addr = dbs->sg->sg[dbs->sg_cur_index].base + dbs->sg_cur_byte;
        cur_len = dbs->sg->sg[dbs->sg_cur_index].len - dbs->sg_cur_byte;
        mem = dma_memory_map(dbs->sg->as, cur_addr, &cur_len, dbs->dir);
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
        trace_dma_map_wait(dbs);
        dbs->bh = aio_bh_new(dbs->ctx, reschedule_dma, dbs);
        cpu_register_map_client(dbs->bh);
        return;
    }

    if (dbs->iov.size & ~BDRV_SECTOR_MASK) {
        qemu_iovec_discard_back(&dbs->iov, dbs->iov.size & ~BDRV_SECTOR_MASK);
    }

    dbs->acb = dbs->io_func(dbs->offset, &dbs->iov,
                            dma_blk_cb, dbs, dbs->io_func_opaque);
    assert(dbs->acb);
}

static void dma_aio_cancel(BlockAIOCB *acb)
{
    DMAAIOCB *dbs = container_of(acb, DMAAIOCB, common);

    trace_dma_aio_cancel(dbs);

    if (dbs->acb) {
        blk_aio_cancel_async(dbs->acb);
    }
    if (dbs->bh) {
        cpu_unregister_map_client(dbs->bh);
        qemu_bh_delete(dbs->bh);
        dbs->bh = NULL;
    }
}


static const AIOCBInfo dma_aiocb_info = {
    .aiocb_size         = sizeof(DMAAIOCB),
    .cancel_async       = dma_aio_cancel,
};

BlockAIOCB *dma_blk_io(AioContext *ctx,
    QEMUSGList *sg, uint64_t offset,
    DMAIOFunc *io_func, void *io_func_opaque,
    BlockCompletionFunc *cb,
    void *opaque, DMADirection dir)
{
    DMAAIOCB *dbs = qemu_aio_get(&dma_aiocb_info, NULL, cb, opaque);

    trace_dma_blk_io(dbs, io_func_opaque, offset, (dir == DMA_DIRECTION_TO_DEVICE));

    dbs->acb = NULL;
    dbs->sg = sg;
    dbs->ctx = ctx;
    dbs->offset = offset;
    dbs->sg_cur_index = 0;
    dbs->sg_cur_byte = 0;
    dbs->dir = dir;
    dbs->io_func = io_func;
    dbs->io_func_opaque = io_func_opaque;
    dbs->bh = NULL;
    qemu_iovec_init(&dbs->iov, sg->nsg);
    dma_blk_cb(dbs, 0);
    return &dbs->common;
}


static
BlockAIOCB *dma_blk_read_io_func(int64_t offset, QEMUIOVector *iov,
                                 BlockCompletionFunc *cb, void *cb_opaque,
                                 void *opaque)
{
    BlockBackend *blk = opaque;
    return blk_aio_preadv(blk, offset, iov, 0, cb, cb_opaque);
}

BlockAIOCB *dma_blk_read(BlockBackend *blk,
                         QEMUSGList *sg, uint64_t offset,
                         void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_blk_io(blk_get_aio_context(blk),
                      sg, offset, dma_blk_read_io_func, blk, cb, opaque,
                      DMA_DIRECTION_FROM_DEVICE);
}

static
BlockAIOCB *dma_blk_write_io_func(int64_t offset, QEMUIOVector *iov,
                                  BlockCompletionFunc *cb, void *cb_opaque,
                                  void *opaque)
{
    BlockBackend *blk = opaque;
    return blk_aio_pwritev(blk, offset, iov, 0, cb, cb_opaque);
}

BlockAIOCB *dma_blk_write(BlockBackend *blk,
                          QEMUSGList *sg, uint64_t offset,
                          void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_blk_io(blk_get_aio_context(blk),
                      sg, offset, dma_blk_write_io_func, blk, cb, opaque,
                      DMA_DIRECTION_TO_DEVICE);
}


static uint64_t dma_buf_rw(uint8_t *ptr, int32_t len, QEMUSGList *sg,
                           DMADirection dir)
{
    uint64_t resid;
    int sg_cur_index;

    resid = sg->size;
    sg_cur_index = 0;
    len = MIN(len, resid);
    while (len > 0) {
        ScatterGatherEntry entry = sg->sg[sg_cur_index++];
        int32_t xfer = MIN(len, entry.len);
        dma_memory_rw(sg->as, entry.base, ptr, xfer, dir);
        ptr += xfer;
        len -= xfer;
        resid -= xfer;
    }

    return resid;
}

uint64_t dma_buf_read(uint8_t *ptr, int32_t len, QEMUSGList *sg)
{
    return dma_buf_rw(ptr, len, sg, DMA_DIRECTION_FROM_DEVICE);
}

uint64_t dma_buf_write(uint8_t *ptr, int32_t len, QEMUSGList *sg)
{
    return dma_buf_rw(ptr, len, sg, DMA_DIRECTION_TO_DEVICE);
}

void dma_acct_start(BlockBackend *blk, BlockAcctCookie *cookie,
                    QEMUSGList *sg, enum BlockAcctType type)
{
    block_acct_start(blk_get_stats(blk), cookie, sg->size, type);
}
