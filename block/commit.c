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

#define SLICE_TIME 100000000ULL /* ns */

typedef struct CommitBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *active;
    BlockBackend *top;
    BlockBackend *base;
    BlockdevOnError on_error;
    int base_flags;
    int orig_overlay_flags;
    char *backing_file_str;
} CommitBlockJob;

static int coroutine_fn commit_populate(BlockBackend *bs, BlockBackend *base,
                                        int64_t sector_num, int nb_sectors,
                                        void *buf)
{
    int ret = 0;
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = nb_sectors * BDRV_SECTOR_SIZE,
    };

    qemu_iovec_init_external(&qiov, &iov, 1);

    ret = blk_co_preadv(bs, sector_num * BDRV_SECTOR_SIZE,
                        qiov.size, &qiov, 0);
    if (ret < 0) {
        return ret;
    }

    ret = blk_co_pwritev(base, sector_num * BDRV_SECTOR_SIZE,
                         qiov.size, &qiov, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

typedef struct {
    int ret;
} CommitCompleteData;

static void commit_complete(BlockJob *job, void *opaque)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common);
    CommitCompleteData *data = opaque;
    BlockDriverState *active = s->active;
    BlockDriverState *top = blk_bs(s->top);
    BlockDriverState *base = blk_bs(s->base);
    BlockDriverState *overlay_bs = bdrv_find_overlay(active, top);
    int ret = data->ret;

    if (!block_job_is_cancelled(&s->common) && ret == 0) {
        /* success */
        ret = bdrv_drop_intermediate(active, top, base, s->backing_file_str);
    }

    /* restore base open flags here if appropriate (e.g., change the base back
     * to r/o). These reopens do not need to be atomic, since we won't abort
     * even on failure here */
    if (s->base_flags != bdrv_get_flags(base)) {
        bdrv_reopen(base, s->base_flags, NULL);
    }
    if (overlay_bs && s->orig_overlay_flags != bdrv_get_flags(overlay_bs)) {
        bdrv_reopen(overlay_bs, s->orig_overlay_flags, NULL);
    }
    g_free(s->backing_file_str);
    blk_unref(s->top);
    blk_unref(s->base);
    block_job_completed(&s->common, ret);
    g_free(data);
}

static void coroutine_fn commit_run(void *opaque)
{
    CommitBlockJob *s = opaque;
    CommitCompleteData *data;
    int64_t sector_num, end;
    uint64_t delay_ns = 0;
    int ret = 0;
    int n = 0;
    void *buf = NULL;
    int bytes_written = 0;
    int64_t base_len;

    ret = s->common.len = blk_getlength(s->top);


    if (s->common.len < 0) {
        goto out;
    }

    ret = base_len = blk_getlength(s->base);
    if (base_len < 0) {
        goto out;
    }

    if (base_len < s->common.len) {
        ret = blk_truncate(s->base, s->common.len);
        if (ret) {
            goto out;
        }
    }

    end = s->common.len >> BDRV_SECTOR_BITS;
    buf = blk_blockalign(s->top, COMMIT_BUFFER_SIZE);

    for (sector_num = 0; sector_num < end; sector_num += n) {
        bool copy;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that bdrv_drain_all() returns.
         */
        block_job_sleep_ns(&s->common, QEMU_CLOCK_REALTIME, delay_ns);
        if (block_job_is_cancelled(&s->common)) {
            break;
        }
        /* Copy if allocated above the base */
        ret = bdrv_is_allocated_above(blk_bs(s->top), blk_bs(s->base),
                                      sector_num,
                                      COMMIT_BUFFER_SIZE / BDRV_SECTOR_SIZE,
                                      &n);
        copy = (ret == 1);
        trace_commit_one_iteration(s, sector_num, n, ret);
        if (copy) {
            ret = commit_populate(s->top, s->base, sector_num, n, buf);
            bytes_written += n * BDRV_SECTOR_SIZE;
        }
        if (ret < 0) {
            BlockErrorAction action =
                block_job_error_action(&s->common, false, s->on_error, -ret);
            if (action == BLOCK_ERROR_ACTION_REPORT) {
                goto out;
            } else {
                n = 0;
                continue;
            }
        }
        /* Publish progress */
        s->common.offset += n * BDRV_SECTOR_SIZE;

        if (copy && s->common.speed) {
            delay_ns = ratelimit_calculate_delay(&s->limit, n);
        }
    }

    ret = 0;

out:
    qemu_vfree(buf);

    data = g_malloc(sizeof(*data));
    data->ret = ret;
    block_job_defer_to_main_loop(&s->common, commit_complete, data);
}

