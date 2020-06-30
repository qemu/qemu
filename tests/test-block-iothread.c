/*
 * Block tests for iothreads
 *
 * Copyright (c) 2018 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "block/blockjob_int.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/main-loop.h"
#include "iothread.h"

static int coroutine_fn bdrv_test_co_prwv(BlockDriverState *bs,
                                          uint64_t offset, uint64_t bytes,
                                          QEMUIOVector *qiov, int flags)
{
    return 0;
}

static int coroutine_fn bdrv_test_co_pdiscard(BlockDriverState *bs,
                                              int64_t offset, int bytes)
{
    return 0;
}

static int coroutine_fn
bdrv_test_co_truncate(BlockDriverState *bs, int64_t offset, bool exact,
                      PreallocMode prealloc, BdrvRequestFlags flags,
                      Error **errp)
{
    return 0;
}

static int coroutine_fn bdrv_test_co_block_status(BlockDriverState *bs,
                                                  bool want_zero,
                                                  int64_t offset, int64_t count,
                                                  int64_t *pnum, int64_t *map,
                                                  BlockDriverState **file)
{
    *pnum = count;
    return 0;
}

static BlockDriver bdrv_test = {
    .format_name            = "test",
    .instance_size          = 1,

    .bdrv_co_preadv         = bdrv_test_co_prwv,
    .bdrv_co_pwritev        = bdrv_test_co_prwv,
    .bdrv_co_pdiscard       = bdrv_test_co_pdiscard,
    .bdrv_co_truncate       = bdrv_test_co_truncate,
    .bdrv_co_block_status   = bdrv_test_co_block_status,
};

static void test_sync_op_pread(BdrvChild *c)
{
    uint8_t buf[512];
    int ret;

    /* Success */
    ret = bdrv_pread(c, 0, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, 512);

    /* Early error: Negative offset */
    ret = bdrv_pread(c, -2, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_pwrite(BdrvChild *c)
{
    uint8_t buf[512];
    int ret;

    /* Success */
    ret = bdrv_pwrite(c, 0, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, 512);

    /* Early error: Negative offset */
    ret = bdrv_pwrite(c, -2, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_blk_pread(BlockBackend *blk)
{
    uint8_t buf[512];
    int ret;

    /* Success */
    ret = blk_pread(blk, 0, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, 512);

    /* Early error: Negative offset */
    ret = blk_pread(blk, -2, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_blk_pwrite(BlockBackend *blk)
{
    uint8_t buf[512];
    int ret;

    /* Success */
    ret = blk_pwrite(blk, 0, buf, sizeof(buf), 0);
    g_assert_cmpint(ret, ==, 512);

    /* Early error: Negative offset */
    ret = blk_pwrite(blk, -2, buf, sizeof(buf), 0);
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_load_vmstate(BdrvChild *c)
{
    uint8_t buf[512];
    int ret;

    /* Error: Driver does not support snapshots */
    ret = bdrv_load_vmstate(c->bs, buf, 0, sizeof(buf));
    g_assert_cmpint(ret, ==, -ENOTSUP);
}

static void test_sync_op_save_vmstate(BdrvChild *c)
{
    uint8_t buf[512];
    int ret;

    /* Error: Driver does not support snapshots */
    ret = bdrv_save_vmstate(c->bs, buf, 0, sizeof(buf));
    g_assert_cmpint(ret, ==, -ENOTSUP);
}

static void test_sync_op_pdiscard(BdrvChild *c)
{
    int ret;

    /* Normal success path */
    c->bs->open_flags |= BDRV_O_UNMAP;
    ret = bdrv_pdiscard(c, 0, 512);
    g_assert_cmpint(ret, ==, 0);

    /* Early success: UNMAP not supported */
    c->bs->open_flags &= ~BDRV_O_UNMAP;
    ret = bdrv_pdiscard(c, 0, 512);
    g_assert_cmpint(ret, ==, 0);

    /* Early error: Negative offset */
    ret = bdrv_pdiscard(c, -2, 512);
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_blk_pdiscard(BlockBackend *blk)
{
    int ret;

    /* Early success: UNMAP not supported */
    ret = blk_pdiscard(blk, 0, 512);
    g_assert_cmpint(ret, ==, 0);

    /* Early error: Negative offset */
    ret = blk_pdiscard(blk, -2, 512);
    g_assert_cmpint(ret, ==, -EIO);
}

static void test_sync_op_truncate(BdrvChild *c)
{
    int ret;

    /* Normal success path */
    ret = bdrv_truncate(c, 65536, false, PREALLOC_MODE_OFF, 0, NULL);
    g_assert_cmpint(ret, ==, 0);

    /* Early error: Negative offset */
    ret = bdrv_truncate(c, -2, false, PREALLOC_MODE_OFF, 0, NULL);
    g_assert_cmpint(ret, ==, -EINVAL);

    /* Error: Read-only image */
    c->bs->read_only = true;
    c->bs->open_flags &= ~BDRV_O_RDWR;

    ret = bdrv_truncate(c, 65536, false, PREALLOC_MODE_OFF, 0, NULL);
    g_assert_cmpint(ret, ==, -EACCES);

    c->bs->read_only = false;
    c->bs->open_flags |= BDRV_O_RDWR;
}

static void test_sync_op_block_status(BdrvChild *c)
{
    int ret;
    int64_t n;

    /* Normal success path */
    ret = bdrv_is_allocated(c->bs, 0, 65536, &n);
    g_assert_cmpint(ret, ==, 0);

    /* Early success: No driver support */
    bdrv_test.bdrv_co_block_status = NULL;
    ret = bdrv_is_allocated(c->bs, 0, 65536, &n);
    g_assert_cmpint(ret, ==, 1);

    /* Early success: bytes = 0 */
    ret = bdrv_is_allocated(c->bs, 0, 0, &n);
    g_assert_cmpint(ret, ==, 0);

    /* Early success: Offset > image size*/
    ret = bdrv_is_allocated(c->bs, 0x1000000, 0x1000000, &n);
    g_assert_cmpint(ret, ==, 0);
}

static void test_sync_op_flush(BdrvChild *c)
{
    int ret;

    /* Normal success path */
    ret = bdrv_flush(c->bs);
    g_assert_cmpint(ret, ==, 0);

    /* Early success: Read-only image */
    c->bs->read_only = true;
    c->bs->open_flags &= ~BDRV_O_RDWR;

    ret = bdrv_flush(c->bs);
    g_assert_cmpint(ret, ==, 0);

    c->bs->read_only = false;
    c->bs->open_flags |= BDRV_O_RDWR;
}

static void test_sync_op_blk_flush(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    int ret;

    /* Normal success path */
    ret = blk_flush(blk);
    g_assert_cmpint(ret, ==, 0);

    /* Early success: Read-only image */
    bs->read_only = true;
    bs->open_flags &= ~BDRV_O_RDWR;

    ret = blk_flush(blk);
    g_assert_cmpint(ret, ==, 0);

    bs->read_only = false;
    bs->open_flags |= BDRV_O_RDWR;
}

static void test_sync_op_check(BdrvChild *c)
{
    BdrvCheckResult result;
    int ret;

    /* Error: Driver does not implement check */
    ret = bdrv_check(c->bs, &result, 0);
    g_assert_cmpint(ret, ==, -ENOTSUP);
}

static void test_sync_op_invalidate_cache(BdrvChild *c)
{
    /* Early success: Image is not inactive */
    bdrv_invalidate_cache(c->bs, NULL);
}


typedef struct SyncOpTest {
    const char *name;
    void (*fn)(BdrvChild *c);
    void (*blkfn)(BlockBackend *blk);
} SyncOpTest;

const SyncOpTest sync_op_tests[] = {
    {
        .name   = "/sync-op/pread",
        .fn     = test_sync_op_pread,
        .blkfn  = test_sync_op_blk_pread,
    }, {
        .name   = "/sync-op/pwrite",
        .fn     = test_sync_op_pwrite,
        .blkfn  = test_sync_op_blk_pwrite,
    }, {
        .name   = "/sync-op/load_vmstate",
        .fn     = test_sync_op_load_vmstate,
    }, {
        .name   = "/sync-op/save_vmstate",
        .fn     = test_sync_op_save_vmstate,
    }, {
        .name   = "/sync-op/pdiscard",
        .fn     = test_sync_op_pdiscard,
        .blkfn  = test_sync_op_blk_pdiscard,
    }, {
        .name   = "/sync-op/truncate",
        .fn     = test_sync_op_truncate,
    }, {
        .name   = "/sync-op/block_status",
        .fn     = test_sync_op_block_status,
    }, {
        .name   = "/sync-op/flush",
        .fn     = test_sync_op_flush,
        .blkfn  = test_sync_op_blk_flush,
    }, {
        .name   = "/sync-op/check",
        .fn     = test_sync_op_check,
    }, {
        .name   = "/sync-op/invalidate_cache",
        .fn     = test_sync_op_invalidate_cache,
    },
};

/* Test synchronous operations that run in a different iothread, so we have to
 * poll for the coroutine there to return. */
static void test_sync_op(const void *opaque)
{
    const SyncOpTest *t = opaque;
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    BlockBackend *blk;
    BlockDriverState *bs;
    BdrvChild *c;

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    bs->total_sectors = 65536 / BDRV_SECTOR_SIZE;
    blk_insert_bs(blk, bs, &error_abort);
    c = QLIST_FIRST(&bs->parents);

    blk_set_aio_context(blk, ctx, &error_abort);
    aio_context_acquire(ctx);
    t->fn(c);
    if (t->blkfn) {
        t->blkfn(blk);
    }
    blk_set_aio_context(blk, qemu_get_aio_context(), &error_abort);
    aio_context_release(ctx);

    bdrv_unref(bs);
    blk_unref(blk);
}

typedef struct TestBlockJob {
    BlockJob common;
    bool should_complete;
    int n;
} TestBlockJob;

static int test_job_prepare(Job *job)
{
    g_assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    return 0;
}

static int coroutine_fn test_job_run(Job *job, Error **errp)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);

    job_transition_to_ready(&s->common.job);
    while (!s->should_complete) {
        s->n++;
        g_assert(qemu_get_current_aio_context() == job->aio_context);

        /* Avoid job_sleep_ns() because it marks the job as !busy. We want to
         * emulate some actual activity (probably some I/O) here so that the
         * drain involved in AioContext switches has to wait for this activity
         * to stop. */
        qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 1000000);

        job_pause_point(&s->common.job);
    }

    g_assert(qemu_get_current_aio_context() == job->aio_context);
    return 0;
}

static void test_job_complete(Job *job, Error **errp)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common.job);
    s->should_complete = true;
}

BlockJobDriver test_job_driver = {
    .job_driver = {
        .instance_size  = sizeof(TestBlockJob),
        .free           = block_job_free,
        .user_resume    = block_job_user_resume,
        .run            = test_job_run,
        .complete       = test_job_complete,
        .prepare        = test_job_prepare,
    },
};

static void test_attach_blockjob(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    BlockBackend *blk;
    BlockDriverState *bs;
    TestBlockJob *tjob;

    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    blk_insert_bs(blk, bs, &error_abort);

    tjob = block_job_create("job0", &test_job_driver, NULL, bs,
                            0, BLK_PERM_ALL,
                            0, 0, NULL, NULL, &error_abort);
    job_start(&tjob->common.job);

    while (tjob->n == 0) {
        aio_poll(qemu_get_aio_context(), false);
    }

    blk_set_aio_context(blk, ctx, &error_abort);

    tjob->n = 0;
    while (tjob->n == 0) {
        aio_poll(qemu_get_aio_context(), false);
    }

    aio_context_acquire(ctx);
    blk_set_aio_context(blk, qemu_get_aio_context(), &error_abort);
    aio_context_release(ctx);

    tjob->n = 0;
    while (tjob->n == 0) {
        aio_poll(qemu_get_aio_context(), false);
    }

    blk_set_aio_context(blk, ctx, &error_abort);

    tjob->n = 0;
    while (tjob->n == 0) {
        aio_poll(qemu_get_aio_context(), false);
    }

    aio_context_acquire(ctx);
    job_complete_sync(&tjob->common.job, &error_abort);
    blk_set_aio_context(blk, qemu_get_aio_context(), &error_abort);
    aio_context_release(ctx);

    bdrv_unref(bs);
    blk_unref(blk);
}

/*
 * Test that changing the AioContext for one node in a tree (here through blk)
 * changes all other nodes as well:
 *
 *  blk
 *   |
 *   |  bs_verify [blkverify]
 *   |   /               \
 *   |  /                 \
 *  bs_a [bdrv_test]    bs_b [bdrv_test]
 *
 */
static void test_propagate_basic(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    AioContext *main_ctx;
    BlockBackend *blk;
    BlockDriverState *bs_a, *bs_b, *bs_verify;
    QDict *options;

    /*
     * Create bs_a and its BlockBackend.  We cannot take the RESIZE
     * permission because blkverify will not share it on the test
     * image.
     */
    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL & ~BLK_PERM_RESIZE,
                  BLK_PERM_ALL);
    bs_a = bdrv_new_open_driver(&bdrv_test, "bs_a", BDRV_O_RDWR, &error_abort);
    blk_insert_bs(blk, bs_a, &error_abort);

    /* Create bs_b */
    bs_b = bdrv_new_open_driver(&bdrv_test, "bs_b", BDRV_O_RDWR, &error_abort);

    /* Create blkverify filter that references both bs_a and bs_b */
    options = qdict_new();
    qdict_put_str(options, "driver", "blkverify");
    qdict_put_str(options, "test", "bs_a");
    qdict_put_str(options, "raw", "bs_b");

    bs_verify = bdrv_open(NULL, NULL, options, BDRV_O_RDWR, &error_abort);

    /* Switch the AioContext */
    blk_set_aio_context(blk, ctx, &error_abort);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs_a) == ctx);
    g_assert(bdrv_get_aio_context(bs_verify) == ctx);
    g_assert(bdrv_get_aio_context(bs_b) == ctx);

    /* Switch the AioContext back */
    main_ctx = qemu_get_aio_context();
    aio_context_acquire(ctx);
    blk_set_aio_context(blk, main_ctx, &error_abort);
    aio_context_release(ctx);
    g_assert(blk_get_aio_context(blk) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_a) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_verify) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_b) == main_ctx);

    bdrv_unref(bs_verify);
    bdrv_unref(bs_b);
    bdrv_unref(bs_a);
    blk_unref(blk);
}

