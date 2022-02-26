/*
 * Image mirroring
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Paolo Bonzini  <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/coroutine.h"
#include "qemu/range.h"
#include "trace.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/bitmap.h"
#include "qemu/memalign.h"

#define MAX_IN_FLIGHT 16
#define MAX_IO_BYTES (1 << 20) /* 1 Mb */
#define DEFAULT_MIRROR_BUF_SIZE (MAX_IN_FLIGHT * MAX_IO_BYTES)

/* The mirroring buffer is a list of granularity-sized chunks.
 * Free chunks are organized in a list.
 */
typedef struct MirrorBuffer {
    QSIMPLEQ_ENTRY(MirrorBuffer) next;
} MirrorBuffer;

typedef struct MirrorOp MirrorOp;

typedef struct MirrorBlockJob {
    BlockJob common;
    BlockBackend *target;
    BlockDriverState *mirror_top_bs;
    BlockDriverState *base;
    BlockDriverState *base_overlay;

    /* The name of the graph node to replace */
    char *replaces;
    /* The BDS to replace */
    BlockDriverState *to_replace;
    /* Used to block operations on the drive-mirror-replace target */
    Error *replace_blocker;
    bool is_none_mode;
    BlockMirrorBackingMode backing_mode;
    /* Whether the target image requires explicit zero-initialization */
    bool zero_target;
    MirrorCopyMode copy_mode;
    BlockdevOnError on_source_error, on_target_error;
    /* Set when the target is synced (dirty bitmap is clean, nothing
     * in flight) and the job is running in active mode */
    bool actively_synced;
    bool should_complete;
    int64_t granularity;
    size_t buf_size;
    int64_t bdev_length;
    unsigned long *cow_bitmap;
    BdrvDirtyBitmap *dirty_bitmap;
    BdrvDirtyBitmapIter *dbi;
    uint8_t *buf;
    QSIMPLEQ_HEAD(, MirrorBuffer) buf_free;
    int buf_free_count;

    uint64_t last_pause_ns;
    unsigned long *in_flight_bitmap;
    int in_flight;
    int64_t bytes_in_flight;
    QTAILQ_HEAD(, MirrorOp) ops_in_flight;
    int ret;
    bool unmap;
    int target_cluster_size;
    int max_iov;
    bool initial_zeroing_ongoing;
    int in_active_write_counter;
    bool prepared;
    bool in_drain;
} MirrorBlockJob;

typedef struct MirrorBDSOpaque {
    MirrorBlockJob *job;
    bool stop;
    bool is_commit;
} MirrorBDSOpaque;

struct MirrorOp {
    MirrorBlockJob *s;
    QEMUIOVector qiov;
    int64_t offset;
    uint64_t bytes;

    /* The pointee is set by mirror_co_read(), mirror_co_zero(), and
     * mirror_co_discard() before yielding for the first time */
    int64_t *bytes_handled;

    bool is_pseudo_op;
    bool is_active_write;
    bool is_in_flight;
    CoQueue waiting_requests;
    Coroutine *co;
    MirrorOp *waiting_for_op;

    QTAILQ_ENTRY(MirrorOp) next;
};

typedef enum MirrorMethod {
    MIRROR_METHOD_COPY,
    MIRROR_METHOD_ZERO,
    MIRROR_METHOD_DISCARD,
} MirrorMethod;

static BlockErrorAction mirror_error_action(MirrorBlockJob *s, bool read,
                                            int error)
{
    s->actively_synced = false;
    if (read) {
        return block_job_error_action(&s->common, s->on_source_error,
                                      true, error);
    } else {
        return block_job_error_action(&s->common, s->on_target_error,
                                      false, error);
    }
}

static void coroutine_fn mirror_wait_on_conflicts(MirrorOp *self,
                                                  MirrorBlockJob *s,
                                                  uint64_t offset,
                                                  uint64_t bytes)
{
    uint64_t self_start_chunk = offset / s->granularity;
    uint64_t self_end_chunk = DIV_ROUND_UP(offset + bytes, s->granularity);
    uint64_t self_nb_chunks = self_end_chunk - self_start_chunk;

    while (find_next_bit(s->in_flight_bitmap, self_end_chunk,
                         self_start_chunk) < self_end_chunk &&
           s->ret >= 0)
    {
        MirrorOp *op;

        QTAILQ_FOREACH(op, &s->ops_in_flight, next) {
            uint64_t op_start_chunk = op->offset / s->granularity;
            uint64_t op_nb_chunks = DIV_ROUND_UP(op->offset + op->bytes,
                                                 s->granularity) -
                                    op_start_chunk;

            if (op == self) {
                continue;
            }

            if (ranges_overlap(self_start_chunk, self_nb_chunks,
                               op_start_chunk, op_nb_chunks))
            {
                if (self) {
                    /*
                     * If the operation is already (indirectly) waiting for us,
                     * or will wait for us as soon as it wakes up, then just go
                     * on (instead of producing a deadlock in the former case).
                     */
                    if (op->waiting_for_op) {
                        continue;
                    }

                    self->waiting_for_op = op;
                }

                qemu_co_queue_wait(&op->waiting_requests, NULL);

                if (self) {
                    self->waiting_for_op = NULL;
                }

                break;
            }
        }
    }
}

static void coroutine_fn mirror_iteration_done(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;
    struct iovec *iov;
    int64_t chunk_num;
    int i, nb_chunks;

    trace_mirror_iteration_done(s, op->offset, op->bytes, ret);

    s->in_flight--;
    s->bytes_in_flight -= op->bytes;
    iov = op->qiov.iov;
    for (i = 0; i < op->qiov.niov; i++) {
        MirrorBuffer *buf = (MirrorBuffer *) iov[i].iov_base;
        QSIMPLEQ_INSERT_TAIL(&s->buf_free, buf, next);
        s->buf_free_count++;
    }

    chunk_num = op->offset / s->granularity;
    nb_chunks = DIV_ROUND_UP(op->bytes, s->granularity);

    bitmap_clear(s->in_flight_bitmap, chunk_num, nb_chunks);
    QTAILQ_REMOVE(&s->ops_in_flight, op, next);
    if (ret >= 0) {
        if (s->cow_bitmap) {
            bitmap_set(s->cow_bitmap, chunk_num, nb_chunks);
        }
        if (!s->initial_zeroing_ongoing) {
            job_progress_update(&s->common.job, op->bytes);
        }
    }
    qemu_iovec_destroy(&op->qiov);

    qemu_co_queue_restart_all(&op->waiting_requests);
    g_free(op);
}

static void coroutine_fn mirror_write_complete(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;

    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->offset, op->bytes);
        action = mirror_error_action(s, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }
    }

    mirror_iteration_done(op, ret);
}

static void coroutine_fn mirror_read_complete(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;

    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->offset, op->bytes);
        action = mirror_error_action(s, true, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }

        mirror_iteration_done(op, ret);
        return;
    }

    ret = blk_co_pwritev(s->target, op->offset, op->qiov.size, &op->qiov, 0);
    mirror_write_complete(op, ret);
}

/* Clip bytes relative to offset to not exceed end-of-file */
static inline int64_t mirror_clip_bytes(MirrorBlockJob *s,
                                        int64_t offset,
                                        int64_t bytes)
{
    return MIN(bytes, s->bdev_length - offset);
}

/* Round offset and/or bytes to target cluster if COW is needed, and
 * return the offset of the adjusted tail against original. */
