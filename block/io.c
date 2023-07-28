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
#include "block/aio-wait.h"
#include "block/blockjob.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "block/coroutines.h"
#include "block/dirty-bitmap.h"
#include "block/write-threshold.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/replay.h"

/* Maximum bounce buffer for copy-on-read and write zeroes, in bytes */
#define MAX_BOUNCE_BUFFER (32768 << BDRV_SECTOR_BITS)

static void bdrv_parent_cb_resize(BlockDriverState *bs);
static int coroutine_fn bdrv_co_do_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int64_t bytes, BdrvRequestFlags flags);

static void bdrv_parent_drained_begin(BlockDriverState *bs, BdrvChild *ignore)
{
    BdrvChild *c, *next;

    QLIST_FOREACH_SAFE(c, &bs->parents, next_parent, next) {
        if (c == ignore) {
            continue;
        }
        bdrv_parent_drained_begin_single(c);
    }
}

void bdrv_parent_drained_end_single(BdrvChild *c)
{
    GLOBAL_STATE_CODE();

    assert(c->quiesced_parent);
    c->quiesced_parent = false;

    if (c->klass->drained_end) {
        c->klass->drained_end(c);
    }
}

static void bdrv_parent_drained_end(BlockDriverState *bs, BdrvChild *ignore)
{
    BdrvChild *c;

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c == ignore) {
            continue;
        }
        bdrv_parent_drained_end_single(c);
    }
}

bool bdrv_parent_drained_poll_single(BdrvChild *c)
{
    if (c->klass->drained_poll) {
        return c->klass->drained_poll(c);
    }
    return false;
}

static bool bdrv_parent_drained_poll(BlockDriverState *bs, BdrvChild *ignore,
                                     bool ignore_bds_parents)
{
    BdrvChild *c, *next;
    bool busy = false;

    QLIST_FOREACH_SAFE(c, &bs->parents, next_parent, next) {
        if (c == ignore || (ignore_bds_parents && c->klass->parent_is_bds)) {
            continue;
        }
        busy |= bdrv_parent_drained_poll_single(c);
    }

    return busy;
}

void bdrv_parent_drained_begin_single(BdrvChild *c)
{
    GLOBAL_STATE_CODE();

    assert(!c->quiesced_parent);
    c->quiesced_parent = true;

    if (c->klass->drained_begin) {
        c->klass->drained_begin(c);
    }
}

static void bdrv_merge_limits(BlockLimits *dst, const BlockLimits *src)
{
    dst->pdiscard_alignment = MAX(dst->pdiscard_alignment,
                                  src->pdiscard_alignment);
    dst->opt_transfer = MAX(dst->opt_transfer, src->opt_transfer);
    dst->max_transfer = MIN_NON_ZERO(dst->max_transfer, src->max_transfer);
    dst->max_hw_transfer = MIN_NON_ZERO(dst->max_hw_transfer,
                                        src->max_hw_transfer);
    dst->opt_mem_alignment = MAX(dst->opt_mem_alignment,
                                 src->opt_mem_alignment);
    dst->min_mem_alignment = MAX(dst->min_mem_alignment,
                                 src->min_mem_alignment);
    dst->max_iov = MIN_NON_ZERO(dst->max_iov, src->max_iov);
    dst->max_hw_iov = MIN_NON_ZERO(dst->max_hw_iov, src->max_hw_iov);
}

typedef struct BdrvRefreshLimitsState {
    BlockDriverState *bs;
    BlockLimits old_bl;
} BdrvRefreshLimitsState;

static void bdrv_refresh_limits_abort(void *opaque)
{
    BdrvRefreshLimitsState *s = opaque;

    s->bs->bl = s->old_bl;
}

static TransactionActionDrv bdrv_refresh_limits_drv = {
    .abort = bdrv_refresh_limits_abort,
    .clean = g_free,
};

/* @tran is allowed to be NULL, in this case no rollback is possible. */
void bdrv_refresh_limits(BlockDriverState *bs, Transaction *tran, Error **errp)
{
    ERRP_GUARD();
    BlockDriver *drv = bs->drv;
    BdrvChild *c;
    bool have_limits;

    GLOBAL_STATE_CODE();

    if (tran) {
        BdrvRefreshLimitsState *s = g_new(BdrvRefreshLimitsState, 1);
        *s = (BdrvRefreshLimitsState) {
            .bs = bs,
            .old_bl = bs->bl,
        };
        tran_add(tran, &bdrv_refresh_limits_drv, s);
    }

    memset(&bs->bl, 0, sizeof(bs->bl));

    if (!drv) {
        return;
    }

    /* Default alignment based on whether driver has byte interface */
    bs->bl.request_alignment = (drv->bdrv_co_preadv ||
                                drv->bdrv_aio_preadv ||
                                drv->bdrv_co_preadv_part) ? 1 : 512;

    /* Take some limits from the children as a default */
    have_limits = false;
    QLIST_FOREACH(c, &bs->children, next) {
        if (c->role & (BDRV_CHILD_DATA | BDRV_CHILD_FILTERED | BDRV_CHILD_COW))
        {
            bdrv_merge_limits(&bs->bl, &c->bs->bl);
            have_limits = true;
        }

        if (c->role & BDRV_CHILD_FILTERED) {
            bs->bl.has_variable_length |= c->bs->bl.has_variable_length;
        }
    }

    if (!have_limits) {
        bs->bl.min_mem_alignment = 512;
        bs->bl.opt_mem_alignment = qemu_real_host_page_size();

        /* Safe default since most protocols use readv()/writev()/etc */
        bs->bl.max_iov = IOV_MAX;
    }

    /* Then let the driver override it */
    if (drv->bdrv_refresh_limits) {
        drv->bdrv_refresh_limits(bs, errp);
        if (*errp) {
            return;
        }
    }

    if (bs->bl.request_alignment > BDRV_MAX_ALIGNMENT) {
        error_setg(errp, "Driver requires too large request alignment");
    }
}

/**
 * The copy-on-read flag is actually a reference count so multiple users may
 * use the feature without worrying about clobbering its previous state.
 * Copy-on-read stays enabled until all users have called to disable it.
 */
void bdrv_enable_copy_on_read(BlockDriverState *bs)
{
    IO_CODE();
    qatomic_inc(&bs->copy_on_read);
}

void bdrv_disable_copy_on_read(BlockDriverState *bs)
{
    int old = qatomic_fetch_dec(&bs->copy_on_read);
    IO_CODE();
    assert(old >= 1);
}

typedef struct {
    Coroutine *co;
    BlockDriverState *bs;
    bool done;
    bool begin;
    bool poll;
    BdrvChild *parent;
} BdrvCoDrainData;

/* Returns true if BDRV_POLL_WHILE() should go into a blocking aio_poll() */
bool bdrv_drain_poll(BlockDriverState *bs, BdrvChild *ignore_parent,
                     bool ignore_bds_parents)
{
    GLOBAL_STATE_CODE();

    if (bdrv_parent_drained_poll(bs, ignore_parent, ignore_bds_parents)) {
        return true;
    }

    if (qatomic_read(&bs->in_flight)) {
        return true;
    }

    return false;
}

static bool bdrv_drain_poll_top_level(BlockDriverState *bs,
                                      BdrvChild *ignore_parent)
{
    return bdrv_drain_poll(bs, ignore_parent, false);
}

static void bdrv_do_drained_begin(BlockDriverState *bs, BdrvChild *parent,
                                  bool poll);
static void bdrv_do_drained_end(BlockDriverState *bs, BdrvChild *parent);

static void bdrv_co_drain_bh_cb(void *opaque)
{
    BdrvCoDrainData *data = opaque;
    Coroutine *co = data->co;
    BlockDriverState *bs = data->bs;

    if (bs) {
        AioContext *ctx = bdrv_get_aio_context(bs);
        aio_context_acquire(ctx);
        bdrv_dec_in_flight(bs);
        if (data->begin) {
            bdrv_do_drained_begin(bs, data->parent, data->poll);
        } else {
            assert(!data->poll);
            bdrv_do_drained_end(bs, data->parent);
        }
        aio_context_release(ctx);
    } else {
        assert(data->begin);
        bdrv_drain_all_begin();
    }

    data->done = true;
    aio_co_wake(co);
}

static void coroutine_fn bdrv_co_yield_to_drain(BlockDriverState *bs,
                                                bool begin,
                                                BdrvChild *parent,
                                                bool poll)
{
    BdrvCoDrainData data;
    Coroutine *self = qemu_coroutine_self();
    AioContext *ctx = bdrv_get_aio_context(bs);
    AioContext *co_ctx = qemu_coroutine_get_aio_context(self);

    /* Calling bdrv_drain() from a BH ensures the current coroutine yields and
     * other coroutines run if they were queued by aio_co_enter(). */

    assert(qemu_in_coroutine());
    data = (BdrvCoDrainData) {
        .co = self,
        .bs = bs,
        .done = false,
        .begin = begin,
        .parent = parent,
        .poll = poll,
    };

    if (bs) {
        bdrv_inc_in_flight(bs);
    }

    /*
     * Temporarily drop the lock across yield or we would get deadlocks.
     * bdrv_co_drain_bh_cb() reaquires the lock as needed.
     *
     * When we yield below, the lock for the current context will be
     * released, so if this is actually the lock that protects bs, don't drop
     * it a second time.
     */
    if (ctx != co_ctx) {
        aio_context_release(ctx);
    }
    replay_bh_schedule_oneshot_event(qemu_get_aio_context(),
                                     bdrv_co_drain_bh_cb, &data);

    qemu_coroutine_yield();
    /* If we are resumed from some other event (such as an aio completion or a
     * timer callback), it is a bug in the caller that should be fixed. */
    assert(data.done);

    /* Reaquire the AioContext of bs if we dropped it */
    if (ctx != co_ctx) {
        aio_context_acquire(ctx);
    }
}

static void bdrv_do_drained_begin(BlockDriverState *bs, BdrvChild *parent,
                                  bool poll)
{
    IO_OR_GS_CODE();

    if (qemu_in_coroutine()) {
        bdrv_co_yield_to_drain(bs, true, parent, poll);
        return;
    }

    GLOBAL_STATE_CODE();

    /* Stop things in parent-to-child order */
    if (qatomic_fetch_inc(&bs->quiesce_counter) == 0) {
        bdrv_parent_drained_begin(bs, parent);
        if (bs->drv && bs->drv->bdrv_drain_begin) {
            bs->drv->bdrv_drain_begin(bs);
        }
    }

    /*
     * Wait for drained requests to finish.
     *
     * Calling BDRV_POLL_WHILE() only once for the top-level node is okay: The
     * call is needed so things in this AioContext can make progress even
     * though we don't return to the main AioContext loop - this automatically
     * includes other nodes in the same AioContext and therefore all child
     * nodes.
     */
    if (poll) {
        BDRV_POLL_WHILE(bs, bdrv_drain_poll_top_level(bs, parent));
    }
}

void bdrv_do_drained_begin_quiesce(BlockDriverState *bs, BdrvChild *parent)
{
    bdrv_do_drained_begin(bs, parent, false);
}

void bdrv_drained_begin(BlockDriverState *bs)
{
    IO_OR_GS_CODE();
    bdrv_do_drained_begin(bs, NULL, true);
}

/**
 * This function does not poll, nor must any of its recursively called
 * functions.
 */
static void bdrv_do_drained_end(BlockDriverState *bs, BdrvChild *parent)
{
    int old_quiesce_counter;

    IO_OR_GS_CODE();

    if (qemu_in_coroutine()) {
        bdrv_co_yield_to_drain(bs, false, parent, false);
        return;
    }
    assert(bs->quiesce_counter > 0);
    GLOBAL_STATE_CODE();

    /* Re-enable things in child-to-parent order */
    old_quiesce_counter = qatomic_fetch_dec(&bs->quiesce_counter);
    if (old_quiesce_counter == 1) {
        if (bs->drv && bs->drv->bdrv_drain_end) {
            bs->drv->bdrv_drain_end(bs);
        }
        bdrv_parent_drained_end(bs, parent);
    }
}

void bdrv_drained_end(BlockDriverState *bs)
{
    IO_OR_GS_CODE();
    bdrv_do_drained_end(bs, NULL);
}

void bdrv_drain(BlockDriverState *bs)
{
    IO_OR_GS_CODE();
    bdrv_drained_begin(bs);
    bdrv_drained_end(bs);
}

static void bdrv_drain_assert_idle(BlockDriverState *bs)
{
    BdrvChild *child, *next;

    assert(qatomic_read(&bs->in_flight) == 0);
    QLIST_FOREACH_SAFE(child, &bs->children, next, next) {
        bdrv_drain_assert_idle(child->bs);
    }
}

unsigned int bdrv_drain_all_count = 0;

static bool bdrv_drain_all_poll(void)
{
    BlockDriverState *bs = NULL;
    bool result = false;
    GLOBAL_STATE_CODE();

    /* bdrv_drain_poll() can't make changes to the graph and we are holding the
     * main AioContext lock, so iterating bdrv_next_all_states() is safe. */
    while ((bs = bdrv_next_all_states(bs))) {
        AioContext *aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);
        result |= bdrv_drain_poll(bs, NULL, true);
        aio_context_release(aio_context);
    }

    return result;
}

/*
 * Wait for pending requests to complete across all BlockDriverStates
 *
 * This function does not flush data to disk, use bdrv_flush_all() for that
 * after calling this function.
 *
 * This pauses all block jobs and disables external clients. It must
 * be paired with bdrv_drain_all_end().
 *
 * NOTE: no new block jobs or BlockDriverStates can be created between
 * the bdrv_drain_all_begin() and bdrv_drain_all_end() calls.
 */
void bdrv_drain_all_begin_nopoll(void)
{
    BlockDriverState *bs = NULL;
    GLOBAL_STATE_CODE();

    /*
     * bdrv queue is managed by record/replay,
     * waiting for finishing the I/O requests may
     * be infinite
     */
    if (replay_events_enabled()) {
        return;
    }

    /* AIO_WAIT_WHILE() with a NULL context can only be called from the main
     * loop AioContext, so make sure we're in the main context. */
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    assert(bdrv_drain_all_count < INT_MAX);
    bdrv_drain_all_count++;

    /* Quiesce all nodes, without polling in-flight requests yet. The graph
     * cannot change during this loop. */
    while ((bs = bdrv_next_all_states(bs))) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_do_drained_begin(bs, NULL, false);
        aio_context_release(aio_context);
    }
}

