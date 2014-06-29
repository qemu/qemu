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
    STREAM_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

#define SLICE_TIME 100000000ULL /* ns */

typedef struct StreamBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *base;
    BlockdevOnError on_error;
    char *backing_file_str;
} StreamBlockJob;

static int coroutine_fn stream_populate(BlockDriverState *bs,
                                        int64_t sector_num, int nb_sectors,
                                        void *buf)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len  = nb_sectors * BDRV_SECTOR_SIZE,
    };
    QEMUIOVector qiov;

    qemu_iovec_init_external(&qiov, &iov, 1);

    /* Copy-on-read the unallocated clusters */
    return bdrv_co_copy_on_readv(bs, sector_num, nb_sectors, &qiov);
}

static void close_unused_images(BlockDriverState *top, BlockDriverState *base,
                                const char *base_id)
{
    BlockDriverState *intermediate;
    intermediate = top->backing_hd;

    /* Must assign before bdrv_delete() to prevent traversing dangling pointer
     * while we delete backing image instances.
     */
    bdrv_set_backing_hd(top, base);

    while (intermediate) {
        BlockDriverState *unused;

        /* reached base */
        if (intermediate == base) {
            break;
        }

        unused = intermediate;
        intermediate = intermediate->backing_hd;
        bdrv_set_backing_hd(unused, NULL);
        bdrv_unref(unused);
    }

    bdrv_refresh_limits(top);
}

static void coroutine_fn stream_run(void *opaque)
{
    StreamBlockJob *s = opaque;
    BlockDriverState *bs = s->common.bs;
    BlockDriverState *base = s->base;
    int64_t sector_num, end;
    int error = 0;
    int ret = 0;
    int n = 0;
    void *buf;

    if (!bs->backing_hd) {
        block_job_completed(&s->common, 0);
        return;
    }

    s->common.len = bdrv_getlength(bs);
    if (s->common.len < 0) {
        block_job_completed(&s->common, s->common.len);
        return;
    }

    end = s->common.len >> BDRV_SECTOR_BITS;
    buf = qemu_blockalign(bs, STREAM_BUFFER_SIZE);

    /* Turn on copy-on-read for the whole block device so that guest read
     * requests help us make progress.  Only do this when copying the entire
     * backing chain since the copy-on-read operation does not take base into
     * account.
     */
    if (!base) {
        bdrv_enable_copy_on_read(bs);
    }

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

        copy = false;

        ret = bdrv_is_allocated(bs, sector_num,
                                STREAM_BUFFER_SIZE / BDRV_SECTOR_SIZE, &n);
        if (ret == 1) {
            /* Allocated in the top, no need to copy.  */
        } else if (ret >= 0) {
            /* Copy if allocated in the intermediate images.  Limit to the
             * known-unallocated area [sector_num, sector_num+n).  */
            ret = bdrv_is_allocated_above(bs->backing_hd, base,
                                          sector_num, n, &n);

            /* Finish early if end of backing file has been reached */
            if (ret == 0 && n == 0) {
                n = end - sector_num;
            }

            copy = (ret == 1);
        }
        trace_stream_one_iteration(s, sector_num, n, ret);
        if (copy) {
            if (s->common.speed) {
                delay_ns = ratelimit_calculate_delay(&s->limit, n);
                if (delay_ns > 0) {
                    goto wait;
                }
            }
            ret = stream_populate(bs, sector_num, n, buf);
        }
        if (ret < 0) {
            BlockErrorAction action =
                block_job_error_action(&s->common, s->common.bs, s->on_error,
                                       true, -ret);
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
        s->common.offset += n * BDRV_SECTOR_SIZE;
    }

    if (!base) {
        bdrv_disable_copy_on_read(bs);
    }

    /* Do not remove the backing file if an error was there but ignored.  */
    ret = error;

    if (!block_job_is_cancelled(&s->common) && sector_num == end && ret == 0) {
        const char *base_id = NULL, *base_fmt = NULL;
        if (base) {
            base_id = s->backing_file_str;
            if (base->drv) {
                base_fmt = base->drv->format_name;
            }
        }
        ret = bdrv_change_backing_file(bs, base_id, base_fmt);
        close_unused_images(bs, base, base_id);
    }

    qemu_vfree(buf);
    g_free(s->backing_file_str);
    block_job_completed(&s->common, ret);
}

static void stream_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common);

    if (speed < 0) {
        error_set(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static const BlockJobDriver stream_job_driver = {
    .instance_size = sizeof(StreamBlockJob),
    .job_type      = BLOCK_JOB_TYPE_STREAM,
    .set_speed     = stream_set_speed,
};

void stream_start(BlockDriverState *bs, BlockDriverState *base,
                  const char *backing_file_str, int64_t speed,
                  BlockdevOnError on_error,
                  BlockDriverCompletionFunc *cb,
                  void *opaque, Error **errp)
{
    StreamBlockJob *s;

    if ((on_error == BLOCKDEV_ON_ERROR_STOP ||
         on_error == BLOCKDEV_ON_ERROR_ENOSPC) &&
        !bdrv_iostatus_is_enabled(bs)) {
        error_set(errp, QERR_INVALID_PARAMETER, "on-error");
        return;
    }

    s = block_job_create(&stream_job_driver, bs, speed, cb, opaque, errp);
    if (!s) {
        return;
    }

    s->base = base;
    s->backing_file_str = g_strdup(backing_file_str);

    s->on_error = on_error;
    s->common.co = qemu_coroutine_create(stream_run);
    trace_stream_start(bs, base, s, s->common.co, opaque);
    qemu_coroutine_enter(s->common.co, s);
}
