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
#include "trace.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/bitmap.h"

#define SLICE_TIME    100000000ULL /* ns */
#define MAX_IN_FLIGHT 16
#define MAX_IO_SECTORS ((1 << 20) >> BDRV_SECTOR_BITS) /* 1 Mb */
#define DEFAULT_MIRROR_BUF_SIZE \
    (MAX_IN_FLIGHT * MAX_IO_SECTORS * BDRV_SECTOR_SIZE)

/* The mirroring buffer is a list of granularity-sized chunks.
 * Free chunks are organized in a list.
 */
typedef struct MirrorBuffer {
    QSIMPLEQ_ENTRY(MirrorBuffer) next;
} MirrorBuffer;

typedef struct MirrorBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockBackend *target;
    BlockDriverState *mirror_top_bs;
    BlockDriverState *source;
    BlockDriverState *base;

    /* The name of the graph node to replace */
    char *replaces;
    /* The BDS to replace */
    BlockDriverState *to_replace;
    /* Used to block operations on the drive-mirror-replace target */
    Error *replace_blocker;
    bool is_none_mode;
    BlockMirrorBackingMode backing_mode;
    BlockdevOnError on_source_error, on_target_error;
    bool synced;
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
    int64_t sectors_in_flight;
    int ret;
    bool unmap;
    bool waiting_for_io;
    int target_cluster_sectors;
    int max_iov;
    bool initial_zeroing_ongoing;
} MirrorBlockJob;

typedef struct MirrorOp {
    MirrorBlockJob *s;
    QEMUIOVector qiov;
    int64_t sector_num;
    int nb_sectors;
} MirrorOp;

static BlockErrorAction mirror_error_action(MirrorBlockJob *s, bool read,
                                            int error)
{
    s->synced = false;
    if (read) {
        return block_job_error_action(&s->common, s->on_source_error,
                                      true, error);
    } else {
        return block_job_error_action(&s->common, s->on_target_error,
                                      false, error);
    }
}

static void mirror_iteration_done(MirrorOp *op, int ret)
{
    MirrorBlockJob *s = op->s;
    struct iovec *iov;
    int64_t chunk_num;
    int i, nb_chunks, sectors_per_chunk;

    trace_mirror_iteration_done(s, op->sector_num, op->nb_sectors, ret);

    s->in_flight--;
    s->sectors_in_flight -= op->nb_sectors;
    iov = op->qiov.iov;
    for (i = 0; i < op->qiov.niov; i++) {
        MirrorBuffer *buf = (MirrorBuffer *) iov[i].iov_base;
        QSIMPLEQ_INSERT_TAIL(&s->buf_free, buf, next);
        s->buf_free_count++;
    }

    sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;
    chunk_num = op->sector_num / sectors_per_chunk;
    nb_chunks = DIV_ROUND_UP(op->nb_sectors, sectors_per_chunk);
    bitmap_clear(s->in_flight_bitmap, chunk_num, nb_chunks);
    if (ret >= 0) {
        if (s->cow_bitmap) {
            bitmap_set(s->cow_bitmap, chunk_num, nb_chunks);
        }
        if (!s->initial_zeroing_ongoing) {
            s->common.offset += (uint64_t)op->nb_sectors * BDRV_SECTOR_SIZE;
        }
    }
    qemu_iovec_destroy(&op->qiov);
    g_free(op);

    if (s->waiting_for_io) {
        qemu_coroutine_enter(s->common.co);
    }
}

static void mirror_write_complete(void *opaque, int ret)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;

    aio_context_acquire(blk_get_aio_context(s->common.blk));
    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->sector_num, op->nb_sectors);
        action = mirror_error_action(s, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }
    }
    mirror_iteration_done(op, ret);
    aio_context_release(blk_get_aio_context(s->common.blk));
}

static void mirror_read_complete(void *opaque, int ret)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;

    aio_context_acquire(blk_get_aio_context(s->common.blk));
    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->sector_num, op->nb_sectors);
        action = mirror_error_action(s, true, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }

        mirror_iteration_done(op, ret);
    } else {
        blk_aio_pwritev(s->target, op->sector_num * BDRV_SECTOR_SIZE, &op->qiov,
                        0, mirror_write_complete, op);
    }
    aio_context_release(blk_get_aio_context(s->common.blk));
}

static inline void mirror_clip_sectors(MirrorBlockJob *s,
                                       int64_t sector_num,
                                       int *nb_sectors)
{
    *nb_sectors = MIN(*nb_sectors,
                      s->bdev_length / BDRV_SECTOR_SIZE - sector_num);
}

/* Round sector_num and/or nb_sectors to target cluster if COW is needed, and
 * return the offset of the adjusted tail sector against original. */
