/*
 * Block layer I/O functions
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "sysemu/block-backend.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "block/throttle-groups.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */

static BlockAIOCB *bdrv_aio_readv_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque);
static BlockAIOCB *bdrv_aio_writev_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque);
static BlockAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                         int64_t sector_num,
                                         QEMUIOVector *qiov,
                                         int nb_sectors,
                                         BdrvRequestFlags flags,
                                         BlockCompletionFunc *cb,
                                         void *opaque,
                                         bool is_write);
static void coroutine_fn bdrv_co_do_rw(void *opaque);
static int coroutine_fn bdrv_co_do_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags);

/* throttling disk I/O limits */
void bdrv_set_io_limits(BlockDriverState *bs,
                        ThrottleConfig *cfg)
{
    throttle_group_config(bs, cfg);
}

void bdrv_no_throttling_begin(BlockDriverState *bs)
{
    if (bs->io_limits_disabled++ == 0) {
        throttle_group_restart_bs(bs);
    }
}

void bdrv_no_throttling_end(BlockDriverState *bs)
{
    assert(bs->io_limits_disabled);
    --bs->io_limits_disabled;
}

void bdrv_io_limits_disable(BlockDriverState *bs)
{
    assert(bs->throttle_state);
    bdrv_no_throttling_begin(bs);
    throttle_group_unregister_bs(bs);
    bdrv_no_throttling_end(bs);
}

/* should be called before bdrv_set_io_limits if a limit is set */
void bdrv_io_limits_enable(BlockDriverState *bs, const char *group)
{
    assert(!bs->throttle_state);
    throttle_group_register_bs(bs, group);
}

void bdrv_io_limits_update_group(BlockDriverState *bs, const char *group)
{
    /* this bs is not part of any group */
    if (!bs->throttle_state) {
        return;
    }

    /* this bs is a part of the same group than the one we want */
    if (!g_strcmp0(throttle_group_get_name(bs), group)) {
        return;
    }

    /* need to change the group this bs belong to */
    bdrv_io_limits_disable(bs);
    bdrv_io_limits_enable(bs, group);
}

void bdrv_setup_io_funcs(BlockDriver *bdrv)
{
    /* bdrv_co_readv_em()/brdv_co_writev_em() work in terms of aio, so if
     * the block driver lacks aio we need to emulate that.
     */
    if (!bdrv->bdrv_aio_readv) {
        /* add AIO emulation layer */
        bdrv->bdrv_aio_readv = bdrv_aio_readv_em;
        bdrv->bdrv_aio_writev = bdrv_aio_writev_em;
    }
}

void bdrv_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BlockDriver *drv = bs->drv;
    Error *local_err = NULL;

    memset(&bs->bl, 0, sizeof(bs->bl));

    if (!drv) {
        return;
    }

    /* Take some limits from the children as a default */
    if (bs->file) {
        bdrv_refresh_limits(bs->file->bs, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        bs->bl.opt_transfer_length = bs->file->bs->bl.opt_transfer_length;
        bs->bl.max_transfer_length = bs->file->bs->bl.max_transfer_length;
        bs->bl.min_mem_alignment = bs->file->bs->bl.min_mem_alignment;
        bs->bl.opt_mem_alignment = bs->file->bs->bl.opt_mem_alignment;
        bs->bl.max_iov = bs->file->bs->bl.max_iov;
    } else {
        bs->bl.min_mem_alignment = 512;
        bs->bl.opt_mem_alignment = getpagesize();

        /* Safe default since most protocols use readv()/writev()/etc */
        bs->bl.max_iov = IOV_MAX;
    }

    if (bs->backing) {
        bdrv_refresh_limits(bs->backing->bs, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        bs->bl.opt_transfer_length =
            MAX(bs->bl.opt_transfer_length,
                bs->backing->bs->bl.opt_transfer_length);
        bs->bl.max_transfer_length =
            MIN_NON_ZERO(bs->bl.max_transfer_length,
                         bs->backing->bs->bl.max_transfer_length);
        bs->bl.opt_mem_alignment =
            MAX(bs->bl.opt_mem_alignment,
                bs->backing->bs->bl.opt_mem_alignment);
        bs->bl.min_mem_alignment =
            MAX(bs->bl.min_mem_alignment,
                bs->backing->bs->bl.min_mem_alignment);
        bs->bl.max_iov =
            MIN(bs->bl.max_iov,
                bs->backing->bs->bl.max_iov);
    }

    /* Then let the driver override it */
    if (drv->bdrv_refresh_limits) {
        drv->bdrv_refresh_limits(bs, errp);
    }
}

/**
 * The copy-on-read flag is actually a reference count so multiple users may
 * use the feature without worrying about clobbering its previous state.
 * Copy-on-read stays enabled until all users have called to disable it.
 */
void bdrv_enable_copy_on_read(BlockDriverState *bs)
{
    bs->copy_on_read++;
}

void bdrv_disable_copy_on_read(BlockDriverState *bs)
{
    assert(bs->copy_on_read > 0);
    bs->copy_on_read--;
}

/* Check if any requests are in-flight (including throttled requests) */
bool bdrv_requests_pending(BlockDriverState *bs)
{
    BdrvChild *child;

    if (!QLIST_EMPTY(&bs->tracked_requests)) {
        return true;
    }
    if (!qemu_co_queue_empty(&bs->throttled_reqs[0])) {
        return true;
    }
    if (!qemu_co_queue_empty(&bs->throttled_reqs[1])) {
        return true;
    }

    QLIST_FOREACH(child, &bs->children, next) {
        if (bdrv_requests_pending(child->bs)) {
            return true;
        }
    }

    return false;
}

static void bdrv_drain_recurse(BlockDriverState *bs)
{
    BdrvChild *child;

    if (bs->drv && bs->drv->bdrv_drain) {
        bs->drv->bdrv_drain(bs);
    }
    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_drain_recurse(child->bs);
    }
}

typedef struct {
    Coroutine *co;
    BlockDriverState *bs;
    QEMUBH *bh;
    bool done;
} BdrvCoDrainData;

static void bdrv_drain_poll(BlockDriverState *bs)
{
    bool busy = true;

    while (busy) {
        /* Keep iterating */
        busy = bdrv_requests_pending(bs);
        busy |= aio_poll(bdrv_get_aio_context(bs), busy);
    }
}

static void bdrv_co_drain_bh_cb(void *opaque)
{
    BdrvCoDrainData *data = opaque;
    Coroutine *co = data->co;

    qemu_bh_delete(data->bh);
    bdrv_drain_poll(data->bs);
    data->done = true;
    qemu_coroutine_enter(co, NULL);
}

static void coroutine_fn bdrv_co_yield_to_drain(BlockDriverState *bs)
{
    BdrvCoDrainData data;

    /* Calling bdrv_drain() from a BH ensures the current coroutine yields and
     * other coroutines run if they were queued from
     * qemu_co_queue_run_restart(). */

    assert(qemu_in_coroutine());
    data = (BdrvCoDrainData) {
        .co = qemu_coroutine_self(),
        .bs = bs,
        .done = false,
        .bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_co_drain_bh_cb, &data),
    };
    qemu_bh_schedule(data.bh);

    qemu_coroutine_yield();
    /* If we are resumed from some other event (such as an aio completion or a
     * timer callback), it is a bug in the caller that should be fixed. */
    assert(data.done);
}

/*
 * Wait for pending requests to complete on a single BlockDriverState subtree,
 * and suspend block driver's internal I/O until next request arrives.
 *
 * Note that unlike bdrv_drain_all(), the caller must hold the BlockDriverState
 * AioContext.
 *
 * Only this BlockDriverState's AioContext is run, so in-flight requests must
 * not depend on events in other AioContexts.  In that case, use
 * bdrv_drain_all() instead.
 */
void coroutine_fn bdrv_co_drain(BlockDriverState *bs)
{
    bdrv_no_throttling_begin(bs);
    bdrv_io_unplugged_begin(bs);
    bdrv_drain_recurse(bs);
    bdrv_co_yield_to_drain(bs);
    bdrv_io_unplugged_end(bs);
    bdrv_no_throttling_end(bs);
}

void bdrv_drain(BlockDriverState *bs)
{
    bdrv_no_throttling_begin(bs);
    bdrv_io_unplugged_begin(bs);
    bdrv_drain_recurse(bs);
    if (qemu_in_coroutine()) {
        bdrv_co_yield_to_drain(bs);
    } else {
        bdrv_drain_poll(bs);
    }
    bdrv_io_unplugged_end(bs);
    bdrv_no_throttling_end(bs);
}

/*
 * Wait for pending requests to complete across all BlockDriverStates
 *
 * This function does not flush data to disk, use bdrv_flush_all() for that
 * after calling this function.
 */
void bdrv_drain_all(void)
{
    /* Always run first iteration so any pending completion BHs run */
    bool busy = true;
    BlockDriverState *bs = NULL;
    GSList *aio_ctxs = NULL, *ctx;

    while ((bs = bdrv_next(bs))) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        if (bs->job) {
            block_job_pause(bs->job);
        }
        bdrv_no_throttling_begin(bs);
        bdrv_io_unplugged_begin(bs);
        bdrv_drain_recurse(bs);
        aio_context_release(aio_context);

        if (!g_slist_find(aio_ctxs, aio_context)) {
            aio_ctxs = g_slist_prepend(aio_ctxs, aio_context);
        }
    }

    /* Note that completion of an asynchronous I/O operation can trigger any
     * number of other I/O operations on other devices---for example a
     * coroutine can submit an I/O request to another device in response to
     * request completion.  Therefore we must keep looping until there was no
     * more activity rather than simply draining each device independently.
     */
    while (busy) {
        busy = false;

        for (ctx = aio_ctxs; ctx != NULL; ctx = ctx->next) {
            AioContext *aio_context = ctx->data;
            bs = NULL;

            aio_context_acquire(aio_context);
            while ((bs = bdrv_next(bs))) {
                if (aio_context == bdrv_get_aio_context(bs)) {
                    if (bdrv_requests_pending(bs)) {
                        busy = true;
                        aio_poll(aio_context, busy);
                    }
                }
            }
            busy |= aio_poll(aio_context, false);
            aio_context_release(aio_context);
        }
    }

    bs = NULL;
    while ((bs = bdrv_next(bs))) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_io_unplugged_end(bs);
        bdrv_no_throttling_end(bs);
        if (bs->job) {
            block_job_resume(bs->job);
        }
        aio_context_release(aio_context);
    }
    g_slist_free(aio_ctxs);
}