void bdrv_drain_all_begin(void)
{
    BlockDriverState *bs = NULL;

    if (qemu_in_coroutine()) {
        bdrv_co_yield_to_drain(NULL, true, NULL, true);
        return;
    }

    /*
     * bdrv queue is managed by record/replay,
     * waiting for finishing the I/O requests may
     * be infinite
     */
    if (replay_events_enabled()) {
        return;
    }

    bdrv_drain_all_begin_nopoll();

    /* Now poll the in-flight requests */
    AIO_WAIT_WHILE_UNLOCKED(NULL, bdrv_drain_all_poll());

    while ((bs = bdrv_next_all_states(bs))) {
        bdrv_drain_assert_idle(bs);
    }
}

void bdrv_drain_all_end_quiesce(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();

    g_assert(bs->quiesce_counter > 0);
    g_assert(!bs->refcnt);

    while (bs->quiesce_counter) {
        bdrv_do_drained_end(bs, NULL);
    }
}

void bdrv_drain_all_end(void)
{
    BlockDriverState *bs = NULL;
    GLOBAL_STATE_CODE();

    /*
     * bdrv queue is managed by record/replay,
     * waiting for finishing the I/O requests may
     * be endless
     */
    if (replay_events_enabled()) {
        return;
    }

    while ((bs = bdrv_next_all_states(bs))) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_do_drained_end(bs, NULL);
        aio_context_release(aio_context);
    }

    assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    assert(bdrv_drain_all_count > 0);
    bdrv_drain_all_count--;
}

void bdrv_drain_all(void)
{
    GLOBAL_STATE_CODE();
    bdrv_drain_all_begin();
    bdrv_drain_all_end();
}

/**
 * Remove an active request from the tracked requests list
 *
 * This function should be called when a tracked request is completing.
 */
static void coroutine_fn tracked_request_end(BdrvTrackedRequest *req)
{
    if (req->serialising) {
        qatomic_dec(&req->bs->serialising_in_flight);
    }

    qemu_co_mutex_lock(&req->bs->reqs_lock);
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
    qemu_co_mutex_unlock(&req->bs->reqs_lock);
}

/**
 * Add an active request to the tracked requests list
 */
static void coroutine_fn tracked_request_begin(BdrvTrackedRequest *req,
                                               BlockDriverState *bs,
                                               int64_t offset,
                                               int64_t bytes,
                                               enum BdrvTrackedRequestType type)
{
    bdrv_check_request(offset, bytes, &error_abort);

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

    qemu_co_mutex_lock(&bs->reqs_lock);
    QLIST_INSERT_HEAD(&bs->tracked_requests, req, list);
    qemu_co_mutex_unlock(&bs->reqs_lock);
}

static bool tracked_request_overlaps(BdrvTrackedRequest *req,
                                     int64_t offset, int64_t bytes)
{
    bdrv_check_request(offset, bytes, &error_abort);

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

/* Called with self->bs->reqs_lock held */
static coroutine_fn BdrvTrackedRequest *
bdrv_find_conflicting_request(BdrvTrackedRequest *self)
{
    BdrvTrackedRequest *req;

    QLIST_FOREACH(req, &self->bs->tracked_requests, list) {
        if (req == self || (!req->serialising && !self->serialising)) {
            continue;
        }
        if (tracked_request_overlaps(req, self->overlap_offset,
                                     self->overlap_bytes))
        {
            /*
             * Hitting this means there was a reentrant request, for
             * example, a block driver issuing nested requests.  This must
             * never happen since it means deadlock.
             */
            assert(qemu_coroutine_self() != req->co);

            /*
             * If the request is already (indirectly) waiting for us, or
             * will wait for us as soon as it wakes up, then just go on
             * (instead of producing a deadlock in the former case).
             */
            if (!req->waiting_for) {
                return req;
            }
        }
    }

    return NULL;
}

/* Called with self->bs->reqs_lock held */
static void coroutine_fn
bdrv_wait_serialising_requests_locked(BdrvTrackedRequest *self)
{
    BdrvTrackedRequest *req;

    while ((req = bdrv_find_conflicting_request(self))) {
        self->waiting_for = req;
        qemu_co_queue_wait(&req->wait_queue, &self->bs->reqs_lock);
        self->waiting_for = NULL;
    }
}

/* Called with req->bs->reqs_lock held */
static void tracked_request_set_serialising(BdrvTrackedRequest *req,
                                            uint64_t align)
{
    int64_t overlap_offset = req->offset & ~(align - 1);
    int64_t overlap_bytes =
        ROUND_UP(req->offset + req->bytes, align) - overlap_offset;

    bdrv_check_request(req->offset, req->bytes, &error_abort);

    if (!req->serialising) {
        qatomic_inc(&req->bs->serialising_in_flight);
        req->serialising = true;
    }

    req->overlap_offset = MIN(req->overlap_offset, overlap_offset);
    req->overlap_bytes = MAX(req->overlap_bytes, overlap_bytes);
}

/**
 * Return the tracked request on @bs for the current coroutine, or
 * NULL if there is none.
 */
BdrvTrackedRequest *coroutine_fn bdrv_co_get_self_request(BlockDriverState *bs)
{
    BdrvTrackedRequest *req;
    Coroutine *self = qemu_coroutine_self();
    IO_CODE();

    QLIST_FOREACH(req, &bs->tracked_requests, list) {
        if (req->co == self) {
            return req;
        }
    }

    return NULL;
}

/**
 * Round a region to cluster boundaries
 */
void coroutine_fn GRAPH_RDLOCK
bdrv_round_to_clusters(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       int64_t *cluster_offset, int64_t *cluster_bytes)
{
    BlockDriverInfo bdi;
    IO_CODE();
    if (bdrv_co_get_info(bs, &bdi) < 0 || bdi.cluster_size == 0) {
        *cluster_offset = offset;
        *cluster_bytes = bytes;
    } else {
        int64_t c = bdi.cluster_size;
        *cluster_offset = QEMU_ALIGN_DOWN(offset, c);
        *cluster_bytes = QEMU_ALIGN_UP(offset - *cluster_offset + bytes, c);
    }
}

static int coroutine_fn GRAPH_RDLOCK bdrv_get_cluster_size(BlockDriverState *bs)
{
    BlockDriverInfo bdi;
    int ret;

    ret = bdrv_co_get_info(bs, &bdi);
    if (ret < 0 || bdi.cluster_size == 0) {
        return bs->bl.request_alignment;
    } else {
        return bdi.cluster_size;
    }
}

void bdrv_inc_in_flight(BlockDriverState *bs)
{
    IO_CODE();
    qatomic_inc(&bs->in_flight);
}

void bdrv_wakeup(BlockDriverState *bs)
{
    IO_CODE();
    aio_wait_kick();
}

void bdrv_dec_in_flight(BlockDriverState *bs)
{
    IO_CODE();
    qatomic_dec(&bs->in_flight);
    bdrv_wakeup(bs);
}

static void coroutine_fn
bdrv_wait_serialising_requests(BdrvTrackedRequest *self)
{
    BlockDriverState *bs = self->bs;

    if (!qatomic_read(&bs->serialising_in_flight)) {
        return;
    }

    qemu_co_mutex_lock(&bs->reqs_lock);
    bdrv_wait_serialising_requests_locked(self);
    qemu_co_mutex_unlock(&bs->reqs_lock);
}

void coroutine_fn bdrv_make_request_serialising(BdrvTrackedRequest *req,
                                                uint64_t align)
{
    IO_CODE();

    qemu_co_mutex_lock(&req->bs->reqs_lock);

    tracked_request_set_serialising(req, align);
    bdrv_wait_serialising_requests_locked(req);

    qemu_co_mutex_unlock(&req->bs->reqs_lock);
}

int bdrv_check_qiov_request(int64_t offset, int64_t bytes,
                            QEMUIOVector *qiov, size_t qiov_offset,
                            Error **errp)
{
    /*
     * Check generic offset/bytes correctness
     */

    if (offset < 0) {
        error_setg(errp, "offset is negative: %" PRIi64, offset);
        return -EIO;
    }

    if (bytes < 0) {
        error_setg(errp, "bytes is negative: %" PRIi64, bytes);
        return -EIO;
    }

    if (bytes > BDRV_MAX_LENGTH) {
        error_setg(errp, "bytes(%" PRIi64 ") exceeds maximum(%" PRIi64 ")",
                   bytes, BDRV_MAX_LENGTH);
        return -EIO;
    }

    if (offset > BDRV_MAX_LENGTH) {
        error_setg(errp, "offset(%" PRIi64 ") exceeds maximum(%" PRIi64 ")",
                   offset, BDRV_MAX_LENGTH);
        return -EIO;
    }

    if (offset > BDRV_MAX_LENGTH - bytes) {
        error_setg(errp, "sum of offset(%" PRIi64 ") and bytes(%" PRIi64 ") "
                   "exceeds maximum(%" PRIi64 ")", offset, bytes,
                   BDRV_MAX_LENGTH);
        return -EIO;
    }

    if (!qiov) {
        return 0;
    }

    /*
     * Check qiov and qiov_offset
     */

    if (qiov_offset > qiov->size) {
        error_setg(errp, "qiov_offset(%zu) overflow io vector size(%zu)",
                   qiov_offset, qiov->size);
        return -EIO;
    }

    if (bytes > qiov->size - qiov_offset) {
        error_setg(errp, "bytes(%" PRIi64 ") + qiov_offset(%zu) overflow io "
                   "vector size(%zu)", bytes, qiov_offset, qiov->size);
        return -EIO;
    }

    return 0;
}

int bdrv_check_request(int64_t offset, int64_t bytes, Error **errp)
{
    return bdrv_check_qiov_request(offset, bytes, NULL, 0, errp);
}

static int bdrv_check_request32(int64_t offset, int64_t bytes,
                                QEMUIOVector *qiov, size_t qiov_offset)
{
    int ret = bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, NULL);
    if (ret < 0) {
        return ret;
    }

    if (bytes > BDRV_REQUEST_MAX_BYTES) {
        return -EIO;
    }

    return 0;
}

/*
 * Completely zero out a block device with the help of bdrv_pwrite_zeroes.
 * The operation is sped up by checking the block status and only writing
 * zeroes to the device if they currently do not return zeroes. Optional
 * flags are passed through to bdrv_pwrite_zeroes (e.g. BDRV_REQ_MAY_UNMAP,
 * BDRV_REQ_FUA).
 *
 * Returns < 0 on error, 0 on success. For error codes see bdrv_pwrite().
 */
int bdrv_make_zero(BdrvChild *child, BdrvRequestFlags flags)
{
    int ret;
    int64_t target_size, bytes, offset = 0;
    BlockDriverState *bs = child->bs;
    IO_CODE();

    target_size = bdrv_getlength(bs);
    if (target_size < 0) {
        return target_size;
    }

    for (;;) {
        bytes = MIN(target_size - offset, BDRV_REQUEST_MAX_BYTES);
        if (bytes <= 0) {
            return 0;
        }
        ret = bdrv_block_status(bs, offset, bytes, &bytes, NULL, NULL);
        if (ret < 0) {
            return ret;
        }
        if (ret & BDRV_BLOCK_ZERO) {
            offset += bytes;
            continue;
        }
        ret = bdrv_pwrite_zeroes(child, offset, bytes, flags);
        if (ret < 0) {
            return ret;
        }
        offset += bytes;
    }
}

/*
 * Writes to the file and ensures that no writes are reordered across this
 * request (acts as a barrier)
 *
 * Returns 0 on success, -errno in error cases.
 */
int coroutine_fn bdrv_co_pwrite_sync(BdrvChild *child, int64_t offset,
                                     int64_t bytes, const void *buf,
                                     BdrvRequestFlags flags)
{
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    ret = bdrv_co_pwrite(child, offset, bytes, buf, flags);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_co_flush(child->bs);
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
    aio_co_wake(co->coroutine);
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_driver_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                   QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    BlockDriver *drv = bs->drv;
    int64_t sector_num;
    unsigned int nb_sectors;
    QEMUIOVector local_qiov;
    int ret;
    assert_bdrv_graph_readable();

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);
    assert(!(flags & ~bs->supported_read_flags));

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (drv->bdrv_co_preadv_part) {
        return drv->bdrv_co_preadv_part(bs, offset, bytes, qiov, qiov_offset,
                                        flags);
    }

    if (qiov_offset > 0 || bytes != qiov->size) {
        qemu_iovec_init_slice(&local_qiov, qiov, qiov_offset, bytes);
        qiov = &local_qiov;
    }

    if (drv->bdrv_co_preadv) {
        ret = drv->bdrv_co_preadv(bs, offset, bytes, qiov, flags);
        goto out;
    }

    if (drv->bdrv_aio_preadv) {
        BlockAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = drv->bdrv_aio_preadv(bs, offset, bytes, qiov, flags,
                                   bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            ret = -EIO;
            goto out;
        } else {
            qemu_coroutine_yield();
            ret = co.ret;
            goto out;
        }
    }

    sector_num = offset >> BDRV_SECTOR_BITS;
    nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert(QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(bytes, BDRV_SECTOR_SIZE));
    assert(bytes <= BDRV_REQUEST_MAX_BYTES);
    assert(drv->bdrv_co_readv);

    ret = drv->bdrv_co_readv(bs, sector_num, nb_sectors, qiov);