/*
 * Test that diamonds in the graph don't lead to endless recursion:
 *
 *              blk
 *               |
 *      bs_verify [blkverify]
 *       /              \
 *      /                \
 *   bs_b [raw]         bs_c[raw]
 *      \                /
 *       \              /
 *       bs_a [bdrv_test]
 */
static void test_propagate_diamond(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    AioContext *main_ctx;
    BlockBackend *blk;
    BlockDriverState *bs_a, *bs_b, *bs_c, *bs_verify;
    QDict *options;

    /* Create bs_a */
    bs_a = bdrv_new_open_driver(&bdrv_test, "bs_a", BDRV_O_RDWR, &error_abort);

    /* Create bs_b and bc_c */
    options = qdict_new();
    qdict_put_str(options, "driver", "raw");
    qdict_put_str(options, "file", "bs_a");
    qdict_put_str(options, "node-name", "bs_b");
    bs_b = bdrv_open(NULL, NULL, options, BDRV_O_RDWR, &error_abort);

    options = qdict_new();
    qdict_put_str(options, "driver", "raw");
    qdict_put_str(options, "file", "bs_a");
    qdict_put_str(options, "node-name", "bs_c");
    bs_c = bdrv_open(NULL, NULL, options, BDRV_O_RDWR, &error_abort);

    /* Create blkverify filter that references both bs_b and bs_c */
    options = qdict_new();
    qdict_put_str(options, "driver", "blkverify");
    qdict_put_str(options, "test", "bs_b");
    qdict_put_str(options, "raw", "bs_c");

    bs_verify = bdrv_open(NULL, NULL, options, BDRV_O_RDWR, &error_abort);
    /*
     * Do not take the RESIZE permission: This would require the same
     * from bs_c and thus from bs_a; however, blkverify will not share
     * it on bs_b, and thus it will not be available for bs_a.
     */
    blk = blk_new(qemu_get_aio_context(), BLK_PERM_ALL & ~BLK_PERM_RESIZE,
                  BLK_PERM_ALL);
    blk_insert_bs(blk, bs_verify, &error_abort);

    /* Switch the AioContext */
    blk_set_aio_context(blk, ctx, &error_abort);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs_verify) == ctx);
    g_assert(bdrv_get_aio_context(bs_a) == ctx);
    g_assert(bdrv_get_aio_context(bs_b) == ctx);
    g_assert(bdrv_get_aio_context(bs_c) == ctx);

    /* Switch the AioContext back */
    main_ctx = qemu_get_aio_context();
    aio_context_acquire(ctx);
    blk_set_aio_context(blk, main_ctx, &error_abort);
    aio_context_release(ctx);
    g_assert(blk_get_aio_context(blk) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_verify) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_a) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_b) == main_ctx);
    g_assert(bdrv_get_aio_context(bs_c) == main_ctx);

    blk_unref(blk);
    bdrv_unref(bs_verify);
    bdrv_unref(bs_c);
    bdrv_unref(bs_b);
    bdrv_unref(bs_a);
}