/**
 * Remove an active request from the tracked requests list
 *
 * This function should be called when a tracked request is completing.
 */
static void tracked_request_end(BdrvTrackedRequest *req)
{
    if (req->serialising) {
        req->bs->serialising_in_flight--;
    }

    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

/**
 * Add an active request to the tracked requests list
 */
static void tracked_request_begin(BdrvTrackedRequest *req,
                                  BlockDriverState *bs,
                                  int64_t offset,
                                  unsigned int bytes,
                                  enum BdrvTrackedRequestType type)
{
    *req = (BdrvTrackedRequest){
        .bs = bs,
        .offset         = offset,
        .bytes          = bytes,
        .type           = type,
        .co             = qemu_coroutine_self(),
        .serialising    = false,
        .overlap_offset = offset,
        .overlap_bytes  = bytes,
    };

    qemu_co_queue_init(&req->wait_queue);

    QLIST_INSERT_HEAD(&bs->tracked_requests, req, list);
}

static void mark_request_serialising(BdrvTrackedRequest *req, uint64_t align)
{
    int64_t overlap_offset = req->offset & ~(align - 1);
    unsigned int overlap_bytes = ROUND_UP(req->offset + req->bytes, align)
                               - overlap_offset;

    if (!req->serialising) {
        req->bs->serialising_in_flight++;
        req->serialising = true;
    }

    req->overlap_offset = MIN(req->overlap_offset, overlap_offset);
    req->overlap_bytes = MAX(req->overlap_bytes, overlap_bytes);
}

/**
 * Round a region to cluster boundaries
 */
void bdrv_round_to_clusters(BlockDriverState *bs,
                            int64_t sector_num, int nb_sectors,
                            int64_t *cluster_sector_num,
                            int *cluster_nb_sectors)
{
    BlockDriverInfo bdi;

    if (bdrv_get_info(bs, &bdi) < 0 || bdi.cluster_size == 0) {
        *cluster_sector_num = sector_num;
        *cluster_nb_sectors = nb_sectors;
    } else {
        int64_t c = bdi.cluster_size / BDRV_SECTOR_SIZE;
        *cluster_sector_num = QEMU_ALIGN_DOWN(sector_num, c);
        *cluster_nb_sectors = QEMU_ALIGN_UP(sector_num - *cluster_sector_num +
                                            nb_sectors, c);
    }
}

static int bdrv_get_cluster_size(BlockDriverState *bs)
{
    BlockDriverInfo bdi;
    int ret;

    ret = bdrv_get_info(bs, &bdi);
    if (ret < 0 || bdi.cluster_size == 0) {
        return bs->request_alignment;
    } else {
        return bdi.cluster_size;
    }
}

static bool tracked_request_overlaps(BdrvTrackedRequest *req,
                                     int64_t offset, unsigned int bytes)
{
    /*        aaaa   bbbb */
    if (offset >= req->overlap_offset + req->overlap_bytes) {
        return false;
    }
    /* bbbb   aaaa        */
    if (req->overlap_offset >= offset + bytes) {
        return false;
    }
    return true;
}

static bool coroutine_fn wait_serialising_requests(BdrvTrackedRequest *self)
{
    BlockDriverState *bs = self->bs;
    BdrvTrackedRequest *req;
    bool retry;
    bool waited = false;

    if (!bs->serialising_in_flight) {
        return false;
    }

    do {
        retry = false;
        QLIST_FOREACH(req, &bs->tracked_requests, list) {
            if (req == self || (!req->serialising && !self->serialising)) {
                continue;
            }
            if (tracked_request_overlaps(req, self->overlap_offset,
                                         self->overlap_bytes))
            {
                /* Hitting this means there was a reentrant request, for
                 * example, a block driver issuing nested requests.  This must
                 * never happen since it means deadlock.
                 */
                assert(qemu_coroutine_self() != req->co);

                /* If the request is already (indirectly) waiting for us, or
                 * will wait for us as soon as it wakes up, then just go on
                 * (instead of producing a deadlock in the former case). */
                if (!req->waiting_for) {
                    self->waiting_for = req;
                    qemu_co_queue_wait(&req->wait_queue);
                    self->waiting_for = NULL;
                    retry = true;
                    waited = true;
                    break;
                }
            }
        }
    } while (retry);

    return waited;
}

static int bdrv_check_byte_request(BlockDriverState *bs, int64_t offset,
                                   size_t size)
{
    if (size > BDRV_REQUEST_MAX_SECTORS << BDRV_SECTOR_BITS) {
        return -EIO;
    }

    if (!bdrv_is_inserted(bs)) {
        return -ENOMEDIUM;
    }

    if (offset < 0) {
        return -EIO;
    }

    return 0;
}

static int bdrv_check_request(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors)
{
    if (nb_sectors < 0 || nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return -EIO;
    }

    return bdrv_check_byte_request(bs, sector_num * BDRV_SECTOR_SIZE,
                                   nb_sectors * BDRV_SECTOR_SIZE);
}

typedef struct RwCo {
    BlockDriverState *bs;
    int64_t offset;
    QEMUIOVector *qiov;
    bool is_write;
    int ret;
    BdrvRequestFlags flags;
} RwCo;

static void coroutine_fn bdrv_rw_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    if (!rwco->is_write) {
        rwco->ret = bdrv_co_do_preadv(rwco->bs, rwco->offset,
                                      rwco->qiov->size, rwco->qiov,
                                      rwco->flags);
    } else {
        rwco->ret = bdrv_co_do_pwritev(rwco->bs, rwco->offset,
                                       rwco->qiov->size, rwco->qiov,
                                       rwco->flags);
    }
}

/*
 * Process a vectored synchronous request using coroutines
 */
static int bdrv_prwv_co(BlockDriverState *bs, int64_t offset,
                        QEMUIOVector *qiov, bool is_write,
                        BdrvRequestFlags flags)
{
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .offset = offset,
        .qiov = qiov,
        .is_write = is_write,
        .ret = NOT_DONE,
        .flags = flags,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_rw_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_rw_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }
    return rwco.ret;
}

/*
 * Process a synchronous request using coroutines
 */
static int bdrv_rw_co(BlockDriverState *bs, int64_t sector_num, uint8_t *buf,
                      int nb_sectors, bool is_write, BdrvRequestFlags flags)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = nb_sectors * BDRV_SECTOR_SIZE,
    };

    if (nb_sectors < 0 || nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_prwv_co(bs, sector_num << BDRV_SECTOR_BITS,
                        &qiov, is_write, flags);
}

/* return < 0 if error. See bdrv_write() for the return codes */
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors)
{
    return bdrv_rw_co(bs, sector_num, buf, nb_sectors, false, 0);
}

/* Return < 0 if error. Important errors are:
  -EIO         generic I/O error (may happen for all errors)
  -ENOMEDIUM   No media inserted.
  -EINVAL      Invalid sector number or nb_sectors
  -EACCES      Trying to write a read-only device
*/
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors)
{
    return bdrv_rw_co(bs, sector_num, (uint8_t *)buf, nb_sectors, true, 0);
}

int bdrv_write_zeroes(BlockDriverState *bs, int64_t sector_num,
                      int nb_sectors, BdrvRequestFlags flags)
{
    return bdrv_rw_co(bs, sector_num, NULL, nb_sectors, true,
                      BDRV_REQ_ZERO_WRITE | flags);
}

/*
 * Completely zero out a block device with the help of bdrv_write_zeroes.
 * The operation is sped up by checking the block status and only writing
 * zeroes to the device if they currently do not return zeroes. Optional
 * flags are passed through to bdrv_write_zeroes (e.g. BDRV_REQ_MAY_UNMAP).
 *
 * Returns < 0 on error, 0 on success. For error codes see bdrv_write().
 */
int bdrv_make_zero(BlockDriverState *bs, BdrvRequestFlags flags)
{
    int64_t target_sectors, ret, nb_sectors, sector_num = 0;
    BlockDriverState *file;
    int n;

    target_sectors = bdrv_nb_sectors(bs);
    if (target_sectors < 0) {
        return target_sectors;
    }

    for (;;) {
        nb_sectors = MIN(target_sectors - sector_num, BDRV_REQUEST_MAX_SECTORS);
        if (nb_sectors <= 0) {
            return 0;
        }
        ret = bdrv_get_block_status(bs, sector_num, nb_sectors, &n, &file);
        if (ret < 0) {
            error_report("error getting block status at sector %" PRId64 ": %s",
                         sector_num, strerror(-ret));
            return ret;
        }
        if (ret & BDRV_BLOCK_ZERO) {
            sector_num += n;
            continue;
        }
        ret = bdrv_write_zeroes(bs, sector_num, n, flags);
        if (ret < 0) {
            error_report("error writing zeroes at sector %" PRId64 ": %s",
                         sector_num, strerror(-ret));
            return ret;
        }
        sector_num += n;
    }
}

int bdrv_pread(BlockDriverState *bs, int64_t offset, void *buf, int bytes)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = bytes,
    };
    int ret;

    if (bytes < 0) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    ret = bdrv_prwv_co(bs, offset, &qiov, false, 0);
    if (ret < 0) {
        return ret;
    }

    return bytes;
}

int bdrv_pwritev(BlockDriverState *bs, int64_t offset, QEMUIOVector *qiov)
{
    int ret;

    ret = bdrv_prwv_co(bs, offset, qiov, true, 0);
    if (ret < 0) {
        return ret;
    }

    return qiov->size;
}

