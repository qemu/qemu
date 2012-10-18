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
#include "blockjob.h"
#include "block_int.h"
#include "qemu/ratelimit.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    BLOCK_SIZE = 512 * BDRV_SECTORS_PER_DIRTY_CHUNK, /* in bytes */
};

#define SLICE_TIME 100000000ULL /* ns */

typedef struct MirrorBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *target;
    MirrorSyncMode mode;
    bool synced;
    bool should_complete;
    int64_t sector_num;
    uint8_t *buf;
} MirrorBlockJob;

static int coroutine_fn mirror_iteration(MirrorBlockJob *s)
{
    BlockDriverState *source = s->common.bs;
    BlockDriverState *target = s->target;
    QEMUIOVector qiov;
    int ret, nb_sectors;
    int64_t end;
    struct iovec iov;

    end = s->common.len >> BDRV_SECTOR_BITS;
    s->sector_num = bdrv_get_next_dirty(source, s->sector_num);
    nb_sectors = MIN(BDRV_SECTORS_PER_DIRTY_CHUNK, end - s->sector_num);
    bdrv_reset_dirty(source, s->sector_num, nb_sectors);

    /* Copy the dirty cluster.  */
    iov.iov_base = s->buf;
    iov.iov_len  = nb_sectors * 512;
    qemu_iovec_init_external(&qiov, &iov, 1);

    trace_mirror_one_iteration(s, s->sector_num, nb_sectors);
    ret = bdrv_co_readv(source, s->sector_num, nb_sectors, &qiov);
    if (ret < 0) {
        return ret;
    }
    return bdrv_co_writev(target, s->sector_num, nb_sectors, &qiov);
}

static void coroutine_fn mirror_run(void *opaque)
{
    MirrorBlockJob *s = opaque;
    BlockDriverState *bs = s->common.bs;
    int64_t sector_num, end;
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

    end = s->common.len >> BDRV_SECTOR_BITS;
    s->buf = qemu_blockalign(bs, BLOCK_SIZE);

    if (s->mode != MIRROR_SYNC_MODE_NONE) {
        /* First part, loop on the sectors and initialize the dirty bitmap.  */
        BlockDriverState *base;
        base = s->mode == MIRROR_SYNC_MODE_FULL ? NULL : bs->backing_hd;
        for (sector_num = 0; sector_num < end; ) {
            int64_t next = (sector_num | (BDRV_SECTORS_PER_DIRTY_CHUNK - 1)) + 1;
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

    s->sector_num = -1;
    for (;;) {
        uint64_t delay_ns;
        int64_t cnt;
        bool should_complete;

        cnt = bdrv_get_dirty_count(bs);
        if (cnt != 0) {
            ret = mirror_iteration(s);
            if (ret < 0) {
                goto immediate_exit;
            }
            cnt = bdrv_get_dirty_count(bs);
        }

        should_complete = false;
        if (cnt == 0) {
            trace_mirror_before_flush(s);
            ret = bdrv_flush(s->target);
            if (ret < 0) {
                goto immediate_exit;
            }

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
            s->common.offset = end * BDRV_SECTOR_SIZE - cnt * BLOCK_SIZE;

            if (s->common.speed) {
                delay_ns = ratelimit_calculate_delay(&s->limit, BDRV_SECTORS_PER_DIRTY_CHUNK);
            } else {
                delay_ns = 0;
            }

            /* Note that even when no rate limit is applied we need to yield
             * with no pending I/O here so that qemu_aio_flush() returns.
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
    g_free(s->buf);
    bdrv_set_dirty_tracking(bs, false);
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
    .complete      = mirror_complete,
};

void mirror_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, MirrorSyncMode mode,
                  BlockDriverCompletionFunc *cb,
                  void *opaque, Error **errp)
{
    MirrorBlockJob *s;

    s = block_job_create(&mirror_job_type, bs, speed, cb, opaque, errp);
    if (!s) {
        return;
    }

    s->target = target;
    s->mode = mode;
    bdrv_set_dirty_tracking(bs, true);
    bdrv_set_enable_write_cache(s->target, true);
    s->common.co = qemu_coroutine_create(mirror_run);
    trace_mirror_start(bs, s, s->common.co, opaque);
    qemu_coroutine_enter(s->common.co, s);
}