static int mirror_cow_align(MirrorBlockJob *s, int64_t *offset,
                            uint64_t *bytes)
{
    bool need_cow;
    int ret = 0;
    int64_t align_offset = *offset;
    int64_t align_bytes = *bytes;
    int max_bytes = s->granularity * s->max_iov;

    need_cow = !test_bit(*offset / s->granularity, s->cow_bitmap);
    need_cow |= !test_bit((*offset + *bytes - 1) / s->granularity,
                          s->cow_bitmap);
    if (need_cow) {
        bdrv_round_to_clusters(blk_bs(s->target), *offset, *bytes,
                               &align_offset, &align_bytes);
    }

    if (align_bytes > max_bytes) {
        align_bytes = max_bytes;
        if (need_cow) {
            align_bytes = QEMU_ALIGN_DOWN(align_bytes, s->target_cluster_size);
        }
    }
    /* Clipping may result in align_bytes unaligned to chunk boundary, but
     * that doesn't matter because it's already the end of source image. */
    align_bytes = mirror_clip_bytes(s, align_offset, align_bytes);

    ret = align_offset + align_bytes - (*offset + *bytes);
    *offset = align_offset;
    *bytes = align_bytes;
    assert(ret >= 0);
    return ret;
}

static inline void coroutine_fn
mirror_wait_for_any_operation(MirrorBlockJob *s, bool active)
{
    MirrorOp *op;

    QTAILQ_FOREACH(op, &s->ops_in_flight, next) {
        /* Do not wait on pseudo ops, because it may in turn wait on
         * some other operation to start, which may in fact be the
         * caller of this function.  Since there is only one pseudo op
         * at any given time, we will always find some real operation
         * to wait on. */
        if (!op->is_pseudo_op && op->is_in_flight &&
            op->is_active_write == active)
        {
            qemu_co_queue_wait(&op->waiting_requests, NULL);
            return;
        }
    }
    abort();
}

static inline void coroutine_fn
mirror_wait_for_free_in_flight_slot(MirrorBlockJob *s)
{
    /* Only non-active operations use up in-flight slots */
    mirror_wait_for_any_operation(s, false);
}

/* Perform a mirror copy operation.
 *
 * *op->bytes_handled is set to the number of bytes copied after and
 * including offset, excluding any bytes copied prior to offset due
 * to alignment.  This will be op->bytes if no alignment is necessary,
 * or (new_end - op->offset) if the tail is rounded up or down due to
 * alignment or buffer limit.
 */
static void coroutine_fn mirror_co_read(void *opaque)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;
    int nb_chunks;
    uint64_t ret;
    uint64_t max_bytes;

    max_bytes = s->granularity * s->max_iov;

    /* We can only handle as much as buf_size at a time. */
    op->bytes = MIN(s->buf_size, MIN(max_bytes, op->bytes));
    assert(op->bytes);
    assert(op->bytes < BDRV_REQUEST_MAX_BYTES);
    *op->bytes_handled = op->bytes;

    if (s->cow_bitmap) {
        *op->bytes_handled += mirror_cow_align(s, &op->offset, &op->bytes);
    }
    /* Cannot exceed BDRV_REQUEST_MAX_BYTES + INT_MAX */
    assert(*op->bytes_handled <= UINT_MAX);
    assert(op->bytes <= s->buf_size);
    /* The offset is granularity-aligned because:
     * 1) Caller passes in aligned values;
     * 2) mirror_cow_align is used only when target cluster is larger. */
    assert(QEMU_IS_ALIGNED(op->offset, s->granularity));
    /* The range is sector-aligned, since bdrv_getlength() rounds up. */
    assert(QEMU_IS_ALIGNED(op->bytes, BDRV_SECTOR_SIZE));
    nb_chunks = DIV_ROUND_UP(op->bytes, s->granularity);

    while (s->buf_free_count < nb_chunks) {
        trace_mirror_yield_in_flight(s, op->offset, s->in_flight);
        mirror_wait_for_free_in_flight_slot(s);
    }

    /* Now make a QEMUIOVector taking enough granularity-sized chunks
     * from s->buf_free.
     */
    qemu_iovec_init(&op->qiov, nb_chunks);
    while (nb_chunks-- > 0) {
        MirrorBuffer *buf = QSIMPLEQ_FIRST(&s->buf_free);
        size_t remaining = op->bytes - op->qiov.size;

        QSIMPLEQ_REMOVE_HEAD(&s->buf_free, next);
        s->buf_free_count--;
        qemu_iovec_add(&op->qiov, buf, MIN(s->granularity, remaining));
    }

    /* Copy the dirty cluster.  */
    s->in_flight++;
    s->bytes_in_flight += op->bytes;
    op->is_in_flight = true;
    trace_mirror_one_iteration(s, op->offset, op->bytes);

    ret = bdrv_co_preadv(s->mirror_top_bs->backing, op->offset, op->bytes,
                         &op->qiov, 0);
    mirror_read_complete(op, ret);
}

static void coroutine_fn mirror_co_zero(void *opaque)
{
    MirrorOp *op = opaque;
    int ret;

    op->s->in_flight++;
    op->s->bytes_in_flight += op->bytes;
    *op->bytes_handled = op->bytes;
    op->is_in_flight = true;

    ret = blk_co_pwrite_zeroes(op->s->target, op->offset, op->bytes,
                               op->s->unmap ? BDRV_REQ_MAY_UNMAP : 0);
    mirror_write_complete(op, ret);
}

static void coroutine_fn mirror_co_discard(void *opaque)
{
    MirrorOp *op = opaque;
    int ret;

    op->s->in_flight++;
    op->s->bytes_in_flight += op->bytes;
    *op->bytes_handled = op->bytes;
    op->is_in_flight = true;

    ret = blk_co_pdiscard(op->s->target, op->offset, op->bytes);
    mirror_write_complete(op, ret);
}

static unsigned mirror_perform(MirrorBlockJob *s, int64_t offset,
                               unsigned bytes, MirrorMethod mirror_method)
{
    MirrorOp *op;
    Coroutine *co;
    int64_t bytes_handled = -1;

    op = g_new(MirrorOp, 1);
    *op = (MirrorOp){
        .s              = s,
        .offset         = offset,
        .bytes          = bytes,
        .bytes_handled  = &bytes_handled,
    };
    qemu_co_queue_init(&op->waiting_requests);

    switch (mirror_method) {
    case MIRROR_METHOD_COPY:
        co = qemu_coroutine_create(mirror_co_read, op);
        break;
    case MIRROR_METHOD_ZERO:
        co = qemu_coroutine_create(mirror_co_zero, op);
        break;
    case MIRROR_METHOD_DISCARD:
        co = qemu_coroutine_create(mirror_co_discard, op);
        break;
    default:
        abort();
    }
    op->co = co;

    QTAILQ_INSERT_TAIL(&s->ops_in_flight, op, next);
    qemu_coroutine_enter(co);
    /* At this point, ownership of op has been moved to the coroutine
     * and the object may already be freed */

    /* Assert that this value has been set */
    assert(bytes_handled >= 0);

    /* Same assertion as in mirror_co_read() (and for mirror_co_read()
     * and mirror_co_discard(), bytes_handled == op->bytes, which
     * is the @bytes parameter given to this function) */
    assert(bytes_handled <= UINT_MAX);
    return bytes_handled;
}