int bdrv_pwrite(BlockDriverState *bs, int64_t offset,
                const void *buf, int bytes)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base   = (void *) buf,
        .iov_len    = bytes,
    };

    if (bytes < 0) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_pwritev(bs, offset, &qiov);
}

/*
 * Writes to the file and ensures that no writes are reordered across this
 * request (acts as a barrier)
 *
 * Returns 0 on success, -errno in error cases.
 */
int bdrv_pwrite_sync(BlockDriverState *bs, int64_t offset,
    const void *buf, int count)
{
    int ret;

    ret = bdrv_pwrite(bs, offset, buf, count);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_flush(bs);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

typedef struct CoroutineIOCompletion {
    Coroutine *coroutine;
    int ret;
} CoroutineIOCompletion;

static void bdrv_co_io_em_complete(void *opaque, int ret)
{
    CoroutineIOCompletion *co = opaque;

    co->ret = ret;
    qemu_coroutine_enter(co->coroutine, NULL);
}

static int coroutine_fn bdrv_driver_preadv(BlockDriverState *bs,
                                           uint64_t offset, uint64_t bytes,
                                           QEMUIOVector *qiov, int flags)
{
    BlockDriver *drv = bs->drv;
    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes >> BDRV_SECTOR_BITS) <= BDRV_REQUEST_MAX_SECTORS);

    if (drv->bdrv_co_readv) {
        return drv->bdrv_co_readv(bs, sector_num, nb_sectors, qiov);
    } else {
        BlockAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = bs->drv->bdrv_aio_readv(bs, sector_num, qiov, nb_sectors,
                                      bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            return -EIO;
        } else {
            qemu_coroutine_yield();
            return co.ret;
        }
    }
}

static int coroutine_fn bdrv_driver_pwritev(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov, int flags)
{
    BlockDriver *drv = bs->drv;
    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    int ret;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes >> BDRV_SECTOR_BITS) <= BDRV_REQUEST_MAX_SECTORS);

    if (drv->bdrv_co_writev_flags) {
        ret = drv->bdrv_co_writev_flags(bs, sector_num, nb_sectors, qiov,
                                        flags);
    } else if (drv->bdrv_co_writev) {
        assert(drv->supported_write_flags == 0);
        ret = drv->bdrv_co_writev(bs, sector_num, nb_sectors, qiov);
    } else {
        BlockAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = bs->drv->bdrv_aio_writev(bs, sector_num, qiov, nb_sectors,
                                       bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            return -EIO;
        } else {
            qemu_coroutine_yield();
            return co.ret;
        }
    }

    if (ret == 0 && (flags & BDRV_REQ_FUA) &&
        !(drv->supported_write_flags & BDRV_REQ_FUA))
    {
        ret = bdrv_co_flush(bs);
    }

    return ret;
}

static int coroutine_fn bdrv_co_do_copy_on_readv(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    /* Perform I/O through a temporary buffer so that users who scribble over
     * their read buffer while the operation is in progress do not end up
     * modifying the image file.  This is critical for zero-copy guest I/O
     * where anything might happen inside guest memory.
     */
    void *bounce_buffer;

    BlockDriver *drv = bs->drv;
    struct iovec iov;
    QEMUIOVector bounce_qiov;
    int64_t cluster_sector_num;
    int cluster_nb_sectors;
    size_t skip_bytes;
    int ret;

    /* Cover entire cluster so no additional backing file I/O is required when
     * allocating cluster in the image file.
     */
    bdrv_round_to_clusters(bs, sector_num, nb_sectors,
                           &cluster_sector_num, &cluster_nb_sectors);

    trace_bdrv_co_do_copy_on_readv(bs, sector_num, nb_sectors,
                                   cluster_sector_num, cluster_nb_sectors);

    iov.iov_len = cluster_nb_sectors * BDRV_SECTOR_SIZE;
    iov.iov_base = bounce_buffer = qemu_try_blockalign(bs, iov.iov_len);
    if (bounce_buffer == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    qemu_iovec_init_external(&bounce_qiov, &iov, 1);

    ret = bdrv_driver_preadv(bs, cluster_sector_num * BDRV_SECTOR_SIZE,
                             cluster_nb_sectors * BDRV_SECTOR_SIZE,
                             &bounce_qiov, 0);
    if (ret < 0) {
        goto err;
    }

    if (drv->bdrv_co_write_zeroes &&
        buffer_is_zero(bounce_buffer, iov.iov_len)) {
        ret = bdrv_co_do_write_zeroes(bs, cluster_sector_num,
                                      cluster_nb_sectors, 0);
    } else {
        /* This does not change the data on the disk, it is not necessary
         * to flush even in cache=writethrough mode.
         */
        ret = bdrv_driver_pwritev(bs, cluster_sector_num * BDRV_SECTOR_SIZE,
                                  cluster_nb_sectors * BDRV_SECTOR_SIZE,
                                  &bounce_qiov, 0);
    }

    if (ret < 0) {
        /* It might be okay to ignore write errors for guest requests.  If this
         * is a deliberate copy-on-read then we don't want to ignore the error.
         * Simply report it in all cases.
         */
        goto err;
    }

    skip_bytes = (sector_num - cluster_sector_num) * BDRV_SECTOR_SIZE;
    qemu_iovec_from_buf(qiov, 0, bounce_buffer + skip_bytes,
                        nb_sectors * BDRV_SECTOR_SIZE);

err:
    qemu_vfree(bounce_buffer);
    return ret;
}

/*
 * Forwards an already correctly aligned request to the BlockDriver. This
 * handles copy on read and zeroing after EOF; any other features must be
 * implemented by the caller.
 */
static int coroutine_fn bdrv_aligned_preadv(BlockDriverState *bs,
    BdrvTrackedRequest *req, int64_t offset, unsigned int bytes,
    int64_t align, QEMUIOVector *qiov, int flags)
{
    int ret;

    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert(!qiov || bytes == qiov->size);
    assert((bs->open_flags & BDRV_O_NO_IO) == 0);

    /* Handle Copy on Read and associated serialisation */
    if (flags & BDRV_REQ_COPY_ON_READ) {
        /* If we touch the same cluster it counts as an overlap.  This
         * guarantees that allocating writes will be serialized and not race
         * with each other for the same cluster.  For example, in copy-on-read
         * it ensures that the CoR read and write operations are atomic and
         * guest writes cannot interleave between them. */
        mark_request_serialising(req, bdrv_get_cluster_size(bs));
    }

    if (!(flags & BDRV_REQ_NO_SERIALISING)) {
        wait_serialising_requests(req);
    }

    if (flags & BDRV_REQ_COPY_ON_READ) {
        int pnum;

        ret = bdrv_is_allocated(bs, sector_num, nb_sectors, &pnum);
        if (ret < 0) {
            goto out;
        }

        if (!ret || pnum != nb_sectors) {
            ret = bdrv_co_do_copy_on_readv(bs, sector_num, nb_sectors, qiov);
            goto out;
        }
    }

    /* Forward the request to the BlockDriver */
    if (!bs->zero_beyond_eof) {
        ret = bdrv_driver_preadv(bs, offset, bytes, qiov, 0);
    } else {
        /* Read zeros after EOF */
        int64_t total_sectors, max_nb_sectors;

        total_sectors = bdrv_nb_sectors(bs);
        if (total_sectors < 0) {
            ret = total_sectors;
            goto out;
        }

        max_nb_sectors = ROUND_UP(MAX(0, total_sectors - sector_num),
                                  align >> BDRV_SECTOR_BITS);
        if (nb_sectors < max_nb_sectors) {
            ret = bdrv_driver_preadv(bs, offset, bytes, qiov, 0);
        } else if (max_nb_sectors > 0) {
            QEMUIOVector local_qiov;

            qemu_iovec_init(&local_qiov, qiov->niov);
            qemu_iovec_concat(&local_qiov, qiov, 0,
                              max_nb_sectors * BDRV_SECTOR_SIZE);

            ret = bdrv_driver_preadv(bs, offset,
                                     max_nb_sectors * BDRV_SECTOR_SIZE,
                                     &local_qiov, 0);

            qemu_iovec_destroy(&local_qiov);
        } else {
            ret = 0;
        }

        /* Reading beyond end of file is supposed to produce zeroes */
        if (ret == 0 && total_sectors < sector_num + nb_sectors) {
            uint64_t offset = MAX(0, total_sectors - sector_num);
            uint64_t bytes = (sector_num + nb_sectors - offset) *
                              BDRV_SECTOR_SIZE;
            qemu_iovec_memset(qiov, offset * BDRV_SECTOR_SIZE, 0, bytes);
        }
    }

out:
    return ret;
}

/*
 * Handle a read request in coroutine context
 */
int coroutine_fn bdrv_co_do_preadv(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest req;

    /* TODO Lift BDRV_SECTOR_SIZE restriction in BlockDriver interface */
    uint64_t align = MAX(BDRV_SECTOR_SIZE, bs->request_alignment);
    uint8_t *head_buf = NULL;
    uint8_t *tail_buf = NULL;
    QEMUIOVector local_qiov;
    bool use_local_qiov = false;
    int ret;

    if (!drv) {
        return -ENOMEDIUM;
    }

    ret = bdrv_check_byte_request(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /* Don't do copy-on-read if we read data before write operation */
    if (bs->copy_on_read && !(flags & BDRV_REQ_NO_SERIALISING)) {
        flags |= BDRV_REQ_COPY_ON_READ;
    }

    /* throttling disk I/O */
    if (bs->throttle_state) {
        throttle_group_co_io_limits_intercept(bs, bytes, false);
    }

    /* Align read if necessary by padding qiov */
    if (offset & (align - 1)) {
        head_buf = qemu_blockalign(bs, align);
        qemu_iovec_init(&local_qiov, qiov->niov + 2);
        qemu_iovec_add(&local_qiov, head_buf, offset & (align - 1));
        qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
        use_local_qiov = true;

        bytes += offset & (align - 1);
        offset = offset & ~(align - 1);
    }

    if ((offset + bytes) & (align - 1)) {
        if (!use_local_qiov) {
            qemu_iovec_init(&local_qiov, qiov->niov + 1);
            qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
            use_local_qiov = true;
        }
        tail_buf = qemu_blockalign(bs, align);
        qemu_iovec_add(&local_qiov, tail_buf,
                       align - ((offset + bytes) & (align - 1)));

        bytes = ROUND_UP(bytes, align);
    }

    tracked_request_begin(&req, bs, offset, bytes, BDRV_TRACKED_READ);
    ret = bdrv_aligned_preadv(bs, &req, offset, bytes, align,
                              use_local_qiov ? &local_qiov : qiov,
                              flags);
    tracked_request_end(&req);

    if (use_local_qiov) {
        qemu_iovec_destroy(&local_qiov);
        qemu_vfree(head_buf);
        qemu_vfree(tail_buf);
    }

    return ret;
}

static int coroutine_fn bdrv_co_do_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    if (nb_sectors < 0 || nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return -EINVAL;
    }

    return bdrv_co_do_preadv(bs, sector_num << BDRV_SECTOR_BITS,
                             nb_sectors << BDRV_SECTOR_BITS, qiov, flags);
}

int coroutine_fn bdrv_co_readv(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_readv(bs, sector_num, nb_sectors);

    return bdrv_co_do_readv(bs, sector_num, nb_sectors, qiov, 0);
}

int coroutine_fn bdrv_co_readv_no_serialising(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_readv_no_serialising(bs, sector_num, nb_sectors);

    return bdrv_co_do_readv(bs, sector_num, nb_sectors, qiov,
                            BDRV_REQ_NO_SERIALISING);
}

int coroutine_fn bdrv_co_copy_on_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_copy_on_readv(bs, sector_num, nb_sectors);

    return bdrv_co_do_readv(bs, sector_num, nb_sectors, qiov,
                            BDRV_REQ_COPY_ON_READ);
}