static int mirror_cow_align(MirrorBlockJob *s,
                            int64_t *sector_num,
                            int *nb_sectors)
{
    bool need_cow;
    int ret = 0;
    int chunk_sectors = s->granularity >> BDRV_SECTOR_BITS;
    int64_t align_sector_num = *sector_num;
    int align_nb_sectors = *nb_sectors;
    int max_sectors = chunk_sectors * s->max_iov;

    need_cow = !test_bit(*sector_num / chunk_sectors, s->cow_bitmap);
    need_cow |= !test_bit((*sector_num + *nb_sectors - 1) / chunk_sectors,
                          s->cow_bitmap);
    if (need_cow) {
        bdrv_round_sectors_to_clusters(blk_bs(s->target), *sector_num,
                                       *nb_sectors, &align_sector_num,
                                       &align_nb_sectors);
    }

    if (align_nb_sectors > max_sectors) {
        align_nb_sectors = max_sectors;
        if (need_cow) {
            align_nb_sectors = QEMU_ALIGN_DOWN(align_nb_sectors,
                                               s->target_cluster_sectors);
        }
    }
    /* Clipping may result in align_nb_sectors unaligned to chunk boundary, but
     * that doesn't matter because it's already the end of source image. */
    mirror_clip_sectors(s, align_sector_num, &align_nb_sectors);

    ret = align_sector_num + align_nb_sectors - (*sector_num + *nb_sectors);
    *sector_num = align_sector_num;
    *nb_sectors = align_nb_sectors;
    assert(ret >= 0);
    return ret;
}

static inline void mirror_wait_for_io(MirrorBlockJob *s)
{
    assert(!s->waiting_for_io);
    s->waiting_for_io = true;
    qemu_coroutine_yield();
    s->waiting_for_io = false;
}

/* Submit async read while handling COW.
 * Returns: The number of sectors copied after and including sector_num,
 *          excluding any sectors copied prior to sector_num due to alignment.
 *          This will be nb_sectors if no alignment is necessary, or
 *          (new_end - sector_num) if tail is rounded up or down due to
 *          alignment or buffer limit.
 */
static int mirror_do_read(MirrorBlockJob *s, int64_t sector_num,
                          int nb_sectors)
{
    BlockBackend *source = s->common.blk;
    int sectors_per_chunk, nb_chunks;
    int ret;
    MirrorOp *op;
    int max_sectors;

    sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;
    max_sectors = sectors_per_chunk * s->max_iov;

    /* We can only handle as much as buf_size at a time. */
    nb_sectors = MIN(s->buf_size >> BDRV_SECTOR_BITS, nb_sectors);
    nb_sectors = MIN(max_sectors, nb_sectors);
    assert(nb_sectors);
    ret = nb_sectors;

    if (s->cow_bitmap) {
        ret += mirror_cow_align(s, &sector_num, &nb_sectors);
    }
    assert(nb_sectors << BDRV_SECTOR_BITS <= s->buf_size);
    /* The sector range must meet granularity because:
     * 1) Caller passes in aligned values;
     * 2) mirror_cow_align is used only when target cluster is larger. */
    assert(!(sector_num % sectors_per_chunk));
    nb_chunks = DIV_ROUND_UP(nb_sectors, sectors_per_chunk);

    while (s->buf_free_count < nb_chunks) {
        trace_mirror_yield_in_flight(s, sector_num, s->in_flight);
        mirror_wait_for_io(s);
    }

    /* Allocate a MirrorOp that is used as an AIO callback.  */
    op = g_new(MirrorOp, 1);
    op->s = s;
    op->sector_num = sector_num;
    op->nb_sectors = nb_sectors;

    /* Now make a QEMUIOVector taking enough granularity-sized chunks
     * from s->buf_free.
     */
    qemu_iovec_init(&op->qiov, nb_chunks);
    while (nb_chunks-- > 0) {
        MirrorBuffer *buf = QSIMPLEQ_FIRST(&s->buf_free);
        size_t remaining = nb_sectors * BDRV_SECTOR_SIZE - op->qiov.size;

        QSIMPLEQ_REMOVE_HEAD(&s->buf_free, next);
        s->buf_free_count--;
        qemu_iovec_add(&op->qiov, buf, MIN(s->granularity, remaining));
    }

    /* Copy the dirty cluster.  */
    s->in_flight++;
    s->sectors_in_flight += nb_sectors;
    trace_mirror_one_iteration(s, sector_num, nb_sectors);

    blk_aio_preadv(source, sector_num * BDRV_SECTOR_SIZE, &op->qiov, 0,
                   mirror_read_complete, op);
    return ret;
}

