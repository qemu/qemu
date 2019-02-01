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
bdrv_test_co_truncate(BlockDriverState *bs, int64_t offset,
                      PreallocMode prealloc, Error **errp)
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
    ret = bdrv_truncate(c, 65536, PREALLOC_MODE_OFF, NULL);
    g_assert_cmpint(ret, ==, 0);

    /* Early error: Negative offset */
    ret = bdrv_truncate(c, -2, PREALLOC_MODE_OFF, NULL);
    g_assert_cmpint(ret, ==, -EINVAL);

    /* Error: Read-only image */
    c->bs->read_only = true;
    c->bs->open_flags &= ~BDRV_O_RDWR;

    ret = bdrv_truncate(c, 65536, PREALLOC_MODE_OFF, NULL);
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

    blk = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "base", BDRV_O_RDWR, &error_abort);
    bs->total_sectors = 65536 / BDRV_SECTOR_SIZE;
    blk_insert_bs(blk, bs, &error_abort);
    c = QLIST_FIRST(&bs->parents);

    blk_set_aio_context(blk, ctx);
    aio_context_acquire(ctx);
    t->fn(c);
    if (t->blkfn) {
        t->blkfn(blk);
    }
    aio_context_release(ctx);
    blk_set_aio_context(blk, qemu_get_aio_context());

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

    return g_test_run();
}