#define MAX_WRITE_ZEROES_BOUNCE_BUFFER 32768

static int coroutine_fn bdrv_co_do_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    QEMUIOVector qiov;
    struct iovec iov = {0};
    int ret = 0;

    int max_write_zeroes = MIN_NON_ZERO(bs->bl.max_write_zeroes,
                                        BDRV_REQUEST_MAX_SECTORS);

    while (nb_sectors > 0 && !ret) {
        int num = nb_sectors;

        /* Align request.  Block drivers can expect the "bulk" of the request
         * to be aligned.
         */
        if (bs->bl.write_zeroes_alignment
            && num > bs->bl.write_zeroes_alignment) {
            if (sector_num % bs->bl.write_zeroes_alignment != 0) {
                /* Make a small request up to the first aligned sector.  */
                num = bs->bl.write_zeroes_alignment;
                num -= sector_num % bs->bl.write_zeroes_alignment;
            } else if ((sector_num + num) % bs->bl.write_zeroes_alignment != 0) {
                /* Shorten the request to the last aligned sector.  num cannot
                 * underflow because num > bs->bl.write_zeroes_alignment.
                 */
                num -= (sector_num + num) % bs->bl.write_zeroes_alignment;
            }
        }

        /* limit request size */
        if (num > max_write_zeroes) {
            num = max_write_zeroes;
        }

        ret = -ENOTSUP;
        /* First try the efficient write zeroes operation */
        if (drv->bdrv_co_write_zeroes) {
            ret = drv->bdrv_co_write_zeroes(bs, sector_num, num, flags);
        }

        if (ret == -ENOTSUP) {
            /* Fall back to bounce buffer if write zeroes is unsupported */
            int max_xfer_len = MIN_NON_ZERO(bs->bl.max_transfer_length,
                                            MAX_WRITE_ZEROES_BOUNCE_BUFFER);
            num = MIN(num, max_xfer_len);
            iov.iov_len = num * BDRV_SECTOR_SIZE;
            if (iov.iov_base == NULL) {
                iov.iov_base = qemu_try_blockalign(bs, num * BDRV_SECTOR_SIZE);
                if (iov.iov_base == NULL) {
                    ret = -ENOMEM;
                    goto fail;
                }
                memset(iov.iov_base, 0, num * BDRV_SECTOR_SIZE);
            }
            qemu_iovec_init_external(&qiov, &iov, 1);

            ret = bdrv_driver_pwritev(bs, sector_num * BDRV_SECTOR_SIZE,
                                      num * BDRV_SECTOR_SIZE, &qiov, 0);

            /* Keep bounce buffer around if it is big enough for all
             * all future requests.
             */
            if (num < max_xfer_len) {
                qemu_vfree(iov.iov_base);
                iov.iov_base = NULL;
            }
        }

        sector_num += num;
        nb_sectors -= num;
    }

fail:
    qemu_vfree(iov.iov_base);
    return ret;
}

/*
 * Forwards an already correctly aligned write request to the BlockDriver.
 */
static int coroutine_fn bdrv_aligned_pwritev(BlockDriverState *bs,
    BdrvTrackedRequest *req, int64_t offset, unsigned int bytes,
    QEMUIOVector *qiov, int flags)
{
    BlockDriver *drv = bs->drv;
    bool waited;
    int ret;

    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert(!qiov || bytes == qiov->size);
    assert((bs->open_flags & BDRV_O_NO_IO) == 0);

    waited = wait_serialising_requests(req);
    assert(!waited || !req->serialising);
    assert(req->overlap_offset <= offset);
    assert(offset + bytes <= req->overlap_offset + req->overlap_bytes);

    ret = notifier_with_return_list_notify(&bs->before_write_notifiers, req);

    if (!ret && bs->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF &&
        !(flags & BDRV_REQ_ZERO_WRITE) && drv->bdrv_co_write_zeroes &&
        qemu_iovec_is_zero(qiov)) {
        flags |= BDRV_REQ_ZERO_WRITE;
        if (bs->detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP) {
            flags |= BDRV_REQ_MAY_UNMAP;
        }
    }

    if (ret < 0) {
        /* Do nothing, write notifier decided to fail this request */
    } else if (flags & BDRV_REQ_ZERO_WRITE) {
        bdrv_debug_event(bs, BLKDBG_PWRITEV_ZERO);
        ret = bdrv_co_do_write_zeroes(bs, sector_num, nb_sectors, flags);
    } else {
        bdrv_debug_event(bs, BLKDBG_PWRITEV);
        ret = bdrv_driver_pwritev(bs, offset, bytes, qiov, flags);
    }
    bdrv_debug_event(bs, BLKDBG_PWRITEV_DONE);

    bdrv_set_dirty(bs, sector_num, nb_sectors);

    if (bs->wr_highest_offset < offset + bytes) {
        bs->wr_highest_offset = offset + bytes;
    }

    if (ret >= 0) {
        bs->total_sectors = MAX(bs->total_sectors, sector_num + nb_sectors);
    }

    return ret;
}

static int coroutine_fn bdrv_co_do_zero_pwritev(BlockDriverState *bs,
                                                int64_t offset,
                                                unsigned int bytes,
                                                BdrvRequestFlags flags,
                                                BdrvTrackedRequest *req)
{
    uint8_t *buf = NULL;
    QEMUIOVector local_qiov;
    struct iovec iov;
    uint64_t align = MAX(BDRV_SECTOR_SIZE, bs->request_alignment);
    unsigned int head_padding_bytes, tail_padding_bytes;
    int ret = 0;

    head_padding_bytes = offset & (align - 1);
    tail_padding_bytes = align - ((offset + bytes) & (align - 1));


    assert(flags & BDRV_REQ_ZERO_WRITE);
    if (head_padding_bytes || tail_padding_bytes) {
        buf = qemu_blockalign(bs, align);
        iov = (struct iovec) {
            .iov_base   = buf,
            .iov_len    = align,
        };
        qemu_iovec_init_external(&local_qiov, &iov, 1);
    }
    if (head_padding_bytes) {
        uint64_t zero_bytes = MIN(bytes, align - head_padding_bytes);

        /* RMW the unaligned part before head. */
        mark_request_serialising(req, align);
        wait_serialising_requests(req);
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_HEAD);
        ret = bdrv_aligned_preadv(bs, req, offset & ~(align - 1), align,
                                  align, &local_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_HEAD);

        memset(buf + head_padding_bytes, 0, zero_bytes);
        ret = bdrv_aligned_pwritev(bs, req, offset & ~(align - 1), align,
                                   &local_qiov,
                                   flags & ~BDRV_REQ_ZERO_WRITE);
        if (ret < 0) {
            goto fail;
        }
        offset += zero_bytes;
        bytes -= zero_bytes;
    }

    assert(!bytes || (offset & (align - 1)) == 0);
    if (bytes >= align) {
        /* Write the aligned part in the middle. */
        uint64_t aligned_bytes = bytes & ~(align - 1);
        ret = bdrv_aligned_pwritev(bs, req, offset, aligned_bytes,
                                   NULL, flags);
        if (ret < 0) {
            goto fail;
        }
        bytes -= aligned_bytes;
        offset += aligned_bytes;
    }

    assert(!bytes || (offset & (align - 1)) == 0);
    if (bytes) {
        assert(align == tail_padding_bytes + bytes);
        /* RMW the unaligned part after tail. */
        mark_request_serialising(req, align);
        wait_serialising_requests(req);
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_TAIL);
        ret = bdrv_aligned_preadv(bs, req, offset, align,
                                  align, &local_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_TAIL);

        memset(buf, 0, bytes);
        ret = bdrv_aligned_pwritev(bs, req, offset, align,
                                   &local_qiov, flags & ~BDRV_REQ_ZERO_WRITE);
    }
fail:
    qemu_vfree(buf);
    return ret;

}

/*
 * Handle a write request in coroutine context
 */
