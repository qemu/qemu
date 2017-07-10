/*
 * Image streaming
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "sysemu/block-backend.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    STREAM_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

#define SLICE_TIME 100000000ULL /* ns */

typedef struct StreamBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *base;
    BlockdevOnError on_error;
    char *backing_file_str;
    int bs_flags;
} StreamBlockJob;

static int coroutine_fn stream_populate(BlockBackend *blk,
                                        int64_t offset, uint64_t bytes,
                                        void *buf)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len  = bytes,
    };
    QEMUIOVector qiov;

    assert(bytes < SIZE_MAX);
    qemu_iovec_init_external(&qiov, &iov, 1);

    /* Copy-on-read the unallocated clusters */
    return blk_co_preadv(blk, offset, qiov.size, &qiov, BDRV_REQ_COPY_ON_READ);
}

typedef struct {
    int ret;
} StreamCompleteData;

static void stream_complete(BlockJob *job, void *opaque)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common);
    StreamCompleteData *data = opaque;
    BlockDriverState *bs = blk_bs(job->blk);
    BlockDriverState *base = s->base;
    Error *local_err = NULL;

    if (!block_job_is_cancelled(&s->common) && bs->backing &&
        data->ret == 0) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (base) {
            base_id = s->backing_file_str;
            if (base->drv) {
                base_fmt = base->drv->format_name;
            }
        }
        data->ret = bdrv_change_backing_file(bs, base_id, base_fmt);
        bdrv_set_backing_hd(bs, base, &local_err);
        if (local_err) {
            error_report_err(local_err);
            data->ret = -EPERM;
            goto out;
        }
    }

out:
    /* Reopen the image back in read-only mode if necessary */
    if (s->bs_flags != bdrv_get_flags(bs)) {
        /* Give up write permissions before making it read-only */
        blk_set_perm(job->blk, 0, BLK_PERM_ALL, &error_abort);
        bdrv_reopen(bs, s->bs_flags, NULL);
    }

    g_free(s->backing_file_str);
    block_job_completed(&s->common, data->ret);
    g_free(data);
}

static void coroutine_fn stream_run(void *opaque)
{
    StreamBlockJob *s = opaque;
    StreamCompleteData *data;
    BlockBackend *blk = s->common.blk;
    BlockDriverState *bs = blk_bs(blk);
    BlockDriverState *base = s->base;
    int64_t offset = 0;
    uint64_t delay_ns = 0;
    int error = 0;
    int ret = 0;
    int64_t n = 0; /* bytes */
    void *buf;

    if (!bs->backing) {
        goto out;
    }

    s->common.len = bdrv_getlength(bs);
    if (s->common.len < 0) {
        ret = s->common.len;
        goto out;
    }

    buf = qemu_blockalign(bs, STREAM_BUFFER_SIZE);

    /* Turn on copy-on-read for the whole block device so that guest read
     * requests help us make progress.  Only do this when copying the entire
     * backing chain since the copy-on-read operation does not take base into
     * account.
     */
    if (!base) {
        bdrv_enable_copy_on_read(bs);
    }

    for ( ; offset < s->common.len; offset += n) {
        bool copy;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, delay_ns);
        if (block_job_is_cancelled(&s->common)) {
            break;
        }

        copy = false;

        ret = bdrv_is_allocated(bs, offset, STREAM_BUFFER_SIZE, &n);
        if (ret == 1) {
            /* Allocated in the top, no need to copy.  */
        } else if (ret >= 0) {
            /* Copy if allocated in the intermediate images.  Limit to the
             * known-unallocated area [offset, offset+n*BDRV_SECTOR_SIZE).  */
            ret = bdrv_is_allocated_above(backing_bs(bs), base,
                                          offset, n, &n);

            /* Finish early if end of backing file has been reached */
            if (ret == 0 && n == 0) {
                n = s->common.len - offset;
            }

            copy = (ret == 1);
        }
        trace_stream_one_iteration(s, offset, n, ret);
        if (copy) {
            ret = stream_populate(blk, offset, n, buf);
        }
        if (ret < 0) {
            BlockErrorAction action =
                block_job_error_action(&s->common, s->on_error, true, -ret);
            if (action == BLOCK_ERROR_ACTION_STOP) {
                n = 0;
                continue;
            }
            if (error == 0) {
                error = ret;
            }
            if (action == BLOCK_ERROR_ACTION_REPORT) {
                break;
            }
        }
        ret = 0;

        /* Publish progress */
        s->common.offset += n;
        if (copy && s->common.speed) {
            delay_ns = ratelimit_calculate_delay(&s->limit, n);
        }
    }

    if (!base) {
        bdrv_disable_copy_on_read(bs);
    }

    /* Do not remove the backing file if an error was there but ignored.  */
    ret = error;

    qemu_vfree(buf);

out:
    /* Modify backing chain and close BDSes in main loop */
    data = g_malloc(sizeof(*data));
    data->ret = ret;
    block_job_defer_to_main_loop(&s->common, stream_complete, data);
}

static void stream_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common);

    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed, SLICE_TIME);
}

static const BlockJobDriver stream_job_driver = {
    .instance_size = sizeof(StreamBlockJob),
    .job_type      = BLOCK_JOB_TYPE_STREAM,
    .set_speed     = stream_set_speed,
    .start         = stream_run,
};

void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  int64_t speed, BlockdevOnError on_error, Error **errp)
{
    StreamBlockJob *s;
    BlockDriverState *iter;
    int orig_bs_flags;

    /* Make sure that the image is opened in read-write mode */
    orig_bs_flags = bdrv_get_flags(bs);
    if (!(orig_bs_flags & BDRV_O_RDWR)) {
        if (bdrv_reopen(bs, orig_bs_flags | BDRV_O_RDWR, errp) != 0) {
            return;
        }
    }

    /* Prevent concurrent jobs trying to modify the graph structure here, we
     * already have our own plans. Also don't allow resize as the image size is
     * queried only at the job start and then cached. */
    s = block_job_create(job_id, &stream_job_driver, bs,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_GRAPH_MOD,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                         BLK_PERM_WRITE,
                         speed, BLOCK_JOB_DEFAULT, NULL, NULL, errp);
    if (!s) {
        goto fail;
    }

    /* Block all intermediate nodes between bs and base, because they will
     * disappear from the chain after this operation. The streaming job reads
     * every block only once, assuming that it doesn't change, so block writes
     * and resizes. */
    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                           BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED,
                           &error_abort);
    }

    s->base = base;
    s->backing_file_str = g_strdup(backing_file_str);
    s->bs_flags = orig_bs_flags;

    s->on_error = on_error;
    trace_stream_start(bs, base, s);
    block_job_start(&s->common);
    return;

fail:
    if (orig_bs_flags != bdrv_get_flags(bs)) {
        bdrv_reopen(bs, orig_bs_flags, NULL);
    }
}
