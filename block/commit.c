/*
 * Live block commit
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Jeff Cody   <jcody@redhat.com>
 *  Based on stream.c by Stefan Hajnoczi
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "qemu/ratelimit.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    COMMIT_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

#define SLICE_TIME 100000000ULL /* ns */

typedef struct CommitBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *active;
    BlockDriverState *top;
    BlockDriverState *base;
    BlockdevOnError on_error;
    int base_flags;
    int orig_overlay_flags;
    char *backing_file_str;
} CommitBlockJob;

static int coroutine_fn commit_populate(BlockDriverState *bs,
                                        BlockDriverState *base,
                                        int64_t sector_num, int nb_sectors,
                                        void *buf)
{
    int ret = 0;

    ret = bdrv_read(bs, sector_num, buf, nb_sectors);
    if (ret) {
        return ret;
    }

    ret = bdrv_write(base, sector_num, buf, nb_sectors);
    if (ret) {
        return ret;
    }

    return 0;
}

static void coroutine_fn commit_run(void *opaque)
{
    CommitBlockJob *s = opaque;
    BlockDriverState *active = s->active;
    BlockDriverState *top = s->top;
    BlockDriverState *base = s->base;
    BlockDriverState *overlay_bs;
    int64_t sector_num, end;
    int ret = 0;
    int n = 0;
    void *buf;
    int bytes_written = 0;
    int64_t base_len;

    ret = s->common.len = bdrv_getlength(top);


    if (s->common.len < 0) {
        goto exit_restore_reopen;
    }

    ret = base_len = bdrv_getlength(base);
    if (base_len < 0) {
        goto exit_restore_reopen;
    }

    if (base_len < s->common.len) {
        ret = bdrv_truncate(base, s->common.len);
        if (ret) {
            goto exit_restore_reopen;
        }
    }

    end = s->common.len >> BDRV_SECTOR_BITS;
    buf = qemu_blockalign(top, COMMIT_BUFFER_SIZE);

    for (sector_num = 0; sector_num < end; sector_num += n) {
        uint64_t delay_ns = 0;
        bool copy;

wait:
        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, delay_ns);
        if (block_job_is_cancelled(&s->common)) {
            break;
        }
        /* Copy if allocated above the base */
        ret = bdrv_is_allocated_above(top, base, sector_num,
                                      COMMIT_BUFFER_SIZE / BDRV_SECTOR_SIZE,
                                      &n);
        copy = (ret == 1);
        trace_commit_one_iteration(s, sector_num, n, ret);
        if (copy) {
            if (s->common.speed) {
                delay_ns = ratelimit_calculate_delay(&s->limit, n);
                if (delay_ns > 0) {
                    goto wait;
                }
            }
            ret = commit_populate(top, base, sector_num, n, buf);
            bytes_written += n * BDRV_SECTOR_SIZE;
        }
        if (ret < 0) {
            if (s->on_error == BLOCKDEV_ON_ERROR_STOP ||
                s->on_error == BLOCKDEV_ON_ERROR_REPORT||
                (s->on_error == BLOCKDEV_ON_ERROR_ENOSPC && ret == -ENOSPC)) {
                goto exit_free_buf;
            } else {
                n = 0;
                continue;
            }
        }
        /* Publish progress */
        s->common.offset += n * BDRV_SECTOR_SIZE;
    }

    ret = 0;

    if (!block_job_is_cancelled(&s->common) && sector_num == end) {
        /* success */
        ret = bdrv_drop_intermediate(active, top, base, s->backing_file_str);
    }

exit_free_buf:
    qemu_vfree(buf);

exit_restore_reopen:
    /* restore base open flags here if appropriate (e.g., change the base back
     * to r/o). These reopens do not need to be atomic, since we won't abort
     * even on failure here */
    if (s->base_flags != bdrv_get_flags(base)) {
        bdrv_reopen(base, s->base_flags, NULL);
    }
    overlay_bs = bdrv_find_overlay(active, top);
    if (overlay_bs && s->orig_overlay_flags != bdrv_get_flags(overlay_bs)) {
        bdrv_reopen(overlay_bs, s->orig_overlay_flags, NULL);
    }
    g_free(s->backing_file_str);
    block_job_completed(&s->common, ret);
}

static void commit_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common);

    if (speed < 0) {
        error_set(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static const BlockJobDriver commit_job_driver = {
    .instance_size = sizeof(CommitBlockJob),
    .job_type      = BLOCK_JOB_TYPE_COMMIT,
    .set_speed     = commit_set_speed,
};

void commit_start(BlockDriverState *bs, BlockDriverState *base,
                  BlockDriverState *top, int64_t speed,
                  BlockdevOnError on_error, BlockCompletionFunc *cb,
                  void *opaque, const char *backing_file_str, Error **errp)
{
    CommitBlockJob *s;
    BlockReopenQueue *reopen_queue = NULL;
    int orig_overlay_flags;
    int orig_base_flags;
    BlockDriverState *overlay_bs;
    Error *local_err = NULL;

    if ((on_error == BLOCKDEV_ON_ERROR_STOP ||
         on_error == BLOCKDEV_ON_ERROR_ENOSPC) &&
        !bdrv_iostatus_is_enabled(bs)) {
        error_setg(errp, "Invalid parameter combination");
        return;
    }

    assert(top != bs);
    if (top == base) {
        error_setg(errp, "Invalid files for merge: top and base are the same");
        return;
    }

    overlay_bs = bdrv_find_overlay(bs, top);

    if (overlay_bs == NULL) {
        error_setg(errp, "Could not find overlay image for %s:", top->filename);
        return;
    }

    orig_base_flags    = bdrv_get_flags(base);
    orig_overlay_flags = bdrv_get_flags(overlay_bs);

    /* convert base & overlay_bs to r/w, if necessary */
    if (!(orig_base_flags & BDRV_O_RDWR)) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, base,
                                         orig_base_flags | BDRV_O_RDWR);
    }
    if (!(orig_overlay_flags & BDRV_O_RDWR)) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, overlay_bs,
                                         orig_overlay_flags | BDRV_O_RDWR);
    }
    if (reopen_queue) {
        bdrv_reopen_multiple(reopen_queue, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }
    }


    s = block_job_create(&commit_job_driver, bs, speed, cb, opaque, errp);
    if (!s) {
        return;
    }

    s->base   = base;
    s->top    = top;
    s->active = bs;

    s->base_flags          = orig_base_flags;
    s->orig_overlay_flags  = orig_overlay_flags;

    s->backing_file_str = g_strdup(backing_file_str);

    s->on_error = on_error;
    s->common.co = qemu_coroutine_create(commit_run);

    trace_commit_start(bs, base, top, s, s->common.co, opaque);
    qemu_coroutine_enter(s->common.co, s);
}