static uint64_t coroutine_fn mirror_iteration(MirrorBlockJob *s)
{
    BlockDriverState *source = s->mirror_top_bs->backing->bs;
    MirrorOp *pseudo_op;
    int64_t offset;
    uint64_t delay_ns = 0, ret = 0;
    /* At least the first dirty chunk is mirrored in one iteration. */
    int nb_chunks = 1;
    bool write_zeroes_ok = bdrv_can_write_zeroes_with_unmap(blk_bs(s->target));
    int max_io_bytes = MAX(s->buf_size / MAX_IN_FLIGHT, MAX_IO_BYTES);

    bdrv_dirty_bitmap_lock(s->dirty_bitmap);
    offset = bdrv_dirty_iter_next(s->dbi);
    if (offset < 0) {
        bdrv_set_dirty_iter(s->dbi, 0);
        offset = bdrv_dirty_iter_next(s->dbi);
        trace_mirror_restart_iter(s, bdrv_get_dirty_count(s->dirty_bitmap));
        assert(offset >= 0);
    }
    bdrv_dirty_bitmap_unlock(s->dirty_bitmap);

    mirror_wait_on_conflicts(NULL, s, offset, 1);

    job_pause_point(&s->common.job);

    /* Find the number of consective dirty chunks following the first dirty
     * one, and wait for in flight requests in them. */
    bdrv_dirty_bitmap_lock(s->dirty_bitmap);
    while (nb_chunks * s->granularity < s->buf_size) {
        int64_t next_dirty;
        int64_t next_offset = offset + nb_chunks * s->granularity;
        int64_t next_chunk = next_offset / s->granularity;
        if (next_offset >= s->bdev_length ||
            !bdrv_dirty_bitmap_get_locked(s->dirty_bitmap, next_offset)) {
            break;
        }
        if (test_bit(next_chunk, s->in_flight_bitmap)) {
            break;
        }

        next_dirty = bdrv_dirty_iter_next(s->dbi);
        if (next_dirty > next_offset || next_dirty < 0) {
            /* The bitmap iterator's cache is stale, refresh it */
            bdrv_set_dirty_iter(s->dbi, next_offset);
            next_dirty = bdrv_dirty_iter_next(s->dbi);
        }
        assert(next_dirty == next_offset);
        nb_chunks++;
    }

    /* Clear dirty bits before querying the block status, because
     * calling bdrv_block_status_above could yield - if some blocks are
     * marked dirty in this window, we need to know.
     */
    bdrv_reset_dirty_bitmap_locked(s->dirty_bitmap, offset,
                                   nb_chunks * s->granularity);
    bdrv_dirty_bitmap_unlock(s->dirty_bitmap);

    /* Before claiming an area in the in-flight bitmap, we have to
     * create a MirrorOp for it so that conflicting requests can wait
     * for it.  mirror_perform() will create the real MirrorOps later,
     * for now we just create a pseudo operation that will wake up all
     * conflicting requests once all real operations have been
     * launched. */
    pseudo_op = g_new(MirrorOp, 1);
    *pseudo_op = (MirrorOp){
        .offset         = offset,
        .bytes          = nb_chunks * s->granularity,
        .is_pseudo_op   = true,
    };
    qemu_co_queue_init(&pseudo_op->waiting_requests);
    QTAILQ_INSERT_TAIL(&s->ops_in_flight, pseudo_op, next);

    bitmap_set(s->in_flight_bitmap, offset / s->granularity, nb_chunks);
    while (nb_chunks > 0 && offset < s->bdev_length) {
        int ret;
        int64_t io_bytes;
        int64_t io_bytes_acct;
        MirrorMethod mirror_method = MIRROR_METHOD_COPY;

        assert(!(offset % s->granularity));
        ret = bdrv_block_status_above(source, NULL, offset,
                                      nb_chunks * s->granularity,
                                      &io_bytes, NULL, NULL);
        if (ret < 0) {
            io_bytes = MIN(nb_chunks * s->granularity, max_io_bytes);
        } else if (ret & BDRV_BLOCK_DATA) {
            io_bytes = MIN(io_bytes, max_io_bytes);
        }

        io_bytes -= io_bytes % s->granularity;
        if (io_bytes < s->granularity) {
            io_bytes = s->granularity;
        } else if (ret >= 0 && !(ret & BDRV_BLOCK_DATA)) {
            int64_t target_offset;
            int64_t target_bytes;
            bdrv_round_to_clusters(blk_bs(s->target), offset, io_bytes,
                                   &target_offset, &target_bytes);
            if (target_offset == offset &&
                target_bytes == io_bytes) {
                mirror_method = ret & BDRV_BLOCK_ZERO ?
                                    MIRROR_METHOD_ZERO :
                                    MIRROR_METHOD_DISCARD;
            }
        }

        while (s->in_flight >= MAX_IN_FLIGHT) {
            trace_mirror_yield_in_flight(s, offset, s->in_flight);
            mirror_wait_for_free_in_flight_slot(s);
        }

        if (s->ret < 0) {
            ret = 0;
            goto fail;
        }

        io_bytes = mirror_clip_bytes(s, offset, io_bytes);
        io_bytes = mirror_perform(s, offset, io_bytes, mirror_method);
        if (mirror_method != MIRROR_METHOD_COPY && write_zeroes_ok) {
            io_bytes_acct = 0;
        } else {
            io_bytes_acct = io_bytes;
        }
        assert(io_bytes);
        offset += io_bytes;
        nb_chunks -= DIV_ROUND_UP(io_bytes, s->granularity);
        delay_ns = block_job_ratelimit_get_delay(&s->common, io_bytes_acct);
    }

    ret = delay_ns;
fail:
    QTAILQ_REMOVE(&s->ops_in_flight, pseudo_op, next);
    qemu_co_queue_restart_all(&pseudo_op->waiting_requests);
    g_free(pseudo_op);

    return ret;
}

static void mirror_free_init(MirrorBlockJob *s)
{
    int granularity = s->granularity;
    size_t buf_size = s->buf_size;
    uint8_t *buf = s->buf;

    assert(s->buf_free_count == 0);
    QSIMPLEQ_INIT(&s->buf_free);
    while (buf_size != 0) {
        MirrorBuffer *cur = (MirrorBuffer *)buf;
        QSIMPLEQ_INSERT_TAIL(&s->buf_free, cur, next);
        s->buf_free_count++;
        buf_size -= granularity;
        buf += granularity;
    }
}

/* This is also used for the .pause callback. There is no matching
 * mirror_resume() because mirror_run() will begin iterating again
 * when the job is resumed.
 */
static void coroutine_fn mirror_wait_for_all_io(MirrorBlockJob *s)
{
    while (s->in_flight > 0) {
        mirror_wait_for_free_in_flight_slot(s);
    }
}

/**
 * mirror_exit_common: handle both abort() and prepare() cases.
 * for .prepare, returns 0 on success and -errno on failure.
 * for .abort cases, denoted by abort = true, MUST return 0.
 */