out:
    if (qiov == &local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_driver_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                    QEMUIOVector *qiov, size_t qiov_offset,
                    BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    bool emulate_fua = false;
    int64_t sector_num;
    unsigned int nb_sectors;
    QEMUIOVector local_qiov;
    int ret;
    assert_bdrv_graph_readable();

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);

    if (!drv) {
        return -ENOMEDIUM;
    }

    if ((flags & BDRV_REQ_FUA) &&
        (~bs->supported_write_flags & BDRV_REQ_FUA)) {
        flags &= ~BDRV_REQ_FUA;
        emulate_fua = true;
    }

    flags &= bs->supported_write_flags;

    if (drv->bdrv_co_pwritev_part) {
        ret = drv->bdrv_co_pwritev_part(bs, offset, bytes, qiov, qiov_offset,
                                        flags);
        goto emulate_flags;
    }

    if (qiov_offset > 0 || bytes != qiov->size) {
        qemu_iovec_init_slice(&local_qiov, qiov, qiov_offset, bytes);
        qiov = &local_qiov;
    }

    if (drv->bdrv_co_pwritev) {
        ret = drv->bdrv_co_pwritev(bs, offset, bytes, qiov, flags);
        goto emulate_flags;
    }

    if (drv->bdrv_aio_pwritev) {
        BlockAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = drv->bdrv_aio_pwritev(bs, offset, bytes, qiov, flags,
                                    bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            ret = -EIO;
        } else {
            qemu_coroutine_yield();
            ret = co.ret;
        }
        goto emulate_flags;
    }

    sector_num = offset >> BDRV_SECTOR_BITS;
    nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert(QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(bytes, BDRV_SECTOR_SIZE));
    assert(bytes <= BDRV_REQUEST_MAX_BYTES);

    assert(drv->bdrv_co_writev);
    ret = drv->bdrv_co_writev(bs, sector_num, nb_sectors, qiov, flags);

emulate_flags:
    if (ret == 0 && emulate_fua) {
        ret = bdrv_co_flush(bs);
    }

    if (qiov == &local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_driver_pwritev_compressed(BlockDriverState *bs, int64_t offset,
                               int64_t bytes, QEMUIOVector *qiov,
                               size_t qiov_offset)
{
    BlockDriver *drv = bs->drv;
    QEMUIOVector local_qiov;
    int ret;
    assert_bdrv_graph_readable();

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (!block_driver_can_compress(drv)) {
        return -ENOTSUP;
    }

    if (drv->bdrv_co_pwritev_compressed_part) {
        return drv->bdrv_co_pwritev_compressed_part(bs, offset, bytes,
                                                    qiov, qiov_offset);
    }

    if (qiov_offset == 0) {
        return drv->bdrv_co_pwritev_compressed(bs, offset, bytes, qiov);
    }

    qemu_iovec_init_slice(&local_qiov, qiov, qiov_offset, bytes);
    ret = drv->bdrv_co_pwritev_compressed(bs, offset, bytes, &local_qiov);
    qemu_iovec_destroy(&local_qiov);

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_co_do_copy_on_readv(BdrvChild *child, int64_t offset, int64_t bytes,
                         QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    BlockDriverState *bs = child->bs;

    /* Perform I/O through a temporary buffer so that users who scribble over
     * their read buffer while the operation is in progress do not end up
     * modifying the image file.  This is critical for zero-copy guest I/O
     * where anything might happen inside guest memory.
     */
    void *bounce_buffer = NULL;

    BlockDriver *drv = bs->drv;
    int64_t cluster_offset;
    int64_t cluster_bytes;
    int64_t skip_bytes;
    int ret;
    int max_transfer = MIN_NON_ZERO(bs->bl.max_transfer,
                                    BDRV_REQUEST_MAX_BYTES);
    int64_t progress = 0;
    bool skip_write;

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);

    if (!drv) {
        return -ENOMEDIUM;
    }

    /*
     * Do not write anything when the BDS is inactive.  That is not
     * allowed, and it would not help.
     */
    skip_write = (bs->open_flags & BDRV_O_INACTIVE);

    /* FIXME We cannot require callers to have write permissions when all they
     * are doing is a read request. If we did things right, write permissions
     * would be obtained anyway, but internally by the copy-on-read code. As
     * long as it is implemented here rather than in a separate filter driver,
     * the copy-on-read code doesn't have its own BdrvChild, however, for which
     * it could request permissions. Therefore we have to bypass the permission
     * system for the moment. */
    // assert(child->perm & (BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE));

    /* Cover entire cluster so no additional backing file I/O is required when
     * allocating cluster in the image file.  Note that this value may exceed
     * BDRV_REQUEST_MAX_BYTES (even when the original read did not), which
     * is one reason we loop rather than doing it all at once.
     */
    bdrv_round_to_clusters(bs, offset, bytes, &cluster_offset, &cluster_bytes);
    skip_bytes = offset - cluster_offset;

    trace_bdrv_co_do_copy_on_readv(bs, offset, bytes,
                                   cluster_offset, cluster_bytes);

    while (cluster_bytes) {
        int64_t pnum;

        if (skip_write) {
            ret = 1; /* "already allocated", so nothing will be copied */
            pnum = MIN(cluster_bytes, max_transfer);
        } else {
            ret = bdrv_is_allocated(bs, cluster_offset,
                                    MIN(cluster_bytes, max_transfer), &pnum);
            if (ret < 0) {
                /*
                 * Safe to treat errors in querying allocation as if
                 * unallocated; we'll probably fail again soon on the
                 * read, but at least that will set a decent errno.
                 */
                pnum = MIN(cluster_bytes, max_transfer);
            }

            /* Stop at EOF if the image ends in the middle of the cluster */
            if (ret == 0 && pnum == 0) {
                assert(progress >= bytes);
                break;
            }

            assert(skip_bytes < pnum);
        }

        if (ret <= 0) {
            QEMUIOVector local_qiov;

            /* Must copy-on-read; use the bounce buffer */
            pnum = MIN(pnum, MAX_BOUNCE_BUFFER);
            if (!bounce_buffer) {
                int64_t max_we_need = MAX(pnum, cluster_bytes - pnum);
                int64_t max_allowed = MIN(max_transfer, MAX_BOUNCE_BUFFER);
                int64_t bounce_buffer_len = MIN(max_we_need, max_allowed);

                bounce_buffer = qemu_try_blockalign(bs, bounce_buffer_len);
                if (!bounce_buffer) {
                    ret = -ENOMEM;
                    goto err;
                }
            }
            qemu_iovec_init_buf(&local_qiov, bounce_buffer, pnum);

            ret = bdrv_driver_preadv(bs, cluster_offset, pnum,
                                     &local_qiov, 0, 0);
            if (ret < 0) {
                goto err;
            }

            bdrv_co_debug_event(bs, BLKDBG_COR_WRITE);
            if (drv->bdrv_co_pwrite_zeroes &&
                buffer_is_zero(bounce_buffer, pnum)) {
                /* FIXME: Should we (perhaps conditionally) be setting
                 * BDRV_REQ_MAY_UNMAP, if it will allow for a sparser copy
                 * that still correctly reads as zero? */
                ret = bdrv_co_do_pwrite_zeroes(bs, cluster_offset, pnum,
                                               BDRV_REQ_WRITE_UNCHANGED);
            } else {
                /* This does not change the data on the disk, it is not
                 * necessary to flush even in cache=writethrough mode.
                 */
                ret = bdrv_driver_pwritev(bs, cluster_offset, pnum,
                                          &local_qiov, 0,
                                          BDRV_REQ_WRITE_UNCHANGED);
            }

            if (ret < 0) {
                /* It might be okay to ignore write errors for guest
                 * requests.  If this is a deliberate copy-on-read
                 * then we don't want to ignore the error.  Simply
                 * report it in all cases.
                 */
                goto err;
            }

            if (!(flags & BDRV_REQ_PREFETCH)) {
                qemu_iovec_from_buf(qiov, qiov_offset + progress,
                                    bounce_buffer + skip_bytes,
                                    MIN(pnum - skip_bytes, bytes - progress));
            }
        } else if (!(flags & BDRV_REQ_PREFETCH)) {
            /* Read directly into the destination */
            ret = bdrv_driver_preadv(bs, offset + progress,
                                     MIN(pnum - skip_bytes, bytes - progress),
                                     qiov, qiov_offset + progress, 0);
            if (ret < 0) {
                goto err;
            }
        }

        cluster_offset += pnum;
        cluster_bytes -= pnum;
        progress += pnum - skip_bytes;
        skip_bytes = 0;
    }
    ret = 0;

err:
    qemu_vfree(bounce_buffer);
    return ret;
}

/*
 * Forwards an already correctly aligned request to the BlockDriver. This
 * handles copy on read, zeroing after EOF, and fragmentation of large
 * reads; any other features must be implemented by the caller.
 */
static int coroutine_fn GRAPH_RDLOCK
bdrv_aligned_preadv(BdrvChild *child, BdrvTrackedRequest *req,
                    int64_t offset, int64_t bytes, int64_t align,
                    QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    BlockDriverState *bs = child->bs;
    int64_t total_bytes, max_bytes;
    int ret = 0;
    int64_t bytes_remaining = bytes;
    int max_transfer;

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);
    assert(is_power_of_2(align));
    assert((offset & (align - 1)) == 0);
    assert((bytes & (align - 1)) == 0);
    assert((bs->open_flags & BDRV_O_NO_IO) == 0);
    max_transfer = QEMU_ALIGN_DOWN(MIN_NON_ZERO(bs->bl.max_transfer, INT_MAX),
                                   align);

    /*
     * TODO: We would need a per-BDS .supported_read_flags and
     * potential fallback support, if we ever implement any read flags
     * to pass through to drivers.  For now, there aren't any
     * passthrough flags except the BDRV_REQ_REGISTERED_BUF optimization hint.
     */
    assert(!(flags & ~(BDRV_REQ_COPY_ON_READ | BDRV_REQ_PREFETCH |
                       BDRV_REQ_REGISTERED_BUF)));

    /* Handle Copy on Read and associated serialisation */
    if (flags & BDRV_REQ_COPY_ON_READ) {
        /* If we touch the same cluster it counts as an overlap.  This
         * guarantees that allocating writes will be serialized and not race
         * with each other for the same cluster.  For example, in copy-on-read
         * it ensures that the CoR read and write operations are atomic and
         * guest writes cannot interleave between them. */
        bdrv_make_request_serialising(req, bdrv_get_cluster_size(bs));
    } else {
        bdrv_wait_serialising_requests(req);
    }

    if (flags & BDRV_REQ_COPY_ON_READ) {
        int64_t pnum;

        /* The flag BDRV_REQ_COPY_ON_READ has reached its addressee */
        flags &= ~BDRV_REQ_COPY_ON_READ;

        ret = bdrv_is_allocated(bs, offset, bytes, &pnum);
        if (ret < 0) {
            goto out;
        }

        if (!ret || pnum != bytes) {
            ret = bdrv_co_do_copy_on_readv(child, offset, bytes,
                                           qiov, qiov_offset, flags);
            goto out;
        } else if (flags & BDRV_REQ_PREFETCH) {
            goto out;
        }
    }

    /* Forward the request to the BlockDriver, possibly fragmenting it */
    total_bytes = bdrv_co_getlength(bs);
    if (total_bytes < 0) {
        ret = total_bytes;
        goto out;
    }

    assert(!(flags & ~(bs->supported_read_flags | BDRV_REQ_REGISTERED_BUF)));

    max_bytes = ROUND_UP(MAX(0, total_bytes - offset), align);
    if (bytes <= max_bytes && bytes <= max_transfer) {
        ret = bdrv_driver_preadv(bs, offset, bytes, qiov, qiov_offset, flags);
        goto out;
    }

    while (bytes_remaining) {
        int64_t num;

        if (max_bytes) {
            num = MIN(bytes_remaining, MIN(max_bytes, max_transfer));
            assert(num);

            ret = bdrv_driver_preadv(bs, offset + bytes - bytes_remaining,
                                     num, qiov,
                                     qiov_offset + bytes - bytes_remaining,
                                     flags);
            max_bytes -= num;
        } else {
            num = bytes_remaining;
            ret = qemu_iovec_memset(qiov, qiov_offset + bytes - bytes_remaining,
                                    0, bytes_remaining);
        }
        if (ret < 0) {
            goto out;
        }
        bytes_remaining -= num;
    }

out:
    return ret < 0 ? ret : 0;
}

/*
 * Request padding
 *
 *  |<---- align ----->|                     |<----- align ---->|
 *  |<- head ->|<------------- bytes ------------->|<-- tail -->|
 *  |          |       |                     |     |            |
 * -*----------$-------*-------- ... --------*-----$------------*---
 *  |          |       |                     |     |            |
 *  |          offset  |                     |     end          |
 *  ALIGN_DOWN(offset) ALIGN_UP(offset)      ALIGN_DOWN(end)   ALIGN_UP(end)
 *  [buf   ... )                             [tail_buf          )
 *
 * @buf is an aligned allocation needed to store @head and @tail paddings. @head
 * is placed at the beginning of @buf and @tail at the @end.
 *
 * @tail_buf is a pointer to sub-buffer, corresponding to align-sized chunk
 * around tail, if tail exists.
 *
 * @merge_reads is true for small requests,
 * if @buf_len == @head + bytes + @tail. In this case it is possible that both
 * head and tail exist but @buf_len == align and @tail_buf == @buf.
 *
 * @write is true for write requests, false for read requests.
 *
 * If padding makes the vector too long (exceeding IOV_MAX), then we need to
 * merge existing vector elements into a single one.  @collapse_bounce_buf acts
 * as the bounce buffer in such cases.  @pre_collapse_qiov has the pre-collapse
 * I/O vector elements so for read requests, the data can be copied back after
 * the read is done.
 */
typedef struct BdrvRequestPadding {
    uint8_t *buf;
    size_t buf_len;
    uint8_t *tail_buf;
    size_t head;
    size_t tail;
    bool merge_reads;
    bool write;
    QEMUIOVector local_qiov;

    uint8_t *collapse_bounce_buf;
    size_t collapse_len;
    QEMUIOVector pre_collapse_qiov;
} BdrvRequestPadding;