int coroutine_fn bdrv_co_do_pwritev(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BdrvTrackedRequest req;
    /* TODO Lift BDRV_SECTOR_SIZE restriction in BlockDriver interface */
    uint64_t align = MAX(BDRV_SECTOR_SIZE, bs->request_alignment);
    uint8_t *head_buf = NULL;
    uint8_t *tail_buf = NULL;
    QEMUIOVector local_qiov;
    bool use_local_qiov = false;
    int ret;

    if (!bs->drv) {
        return -ENOMEDIUM;
    }
    if (bs->read_only) {
        return -EPERM;
    }
    assert(!(bs->open_flags & BDRV_O_INACTIVE));

    ret = bdrv_check_byte_request(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /* throttling disk I/O */
    if (bs->throttle_state) {
        throttle_group_co_io_limits_intercept(bs, bytes, true);
    }

    /*
     * Align write if necessary by performing a read-modify-write cycle.
     * Pad qiov with the read parts and be sure to have a tracked request not
     * only for bdrv_aligned_pwritev, but also for the reads of the RMW cycle.
     */
    tracked_request_begin(&req, bs, offset, bytes, BDRV_TRACKED_WRITE);

    if (!qiov) {
        ret = bdrv_co_do_zero_pwritev(bs, offset, bytes, flags, &req);
        goto out;
    }

    if (offset & (align - 1)) {
        QEMUIOVector head_qiov;
        struct iovec head_iov;

        mark_request_serialising(&req, align);
        wait_serialising_requests(&req);

        head_buf = qemu_blockalign(bs, align);
        head_iov = (struct iovec) {
            .iov_base   = head_buf,
            .iov_len    = align,
        };
        qemu_iovec_init_external(&head_qiov, &head_iov, 1);

        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_HEAD);
        ret = bdrv_aligned_preadv(bs, &req, offset & ~(align - 1), align,
                                  align, &head_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_HEAD);

        qemu_iovec_init(&local_qiov, qiov->niov + 2);
        qemu_iovec_add(&local_qiov, head_buf, offset & (align - 1));
        qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
        use_local_qiov = true;

        bytes += offset & (align - 1);
        offset = offset & ~(align - 1);
    }

    if ((offset + bytes) & (align - 1)) {
        QEMUIOVector tail_qiov;
        struct iovec tail_iov;
        size_t tail_bytes;
        bool waited;

        mark_request_serialising(&req, align);
        waited = wait_serialising_requests(&req);
        assert(!waited || !use_local_qiov);

        tail_buf = qemu_blockalign(bs, align);
        tail_iov = (struct iovec) {
            .iov_base   = tail_buf,
            .iov_len    = align,
        };
        qemu_iovec_init_external(&tail_qiov, &tail_iov, 1);

        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_TAIL);
        ret = bdrv_aligned_preadv(bs, &req, (offset + bytes) & ~(align - 1), align,
                                  align, &tail_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        bdrv_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_TAIL);

        if (!use_local_qiov) {
            qemu_iovec_init(&local_qiov, qiov->niov + 1);
            qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
            use_local_qiov = true;
        }

        tail_bytes = (offset + bytes) & (align - 1);
        qemu_iovec_add(&local_qiov, tail_buf + tail_bytes, align - tail_bytes);

        bytes = ROUND_UP(bytes, align);
    }

    ret = bdrv_aligned_pwritev(bs, &req, offset, bytes,
                               use_local_qiov ? &local_qiov : qiov,
                               flags);

fail:

    if (use_local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }
    qemu_vfree(head_buf);
    qemu_vfree(tail_buf);
out:
    tracked_request_end(&req);
    return ret;
}

static int coroutine_fn bdrv_co_do_writev(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    if (nb_sectors < 0 || nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return -EINVAL;
    }

    return bdrv_co_do_pwritev(bs, sector_num << BDRV_SECTOR_BITS,
                              nb_sectors << BDRV_SECTOR_BITS, qiov, flags);
}

int coroutine_fn bdrv_co_writev(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_writev(bs, sector_num, nb_sectors);

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, qiov, 0);
}

int coroutine_fn bdrv_co_write_zeroes(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors,
                                      BdrvRequestFlags flags)
{
    trace_bdrv_co_write_zeroes(bs, sector_num, nb_sectors, flags);

    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        flags &= ~BDRV_REQ_MAY_UNMAP;
    }

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, NULL,
                             BDRV_REQ_ZERO_WRITE | flags);
}

typedef struct BdrvCoGetBlockStatusData {
    BlockDriverState *bs;
    BlockDriverState *base;
    BlockDriverState **file;
    int64_t sector_num;
    int nb_sectors;
    int *pnum;
    int64_t ret;
    bool done;
} BdrvCoGetBlockStatusData;

/*
 * Returns the allocation status of the specified sectors.
 * Drivers not implementing the functionality are assumed to not support
 * backing files, hence all their sectors are reported as allocated.
 *
 * If 'sector_num' is beyond the end of the disk image the return value is 0
 * and 'pnum' is set to 0.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the specified sector) that are known to be in the same
 * allocated/unallocated state.
 *
 * 'nb_sectors' is the max value 'pnum' should be set to.  If nb_sectors goes
 * beyond the end of the disk image it will be clamped.
 *
 * If returned value is positive and BDRV_BLOCK_OFFSET_VALID bit is set, 'file'
 * points to the BDS which the sector range is allocated in.
 */
static int64_t coroutine_fn bdrv_co_get_block_status(BlockDriverState *bs,
                                                     int64_t sector_num,
                                                     int nb_sectors, int *pnum,
                                                     BlockDriverState **file)
{
    int64_t total_sectors;
    int64_t n;
    int64_t ret, ret2;

    total_sectors = bdrv_nb_sectors(bs);
    if (total_sectors < 0) {
        return total_sectors;
    }

    if (sector_num >= total_sectors) {
        *pnum = 0;
        return 0;
    }

    n = total_sectors - sector_num;
    if (n < nb_sectors) {
        nb_sectors = n;
    }

    if (!bs->drv->bdrv_co_get_block_status) {
        *pnum = nb_sectors;
        ret = BDRV_BLOCK_DATA | BDRV_BLOCK_ALLOCATED;
        if (bs->drv->protocol_name) {
            ret |= BDRV_BLOCK_OFFSET_VALID | (sector_num * BDRV_SECTOR_SIZE);
        }
        return ret;
    }

    *file = NULL;
    ret = bs->drv->bdrv_co_get_block_status(bs, sector_num, nb_sectors, pnum,
                                            file);
    if (ret < 0) {
        *pnum = 0;
        return ret;
    }

    if (ret & BDRV_BLOCK_RAW) {
        assert(ret & BDRV_BLOCK_OFFSET_VALID);
        return bdrv_get_block_status(bs->file->bs, ret >> BDRV_SECTOR_BITS,
                                     *pnum, pnum, file);
    }

    if (ret & (BDRV_BLOCK_DATA | BDRV_BLOCK_ZERO)) {
        ret |= BDRV_BLOCK_ALLOCATED;
    } else {
        if (bdrv_unallocated_blocks_are_zero(bs)) {
            ret |= BDRV_BLOCK_ZERO;
        } else if (bs->backing) {
            BlockDriverState *bs2 = bs->backing->bs;
            int64_t nb_sectors2 = bdrv_nb_sectors(bs2);
            if (nb_sectors2 >= 0 && sector_num >= nb_sectors2) {
                ret |= BDRV_BLOCK_ZERO;
            }
        }
    }

    if (*file && *file != bs &&
        (ret & BDRV_BLOCK_DATA) && !(ret & BDRV_BLOCK_ZERO) &&
        (ret & BDRV_BLOCK_OFFSET_VALID)) {
        BlockDriverState *file2;
        int file_pnum;

        ret2 = bdrv_co_get_block_status(*file, ret >> BDRV_SECTOR_BITS,
                                        *pnum, &file_pnum, &file2);
        if (ret2 >= 0) {
            /* Ignore errors.  This is just providing extra information, it
             * is useful but not necessary.
             */
            if (!file_pnum) {
                /* !file_pnum indicates an offset at or beyond the EOF; it is
                 * perfectly valid for the format block driver to point to such
                 * offsets, so catch it and mark everything as zero */
                ret |= BDRV_BLOCK_ZERO;
            } else {
                /* Limit request to the range reported by the protocol driver */
                *pnum = file_pnum;
                ret |= (ret2 & BDRV_BLOCK_ZERO);
            }
        }
    }

    return ret;
}

static int64_t coroutine_fn bdrv_co_get_block_status_above(BlockDriverState *bs,
        BlockDriverState *base,
        int64_t sector_num,
        int nb_sectors,
        int *pnum,
        BlockDriverState **file)
{
    BlockDriverState *p;
    int64_t ret = 0;

    assert(bs != base);
    for (p = bs; p != base; p = backing_bs(p)) {
        ret = bdrv_co_get_block_status(p, sector_num, nb_sectors, pnum, file);
        if (ret < 0 || ret & BDRV_BLOCK_ALLOCATED) {
            break;
        }
        /* [sector_num, pnum] unallocated on this layer, which could be only
         * the first part of [sector_num, nb_sectors].  */
        nb_sectors = MIN(nb_sectors, *pnum);
    }
    return ret;
}

/* Coroutine wrapper for bdrv_get_block_status_above() */
static void coroutine_fn bdrv_get_block_status_above_co_entry(void *opaque)
{
    BdrvCoGetBlockStatusData *data = opaque;

    data->ret = bdrv_co_get_block_status_above(data->bs, data->base,
                                               data->sector_num,
                                               data->nb_sectors,
                                               data->pnum,
                                               data->file);
    data->done = true;
}

/*
 * Synchronous wrapper around bdrv_co_get_block_status_above().
 *
 * See bdrv_co_get_block_status_above() for details.
 */
