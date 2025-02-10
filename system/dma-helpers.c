/*
 * DMA helper functions
 *
 * Copyright (c) 2009,2020 Red Hat
 *
 * This work is licensed under the terms of the GNU General Public License
 * (GNU GPL), version 2 or later.
 */

#include "qemu/osdep.h"
#include "system/block-backend.h"
#include "system/dma.h"
#include "trace.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "system/cpu-timers.h"
#include "qemu/range.h"

/* #define DEBUG_IOMMU */

MemTxResult dma_memory_set(AddressSpace *as, dma_addr_t addr,
                           uint8_t c, dma_addr_t len, MemTxAttrs attrs)
{
    dma_barrier(as, DMA_DIRECTION_FROM_DEVICE);

    return address_space_set(as, addr, c, len, attrs);
}

void qemu_sglist_init(QEMUSGList *qsg, DeviceState *dev, int alloc_hint,
                      AddressSpace *as)
{
    qsg->sg = g_new(ScatterGatherEntry, alloc_hint);
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
        qsg->sg = g_renew(ScatterGatherEntry, qsg->sg, qsg->nalloc);
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
    uint32_t align;
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

    assert(!dbs->acb && dbs->bh);
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

    assert(!dbs->acb && !dbs->bh);
    dma_blk_unmap(dbs);
    if (dbs->common.cb) {
        dbs->common.cb(dbs->common.opaque, ret);
    }
    qemu_iovec_destroy(&dbs->iov);
    qemu_aio_unref(dbs);
}

static void dma_blk_cb(void *opaque, int ret)
{
    DMAAIOCB *dbs = (DMAAIOCB *)opaque;
    AioContext *ctx = dbs->ctx;
    dma_addr_t cur_addr, cur_len;
    void *mem;

    trace_dma_blk_cb(dbs, ret);

    /* DMAAIOCB is not thread-safe and must be accessed only from dbs->ctx */
    assert(ctx == qemu_get_current_aio_context());

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
        mem = dma_memory_map(dbs->sg->as, cur_addr, &cur_len, dbs->dir,
                             MEMTXATTRS_UNSPECIFIED);
        /*
         * Make reads deterministic in icount mode. Windows sometimes issues
         * disk read requests with overlapping SGs. It leads
         * to non-determinism, because resulting buffer contents may be mixed
         * from several sectors. This code splits all SGs into several
         * groups. SGs in every group do not overlap.
         */
        if (mem && icount_enabled() && dbs->dir == DMA_DIRECTION_FROM_DEVICE) {
            int i;
            for (i = 0 ; i < dbs->iov.niov ; ++i) {
                if (ranges_overlap((intptr_t)dbs->iov.iov[i].iov_base,
                                   dbs->iov.iov[i].iov_len, (intptr_t)mem,
                                   cur_len)) {
                    dma_memory_unmap(dbs->sg->as, mem, cur_len,
                                     dbs->dir, cur_len);
                    mem = NULL;
                    break;
                }
            }
        }
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
        dbs->bh = aio_bh_new(ctx, reschedule_dma, dbs);
        address_space_register_map_client(dbs->sg->as, dbs->bh);
        return;
    }

    if (!QEMU_IS_ALIGNED(dbs->iov.size, dbs->align)) {
        qemu_iovec_discard_back(&dbs->iov,
                                QEMU_ALIGN_DOWN(dbs->iov.size, dbs->align));
    }

    dbs->acb = dbs->io_func(dbs->offset, &dbs->iov,
                            dma_blk_cb, dbs, dbs->io_func_opaque);
    assert(dbs->acb);
}