static void mirror_do_zero_or_discard(MirrorBlockJob *s,
                                      int64_t sector_num,
                                      int nb_sectors,
                                      bool is_discard)
{
    MirrorOp *op;

    /* Allocate a MirrorOp that is used as an AIO callback. The qiov is zeroed
     * so the freeing in mirror_iteration_done is nop. */
    op = g_new0(MirrorOp, 1);
    op->s = s;
    op->sector_num = sector_num;
    op->nb_sectors = nb_sectors;

    s->in_flight++;
    s->sectors_in_flight += nb_sectors;
    if (is_discard) {
        blk_aio_pdiscard(s->target, sector_num << BDRV_SECTOR_BITS,
                         op->nb_sectors << BDRV_SECTOR_BITS,
                         mirror_write_complete, op);
    } else {
        blk_aio_pwrite_zeroes(s->target, sector_num * BDRV_SECTOR_SIZE,
                              op->nb_sectors * BDRV_SECTOR_SIZE,
                              s->unmap ? BDRV_REQ_MAY_UNMAP : 0,
                              mirror_write_complete, op);
    }
}

static uint64_t coroutine_fn mirror_iteration(MirrorBlockJob *s)
{
    BlockDriverState *source = s->source;
    int64_t sector_num, first_chunk;
    uint64_t delay_ns = 0;
    /* At least the first dirty chunk is mirrored in one iteration. */
    int nb_chunks = 1;
    int64_t end = s->bdev_length / BDRV_SECTOR_SIZE;
    int sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;
    bool write_zeroes_ok = bdrv_can_write_zeroes_with_unmap(blk_bs(s->target));
    int max_io_sectors = MAX((s->buf_size >> BDRV_SECTOR_BITS) / MAX_IN_FLIGHT,
                             MAX_IO_SECTORS);

    sector_num = bdrv_dirty_iter_next(s->dbi);
    if (sector_num < 0) {
        bdrv_set_dirty_iter(s->dbi, 0);
        sector_num = bdrv_dirty_iter_next(s->dbi);
        trace_mirror_restart_iter(s, bdrv_get_dirty_count(s->dirty_bitmap));
        assert(sector_num >= 0);
    }

    first_chunk = sector_num / sectors_per_chunk;
    while (test_bit(first_chunk, s->in_flight_bitmap)) {
        trace_mirror_yield_in_flight(s, sector_num, s->in_flight);
        mirror_wait_for_io(s);
    }

    block_job_pause_point(&s->common);

    /* Find the number of consective dirty chunks following the first dirty
     * one, and wait for in flight requests in them. */
    while (nb_chunks * sectors_per_chunk < (s->buf_size >> BDRV_SECTOR_BITS)) {
        int64_t next_dirty;
        int64_t next_sector = sector_num + nb_chunks * sectors_per_chunk;
        int64_t next_chunk = next_sector / sectors_per_chunk;
        if (next_sector >= end ||
            !bdrv_get_dirty(source, s->dirty_bitmap, next_sector)) {
            break;
        }
        if (test_bit(next_chunk, s->in_flight_bitmap)) {
            break;
        }

        next_dirty = bdrv_dirty_iter_next(s->dbi);
        if (next_dirty > next_sector || next_dirty < 0) {
            /* The bitmap iterator's cache is stale, refresh it */
            bdrv_set_dirty_iter(s->dbi, next_sector);
            next_dirty = bdrv_dirty_iter_next(s->dbi);
        }
        assert(next_dirty == next_sector);
        nb_chunks++;
    }

    /* Clear dirty bits before querying the block status, because
     * calling bdrv_get_block_status_above could yield - if some blocks are
     * marked dirty in this window, we need to know.
     */
    bdrv_reset_dirty_bitmap(s->dirty_bitmap, sector_num,
                            nb_chunks * sectors_per_chunk);
    bitmap_set(s->in_flight_bitmap, sector_num / sectors_per_chunk, nb_chunks);
    while (nb_chunks > 0 && sector_num < end) {
        int64_t ret;
        int io_sectors, io_sectors_acct;
        BlockDriverState *file;
        enum MirrorMethod {
            MIRROR_METHOD_COPY,
            MIRROR_METHOD_ZERO,
            MIRROR_METHOD_DISCARD
        } mirror_method = MIRROR_METHOD_COPY;

        assert(!(sector_num % sectors_per_chunk));
        ret = bdrv_get_block_status_above(source, NULL, sector_num,
                                          nb_chunks * sectors_per_chunk,
                                          &io_sectors, &file);
        if (ret < 0) {
            io_sectors = MIN(nb_chunks * sectors_per_chunk, max_io_sectors);
        } else if (ret & BDRV_BLOCK_DATA) {
            io_sectors = MIN(io_sectors, max_io_sectors);
        }

        io_sectors -= io_sectors % sectors_per_chunk;
        if (io_sectors < sectors_per_chunk) {
            io_sectors = sectors_per_chunk;
        } else if (ret >= 0 && !(ret & BDRV_BLOCK_DATA)) {
            int64_t target_sector_num;
            int target_nb_sectors;
            bdrv_round_sectors_to_clusters(blk_bs(s->target), sector_num,
                                           io_sectors,  &target_sector_num,
                                           &target_nb_sectors);
            if (target_sector_num == sector_num &&
                target_nb_sectors == io_sectors) {
                mirror_method = ret & BDRV_BLOCK_ZERO ?
                                    MIRROR_METHOD_ZERO :
                                    MIRROR_METHOD_DISCARD;
            }
        }

        while (s->in_flight >= MAX_IN_FLIGHT) {
            trace_mirror_yield_in_flight(s, sector_num, s->in_flight);
            mirror_wait_for_io(s);
        }

        if (s->ret < 0) {
            return 0;
        }

        mirror_clip_sectors(s, sector_num, &io_sectors);
        switch (mirror_method) {
        case MIRROR_METHOD_COPY:
            io_sectors = mirror_do_read(s, sector_num, io_sectors);
            io_sectors_acct = io_sectors;
            break;
        case MIRROR_METHOD_ZERO:
        case MIRROR_METHOD_DISCARD:
            mirror_do_zero_or_discard(s, sector_num, io_sectors,
                                      mirror_method == MIRROR_METHOD_DISCARD);
            if (write_zeroes_ok) {
                io_sectors_acct = 0;
            } else {
                io_sectors_acct = io_sectors;
            }
            break;
        default:
            abort();
        }
        assert(io_sectors);
        sector_num += io_sectors;
        nb_chunks -= DIV_ROUND_UP(io_sectors, sectors_per_chunk);
        if (s->common.speed) {
            delay_ns = ratelimit_calculate_delay(&s->limit, io_sectors_acct);
        }
    }
    return delay_ns;
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
static void mirror_wait_for_all_io(MirrorBlockJob *s)
{
    while (s->in_flight > 0) {
        mirror_wait_for_io(s);
    }
}

typedef struct {
    int ret;
} MirrorExitData;

static void mirror_exit(BlockJob *job, void *opaque)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);
    MirrorExitData *data = opaque;
    AioContext *replace_aio_context = NULL;
    BlockDriverState *src = s->source;
    BlockDriverState *target_bs = blk_bs(s->target);
    BlockDriverState *mirror_top_bs = s->mirror_top_bs;
    Error *local_err = NULL;

    /* Make sure that the source BDS doesn't go away before we called
     * block_job_completed(). */
    bdrv_ref(src);
    bdrv_ref(mirror_top_bs);
    bdrv_ref(target_bs);

    /* Remove target parent that still uses BLK_PERM_WRITE/RESIZE before
     * inserting target_bs at s->to_replace, where we might not be able to get
     * these permissions. */
    blk_unref(s->target);
    s->target = NULL;

    /* We don't access the source any more. Dropping any WRITE/RESIZE is
     * required before it could become a backing file of target_bs. */
    bdrv_child_try_set_perm(mirror_top_bs->backing, 0, BLK_PERM_ALL,
                            &error_abort);
    if (s->backing_mode == MIRROR_SOURCE_BACKING_CHAIN) {
        BlockDriverState *backing = s->is_none_mode ? src : s->base;
        if (backing_bs(target_bs) != backing) {
            bdrv_set_backing_hd(target_bs, backing, &local_err);
            if (local_err) {
                error_report_err(local_err);
                data->ret = -EPERM;
            }
        }
    }

    if (s->to_replace) {
        replace_aio_context = bdrv_get_aio_context(s->to_replace);
        aio_context_acquire(replace_aio_context);
    }

    if (s->should_complete && data->ret == 0) {
        BlockDriverState *to_replace = src;
        if (s->to_replace) {
            to_replace = s->to_replace;
        }

        if (bdrv_get_flags(target_bs) != bdrv_get_flags(to_replace)) {
            bdrv_reopen(target_bs, bdrv_get_flags(to_replace), NULL);
        }

        /* The mirror job has no requests in flight any more, but we need to
         * drain potential other users of the BDS before changing the graph. */
        bdrv_drained_begin(target_bs);
        bdrv_replace_node(to_replace, target_bs, &local_err);
        bdrv_drained_end(target_bs);
        if (local_err) {
            error_report_err(local_err);
            data->ret = -EPERM;
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

    /* Remove the mirror filter driver from the graph. Before this, get rid of
     * the blockers on the intermediate nodes so that the resulting state is
     * valid. Also give up permissions on mirror_top_bs->backing, which might
     * block the removal. */
    block_job_remove_all_bdrv(job);
    bdrv_child_try_set_perm(mirror_top_bs->backing, 0, BLK_PERM_ALL,
                            &error_abort);
    bdrv_replace_node(mirror_top_bs, backing_bs(mirror_top_bs), &error_abort);

    /* We just changed the BDS the job BB refers to (with either or both of the
     * bdrv_replace_node() calls), so switch the BB back so the cleanup does
     * the right thing. We don't need any permissions any more now. */
    blk_remove_bs(job->blk);
    blk_set_perm(job->blk, 0, BLK_PERM_ALL, &error_abort);
    blk_insert_bs(job->blk, mirror_top_bs, &error_abort);

    block_job_completed(&s->common, data->ret);

    g_free(data);
    bdrv_drained_end(src);
    bdrv_unref(mirror_top_bs);
    bdrv_unref(src);
}

static void mirror_throttle(MirrorBlockJob *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    if (now - s->last_pause_ns > SLICE_TIME) {
        s->last_pause_ns = now;
        block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, 0);
    } else {
        block_job_pause_point(&s->common);
    }
}