int64_t bdrv_get_block_status_above(BlockDriverState *bs,
                                    BlockDriverState *base,
                                    int64_t sector_num,
                                    int nb_sectors, int *pnum,
                                    BlockDriverState **file)
{
    Coroutine *co;
    BdrvCoGetBlockStatusData data = {
        .bs = bs,
        .base = base,
        .file = file,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .pnum = pnum,
        .done = false,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_get_block_status_above_co_entry(&data);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_get_block_status_above_co_entry);
        qemu_coroutine_enter(co, &data);
        while (!data.done) {
            aio_poll(aio_context, true);
        }
    }
    return data.ret;
}

int64_t bdrv_get_block_status(BlockDriverState *bs,
                              int64_t sector_num,
                              int nb_sectors, int *pnum,
                              BlockDriverState **file)
{
    return bdrv_get_block_status_above(bs, backing_bs(bs),
                                       sector_num, nb_sectors, pnum, file);
}

int coroutine_fn bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                   int nb_sectors, int *pnum)
{
    BlockDriverState *file;
    int64_t ret = bdrv_get_block_status(bs, sector_num, nb_sectors, pnum,
                                        &file);
    if (ret < 0) {
        return ret;
    }
    return !!(ret & BDRV_BLOCK_ALLOCATED);
}

/*
 * Given an image chain: ... -> [BASE] -> [INTER1] -> [INTER2] -> [TOP]
 *
 * Return true if the given sector is allocated in any image between
 * BASE and TOP (inclusive).  BASE can be NULL to check if the given
 * sector is allocated in any image of the chain.  Return false otherwise.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 *  the specified sector) that are known to be in the same
 *  allocated/unallocated state.
 *
 */
int bdrv_is_allocated_above(BlockDriverState *top,
                            BlockDriverState *base,
                            int64_t sector_num,
                            int nb_sectors, int *pnum)
{
    BlockDriverState *intermediate;
    int ret, n = nb_sectors;

    intermediate = top;
    while (intermediate && intermediate != base) {
        int pnum_inter;
        ret = bdrv_is_allocated(intermediate, sector_num, nb_sectors,
                                &pnum_inter);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            *pnum = pnum_inter;
            return 1;
        }

        /*
         * [sector_num, nb_sectors] is unallocated on top but intermediate
         * might have
         *
         * [sector_num+x, nr_sectors] allocated.
         */
        if (n > pnum_inter &&
            (intermediate == top ||
             sector_num + pnum_inter < intermediate->total_sectors)) {
            n = pnum_inter;
        }

        intermediate = backing_bs(intermediate);
    }

    *pnum = n;
    return 0;
}

int bdrv_write_compressed(BlockDriverState *bs, int64_t sector_num,
                          const uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    int ret;

    if (!drv) {
        return -ENOMEDIUM;
    }
    if (!drv->bdrv_write_compressed) {
        return -ENOTSUP;
    }
    ret = bdrv_check_request(bs, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    assert(QLIST_EMPTY(&bs->dirty_bitmaps));

    return drv->bdrv_write_compressed(bs, sector_num, buf, nb_sectors);
}

int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base   = (void *) buf,
        .iov_len    = size,
    };

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_writev_vmstate(bs, &qiov, pos);
}

int bdrv_writev_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos)
{
    BlockDriver *drv = bs->drv;

    if (!drv) {
        return -ENOMEDIUM;
    } else if (drv->bdrv_save_vmstate) {
        return drv->bdrv_save_vmstate(bs, qiov, pos);
    } else if (bs->file) {
        return bdrv_writev_vmstate(bs->file->bs, qiov, pos);
    }

    return -ENOTSUP;
}

int bdrv_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                      int64_t pos, int size)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_load_vmstate)
        return drv->bdrv_load_vmstate(bs, buf, pos, size);
    if (bs->file)
        return bdrv_load_vmstate(bs->file->bs, buf, pos, size);
    return -ENOTSUP;
}

/**************************************************************/
/* async I/Os */

BlockAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                           QEMUIOVector *qiov, int nb_sectors,
                           BlockCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_readv(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors, 0,
                                 cb, opaque, false);
}

BlockAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                            QEMUIOVector *qiov, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_writev(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors, 0,
                                 cb, opaque, true);
}

BlockAIOCB *bdrv_aio_write_zeroes(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, BdrvRequestFlags flags,
        BlockCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_write_zeroes(bs, sector_num, nb_sectors, flags, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, NULL, nb_sectors,
                                 BDRV_REQ_ZERO_WRITE | flags,
                                 cb, opaque, true);
}


typedef struct MultiwriteCB {
    int error;
    int num_requests;
    int num_callbacks;
    struct {
        BlockCompletionFunc *cb;
        void *opaque;
        QEMUIOVector *free_qiov;
    } callbacks[];
} MultiwriteCB;

static void multiwrite_user_cb(MultiwriteCB *mcb)
{
    int i;

    for (i = 0; i < mcb->num_callbacks; i++) {
        mcb->callbacks[i].cb(mcb->callbacks[i].opaque, mcb->error);
        if (mcb->callbacks[i].free_qiov) {
            qemu_iovec_destroy(mcb->callbacks[i].free_qiov);
        }
        g_free(mcb->callbacks[i].free_qiov);
    }
}

static void multiwrite_cb(void *opaque, int ret)
{
    MultiwriteCB *mcb = opaque;

    trace_multiwrite_cb(mcb, ret);

    if (ret < 0 && !mcb->error) {
        mcb->error = ret;
    }

    mcb->num_requests--;
    if (mcb->num_requests == 0) {
        multiwrite_user_cb(mcb);
        g_free(mcb);
    }
}

static int multiwrite_req_compare(const void *a, const void *b)
{
    const BlockRequest *req1 = a, *req2 = b;

    /*
     * Note that we can't simply subtract req2->sector from req1->sector
     * here as that could overflow the return value.
     */
    if (req1->sector > req2->sector) {
        return 1;
    } else if (req1->sector < req2->sector) {
        return -1;
    } else {
        return 0;
    }
}

/*
 * Takes a bunch of requests and tries to merge them. Returns the number of
 * requests that remain after merging.
 */
static int multiwrite_merge(BlockDriverState *bs, BlockRequest *reqs,
    int num_reqs, MultiwriteCB *mcb)
{
    int i, outidx;

    // Sort requests by start sector
    qsort(reqs, num_reqs, sizeof(*reqs), &multiwrite_req_compare);

    // Check if adjacent requests touch the same clusters. If so, combine them,
    // filling up gaps with zero sectors.
    outidx = 0;
    for (i = 1; i < num_reqs; i++) {
        int merge = 0;
        int64_t oldreq_last = reqs[outidx].sector + reqs[outidx].nb_sectors;

        // Handle exactly sequential writes and overlapping writes.
        if (reqs[i].sector <= oldreq_last) {
            merge = 1;
        }

        if (reqs[outidx].qiov->niov + reqs[i].qiov->niov + 1 >
            bs->bl.max_iov) {
            merge = 0;
        }

        if (bs->bl.max_transfer_length && reqs[outidx].nb_sectors +
            reqs[i].nb_sectors > bs->bl.max_transfer_length) {
            merge = 0;
        }

        if (merge) {
            size_t size;
            QEMUIOVector *qiov = g_malloc0(sizeof(*qiov));
            qemu_iovec_init(qiov,
                reqs[outidx].qiov->niov + reqs[i].qiov->niov + 1);

            // Add the first request to the merged one. If the requests are
            // overlapping, drop the last sectors of the first request.
            size = (reqs[i].sector - reqs[outidx].sector) << 9;
            qemu_iovec_concat(qiov, reqs[outidx].qiov, 0, size);

            // We should need to add any zeros between the two requests
            assert (reqs[i].sector <= oldreq_last);

            // Add the second request
            qemu_iovec_concat(qiov, reqs[i].qiov, 0, reqs[i].qiov->size);

            // Add tail of first request, if necessary
            if (qiov->size < reqs[outidx].qiov->size) {
                qemu_iovec_concat(qiov, reqs[outidx].qiov, qiov->size,
                                  reqs[outidx].qiov->size - qiov->size);
            }

            reqs[outidx].nb_sectors = qiov->size >> 9;
            reqs[outidx].qiov = qiov;

            mcb->callbacks[i].free_qiov = reqs[outidx].qiov;
        } else {
            outidx++;
            reqs[outidx].sector     = reqs[i].sector;
            reqs[outidx].nb_sectors = reqs[i].nb_sectors;
            reqs[outidx].qiov       = reqs[i].qiov;
        }
    }

    if (bs->blk) {
        block_acct_merge_done(blk_get_stats(bs->blk), BLOCK_ACCT_WRITE,
                              num_reqs - outidx - 1);
    }

    return outidx + 1;
}

/*
 * Submit multiple AIO write requests at once.
 *
 * On success, the function returns 0 and all requests in the reqs array have
 * been submitted. In error case this function returns -1, and any of the
 * requests may or may not be submitted yet. In particular, this means that the
 * callback will be called for some of the requests, for others it won't. The
 * caller must check the error field of the BlockRequest to wait for the right
 * callbacks (if error != 0, no callback will be called).
 *
 * The implementation may modify the contents of the reqs array, e.g. to merge
 * requests. However, the fields opaque and error are left unmodified as they
 * are used to signal failure for a single request to the caller.
 */
int bdrv_aio_multiwrite(BlockDriverState *bs, BlockRequest *reqs, int num_reqs)
{
    MultiwriteCB *mcb;
    int i;

    /* don't submit writes if we don't have a medium */
    if (bs->drv == NULL) {
        for (i = 0; i < num_reqs; i++) {
            reqs[i].error = -ENOMEDIUM;
        }
        return -1;
    }

    if (num_reqs == 0) {
        return 0;
    }

    // Create MultiwriteCB structure
    mcb = g_malloc0(sizeof(*mcb) + num_reqs * sizeof(*mcb->callbacks));
    mcb->num_requests = 0;
    mcb->num_callbacks = num_reqs;

    for (i = 0; i < num_reqs; i++) {
        mcb->callbacks[i].cb = reqs[i].cb;
        mcb->callbacks[i].opaque = reqs[i].opaque;
    }

    // Check for mergable requests
    num_reqs = multiwrite_merge(bs, reqs, num_reqs, mcb);

    trace_bdrv_aio_multiwrite(mcb, mcb->num_callbacks, num_reqs);

    /* Run the aio requests. */
    mcb->num_requests = num_reqs;
    for (i = 0; i < num_reqs; i++) {
        bdrv_co_aio_rw_vector(bs, reqs[i].sector, reqs[i].qiov,
                              reqs[i].nb_sectors, reqs[i].flags,
                              multiwrite_cb, mcb,
                              true);
    }

    return 0;
}