static bool bdrv_init_padding(BlockDriverState *bs,
                              int64_t offset, int64_t bytes,
                              bool write,
                              BdrvRequestPadding *pad)
{
    int64_t align = bs->bl.request_alignment;
    int64_t sum;

    bdrv_check_request(offset, bytes, &error_abort);
    assert(align <= INT_MAX); /* documented in block/block_int.h */
    assert(align <= SIZE_MAX / 2); /* so we can allocate the buffer */

    memset(pad, 0, sizeof(*pad));

    pad->head = offset & (align - 1);
    pad->tail = ((offset + bytes) & (align - 1));
    if (pad->tail) {
        pad->tail = align - pad->tail;
    }

    if (!pad->head && !pad->tail) {
        return false;
    }

    assert(bytes); /* Nothing good in aligning zero-length requests */

    sum = pad->head + bytes + pad->tail;
    pad->buf_len = (sum > align && pad->head && pad->tail) ? 2 * align : align;
    pad->buf = qemu_blockalign(bs, pad->buf_len);
    pad->merge_reads = sum == pad->buf_len;
    if (pad->tail) {
        pad->tail_buf = pad->buf + pad->buf_len - align;
    }

    pad->write = write;

    return true;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_padding_rmw_read(BdrvChild *child, BdrvTrackedRequest *req,
                      BdrvRequestPadding *pad, bool zero_middle)
{
    QEMUIOVector local_qiov;
    BlockDriverState *bs = child->bs;
    uint64_t align = bs->bl.request_alignment;
    int ret;

    assert(req->serialising && pad->buf);

    if (pad->head || pad->merge_reads) {
        int64_t bytes = pad->merge_reads ? pad->buf_len : align;

        qemu_iovec_init_buf(&local_qiov, pad->buf, bytes);

        if (pad->head) {
            bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_HEAD);
        }
        if (pad->merge_reads && pad->tail) {
            bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_TAIL);
        }
        ret = bdrv_aligned_preadv(child, req, req->overlap_offset, bytes,
                                  align, &local_qiov, 0, 0);
        if (ret < 0) {
            return ret;
        }
        if (pad->head) {
            bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_HEAD);
        }
        if (pad->merge_reads && pad->tail) {
            bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_TAIL);
        }

        if (pad->merge_reads) {
            goto zero_mem;
        }
    }

    if (pad->tail) {
        qemu_iovec_init_buf(&local_qiov, pad->tail_buf, align);

        bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_TAIL);
        ret = bdrv_aligned_preadv(
                child, req,
                req->overlap_offset + req->overlap_bytes - align,
                align, align, &local_qiov, 0, 0);
        if (ret < 0) {
            return ret;
        }
        bdrv_co_debug_event(bs, BLKDBG_PWRITEV_RMW_AFTER_TAIL);
    }

zero_mem:
    if (zero_middle) {
        memset(pad->buf + pad->head, 0, pad->buf_len - pad->head - pad->tail);
    }

    return 0;
}

/**
 * Free *pad's associated buffers, and perform any necessary finalization steps.
 */
static void bdrv_padding_finalize(BdrvRequestPadding *pad)
{
    if (pad->collapse_bounce_buf) {
        if (!pad->write) {
            /*
             * If padding required elements in the vector to be collapsed into a
             * bounce buffer, copy the bounce buffer content back
             */
            qemu_iovec_from_buf(&pad->pre_collapse_qiov, 0,
                                pad->collapse_bounce_buf, pad->collapse_len);
        }
        qemu_vfree(pad->collapse_bounce_buf);
        qemu_iovec_destroy(&pad->pre_collapse_qiov);
    }
    if (pad->buf) {
        qemu_vfree(pad->buf);
        qemu_iovec_destroy(&pad->local_qiov);
    }
    memset(pad, 0, sizeof(*pad));
}

/*
 * Create pad->local_qiov by wrapping @iov in the padding head and tail, while
 * ensuring that the resulting vector will not exceed IOV_MAX elements.
 *
 * To ensure this, when necessary, the first two or three elements of @iov are
 * merged into pad->collapse_bounce_buf and replaced by a reference to that
 * bounce buffer in pad->local_qiov.
 *
 * After performing a read request, the data from the bounce buffer must be
 * copied back into pad->pre_collapse_qiov (e.g. by bdrv_padding_finalize()).
 */
static int bdrv_create_padded_qiov(BlockDriverState *bs,
                                   BdrvRequestPadding *pad,
                                   struct iovec *iov, int niov,
                                   size_t iov_offset, size_t bytes)
{
    int padded_niov, surplus_count, collapse_count;

    /* Assert this invariant */
    assert(niov <= IOV_MAX);

    /*
     * Cannot pad if resulting length would exceed SIZE_MAX.  Returning an error
     * to the guest is not ideal, but there is little else we can do.  At least
     * this will practically never happen on 64-bit systems.
     */
    if (SIZE_MAX - pad->head < bytes ||
        SIZE_MAX - pad->head - bytes < pad->tail)
    {
        return -EINVAL;
    }

    /* Length of the resulting IOV if we just concatenated everything */
    padded_niov = !!pad->head + niov + !!pad->tail;

    qemu_iovec_init(&pad->local_qiov, MIN(padded_niov, IOV_MAX));

    if (pad->head) {
        qemu_iovec_add(&pad->local_qiov, pad->buf, pad->head);
    }

    /*
     * If padded_niov > IOV_MAX, we cannot just concatenate everything.
     * Instead, merge the first two or three elements of @iov to reduce the
     * number of vector elements as necessary.
     */
    if (padded_niov > IOV_MAX) {
        /*
         * Only head and tail can have lead to the number of entries exceeding
         * IOV_MAX, so we can exceed it by the head and tail at most.  We need
         * to reduce the number of elements by `surplus_count`, so we merge that
         * many elements plus one into one element.
         */
        surplus_count = padded_niov - IOV_MAX;
        assert(surplus_count <= !!pad->head + !!pad->tail);
        collapse_count = surplus_count + 1;

        /*
         * Move the elements to collapse into `pad->pre_collapse_qiov`, then
         * advance `iov` (and associated variables) by those elements.
         */
        qemu_iovec_init(&pad->pre_collapse_qiov, collapse_count);
        qemu_iovec_concat_iov(&pad->pre_collapse_qiov, iov,
                              collapse_count, iov_offset, SIZE_MAX);
        iov += collapse_count;
        iov_offset = 0;
        niov -= collapse_count;
        bytes -= pad->pre_collapse_qiov.size;

        /*
         * Construct the bounce buffer to match the length of the to-collapse
         * vector elements, and for write requests, initialize it with the data
         * from those elements.  Then add it to `pad->local_qiov`.
         */
        pad->collapse_len = pad->pre_collapse_qiov.size;
        pad->collapse_bounce_buf = qemu_blockalign(bs, pad->collapse_len);
        if (pad->write) {
            qemu_iovec_to_buf(&pad->pre_collapse_qiov, 0,
                              pad->collapse_bounce_buf, pad->collapse_len);
        }
        qemu_iovec_add(&pad->local_qiov,
                       pad->collapse_bounce_buf, pad->collapse_len);
    }

    qemu_iovec_concat_iov(&pad->local_qiov, iov, niov, iov_offset, bytes);

    if (pad->tail) {
        qemu_iovec_add(&pad->local_qiov,
                       pad->buf + pad->buf_len - pad->tail, pad->tail);
    }

    assert(pad->local_qiov.niov == MIN(padded_niov, IOV_MAX));
    return 0;
}

/*
 * bdrv_pad_request
 *
 * Exchange request parameters with padded request if needed. Don't include RMW
 * read of padding, bdrv_padding_rmw_read() should be called separately if
 * needed.
 *
 * @write is true for write requests, false for read requests.
 *
 * Request parameters (@qiov, &qiov_offset, &offset, &bytes) are in-out:
 *  - on function start they represent original request
 *  - on failure or when padding is not needed they are unchanged
 *  - on success when padding is needed they represent padded request
 */
static int bdrv_pad_request(BlockDriverState *bs,
                            QEMUIOVector **qiov, size_t *qiov_offset,
                            int64_t *offset, int64_t *bytes,
                            bool write,
                            BdrvRequestPadding *pad, bool *padded,
                            BdrvRequestFlags *flags)
{
    int ret;
    struct iovec *sliced_iov;
    int sliced_niov;
    size_t sliced_head, sliced_tail;

    /* Should have been checked by the caller already */
    ret = bdrv_check_request32(*offset, *bytes, *qiov, *qiov_offset);
    if (ret < 0) {
        return ret;
    }

    if (!bdrv_init_padding(bs, *offset, *bytes, write, pad)) {
        if (padded) {
            *padded = false;
        }
        return 0;
    }

    sliced_iov = qemu_iovec_slice(*qiov, *qiov_offset, *bytes,
                                  &sliced_head, &sliced_tail,
                                  &sliced_niov);

    /* Guaranteed by bdrv_check_request32() */
    assert(*bytes <= SIZE_MAX);
    ret = bdrv_create_padded_qiov(bs, pad, sliced_iov, sliced_niov,
                                  sliced_head, *bytes);
    if (ret < 0) {
        bdrv_padding_finalize(pad);
        return ret;
    }
    *bytes += pad->head + pad->tail;
    *offset -= pad->head;
    *qiov = &pad->local_qiov;
    *qiov_offset = 0;
    if (padded) {
        *padded = true;
    }
    if (flags) {
        /* Can't use optimization hint with bounce buffer */
        *flags &= ~BDRV_REQ_REGISTERED_BUF;
    }

    return 0;
}

int coroutine_fn bdrv_co_preadv(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    IO_CODE();
    return bdrv_co_preadv_part(child, offset, bytes, qiov, 0, flags);
}

int coroutine_fn bdrv_co_preadv_part(BdrvChild *child,
    int64_t offset, int64_t bytes,
    QEMUIOVector *qiov, size_t qiov_offset,
    BdrvRequestFlags flags)
{
    BlockDriverState *bs = child->bs;
    BdrvTrackedRequest req;
    BdrvRequestPadding pad;
    int ret;
    IO_CODE();

    trace_bdrv_co_preadv_part(bs, offset, bytes, flags);

    if (!bdrv_co_is_inserted(bs)) {
        return -ENOMEDIUM;
    }

    ret = bdrv_check_request32(offset, bytes, qiov, qiov_offset);
    if (ret < 0) {
        return ret;
    }

    if (bytes == 0 && !QEMU_IS_ALIGNED(offset, bs->bl.request_alignment)) {
        /*
         * Aligning zero request is nonsense. Even if driver has special meaning
         * of zero-length (like qcow2_co_pwritev_compressed_part), we can't pass
         * it to driver due to request_alignment.
         *
         * Still, no reason to return an error if someone do unaligned
         * zero-length read occasionally.
         */
        return 0;
    }

    bdrv_inc_in_flight(bs);

    /* Don't do copy-on-read if we read data before write operation */
    if (qatomic_read(&bs->copy_on_read)) {
        flags |= BDRV_REQ_COPY_ON_READ;
    }

    ret = bdrv_pad_request(bs, &qiov, &qiov_offset, &offset, &bytes, false,
                           &pad, NULL, &flags);
    if (ret < 0) {
        goto fail;
    }

    tracked_request_begin(&req, bs, offset, bytes, BDRV_TRACKED_READ);
    ret = bdrv_aligned_preadv(child, &req, offset, bytes,
                              bs->bl.request_alignment,
                              qiov, qiov_offset, flags);
    tracked_request_end(&req);
    bdrv_padding_finalize(&pad);

fail:
    bdrv_dec_in_flight(bs);

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_co_do_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                         BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    QEMUIOVector qiov;
    void *buf = NULL;
    int ret = 0;
    bool need_flush = false;
    int head = 0;
    int tail = 0;

    int64_t max_write_zeroes = MIN_NON_ZERO(bs->bl.max_pwrite_zeroes,
                                            INT64_MAX);
    int alignment = MAX(bs->bl.pwrite_zeroes_alignment,
                        bs->bl.request_alignment);
    int max_transfer = MIN_NON_ZERO(bs->bl.max_transfer, MAX_BOUNCE_BUFFER);

    assert_bdrv_graph_readable();
    bdrv_check_request(offset, bytes, &error_abort);

    if (!drv) {
        return -ENOMEDIUM;
    }

    if ((flags & ~bs->supported_zero_flags) & BDRV_REQ_NO_FALLBACK) {
        return -ENOTSUP;
    }

    /* By definition there is no user buffer so this flag doesn't make sense */
    if (flags & BDRV_REQ_REGISTERED_BUF) {
        return -EINVAL;
    }

    /* Invalidate the cached block-status data range if this write overlaps */
    bdrv_bsc_invalidate_range(bs, offset, bytes);

    assert(alignment % bs->bl.request_alignment == 0);
    head = offset % alignment;
    tail = (offset + bytes) % alignment;
    max_write_zeroes = QEMU_ALIGN_DOWN(max_write_zeroes, alignment);
    assert(max_write_zeroes >= bs->bl.request_alignment);

    while (bytes > 0 && !ret) {
        int64_t num = bytes;

        /* Align request.  Block drivers can expect the "bulk" of the request
         * to be aligned, and that unaligned requests do not cross cluster
         * boundaries.
         */
        if (head) {
            /* Make a small request up to the first aligned sector. For
             * convenience, limit this request to max_transfer even if
             * we don't need to fall back to writes.  */
            num = MIN(MIN(bytes, max_transfer), alignment - head);
            head = (head + num) % alignment;
            assert(num < max_write_zeroes);
        } else if (tail && num > alignment) {
            /* Shorten the request to the last aligned sector.  */
            num -= tail;
        }

        /* limit request size */
        if (num > max_write_zeroes) {
            num = max_write_zeroes;
        }

        ret = -ENOTSUP;
        /* First try the efficient write zeroes operation */
        if (drv->bdrv_co_pwrite_zeroes) {
            ret = drv->bdrv_co_pwrite_zeroes(bs, offset, num,
                                             flags & bs->supported_zero_flags);
            if (ret != -ENOTSUP && (flags & BDRV_REQ_FUA) &&
                !(bs->supported_zero_flags & BDRV_REQ_FUA)) {
                need_flush = true;
            }
        } else {
            assert(!bs->supported_zero_flags);
        }

        if (ret == -ENOTSUP && !(flags & BDRV_REQ_NO_FALLBACK)) {
            /* Fall back to bounce buffer if write zeroes is unsupported */
            BdrvRequestFlags write_flags = flags & ~BDRV_REQ_ZERO_WRITE;

            if ((flags & BDRV_REQ_FUA) &&
                !(bs->supported_write_flags & BDRV_REQ_FUA)) {
                /* No need for bdrv_driver_pwrite() to do a fallback
                 * flush on each chunk; use just one at the end */
                write_flags &= ~BDRV_REQ_FUA;
                need_flush = true;
            }
            num = MIN(num, max_transfer);
            if (buf == NULL) {
                buf = qemu_try_blockalign0(bs, num);
                if (buf == NULL) {
                    ret = -ENOMEM;
                    goto fail;
                }
            }
            qemu_iovec_init_buf(&qiov, buf, num);

            ret = bdrv_driver_pwritev(bs, offset, num, &qiov, 0, write_flags);

            /* Keep bounce buffer around if it is big enough for all
             * all future requests.
             */
            if (num < max_transfer) {
                qemu_vfree(buf);
                buf = NULL;
            }
        }

        offset += num;
        bytes -= num;
    }

fail:
    if (ret == 0 && need_flush) {
        ret = bdrv_co_flush(bs);
    }
    qemu_vfree(buf);
    return ret;
}