static int mirror_exit_common(Job *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockJob *bjob = &s->common;
    MirrorBDSOpaque *bs_opaque;
    AioContext *replace_aio_context = NULL;
    BlockDriverState *src;
    BlockDriverState *target_bs;
    BlockDriverState *mirror_top_bs;
    Error *local_err = NULL;
    bool abort = job->ret < 0;
    int ret = 0;

    if (s->prepared) {
        return 0;
    }
    s->prepared = true;

    mirror_top_bs = s->mirror_top_bs;
    bs_opaque = mirror_top_bs->opaque;
    src = mirror_top_bs->backing->bs;
    target_bs = blk_bs(s->target);

    if (bdrv_chain_contains(src, target_bs)) {
        bdrv_unfreeze_backing_chain(mirror_top_bs, target_bs);
    }

    bdrv_release_dirty_bitmap(s->dirty_bitmap);

    /* Make sure that the source BDS doesn't go away during bdrv_replace_node,
     * before we can call bdrv_drained_end */
    bdrv_ref(src);
    bdrv_ref(mirror_top_bs);
    bdrv_ref(target_bs);

    /*
     * Remove target parent that still uses BLK_PERM_WRITE/RESIZE before
     * inserting target_bs at s->to_replace, where we might not be able to get
     * these permissions.
     */
    blk_unref(s->target);
    s->target = NULL;

    /* We don't access the source any more. Dropping any WRITE/RESIZE is
     * required before it could become a backing file of target_bs. Not having
     * these permissions any more means that we can't allow any new requests on
     * mirror_top_bs from now on, so keep it drained. */
    bdrv_drained_begin(mirror_top_bs);
    bs_opaque->stop = true;
    bdrv_child_refresh_perms(mirror_top_bs, mirror_top_bs->backing,
                             &error_abort);
    if (!abort && s->backing_mode == MIRROR_SOURCE_BACKING_CHAIN) {
        BlockDriverState *backing = s->is_none_mode ? src : s->base;
        BlockDriverState *unfiltered_target = bdrv_skip_filters(target_bs);

        if (bdrv_cow_bs(unfiltered_target) != backing) {
            bdrv_set_backing_hd(unfiltered_target, backing, &local_err);
            if (local_err) {
                error_report_err(local_err);
                local_err = NULL;
                ret = -EPERM;
            }
        }
    } else if (!abort && s->backing_mode == MIRROR_OPEN_BACKING_CHAIN) {
        assert(!bdrv_backing_chain_next(target_bs));
        ret = bdrv_open_backing_file(bdrv_skip_filters(target_bs), NULL,
                                     "backing", &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            local_err = NULL;
        }
    }

    if (s->to_replace) {
        replace_aio_context = bdrv_get_aio_context(s->to_replace);
        aio_context_acquire(replace_aio_context);
    }

    if (s->should_complete && !abort) {
        BlockDriverState *to_replace = s->to_replace ?: src;
        bool ro = bdrv_is_read_only(to_replace);

        if (ro != bdrv_is_read_only(target_bs)) {
            bdrv_reopen_set_read_only(target_bs, ro, NULL);
        }

        /* The mirror job has no requests in flight any more, but we need to
         * drain potential other users of the BDS before changing the graph. */
        assert(s->in_drain);
        bdrv_drained_begin(target_bs);
        /*
         * Cannot use check_to_replace_node() here, because that would
         * check for an op blocker on @to_replace, and we have our own
         * there.
         */
        if (bdrv_recurse_can_replace(src, to_replace)) {
            bdrv_replace_node(to_replace, target_bs, &local_err);
        } else {
            error_setg(&local_err, "Can no longer replace '%s' by '%s', "
                       "because it can no longer be guaranteed that doing so "
                       "would not lead to an abrupt change of visible data",
                       to_replace->node_name, target_bs->node_name);
        }
        bdrv_drained_end(target_bs);
        if (local_err) {
            error_report_err(local_err);
            ret = -EPERM;
        }
    }
    if (s->to_replace) {
        bdrv_op_unblock_all(s->to_replace, s->replace_blocker);
        error_free(s->replace_blocker);
        bdrv_unref(s->to_replace);
    }
    if (replace_aio_context) {
        aio_context_release(replace_aio_context);
    }
    g_free(s->replaces);
    bdrv_unref(target_bs);

    /*
     * Remove the mirror filter driver from the graph. Before this, get rid of
     * the blockers on the intermediate nodes so that the resulting state is
     * valid.
     */
    block_job_remove_all_bdrv(bjob);
    bdrv_replace_node(mirror_top_bs, mirror_top_bs->backing->bs, &error_abort);

    bs_opaque->job = NULL;

    bdrv_drained_end(src);
    bdrv_drained_end(mirror_top_bs);
    s->in_drain = false;
    bdrv_unref(mirror_top_bs);
    bdrv_unref(src);

    return ret;
}

static int mirror_prepare(Job *job)
{
    return mirror_exit_common(job);
}

static void mirror_abort(Job *job)
{
    int ret = mirror_exit_common(job);
    assert(ret == 0);
}

static void coroutine_fn mirror_throttle(MirrorBlockJob *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    if (now - s->last_pause_ns > BLOCK_JOB_SLICE_TIME) {
        s->last_pause_ns = now;
        job_sleep_ns(&s->common.job, 0);
    } else {
        job_pause_point(&s->common.job);
    }
}

static int coroutine_fn mirror_dirty_init(MirrorBlockJob *s)
{
    int64_t offset;
    BlockDriverState *bs = s->mirror_top_bs->backing->bs;
    BlockDriverState *target_bs = blk_bs(s->target);
    int ret;
    int64_t count;

    if (s->zero_target) {
        if (!bdrv_can_write_zeroes_with_unmap(target_bs)) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, 0, s->bdev_length);
            return 0;
        }

        s->initial_zeroing_ongoing = true;
        for (offset = 0; offset < s->bdev_length; ) {
            int bytes = MIN(s->bdev_length - offset,
                            QEMU_ALIGN_DOWN(INT_MAX, s->granularity));

            mirror_throttle(s);

            if (job_is_cancelled(&s->common.job)) {
                s->initial_zeroing_ongoing = false;
                return 0;
            }

            if (s->in_flight >= MAX_IN_FLIGHT) {
                trace_mirror_yield(s, UINT64_MAX, s->buf_free_count,
                                   s->in_flight);
                mirror_wait_for_free_in_flight_slot(s);
                continue;
            }

            mirror_perform(s, offset, bytes, MIRROR_METHOD_ZERO);
            offset += bytes;
        }

        mirror_wait_for_all_io(s);
        s->initial_zeroing_ongoing = false;
    }

    /* First part, loop on the sectors and initialize the dirty bitmap.  */
    for (offset = 0; offset < s->bdev_length; ) {
        /* Just to make sure we are not exceeding int limit. */
        int bytes = MIN(s->bdev_length - offset,
                        QEMU_ALIGN_DOWN(INT_MAX, s->granularity));

        mirror_throttle(s);

        if (job_is_cancelled(&s->common.job)) {
            return 0;
        }

        ret = bdrv_is_allocated_above(bs, s->base_overlay, true, offset, bytes,
                                      &count);
        if (ret < 0) {
            return ret;
        }

        assert(count);
        if (ret > 0) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, offset, count);
        }
        offset += count;
    }
    return 0;
}

/* Called when going out of the streaming phase to flush the bulk of the
 * data to the medium, or just before completing.
 */
static int mirror_flush(MirrorBlockJob *s)
{
    int ret = blk_flush(s->target);
    if (ret < 0) {
        if (mirror_error_action(s, false, -ret) == BLOCK_ERROR_ACTION_REPORT) {
            s->ret = ret;
        }
    }
    return ret;
}

