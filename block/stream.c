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
#include "qapi/qmp/qdict.h"
#include "qemu/ratelimit.h"
#include "sysemu/block-backend.h"
#include "block/copy-on-read.h"

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
    BlockBackend *blk;
    BlockDriverState *base_overlay; /* COW overlay (stream from this) */
    BlockDriverState *above_base;   /* Node directly above the base */
    BlockDriverState *cor_filter_bs;
    BlockDriverState *target_bs;
    BlockdevOnError on_error;
    char *backing_file_str;
    bool bs_read_only;
} StreamBlockJob;

static int coroutine_fn stream_populate(BlockBackend *blk,
                                        int64_t offset, uint64_t bytes)
{
    assert(bytes < SIZE_MAX);

    return blk_co_preadv(blk, offset, bytes, NULL, BDRV_REQ_PREFETCH);
}

static int stream_prepare(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockDriverState *unfiltered_bs = bdrv_skip_filters(s->target_bs);
    BlockDriverState *base;
    BlockDriverState *unfiltered_base;
    Error *local_err = NULL;
    int ret = 0;

    /* We should drop filter at this point, as filter hold the backing chain */
    bdrv_cor_filter_drop(s->cor_filter_bs);
    s->cor_filter_bs = NULL;

    bdrv_subtree_drained_begin(s->above_base);

    base = bdrv_filter_or_cow_bs(s->above_base);
    if (base) {
        bdrv_ref(base);
    }

    unfiltered_base = bdrv_skip_filters(base);

    if (bdrv_cow_child(unfiltered_bs)) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (unfiltered_base) {
            base_id = s->backing_file_str ?: unfiltered_base->filename;
            if (unfiltered_base->drv) {
                base_fmt = unfiltered_base->drv->format_name;
            }
        }

        bdrv_set_backing_hd(unfiltered_bs, base, &local_err);
        ret = bdrv_change_backing_file(unfiltered_bs, base_id, base_fmt, false);
        if (local_err) {
            error_report_err(local_err);
            ret = -EPERM;
            goto out;
        }
    }

out:
    if (base) {
        bdrv_unref(base);
    }
    bdrv_subtree_drained_end(s->above_base);
    return ret;
}

static void stream_clean(Job *job)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);

    if (s->cor_filter_bs) {
        bdrv_cor_filter_drop(s->cor_filter_bs);
        s->cor_filter_bs = NULL;
    }

    blk_unref(s->blk);
    s->blk = NULL;

    /* Reopen the image back in read-only mode if necessary */
    if (s->bs_read_only) {
        /* Give up write permissions before making it read-only */
        bdrv_reopen_set_read_only(s->target_bs, true, NULL);
    }

    g_free(s->backing_file_str);
}

static int coroutine_fn stream_run(Job *job, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common.job);
    BlockDriverState *unfiltered_bs = bdrv_skip_filters(s->target_bs);
    int64_t len;
    int64_t offset = 0;
    uint64_t delay_ns = 0;
    int error = 0;
    int64_t n = 0; /* bytes */

    if (unfiltered_bs == s->base_overlay) {
        /* Nothing to stream */
        return 0;
    }

    len = bdrv_getlength(s->target_bs);
    if (len < 0) {
        return len;
    }
    job_progress_set_remaining(&s->common.job, len);

    for ( ; offset < len; offset += n) {
        bool copy;
        int ret;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        job_sleep_ns(&s->common.job, delay_ns);
        if (job_is_cancelled(&s->common.job)) {
            break;
        }

        copy = false;

        ret = bdrv_is_allocated(unfiltered_bs, offset, STREAM_CHUNK, &n);
        if (ret == 1) {
            /* Allocated in the top, no need to copy.  */
        } else if (ret >= 0) {
            /* Copy if allocated in the intermediate images.  Limit to the
             * known-unallocated area [offset, offset+n*BDRV_SECTOR_SIZE).  */
            ret = bdrv_is_allocated_above(bdrv_cow_bs(unfiltered_bs),
                                          s->base_overlay, true,
                                          offset, n, &n);
            /* Finish early if end of backing file has been reached */
            if (ret == 0 && n == 0) {
                n = len - offset;
            }

            copy = (ret > 0);
        }
        trace_stream_one_iteration(s, offset, n, ret);
        if (copy) {
            ret = stream_populate(s->blk, offset, n);
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

        /* Publish progress */
        job_progress_update(&s->common.job, n);
        if (copy) {
            delay_ns = block_job_ratelimit_get_delay(&s->common, n);
        } else {
            delay_ns = 0;
        }
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
        .clean         = stream_clean,
        .user_resume   = block_job_user_resume,
    },
};

