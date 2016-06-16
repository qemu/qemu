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
#include "trace.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/bitmap.h"

#define SLICE_TIME    100000000ULL /* ns */
#define MAX_IN_FLIGHT 16
#define DEFAULT_MIRROR_BUF_SIZE   (10 << 20)

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
    HBitmapIter hbi;
    uint8_t *buf;
    QSIMPLEQ_HEAD(, MirrorBuffer) buf_free;
    int buf_free_count;

    unsigned long *in_flight_bitmap;
    int in_flight;
    int sectors_in_flight;
    int ret;
    bool unmap;
    bool waiting_for_io;
    int target_cluster_sectors;
    int max_iov;
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
        s->common.offset += (uint64_t)op->nb_sectors * BDRV_SECTOR_SIZE;
    }

    qemu_iovec_destroy(&op->qiov);
    g_free(op);

    if (s->waiting_for_io) {
        qemu_coroutine_enter(s->common.co, NULL);
    }
}

static void mirror_write_complete(void *opaque, int ret)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;
    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->sector_num, op->nb_sectors);
        action = mirror_error_action(s, false, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }
    }
    mirror_iteration_done(op, ret);
}

static void mirror_read_complete(void *opaque, int ret)
{
    MirrorOp *op = opaque;
    MirrorBlockJob *s = op->s;
    if (ret < 0) {
        BlockErrorAction action;

        bdrv_set_dirty_bitmap(s->dirty_bitmap, op->sector_num, op->nb_sectors);
        action = mirror_error_action(s, true, -ret);
        if (action == BLOCK_ERROR_ACTION_REPORT && s->ret >= 0) {
            s->ret = ret;
        }

        mirror_iteration_done(op, ret);
        return;
    }
    blk_aio_pwritev(s->target, op->sector_num * BDRV_SECTOR_SIZE, &op->qiov,
                    0, mirror_write_complete, op);
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
 * Returns: nb_sectors if no alignment is necessary, or
 *          (new_end - sector_num) if tail is rounded up or down due to
 *          alignment or buffer limit.
 */
static int mirror_do_read(MirrorBlockJob *s, int64_t sector_num,
                          int nb_sectors)
{
    BlockBackend *source = s->common.blk;
    int sectors_per_chunk, nb_chunks;
    int ret = nb_sectors;
    MirrorOp *op;

    sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;

    /* We can only handle as much as buf_size at a time. */
    nb_sectors = MIN(s->buf_size >> BDRV_SECTOR_BITS, nb_sectors);
    assert(nb_sectors);

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
        blk_aio_discard(s->target, sector_num, op->nb_sectors,
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
    BlockDriverState *source = blk_bs(s->common.blk);
    int64_t sector_num, first_chunk;
    uint64_t delay_ns = 0;
    /* At least the first dirty chunk is mirrored in one iteration. */
    int nb_chunks = 1;
    int64_t end = s->bdev_length / BDRV_SECTOR_SIZE;
    int sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;

    sector_num = hbitmap_iter_next(&s->hbi);
    if (sector_num < 0) {
        bdrv_dirty_iter_init(s->dirty_bitmap, &s->hbi);
        sector_num = hbitmap_iter_next(&s->hbi);
        trace_mirror_restart_iter(s, bdrv_get_dirty_count(s->dirty_bitmap));
        assert(sector_num >= 0);
    }

    first_chunk = sector_num / sectors_per_chunk;
    while (test_bit(first_chunk, s->in_flight_bitmap)) {
        trace_mirror_yield_in_flight(s, first_chunk, s->in_flight);
        mirror_wait_for_io(s);
    }

    /* Find the number of consective dirty chunks following the first dirty
     * one, and wait for in flight requests in them. */
    while (nb_chunks * sectors_per_chunk < (s->buf_size >> BDRV_SECTOR_BITS)) {
        int64_t hbitmap_next;
        int64_t next_sector = sector_num + nb_chunks * sectors_per_chunk;
        int64_t next_chunk = next_sector / sectors_per_chunk;
        if (next_sector >= end ||
            !bdrv_get_dirty(source, s->dirty_bitmap, next_sector)) {
            break;
        }
        if (test_bit(next_chunk, s->in_flight_bitmap)) {
            break;
        }

        hbitmap_next = hbitmap_iter_next(&s->hbi);
        if (hbitmap_next > next_sector || hbitmap_next < 0) {
            /* The bitmap iterator's cache is stale, refresh it */
            bdrv_set_dirty_iter(&s->hbi, next_sector);
            hbitmap_next = hbitmap_iter_next(&s->hbi);
        }
        assert(hbitmap_next == next_sector);
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
        int ret;
        int io_sectors;
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
            io_sectors = nb_chunks * sectors_per_chunk;
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

        mirror_clip_sectors(s, sector_num, &io_sectors);
        switch (mirror_method) {
        case MIRROR_METHOD_COPY:
            io_sectors = mirror_do_read(s, sector_num, io_sectors);
            break;
        case MIRROR_METHOD_ZERO:
            mirror_do_zero_or_discard(s, sector_num, io_sectors, false);
            break;
        case MIRROR_METHOD_DISCARD:
            mirror_do_zero_or_discard(s, sector_num, io_sectors, true);
            break;
        default:
            abort();
        }
        assert(io_sectors);
        sector_num += io_sectors;
        nb_chunks -= DIV_ROUND_UP(io_sectors, sectors_per_chunk);
        delay_ns += ratelimit_calculate_delay(&s->limit, io_sectors);
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

static void mirror_drain(MirrorBlockJob *s)
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
    BlockDriverState *src = blk_bs(s->common.blk);
    BlockDriverState *target_bs = blk_bs(s->target);

    /* Make sure that the source BDS doesn't go away before we called
     * block_job_completed(). */
    bdrv_ref(src);

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
        bdrv_replace_in_backing_chain(to_replace, target_bs);
        bdrv_drained_end(target_bs);

        /* We just changed the BDS the job BB refers to */
        blk_remove_bs(job->blk);
        blk_insert_bs(job->blk, src);
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
    bdrv_op_unblock_all(target_bs, s->common.blocker);
    blk_unref(s->target);
    block_job_completed(&s->common, data->ret);
    g_free(data);
    bdrv_drained_end(src);
    if (qemu_get_aio_context() == bdrv_get_aio_context(src)) {
        aio_enable_external(iohandler_get_aio_context());
    }
    bdrv_unref(src);
}

static void coroutine_fn mirror_run(void *opaque)
{
    MirrorBlockJob *s = opaque;
    MirrorExitData *data;
    BlockDriverState *bs = blk_bs(s->common.blk);
    BlockDriverState *target_bs = blk_bs(s->target);
    int64_t sector_num, end, length;
    uint64_t last_pause_ns;
    BlockDriverInfo bdi;
    char backing_filename[2]; /* we only need 2 characters because we are only
                                 checking for a NULL string */
    int ret = 0;
    int n;
    int target_cluster_size = BDRV_SECTOR_SIZE;

    if (block_job_is_cancelled(&s->common)) {
        goto immediate_exit;
    }

    s->bdev_length = bdrv_getlength(bs);
    if (s->bdev_length < 0) {
        ret = s->bdev_length;
        goto immediate_exit;
    } else if (s->bdev_length == 0) {
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

    end = s->bdev_length / BDRV_SECTOR_SIZE;
    s->buf = qemu_try_blockalign(bs, s->buf_size);
    if (s->buf == NULL) {
        ret = -ENOMEM;
        goto immediate_exit;
    }

    mirror_free_init(s);

    last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (!s->is_none_mode) {
        /* First part, loop on the sectors and initialize the dirty bitmap.  */
        BlockDriverState *base = s->base;
        bool mark_all_dirty = s->base == NULL && !bdrv_has_zero_init(target_bs);

        for (sector_num = 0; sector_num < end; ) {
            /* Just to make sure we are not exceeding int limit. */
            int nb_sectors = MIN(INT_MAX >> BDRV_SECTOR_BITS,
                                 end - sector_num);
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

            if (now - last_pause_ns > SLICE_TIME) {
                last_pause_ns = now;
                block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, 0);
            }

            if (block_job_is_cancelled(&s->common)) {
                goto immediate_exit;
            }

            ret = bdrv_is_allocated_above(bs, base, sector_num, nb_sectors, &n);

            if (ret < 0) {
                goto immediate_exit;
            }

            assert(n > 0);
            if (ret == 1 || mark_all_dirty) {
                bdrv_set_dirty_bitmap(s->dirty_bitmap, sector_num, n);
            }
            sector_num += n;
        }
    }

    bdrv_dirty_iter_init(s->dirty_bitmap, &s->hbi);
    for (;;) {
        uint64_t delay_ns = 0;
        int64_t cnt;
        bool should_complete;

        if (s->ret < 0) {
            ret = s->ret;
            goto immediate_exit;
        }

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
        if (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - last_pause_ns < SLICE_TIME &&
            s->common.iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
            if (s->in_flight == MAX_IN_FLIGHT || s->buf_free_count == 0 ||
                (cnt == 0 && s->in_flight > 0)) {
                trace_mirror_yield(s, s->in_flight, s->buf_free_count, cnt);
                mirror_wait_for_io(s);
                continue;
            } else if (cnt != 0) {
                delay_ns = mirror_iteration(s);
            }
        }

        should_complete = false;
        if (s->in_flight == 0 && cnt == 0) {
            trace_mirror_before_flush(s);
            ret = blk_flush(s->target);
            if (ret < 0) {
                if (mirror_error_action(s, false, -ret) ==
                    BLOCK_ERROR_ACTION_REPORT) {
                    goto immediate_exit;
                }
            } else {
                /* We're out of the streaming phase.  From now on, if the job
                 * is cancelled we will actually complete all pending I/O and
                 * report completion.  This way, block-job-cancel will leave
                 * the target in a consistent state.
                 */
                if (!s->synced) {
                    block_job_event_ready(&s->common);
                    s->synced = true;
                }

                should_complete = s->should_complete ||
                    block_job_is_cancelled(&s->common);
                cnt = bdrv_get_dirty_count(s->dirty_bitmap);
            }
        }

        if (cnt == 0 && should_complete) {
            /* The dirty bitmap is not updated while operations are pending.
             * If we're about to exit, wait for pending operations before
             * calling bdrv_get_dirty_count(bs), or we may exit while the
             * source has dirty data to copy!
             *
             * Note that I/O can be submitted by the guest while
             * mirror_populate runs.
             */
            trace_mirror_before_drain(s, cnt);
            bdrv_co_drain(bs);
            cnt = bdrv_get_dirty_count(s->dirty_bitmap);
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
        } else if (cnt == 0) {
            /* The two disks are in sync.  Exit and report successful
             * completion.
             */
            assert(QLIST_EMPTY(&bs->tracked_requests));
            s->common.cancelled = false;
            break;
        }
        last_pause_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

immediate_exit:
    if (s->in_flight > 0) {
        /* We get here only if something went wrong.  Either the job failed,
         * or it was cancelled prematurely so that we do not guarantee that
         * the target is a copy of the source.
         */
        assert(ret < 0 || (!s->synced && block_job_is_cancelled(&s->common)));
        mirror_drain(s);
    }

    assert(s->in_flight == 0);
    qemu_vfree(s->buf);
    g_free(s->cow_bitmap);
    g_free(s->in_flight_bitmap);
    bdrv_release_dirty_bitmap(bs, s->dirty_bitmap);

    data = g_malloc(sizeof(*data));
    data->ret = ret;
    /* Before we switch to target in mirror_exit, make sure data doesn't
     * change. */
    bdrv_drained_begin(bs);
    if (qemu_get_aio_context() == bdrv_get_aio_context(bs)) {
        /* FIXME: virtio host notifiers run on iohandler_ctx, therefore the
         * above bdrv_drained_end isn't enough to quiesce it. This is ugly, we
         * need a block layer API change to achieve this. */
        aio_disable_external(iohandler_get_aio_context());
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
    BlockDriverState *src, *target;

    src = blk_bs(job->blk);
    target = blk_bs(s->target);

    if (!s->synced) {
        error_setg(errp, QERR_BLOCK_JOB_NOT_READY, job->id);
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

    /* check the target bs is not blocked and block all operations on it */
    if (s->replaces) {
        AioContext *replace_aio_context;

        s->to_replace = bdrv_find_node(s->replaces);
        if (!s->to_replace) {
            error_setg(errp, "Node name '%s' not found", s->replaces);
            return;
        }

        replace_aio_context = bdrv_get_aio_context(s->to_replace);
        aio_context_acquire(replace_aio_context);

        error_setg(&s->replace_blocker,
                   "block device is in use by block-job-complete");
        bdrv_op_block_all(s->to_replace, s->replace_blocker);
        bdrv_ref(s->to_replace);

        aio_context_release(replace_aio_context);
    }

    if (s->backing_mode == MIRROR_SOURCE_BACKING_CHAIN) {
        BlockDriverState *backing = s->is_none_mode ? src : s->base;
        if (backing_bs(target) != backing) {
            bdrv_set_backing_hd(target, backing);
        }
    }

    s->should_complete = true;
    block_job_enter(&s->common);
}

static const BlockJobDriver mirror_job_driver = {
    .instance_size = sizeof(MirrorBlockJob),
    .job_type      = BLOCK_JOB_TYPE_MIRROR,
    .set_speed     = mirror_set_speed,
    .complete      = mirror_complete,
};

static const BlockJobDriver commit_active_job_driver = {
    .instance_size = sizeof(MirrorBlockJob),
    .job_type      = BLOCK_JOB_TYPE_COMMIT,
    .set_speed     = mirror_set_speed,
    .complete      = mirror_complete,
};

static void mirror_start_job(BlockDriverState *bs, BlockDriverState *target,
                             const char *replaces,
                             int64_t speed, uint32_t granularity,
                             int64_t buf_size,
                             BlockMirrorBackingMode backing_mode,
                             BlockdevOnError on_source_error,
                             BlockdevOnError on_target_error,
                             bool unmap,
                             BlockCompletionFunc *cb,
                             void *opaque, Error **errp,
                             const BlockJobDriver *driver,
                             bool is_none_mode, BlockDriverState *base)
{
    MirrorBlockJob *s;

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

    s = block_job_create(driver, bs, speed, cb, opaque, errp);
    if (!s) {
        return;
    }

    s->target = blk_new();
    blk_insert_bs(s->target, target);

    s->replaces = g_strdup(replaces);
    s->on_source_error = on_source_error;
    s->on_target_error = on_target_error;
    s->is_none_mode = is_none_mode;
    s->backing_mode = backing_mode;
    s->base = base;
    s->granularity = granularity;
    s->buf_size = ROUND_UP(buf_size, granularity);
    s->unmap = unmap;

    s->dirty_bitmap = bdrv_create_dirty_bitmap(bs, granularity, NULL, errp);
    if (!s->dirty_bitmap) {
        g_free(s->replaces);
        blk_unref(s->target);
        block_job_unref(&s->common);
        return;
    }

    bdrv_op_block_all(target, s->common.blocker);

    s->common.co = qemu_coroutine_create(mirror_run);
    trace_mirror_start(bs, s, s->common.co, opaque);
    qemu_coroutine_enter(s->common.co, s);
}

void mirror_start(BlockDriverState *bs, BlockDriverState *target,
                  const char *replaces,
                  int64_t speed, uint32_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockMirrorBackingMode backing_mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  bool unmap,
                  BlockCompletionFunc *cb,
                  void *opaque, Error **errp)
{
    bool is_none_mode;
    BlockDriverState *base;

    if (mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        error_setg(errp, "Sync mode 'incremental' not supported");
        return;
    }
    is_none_mode = mode == MIRROR_SYNC_MODE_NONE;
    base = mode == MIRROR_SYNC_MODE_TOP ? backing_bs(bs) : NULL;
    mirror_start_job(bs, target, replaces,
                     speed, granularity, buf_size, backing_mode,
                     on_source_error, on_target_error, unmap, cb, opaque, errp,
                     &mirror_job_driver, is_none_mode, base);
}

void commit_active_start(BlockDriverState *bs, BlockDriverState *base,
                         int64_t speed,
                         BlockdevOnError on_error,
                         BlockCompletionFunc *cb,
                         void *opaque, Error **errp)
{
    int64_t length, base_length;
    int orig_base_flags;
    int ret;
    Error *local_err = NULL;

    orig_base_flags = bdrv_get_flags(base);

    if (bdrv_reopen(base, bs->open_flags, errp)) {
        return;
    }

    length = bdrv_getlength(bs);
    if (length < 0) {
        error_setg_errno(errp, -length,
                         "Unable to determine length of %s", bs->filename);
        goto error_restore_flags;
    }

    base_length = bdrv_getlength(base);
    if (base_length < 0) {
        error_setg_errno(errp, -base_length,
                         "Unable to determine length of %s", base->filename);
        goto error_restore_flags;
    }

    if (length > base_length) {
        ret = bdrv_truncate(base, length);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                            "Top image %s is larger than base image %s, and "
                             "resize of base image failed",
                             bs->filename, base->filename);
            goto error_restore_flags;
        }
    }

    mirror_start_job(bs, base, NULL, speed, 0, 0, MIRROR_LEAVE_BACKING_CHAIN,
                     on_error, on_error, false, cb, opaque, &local_err,
                     &commit_active_job_driver, false, base);
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