void bdrv_aio_cancel(BlockAIOCB *acb)
{
    qemu_aio_ref(acb);
    bdrv_aio_cancel_async(acb);
    while (acb->refcnt > 1) {
        if (acb->aiocb_info->get_aio_context) {
            aio_poll(acb->aiocb_info->get_aio_context(acb), true);
        } else if (acb->bs) {
            aio_poll(bdrv_get_aio_context(acb->bs), true);
        } else {
            abort();
        }
    }
    qemu_aio_unref(acb);
}

/* Async version of aio cancel. The caller is not blocked if the acb implements
 * cancel_async, otherwise we do nothing and let the request normally complete.
 * In either case the completion callback must be called. */
void bdrv_aio_cancel_async(BlockAIOCB *acb)
{
    if (acb->aiocb_info->cancel_async) {
        acb->aiocb_info->cancel_async(acb);
    }
}

/**************************************************************/
/* async block device emulation */

typedef struct BlockAIOCBSync {
    BlockAIOCB common;
    QEMUBH *bh;
    int ret;
    /* vector translation state */
    QEMUIOVector *qiov;
    uint8_t *bounce;
    int is_write;
} BlockAIOCBSync;

static const AIOCBInfo bdrv_em_aiocb_info = {
    .aiocb_size         = sizeof(BlockAIOCBSync),
};

static void bdrv_aio_bh_cb(void *opaque)
{
    BlockAIOCBSync *acb = opaque;

    if (!acb->is_write && acb->ret >= 0) {
        qemu_iovec_from_buf(acb->qiov, 0, acb->bounce, acb->qiov->size);
    }
    qemu_vfree(acb->bounce);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    qemu_aio_unref(acb);
}

static BlockAIOCB *bdrv_aio_rw_vector(BlockDriverState *bs,
                                      int64_t sector_num,
                                      QEMUIOVector *qiov,
                                      int nb_sectors,
                                      BlockCompletionFunc *cb,
                                      void *opaque,
                                      int is_write)

{
    BlockAIOCBSync *acb;

    acb = qemu_aio_get(&bdrv_em_aiocb_info, bs, cb, opaque);
    acb->is_write = is_write;
    acb->qiov = qiov;
    acb->bounce = qemu_try_blockalign(bs, qiov->size);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_aio_bh_cb, acb);

    if (acb->bounce == NULL) {
        acb->ret = -ENOMEM;
    } else if (is_write) {
        qemu_iovec_to_buf(acb->qiov, 0, acb->bounce, qiov->size);
        acb->ret = bs->drv->bdrv_write(bs, sector_num, acb->bounce, nb_sectors);
    } else {
        acb->ret = bs->drv->bdrv_read(bs, sector_num, acb->bounce, nb_sectors);
    }

    qemu_bh_schedule(acb->bh);

    return &acb->common;
}

static BlockAIOCB *bdrv_aio_readv_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 0);
}

static BlockAIOCB *bdrv_aio_writev_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 1);
}


typedef struct BlockAIOCBCoroutine {
    BlockAIOCB common;
    BlockRequest req;
    bool is_write;
    bool need_bh;
    bool *done;
    QEMUBH* bh;
} BlockAIOCBCoroutine;

static const AIOCBInfo bdrv_em_co_aiocb_info = {
    .aiocb_size         = sizeof(BlockAIOCBCoroutine),
};

static void bdrv_co_complete(BlockAIOCBCoroutine *acb)
{
    if (!acb->need_bh) {
        acb->common.cb(acb->common.opaque, acb->req.error);
        qemu_aio_unref(acb);
    }
}

static void bdrv_co_em_bh(void *opaque)
{
    BlockAIOCBCoroutine *acb = opaque;

    assert(!acb->need_bh);
    qemu_bh_delete(acb->bh);
    bdrv_co_complete(acb);
}

static void bdrv_co_maybe_schedule_bh(BlockAIOCBCoroutine *acb)
{
    acb->need_bh = false;
    if (acb->req.error != -EINPROGRESS) {
        BlockDriverState *bs = acb->common.bs;

        acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_co_em_bh, acb);
        qemu_bh_schedule(acb->bh);
    }
}

/* Invoke bdrv_co_do_readv/bdrv_co_do_writev */
static void coroutine_fn bdrv_co_do_rw(void *opaque)
{
    BlockAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    if (!acb->is_write) {
        acb->req.error = bdrv_co_do_readv(bs, acb->req.sector,
            acb->req.nb_sectors, acb->req.qiov, acb->req.flags);
    } else {
        acb->req.error = bdrv_co_do_writev(bs, acb->req.sector,
            acb->req.nb_sectors, acb->req.qiov, acb->req.flags);
    }

    bdrv_co_complete(acb);
}

static BlockAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                         int64_t sector_num,
                                         QEMUIOVector *qiov,
                                         int nb_sectors,
                                         BdrvRequestFlags flags,
                                         BlockCompletionFunc *cb,
                                         void *opaque,
                                         bool is_write)
{
    Coroutine *co;
    BlockAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->need_bh = true;
    acb->req.error = -EINPROGRESS;
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
    acb->req.qiov = qiov;
    acb->req.flags = flags;
    acb->is_write = is_write;

    co = qemu_coroutine_create(bdrv_co_do_rw);
    qemu_coroutine_enter(co, acb);

    bdrv_co_maybe_schedule_bh(acb);
    return &acb->common;
}

static void coroutine_fn bdrv_aio_flush_co_entry(void *opaque)
{
    BlockAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_flush(bs);
    bdrv_co_complete(acb);
}

BlockAIOCB *bdrv_aio_flush(BlockDriverState *bs,
        BlockCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_flush(bs, opaque);

    Coroutine *co;
    BlockAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->need_bh = true;
    acb->req.error = -EINPROGRESS;

    co = qemu_coroutine_create(bdrv_aio_flush_co_entry);
    qemu_coroutine_enter(co, acb);

    bdrv_co_maybe_schedule_bh(acb);
    return &acb->common;
}

static void coroutine_fn bdrv_aio_discard_co_entry(void *opaque)
{
    BlockAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_discard(bs, acb->req.sector, acb->req.nb_sectors);
    bdrv_co_complete(acb);
}

BlockAIOCB *bdrv_aio_discard(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque)
{
    Coroutine *co;
    BlockAIOCBCoroutine *acb;

    trace_bdrv_aio_discard(bs, sector_num, nb_sectors, opaque);

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->need_bh = true;
    acb->req.error = -EINPROGRESS;
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
    co = qemu_coroutine_create(bdrv_aio_discard_co_entry);
    qemu_coroutine_enter(co, acb);

    bdrv_co_maybe_schedule_bh(acb);
    return &acb->common;
}

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockCompletionFunc *cb, void *opaque)
{
    BlockAIOCB *acb;

    acb = g_malloc(aiocb_info->aiocb_size);
    acb->aiocb_info = aiocb_info;
    acb->bs = bs;
    acb->cb = cb;
    acb->opaque = opaque;
    acb->refcnt = 1;
    return acb;
}

void qemu_aio_ref(void *p)
{
    BlockAIOCB *acb = p;
    acb->refcnt++;
}

void qemu_aio_unref(void *p)
{
    BlockAIOCB *acb = p;
    assert(acb->refcnt > 0);
    if (--acb->refcnt == 0) {
        g_free(acb);
    }
}

/**************************************************************/
/* Coroutine block device emulation */

static void coroutine_fn bdrv_flush_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    rwco->ret = bdrv_co_flush(rwco->bs);
}

int coroutine_fn bdrv_co_flush(BlockDriverState *bs)
{
    int ret;
    BdrvTrackedRequest req;

    if (!bs || !bdrv_is_inserted(bs) || bdrv_is_read_only(bs) ||
        bdrv_is_sg(bs)) {
        return 0;
    }

    tracked_request_begin(&req, bs, 0, 0, BDRV_TRACKED_FLUSH);

    /* Write back all layers by calling one driver function */
    if (bs->drv->bdrv_co_flush) {
        ret = bs->drv->bdrv_co_flush(bs);
        goto out;
    }

    /* Write back cached data to the OS even with cache=unsafe */
    BLKDBG_EVENT(bs->file, BLKDBG_FLUSH_TO_OS);
    if (bs->drv->bdrv_co_flush_to_os) {
        ret = bs->drv->bdrv_co_flush_to_os(bs);
        if (ret < 0) {
            goto out;
        }
    }

    /* But don't actually force it to the disk with cache=unsafe */
    if (bs->open_flags & BDRV_O_NO_FLUSH) {
        goto flush_parent;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_FLUSH_TO_DISK);
    if (bs->drv->bdrv_co_flush_to_disk) {
        ret = bs->drv->bdrv_co_flush_to_disk(bs);
    } else if (bs->drv->bdrv_aio_flush) {
        BlockAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = bs->drv->bdrv_aio_flush(bs, bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            ret = -EIO;
        } else {
            qemu_coroutine_yield();
            ret = co.ret;
        }
    } else {
        /*
         * Some block drivers always operate in either writethrough or unsafe
         * mode and don't support bdrv_flush therefore. Usually qemu doesn't
         * know how the server works (because the behaviour is hardcoded or
         * depends on server-side configuration), so we can't ensure that
         * everything is safe on disk. Returning an error doesn't work because
         * that would break guests even if the server operates in writethrough
         * mode.
         *
         * Let's hope the user knows what he's doing.
         */
        ret = 0;
    }
    if (ret < 0) {
        goto out;
    }

    /* Now flush the underlying protocol.  It will also have BDRV_O_NO_FLUSH
     * in the case of cache=unsafe, so there are no useless flushes.
     */
flush_parent:
    ret = bs->file ? bdrv_co_flush(bs->file->bs) : 0;
out:
    tracked_request_end(&req);
    return ret;
}