void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  BlockDriverState *bottom,
                  int creation_flags, int64_t speed,
                  BlockdevOnError on_error,
                  const char *filter_node_name,
                  Error **errp)
{
    StreamBlockJob *s = NULL;
    BlockDriverState *iter;
    bool bs_read_only;
    int basic_flags = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    BlockDriverState *base_overlay;
    BlockDriverState *cor_filter_bs = NULL;
    BlockDriverState *above_base;
    QDict *opts;
    int ret;

    GLOBAL_STATE_CODE();

    assert(!(base && bottom));
    assert(!(backing_file_str && bottom));

    if (bottom) {
        /*
         * New simple interface. The code is written in terms of old interface
         * with @base parameter (still, it doesn't freeze link to base, so in
         * this mean old code is correct for new interface). So, for now, just
         * emulate base_overlay and above_base. Still, when old interface
         * finally removed, we should refactor code to use only "bottom", but
         * not "*base*" things.
         */
        assert(!bottom->drv->is_filter);
        base_overlay = above_base = bottom;
    } else {
        base_overlay = bdrv_find_overlay(bs, base);
        if (!base_overlay) {
            error_setg(errp, "'%s' is not in the backing chain of '%s'",
                       base->node_name, bs->node_name);
            return;
        }

        /*
         * Find the node directly above @base.  @base_overlay is a COW overlay,
         * so it must have a bdrv_cow_child(), but it is the immediate overlay
         * of @base, so between the two there can only be filters.
         */
        above_base = base_overlay;
        if (bdrv_cow_bs(above_base) != base) {
            above_base = bdrv_cow_bs(above_base);
            while (bdrv_filter_bs(above_base) != base) {
                above_base = bdrv_filter_bs(above_base);
            }
        }
    }

    /* Make sure that the image is opened in read-write mode */
    bs_read_only = bdrv_is_read_only(bs);
    if (bs_read_only) {
        int ret;
        /* Hold the chain during reopen */
        if (bdrv_freeze_backing_chain(bs, above_base, errp) < 0) {
            return;
        }

        ret = bdrv_reopen_set_read_only(bs, false, errp);

        /* failure, or cor-filter will hold the chain */
        bdrv_unfreeze_backing_chain(bs, above_base);

        if (ret < 0) {
            return;
        }
    }

    opts = qdict_new();

    qdict_put_str(opts, "driver", "copy-on-read");
    qdict_put_str(opts, "file", bdrv_get_node_name(bs));
    /* Pass the base_overlay node name as 'bottom' to COR driver */
    qdict_put_str(opts, "bottom", base_overlay->node_name);
    if (filter_node_name) {
        qdict_put_str(opts, "node-name", filter_node_name);
    }

    cor_filter_bs = bdrv_insert_node(bs, opts, BDRV_O_RDWR, errp);
    if (!cor_filter_bs) {
        goto fail;
    }

    if (!filter_node_name) {
        cor_filter_bs->implicit = true;
    }

    s = block_job_create(job_id, &stream_job_driver, NULL, cor_filter_bs,
                         0, BLK_PERM_ALL,
                         speed, creation_flags, NULL, NULL, errp);
    if (!s) {
        goto fail;
    }

    s->blk = blk_new_with_bs(cor_filter_bs, BLK_PERM_CONSISTENT_READ,
                             basic_flags | BLK_PERM_WRITE, errp);
    if (!s->blk) {
        goto fail;
    }
    /*
     * Disable request queuing in the BlockBackend to avoid deadlocks on drain:
     * The job reports that it's busy until it reaches a pause point.
     */
    blk_set_disable_request_queuing(s->blk, true);
    blk_set_allow_aio_context_change(s->blk, true);

    /*
     * Prevent concurrent jobs trying to modify the graph structure here, we
     * already have our own plans. Also don't allow resize as the image size is
     * queried only at the job start and then cached.
     */
    if (block_job_add_bdrv(&s->common, "active node", bs, 0,
                           basic_flags | BLK_PERM_WRITE, errp)) {
        goto fail;
    }

    /* Block all intermediate nodes between bs and base, because they will
     * disappear from the chain after this operation. The streaming job reads
     * every block only once, assuming that it doesn't change, so forbid writes
     * and resizes. Reassign the base node pointer because the backing BS of the
     * bottom node might change after the call to bdrv_reopen_set_read_only()
     * due to parallel block jobs running.
     * above_base node might change after the call to
     * bdrv_reopen_set_read_only() due to parallel block jobs running.
     */
    base = bdrv_filter_or_cow_bs(above_base);
    for (iter = bdrv_filter_or_cow_bs(bs); iter != base;
         iter = bdrv_filter_or_cow_bs(iter))
    {
        ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                 basic_flags, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    s->base_overlay = base_overlay;
    s->above_base = above_base;
    s->backing_file_str = g_strdup(backing_file_str);
    s->cor_filter_bs = cor_filter_bs;
    s->target_bs = bs;
    s->bs_read_only = bs_read_only;

    s->on_error = on_error;
    trace_stream_start(bs, base, s);
    job_start(&s->common.job);
    return;

fail:
    if (s) {
        job_early_fail(&s->common.job);
    }
    if (cor_filter_bs) {
        bdrv_cor_filter_drop(cor_filter_bs);
    }
    if (bs_read_only) {
        bdrv_reopen_set_read_only(bs, true, NULL);
    }
}
