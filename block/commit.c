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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "qapi/error.h"
#include "qemu/ratelimit.h"
#include "qemu/memalign.h"
#include "system/block-backend.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    COMMIT_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

typedef struct CommitBlockJob {
    BlockJob common;
    BlockDriverState *commit_top_bs;
    BlockBackend *top;
    BlockBackend *base;
    BlockDriverState *base_bs;
    BlockDriverState *base_overlay;
    BlockdevOnError on_error;
    bool base_read_only;
    bool chain_frozen;
    char *backing_file_str;
    bool backing_mask_protocol;
} CommitBlockJob;

static int commit_prepare(Job *job)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);

    bdrv_graph_rdlock_main_loop();
    bdrv_unfreeze_backing_chain(s->commit_top_bs, s->base_bs);
    s->chain_frozen = false;
    bdrv_graph_rdunlock_main_loop();

    /* Remove base node parent that still uses BLK_PERM_WRITE/RESIZE before
     * the normal backing chain can be restored. */
    blk_unref(s->base);
    s->base = NULL;

    /* FIXME: bdrv_drop_intermediate treats total failures and partial failures
     * identically. Further work is needed to disambiguate these cases. */
    return bdrv_drop_intermediate(s->commit_top_bs, s->base_bs,
                                  s->backing_file_str,
                                  s->backing_mask_protocol);
}

static void commit_abort(Job *job)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);
    BlockDriverState *top_bs = blk_bs(s->top);
    BlockDriverState *commit_top_backing_bs;

    if (s->chain_frozen) {
        bdrv_graph_rdlock_main_loop();
        bdrv_unfreeze_backing_chain(s->commit_top_bs, s->base_bs);
        bdrv_graph_rdunlock_main_loop();
    }

    /* Make sure commit_top_bs and top stay around until bdrv_replace_node() */
    bdrv_ref(top_bs);
    bdrv_ref(s->commit_top_bs);

    if (s->base) {
        blk_unref(s->base);
    }

    /* free the blockers on the intermediate nodes so that bdrv_replace_nodes
     * can succeed */
    block_job_remove_all_bdrv(&s->common);

    /* If bdrv_drop_intermediate() failed (or was not invoked), remove the
     * commit filter driver from the backing chain now. Do this as the final
     * step so that the 'consistent read' permission can be granted.
     *
     * XXX Can (or should) we somehow keep 'consistent read' blocked even
     * after the failed/cancelled commit job is gone? If we already wrote
     * something to base, the intermediate images aren't valid any more. */
    bdrv_graph_rdlock_main_loop();
    commit_top_backing_bs = s->commit_top_bs->backing->bs;
    bdrv_graph_rdunlock_main_loop();

    bdrv_drained_begin(commit_top_backing_bs);
    bdrv_graph_wrlock();
    bdrv_replace_node(s->commit_top_bs, commit_top_backing_bs, &error_abort);
    bdrv_graph_wrunlock();
    bdrv_drained_end(commit_top_backing_bs);

    bdrv_unref(s->commit_top_bs);
    bdrv_unref(top_bs);
}

static void commit_clean(Job *job)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);

    /* restore base open flags here if appropriate (e.g., change the base back
     * to r/o). These reopens do not need to be atomic, since we won't abort
     * even on failure here */
    if (s->base_read_only) {
        bdrv_reopen_set_read_only(s->base_bs, true, NULL);
    }

    g_free(s->backing_file_str);
    blk_unref(s->top);
}