static int coroutine_fn mirror_run(Job *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockDriverState *bs = s->mirror_top_bs->backing->bs;
    BlockDriverState *target_bs = blk_bs(s->target);
    bool need_drain = true;
    int64_t length;
    int64_t target_length;
    BlockDriverInfo bdi;
    char backing_filename[2]; /* we only need 2 characters because we are only
                                 checking for a NULL string */
    int ret = 0;

    if (job_is_cancelled(&s->common.job)) {
        goto immediate_exit;
    }

    s->bdev_length = bdrv_getlength(bs);
    if (s->bdev_length < 0) {
        ret = s->bdev_length;
        goto immediate_exit;
    }

    target_length = blk_getlength(s->target);
    if (target_length < 0) {
        ret = target_length;
        goto immediate_exit;
    }

    /* Active commit must resize the base image if its size differs from the
     * active layer. */
    if (s->base == blk_bs(s->target)) {
        if (s->bdev_length > target_length) {
            ret = blk_truncate(s->target, s->bdev_length, false,
                               PREALLOC_MODE_OFF, 0, NULL);
            if (ret < 0) {
                goto immediate_exit;
            }
        }
    } else if (s->bdev_length != target_length) {
        error_setg(errp, "Source and target image have different sizes");
        ret = -EINVAL;
        goto immediate_exit;
    }

    if (s->bdev_length == 0) {
        /* Transition to the READY state and wait for complete. */
        job_transition_to_ready(&s->common.job);
        s->actively_synced = true;
        while (!job_cancel_requested(&s->common.job) && !s->should_complete) {
            job_yield(&s->common.job);
        }
        goto immediate_exit;
    }

    length = DIV_ROUND_UP(s->bdev_length, s->granularity);
    s->in_flight_bitmap = bitmap_new(length);

    /* If we have no backing file yet in the destination, we cannot let
     * the destination do COW.  Instead, we copy sectors around the
     * dirty data if needed.  We need a bitmap to do that.
     */
    bdrv_get_backing_filename(target_bs, backing_filename,
                              sizeof(backing_filename));
    if (!bdrv_get_info(target_bs, &bdi) && bdi.cluster_size) {
        s->target_cluster_size = bdi.cluster_size;
    } else {
        s->target_cluster_size = BDRV_SECTOR_SIZE;
    }
    if (backing_filename[0] && !bdrv_backing_chain_next(target_bs) &&
        s->granularity < s->target_cluster_size) {
        s->buf_size = MAX(s->buf_size, s->target_cluster_size);
        s->cow_bitmap = bitmap_new(length);
    }
    s->max_iov = MIN(bs->bl.max_iov, target_bs->bl.max_iov);

    s->buf = qemu_try_blockalign(bs, s->buf_size);
    if (s->buf == NULL) {
        ret = -ENOMEM;
        goto immediate_exit;
    }

    mirror_free_init(s);

    s->last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (!s->is_none_mode) {
        ret = mirror_dirty_init(s);
        if (ret < 0 || job_is_cancelled(&s->common.job)) {
            goto immediate_exit;
        }
    }

    assert(!s->dbi);
    s->dbi = bdrv_dirty_iter_new(s->dirty_bitmap);
    for (;;) {
        uint64_t delay_ns = 0;
        int64_t cnt, delta;
        bool should_complete;

        /* Do not start passive operations while there are active
         * writes in progress */
        while (s->in_active_write_counter) {
            mirror_wait_for_any_operation(s, true);
        }

        if (s->ret < 0) {
            ret = s->ret;
            goto immediate_exit;
        }

        job_pause_point(&s->common.job);

        if (job_is_cancelled(&s->common.job)) {
            ret = 0;
            goto immediate_exit;
        }

        cnt = bdrv_get_dirty_count(s->dirty_bitmap);
        /* cnt is the number of dirty bytes remaining and s->bytes_in_flight is
         * the number of bytes currently being processed; together those are
         * the current remaining operation length */
        job_progress_set_remaining(&s->common.job, s->bytes_in_flight + cnt);

        /* Note that even when no rate limit is applied we need to yield
         * periodically with no pending I/O so that bdrv_drain_all() returns.
         * We do so every BLKOCK_JOB_SLICE_TIME nanoseconds, or when there is
         * an error, or when the source is clean, whichever comes first. */
        delta = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s->last_pause_ns;
        if (delta < BLOCK_JOB_SLICE_TIME &&
            s->common.iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
            if (s->in_flight >= MAX_IN_FLIGHT || s->buf_free_count == 0 ||
                (cnt == 0 && s->in_flight > 0)) {
                trace_mirror_yield(s, cnt, s->buf_free_count, s->in_flight);
                mirror_wait_for_free_in_flight_slot(s);
                continue;
            } else if (cnt != 0) {
                delay_ns = mirror_iteration(s);
            }
        }

        should_complete = false;
        if (s->in_flight == 0 && cnt == 0) {
            trace_mirror_before_flush(s);
            if (!job_is_ready(&s->common.job)) {
                if (mirror_flush(s) < 0) {
                    /* Go check s->ret.  */
                    continue;
                }
                /* We're out of the streaming phase.  From now on, if the job
                 * is cancelled we will actually complete all pending I/O and
                 * report completion.  This way, block-job-cancel will leave
                 * the target in a consistent state.
                 */
                job_transition_to_ready(&s->common.job);
                if (s->copy_mode != MIRROR_COPY_MODE_BACKGROUND) {
                    s->actively_synced = true;
                }
            }

            should_complete = s->should_complete ||
                job_cancel_requested(&s->common.job);
            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
        }

        if (cnt == 0 && should_complete) {
            /* The dirty bitmap is not updated while operations are pending.
             * If we're about to exit, wait for pending operations before
             * calling bdrv_get_dirty_count(bs), or we may exit while the
             * source has dirty data to copy!
             *
             * Note that I/O can be submitted by the guest while
             * mirror_populate runs, so pause it now.  Before deciding
             * whether to switch to target check one last time if I/O has
             * come in the meanwhile, and if not flush the data to disk.
             */
            trace_mirror_before_drain(s, cnt);

            s->in_drain = true;
            bdrv_drained_begin(bs);
            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
            if (cnt > 0 || mirror_flush(s) < 0) {
                bdrv_drained_end(bs);
                s->in_drain = false;
                continue;
            }

            /* The two disks are in sync.  Exit and report successful
             * completion.
             */
            assert(QLIST_EMPTY(&bs->tracked_requests));
            need_drain = false;
            break;
        }

        if (job_is_ready(&s->common.job) && !should_complete) {
            delay_ns = (s->in_flight == 0 &&
                        cnt == 0 ? BLOCK_JOB_SLICE_TIME : 0);
        }
        trace_mirror_before_sleep(s, cnt, job_is_ready(&s->common.job),
                                  delay_ns);
        job_sleep_ns(&s->common.job, delay_ns);
        s->last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

immediate_exit:
    if (s->in_flight > 0) {
        /* We get here only if something went wrong.  Either the job failed,
         * or it was cancelled prematurely so that we do not guarantee that
         * the target is a copy of the source.
         */
        assert(ret < 0 || job_is_cancelled(&s->common.job));
        assert(need_drain);
        mirror_wait_for_all_io(s);
    }

    assert(s->in_flight == 0);
    qemu_vfree(s->buf);
    g_free(s->cow_bitmap);
    g_free(s->in_flight_bitmap);
    bdrv_dirty_iter_free(s->dbi);

    if (need_drain) {
        s->in_drain = true;
        bdrv_drained_begin(bs);
    }

    return ret;
}

static void mirror_complete(Job *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);

    if (!job_is_ready(job)) {
        error_setg(errp, "The active block job '%s' cannot be completed",
                   job->id);
        return;
    }

    /* block all operations on to_replace bs */
    if (s->replaces) {
        AioContext *replace_aio_context;

        s->to_replace = bdrv_find_node(s->replaces);
        if (!s->to_replace) {
            error_setg(errp, "Node name '%s' not found", s->replaces);
            return;
        }

        replace_aio_context = bdrv_get_aio_context(s->to_replace);
        aio_context_acquire(replace_aio_context);

        /* TODO Translate this into child freeze system. */
        error_setg(&s->replace_blocker,
                   "block device is in use by block-job-complete");
        bdrv_op_block_all(s->to_replace, s->replace_blocker);
        bdrv_ref(s->to_replace);

        aio_context_release(replace_aio_context);
    }

    s->should_complete = true;

    /* If the job is paused, it will be re-entered when it is resumed */
    if (!job->paused) {
        job_enter(job);
    }
}

static void coroutine_fn mirror_pause(Job *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);

    mirror_wait_for_all_io(s);
}

static bool mirror_drained_poll(BlockJob *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    /* If the job isn't paused nor cancelled, we can't be sure that it won't
     * issue more requests. We make an exception if we've reached this point
     * from one of our own drain sections, to avoid a deadlock waiting for
     * ourselves.
     */
    if (!s->common.job.paused && !job_is_cancelled(&job->job) && !s->in_drain) {
        return true;
    }

    return !!s->in_flight;
}

static bool mirror_cancel(Job *job, bool force)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common.job);
    BlockDriverState *target = blk_bs(s->target);

    /*
     * Before the job is READY, we treat any cancellation like a
     * force-cancellation.
     */
    force = force || !job_is_ready(job);

    if (force) {
        bdrv_cancel_in_flight(target);
    }
    return force;
}