static void test_propagate_mirror(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    AioContext *main_ctx = qemu_get_aio_context();
    BlockDriverState *src, *target, *filter;
    BlockBackend *blk;
    Job *job;
    Error *local_err = NULL;

    /* Create src and target*/
    src = bdrv_new_open_driver(&bdrv_test, "src", BDRV_O_RDWR, &error_abort);
    target = bdrv_new_open_driver(&bdrv_test, "target", BDRV_O_RDWR,
                                  &error_abort);

    /* Start a mirror job */
    mirror_start("job0", src, target, NULL, JOB_DEFAULT, 0, 0, 0,
                 MIRROR_SYNC_MODE_NONE, MIRROR_OPEN_BACKING_CHAIN, false,
                 BLOCKDEV_ON_ERROR_REPORT, BLOCKDEV_ON_ERROR_REPORT,
                 false, "filter_node", MIRROR_COPY_MODE_BACKGROUND,
                 &error_abort);
    job = job_get("job0");
    filter = bdrv_find_node("filter_node");

    /* Change the AioContext of src */
    bdrv_try_set_aio_context(src, ctx, &error_abort);
    g_assert(bdrv_get_aio_context(src) == ctx);
    g_assert(bdrv_get_aio_context(target) == ctx);
    g_assert(bdrv_get_aio_context(filter) == ctx);
    g_assert(job->aio_context == ctx);

    /* Change the AioContext of target */
    aio_context_acquire(ctx);
    bdrv_try_set_aio_context(target, main_ctx, &error_abort);
    aio_context_release(ctx);
    g_assert(bdrv_get_aio_context(src) == main_ctx);
    g_assert(bdrv_get_aio_context(target) == main_ctx);
    g_assert(bdrv_get_aio_context(filter) == main_ctx);

    /* With a BlockBackend on src, changing target must fail */
    blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    blk_insert_bs(blk, src, &error_abort);

    bdrv_try_set_aio_context(target, ctx, &local_err);
    error_free_or_abort(&local_err);

    g_assert(blk_get_aio_context(blk) == main_ctx);
    g_assert(bdrv_get_aio_context(src) == main_ctx);
    g_assert(bdrv_get_aio_context(target) == main_ctx);
    g_assert(bdrv_get_aio_context(filter) == main_ctx);

    /* ...unless we explicitly allow it */
    aio_context_acquire(ctx);
    blk_set_allow_aio_context_change(blk, true);
    bdrv_try_set_aio_context(target, ctx, &error_abort);
    aio_context_release(ctx);

    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(src) == ctx);
    g_assert(bdrv_get_aio_context(target) == ctx);
    g_assert(bdrv_get_aio_context(filter) == ctx);

    job_cancel_sync_all();

    aio_context_acquire(ctx);
    blk_set_aio_context(blk, main_ctx, &error_abort);
    bdrv_try_set_aio_context(target, main_ctx, &error_abort);
    aio_context_release(ctx);

    blk_unref(blk);
    bdrv_unref(src);
    bdrv_unref(target);
}