static int coroutine_fn mirror_dirty_init(MirrorBlockJob *s)
{
    int64_t sector_num, end;
    BlockDriverState *base = s->base;
    BlockDriverState *bs = s->source;
    BlockDriverState *target_bs = blk_bs(s->target);
    int ret, n;

    end = s->bdev_length / BDRV_SECTOR_SIZE;

    if (base == NULL && !bdrv_has_zero_init(target_bs)) {
        if (!bdrv_can_write_zeroes_with_unmap(target_bs)) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, 0, end);
            return 0;
        }

        s->initial_zeroing_ongoing = true;
        for (sector_num = 0; sector_num < end; ) {
            int nb_sectors = MIN(end - sector_num,
                QEMU_ALIGN_DOWN(INT_MAX, s->granularity) >> BDRV_SECTOR_BITS);

            mirror_throttle(s);

            if (block_job_is_cancelled(&s->common)) {
                s->initial_zeroing_ongoing = false;
                return 0;
            }

            if (s->in_flight >= MAX_IN_FLIGHT) {
                trace_mirror_yield(s, UINT64_MAX, s->buf_free_count,
                                   s->in_flight);
                mirror_wait_for_io(s);
                continue;
            }

            mirror_do_zero_or_discard(s, sector_num, nb_sectors, false);
            sector_num += nb_sectors;
        }

        mirror_wait_for_all_io(s);
        s->initial_zeroing_ongoing = false;
    }

    /* First part, loop on the sectors and initialize the dirty bitmap.  */
    for (sector_num = 0; sector_num < end; ) {
        /* Just to make sure we are not exceeding int limit. */
        int nb_sectors = MIN(INT_MAX >> BDRV_SECTOR_BITS,
                             end - sector_num);

        mirror_throttle(s);

        if (block_job_is_cancelled(&s->common)) {
            return 0;
        }

        ret = bdrv_is_allocated_above(bs, base, sector_num, nb_sectors, &n);
        if (ret < 0) {
            return ret;
        }

        assert(n > 0);
        if (ret == 1) {
            bdrv_set_dirty_bitmap(s->dirty_bitmap, sector_num, n);
        }
        sector_num += n;
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

static void coroutine_fn mirror_run(void *opaque)
{
    MirrorBlockJob *s = opaque;
    MirrorExitData *data;
    BlockDriverState *bs = s->source;
    BlockDriverState *target_bs = blk_bs(s->target);
    bool need_drain = true;
    int64_t length;
    BlockDriverInfo bdi;
    char backing_filename[2]; /* we only need 2 characters because we are only
                                 checking for a NULL string */
    int ret = 0;
    int target_cluster_size = BDRV_SECTOR_SIZE;

    if (block_job_is_cancelled(&s->common)) {
        goto immediate_exit;
    }

    s->bdev_length = bdrv_getlength(bs);
    if (s->bdev_length < 0) {
        ret = s->bdev_length;
        goto immediate_exit;
    }

    /* Active commit must resize the base image if its size differs from the
     * active layer. */
    if (s->base == blk_bs(s->target)) {
        int64_t base_length;

        base_length = blk_getlength(s->target);
        if (base_length < 0) {
            ret = base_length;
            goto immediate_exit;
        }

        if (s->bdev_length > base_length) {
            ret = blk_truncate(s->target, s->bdev_length);
            if (ret < 0) {
                goto immediate_exit;
            }
        }
    }

    if (s->bdev_length == 0) {
        /* Report BLOCK_JOB_READY and wait for complete. */
        block_job_event_ready(&s->common);
        s->synced = true;
        while (!block_job_is_cancelled(&s->common) && !s->should_complete) {
            block_job_yield(&s->common);
        }
        s->common.cancelled = false;
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
        target_cluster_size = bdi.cluster_size;
    }
    if (backing_filename[0] && !target_bs->backing
        && s->granularity < target_cluster_size) {
        s->buf_size = MAX(s->buf_size, target_cluster_size);
        s->cow_bitmap = bitmap_new(length);
    }
    s->target_cluster_sectors = target_cluster_size >> BDRV_SECTOR_BITS;
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
        if (ret < 0 || block_job_is_cancelled(&s->common)) {
            goto immediate_exit;
        }
    }

    assert(!s->dbi);
    s->dbi = bdrv_dirty_iter_new(s->dirty_bitmap, 0);
    for (;;) {
        uint64_t delay_ns = 0;
        int64_t cnt, delta;
        bool should_complete;

        if (s->ret < 0) {
            ret = s->ret;
            goto immediate_exit;
        }

        block_job_pause_point(&s->common);

        cnt = bdrv_get_dirty_count(s->dirty_bitmap);
        /* s->common.offset contains the number of bytes already processed so
         * far, cnt is the number of dirty sectors remaining and
         * s->sectors_in_flight is the number of sectors currently being
         * processed; together those are the current total operation length */
        s->common.len = s->common.offset +
                        (cnt + s->sectors_in_flight) * BDRV_SECTOR_SIZE;

        /* Note that even when no rate limit is applied we need to yield
         * periodically with no pending I/O so that bdrv_drain_all() returns.
         * We do so every SLICE_TIME nanoseconds, or when there is an error,
         * or when the source is clean, whichever comes first.
         */
        delta = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s->last_pause_ns;
        if (delta < SLICE_TIME &&
            s->common.iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
            if (s->in_flight >= MAX_IN_FLIGHT || s->buf_free_count == 0 ||
                (cnt == 0 && s->in_flight > 0)) {
                trace_mirror_yield(s, cnt, s->buf_free_count, s->in_flight);
                mirror_wait_for_io(s);
                continue;
            } else if (cnt != 0) {
                delay_ns = mirror_iteration(s);
            }
        }

        should_complete = false;
        if (s->in_flight == 0 && cnt == 0) {
            trace_mirror_before_flush(s);
            if (!s->synced) {
                if (mirror_flush(s) < 0) {
                    /* Go check s->ret.  */
                    continue;
                }
                /* We're out of the streaming phase.  From now on, if the job
                 * is cancelled we will actually complete all pending I/O and
                 * report completion.  This way, block-job-cancel will leave
                 * the target in a consistent state.
                 */
                block_job_event_ready(&s->common);
                s->synced = true;
            }

            should_complete = s->should_complete ||
                block_job_is_cancelled(&s->common);
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

            bdrv_drained_begin(bs);
            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
            if (cnt > 0 || mirror_flush(s) < 0) {
                bdrv_drained_end(bs);
                continue;
            }

            /* The two disks are in sync.  Exit and report successful
             * completion.
             */
            assert(QLIST_EMPTY(&bs->tracked_requests));
            s->common.cancelled = false;
            need_drain = false;
            break;
        }

        ret = 0;
        trace_mirror_before_sleep(s, cnt, s->synced, delay_ns);
        if (!s->synced) {
            block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, delay_ns);
            if (block_job_is_cancelled(&s->common)) {
                break;
            }
        } else if (!should_complete) {
            delay_ns = (s->in_flight == 0 && cnt == 0 ? SLICE_TIME : 0);
            block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, delay_ns);
        }
        s->last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