static void commit_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    CommitBlockJob *s = container_of(job, CommitBlockJob, common);

    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static const BlockJobDriver commit_job_driver = {
    .instance_size = sizeof(CommitBlockJob),
    .job_type      = BLOCK_JOB_TYPE_COMMIT,
    .set_speed     = commit_set_speed,
    .start         = commit_run,
};

void commit_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, BlockDriverState *top, int64_t speed,
                  BlockdevOnError on_error, const char *backing_file_str,
                  Error **errp)
{
    CommitBlockJob *s;
    BlockReopenQueue *reopen_queue = NULL;
    int orig_overlay_flags;
    int orig_base_flags;
    BlockDriverState *iter;
    BlockDriverState *overlay_bs;
    Error *local_err = NULL;

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

    s = block_job_create(job_id, &commit_job_driver, bs, speed,
                         BLOCK_JOB_DEFAULT, NULL, NULL, errp);
    if (!s) {
        return;
    }

    orig_base_flags    = bdrv_get_flags(base);
    orig_overlay_flags = bdrv_get_flags(overlay_bs);

    /* convert base & overlay_bs to r/w, if necessary */
    if (!(orig_base_flags & BDRV_O_RDWR)) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, base, NULL,
                                         orig_base_flags | BDRV_O_RDWR);
    }
    if (!(orig_overlay_flags & BDRV_O_RDWR)) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, overlay_bs, NULL,
                                         orig_overlay_flags | BDRV_O_RDWR);
    }
    if (reopen_queue) {
        bdrv_reopen_multiple(bdrv_get_aio_context(bs), reopen_queue, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            block_job_unref(&s->common);
            return;
        }
    }


    /* Block all nodes between top and base, because they will
     * disappear from the chain after this operation. */
    assert(bdrv_chain_contains(top, base));
    for (iter = top; iter != backing_bs(base); iter = backing_bs(iter)) {
        block_job_add_bdrv(&s->common, iter);
    }
    /* overlay_bs must be blocked because it needs to be modified to
     * update the backing image string, but if it's the root node then
     * don't block it again */
    if (bs != overlay_bs) {
        block_job_add_bdrv(&s->common, overlay_bs);
    }

    s->base = blk_new();
    blk_insert_bs(s->base, base);

    s->top = blk_new();
    blk_insert_bs(s->top, top);

    s->active = bs;

    s->base_flags          = orig_base_flags;
    s->orig_overlay_flags  = orig_overlay_flags;

    s->backing_file_str = g_strdup(backing_file_str);

    s->on_error = on_error;

    trace_commit_start(bs, base, top, s);
    block_job_start(&s->common);
}


#define COMMIT_BUF_SECTORS 2048

/* commit COW file into the raw image */
int bdrv_commit(BlockDriverState *bs)
{
    BlockBackend *src, *backing;
    BlockDriver *drv = bs->drv;
    int64_t sector, total_sectors, length, backing_length;
    int n, ro, open_flags;
    int ret = 0;
    uint8_t *buf = NULL;

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
    open_flags =  bs->backing->bs->open_flags;

    if (ro) {
        if (bdrv_reopen(bs->backing->bs, open_flags | BDRV_O_RDWR, NULL)) {
            return -EACCES;
        }
    }

    src = blk_new();
    blk_insert_bs(src, bs);

    backing = blk_new();
    blk_insert_bs(backing, bs->backing->bs);

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
        ret = blk_truncate(backing, length);
        if (ret < 0) {
            goto ro_cleanup;
        }
    }

    total_sectors = length >> BDRV_SECTOR_BITS;

    /* blk_try_blockalign() for src will choose an alignment that works for
     * backing as well, so no need to compare the alignment manually. */
    buf = blk_try_blockalign(src, COMMIT_BUF_SECTORS * BDRV_SECTOR_SIZE);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto ro_cleanup;
    }

    for (sector = 0; sector < total_sectors; sector += n) {
        ret = bdrv_is_allocated(bs, sector, COMMIT_BUF_SECTORS, &n);
        if (ret < 0) {
            goto ro_cleanup;
        }
        if (ret) {
            ret = blk_pread(src, sector * BDRV_SECTOR_SIZE, buf,
                            n * BDRV_SECTOR_SIZE);
            if (ret < 0) {
                goto ro_cleanup;
            }

            ret = blk_pwrite(backing, sector * BDRV_SECTOR_SIZE, buf,
                             n * BDRV_SECTOR_SIZE, 0);
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

    blk_unref(src);
    blk_unref(backing);

    if (ro) {
        /* ignoring error return here */
        bdrv_reopen(bs->backing->bs, open_flags & ~BDRV_O_RDWR, NULL);
    }

    return ret;
}