static bool commit_active_cancel(Job *job, bool force)
{
    /* Same as above in mirror_cancel() */
    return force || !job_is_ready(job);
}

static const BlockJobDriver mirror_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(MirrorBlockJob),
        .job_type               = JOB_TYPE_MIRROR,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = mirror_run,
        .prepare                = mirror_prepare,
        .abort                  = mirror_abort,
        .pause                  = mirror_pause,
        .complete               = mirror_complete,
        .cancel                 = mirror_cancel,
    },
    .drained_poll           = mirror_drained_poll,
};

static const BlockJobDriver commit_active_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(MirrorBlockJob),
        .job_type               = JOB_TYPE_COMMIT,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = mirror_run,
        .prepare                = mirror_prepare,
        .abort                  = mirror_abort,
        .pause                  = mirror_pause,
        .complete               = mirror_complete,
        .cancel                 = commit_active_cancel,
    },
    .drained_poll           = mirror_drained_poll,
};

static void coroutine_fn
do_sync_target_write(MirrorBlockJob *job, MirrorMethod method,
                     uint64_t offset, uint64_t bytes,
                     QEMUIOVector *qiov, int flags)
{
    int ret;
    size_t qiov_offset = 0;
    int64_t bitmap_offset, bitmap_end;

    if (!QEMU_IS_ALIGNED(offset, job->granularity) &&
        bdrv_dirty_bitmap_get(job->dirty_bitmap, offset))
    {
            /*
             * Dirty unaligned padding: ignore it.
             *
             * Reasoning:
             * 1. If we copy it, we can't reset corresponding bit in
             *    dirty_bitmap as there may be some "dirty" bytes still not
             *    copied.
             * 2. It's already dirty, so skipping it we don't diverge mirror
             *    progress.
             *
             * Note, that because of this, guest write may have no contribution
             * into mirror converge, but that's not bad, as we have background
             * process of mirroring. If under some bad circumstances (high guest
             * IO load) background process starve, we will not converge anyway,
             * even if each write will contribute, as guest is not guaranteed to
             * rewrite the whole disk.
             */
            qiov_offset = QEMU_ALIGN_UP(offset, job->granularity) - offset;
            if (bytes <= qiov_offset) {
                /* nothing to do after shrink */
                return;
            }
            offset += qiov_offset;
            bytes -= qiov_offset;
    }

    if (!QEMU_IS_ALIGNED(offset + bytes, job->granularity) &&
        bdrv_dirty_bitmap_get(job->dirty_bitmap, offset + bytes - 1))
    {
        uint64_t tail = (offset + bytes) % job->granularity;

        if (bytes <= tail) {
            /* nothing to do after shrink */
            return;
        }
        bytes -= tail;
    }

    /*
     * Tails are either clean or shrunk, so for bitmap resetting
     * we safely align the range down.
     */
    bitmap_offset = QEMU_ALIGN_UP(offset, job->granularity);
    bitmap_end = QEMU_ALIGN_DOWN(offset + bytes, job->granularity);
    if (bitmap_offset < bitmap_end) {
        bdrv_reset_dirty_bitmap(job->dirty_bitmap, bitmap_offset,
                                bitmap_end - bitmap_offset);
    }

    job_progress_increase_remaining(&job->common.job, bytes);

    switch (method) {
    case MIRROR_METHOD_COPY:
        ret = blk_co_pwritev_part(job->target, offset, bytes,
                                  qiov, qiov_offset, flags);
        break;

    case MIRROR_METHOD_ZERO:
        assert(!qiov);
        ret = blk_co_pwrite_zeroes(job->target, offset, bytes, flags);
        break;

    case MIRROR_METHOD_DISCARD:
        assert(!qiov);
        ret = blk_co_pdiscard(job->target, offset, bytes);
        break;

    default:
        abort();
    }

    if (ret >= 0) {
        job_progress_update(&job->common.job, bytes);
    } else {
        BlockErrorAction action;

        /*
         * We failed, so we should mark dirty the whole area, aligned up.
         * Note that we don't care about shrunk tails if any: they were dirty
         * at function start, and they must be still dirty, as we've locked
         * the region for in-flight op.
         */
        bitmap_offset = QEMU_ALIGN_DOWN(offset, job->granularity);
        bitmap_end = QEMU_ALIGN_UP(offset + bytes, job->granularity);
        bdrv_set_dirty_bitmap(job->dirty_bitmap, bitmap_offset,
                              bitmap_end - bitmap_offset);
        job->actively_synced = false;

        action = mirror_error_action(job, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT) {
            if (!job->ret) {
                job->ret = ret;
            }
        }
    }
}

static MirrorOp *coroutine_fn active_write_prepare(MirrorBlockJob *s,
                                                   uint64_t offset,
                                                   uint64_t bytes)
{
    MirrorOp *op;
    uint64_t start_chunk = offset / s->granularity;
    uint64_t end_chunk = DIV_ROUND_UP(offset + bytes, s->granularity);

    op = g_new(MirrorOp, 1);
    *op = (MirrorOp){
        .s                  = s,
        .offset             = offset,
        .bytes              = bytes,
        .is_active_write    = true,
        .is_in_flight       = true,
        .co                 = qemu_coroutine_self(),
    };
    qemu_co_queue_init(&op->waiting_requests);
    QTAILQ_INSERT_TAIL(&s->ops_in_flight, op, next);

    s->in_active_write_counter++;

    mirror_wait_on_conflicts(op, s, offset, bytes);

    bitmap_set(s->in_flight_bitmap, start_chunk, end_chunk - start_chunk);

    return op;
}

static void coroutine_fn active_write_settle(MirrorOp *op)
{
    uint64_t start_chunk = op->offset / op->s->granularity;
    uint64_t end_chunk = DIV_ROUND_UP(op->offset + op->bytes,
                                      op->s->granularity);

    if (!--op->s->in_active_write_counter && op->s->actively_synced) {
        BdrvChild *source = op->s->mirror_top_bs->backing;

        if (QLIST_FIRST(&source->bs->parents) == source &&
            QLIST_NEXT(source, next_parent) == NULL)
        {
            /* Assert that we are back in sync once all active write
             * operations are settled.
             * Note that we can only assert this if the mirror node
             * is the source node's only parent. */
            assert(!bdrv_get_dirty_count(op->s->dirty_bitmap));
        }
    }
    bitmap_clear(op->s->in_flight_bitmap, start_chunk, end_chunk - start_chunk);
    QTAILQ_REMOVE(&op->s->ops_in_flight, op, next);
    qemu_co_queue_restart_all(&op->waiting_requests);
    g_free(op);
}