static void dma_aio_cancel(BlockAIOCB *acb)
{
    DMAAIOCB *dbs = container_of(acb, DMAAIOCB, common);

    trace_dma_aio_cancel(dbs);

    assert(!(dbs->acb && dbs->bh));
    if (dbs->acb) {
        /* This will invoke dma_blk_cb.  */
        blk_aio_cancel_async(dbs->acb);
        return;
    }

    if (dbs->bh) {
        address_space_unregister_map_client(dbs->sg->as, dbs->bh);
        qemu_bh_delete(dbs->bh);
        dbs->bh = NULL;
    }
    if (dbs->common.cb) {
        dbs->common.cb(dbs->common.opaque, -ECANCELED);
    }
}

static const AIOCBInfo dma_aiocb_info = {
    .aiocb_size         = sizeof(DMAAIOCB),
    .cancel_async       = dma_aio_cancel,
};

BlockAIOCB *dma_blk_io(AioContext *ctx,
    QEMUSGList *sg, uint64_t offset, uint32_t align,
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
    dbs->align = align;
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
                         QEMUSGList *sg, uint64_t offset, uint32_t align,
                         void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_blk_io(blk_get_aio_context(blk), sg, offset, align,
                      dma_blk_read_io_func, blk, cb, opaque,
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
                          QEMUSGList *sg, uint64_t offset, uint32_t align,
                          void (*cb)(void *opaque, int ret), void *opaque)
{
    return dma_blk_io(blk_get_aio_context(blk), sg, offset, align,
                      dma_blk_write_io_func, blk, cb, opaque,
                      DMA_DIRECTION_TO_DEVICE);
}


static MemTxResult dma_buf_rw(void *buf, dma_addr_t len, dma_addr_t *residual,
                              QEMUSGList *sg, DMADirection dir,
                              MemTxAttrs attrs)
{
    uint8_t *ptr = buf;
    dma_addr_t xresidual;
    int sg_cur_index;
    MemTxResult res = MEMTX_OK;

    xresidual = sg->size;
    sg_cur_index = 0;
    len = MIN(len, xresidual);
    while (len > 0) {
        ScatterGatherEntry entry = sg->sg[sg_cur_index++];
        dma_addr_t xfer = MIN(len, entry.len);
        res |= dma_memory_rw(sg->as, entry.base, ptr, xfer, dir, attrs);
        ptr += xfer;
        len -= xfer;
        xresidual -= xfer;
    }

    if (residual) {
        *residual = xresidual;
    }
    return res;
}

MemTxResult dma_buf_read(void *ptr, dma_addr_t len, dma_addr_t *residual,
                         QEMUSGList *sg, MemTxAttrs attrs)
{
    return dma_buf_rw(ptr, len, residual, sg, DMA_DIRECTION_FROM_DEVICE, attrs);
}

MemTxResult dma_buf_write(void *ptr, dma_addr_t len, dma_addr_t *residual,
                          QEMUSGList *sg, MemTxAttrs attrs)
{
    return dma_buf_rw(ptr, len, residual, sg, DMA_DIRECTION_TO_DEVICE, attrs);
}

void dma_acct_start(BlockBackend *blk, BlockAcctCookie *cookie,
                    QEMUSGList *sg, enum BlockAcctType type)
{
    block_acct_start(blk_get_stats(blk), cookie, sg->size, type);
}

uint64_t dma_aligned_pow2_mask(uint64_t start, uint64_t end, int max_addr_bits)
{
    uint64_t max_mask = UINT64_MAX, addr_mask = end - start;
    uint64_t alignment_mask, size_mask;

    if (max_addr_bits != 64) {
        max_mask = (1ULL << max_addr_bits) - 1;
    }

    alignment_mask = start ? (start & -start) - 1 : max_mask;
    alignment_mask = MIN(alignment_mask, max_mask);
    size_mask = MIN(addr_mask, max_mask);

    if (alignment_mask <= size_mask) {
        /* Increase the alignment of start */
        return alignment_mask;
    } else {
        /* Find the largest page mask from size */
        if (addr_mask == UINT64_MAX) {
            return UINT64_MAX;
        }
        return (1ULL << (63 - clz64(addr_mask + 1))) - 1;
    }
}

