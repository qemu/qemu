/*
 * Null block driver
 *
 * Authors:
 *  Fam Zheng <famz@redhat.com>
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "block/block_int.h"

typedef struct {
    int64_t length;
} BDRVNullState;

static QemuOptsList runtime_opts = {
    .name = "null",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "",
        },
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "size of the null block",
        },
        { /* end of list */ }
    },
};

static int null_file_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    QemuOpts *opts;
    BDRVNullState *s = bs->opaque;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &error_abort);
    s->length =
        qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 1 << 30);
    qemu_opts_del(opts);
    return 0;
}

static void null_close(BlockDriverState *bs)
{
}

static int64_t null_getlength(BlockDriverState *bs)
{
    BDRVNullState *s = bs->opaque;
    return s->length;
}

static coroutine_fn int null_co_readv(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors,
                                      QEMUIOVector *qiov)
{
    return 0;
}

static coroutine_fn int null_co_writev(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors,
                                       QEMUIOVector *qiov)
{
    return 0;
}

static coroutine_fn int null_co_flush(BlockDriverState *bs)
{
    return 0;
}

typedef struct {
    BlockAIOCB common;
    QEMUBH *bh;
} NullAIOCB;

static const AIOCBInfo null_aiocb_info = {
    .aiocb_size = sizeof(NullAIOCB),
};

static void null_bh_cb(void *opaque)
{
    NullAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, 0);
    qemu_bh_delete(acb->bh);
    qemu_aio_unref(acb);
}

static inline BlockAIOCB *null_aio_common(BlockDriverState *bs,
                                          BlockCompletionFunc *cb,
                                          void *opaque)
{
    NullAIOCB *acb;

    acb = qemu_aio_get(&null_aiocb_info, bs, cb, opaque);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), null_bh_cb, acb);
    qemu_bh_schedule(acb->bh);
    return &acb->common;
}

static BlockAIOCB *null_aio_readv(BlockDriverState *bs,
                                  int64_t sector_num, QEMUIOVector *qiov,
                                  int nb_sectors,
                                  BlockCompletionFunc *cb,
                                  void *opaque)
{
    return null_aio_common(bs, cb, opaque);
}

static BlockAIOCB *null_aio_writev(BlockDriverState *bs,
                                   int64_t sector_num, QEMUIOVector *qiov,
                                   int nb_sectors,
                                   BlockCompletionFunc *cb,
                                   void *opaque)
{
    return null_aio_common(bs, cb, opaque);
}

static BlockAIOCB *null_aio_flush(BlockDriverState *bs,
                                  BlockCompletionFunc *cb,
                                  void *opaque)
{
    return null_aio_common(bs, cb, opaque);
}

static BlockDriver bdrv_null_co = {
    .format_name            = "null-co",
    .protocol_name          = "null-co",
    .instance_size          = sizeof(BDRVNullState),

    .bdrv_file_open         = null_file_open,
    .bdrv_close             = null_close,
    .bdrv_getlength         = null_getlength,

    .bdrv_co_readv          = null_co_readv,
    .bdrv_co_writev         = null_co_writev,
    .bdrv_co_flush_to_disk  = null_co_flush,
};

static BlockDriver bdrv_null_aio = {
    .format_name            = "null-aio",
    .protocol_name          = "null-aio",
    .instance_size          = sizeof(BDRVNullState),

    .bdrv_file_open         = null_file_open,
    .bdrv_close             = null_close,
    .bdrv_getlength         = null_getlength,

    .bdrv_aio_readv         = null_aio_readv,
    .bdrv_aio_writev        = null_aio_writev,
    .bdrv_aio_flush         = null_aio_flush,
};

static void bdrv_null_init(void)
{
    bdrv_register(&bdrv_null_co);
    bdrv_register(&bdrv_null_aio);
}

block_init(bdrv_null_init);
