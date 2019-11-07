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
     * Maximum chunk size to feed to copy-on-read.  This should be
     * large enough to process multiple clusters in a single call, so
     * that populating contiguous regions of the image is efficient.
     */
    STREAM_CHUNK = 512 * 1024, /* in bytes */
};

typedef struct StreamBlockJob {
    BlockJob common;
    BlockDriverState *bottom;
    BlockdevOnError on_error;
    char *backing_file_str;
    bool bs_read_only;
    bool chain_frozen;
} StreamBlockJob;

static int coroutine_fn stream_populate(BlockBackend *blk,
                                        int64_t offset, uint64_t bytes)
{
    assert(bytes < SIZE_MAX);

    return blk_co_preadv(blk, offset, bytes, NULL,
                         BDRV_REQ_COPY_ON_READ | BDRV_REQ_PREFETCH);
}

static void stream_abort(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);

    if (s->chain_frozen) {
        BlockJob *bjob = &s->common;
        bdrv_unfreeze_backing_chain(blk_bs(bjob->blk), s->bottom);
    }
}

static int stream_prepare(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);
    BlockDriverState *base = backing_bs(s->bottom);
    Error *local_err = NULL;
    int ret = 0;

    bdrv_unfreeze_backing_chain(bs, s->bottom);
    s->chain_frozen = false;

    if (bs->backing) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (base) {
            base_id = s->backing_file_str;
            if (base->drv) {
                base_fmt = base->drv->format_name;
            }
        }
        bdrv_set_backing_hd(bs, base, &local_err);
        ret = bdrv_change_backing_file(bs, base_id, base_fmt);
        if (local_err) {
            error_report_err(local_err);
            return -EPERM;
        }
    }

    return ret;
}

static void stream_clean(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockJob *bjob = &s->common;
    BlockDriverState *bs = blk_bs(bjob->blk);

    /* Reopen the image back in read-only mode if necessary */
    if (s->bs_read_only) {
        /* Give up write permissions before making it read-only */
        blk_set_perm(bjob->blk, 0, BLK_PERM_ALL, &error_abort);
        bdrv_reopen_set_read_only(bs, true, NULL);
    }

    g_free(s->backing_file_str);
}

static int coroutine_fn stream_run(Job *job, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockBackend *blk = s->common.blk;
    BlockDriverState *bs = blk_bs(blk);
    bool enable_cor = !backing_bs(s->bottom);
    int64_t len;
    int64_t offset = 0;
    uint64_t delay_ns = 0;
    int error = 0;
    int ret = 0;
    int64_t n = 0; /* bytes */

    if (bs == s->bottom) {
        /* Nothing to stream */
        return 0;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        return len;
    }
    job_progress_set_remaining(&s->common.job, len);

    /* Turn on copy-on-read for the whole block device so that guest read
     * requests help us make progress.  Only do this when copying the entire
     * backing chain since the copy-on-read operation does not take base into
     * account.
     */
    if (enable_cor) {
        bdrv_enable_copy_on_read(bs);
    }

    for ( ; offset < len; offset += n) {
        bool copy;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        job_sleep_ns(&s->common.job, delay_ns);
        if (job_is_cancelled(&s->common.job)) {
            break;
        }

        copy = false;

        ret = bdrv_is_allocated(bs, offset, STREAM_CHUNK, &n);
        if (ret == 1) {
            /* Allocated in the top, no need to copy.  */
        } else if (ret >= 0) {
            /* Copy if allocated in the intermediate images.  Limit to the
             * known-unallocated area [offset, offset+n*BDRV_SECTOR_SIZE).  */
            ret = bdrv_is_allocated_above(backing_bs(bs), s->bottom, true,
                                          offset, n, &n);
            /* Finish early if end of backing file has been reached */
            if (ret == 0 && n == 0) {
                n = len - offset;
            }

            copy = (ret == 1);
        }
        trace_stream_one_iteration(s, offset, n, ret);
        if (copy) {
            ret = stream_populate(blk, offset, n);
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
        job_progress_update(&s->common.job, n);
        if (copy) {
            delay_ns = block_job_ratelimit_get_delay(&s->common, n);
        } else {
            delay_ns = 0;
        }
    }

    if (enable_cor) {
        bdrv_disable_copy_on_read(bs);
    }

    /* Do not remove the backing file if an error was there but ignored. */
    return error;
}

static const BlockJobDriver stream_job_driver = {
    .job_driver = {
        .instance_size = sizeof(StreamBlockJob),
        .job_type      = JOB_TYPE_STREAM,
        .free          = block_job_free,
        .run           = stream_run,
        .prepare       = stream_prepare,
        .abort         = stream_abort,
        .clean         = stream_clean,
        .user_resume   = block_job_user_resume,
    },
};

void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  int creation_flags, int64_t speed,
                  BlockdevOnError on_error, Error **errp)
{
    StreamBlockJob *s;
    BlockDriverState *iter;
    bool bs_read_only;
    int basic_flags = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    BlockDriverState *bottom = bdrv_find_overlay(bs, base);

    if (bdrv_freeze_backing_chain(bs, bottom, errp) < 0) {
        return;
    }

    /* Make sure that the image is opened in read-write mode */
    bs_read_only = bdrv_is_read_only(bs);
    if (bs_read_only) {
        if (bdrv_reopen_set_read_only(bs, false, errp) != 0) {
            bs_read_only = false;
            goto fail;
        }
    }

    /* Prevent concurrent jobs trying to modify the graph structure here, we
     * already have our own plans. Also don't allow resize as the image size is
     * queried only at the job start and then cached. */
    s = block_job_create(job_id, &stream_job_driver, NULL, bs,
                         basic_flags | BLK_PERM_GRAPH_MOD,
                         basic_flags | BLK_PERM_WRITE,
                         speed, creation_flags, NULL, NULL, errp);
    if (!s) {
        goto fail;
    }

    /* Block all intermediate nodes between bs and base, because they will
     * disappear from the chain after this operation. The streaming job reads
     * every block only once, assuming that it doesn't change, so forbid writes
     * and resizes. Reassign the base node pointer because the backing BS of the
     * bottom node might change after the call to bdrv_reopen_set_read_only()
     * due to parallel block jobs running.
     */
    base = backing_bs(bottom);
    for (iter = backing_bs(bs); iter && iter != base; iter = backing_bs(iter)) {
        block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                           basic_flags, &error_abort);
    }

    s->bottom = bottom;
    s->backing_file_str = g_strdup(backing_file_str);
    s->bs_read_only = bs_read_only;
    s->chain_frozen = true;

    s->on_error = on_error;
    trace_stream_start(bs, base, s);
    job_start(&s->common.job);
    return;

fail:
    if (bs_read_only) {
        bdrv_reopen_set_read_only(bs, true, NULL);
    }
    bdrv_unfreeze_backing_chain(bs, bottom);
}