static int coroutine_fn commit_run(Job *job, Error **errp)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);
    int64_t offset;
    int ret = 0;
    int64_t n = 0; /* bytes */
    QEMU_AUTO_VFREE void *buf = NULL;
    int64_t len, base_len;

    len = blk_co_getlength(s->top);
    if (len < 0) {
        return len;
    }
    job_progress_set_remaining(&s->common.job, len);

    base_len = blk_co_getlength(s->base);
    if (base_len < 0) {
        return base_len;
    }

    if (base_len < len) {
        ret = blk_co_truncate(s->base, len, false, PREALLOC_MODE_OFF, 0, NULL);
        if (ret) {
            return ret;
        }
    }

    buf = blk_blockalign(s->top, COMMIT_BUFFER_SIZE);

    for (offset = 0; offset < len; offset += n) {
        bool copy;
        bool error_in_source = true;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        block_job_ratelimit_sleep(&s->common);
        if (job_is_cancelled(&s->common.job)) {
            break;
        }
        /* Copy if allocated above the base */
        ret = blk_co_is_allocated_above(s->top, s->base_overlay, true,
                                        offset, COMMIT_BUFFER_SIZE, &n);
        copy = (ret > 0);
        trace_commit_one_iteration(s, offset, n, ret);
        if (copy) {
            assert(n < SIZE_MAX);

            ret = blk_co_pread(s->top, offset, n, buf, 0);
            if (ret >= 0) {
                ret = blk_co_pwrite(s->base, offset, n, buf, 0);
                if (ret < 0) {
                    error_in_source = false;
                }
            }
        }
        if (ret < 0) {
            BlockErrorAction action =
                block_job_error_action(&s->common, s->on_error,
                                       error_in_source, -ret);
            if (action == BLOCK_ERROR_ACTION_REPORT) {
                return ret;
            } else {
                n = 0;
                continue;
            }
        }
        /* Publish progress */
        job_progress_update(&s->common.job, n);

        if (copy) {
            block_job_ratelimit_processed_bytes(&s->common, n);
        }
    }

    return 0;
}

static const BlockJobDriver commit_job_driver = {
    .job_driver = {
        .instance_size = sizeof(CommitBlockJob),
        .job_type      = JOB_TYPE_COMMIT,
        .free          = block_job_free,
        .user_resume   = block_job_user_resume,
        .run           = commit_run,
        .prepare       = commit_prepare,
        .abort         = commit_abort,
        .clean         = commit_clean
    },
};

static int coroutine_fn GRAPH_RDLOCK
bdrv_commit_top_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static GRAPH_RDLOCK void bdrv_commit_top_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_commit_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       BdrvChildRole role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    *nperm = 0;
    *nshared = BLK_PERM_ALL;
}

/* Dummy node that provides consistent read to its users without requiring it
 * from its backing file and that allows writes on the backing file chain. */
static BlockDriver bdrv_commit_top = {
    .format_name                = "commit_top",
    .bdrv_co_preadv             = bdrv_commit_top_preadv,
    .bdrv_refresh_filename      = bdrv_commit_top_refresh_filename,
    .bdrv_child_perm            = bdrv_commit_top_child_perm,

    .is_filter                  = true,
    .filtered_child_is_backing  = true,
};