immediate_exit:
    if (s->in_flight > 0) {
        /* We get here only if something went wrong.  Either the job failed,
         * or it was cancelled prematurely so that we do not guarantee that
         * the target is a copy of the source.
         */
        assert(ret < 0 || (!s->synced && block_job_is_cancelled(&s->common)));
        assert(need_drain);
        mirror_wait_for_all_io(s);
    }

    assert(s->in_flight == 0);
    qemu_vfree(s->buf);
    g_free(s->cow_bitmap);
    g_free(s->in_flight_bitmap);
    bdrv_dirty_iter_free(s->dbi);
    bdrv_release_dirty_bitmap(bs, s->dirty_bitmap);

    data = g_malloc(sizeof(*data));
    data->ret = ret;

    if (need_drain) {
        bdrv_drained_begin(bs);
    }
    block_job_defer_to_main_loop(&s->common, mirror_exit, data);
}

static void mirror_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static void mirror_complete(BlockJob *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);
    BlockDriverState *target;

    target = blk_bs(s->target);

    if (!s->synced) {
        error_setg(errp, "The active block job '%s' cannot be completed",
                   job->id);
        return;
    }

    if (s->backing_mode == MIRROR_OPEN_BACKING_CHAIN) {
        int ret;

        assert(!target->backing);
        ret = bdrv_open_backing_file(target, NULL, "backing", errp);
        if (ret < 0) {
            return;
        }
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

        /* TODO Translate this into permission system. Current definition of
         * GRAPH_MOD would require to request it for the parents; they might
         * not even be BlockDriverStates, however, so a BdrvChild can't address
         * them. May need redefinition of GRAPH_MOD. */
        error_setg(&s->replace_blocker,
                   "block device is in use by block-job-complete");
        bdrv_op_block_all(s->to_replace, s->replace_blocker);
        bdrv_ref(s->to_replace);

        aio_context_release(replace_aio_context);
    }

    s->should_complete = true;
    block_job_enter(&s->common);
}

