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

#include "trace.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "qemu/ratelimit.h"
#include "qemu/bitmap.h"

#define SLICE_TIME 100000000ULL /* ns */

typedef struct MirrorBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *target;
    MirrorSyncMode mode;
    BlockdevOnError on_source_error, on_target_error;
    bool synced;
    bool should_complete;
    int64_t sector_num;
    int64_t granularity;
    size_t buf_size;
    unsigned long *cow_bitmap;
    HBitmapIter hbi;
    uint8_t *buf;
} MirrorBlockJob;

static BlockErrorAction mirror_error_action(MirrorBlockJob *s, bool read,
                                            int error)
{
    s->synced = false;
    if (read) {
        return block_job_error_action(&s->common, s->common.bs,
                                      s->on_source_error, true, error);
    } else {
        return block_job_error_action(&s->common, s->target,
                                      s->on_target_error, false, error);
    }
}

static int coroutine_fn mirror_iteration(MirrorBlockJob *s,
                                         BlockErrorAction *p_action)
{
    BlockDriverState *source = s->common.bs;
    BlockDriverState *target = s->target;
    QEMUIOVector qiov;
    int ret, nb_sectors, sectors_per_chunk;
    int64_t end, sector_num, chunk_num;
    struct iovec iov;

    s->sector_num = hbitmap_iter_next(&s->hbi);
    if (s->sector_num < 0) {
        bdrv_dirty_iter_init(source, &s->hbi);
        s->sector_num = hbitmap_iter_next(&s->hbi);
        trace_mirror_restart_iter(s, bdrv_get_dirty_count(source));
        assert(s->sector_num >= 0);
    }

    /* If we have no backing file yet in the destination, and the cluster size
     * is very large, we need to do COW ourselves.  The first time a cluster is
     * copied, copy it entirely.
     *
     * Because both the granularity and the cluster size are powers of two, the
     * number of sectors to copy cannot exceed one cluster.
     */
    sector_num = s->sector_num;
    sectors_per_chunk = nb_sectors = s->granularity >> BDRV_SECTOR_BITS;
    chunk_num = sector_num / sectors_per_chunk;
    if (s->cow_bitmap && !test_bit(chunk_num, s->cow_bitmap)) {
        trace_mirror_cow(s, sector_num);
        bdrv_round_to_clusters(s->target,
                               sector_num, sectors_per_chunk,
                               &sector_num, &nb_sectors);
    }

    end = s->common.len >> BDRV_SECTOR_BITS;
    nb_sectors = MIN(nb_sectors, end - sector_num);
    bdrv_reset_dirty(source, sector_num, nb_sectors);

    /* Copy the dirty cluster.  */
    iov.iov_base = s->buf;
    iov.iov_len  = nb_sectors * 512;
    qemu_iovec_init_external(&qiov, &iov, 1);

    trace_mirror_one_iteration(s, sector_num, nb_sectors);
    ret = bdrv_co_readv(source, sector_num, nb_sectors, &qiov);
    if (ret < 0) {
        *p_action = mirror_error_action(s, true, -ret);
        goto fail;
    }
    ret = bdrv_co_writev(target, sector_num, nb_sectors, &qiov);
    if (ret < 0) {
        *p_action = mirror_error_action(s, false, -ret);
        s->synced = false;
        goto fail;
    }
    if (s->cow_bitmap) {
        bitmap_set(s->cow_bitmap, sector_num / sectors_per_chunk,
                   nb_sectors / sectors_per_chunk);
    }
    return 0;

fail:
    /* Try again later.  */
    bdrv_set_dirty(source, sector_num, nb_sectors);
    return ret;
}