static inline int coroutine_fn GRAPH_RDLOCK
bdrv_co_write_req_prepare(BdrvChild *child, int64_t offset, int64_t bytes,
                          BdrvTrackedRequest *req, int flags)
{
    BlockDriverState *bs = child->bs;

    bdrv_check_request(offset, bytes, &error_abort);

    if (bdrv_is_read_only(bs)) {
        return -EPERM;
    }

    assert(!(bs->open_flags & BDRV_O_INACTIVE));
    assert((bs->open_flags & BDRV_O_NO_IO) == 0);
    assert(!(flags & ~BDRV_REQ_MASK));
    assert(!((flags & BDRV_REQ_NO_WAIT) && !(flags & BDRV_REQ_SERIALISING)));

    if (flags & BDRV_REQ_SERIALISING) {
        QEMU_LOCK_GUARD(&bs->reqs_lock);

        tracked_request_set_serialising(req, bdrv_get_cluster_size(bs));

        if ((flags & BDRV_REQ_NO_WAIT) && bdrv_find_conflicting_request(req)) {
            return -EBUSY;
        }

        bdrv_wait_serialising_requests_locked(req);
    } else {
        bdrv_wait_serialising_requests(req);
    }

    assert(req->overlap_offset <= offset);
    assert(offset + bytes <= req->overlap_offset + req->overlap_bytes);
    assert(offset + bytes <= bs->total_sectors * BDRV_SECTOR_SIZE ||
           child->perm & BLK_PERM_RESIZE);

    switch (req->type) {
    case BDRV_TRACKED_WRITE:
    case BDRV_TRACKED_DISCARD:
        if (flags & BDRV_REQ_WRITE_UNCHANGED) {
            assert(child->perm & (BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE));
        } else {
            assert(child->perm & BLK_PERM_WRITE);
        }
        bdrv_write_threshold_check_write(bs, offset, bytes);
        return 0;
    case BDRV_TRACKED_TRUNCATE:
        assert(child->perm & BLK_PERM_RESIZE);
        return 0;
    default:
        abort();
    }
}

static inline void coroutine_fn
bdrv_co_write_req_finish(BdrvChild *child, int64_t offset, int64_t bytes,
                         BdrvTrackedRequest *req, int ret)
{
    int64_t end_sector = DIV_ROUND_UP(offset + bytes, BDRV_SECTOR_SIZE);
    BlockDriverState *bs = child->bs;

    bdrv_check_request(offset, bytes, &error_abort);

    qatomic_inc(&bs->write_gen);

    /*
     * Discard cannot extend the image, but in error handling cases, such as
     * when reverting a qcow2 cluster allocation, the discarded range can pass
     * the end of image file, so we cannot assert about BDRV_TRACKED_DISCARD
     * here. Instead, just skip it, since semantically a discard request
     * beyond EOF cannot expand the image anyway.
     */
    if (ret == 0 &&
        (req->type == BDRV_TRACKED_TRUNCATE ||
         end_sector > bs->total_sectors) &&
        req->type != BDRV_TRACKED_DISCARD) {
        bs->total_sectors = end_sector;
        bdrv_parent_cb_resize(bs);
        bdrv_dirty_bitmap_truncate(bs, end_sector << BDRV_SECTOR_BITS);
    }
    if (req->bytes) {
        switch (req->type) {
        case BDRV_TRACKED_WRITE:
            stat64_max(&bs->wr_highest_offset, offset + bytes);
            /* fall through, to set dirty bits */
        case BDRV_TRACKED_DISCARD:
            bdrv_set_dirty(bs, offset, bytes);
            break;
        default:
            break;
        }
    }
}

/*
 * Forwards an already correctly aligned write request to the BlockDriver,
 * after possibly fragmenting it.
 */
static int coroutine_fn GRAPH_RDLOCK
bdrv_aligned_pwritev(BdrvChild *child, BdrvTrackedRequest *req,
                     int64_t offset, int64_t bytes, int64_t align,
                     QEMUIOVector *qiov, size_t qiov_offset,
                     BdrvRequestFlags flags)
{
    BlockDriverState *bs = child->bs;
    BlockDriver *drv = bs->drv;
    int ret;

    int64_t bytes_remaining = bytes;
    int max_transfer;

    bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, &error_abort);

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (bdrv_has_readonly_bitmaps(bs)) {
        return -EPERM;
    }

    assert(is_power_of_2(align));
    assert((offset & (align - 1)) == 0);
    assert((bytes & (align - 1)) == 0);
    max_transfer = QEMU_ALIGN_DOWN(MIN_NON_ZERO(bs->bl.max_transfer, INT_MAX),
                                   align);

    ret = bdrv_co_write_req_prepare(child, offset, bytes, req, flags);

    if (!ret && bs->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF &&
        !(flags & BDRV_REQ_ZERO_WRITE) && drv->bdrv_co_pwrite_zeroes &&
        qemu_iovec_is_zero(qiov, qiov_offset, bytes)) {
        flags |= BDRV_REQ_ZERO_WRITE;
        if (bs->detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP) {
            flags |= BDRV_REQ_MAY_UNMAP;
        }

        /* Can't use optimization hint with bufferless zero write */
        flags &= ~BDRV_REQ_REGISTERED_BUF;
    }

    if (ret < 0) {
        /* Do nothing, write notifier decided to fail this request */
    } else if (flags & BDRV_REQ_ZERO_WRITE) {
        bdrv_co_debug_event(bs, BLKDBG_PWRITEV_ZERO);
        ret = bdrv_co_do_pwrite_zeroes(bs, offset, bytes, flags);
    } else if (flags & BDRV_REQ_WRITE_COMPRESSED) {
        ret = bdrv_driver_pwritev_compressed(bs, offset, bytes,
                                             qiov, qiov_offset);
    } else if (bytes <= max_transfer) {
        bdrv_co_debug_event(bs, BLKDBG_PWRITEV);
        ret = bdrv_driver_pwritev(bs, offset, bytes, qiov, qiov_offset, flags);
    } else {
        bdrv_co_debug_event(bs, BLKDBG_PWRITEV);
        while (bytes_remaining) {
            int num = MIN(bytes_remaining, max_transfer);
            int local_flags = flags;

            assert(num);
            if (num < bytes_remaining && (flags & BDRV_REQ_FUA) &&
                !(bs->supported_write_flags & BDRV_REQ_FUA)) {
                /* If FUA is going to be emulated by flush, we only
                 * need to flush on the last iteration */
                local_flags &= ~BDRV_REQ_FUA;
            }

            ret = bdrv_driver_pwritev(bs, offset + bytes - bytes_remaining,
                                      num, qiov,
                                      qiov_offset + bytes - bytes_remaining,
                                      local_flags);
            if (ret < 0) {
                break;
            }
            bytes_remaining -= num;
        }
    }
    bdrv_co_debug_event(bs, BLKDBG_PWRITEV_DONE);

    if (ret >= 0) {
        ret = 0;
    }
    bdrv_co_write_req_finish(child, offset, bytes, req, ret);

    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
bdrv_co_do_zero_pwritev(BdrvChild *child, int64_t offset, int64_t bytes,
                        BdrvRequestFlags flags, BdrvTrackedRequest *req)
{
    BlockDriverState *bs = child->bs;
    QEMUIOVector local_qiov;
    uint64_t align = bs->bl.request_alignment;
    int ret = 0;
    bool padding;
    BdrvRequestPadding pad;

    /* This flag doesn't make sense for padding or zero writes */
    flags &= ~BDRV_REQ_REGISTERED_BUF;

    padding = bdrv_init_padding(bs, offset, bytes, true, &pad);
    if (padding) {
        assert(!(flags & BDRV_REQ_NO_WAIT));
        bdrv_make_request_serialising(req, align);

        bdrv_padding_rmw_read(child, req, &pad, true);

        if (pad.head || pad.merge_reads) {
            int64_t aligned_offset = offset & ~(align - 1);
            int64_t write_bytes = pad.merge_reads ? pad.buf_len : align;

            qemu_iovec_init_buf(&local_qiov, pad.buf, write_bytes);
            ret = bdrv_aligned_pwritev(child, req, aligned_offset, write_bytes,
                                       align, &local_qiov, 0,
                                       flags & ~BDRV_REQ_ZERO_WRITE);
            if (ret < 0 || pad.merge_reads) {
                /* Error or all work is done */
                goto out;
            }
            offset += write_bytes - pad.head;
            bytes -= write_bytes - pad.head;
        }
    }

    assert(!bytes || (offset & (align - 1)) == 0);
    if (bytes >= align) {
        /* Write the aligned part in the middle. */
        int64_t aligned_bytes = bytes & ~(align - 1);
        ret = bdrv_aligned_pwritev(child, req, offset, aligned_bytes, align,
                                   NULL, 0, flags);
        if (ret < 0) {
            goto out;
        }
        bytes -= aligned_bytes;
        offset += aligned_bytes;
    }

    assert(!bytes || (offset & (align - 1)) == 0);
    if (bytes) {
        assert(align == pad.tail + bytes);

        qemu_iovec_init_buf(&local_qiov, pad.tail_buf, align);
        ret = bdrv_aligned_pwritev(child, req, offset, align, align,
                                   &local_qiov, 0,
                                   flags & ~BDRV_REQ_ZERO_WRITE);
    }

out:
    bdrv_padding_finalize(&pad);

    return ret;
}

/*
 * Handle a write request in coroutine context
 */
int coroutine_fn bdrv_co_pwritev(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    IO_CODE();
    return bdrv_co_pwritev_part(child, offset, bytes, qiov, 0, flags);
}

int coroutine_fn bdrv_co_pwritev_part(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov, size_t qiov_offset,
    BdrvRequestFlags flags)
{
    BlockDriverState *bs = child->bs;
    BdrvTrackedRequest req;
    uint64_t align = bs->bl.request_alignment;
    BdrvRequestPadding pad;
    int ret;
    bool padded = false;
    IO_CODE();

    trace_bdrv_co_pwritev_part(child->bs, offset, bytes, flags);

    if (!bdrv_co_is_inserted(bs)) {
        return -ENOMEDIUM;
    }

    if (flags & BDRV_REQ_ZERO_WRITE) {
        ret = bdrv_check_qiov_request(offset, bytes, qiov, qiov_offset, NULL);
    } else {
        ret = bdrv_check_request32(offset, bytes, qiov, qiov_offset);
    }
    if (ret < 0) {
        return ret;
    }

    /* If the request is misaligned then we can't make it efficient */
    if ((flags & BDRV_REQ_NO_FALLBACK) &&
        !QEMU_IS_ALIGNED(offset | bytes, align))
    {
        return -ENOTSUP;
    }

    if (bytes == 0 && !QEMU_IS_ALIGNED(offset, bs->bl.request_alignment)) {
        /*
         * Aligning zero request is nonsense. Even if driver has special meaning
         * of zero-length (like qcow2_co_pwritev_compressed_part), we can't pass
         * it to driver due to request_alignment.
         *
         * Still, no reason to return an error if someone do unaligned
         * zero-length write occasionally.
         */
        return 0;
    }

    if (!(flags & BDRV_REQ_ZERO_WRITE)) {
        /*
         * Pad request for following read-modify-write cycle.
         * bdrv_co_do_zero_pwritev() does aligning by itself, so, we do
         * alignment only if there is no ZERO flag.
         */
        ret = bdrv_pad_request(bs, &qiov, &qiov_offset, &offset, &bytes, true,
                               &pad, &padded, &flags);
        if (ret < 0) {
            return ret;
        }
    }

    bdrv_inc_in_flight(bs);
    tracked_request_begin(&req, bs, offset, bytes, BDRV_TRACKED_WRITE);

    if (flags & BDRV_REQ_ZERO_WRITE) {
        assert(!padded);
        ret = bdrv_co_do_zero_pwritev(child, offset, bytes, flags, &req);
        goto out;
    }

    if (padded) {
        /*
         * Request was unaligned to request_alignment and therefore
         * padded.  We are going to do read-modify-write, and must
         * serialize the request to prevent interactions of the
         * widened region with other transactions.
         */
        assert(!(flags & BDRV_REQ_NO_WAIT));
        bdrv_make_request_serialising(&req, align);
        bdrv_padding_rmw_read(child, &req, &pad, false);
    }

    ret = bdrv_aligned_pwritev(child, &req, offset, bytes, align,
                               qiov, qiov_offset, flags);

    bdrv_padding_finalize(&pad);

out:
    tracked_request_end(&req);
    bdrv_dec_in_flight(bs);

    return ret;
}

int coroutine_fn bdrv_co_pwrite_zeroes(BdrvChild *child, int64_t offset,
                                       int64_t bytes, BdrvRequestFlags flags)
{
    IO_CODE();
    trace_bdrv_co_pwrite_zeroes(child->bs, offset, bytes, flags);
    assert_bdrv_graph_readable();

    if (!(child->bs->open_flags & BDRV_O_UNMAP)) {
        flags &= ~BDRV_REQ_MAY_UNMAP;
    }

    return bdrv_co_pwritev(child, offset, bytes, NULL,
                           BDRV_REQ_ZERO_WRITE | flags);
}

/*
 * Flush ALL BDSes regardless of if they are reachable via a BlkBackend or not.
 */