static void mirror_pause(BlockJob *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    mirror_wait_for_all_io(s);
}

static void mirror_attached_aio_context(BlockJob *job, AioContext *new_context)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    blk_set_aio_context(s->target, new_context);
}

static void mirror_drain(BlockJob *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    /* Need to keep a reference in case blk_drain triggers execution
     * of mirror_complete...
     */
    if (s->target) {
        BlockBackend *target = s->target;
        blk_ref(target);
        blk_drain(target);
        blk_unref(target);
    }
}

static const BlockJobDriver mirror_job_driver = {
    .instance_size          = sizeof(MirrorBlockJob),
    .job_type               = BLOCK_JOB_TYPE_MIRROR,
    .set_speed              = mirror_set_speed,
    .start                  = mirror_run,
    .complete               = mirror_complete,
    .pause                  = mirror_pause,
    .attached_aio_context   = mirror_attached_aio_context,
    .drain                  = mirror_drain,
};

static const BlockJobDriver commit_active_job_driver = {
    .instance_size          = sizeof(MirrorBlockJob),
    .job_type               = BLOCK_JOB_TYPE_COMMIT,
    .set_speed              = mirror_set_speed,
    .start                  = mirror_run,
    .complete               = mirror_complete,
    .pause                  = mirror_pause,
    .attached_aio_context   = mirror_attached_aio_context,
    .drain                  = mirror_drain,
};