void commit_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, BlockDriverState *top,
                  int creation_flags, int64_t speed,
                  BlockdevOnError on_error, const char *backing_file_str,
                  bool backing_mask_protocol,
                  const char *filter_node_name, Error **errp)
{
    CommitBlockJob *s;
    BlockDriverState *iter;
    BlockDriverState *commit_top_bs = NULL;
    BlockDriverState *filtered_base;
    int64_t base_size, top_size;
    uint64_t base_perms, iter_shared_perms;
    int ret;

    GLOBAL_STATE_CODE();

    assert(top != bs);
    bdrv_graph_rdlock_main_loop();
    if (bdrv_skip_filters(top) == bdrv_skip_filters(base)) {
        error_setg(errp, "Invalid files for merge: top and base are the same");
        bdrv_graph_rdunlock_main_loop();
        return;
    }
    bdrv_graph_rdunlock_main_loop();

    base_size = bdrv_getlength(base);
    if (base_size < 0) {
        error_setg_errno(errp, -base_size, "Could not inquire base image size");
        return;
    }

    top_size = bdrv_getlength(top);
    if (top_size < 0) {
        error_setg_errno(errp, -top_size, "Could not inquire top image size");
        return;
    }

    base_perms = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
    if (base_size < top_size) {
        base_perms |= BLK_PERM_RESIZE;
    }

    s = block_job_create(job_id, &commit_job_driver, NULL, bs, 0, BLK_PERM_ALL,
                         speed, creation_flags, NULL, NULL, errp);
    if (!s) {
        return;
    }

    /* convert base to r/w, if necessary */
    s->base_read_only = bdrv_is_read_only(base);
    if (s->base_read_only) {
        if (bdrv_reopen_set_read_only(base, false, errp) != 0) {
            goto fail;
        }
    }

    /* Insert commit_top block node above top, so we can block consistent read
     * on the backing chain below it */
    commit_top_bs = bdrv_new_open_driver(&bdrv_commit_top, filter_node_name, 0,
                                         errp);
    if (commit_top_bs == NULL) {
        goto fail;
    }
    if (!filter_node_name) {
        commit_top_bs->implicit = true;
    }

    /* So that we can always drop this node */
    commit_top_bs->never_freeze = true;

    commit_top_bs->total_sectors = top->total_sectors;

    ret = bdrv_append(commit_top_bs, top, errp);
    bdrv_unref(commit_top_bs); /* referenced by new parents or failed */
    if (ret < 0) {
        commit_top_bs = NULL;
        goto fail;
    }

    s->commit_top_bs = commit_top_bs;

    /*
     * Block all nodes between top and base, because they will
     * disappear from the chain after this operation.
     * Note that this assumes that the user is fine with removing all
     * nodes (including R/W filters) between top and base.  Assuring
     * this is the responsibility of the interface (i.e. whoever calls
     * commit_start()).
     */
    bdrv_graph_wrlock();
    s->base_overlay = bdrv_find_overlay(top, base);
    assert(s->base_overlay);

    /*
     * The topmost node with
     * bdrv_skip_filters(filtered_base) == bdrv_skip_filters(base)
     */
    filtered_base = bdrv_cow_bs(s->base_overlay);
    assert(bdrv_skip_filters(filtered_base) == bdrv_skip_filters(base));

    /*
     * XXX BLK_PERM_WRITE needs to be allowed so we don't block ourselves
     * at s->base (if writes are blocked for a node, they are also blocked
     * for its backing file). The other options would be a second filter
     * driver above s->base.
     */
    iter_shared_perms = BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE;

    for (iter = top; iter != base; iter = bdrv_filter_or_cow_bs(iter)) {
        if (iter == filtered_base) {
            /*
             * From here on, all nodes are filters on the base.  This
             * allows us to share BLK_PERM_CONSISTENT_READ.
             */
            iter_shared_perms |= BLK_PERM_CONSISTENT_READ;
        }

        ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                 iter_shared_perms, errp);
        if (ret < 0) {
            bdrv_graph_wrunlock();
            goto fail;
        }
    }

    if (bdrv_freeze_backing_chain(commit_top_bs, base, errp) < 0) {
        bdrv_graph_wrunlock();
        goto fail;
    }
    s->chain_frozen = true;

    ret = block_job_add_bdrv(&s->common, "base", base, 0, BLK_PERM_ALL, errp);
    bdrv_graph_wrunlock();

    if (ret < 0) {
        goto fail;
    }

    s->base = blk_new(s->common.job.aio_context,
                      base_perms,
                      BLK_PERM_CONSISTENT_READ
                      | BLK_PERM_WRITE_UNCHANGED);
    ret = blk_insert_bs(s->base, base, errp);
    if (ret < 0) {
        goto fail;
    }
    blk_set_disable_request_queuing(s->base, true);
    s->base_bs = base;

    /* Required permissions are already taken with block_job_add_bdrv() */
    s->top = blk_new(s->common.job.aio_context, 0, BLK_PERM_ALL);
    ret = blk_insert_bs(s->top, top, errp);
    if (ret < 0) {
        goto fail;
    }
    blk_set_disable_request_queuing(s->top, true);

    s->backing_file_str = g_strdup(backing_file_str);
    s->backing_mask_protocol = backing_mask_protocol;
    s->on_error = on_error;

    trace_commit_start(bs, base, top, s);
    job_start(&s->common.job);
    return;

fail:
    if (s->chain_frozen) {
        bdrv_graph_rdlock_main_loop();
        bdrv_unfreeze_backing_chain(commit_top_bs, base);
        bdrv_graph_rdunlock_main_loop();
    }
    if (s->base) {
        blk_unref(s->base);
    }
    if (s->top) {
        blk_unref(s->top);
    }
    if (s->base_read_only) {
        bdrv_reopen_set_read_only(base, true, NULL);
    }
    job_early_fail(&s->common.job);
    /* commit_top_bs has to be replaced after deleting the block job,
     * otherwise this would fail because of lack of permissions. */
    if (commit_top_bs) {
        bdrv_drained_begin(top);
        bdrv_graph_wrlock();
        bdrv_replace_node(commit_top_bs, top, &error_abort);
        bdrv_graph_wrunlock();
        bdrv_drained_end(top);
    }
}


