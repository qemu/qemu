/*
 * Block protocol for I/O error injection
 *
 * Copyright (c) 2010 Kevin Wolf <kwolf@redhat.com>
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

#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

#include <stdbool.h>

typedef struct BlkdebugVars {
    int state;

    /* If inject_errno != 0, an error is injected for requests */
    int inject_errno;

    /* Decides if all future requests fail (false) or only the next one and
     * after the next request inject_errno is reset to 0 (true) */
    bool inject_once;

    /* Decides if aio_readv/writev fails right away (true) or returns an error
     * return value only in the callback (false) */
    bool inject_immediately;
} BlkdebugVars;

typedef struct BDRVBlkdebugState {
    BlockDriverState *hd;
    BlkdebugVars vars;
} BDRVBlkdebugState;

typedef struct BlkdebugAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
} BlkdebugAIOCB;

static void blkdebug_aio_cancel(BlockDriverAIOCB *blockacb);

static AIOPool blkdebug_aio_pool = {
    .aiocb_size = sizeof(BlkdebugAIOCB),
    .cancel     = blkdebug_aio_cancel,
};

static int blkdebug_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (strncmp(filename, "blkdebug:", strlen("blkdebug:"))) {
        return -EINVAL;
    }
    filename += strlen("blkdebug:");

    return bdrv_file_open(&s->hd, filename, flags);
}

static void error_callback_bh(void *opaque)
{
    struct BlkdebugAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_release(acb);
}

static void blkdebug_aio_cancel(BlockDriverAIOCB *blockacb)
{
    BlkdebugAIOCB *acb = (BlkdebugAIOCB*) blockacb;
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *inject_error(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    int error = s->vars.inject_errno;
    struct BlkdebugAIOCB *acb;
    QEMUBH *bh;

    if (s->vars.inject_once) {
        s->vars.inject_errno = 0;
    }

    if (s->vars.inject_immediately) {
        return NULL;
    }

    acb = qemu_aio_get(&blkdebug_aio_pool, bs, cb, opaque);
    acb->ret = -error;

    bh = qemu_bh_new(error_callback_bh, acb);
    acb->bh = bh;
    qemu_bh_schedule(bh);

    return (BlockDriverAIOCB*) acb;
}

static BlockDriverAIOCB *blkdebug_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (s->vars.inject_errno) {
        return inject_error(bs, cb, opaque);
    }

    BlockDriverAIOCB *acb =
        bdrv_aio_readv(s->hd, sector_num, qiov, nb_sectors, cb, opaque);
    return acb;
}

static BlockDriverAIOCB *blkdebug_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (s->vars.inject_errno) {
        return inject_error(bs, cb, opaque);
    }

    BlockDriverAIOCB *acb =
        bdrv_aio_writev(s->hd, sector_num, qiov, nb_sectors, cb, opaque);
    return acb;
}

static void blkdebug_close(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    bdrv_delete(s->hd);
}

static void blkdebug_flush(BlockDriverState *bs)
{
    BDRVBlkdebugState *s = bs->opaque;
    bdrv_flush(s->hd);
}

static BlockDriverAIOCB *blkdebug_aio_flush(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    return bdrv_aio_flush(s->hd, cb, opaque);
}

static BlockDriver bdrv_blkdebug = {
    .format_name        = "blkdebug",
    .protocol_name      = "blkdebug",

    .instance_size      = sizeof(BDRVBlkdebugState),

    .bdrv_open          = blkdebug_open,
    .bdrv_close         = blkdebug_close,
    .bdrv_flush         = blkdebug_flush,

    .bdrv_aio_readv     = blkdebug_aio_readv,
    .bdrv_aio_writev    = blkdebug_aio_writev,
    .bdrv_aio_flush     = blkdebug_aio_flush,
};

static void bdrv_blkdebug_init(void)
{
    bdrv_register(&bdrv_blkdebug);
}

block_init(bdrv_blkdebug_init);