static int coroutine_fn bdrv_mirror_top_preadv(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn bdrv_mirror_top_pwritev(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    return bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn bdrv_mirror_top_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->backing->bs);
}

static int64_t coroutine_fn bdrv_mirror_top_get_block_status(
    BlockDriverState *bs, int64_t sector_num, int nb_sectors, int *pnum,
    BlockDriverState **file)
{
    *pnum = nb_sectors;
    *file = bs->backing->bs;
    return BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID | BDRV_BLOCK_DATA |
           (sector_num << BDRV_SECTOR_BITS);
}

static int coroutine_fn bdrv_mirror_top_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int count, BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->backing, offset, count, flags);
}

static int coroutine_fn bdrv_mirror_top_pdiscard(BlockDriverState *bs,
    int64_t offset, int count)
{
    return bdrv_co_pdiscard(bs->backing->bs, offset, count);
}

static void bdrv_mirror_top_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    bdrv_refresh_filename(bs->backing->bs);
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_mirror_top_close(BlockDriverState *bs)
{
}

static void bdrv_mirror_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       const BdrvChildRole *role,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    /* Must be able to forward guest writes to the real image */
    *nperm = 0;
    if (perm & BLK_PERM_WRITE) {
        *nperm |= BLK_PERM_WRITE;
    }

    *nshared = BLK_PERM_ALL;
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
    .bdrv_co_get_block_status   = bdrv_mirror_top_get_block_status,
    .bdrv_refresh_filename      = bdrv_mirror_top_refresh_filename,
    .bdrv_close                 = bdrv_mirror_top_close,
    .bdrv_child_perm            = bdrv_mirror_top_child_perm,
};