static int coroutine_fn bdrv_mirror_top_preadv(BlockDriverState *bs,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn bdrv_mirror_top_do_write(BlockDriverState *bs,
    MirrorMethod method, uint64_t offset, uint64_t bytes, QEMUIOVector *qiov,
    int flags)
{
    MirrorOp *op = NULL;
    MirrorBDSOpaque *s = bs->opaque;
    int ret = 0;
    bool copy_to_target;

    copy_to_target = s->job->ret >= 0 &&
                     !job_is_cancelled(&s->job->common.job) &&
                     s->job->copy_mode == MIRROR_COPY_MODE_WRITE_BLOCKING;

    if (copy_to_target) {
        op = active_write_prepare(s->job, offset, bytes);
    }

    switch (method) {
    case MIRROR_METHOD_COPY:
        ret = bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
        break;

    case MIRROR_METHOD_ZERO:
        ret = bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
        break;

    case MIRROR_METHOD_DISCARD:
        ret = bdrv_co_pdiscard(bs->backing, offset, bytes);
        break;

    default:
        abort();
    }

    if (ret < 0) {
        goto out;
    }

    if (copy_to_target) {
        do_sync_target_write(s->job, method, offset, bytes, qiov, flags);
    }

out:
    if (copy_to_target) {
        active_write_settle(op);
    }
    return ret;
}

static int coroutine_fn bdrv_mirror_top_pwritev(BlockDriverState *bs,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    MirrorBDSOpaque *s = bs->opaque;
    QEMUIOVector bounce_qiov;
    void *bounce_buf;
    int ret = 0;
    bool copy_to_target;

    copy_to_target = s->job->ret >= 0 &&
                     !job_is_cancelled(&s->job->common.job) &&
                     s->job->copy_mode == MIRROR_COPY_MODE_WRITE_BLOCKING;

    if (copy_to_target) {
        /* The guest might concurrently modify the data to write; but
         * the data on source and destination must match, so we have
         * to use a bounce buffer if we are going to write to the
         * target now. */
        bounce_buf = qemu_blockalign(bs, bytes);
        iov_to_buf_full(qiov->iov, qiov->niov, 0, bounce_buf, bytes);

        qemu_iovec_init(&bounce_qiov, 1);
        qemu_iovec_add(&bounce_qiov, bounce_buf, bytes);
        qiov = &bounce_qiov;
    }

    ret = bdrv_mirror_top_do_write(bs, MIRROR_METHOD_COPY, offset, bytes, qiov,
                                   flags);

    if (copy_to_target) {
        qemu_iovec_destroy(&bounce_qiov);
        qemu_vfree(bounce_buf);
    }

    return ret;
}

static int coroutine_fn bdrv_mirror_top_flush(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        /* we can be here after failed bdrv_append in mirror_start_job */
        return 0;
    }
    return bdrv_co_flush(bs->backing->bs);
}

static int coroutine_fn bdrv_mirror_top_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    return bdrv_mirror_top_do_write(bs, MIRROR_METHOD_ZERO, offset, bytes, NULL,
                                    flags);
}

static int coroutine_fn bdrv_mirror_top_pdiscard(BlockDriverState *bs,
    int64_t offset, int64_t bytes)
{
    return bdrv_mirror_top_do_write(bs, MIRROR_METHOD_DISCARD, offset, bytes,
                                    NULL, 0);
}

static void bdrv_mirror_top_refresh_filename(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        /* we can be here after failed bdrv_attach_child in
         * bdrv_set_backing_hd */
        return;
    }
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_mirror_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       BdrvChildRole role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    MirrorBDSOpaque *s = bs->opaque;

    if (s->stop) {
        /*
         * If the job is to be stopped, we do not need to forward
         * anything to the real image.
         */
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
        return;
    }

    bdrv_default_perms(bs, c, role, reopen_queue,
                       perm, shared, nperm, nshared);

    if (s->is_commit) {
        /*
         * For commit jobs, we cannot take CONSISTENT_READ, because
         * that permission is unshared for everything above the base
         * node (except for filters on the base node).
         * We also have to force-share the WRITE permission, or
         * otherwise we would block ourselves at the base node (if
         * writes are blocked for a node, they are also blocked for
         * its backing file).
         * (We could also share RESIZE, because it may be needed for
         * the target if its size is less than the top node's; but
         * bdrv_default_perms_for_cow() automatically shares RESIZE
         * for backing nodes if WRITE is shared, so there is no need
         * to do it here.)
         */
        *nperm &= ~BLK_PERM_CONSISTENT_READ;
        *nshared |= BLK_PERM_WRITE;
    }
}

/* Dummy node that provides consistent read to its users without requiring it
 * from its backing file and that allows writes on the backing file chain. */
static BlockDriver bdrv_mirror_top = {
    .format_name                = "mirror_top",
    .bdrv_co_preadv             = bdrv_mirror_top_preadv,
    .bdrv_co_pwritev            = bdrv_mirror_top_pwritev,
    .bdrv_co_pwrite_zeroes      = bdrv_mirror_top_pwrite_zeroes,
    .bdrv_co_pdiscard           = bdrv_mirror_top_pdiscard,
    .bdrv_co_flush              = bdrv_mirror_top_flush,
    .bdrv_refresh_filename      = bdrv_mirror_top_refresh_filename,
    .bdrv_child_perm            = bdrv_mirror_top_child_perm,

    .is_filter                  = true,
};