static void test_attach_second_node(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    AioContext *main_ctx = qemu_get_aio_context();
    BlockBackend *blk;
    BlockDriverState *bs, *filter;
    QDict *options;

    blk = blk_new(ctx, BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    blk_insert_bs(blk, bs, &error_abort);

    options = qdict_new();
    qdict_put_str(options, "driver", "raw");
    qdict_put_str(options, "file", "base");

    filter = bdrv_open(NULL, NULL, options, BDRV_O_RDWR, &error_abort);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs) == ctx);
    g_assert(bdrv_get_aio_context(filter) == ctx);

    aio_context_acquire(ctx);
    blk_set_aio_context(blk, main_ctx, &error_abort);
    aio_context_release(ctx);
    g_assert(blk_get_aio_context(blk) == main_ctx);
    g_assert(bdrv_get_aio_context(bs) == main_ctx);
    g_assert(bdrv_get_aio_context(filter) == main_ctx);

    bdrv_unref(filter);
    bdrv_unref(bs);
    blk_unref(blk);
}

static void test_attach_preserve_blk_ctx(void)
{
    IOThread *iothread = iothread_new();
    AioContext *ctx = iothread_get_aio_context(iothread);
    BlockBackend *blk;
    BlockDriverState *bs;

    blk = blk_new(ctx, BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    bs->total_sectors = 65536 / BDRV_SECTOR_SIZE;

    /* Add node to BlockBackend that has an iothread context assigned */
    blk_insert_bs(blk, bs, &error_abort);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs) == ctx);

    /* Remove the node again */
    aio_context_acquire(ctx);
    blk_remove_bs(blk);
    aio_context_release(ctx);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs) == qemu_get_aio_context());

    /* Re-attach the node */
    blk_insert_bs(blk, bs, &error_abort);
    g_assert(blk_get_aio_context(blk) == ctx);
    g_assert(bdrv_get_aio_context(bs) == ctx);

    aio_context_acquire(ctx);
    blk_set_aio_context(blk, qemu_get_aio_context(), &error_abort);
    aio_context_release(ctx);
    bdrv_unref(bs);
    blk_unref(blk);
}

int main(int argc, char **argv)
{
    int i;

    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(sync_op_tests); i++) {
        const SyncOpTest *t = &sync_op_tests[i];
        g_test_add_data_func(t->name, t, test_sync_op);
    }

    g_test_add_func("/attach/blockjob", test_attach_blockjob);
    g_test_add_func("/attach/second_node", test_attach_second_node);
    g_test_add_func("/attach/preserve_blk_ctx", test_attach_preserve_blk_ctx);
    g_test_add_func("/propagate/basic", test_propagate_basic);
    g_test_add_func("/propagate/diamond", test_propagate_diamond);
    g_test_add_func("/propagate/mirror", test_propagate_mirror);

    return g_test_run();
}
