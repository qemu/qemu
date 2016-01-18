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

#include "qemu/osdep.h"
#include "block/block_int.h"

#define NULL_OPT_LATENCY "latency-ns"

typedef struct {
    int64_t length;
    int64_t latency_ns;
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
        {
            .name = NULL_OPT_LATENCY,
            .type = QEMU_OPT_NUMBER,
            .help = "nanoseconds (approximated) to wait "
                    "before completing request",
        },
        { /* end of list */ }
    },
};

static int null_file_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    QemuOpts *opts;
    BDRVNullState *s = bs->opaque;
    int ret = 0;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &error_abort);
    s->length =
        qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 1 << 30);
    s->latency_ns =
        qemu_opt_get_number(opts, NULL_OPT_LATENCY, 0);
    if (s->latency_ns < 0) {
        error_setg(errp, "latency-ns is invalid");
        ret = -EINVAL;
    }
    qemu_opts_del(opts);
    return ret;
}

static void null_close(BlockDriverState *bs)
{
}

static int64_t null_getlength(BlockDriverState *bs)
{
    BDRVNullState *s = bs->opaque;
    return s->length;
}

static coroutine_fn int null_co_common(BlockDriverState *bs)
{
    BDRVNullState *s = bs->opaque;

    if (s->latency_ns) {
        co_aio_sleep_ns(bdrv_get_aio_context(bs), QEMU_CLOCK_REALTIME,
                        s->latency_ns);
    }
    return 0;
}

static coroutine_fn int null_co_readv(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors,
                                      QEMUIOVector *qiov)
{
    return null_co_common(bs);
}

static coroutine_fn int null_co_writev(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors,
                                       QEMUIOVector *qiov)
{
    return null_co_common(bs);
}

static coroutine_fn int null_co_flush(BlockDriverState *bs)
{
    return null_co_common(bs);
}

typedef struct {
    BlockAIOCB common;
    QEMUBH *bh;
    QEMUTimer timer;
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

static void null_timer_cb(void *opaque)
{
    NullAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, 0);
    timer_deinit(&acb->timer);
    qemu_aio_unref(acb);
}

static inline BlockAIOCB *null_aio_common(BlockDriverState *bs,
                                          BlockCompletionFunc *cb,
                                          void *opaque)
{
    NullAIOCB *acb;
    BDRVNullState *s = bs->opaque;

    acb = qemu_aio_get(&null_aiocb_info, bs, cb, opaque);
    /* Only emulate latency after vcpu is running. */
    if (s->latency_ns) {
        aio_timer_init(bdrv_get_aio_context(bs), &acb->timer,
                       QEMU_CLOCK_REALTIME, SCALE_NS,
                       null_timer_cb, acb);
        timer_mod_ns(&acb->timer,
                     qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + s->latency_ns);
    } else {
        acb->bh = aio_bh_new(bdrv_get_aio_context(bs), null_bh_cb, acb);
        qemu_bh_schedule(acb->bh);
    }
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

static int null_reopen_prepare(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp)
{
    return 0;
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
    .bdrv_reopen_prepare    = null_reopen_prepare,
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
    .bdrv_reopen_prepare    = null_reopen_prepare,
};

static void bdrv_null_init(void)
{
    bdrv_register(&bdrv_null_co);
    bdrv_register(&bdrv_null_aio);
}

block_init(bdrv_null_init);
