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

typedef struct BDRVBlkdebugState {
    BlockDriverState *hd;
} BDRVBlkdebugState;

static int blkdebug_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVBlkdebugState *s = bs->opaque;

    if (strncmp(filename, "blkdebug:", strlen("blkdebug:"))) {
        return -EINVAL;
    }
    filename += strlen("blkdebug:");

    return bdrv_file_open(&s->hd, filename, flags);
}

static BlockDriverAIOCB *blkdebug_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
    BlockDriverAIOCB *acb =
        bdrv_aio_readv(s->hd, sector_num, qiov, nb_sectors, cb, opaque);
    return acb;
}

static BlockDriverAIOCB *blkdebug_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkdebugState *s = bs->opaque;
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
