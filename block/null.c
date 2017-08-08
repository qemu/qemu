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
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "block/block_int.h"

#define NULL_OPT_LATENCY "latency-ns"
#define NULL_OPT_ZEROES  "read-zeroes"

typedef struct {
    int64_t length;
    int64_t latency_ns;
    bool read_zeroes;
} BDRVNullState;

static QemuOptsList runtime_opts = {
    .name = "null",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
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
        {
            .name = NULL_OPT_ZEROES,
            .type = QEMU_OPT_BOOL,
            .help = "return zeroes when read",
        },
        { /* end of list */ }
    },
};

static void null_co_parse_filename(const char *filename, QDict *options,
                                   Error **errp)
{
    /* This functions only exists so that a null-co:// filename is accepted
     * with the null-co driver. */
    if (strcmp(filename, "null-co://")) {
        error_setg(errp, "The only allowed filename for this driver is "
                         "'null-co://'");
        return;
    }
}

static void null_aio_parse_filename(const char *filename, QDict *options,
                                    Error **errp)
{
    /* This functions only exists so that a null-aio:// filename is accepted
     * with the null-aio driver. */
    if (strcmp(filename, "null-aio://")) {
        error_setg(errp, "The only allowed filename for this driver is "
                         "'null-aio://'");
        return;
    }
}

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
    s->read_zeroes = qemu_opt_get_bool(opts, NULL_OPT_ZEROES, false);
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
    BDRVNullState *s = bs->opaque;

    if (s->read_zeroes) {
        qemu_iovec_memset(qiov, 0, 0, nb_sectors * BDRV_SECTOR_SIZE);
    }

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
    QEMUTimer timer;
} NullAIOCB;

static const AIOCBInfo null_aiocb_info = {
    .aiocb_size = sizeof(NullAIOCB),
};

static void null_bh_cb(void *opaque)
{
    NullAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, 0);
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
        aio_bh_schedule_oneshot(bdrv_get_aio_context(bs), null_bh_cb, acb);
    }
    return &acb->common;
}

static BlockAIOCB *null_aio_readv(BlockDriverState *bs,
                                  int64_t sector_num, QEMUIOVector *qiov,
                                  int nb_sectors,
                                  BlockCompletionFunc *cb,
                                  void *opaque)
{
    BDRVNullState *s = bs->opaque;

    if (s->read_zeroes) {
        qemu_iovec_memset(qiov, 0, 0, nb_sectors * BDRV_SECTOR_SIZE);
    }

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

static int64_t coroutine_fn null_co_get_block_status(BlockDriverState *bs,
                                                     int64_t sector_num,
                                                     int nb_sectors, int *pnum,
                                                     BlockDriverState **file)
{
    BDRVNullState *s = bs->opaque;
    off_t start = sector_num * BDRV_SECTOR_SIZE;

    *pnum = nb_sectors;
    *file = bs;

    if (s->read_zeroes) {
        return BDRV_BLOCK_OFFSET_VALID | start | BDRV_BLOCK_ZERO;
    } else {
        return BDRV_BLOCK_OFFSET_VALID | start;
    }
}

static void null_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    QINCREF(opts);
    qdict_del(opts, "filename");

    if (!qdict_size(opts)) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename), "%s://",
                 bs->drv->format_name);
    }

    qdict_put_str(opts, "driver", bs->drv->format_name);
    bs->full_open_options = opts;
}

static BlockDriver bdrv_null_co = {
    .format_name            = "null-co",
    .protocol_name          = "null-co",
    .instance_size          = sizeof(BDRVNullState),

    .bdrv_file_open         = null_file_open,
    .bdrv_parse_filename    = null_co_parse_filename,
    .bdrv_close             = null_close,
    .bdrv_getlength         = null_getlength,

    .bdrv_co_readv          = null_co_readv,
    .bdrv_co_writev         = null_co_writev,
    .bdrv_co_flush_to_disk  = null_co_flush,
    .bdrv_reopen_prepare    = null_reopen_prepare,

    .bdrv_co_get_block_status   = null_co_get_block_status,

    .bdrv_refresh_filename  = null_refresh_filename,
};

static BlockDriver bdrv_null_aio = {
    .format_name            = "null-aio",
    .protocol_name          = "null-aio",
    .instance_size          = sizeof(BDRVNullState),

    .bdrv_file_open         = null_file_open,
    .bdrv_parse_filename    = null_aio_parse_filename,
    .bdrv_close             = null_close,
    .bdrv_getlength         = null_getlength,

    .bdrv_aio_readv         = null_aio_readv,
    .bdrv_aio_writev        = null_aio_writev,
    .bdrv_aio_flush         = null_aio_flush,
    .bdrv_reopen_prepare    = null_reopen_prepare,

    .bdrv_co_get_block_status   = null_co_get_block_status,

    .bdrv_refresh_filename  = null_refresh_filename,
};

static void bdrv_null_init(void)
{
    bdrv_register(&bdrv_null_co);
    bdrv_register(&bdrv_null_aio);
}

block_init(bdrv_null_init);