int bdrv_flush_all(void)
{
    BdrvNextIterator it;
    BlockDriverState *bs = NULL;
    int result = 0;

    GLOBAL_STATE_CODE();

    /*
     * bdrv queue is managed by record/replay,
     * creating new flush request for stopping
     * the VM may break the determinism
     */
    if (replay_events_enabled()) {
        return result;
    }

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        AioContext *aio_context = bdrv_get_aio_context(bs);
        int ret;

        aio_context_acquire(aio_context);
        ret = bdrv_flush(bs);
        if (ret < 0 && !result) {
            result = ret;
        }
        aio_context_release(aio_context);
    }

    return result;
}

/*
 * Returns the allocation status of the specified sectors.
 * Drivers not implementing the functionality are assumed to not support
 * backing files, hence all their sectors are reported as allocated.
 *
 * If 'want_zero' is true, the caller is querying for mapping
 * purposes, with a focus on valid BDRV_BLOCK_OFFSET_VALID, _DATA, and
 * _ZERO where possible; otherwise, the result favors larger 'pnum',
 * with a focus on accurate BDRV_BLOCK_ALLOCATED.
 *
 * If 'offset' is beyond the end of the disk image the return value is
 * BDRV_BLOCK_EOF and 'pnum' is set to 0.
 *
 * 'bytes' is the max value 'pnum' should be set to.  If bytes goes
 * beyond the end of the disk image it will be clamped; if 'pnum' is set to
 * the end of the image, then the returned value will include BDRV_BLOCK_EOF.
 *
 * 'pnum' is set to the number of bytes (including and immediately
 * following the specified offset) that are easily known to be in the
 * same allocated/unallocated state.  Note that a second call starting
 * at the original offset plus returned pnum may have the same status.
 * The returned value is non-zero on success except at end-of-file.
 *
 * Returns negative errno on failure.  Otherwise, if the
 * BDRV_BLOCK_OFFSET_VALID bit is set, 'map' and 'file' (if non-NULL) are
 * set to the host mapping and BDS corresponding to the guest offset.
 */
static int coroutine_fn GRAPH_RDLOCK
bdrv_co_block_status(BlockDriverState *bs, bool want_zero,
                     int64_t offset, int64_t bytes,
                     int64_t *pnum, int64_t *map, BlockDriverState **file)
{
    int64_t total_size;
    int64_t n; /* bytes */
    int ret;
    int64_t local_map = 0;
    BlockDriverState *local_file = NULL;
    int64_t aligned_offset, aligned_bytes;
    uint32_t align;
    bool has_filtered_child;

    assert(pnum);
    assert_bdrv_graph_readable();
    *pnum = 0;
    total_size = bdrv_co_getlength(bs);
    if (total_size < 0) {
        ret = total_size;
        goto early_out;
    }

    if (offset >= total_size) {
        ret = BDRV_BLOCK_EOF;
        goto early_out;
    }
    if (!bytes) {
        ret = 0;
        goto early_out;
    }

    n = total_size - offset;
    if (n < bytes) {
        bytes = n;
    }

    /* Must be non-NULL or bdrv_co_getlength() would have failed */
    assert(bs->drv);
    has_filtered_child = bdrv_filter_child(bs);
    if (!bs->drv->bdrv_co_block_status && !has_filtered_child) {
        *pnum = bytes;
        ret = BDRV_BLOCK_DATA | BDRV_BLOCK_ALLOCATED;
        if (offset + bytes == total_size) {
            ret |= BDRV_BLOCK_EOF;
        }
        if (bs->drv->protocol_name) {
            ret |= BDRV_BLOCK_OFFSET_VALID;
            local_map = offset;
            local_file = bs;
        }
        goto early_out;
    }

    bdrv_inc_in_flight(bs);

    /* Round out to request_alignment boundaries */
    align = bs->bl.request_alignment;
    aligned_offset = QEMU_ALIGN_DOWN(offset, align);
    aligned_bytes = ROUND_UP(offset + bytes, align) - aligned_offset;

    if (bs->drv->bdrv_co_block_status) {
        /*
         * Use the block-status cache only for protocol nodes: Format
         * drivers are generally quick to inquire the status, but protocol
         * drivers often need to get information from outside of qemu, so
         * we do not have control over the actual implementation.  There
         * have been cases where inquiring the status took an unreasonably
         * long time, and we can do nothing in qemu to fix it.
         * This is especially problematic for images with large data areas,
         * because finding the few holes in them and giving them special
         * treatment does not gain much performance.  Therefore, we try to
         * cache the last-identified data region.
         *
         * Second, limiting ourselves to protocol nodes allows us to assume
         * the block status for data regions to be DATA | OFFSET_VALID, and
         * that the host offset is the same as the guest offset.
         *
         * Note that it is possible that external writers zero parts of
         * the cached regions without the cache being invalidated, and so
         * we may report zeroes as data.  This is not catastrophic,
         * however, because reporting zeroes as data is fine.
         */
        if (QLIST_EMPTY(&bs->children) &&
            bdrv_bsc_is_data(bs, aligned_offset, pnum))
        {
            ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
            local_file = bs;
            local_map = aligned_offset;
        } else {
            ret = bs->drv->bdrv_co_block_status(bs, want_zero, aligned_offset,
                                                aligned_bytes, pnum, &local_map,
                                                &local_file);

            /*
             * Note that checking QLIST_EMPTY(&bs->children) is also done when
             * the cache is queried above.  Technically, we do not need to check
             * it here; the worst that can happen is that we fill the cache for
             * non-protocol nodes, and then it is never used.  However, filling
             * the cache requires an RCU update, so double check here to avoid
             * such an update if possible.
             *
             * Check want_zero, because we only want to update the cache when we
             * have accurate information about what is zero and what is data.
             */
            if (want_zero &&
                ret == (BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID) &&
                QLIST_EMPTY(&bs->children))
            {
                /*
                 * When a protocol driver reports BLOCK_OFFSET_VALID, the
                 * returned local_map value must be the same as the offset we
                 * have passed (aligned_offset), and local_bs must be the node
                 * itself.
                 * Assert this, because we follow this rule when reading from
                 * the cache (see the `local_file = bs` and
                 * `local_map = aligned_offset` assignments above), and the
                 * result the cache delivers must be the same as the driver
                 * would deliver.
                 */
                assert(local_file == bs);
                assert(local_map == aligned_offset);
                bdrv_bsc_fill(bs, aligned_offset, *pnum);
            }
        }
    } else {
        /* Default code for filters */

        local_file = bdrv_filter_bs(bs);
        assert(local_file);

        *pnum = aligned_bytes;
        local_map = aligned_offset;
        ret = BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID;
    }
    if (ret < 0) {
        *pnum = 0;
        goto out;
    }

    /*
     * The driver's result must be a non-zero multiple of request_alignment.
     * Clamp pnum and adjust map to original request.
     */
    assert(*pnum && QEMU_IS_ALIGNED(*pnum, align) &&
           align > offset - aligned_offset);
    if (ret & BDRV_BLOCK_RECURSE) {
        assert(ret & BDRV_BLOCK_DATA);
        assert(ret & BDRV_BLOCK_OFFSET_VALID);
        assert(!(ret & BDRV_BLOCK_ZERO));
    }

    *pnum -= offset - aligned_offset;
    if (*pnum > bytes) {
        *pnum = bytes;
    }
    if (ret & BDRV_BLOCK_OFFSET_VALID) {
        local_map += offset - aligned_offset;
    }

    if (ret & BDRV_BLOCK_RAW) {
        assert(ret & BDRV_BLOCK_OFFSET_VALID && local_file);
        ret = bdrv_co_block_status(local_file, want_zero, local_map,
                                   *pnum, pnum, &local_map, &local_file);
        goto out;
    }

    if (ret & (BDRV_BLOCK_DATA | BDRV_BLOCK_ZERO)) {
        ret |= BDRV_BLOCK_ALLOCATED;
    } else if (bs->drv->supports_backing) {
        BlockDriverState *cow_bs = bdrv_cow_bs(bs);

        if (!cow_bs) {
            ret |= BDRV_BLOCK_ZERO;
        } else if (want_zero) {
            int64_t size2 = bdrv_co_getlength(cow_bs);

            if (size2 >= 0 && offset >= size2) {
                ret |= BDRV_BLOCK_ZERO;
            }
        }
    }

    if (want_zero && ret & BDRV_BLOCK_RECURSE &&
        local_file && local_file != bs &&
        (ret & BDRV_BLOCK_DATA) && !(ret & BDRV_BLOCK_ZERO) &&
        (ret & BDRV_BLOCK_OFFSET_VALID)) {
        int64_t file_pnum;
        int ret2;

        ret2 = bdrv_co_block_status(local_file, want_zero, local_map,
                                    *pnum, &file_pnum, NULL, NULL);
        if (ret2 >= 0) {
            /* Ignore errors.  This is just providing extra information, it
             * is useful but not necessary.
             */
            if (ret2 & BDRV_BLOCK_EOF &&
                (!file_pnum || ret2 & BDRV_BLOCK_ZERO)) {
                /*
                 * It is valid for the format block driver to read
                 * beyond the end of the underlying file's current
                 * size; such areas read as zero.
                 */
                ret |= BDRV_BLOCK_ZERO;
            } else {
                /* Limit request to the range reported by the protocol driver */
                *pnum = file_pnum;
                ret |= (ret2 & BDRV_BLOCK_ZERO);
            }
        }
    }

out:
    bdrv_dec_in_flight(bs);
    if (ret >= 0 && offset + *pnum == total_size) {
        ret |= BDRV_BLOCK_EOF;
    }
early_out:
    if (file) {
        *file = local_file;
    }
    if (map) {
        *map = local_map;
    }
    return ret;
}

int coroutine_fn
bdrv_co_common_block_status_above(BlockDriverState *bs,
                                  BlockDriverState *base,
                                  bool include_base,
                                  bool want_zero,
                                  int64_t offset,
                                  int64_t bytes,
                                  int64_t *pnum,
                                  int64_t *map,
                                  BlockDriverState **file,
                                  int *depth)
{
    int ret;
    BlockDriverState *p;
    int64_t eof = 0;
    int dummy;
    IO_CODE();

    assert(!include_base || base); /* Can't include NULL base */
    assert_bdrv_graph_readable();

    if (!depth) {
        depth = &dummy;
    }
    *depth = 0;

    if (!include_base && bs == base) {
        *pnum = bytes;
        return 0;
    }

    ret = bdrv_co_block_status(bs, want_zero, offset, bytes, pnum, map, file);
    ++*depth;
    if (ret < 0 || *pnum == 0 || ret & BDRV_BLOCK_ALLOCATED || bs == base) {
        return ret;
    }

    if (ret & BDRV_BLOCK_EOF) {
        eof = offset + *pnum;
    }

    assert(*pnum <= bytes);
    bytes = *pnum;

    for (p = bdrv_filter_or_cow_bs(bs); include_base || p != base;
         p = bdrv_filter_or_cow_bs(p))
    {
        ret = bdrv_co_block_status(p, want_zero, offset, bytes, pnum, map,
                                   file);
        ++*depth;
        if (ret < 0) {
            return ret;
        }
        if (*pnum == 0) {
            /*
             * The top layer deferred to this layer, and because this layer is
             * short, any zeroes that we synthesize beyond EOF behave as if they
             * were allocated at this layer.
             *
             * We don't include BDRV_BLOCK_EOF into ret, as upper layer may be
             * larger. We'll add BDRV_BLOCK_EOF if needed at function end, see
             * below.
             */
            assert(ret & BDRV_BLOCK_EOF);
            *pnum = bytes;
            if (file) {
                *file = p;
            }
            ret = BDRV_BLOCK_ZERO | BDRV_BLOCK_ALLOCATED;
            break;
        }
        if (ret & BDRV_BLOCK_ALLOCATED) {
            /*
             * We've found the node and the status, we must break.
             *
             * Drop BDRV_BLOCK_EOF, as it's not for upper layer, which may be
             * larger. We'll add BDRV_BLOCK_EOF if needed at function end, see
             * below.
             */
            ret &= ~BDRV_BLOCK_EOF;
            break;
        }

        if (p == base) {
            assert(include_base);
            break;
        }

        /*
         * OK, [offset, offset + *pnum) region is unallocated on this layer,
         * let's continue the diving.
         */
        assert(*pnum <= bytes);
        bytes = *pnum;
    }

    if (offset + *pnum == eof) {
        ret |= BDRV_BLOCK_EOF;
    }

    return ret;
}

int coroutine_fn bdrv_co_block_status_above(BlockDriverState *bs,
                                            BlockDriverState *base,
                                            int64_t offset, int64_t bytes,
                                            int64_t *pnum, int64_t *map,
                                            BlockDriverState **file)
{
    IO_CODE();
    return bdrv_co_common_block_status_above(bs, base, false, true, offset,
                                             bytes, pnum, map, file, NULL);
}

int bdrv_block_status_above(BlockDriverState *bs, BlockDriverState *base,
                            int64_t offset, int64_t bytes, int64_t *pnum,
                            int64_t *map, BlockDriverState **file)
{
    IO_CODE();
    return bdrv_common_block_status_above(bs, base, false, true, offset, bytes,
                                          pnum, map, file, NULL);
}

int bdrv_block_status(BlockDriverState *bs, int64_t offset, int64_t bytes,
                      int64_t *pnum, int64_t *map, BlockDriverState **file)
{
    IO_CODE();
    return bdrv_block_status_above(bs, bdrv_filter_or_cow_bs(bs),
                                   offset, bytes, pnum, map, file);
}

/*
 * Check @bs (and its backing chain) to see if the range defined
 * by @offset and @bytes is known to read as zeroes.
 * Return 1 if that is the case, 0 otherwise and -errno on error.
 * This test is meant to be fast rather than accurate so returning 0
 * does not guarantee non-zero data.
 */
int coroutine_fn bdrv_co_is_zero_fast(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes)
{
    int ret;
    int64_t pnum = bytes;
    IO_CODE();

    if (!bytes) {
        return 1;
    }

    ret = bdrv_co_common_block_status_above(bs, NULL, false, false, offset,
                                            bytes, &pnum, NULL, NULL, NULL);

    if (ret < 0) {
        return ret;
    }

    return (pnum == bytes) && (ret & BDRV_BLOCK_ZERO);
}