static BlockJob *mirror_start_job(
                             const char *job_id, BlockDriverState *bs,
                             int creation_flags, BlockDriverState *target,
                             const char *replaces, int64_t speed,
                             uint32_t granularity, int64_t buf_size,
                             BlockMirrorBackingMode backing_mode,
                             bool zero_target,
                             BlockdevOnError on_source_error,
                             BlockdevOnError on_target_error,
                             bool unmap,
                             BlockCompletionFunc *cb,
                             void *opaque,
                             const BlockJobDriver *driver,
                             bool is_none_mode, BlockDriverState *base,
                             bool auto_complete, const char *filter_node_name,
                             bool is_mirror, MirrorCopyMode copy_mode,
                             Error **errp)
{
    MirrorBlockJob *s;
    MirrorBDSOpaque *bs_opaque;
    BlockDriverState *mirror_top_bs;
    bool target_is_backing;
    uint64_t target_perms, target_shared_perms;
    int ret;

    if (granularity == 0) {
        granularity = bdrv_get_default_bitmap_granularity(target);
    }

    assert(is_power_of_2(granularity));

    if (buf_size < 0) {
        error_setg(errp, "Invalid parameter 'buf-size'");
        return NULL;
    }

    if (buf_size == 0) {
        buf_size = DEFAULT_MIRROR_BUF_SIZE;
    }

    if (bdrv_skip_filters(bs) == bdrv_skip_filters(target)) {
        error_setg(errp, "Can't mirror node into itself");
        return NULL;
    }

    target_is_backing = bdrv_chain_contains(bs, target);

    /* In the case of active commit, add dummy driver to provide consistent
     * reads on the top, while disabling it in the intermediate nodes, and make
     * the backing chain writable. */
    mirror_top_bs = bdrv_new_open_driver(&bdrv_mirror_top, filter_node_name,
                                         BDRV_O_RDWR, errp);
    if (mirror_top_bs == NULL) {
        return NULL;
    }
    if (!filter_node_name) {
        mirror_top_bs->implicit = true;
    }

    /* So that we can always drop this node */
    mirror_top_bs->never_freeze = true;

    mirror_top_bs->total_sectors = bs->total_sectors;
    mirror_top_bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED;
    mirror_top_bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
                                          BDRV_REQ_NO_FALLBACK;
    bs_opaque = g_new0(MirrorBDSOpaque, 1);
    mirror_top_bs->opaque = bs_opaque;

    bs_opaque->is_commit = target_is_backing;

    bdrv_drained_begin(bs);
    ret = bdrv_append(mirror_top_bs, bs, errp);
    bdrv_drained_end(bs);

    if (ret < 0) {
        bdrv_unref(mirror_top_bs);
        return NULL;
    }

    /* Make sure that the source is not resized while the job is running */
    s = block_job_create(job_id, driver, NULL, mirror_top_bs,
                         BLK_PERM_CONSISTENT_READ,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE, speed,
                         creation_flags, cb, opaque, errp);
    if (!s) {
        goto fail;
    }
    bs_opaque->job = s;

    /* The block job now has a reference to this node */
    bdrv_unref(mirror_top_bs);

    s->mirror_top_bs = mirror_top_bs;

    /* No resize for the target either; while the mirror is still running, a
     * consistent read isn't necessarily possible. We could possibly allow
     * writes and graph modifications, though it would likely defeat the
     * purpose of a mirror, so leave them blocked for now.
     *
     * In the case of active commit, things look a bit different, though,
     * because the target is an already populated backing file in active use.
     * We can allow anything except resize there.*/

    target_perms = BLK_PERM_WRITE;
    target_shared_perms = BLK_PERM_WRITE_UNCHANGED;

    if (target_is_backing) {
        int64_t bs_size, target_size;
        bs_size = bdrv_getlength(bs);
        if (bs_size < 0) {
            error_setg_errno(errp, -bs_size,
                             "Could not inquire top image size");
            goto fail;
        }

        target_size = bdrv_getlength(target);
        if (target_size < 0) {
            error_setg_errno(errp, -target_size,
                             "Could not inquire base image size");
            goto fail;
        }

        if (target_size < bs_size) {
            target_perms |= BLK_PERM_RESIZE;
        }

        target_shared_perms |= BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
    } else if (bdrv_chain_contains(bs, bdrv_skip_filters(target))) {
        /*
         * We may want to allow this in the future, but it would
         * require taking some extra care.
         */
        error_setg(errp, "Cannot mirror to a filter on top of a node in the "
                   "source's backing chain");
        goto fail;
    }

    s->target = blk_new(s->common.job.aio_context,
                        target_perms, target_shared_perms);
    ret = blk_insert_bs(s->target, target, errp);
    if (ret < 0) {
        goto fail;
    }
    if (is_mirror) {
        /* XXX: Mirror target could be a NBD server of target QEMU in the case
         * of non-shared block migration. To allow migration completion, we
         * have to allow "inactivate" of the target BB.  When that happens, we
         * know the job is drained, and the vcpus are stopped, so no write
         * operation will be performed. Block layer already has assertions to
         * ensure that. */
        blk_set_force_allow_inactivate(s->target);
    }
    blk_set_allow_aio_context_change(s->target, true);
    blk_set_disable_request_queuing(s->target, true);

    s->replaces = g_strdup(replaces);
    s->on_source_error = on_source_error;
    s->on_target_error = on_target_error;
    s->is_none_mode = is_none_mode;
    s->backing_mode = backing_mode;
    s->zero_target = zero_target;
    s->copy_mode = copy_mode;
    s->base = base;
    s->base_overlay = bdrv_find_overlay(bs, base);
    s->granularity = granularity;
    s->buf_size = ROUND_UP(buf_size, granularity);
    s->unmap = unmap;
    if (auto_complete) {
        s->should_complete = true;
    }

    s->dirty_bitmap = bdrv_create_dirty_bitmap(bs, granularity, NULL, errp);
    if (!s->dirty_bitmap) {
        goto fail;
    }
    if (s->copy_mode == MIRROR_COPY_MODE_WRITE_BLOCKING) {
        bdrv_disable_dirty_bitmap(s->dirty_bitmap);
    }

    ret = block_job_add_bdrv(&s->common, "source", bs, 0,
                             BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE |
                             BLK_PERM_CONSISTENT_READ,
                             errp);
    if (ret < 0) {
        goto fail;
    }

    /* Required permissions are already taken with blk_new() */
    block_job_add_bdrv(&s->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);

    /* In commit_active_start() all intermediate nodes disappear, so
     * any jobs in them must be blocked */
    if (target_is_backing) {
        BlockDriverState *iter, *filtered_target;
        uint64_t iter_shared_perms;

        /*
         * The topmost node with
         * bdrv_skip_filters(filtered_target) == bdrv_skip_filters(target)
         */
        filtered_target = bdrv_cow_bs(bdrv_find_overlay(bs, target));

        assert(bdrv_skip_filters(filtered_target) ==
               bdrv_skip_filters(target));

        /*
         * XXX BLK_PERM_WRITE needs to be allowed so we don't block
         * ourselves at s->base (if writes are blocked for a node, they are
         * also blocked for its backing file). The other options would be a
         * second filter driver above s->base (== target).
         */
        iter_shared_perms = BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE;

        for (iter = bdrv_filter_or_cow_bs(bs); iter != target;
             iter = bdrv_filter_or_cow_bs(iter))
        {
            if (iter == filtered_target) {
                /*
                 * From here on, all nodes are filters on the base.
                 * This allows us to share BLK_PERM_CONSISTENT_READ.
                 */
                iter_shared_perms |= BLK_PERM_CONSISTENT_READ;
            }

            ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                     iter_shared_perms, errp);
            if (ret < 0) {
                goto fail;
            }
        }

        if (bdrv_freeze_backing_chain(mirror_top_bs, target, errp) < 0) {
            goto fail;
        }
    }

    QTAILQ_INIT(&s->ops_in_flight);

    trace_mirror_start(bs, s, opaque);
    job_start(&s->common.job);

    return &s->common;

fail:
    if (s) {
        /* Make sure this BDS does not go away until we have completed the graph
         * changes below */
        bdrv_ref(mirror_top_bs);

        g_free(s->replaces);
        blk_unref(s->target);
        bs_opaque->job = NULL;
        if (s->dirty_bitmap) {
            bdrv_release_dirty_bitmap(s->dirty_bitmap);
        }
        job_early_fail(&s->common.job);
    }

    bs_opaque->stop = true;
    bdrv_child_refresh_perms(mirror_top_bs, mirror_top_bs->backing,
                             &error_abort);
    bdrv_replace_node(mirror_top_bs, mirror_top_bs->backing->bs, &error_abort);

    bdrv_unref(mirror_top_bs);

    return NULL;
}

void mirror_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, const char *replaces,
                  int creation_flags, int64_t speed,
                  uint32_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockMirrorBackingMode backing_mode,
                  bool zero_target,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  bool unmap, const char *filter_node_name,
                  MirrorCopyMode copy_mode, Error **errp)
{
    bool is_none_mode;
    BlockDriverState *base;

    GLOBAL_STATE_CODE();

    if ((mode == MIRROR_SYNC_MODE_INCREMENTAL) ||
        (mode == MIRROR_SYNC_MODE_BITMAP)) {
        error_setg(errp, "Sync mode '%s' not supported",
                   MirrorSyncMode_str(mode));
        return;
    }
    is_none_mode = mode == MIRROR_SYNC_MODE_NONE;
    base = mode == MIRROR_SYNC_MODE_TOP ? bdrv_backing_chain_next(bs) : NULL;
    mirror_start_job(job_id, bs, creation_flags, target, replaces,
                     speed, granularity, buf_size, backing_mode, zero_target,
                     on_source_error, on_target_error, unmap, NULL, NULL,
                     &mirror_job_driver, is_none_mode, base, false,
                     filter_node_name, true, copy_mode, errp);
}

BlockJob *commit_active_start(const char *job_id, BlockDriverState *bs,
                              BlockDriverState *base, int creation_flags,
                              int64_t speed, BlockdevOnError on_error,
                              const char *filter_node_name,
                              BlockCompletionFunc *cb, void *opaque,
                              bool auto_complete, Error **errp)
{
    bool base_read_only;
    BlockJob *job;

    GLOBAL_STATE_CODE();

    base_read_only = bdrv_is_read_only(base);

    if (base_read_only) {
        if (bdrv_reopen_set_read_only(base, false, errp) < 0) {
            return NULL;
        }
    }

    job = mirror_start_job(
                     job_id, bs, creation_flags, base, NULL, speed, 0, 0,
                     MIRROR_LEAVE_BACKING_CHAIN, false,
                     on_error, on_error, true, cb, opaque,
                     &commit_active_job_driver, false, base, auto_complete,
                     filter_node_name, false, MIRROR_COPY_MODE_BACKGROUND,
                     errp);
    if (!job) {
        goto error_restore_flags;
    }

    return job;

error_restore_flags:
    /* ignore error and errp for bdrv_reopen, because we want to propagate
     * the original error */
    if (base_read_only) {
        bdrv_reopen_set_read_only(base, true, NULL);
    }
    return NULL;
}
