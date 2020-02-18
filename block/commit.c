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
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "sysemu/block-backend.h"

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
    BlockdevOnError on_error;
    bool base_read_only;
    bool chain_frozen;
    char *backing_file_str;
} CommitBlockJob;

static int commit_prepare(Job *job)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);

    bdrv_unfreeze_backing_chain(s->commit_top_bs, s->base_bs);
    s->chain_frozen = false;

    /* Remove base node parent that still uses BLK_PERM_WRITE/RESIZE before
     * the normal backing chain can be restored. */
    blk_unref(s->base);
    s->base = NULL;

    /* FIXME: bdrv_drop_intermediate treats total failures and partial failures
     * identically. Further work is needed to disambiguate these cases. */
    return bdrv_drop_intermediate(s->commit_top_bs, s->base_bs,
                                  s->backing_file_str);
}

static void commit_abort(Job *job)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common.job);
    BlockDriverState *top_bs = blk_bs(s->top);

    if (s->chain_frozen) {
        bdrv_unfreeze_backing_chain(s->commit_top_bs, s->base_bs);
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
    bdrv_replace_node(s->commit_top_bs, backing_bs(s->commit_top_bs),
                      &error_abort);

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
    uint64_t delay_ns = 0;
    int ret = 0;
    int64_t n = 0; /* bytes */
    void *buf = NULL;
    int64_t len, base_len;

    ret = len = blk_getlength(s->top);
    if (len < 0) {
        goto out;
    }
    job_progress_set_remaining(&s->common.job, len);

    ret = base_len = blk_getlength(s->base);
    if (base_len < 0) {
        goto out;
    }

    if (base_len < len) {
        ret = blk_truncate(s->base, len, false, PREALLOC_MODE_OFF, NULL);
        if (ret) {
            goto out;
        }
    }

    buf = blk_blockalign(s->top, COMMIT_BUFFER_SIZE);

    for (offset = 0; offset < len; offset += n) {
        bool copy;
        bool error_in_source = true;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        job_sleep_ns(&s->common.job, delay_ns);
        if (job_is_cancelled(&s->common.job)) {
            break;
        }
        /* Copy if allocated above the base */
        ret = bdrv_is_allocated_above(blk_bs(s->top), blk_bs(s->base), false,
                                      offset, COMMIT_BUFFER_SIZE, &n);
        copy = (ret == 1);
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
                goto out;
            } else {
                n = 0;
                continue;
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

    ret = 0;

out:
    qemu_vfree(buf);

    return ret;
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

static int coroutine_fn bdrv_commit_top_preadv(BlockDriverState *bs,
    uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static void bdrv_commit_top_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void bdrv_commit_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       const BdrvChildRole *role,
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
    .bdrv_co_block_status       = bdrv_co_block_status_from_backing,
    .bdrv_refresh_filename      = bdrv_commit_top_refresh_filename,
    .bdrv_child_perm            = bdrv_commit_top_child_perm,
};

void commit_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, BlockDriverState *top,
                  int creation_flags, int64_t speed,
                  BlockdevOnError on_error, const char *backing_file_str,
                  const char *filter_node_name, Error **errp)
{
    CommitBlockJob *s;
    BlockDriverState *iter;
    BlockDriverState *commit_top_bs = NULL;
    Error *local_err = NULL;
    int ret;

    assert(top != bs);
    if (top == base) {
        error_setg(errp, "Invalid files for merge: top and base are the same");
        return;
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

    bdrv_append(commit_top_bs, top, &local_err);
    if (local_err) {
        commit_top_bs = NULL;
        error_propagate(errp, local_err);
        goto fail;
    }

    s->commit_top_bs = commit_top_bs;

    /* Block all nodes between top and base, because they will
     * disappear from the chain after this operation. */
    assert(bdrv_chain_contains(top, base));
    for (iter = top; iter != base; iter = backing_bs(iter)) {
        /* XXX BLK_PERM_WRITE needs to be allowed so we don't block ourselves
         * at s->base (if writes are blocked for a node, they are also blocked
         * for its backing file). The other options would be a second filter
         * driver above s->base. */
        ret = block_job_add_bdrv(&s->common, "intermediate node", iter, 0,
                                 BLK_PERM_WRITE_UNCHANGED | BLK_PERM_WRITE,
                                 errp);
        if (ret < 0) {
            goto fail;
        }
    }

    if (bdrv_freeze_backing_chain(commit_top_bs, base, errp) < 0) {
        goto fail;
    }
    s->chain_frozen = true;

    ret = block_job_add_bdrv(&s->common, "base", base, 0, BLK_PERM_ALL, errp);
    if (ret < 0) {
        goto fail;
    }

    s->base = blk_new(s->common.job.aio_context,
                      BLK_PERM_CONSISTENT_READ
                      | BLK_PERM_WRITE
                      | BLK_PERM_RESIZE,
                      BLK_PERM_CONSISTENT_READ
                      | BLK_PERM_GRAPH_MOD
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
    s->on_error = on_error;

    trace_commit_start(bs, base, top, s);
    job_start(&s->common.job);
    return;

fail:
    if (s->chain_frozen) {
        bdrv_unfreeze_backing_chain(commit_top_bs, base);
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
        bdrv_replace_node(commit_top_bs, top, &error_abort);
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
    uint8_t *buf = NULL;
    Error *local_err = NULL;

    if (!drv)
        return -ENOMEDIUM;

    if (!bs->backing) {
        return -ENOTSUP;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT_SOURCE, NULL) ||
        bdrv_op_is_blocked(bs->backing->bs, BLOCK_OP_TYPE_COMMIT_TARGET, NULL)) {
        return -EBUSY;
    }

    ro = bs->backing->bs->read_only;

    if (ro) {
        if (bdrv_reopen_set_read_only(bs->backing->bs, false, NULL)) {
            return -EACCES;
        }
    }

    ctx = bdrv_get_aio_context(bs);
    src = blk_new(ctx, BLK_PERM_CONSISTENT_READ, BLK_PERM_ALL);
    backing = blk_new(ctx, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL);

    ret = blk_insert_bs(src, bs, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        goto ro_cleanup;
    }

    /* Insert commit_top block node above backing, so we can write to it */
    backing_file_bs = backing_bs(bs);

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
        ret = blk_truncate(backing, length, false, PREALLOC_MODE_OFF,
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
            ret = blk_pread(src, offset, buf, n);
            if (ret < 0) {
                goto ro_cleanup;
            }

            ret = blk_pwrite(backing, offset, buf, n, 0);
            if (ret < 0) {
                goto ro_cleanup;
            }
        }
    }

    if (drv->bdrv_make_empty) {
        ret = drv->bdrv_make_empty(bs);
        if (ret < 0) {
            goto ro_cleanup;
        }
        blk_flush(src);
    }

    /*
     * Make sure all data we wrote to the backing device is actually
     * stable on disk.
     */
    blk_flush(backing);

    ret = 0;
ro_cleanup:
    qemu_vfree(buf);

    blk_unref(backing);
    if (backing_file_bs) {
        bdrv_set_backing_hd(bs, backing_file_bs, &error_abort);
    }
    bdrv_unref(commit_top_bs);
    blk_unref(src);

    if (ro) {
        /* ignoring error return here */
        bdrv_reopen_set_read_only(bs->backing->bs, true, NULL);
    }

    return ret;
}