int coroutine_fn bdrv_co_is_allocated(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes, int64_t *pnum)
{
    int ret;
    int64_t dummy;
    IO_CODE();

    ret = bdrv_co_common_block_status_above(bs, bs, true, false, offset,
                                            bytes, pnum ? pnum : &dummy, NULL,
                                            NULL, NULL);
    if (ret < 0) {
        return ret;
    }
    return !!(ret & BDRV_BLOCK_ALLOCATED);
}

int bdrv_is_allocated(BlockDriverState *bs, int64_t offset, int64_t bytes,
                      int64_t *pnum)
{
    int ret;
    int64_t dummy;
    IO_CODE();

    ret = bdrv_common_block_status_above(bs, bs, true, false, offset,
                                         bytes, pnum ? pnum : &dummy, NULL,
                                         NULL, NULL);
    if (ret < 0) {
        return ret;
    }
    return !!(ret & BDRV_BLOCK_ALLOCATED);
}

/* See bdrv_is_allocated_above for documentation */
int coroutine_fn bdrv_co_is_allocated_above(BlockDriverState *top,
                                            BlockDriverState *base,
                                            bool include_base, int64_t offset,
                                            int64_t bytes, int64_t *pnum)
{
    int depth;
    int ret;
    IO_CODE();

    ret = bdrv_co_common_block_status_above(top, base, include_base, false,
                                            offset, bytes, pnum, NULL, NULL,
                                            &depth);
    if (ret < 0) {
        return ret;
    }

    if (ret & BDRV_BLOCK_ALLOCATED) {
        return depth;
    }
    return 0;
}

/*
 * Given an image chain: ... -> [BASE] -> [INTER1] -> [INTER2] -> [TOP]
 *
 * Return a positive depth if (a prefix of) the given range is allocated
 * in any image between BASE and TOP (BASE is only included if include_base
 * is set).  Depth 1 is TOP, 2 is the first backing layer, and so forth.
 * BASE can be NULL to check if the given offset is allocated in any
 * image of the chain.  Return 0 otherwise, or negative errno on
 * failure.
 *
 * 'pnum' is set to the number of bytes (including and immediately
 * following the specified offset) that are known to be in the same
 * allocated/unallocated state.  Note that a subsequent call starting
 * at 'offset + *pnum' may return the same allocation status (in other
 * words, the result is not necessarily the maximum possible range);
 * but 'pnum' will only be 0 when end of file is reached.
 */
int bdrv_is_allocated_above(BlockDriverState *top,
                            BlockDriverState *base,
                            bool include_base, int64_t offset,
                            int64_t bytes, int64_t *pnum)
{
    int depth;
    int ret;
    IO_CODE();

    ret = bdrv_common_block_status_above(top, base, include_base, false,
                                         offset, bytes, pnum, NULL, NULL,
                                         &depth);
    if (ret < 0) {
        return ret;
    }

    if (ret & BDRV_BLOCK_ALLOCATED) {
        return depth;
    }
    return 0;
}

int coroutine_fn
bdrv_co_readv_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos)
{
    BlockDriver *drv = bs->drv;
    BlockDriverState *child_bs = bdrv_primary_bs(bs);
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    ret = bdrv_check_qiov_request(pos, qiov->size, qiov, 0, NULL);
    if (ret < 0) {
        return ret;
    }

    if (!drv) {
        return -ENOMEDIUM;
    }

    bdrv_inc_in_flight(bs);

    if (drv->bdrv_co_load_vmstate) {
        ret = drv->bdrv_co_load_vmstate(bs, qiov, pos);
    } else if (child_bs) {
        ret = bdrv_co_readv_vmstate(child_bs, qiov, pos);
    } else {
        ret = -ENOTSUP;
    }

    bdrv_dec_in_flight(bs);

    return ret;
}

int coroutine_fn
bdrv_co_writev_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos)
{
    BlockDriver *drv = bs->drv;
    BlockDriverState *child_bs = bdrv_primary_bs(bs);
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    ret = bdrv_check_qiov_request(pos, qiov->size, qiov, 0, NULL);
    if (ret < 0) {
        return ret;
    }

    if (!drv) {
        return -ENOMEDIUM;
    }

    bdrv_inc_in_flight(bs);

    if (drv->bdrv_co_save_vmstate) {
        ret = drv->bdrv_co_save_vmstate(bs, qiov, pos);
    } else if (child_bs) {
        ret = bdrv_co_writev_vmstate(child_bs, qiov, pos);
    } else {
        ret = -ENOTSUP;
    }

    bdrv_dec_in_flight(bs);

    return ret;
}

int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, size);
    int ret = bdrv_writev_vmstate(bs, &qiov, pos);
    IO_CODE();

    return ret < 0 ? ret : size;
}

int bdrv_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                      int64_t pos, int size)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, size);
    int ret = bdrv_readv_vmstate(bs, &qiov, pos);
    IO_CODE();

    return ret < 0 ? ret : size;
}

/**************************************************************/
/* async I/Os */

void bdrv_aio_cancel(BlockAIOCB *acb)
{
    IO_CODE();
    qemu_aio_ref(acb);
    bdrv_aio_cancel_async(acb);
    while (acb->refcnt > 1) {
        if (acb->aiocb_info->get_aio_context) {
            aio_poll(acb->aiocb_info->get_aio_context(acb), true);
        } else if (acb->bs) {
            /* qemu_aio_ref and qemu_aio_unref are not thread-safe, so
             * assert that we're not using an I/O thread.  Thread-safe
             * code should use bdrv_aio_cancel_async exclusively.
             */
            assert(bdrv_get_aio_context(acb->bs) == qemu_get_aio_context());
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
    IO_CODE();
    if (acb->aiocb_info->cancel_async) {
        acb->aiocb_info->cancel_async(acb);
    }
}

/**************************************************************/
/* Coroutine block device emulation */

int coroutine_fn bdrv_co_flush(BlockDriverState *bs)
{
    BdrvChild *primary_child = bdrv_primary_child(bs);
    BdrvChild *child;
    int current_gen;
    int ret = 0;
    IO_CODE();

    assert_bdrv_graph_readable();
    bdrv_inc_in_flight(bs);

    if (!bdrv_co_is_inserted(bs) || bdrv_is_read_only(bs) ||
        bdrv_is_sg(bs)) {
        goto early_exit;
    }

    qemu_co_mutex_lock(&bs->reqs_lock);
    current_gen = qatomic_read(&bs->write_gen);

    /* Wait until any previous flushes are completed */
    while (bs->active_flush_req) {
        qemu_co_queue_wait(&bs->flush_queue, &bs->reqs_lock);
    }

    /* Flushes reach this point in nondecreasing current_gen order.  */
    bs->active_flush_req = true;
    qemu_co_mutex_unlock(&bs->reqs_lock);

    /* Write back all layers by calling one driver function */
    if (bs->drv->bdrv_co_flush) {
        ret = bs->drv->bdrv_co_flush(bs);
        goto out;
    }

    /* Write back cached data to the OS even with cache=unsafe */
    BLKDBG_CO_EVENT(primary_child, BLKDBG_FLUSH_TO_OS);
    if (bs->drv->bdrv_co_flush_to_os) {
        ret = bs->drv->bdrv_co_flush_to_os(bs);
        if (ret < 0) {
            goto out;
        }
    }

    /* But don't actually force it to the disk with cache=unsafe */
    if (bs->open_flags & BDRV_O_NO_FLUSH) {
        goto flush_children;
    }

    /* Check if we really need to flush anything */
    if (bs->flushed_gen == current_gen) {
        goto flush_children;
    }

    BLKDBG_CO_EVENT(primary_child, BLKDBG_FLUSH_TO_DISK);
    if (!bs->drv) {
        /* bs->drv->bdrv_co_flush() might have ejected the BDS
         * (even in case of apparent success) */
        ret = -ENOMEDIUM;
        goto out;
    }
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
flush_children:
    ret = 0;
    QLIST_FOREACH(child, &bs->children, next) {
        if (child->perm & (BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED)) {
            int this_child_ret = bdrv_co_flush(child->bs);
            if (!ret) {
                ret = this_child_ret;
            }
        }
    }

out:
    /* Notify any pending flushes that we have completed */
    if (ret == 0) {
        bs->flushed_gen = current_gen;
    }

    qemu_co_mutex_lock(&bs->reqs_lock);
    bs->active_flush_req = false;
    /* Return value is ignored - it's ok if wait queue is empty */
    qemu_co_queue_next(&bs->flush_queue);
    qemu_co_mutex_unlock(&bs->reqs_lock);

early_exit:
    bdrv_dec_in_flight(bs);
    return ret;
}

int coroutine_fn bdrv_co_pdiscard(BdrvChild *child, int64_t offset,
                                  int64_t bytes)
{
    BdrvTrackedRequest req;
    int ret;
    int64_t max_pdiscard;
    int head, tail, align;
    BlockDriverState *bs = child->bs;
    IO_CODE();
    assert_bdrv_graph_readable();

    if (!bs || !bs->drv || !bdrv_co_is_inserted(bs)) {
        return -ENOMEDIUM;
    }

    if (bdrv_has_readonly_bitmaps(bs)) {
        return -EPERM;
    }

    ret = bdrv_check_request(offset, bytes, NULL);
    if (ret < 0) {
        return ret;
    }

    /* Do nothing if disabled.  */
    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        return 0;
    }

    if (!bs->drv->bdrv_co_pdiscard && !bs->drv->bdrv_aio_pdiscard) {
        return 0;
    }

    /* Invalidate the cached block-status data range if this discard overlaps */
    bdrv_bsc_invalidate_range(bs, offset, bytes);

    /* Discard is advisory, but some devices track and coalesce
     * unaligned requests, so we must pass everything down rather than
     * round here.  Still, most devices will just silently ignore
     * unaligned requests (by returning -ENOTSUP), so we must fragment
     * the request accordingly.  */
    align = MAX(bs->bl.pdiscard_alignment, bs->bl.request_alignment);
    assert(align % bs->bl.request_alignment == 0);
    head = offset % align;
    tail = (offset + bytes) % align;

    bdrv_inc_in_flight(bs);
    tracked_request_begin(&req, bs, offset, bytes, BDRV_TRACKED_DISCARD);

    ret = bdrv_co_write_req_prepare(child, offset, bytes, &req, 0);
    if (ret < 0) {
        goto out;
    }

    max_pdiscard = QEMU_ALIGN_DOWN(MIN_NON_ZERO(bs->bl.max_pdiscard, INT64_MAX),
                                   align);
    assert(max_pdiscard >= bs->bl.request_alignment);

    while (bytes > 0) {
        int64_t num = bytes;

        if (head) {
            /* Make small requests to get to alignment boundaries. */
            num = MIN(bytes, align - head);
            if (!QEMU_IS_ALIGNED(num, bs->bl.request_alignment)) {
                num %= bs->bl.request_alignment;
            }
            head = (head + num) % align;
            assert(num < max_pdiscard);
        } else if (tail) {
            if (num > align) {
                /* Shorten the request to the last aligned cluster.  */
                num -= tail;
            } else if (!QEMU_IS_ALIGNED(tail, bs->bl.request_alignment) &&
                       tail > bs->bl.request_alignment) {
                tail %= bs->bl.request_alignment;
                num -= tail;
            }
        }
        /* limit request size */
        if (num > max_pdiscard) {
            num = max_pdiscard;
        }

        if (!bs->drv) {
            ret = -ENOMEDIUM;
            goto out;
        }
        if (bs->drv->bdrv_co_pdiscard) {
            ret = bs->drv->bdrv_co_pdiscard(bs, offset, num);
        } else {
            BlockAIOCB *acb;
            CoroutineIOCompletion co = {
                .coroutine = qemu_coroutine_self(),
            };

            acb = bs->drv->bdrv_aio_pdiscard(bs, offset, num,
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

        offset += num;
        bytes -= num;
    }
    ret = 0;
out:
    bdrv_co_write_req_finish(child, req.offset, req.bytes, &req, ret);
    tracked_request_end(&req);
    bdrv_dec_in_flight(bs);
    return ret;
}

int coroutine_fn bdrv_co_ioctl(BlockDriverState *bs, int req, void *buf)
{
    BlockDriver *drv = bs->drv;
    CoroutineIOCompletion co = {
        .coroutine = qemu_coroutine_self(),
    };
    BlockAIOCB *acb;
    IO_CODE();
    assert_bdrv_graph_readable();

    bdrv_inc_in_flight(bs);
    if (!drv || (!drv->bdrv_aio_ioctl && !drv->bdrv_co_ioctl)) {
        co.ret = -ENOTSUP;
        goto out;
    }

    if (drv->bdrv_co_ioctl) {
        co.ret = drv->bdrv_co_ioctl(bs, req, buf);
    } else {
        acb = drv->bdrv_aio_ioctl(bs, req, buf, bdrv_co_io_em_complete, &co);
        if (!acb) {
            co.ret = -ENOTSUP;
            goto out;
        }
        qemu_coroutine_yield();
    }
out:
    bdrv_dec_in_flight(bs);
    return co.ret;
}

int coroutine_fn bdrv_co_zone_report(BlockDriverState *bs, int64_t offset,
                        unsigned int *nr_zones,
                        BlockZoneDescriptor *zones)
{
    BlockDriver *drv = bs->drv;
    CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
    };
    IO_CODE();

    bdrv_inc_in_flight(bs);
    if (!drv || !drv->bdrv_co_zone_report || bs->bl.zoned == BLK_Z_NONE) {
        co.ret = -ENOTSUP;
        goto out;
    }
    co.ret = drv->bdrv_co_zone_report(bs, offset, nr_zones, zones);
out:
    bdrv_dec_in_flight(bs);
    return co.ret;
}

int coroutine_fn bdrv_co_zone_mgmt(BlockDriverState *bs, BlockZoneOp op,
        int64_t offset, int64_t len)
{
    BlockDriver *drv = bs->drv;
    CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
    };
    IO_CODE();

    bdrv_inc_in_flight(bs);
    if (!drv || !drv->bdrv_co_zone_mgmt || bs->bl.zoned == BLK_Z_NONE) {
        co.ret = -ENOTSUP;
        goto out;
    }
    co.ret = drv->bdrv_co_zone_mgmt(bs, op, offset, len);
out:
    bdrv_dec_in_flight(bs);
    return co.ret;
}

int coroutine_fn bdrv_co_zone_append(BlockDriverState *bs, int64_t *offset,
                        QEMUIOVector *qiov,
                        BdrvRequestFlags flags)
{
    int ret;
    BlockDriver *drv = bs->drv;
    CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
    };
    IO_CODE();

    ret = bdrv_check_qiov_request(*offset, qiov->size, qiov, 0, NULL);
    if (ret < 0) {
        return ret;
    }

    bdrv_inc_in_flight(bs);
    if (!drv || !drv->bdrv_co_zone_append || bs->bl.zoned == BLK_Z_NONE) {
        co.ret = -ENOTSUP;
        goto out;
    }
    co.ret = drv->bdrv_co_zone_append(bs, offset, qiov, flags);
out:
    bdrv_dec_in_flight(bs);
    return co.ret;
}

