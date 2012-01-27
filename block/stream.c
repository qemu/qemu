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
#include "block_int.h"

enum {
    /*
     * Size of data buffer for populating the image file.  This should be large
     * enough to process multiple clusters in a single call, so that populating
     * contiguous regions of the image is efficient.
     */
    STREAM_BUFFER_SIZE = 512 * 1024, /* in bytes */
};

#define SLICE_TIME 100000000ULL /* ns */

typedef struct {
    int64_t next_slice_time;
    uint64_t slice_quota;
    uint64_t dispatched;
} RateLimit;

static int64_t ratelimit_calculate_delay(RateLimit *limit, uint64_t n)
{
    int64_t delay_ns = 0;
    int64_t now = qemu_get_clock_ns(rt_clock);

    if (limit->next_slice_time < now) {
        limit->next_slice_time = now + SLICE_TIME;
        limit->dispatched = 0;
    }
    if (limit->dispatched + n > limit->slice_quota) {
        delay_ns = limit->next_slice_time - now;
    } else {
        limit->dispatched += n;
    }
    return delay_ns;
}

static void ratelimit_set_speed(RateLimit *limit, uint64_t speed)
{
    limit->slice_quota = speed / (1000000000ULL / SLICE_TIME);
}

typedef struct StreamBlockJob {
    BlockJob common;
    RateLimit limit;
    BlockDriverState *base;
    char backing_file_id[1024];
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

/*
 * Given an image chain: [BASE] -> [INTER1] -> [INTER2] -> [TOP]
 *
 * Return true if the given sector is allocated in top.
 * Return false if the given sector is allocated in intermediate images.
 * Return true otherwise.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 *  the specified sector) that are known to be in the same
 *  allocated/unallocated state.
 *
 */
static int coroutine_fn is_allocated_base(BlockDriverState *top,
                                          BlockDriverState *base,
                                          int64_t sector_num,
                                          int nb_sectors, int *pnum)
{
    BlockDriverState *intermediate;
    int ret, n;

    ret = bdrv_co_is_allocated(top, sector_num, nb_sectors, &n);
    if (ret) {
        *pnum = n;
        return ret;
    }

    /*
     * Is the unallocated chunk [sector_num, n] also
     * unallocated between base and top?
     */
    intermediate = top->backing_hd;

    while (intermediate) {
        int pnum_inter;

        /* reached base */
        if (intermediate == base) {
            *pnum = n;
            return 1;
        }
        ret = bdrv_co_is_allocated(intermediate, sector_num, nb_sectors,
                                   &pnum_inter);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            *pnum = pnum_inter;
            return 0;
        }

        /*
         * [sector_num, nb_sectors] is unallocated on top but intermediate
         * might have
         *
         * [sector_num+x, nr_sectors] allocated.
         */
        if (n > pnum_inter) {
            n = pnum_inter;
        }

        intermediate = intermediate->backing_hd;
    }

    return 1;
}

static void coroutine_fn stream_run(void *opaque)
{
    StreamBlockJob *s = opaque;
    BlockDriverState *bs = s->common.bs;
    BlockDriverState *base = s->base;
    int64_t sector_num, end;
    int ret = 0;
    int n;
    void *buf;

    s->common.len = bdrv_getlength(bs);
    if (s->common.len < 0) {
        block_job_complete(&s->common, s->common.len);
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
retry:
        if (block_job_is_cancelled(&s->common)) {
            break;
        }


        if (base) {
            ret = is_allocated_base(bs, base, sector_num,
                                    STREAM_BUFFER_SIZE / BDRV_SECTOR_SIZE, &n);
        } else {
            ret = bdrv_co_is_allocated(bs, sector_num,
                                       STREAM_BUFFER_SIZE / BDRV_SECTOR_SIZE,
                                       &n);
        }
        trace_stream_one_iteration(s, sector_num, n, ret);
        if (ret == 0) {
            if (s->common.speed) {
                uint64_t delay_ns = ratelimit_calculate_delay(&s->limit, n);
                if (delay_ns > 0) {
                    co_sleep_ns(rt_clock, delay_ns);

                    /* Recheck cancellation and that sectors are unallocated */
                    goto retry;
                }
            }
            ret = stream_populate(bs, sector_num, n, buf);
        }
        if (ret < 0) {
            break;
        }
        ret = 0;

        /* Publish progress */
        s->common.offset += n * BDRV_SECTOR_SIZE;

        /* Note that even when no rate limit is applied we need to yield
         * with no pending I/O here so that qemu_aio_flush() returns.
         */
        co_sleep_ns(rt_clock, 0);
    }

    if (!base) {
        bdrv_disable_copy_on_read(bs);
    }

    if (sector_num == end && ret == 0) {
        const char *base_id = NULL;
        if (base) {
            base_id = s->backing_file_id;
        }
        ret = bdrv_change_backing_file(bs, base_id, NULL);
    }

    qemu_vfree(buf);
    block_job_complete(&s->common, ret);
}

static int stream_set_speed(BlockJob *job, int64_t value)
{
    StreamBlockJob *s = container_of(job, StreamBlockJob, common);

    if (value < 0) {
        return -EINVAL;
    }
    job->speed = value;
    ratelimit_set_speed(&s->limit, value / BDRV_SECTOR_SIZE);
    return 0;
}

static BlockJobType stream_job_type = {
    .instance_size = sizeof(StreamBlockJob),
    .job_type      = "stream",
    .set_speed     = stream_set_speed,
};

int stream_start(BlockDriverState *bs, BlockDriverState *base,
                 const char *base_id, BlockDriverCompletionFunc *cb,
                 void *opaque)
{
    StreamBlockJob *s;
    Coroutine *co;

    s = block_job_create(&stream_job_type, bs, cb, opaque);
    if (!s) {
        return -EBUSY; /* bs must already be in use */
    }

    s->base = base;
    if (base_id) {
        pstrcpy(s->backing_file_id, sizeof(s->backing_file_id), base_id);
    }

    co = qemu_coroutine_create(stream_run);
    trace_stream_start(bs, base, s, co, opaque);
    qemu_coroutine_enter(co, s);
    return 0;
}