#define COMMIT_BUF_SIZE (2048 * BDRV_SECTOR_SIZE)

/* commit COW file into the raw image */
int bdrv_commit(BlockDriverState *bs)
{
    BlockBackend *src, *backing;
    BlockDriverState *backing_file_bs = NULL;
    BlockDriverState *commit_top_bs = NULL;
    BlockDriver *drv = bs->drv;
    AioContext *ctx;
    int64_t offset, length, backing_length;
    int ro;
    int64_t n;
    int ret = 0;
    QEMU_AUTO_VFREE uint8_t *buf = NULL;
    Error *local_err = NULL;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!drv)
        return -ENOMEDIUM;

    backing_file_bs = bdrv_cow_bs(bs);

    if (!backing_file_bs) {
        return -ENOTSUP;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT_SOURCE, NULL) ||
        bdrv_op_is_blocked(backing_file_bs, BLOCK_OP_TYPE_COMMIT_TARGET, NULL))
    {
        return -EBUSY;
    }

    ro = bdrv_is_read_only(backing_file_bs);

    if (ro) {
        if (bdrv_reopen_set_read_only(backing_file_bs, false, NULL)) {
            return -EACCES;
        }
    }

    ctx = bdrv_get_aio_context(bs);
    /* WRITE_UNCHANGED is required for bdrv_make_empty() */
    src = blk_new(ctx, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED,
                  BLK_PERM_ALL);
    backing = blk_new(ctx, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL);

    ret = blk_insert_bs(src, bs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        goto ro_cleanup;
    }

    /* Insert commit_top block node above backing, so we can write to it */
    commit_top_bs = bdrv_new_open_driver(&bdrv_commit_top, NULL, BDRV_O_RDWR,
                                         &local_err);
    if (commit_top_bs == NULL) {
        error_report_err(local_err);
        goto ro_cleanup;
    }

    bdrv_set_backing_hd(commit_top_bs, backing_file_bs, &error_abort);
    bdrv_set_backing_hd(bs, commit_top_bs, &error_abort);

    ret = blk_insert_bs(backing, backing_file_bs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        goto ro_cleanup;
    }

    length = blk_getlength(src);
    if (length < 0) {
        ret = length;
        goto ro_cleanup;
    }

    backing_length = blk_getlength(backing);
    if (backing_length < 0) {
        ret = backing_length;
        goto ro_cleanup;
    }

    /* If our top snapshot is larger than the backing file image,
     * grow the backing file image if possible.  If not possible,
     * we must return an error */
    if (length > backing_length) {
        ret = blk_truncate(backing, length, false, PREALLOC_MODE_OFF, 0,
                           &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            goto ro_cleanup;
        }
    }

    /* blk_try_blockalign() for src will choose an alignment that works for
     * backing as well, so no need to compare the alignment manually. */
    buf = blk_try_blockalign(src, COMMIT_BUF_SIZE);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto ro_cleanup;
    }

    for (offset = 0; offset < length; offset += n) {
        ret = bdrv_is_allocated(bs, offset, COMMIT_BUF_SIZE, &n);
        if (ret < 0) {
            goto ro_cleanup;
        }
        if (ret) {
            ret = blk_pread(src, offset, n, buf, 0);
            if (ret < 0) {
                goto ro_cleanup;
            }

            ret = blk_pwrite(backing, offset, n, buf, 0);
            if (ret < 0) {
                goto ro_cleanup;
            }
        }
    }

    ret = blk_make_empty(src, NULL);
    /* Ignore -ENOTSUP */
    if (ret < 0 && ret != -ENOTSUP) {
        goto ro_cleanup;
    }

    blk_flush(src);

    /*
     * Make sure all data we wrote to the backing device is actually
     * stable on disk.
     */
    blk_flush(backing);

    ret = 0;
ro_cleanup:
    blk_unref(backing);
    if (bdrv_cow_bs(bs) != backing_file_bs) {
        bdrv_set_backing_hd(bs, backing_file_bs, &error_abort);
    }
    bdrv_unref(commit_top_bs);
    blk_unref(src);

    if (ro) {
        /* ignoring error return here */
        bdrv_reopen_set_read_only(backing_file_bs, true, NULL);
    }

    return ret;
}