void *qemu_blockalign(BlockDriverState *bs, size_t size)
{
    IO_CODE();
    return qemu_memalign(bdrv_opt_mem_align(bs), size);
}

void *qemu_blockalign0(BlockDriverState *bs, size_t size)
{
    IO_CODE();
    return memset(qemu_blockalign(bs, size), 0, size);
}

void *qemu_try_blockalign(BlockDriverState *bs, size_t size)
{
    size_t align = bdrv_opt_mem_align(bs);
    IO_CODE();

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
    IO_CODE();

    if (mem) {
        memset(mem, 0, size);
    }

    return mem;
}

/* Helper that undoes bdrv_register_buf() when it fails partway through */
static void GRAPH_RDLOCK
bdrv_register_buf_rollback(BlockDriverState *bs, void *host, size_t size,
                           BdrvChild *final_child)
{
    BdrvChild *child;

    GLOBAL_STATE_CODE();
    assert_bdrv_graph_readable();

    QLIST_FOREACH(child, &bs->children, next) {
        if (child == final_child) {
            break;
        }

        bdrv_unregister_buf(child->bs, host, size);
    }

    if (bs->drv && bs->drv->bdrv_unregister_buf) {
        bs->drv->bdrv_unregister_buf(bs, host, size);
    }
}

bool bdrv_register_buf(BlockDriverState *bs, void *host, size_t size,
                       Error **errp)
{
    BdrvChild *child;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (bs->drv && bs->drv->bdrv_register_buf) {
        if (!bs->drv->bdrv_register_buf(bs, host, size, errp)) {
            return false;
        }
    }
    QLIST_FOREACH(child, &bs->children, next) {
        if (!bdrv_register_buf(child->bs, host, size, errp)) {
            bdrv_register_buf_rollback(bs, host, size, child);
            return false;
        }
    }
    return true;
}

void bdrv_unregister_buf(BlockDriverState *bs, void *host, size_t size)
{
    BdrvChild *child;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (bs->drv && bs->drv->bdrv_unregister_buf) {
        bs->drv->bdrv_unregister_buf(bs, host, size);
    }
    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_unregister_buf(child->bs, host, size);
    }
}

static int coroutine_fn GRAPH_RDLOCK bdrv_co_copy_range_internal(
        BdrvChild *src, int64_t src_offset, BdrvChild *dst,
        int64_t dst_offset, int64_t bytes,
        BdrvRequestFlags read_flags, BdrvRequestFlags write_flags,
        bool recurse_src)
{
    BdrvTrackedRequest req;
    int ret;
    assert_bdrv_graph_readable();

    /* TODO We can support BDRV_REQ_NO_FALLBACK here */
    assert(!(read_flags & BDRV_REQ_NO_FALLBACK));
    assert(!(write_flags & BDRV_REQ_NO_FALLBACK));
    assert(!(read_flags & BDRV_REQ_NO_WAIT));
    assert(!(write_flags & BDRV_REQ_NO_WAIT));

    if (!dst || !dst->bs || !bdrv_co_is_inserted(dst->bs)) {
        return -ENOMEDIUM;
    }
    ret = bdrv_check_request32(dst_offset, bytes, NULL, 0);
    if (ret) {
        return ret;
    }
    if (write_flags & BDRV_REQ_ZERO_WRITE) {
        return bdrv_co_pwrite_zeroes(dst, dst_offset, bytes, write_flags);
    }

    if (!src || !src->bs || !bdrv_co_is_inserted(src->bs)) {
        return -ENOMEDIUM;
    }
    ret = bdrv_check_request32(src_offset, bytes, NULL, 0);
    if (ret) {
        return ret;
    }

    if (!src->bs->drv->bdrv_co_copy_range_from
        || !dst->bs->drv->bdrv_co_copy_range_to
        || src->bs->encrypted || dst->bs->encrypted) {
        return -ENOTSUP;
    }

    if (recurse_src) {
        bdrv_inc_in_flight(src->bs);
        tracked_request_begin(&req, src->bs, src_offset, bytes,
                              BDRV_TRACKED_READ);

        /* BDRV_REQ_SERIALISING is only for write operation */
        assert(!(read_flags & BDRV_REQ_SERIALISING));
        bdrv_wait_serialising_requests(&req);

        ret = src->bs->drv->bdrv_co_copy_range_from(src->bs,
                                                    src, src_offset,
                                                    dst, dst_offset,
                                                    bytes,
                                                    read_flags, write_flags);

        tracked_request_end(&req);
        bdrv_dec_in_flight(src->bs);
    } else {
        bdrv_inc_in_flight(dst->bs);
        tracked_request_begin(&req, dst->bs, dst_offset, bytes,
                              BDRV_TRACKED_WRITE);
        ret = bdrv_co_write_req_prepare(dst, dst_offset, bytes, &req,
                                        write_flags);
        if (!ret) {
            ret = dst->bs->drv->bdrv_co_copy_range_to(dst->bs,
                                                      src, src_offset,
                                                      dst, dst_offset,
                                                      bytes,
                                                      read_flags, write_flags);
        }
        bdrv_co_write_req_finish(dst, dst_offset, bytes, &req, ret);
        tracked_request_end(&req);
        bdrv_dec_in_flight(dst->bs);
    }

    return ret;
}

/* Copy range from @src to @dst.
 *
 * See the comment of bdrv_co_copy_range for the parameter and return value
 * semantics. */
int coroutine_fn bdrv_co_copy_range_from(BdrvChild *src, int64_t src_offset,
                                         BdrvChild *dst, int64_t dst_offset,
                                         int64_t bytes,
                                         BdrvRequestFlags read_flags,
                                         BdrvRequestFlags write_flags)
{
    IO_CODE();
    assert_bdrv_graph_readable();
    trace_bdrv_co_copy_range_from(src, src_offset, dst, dst_offset, bytes,
                                  read_flags, write_flags);
    return bdrv_co_copy_range_internal(src, src_offset, dst, dst_offset,
                                       bytes, read_flags, write_flags, true);
}

/* Copy range from @src to @dst.
 *
 * See the comment of bdrv_co_copy_range for the parameter and return value
 * semantics. */
int coroutine_fn bdrv_co_copy_range_to(BdrvChild *src, int64_t src_offset,
                                       BdrvChild *dst, int64_t dst_offset,
                                       int64_t bytes,
                                       BdrvRequestFlags read_flags,
                                       BdrvRequestFlags write_flags)
{
    IO_CODE();
    assert_bdrv_graph_readable();
    trace_bdrv_co_copy_range_to(src, src_offset, dst, dst_offset, bytes,
                                read_flags, write_flags);
    return bdrv_co_copy_range_internal(src, src_offset, dst, dst_offset,
                                       bytes, read_flags, write_flags, false);
}

int coroutine_fn bdrv_co_copy_range(BdrvChild *src, int64_t src_offset,
                                    BdrvChild *dst, int64_t dst_offset,
                                    int64_t bytes, BdrvRequestFlags read_flags,
                                    BdrvRequestFlags write_flags)
{
    IO_CODE();
    assert_bdrv_graph_readable();

    return bdrv_co_copy_range_from(src, src_offset,
                                   dst, dst_offset,
                                   bytes, read_flags, write_flags);
}

static void bdrv_parent_cb_resize(BlockDriverState *bs)
{
    BdrvChild *c;
    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c->klass->resize) {
            c->klass->resize(c);
        }
    }
}

/**
 * Truncate file to 'offset' bytes (needed only for file protocols)
 *
 * If 'exact' is true, the file must be resized to exactly the given
 * 'offset'.  Otherwise, it is sufficient for the node to be at least
 * 'offset' bytes in length.
 */
int coroutine_fn bdrv_co_truncate(BdrvChild *child, int64_t offset, bool exact,
                                  PreallocMode prealloc, BdrvRequestFlags flags,
                                  Error **errp)
{
    BlockDriverState *bs = child->bs;
    BdrvChild *filtered, *backing;
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest req;
    int64_t old_size, new_bytes;
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    /* if bs->drv == NULL, bs is closed, so there's nothing to do here */
    if (!drv) {
        error_setg(errp, "No medium inserted");
        return -ENOMEDIUM;
    }
    if (offset < 0) {
        error_setg(errp, "Image size cannot be negative");
        return -EINVAL;
    }

    ret = bdrv_check_request(offset, 0, errp);
    if (ret < 0) {
        return ret;
    }

    old_size = bdrv_co_getlength(bs);
    if (old_size < 0) {
        error_setg_errno(errp, -old_size, "Failed to get old image size");
        return old_size;
    }

    if (bdrv_is_read_only(bs)) {
        error_setg(errp, "Image is read-only");
        return -EACCES;
    }

    if (offset > old_size) {
        new_bytes = offset - old_size;
    } else {
        new_bytes = 0;
    }

    bdrv_inc_in_flight(bs);
    tracked_request_begin(&req, bs, offset - new_bytes, new_bytes,
                          BDRV_TRACKED_TRUNCATE);

    /* If we are growing the image and potentially using preallocation for the
     * new area, we need to make sure that no write requests are made to it
     * concurrently or they might be overwritten by preallocation. */
    if (new_bytes) {
        bdrv_make_request_serialising(&req, 1);
    }
    ret = bdrv_co_write_req_prepare(child, offset - new_bytes, new_bytes, &req,
                                    0);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Failed to prepare request for truncation");
        goto out;
    }

    filtered = bdrv_filter_child(bs);
    backing = bdrv_cow_child(bs);

    /*
     * If the image has a backing file that is large enough that it would
     * provide data for the new area, we cannot leave it unallocated because
     * then the backing file content would become visible. Instead, zero-fill
     * the new area.
     *
     * Note that if the image has a backing file, but was opened without the
     * backing file, taking care of keeping things consistent with that backing
     * file is the user's responsibility.
     */
    if (new_bytes && backing) {
        int64_t backing_len;

        backing_len = bdrv_co_getlength(backing->bs);
        if (backing_len < 0) {
            ret = backing_len;
            error_setg_errno(errp, -ret, "Could not get backing file size");
            goto out;
        }

        if (backing_len > old_size) {
            flags |= BDRV_REQ_ZERO_WRITE;
        }
    }

    if (drv->bdrv_co_truncate) {
        if (flags & ~bs->supported_truncate_flags) {
            error_setg(errp, "Block driver does not support requested flags");
            ret = -ENOTSUP;
            goto out;
        }
        ret = drv->bdrv_co_truncate(bs, offset, exact, prealloc, flags, errp);
    } else if (filtered) {
        ret = bdrv_co_truncate(filtered, offset, exact, prealloc, flags, errp);
    } else {
        error_setg(errp, "Image format driver does not support resize");
        ret = -ENOTSUP;
        goto out;
    }
    if (ret < 0) {
        goto out;
    }

    ret = bdrv_co_refresh_total_sectors(bs, offset >> BDRV_SECTOR_BITS);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not refresh total sector count");
    } else {
        offset = bs->total_sectors * BDRV_SECTOR_SIZE;
    }
    /*
     * It's possible that truncation succeeded but bdrv_refresh_total_sectors
     * failed, but the latter doesn't affect how we should finish the request.
     * Pass 0 as the last parameter so that dirty bitmaps etc. are handled.
     */
    bdrv_co_write_req_finish(child, offset - new_bytes, new_bytes, &req, 0);

out:
    tracked_request_end(&req);
    bdrv_dec_in_flight(bs);

    return ret;
}

void bdrv_cancel_in_flight(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();
    if (!bs || !bs->drv) {
        return;
    }

    if (bs->drv->bdrv_cancel_in_flight) {
        bs->drv->bdrv_cancel_in_flight(bs);
    }
}

int coroutine_fn
bdrv_co_preadv_snapshot(BdrvChild *child, int64_t offset, int64_t bytes,
                        QEMUIOVector *qiov, size_t qiov_offset)
{
    BlockDriverState *bs = child->bs;
    BlockDriver *drv = bs->drv;
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (!drv->bdrv_co_preadv_snapshot) {
        return -ENOTSUP;
    }

    bdrv_inc_in_flight(bs);
    ret = drv->bdrv_co_preadv_snapshot(bs, offset, bytes, qiov, qiov_offset);
    bdrv_dec_in_flight(bs);

    return ret;
}

int coroutine_fn
bdrv_co_snapshot_block_status(BlockDriverState *bs,
                              bool want_zero, int64_t offset, int64_t bytes,
                              int64_t *pnum, int64_t *map,
                              BlockDriverState **file)
{
    BlockDriver *drv = bs->drv;
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (!drv->bdrv_co_snapshot_block_status) {
        return -ENOTSUP;
    }

    bdrv_inc_in_flight(bs);
    ret = drv->bdrv_co_snapshot_block_status(bs, want_zero, offset, bytes,
                                             pnum, map, file);
    bdrv_dec_in_flight(bs);

    return ret;
}

int coroutine_fn
bdrv_co_pdiscard_snapshot(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    BlockDriver *drv = bs->drv;
    int ret;
    IO_CODE();
    assert_bdrv_graph_readable();

    if (!drv) {
        return -ENOMEDIUM;
    }

    if (!drv->bdrv_co_pdiscard_snapshot) {
        return -ENOTSUP;
    }

    bdrv_inc_in_flight(bs);
    ret = drv->bdrv_co_pdiscard_snapshot(bs, offset, bytes);
    bdrv_dec_in_flight(bs);

    return ret;
}