static void mirror_start_job(const char *job_id, BlockDriverState *bs,
                             int creation_flags, BlockDriverState *target,
                             const char *replaces, int64_t speed,
                             uint32_t granularity, int64_t buf_size,
                             BlockMirrorBackingMode backing_mode,
                             BlockdevOnError on_source_error,
                             BlockdevOnError on_target_error,
                             bool unmap,
                             BlockCompletionFunc *cb,
                             void *opaque, Error **errp,
                             const BlockJobDriver *driver,
                             bool is_none_mode, BlockDriverState *base,
                             bool auto_complete, const char *filter_node_name)
{
    MirrorBlockJob *s;
    BlockDriverState *mirror_top_bs;
    bool target_graph_mod;
    bool target_is_backing;
    Error *local_err = NULL;
    int ret;

    if (granularity == 0) {
        granularity = bdrv_get_default_bitmap_granularity(target);
    }

    assert ((granularity & (granularity - 1)) == 0);

    if (buf_size < 0) {
        error_setg(errp, "Invalid parameter 'buf-size'");
        return;
    }

    if (buf_size == 0) {
        buf_size = DEFAULT_MIRROR_BUF_SIZE;
    }

    /* In the case of active commit, add dummy driver to provide consistent
     * reads on the top, while disabling it in the intermediate nodes, and make
     * the backing chain writable. */
    mirror_top_bs = bdrv_new_open_driver(&bdrv_mirror_top, filter_node_name,
                                         BDRV_O_RDWR, errp);
    if (mirror_top_bs == NULL) {
        return;
    }
    mirror_top_bs->total_sectors = bs->total_sectors;
    bdrv_set_aio_context(mirror_top_bs, bdrv_get_aio_context(bs));

    /* bdrv_append takes ownership of the mirror_top_bs reference, need to keep
     * it alive until block_job_create() succeeds even if bs has no parent. */
    bdrv_ref(mirror_top_bs);
    bdrv_drained_begin(bs);
    bdrv_append(mirror_top_bs, bs, &local_err);
    bdrv_drained_end(bs);

    if (local_err) {
        bdrv_unref(mirror_top_bs);
        error_propagate(errp, local_err);
        return;
    }

    /* Make sure that the source is not resized while the job is running */
    s = block_job_create(job_id, driver, mirror_top_bs,
                         BLK_PERM_CONSISTENT_READ,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE | BLK_PERM_GRAPH_MOD, speed,
                         creation_flags, cb, opaque, errp);
    if (!s) {
        goto fail;
    }
    /* The block job now has a reference to this node */
    bdrv_unref(mirror_top_bs);

    s->source = bs;
    s->mirror_top_bs = mirror_top_bs;

    /* No resize for the target either; while the mirror is still running, a
     * consistent read isn't necessarily possible. We could possibly allow
     * writes and graph modifications, though it would likely defeat the
     * purpose of a mirror, so leave them blocked for now.
     *
     * In the case of active commit, things look a bit different, though,
     * because the target is an already populated backing file in active use.
     * We can allow anything except resize there.*/
    target_is_backing = bdrv_chain_contains(bs, target);
    target_graph_mod = (backing_mode != MIRROR_LEAVE_BACKING_CHAIN);
    s->target = blk_new(BLK_PERM_WRITE | BLK_PERM_RESIZE |
                        (target_graph_mod ? BLK_PERM_GRAPH_MOD : 0),
                        BLK_PERM_WRITE_UNCHANGED |
                        (target_is_backing ? BLK_PERM_CONSISTENT_READ |
                                             BLK_PERM_WRITE |
                                             BLK_PERM_GRAPH_MOD : 0));
    ret = blk_insert_bs(s->target, target, errp);
    if (ret < 0) {
        goto fail;
    }

    s->replaces = g_strdup(replaces);
    s->on_source_error = on_source_error;
    s->on_target_error = on_target_error;
    s->is_none_mode = is_none_mode;
    s->backing_mode = backing_mode;
    s->base = base;
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

    /* Required permissions are already taken with blk_new() */
    block_job_add_bdrv(&s->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);

    /* In commit_active_start() all intermediate nodes disappear, so
     * any jobs in them must be blocked */
    if (target_is_backing) {
        BlockDriverState *iter;
        for (iter = backing_bs(bs); iter != target; iter = backing_bs(iter)) {
            /* XXX BLK_PERM_WRITE needs to be allowed so we don't block
             * ourselves at s->base (if writes are blocked for a node, they are
             * also blocked for its backing file). The other options would be a
             * second filter driver above s->base (== target). */
            ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                     BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE,
                                     errp);
            if (ret < 0) {
                goto fail;
            }
        }
    }

    trace_mirror_start(bs, s, opaque);
    block_job_start(&s->common);
    return;

fail:
    if (s) {
        /* Make sure this BDS does not go away until we have completed the graph
         * changes below */
        bdrv_ref(mirror_top_bs);

        g_free(s->replaces);
        blk_unref(s->target);
        block_job_unref(&s->common);
    }

    bdrv_child_try_set_perm(mirror_top_bs->backing, 0, BLK_PERM_ALL,
                            &error_abort);
    bdrv_replace_node(mirror_top_bs, backing_bs(mirror_top_bs), &error_abort);

    bdrv_unref(mirror_top_bs);
}

void mirror_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, const char *replaces,
                  int64_t speed, uint32_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockMirrorBackingMode backing_mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  bool unmap, const char *filter_node_name, Error **errp)
{
    bool is_none_mode;
    BlockDriverState *base;

    if (mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        error_setg(errp, "Sync mode 'incremental' not supported");
        return;
    }
    is_none_mode = mode == MIRROR_SYNC_MODE_NONE;
    base = mode == MIRROR_SYNC_MODE_TOP ? backing_bs(bs) : NULL;
    mirror_start_job(job_id, bs, BLOCK_JOB_DEFAULT, target, replaces,
                     speed, granularity, buf_size, backing_mode,
                     on_source_error, on_target_error, unmap, NULL, NULL, errp,
                     &mirror_job_driver, is_none_mode, base, false,
                     filter_node_name);
}

void commit_active_start(const char *job_id, BlockDriverState *bs,
                         BlockDriverState *base, int creation_flags,
                         int64_t speed, BlockdevOnError on_error,
                         const char *filter_node_name,
                         BlockCompletionFunc *cb, void *opaque, Error **errp,
                         bool auto_complete)
{
    int orig_base_flags;
    Error *local_err = NULL;

    orig_base_flags = bdrv_get_flags(base);

    if (bdrv_reopen(base, bs->open_flags, errp)) {
        return;
    }

    mirror_start_job(job_id, bs, creation_flags, base, NULL, speed, 0, 0,
                     MIRROR_LEAVE_BACKING_CHAIN,
                     on_error, on_error, true, cb, opaque, &local_err,
                     &commit_active_job_driver, false, base, auto_complete,
                     filter_node_name);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error_restore_flags;
    }

    return;

error_restore_flags:
    /* ignore error and errp for bdrv_reopen, because we want to propagate
     * the original error */
    bdrv_reopen(base, orig_base_flags, NULL);
    return;
}