int bdrv_flush(BlockDriverState *bs)
{
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .ret = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_flush_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_flush_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }

    return rwco.ret;
}

typedef struct DiscardCo {
    BlockDriverState *bs;
    int64_t sector_num;
    int nb_sectors;
    int ret;
} DiscardCo;
static void coroutine_fn bdrv_discard_co_entry(void *opaque)
{
    DiscardCo *rwco = opaque;

    rwco->ret = bdrv_co_discard(rwco->bs, rwco->sector_num, rwco->nb_sectors);
}

int coroutine_fn bdrv_co_discard(BlockDriverState *bs, int64_t sector_num,
                                 int nb_sectors)
{
    BdrvTrackedRequest req;
    int max_discard, ret;

    if (!bs->drv) {
        return -ENOMEDIUM;
    }

    ret = bdrv_check_request(bs, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    } else if (bs->read_only) {
        return -EPERM;
    }
    assert(!(bs->open_flags & BDRV_O_INACTIVE));

    /* Do nothing if disabled.  */
    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        return 0;
    }

    if (!bs->drv->bdrv_co_discard && !bs->drv->bdrv_aio_discard) {
        return 0;
    }

    tracked_request_begin(&req, bs, sector_num, nb_sectors,
                          BDRV_TRACKED_DISCARD);
    bdrv_set_dirty(bs, sector_num, nb_sectors);

    max_discard = MIN_NON_ZERO(bs->bl.max_discard, BDRV_REQUEST_MAX_SECTORS);
    while (nb_sectors > 0) {
        int ret;
        int num = nb_sectors;

        /* align request */
        if (bs->bl.discard_alignment &&
            num >= bs->bl.discard_alignment &&
            sector_num % bs->bl.discard_alignment) {
            if (num > bs->bl.discard_alignment) {
                num = bs->bl.discard_alignment;
            }
            num -= sector_num % bs->bl.discard_alignment;
        }

        /* limit request size */
        if (num > max_discard) {
            num = max_discard;
        }

        if (bs->drv->bdrv_co_discard) {
            ret = bs->drv->bdrv_co_discard(bs, sector_num, num);
        } else {
            BlockAIOCB *acb;
            CoroutineIOCompletion co = {
                .coroutine = qemu_coroutine_self(),
            };

            acb = bs->drv->bdrv_aio_discard(bs, sector_num, nb_sectors,
                                            bdrv_co_io_em_complete, &co);
            if (acb == NULL) {
                ret = -EIO;
                goto out;
            } else {
                qemu_coroutine_yield();
                ret = co.ret;
            }
        }
        if (ret && ret != -ENOTSUP) {
            goto out;
        }

        sector_num += num;
        nb_sectors -= num;
    }
    ret = 0;
out:
    tracked_request_end(&req);
    return ret;
}

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors)
{
    Coroutine *co;
    DiscardCo rwco = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .ret = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_discard_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_discard_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }

    return rwco.ret;
}

typedef struct {
    CoroutineIOCompletion *co;
    QEMUBH *bh;
} BdrvIoctlCompletionData;

static void bdrv_ioctl_bh_cb(void *opaque)
{
    BdrvIoctlCompletionData *data = opaque;

    bdrv_co_io_em_complete(data->co, -ENOTSUP);
    qemu_bh_delete(data->bh);
}

static int bdrv_co_do_ioctl(BlockDriverState *bs, int req, void *buf)
{
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest tracked_req;
    CoroutineIOCompletion co = {
        .coroutine = qemu_coroutine_self(),
    };
    BlockAIOCB *acb;

    tracked_request_begin(&tracked_req, bs, 0, 0, BDRV_TRACKED_IOCTL);
    if (!drv || !drv->bdrv_aio_ioctl) {
        co.ret = -ENOTSUP;
        goto out;
    }

    acb = drv->bdrv_aio_ioctl(bs, req, buf, bdrv_co_io_em_complete, &co);
    if (!acb) {
        BdrvIoctlCompletionData *data = g_new(BdrvIoctlCompletionData, 1);
        data->bh = aio_bh_new(bdrv_get_aio_context(bs),
                                bdrv_ioctl_bh_cb, data);
        data->co = &co;
        qemu_bh_schedule(data->bh);
    }
    qemu_coroutine_yield();
out:
    tracked_request_end(&tracked_req);
    return co.ret;
}

typedef struct {
    BlockDriverState *bs;
    int req;
    void *buf;
    int ret;
} BdrvIoctlCoData;

static void coroutine_fn bdrv_co_ioctl_entry(void *opaque)
{
    BdrvIoctlCoData *data = opaque;
    data->ret = bdrv_co_do_ioctl(data->bs, data->req, data->buf);
}

/* needed for generic scsi interface */
int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BdrvIoctlCoData data = {
        .bs = bs,
        .req = req,
        .buf = buf,
        .ret = -EINPROGRESS,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_co_ioctl_entry(&data);
    } else {
        Coroutine *co = qemu_coroutine_create(bdrv_co_ioctl_entry);

        qemu_coroutine_enter(co, &data);
        while (data.ret == -EINPROGRESS) {
            aio_poll(bdrv_get_aio_context(bs), true);
        }
    }
    return data.ret;
}

static void coroutine_fn bdrv_co_aio_ioctl_entry(void *opaque)
{
    BlockAIOCBCoroutine *acb = opaque;
    acb->req.error = bdrv_co_do_ioctl(acb->common.bs,
                                      acb->req.req, acb->req.buf);
    bdrv_co_complete(acb);
}

BlockAIOCB *bdrv_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque)
{
    BlockAIOCBCoroutine *acb = qemu_aio_get(&bdrv_em_co_aiocb_info,
                                            bs, cb, opaque);
    Coroutine *co;

    acb->need_bh = true;
    acb->req.error = -EINPROGRESS;
    acb->req.req = req;
    acb->req.buf = buf;
    co = qemu_coroutine_create(bdrv_co_aio_ioctl_entry);
    qemu_coroutine_enter(co, acb);

    bdrv_co_maybe_schedule_bh(acb);
    return &acb->common;
}

void *qemu_blockalign(BlockDriverState *bs, size_t size)
{
    return qemu_memalign(bdrv_opt_mem_align(bs), size);
}

void *qemu_blockalign0(BlockDriverState *bs, size_t size)
{
    return memset(qemu_blockalign(bs, size), 0, size);
}

void *qemu_try_blockalign(BlockDriverState *bs, size_t size)
{
    size_t align = bdrv_opt_mem_align(bs);

    /* Ensure that NULL is never returned on success */
    assert(align > 0);
    if (size == 0) {
        size = align;
    }

    return qemu_try_memalign(align, size);
}

void *qemu_try_blockalign0(BlockDriverState *bs, size_t size)
{
    void *mem = qemu_try_blockalign(bs, size);

    if (mem) {
        memset(mem, 0, size);
    }

    return mem;
}

/*
 * Check if all memory in this vector is sector aligned.
 */
bool bdrv_qiov_is_aligned(BlockDriverState *bs, QEMUIOVector *qiov)
{
    int i;
    size_t alignment = bdrv_min_mem_align(bs);

    for (i = 0; i < qiov->niov; i++) {
        if ((uintptr_t) qiov->iov[i].iov_base % alignment) {
            return false;
        }
        if (qiov->iov[i].iov_len % alignment) {
            return false;
        }
    }

    return true;
}

void bdrv_add_before_write_notifier(BlockDriverState *bs,
                                    NotifierWithReturn *notifier)
{
    notifier_with_return_list_add(&bs->before_write_notifiers, notifier);
}

void bdrv_io_plug(BlockDriverState *bs)
{
    BdrvChild *child;

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_io_plug(child->bs);
    }

    if (bs->io_plugged++ == 0 && bs->io_plug_disabled == 0) {
        BlockDriver *drv = bs->drv;
        if (drv && drv->bdrv_io_plug) {
            drv->bdrv_io_plug(bs);
        }
    }
}

void bdrv_io_unplug(BlockDriverState *bs)
{
    BdrvChild *child;

    assert(bs->io_plugged);
    if (--bs->io_plugged == 0 && bs->io_plug_disabled == 0) {
        BlockDriver *drv = bs->drv;
        if (drv && drv->bdrv_io_unplug) {
            drv->bdrv_io_unplug(bs);
        }
    }

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_io_unplug(child->bs);
    }
}

void bdrv_io_unplugged_begin(BlockDriverState *bs)
{
    BdrvChild *child;

    if (bs->io_plug_disabled++ == 0 && bs->io_plugged > 0) {
        BlockDriver *drv = bs->drv;
        if (drv && drv->bdrv_io_unplug) {
            drv->bdrv_io_unplug(bs);
        }
    }

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_io_unplugged_begin(child->bs);
    }
}

void bdrv_io_unplugged_end(BlockDriverState *bs)
{
    BdrvChild *child;

    assert(bs->io_plug_disabled);
    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_io_unplugged_end(child->bs);
    }

    if (--bs->io_plug_disabled == 0 && bs->io_plugged > 0) {
        BlockDriver *drv = bs->drv;
        if (drv && drv->bdrv_io_plug) {
            drv->bdrv_io_plug(bs);
        }
    }
}

void bdrv_drained_begin(BlockDriverState *bs)
{
    if (!bs->quiesce_counter++) {
        aio_disable_external(bdrv_get_aio_context(bs));
    }
    bdrv_drain(bs);
}

void bdrv_drained_end(BlockDriverState *bs)
{
    assert(bs->quiesce_counter > 0);
    if (--bs->quiesce_counter > 0) {
        return;
    }
    aio_enable_external(bdrv_get_aio_context(bs));
}