static void coroutine_fn mirror_run(void *opaque)
{
    MirrorBlockJob *s = opaque;
    BlockDriverState *bs = s->common.bs;
    int64_t sector_num, end, sectors_per_chunk, length;
    BlockDriverInfo bdi;
    char backing_filename[1024];
    int ret = 0;
    int n;

    if (block_job_is_cancelled(&s->common)) {
        goto immediate_exit;
    }

    s->common.len = bdrv_getlength(bs);
    if (s->common.len < 0) {
        block_job_completed(&s->common, s->common.len);
        return;
    }

    /* If we have no backing file yet in the destination, we cannot let
     * the destination do COW.  Instead, we copy sectors around the
     * dirty data if needed.  We need a bitmap to do that.
     */
    bdrv_get_backing_filename(s->target, backing_filename,
                              sizeof(backing_filename));
    if (backing_filename[0] && !s->target->backing_hd) {
        bdrv_get_info(s->target, &bdi);
        if (s->granularity < bdi.cluster_size) {
            s->buf_size = bdi.cluster_size;
            length = (bdrv_getlength(bs) + s->granularity - 1) / s->granularity;
            s->cow_bitmap = bitmap_new(length);
        }
    }

    end = s->common.len >> BDRV_SECTOR_BITS;
    s->buf = qemu_blockalign(bs, s->buf_size);
    sectors_per_chunk = s->granularity >> BDRV_SECTOR_BITS;

    if (s->mode != MIRROR_SYNC_MODE_NONE) {
        /* First part, loop on the sectors and initialize the dirty bitmap.  */
        BlockDriverState *base;
        base = s->mode == MIRROR_SYNC_MODE_FULL ? NULL : bs->backing_hd;
        for (sector_num = 0; sector_num < end; ) {
            int64_t next = (sector_num | (sectors_per_chunk - 1)) + 1;
            ret = bdrv_co_is_allocated_above(bs, base,
                                             sector_num, next - sector_num, &n);

            if (ret < 0) {
                goto immediate_exit;
            }

            assert(n > 0);
            if (ret == 1) {
                bdrv_set_dirty(bs, sector_num, n);
                sector_num = next;
            } else {
                sector_num += n;
            }
        }
    }

    bdrv_dirty_iter_init(bs, &s->hbi);
    for (;;) {
        uint64_t delay_ns;
        int64_t cnt;
        bool should_complete;

        cnt = bdrv_get_dirty_count(bs);
        if (cnt != 0) {
            BlockErrorAction action = BDRV_ACTION_REPORT;
            ret = mirror_iteration(s, &action);
            if (ret < 0 && action == BDRV_ACTION_REPORT) {
                goto immediate_exit;
            }
            cnt = bdrv_get_dirty_count(bs);
        }

        should_complete = false;
        if (cnt == 0) {
            trace_mirror_before_flush(s);
            ret = bdrv_flush(s->target);
            if (ret < 0) {
                if (mirror_error_action(s, false, -ret) == BDRV_ACTION_REPORT) {
                    goto immediate_exit;
                }
            } else {
                /* We're out of the streaming phase.  From now on, if the job
                 * is cancelled we will actually complete all pending I/O and
                 * report completion.  This way, block-job-cancel will leave
                 * the target in a consistent state.
                 */
                s->common.offset = end * BDRV_SECTOR_SIZE;
                if (!s->synced) {
                    block_job_ready(&s->common);
                    s->synced = true;
                }

                should_complete = s->should_complete ||
                    block_job_is_cancelled(&s->common);
                cnt = bdrv_get_dirty_count(bs);
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
            bdrv_drain_all();
            cnt = bdrv_get_dirty_count(bs);
        }

        ret = 0;
        trace_mirror_before_sleep(s, cnt, s->synced);
        if (!s->synced) {
            /* Publish progress */
            s->common.offset = (end - cnt) * BDRV_SECTOR_SIZE;

            if (s->common.speed) {
                delay_ns = ratelimit_calculate_delay(&s->limit, sectors_per_chunk);
            } else {
                delay_ns = 0;
            }

            /* Note that even when no rate limit is applied we need to yield
             * with no pending I/O here so that bdrv_drain_all() returns.
             */
            block_job_sleep_ns(&s->common, rt_clock, delay_ns);
            if (block_job_is_cancelled(&s->common)) {
                break;
            }
        } else if (!should_complete) {
            delay_ns = (cnt == 0 ? SLICE_TIME : 0);
            block_job_sleep_ns(&s->common, rt_clock, delay_ns);
        } else if (cnt == 0) {
            /* The two disks are in sync.  Exit and report successful
             * completion.
             */
            assert(QLIST_EMPTY(&bs->tracked_requests));
            s->common.cancelled = false;
            break;
        }
    }

immediate_exit:
    qemu_vfree(s->buf);
    g_free(s->cow_bitmap);
    bdrv_set_dirty_tracking(bs, 0);
    bdrv_iostatus_disable(s->target);
    if (s->should_complete && ret == 0) {
        if (bdrv_get_flags(s->target) != bdrv_get_flags(s->common.bs)) {
            bdrv_reopen(s->target, bdrv_get_flags(s->common.bs), NULL);
        }
        bdrv_swap(s->target, s->common.bs);
    }
    bdrv_close(s->target);
    bdrv_delete(s->target);
    block_job_completed(&s->common, ret);
}

static void mirror_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    if (speed < 0) {
        error_set(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static void mirror_iostatus_reset(BlockJob *job)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);

    bdrv_iostatus_reset(s->target);
}

static void mirror_complete(BlockJob *job, Error **errp)
{
    MirrorBlockJob *s = container_of(job, MirrorBlockJob, common);
    int ret;

    ret = bdrv_open_backing_file(s->target);
    if (ret < 0) {
        char backing_filename[PATH_MAX];
        bdrv_get_full_backing_filename(s->target, backing_filename,
                                       sizeof(backing_filename));
        error_set(errp, QERR_OPEN_FILE_FAILED, backing_filename);
        return;
    }
    if (!s->synced) {
        error_set(errp, QERR_BLOCK_JOB_NOT_READY, job->bs->device_name);
        return;
    }

    s->should_complete = true;
    block_job_resume(job);
}

static BlockJobType mirror_job_type = {
    .instance_size = sizeof(MirrorBlockJob),
    .job_type      = "mirror",
    .set_speed     = mirror_set_speed,
    .iostatus_reset= mirror_iostatus_reset,
    .complete      = mirror_complete,
};

void mirror_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, int64_t granularity, MirrorSyncMode mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  BlockDriverCompletionFunc *cb,
                  void *opaque, Error **errp)
{
    MirrorBlockJob *s;

    if (granularity == 0) {
        /* Choose the default granularity based on the target file's cluster
         * size, clamped between 4k and 64k.  */
        BlockDriverInfo bdi;
        if (bdrv_get_info(target, &bdi) >= 0 && bdi.cluster_size != 0) {
            granularity = MAX(4096, bdi.cluster_size);
            granularity = MIN(65536, granularity);
        } else {
            granularity = 65536;
        }
    }

    assert ((granularity & (granularity - 1)) == 0);

    if ((on_source_error == BLOCKDEV_ON_ERROR_STOP ||
         on_source_error == BLOCKDEV_ON_ERROR_ENOSPC) &&
        !bdrv_iostatus_is_enabled(bs)) {
        error_set(errp, QERR_INVALID_PARAMETER, "on-source-error");
        return;
    }

    s = block_job_create(&mirror_job_type, bs, speed, cb, opaque, errp);
    if (!s) {
        return;
    }

    s->on_source_error = on_source_error;
    s->on_target_error = on_target_error;
    s->target = target;
    s->mode = mode;
    s->granularity = granularity;
    s->buf_size = granularity;

    bdrv_set_dirty_tracking(bs, granularity);
    bdrv_set_enable_write_cache(s->target, true);
    bdrv_set_on_error(s->target, on_target_error, on_target_error);
    bdrv_iostatus_enable(s->target);
    s->common.co = qemu_coroutine_create(mirror_run);
    trace_mirror_start(bs, s, s->common.co, opaque);
    qemu_coroutine_enter(s->common.co, s);
}
