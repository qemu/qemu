/*
 * Block protocol for block driver correctness testing
 *
 * Copyright (C) 2010 IBM, Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdarg.h>
#include "qemu/sockets.h" /* for EINPROGRESS on Windows */
#include "block/block_int.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

typedef struct {
    BlockDriverState *test_file;
} BDRVBlkverifyState;

typedef struct BlkverifyAIOCB BlkverifyAIOCB;
struct BlkverifyAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;

    /* Request metadata */
    bool is_write;
    int64_t sector_num;
    int nb_sectors;

    int ret;                    /* first completed request's result */
    unsigned int done;          /* completion counter */
    bool *finished;             /* completion signal for cancel */

    QEMUIOVector *qiov;         /* user I/O vector */
    QEMUIOVector raw_qiov;      /* cloned I/O vector for raw file */
    void *buf;                  /* buffer for raw file I/O */

    void (*verify)(BlkverifyAIOCB *acb);
};

static void blkverify_aio_cancel(BlockDriverAIOCB *blockacb)
{
    BlkverifyAIOCB *acb = (BlkverifyAIOCB *)blockacb;
    AioContext *aio_context = bdrv_get_aio_context(blockacb->bs);
    bool finished = false;

    /* Wait until request completes, invokes its callback, and frees itself */
    acb->finished = &finished;
    while (!finished) {
        aio_poll(aio_context, true);
    }
}

static const AIOCBInfo blkverify_aiocb_info = {
    .aiocb_size         = sizeof(BlkverifyAIOCB),
    .cancel             = blkverify_aio_cancel,
};

static void GCC_FMT_ATTR(2, 3) blkverify_err(BlkverifyAIOCB *acb,
                                             const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "blkverify: %s sector_num=%" PRId64 " nb_sectors=%d ",
            acb->is_write ? "write" : "read", acb->sector_num,
            acb->nb_sectors);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Valid blkverify filenames look like blkverify:path/to/raw_image:path/to/image */
static void blkverify_parse_filename(const char *filename, QDict *options,
                                     Error **errp)
{
    const char *c;
    QString *raw_path;


    /* Parse the blkverify: prefix */
    if (!strstart(filename, "blkverify:", &filename)) {
        /* There was no prefix; therefore, all options have to be already
           present in the QDict (except for the filename) */
        qdict_put(options, "x-image", qstring_from_str(filename));
        return;
    }

    /* Parse the raw image filename */
    c = strchr(filename, ':');
    if (c == NULL) {
        error_setg(errp, "blkverify requires raw copy and original image path");
        return;
    }

    /* TODO Implement option pass-through and set raw.filename here */
    raw_path = qstring_from_substr(filename, 0, c - filename - 1);
    qdict_put(options, "x-raw", raw_path);

    /* TODO Allow multi-level nesting and set file.filename here */
    filename = c + 1;
    qdict_put(options, "x-image", qstring_from_str(filename));
}

static QemuOptsList runtime_opts = {
    .name = "blkverify",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "x-raw",
            .type = QEMU_OPT_STRING,
            .help = "[internal use only, will be removed]",
        },
        {
            .name = "x-image",
            .type = QEMU_OPT_STRING,
            .help = "[internal use only, will be removed]",
        },
        { /* end of list */ }
    },
};

static int blkverify_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    BDRVBlkverifyState *s = bs->opaque;
    QemuOpts *opts;
    Error *local_err = NULL;
    int ret;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    /* Open the raw file */
    assert(bs->file == NULL);
    ret = bdrv_open_image(&bs->file, qemu_opt_get(opts, "x-raw"), options,
                          "raw", flags | BDRV_O_PROTOCOL, false, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto fail;
    }

    /* Open the test file */
    assert(s->test_file == NULL);
    ret = bdrv_open_image(&s->test_file, qemu_opt_get(opts, "x-image"), options,
                          "test", flags, false, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        s->test_file = NULL;
        goto fail;
    }

    ret = 0;
fail:
    return ret;
}

static void blkverify_close(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    bdrv_unref(s->test_file);
    s->test_file = NULL;
}

static int64_t blkverify_getlength(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    return bdrv_getlength(s->test_file);
}

static BlkverifyAIOCB *blkverify_aio_get(BlockDriverState *bs, bool is_write,
                                         int64_t sector_num, QEMUIOVector *qiov,
                                         int nb_sectors,
                                         BlockDriverCompletionFunc *cb,
                                         void *opaque)
{
    BlkverifyAIOCB *acb = qemu_aio_get(&blkverify_aiocb_info, bs, cb, opaque);

    acb->bh = NULL;
    acb->is_write = is_write;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->ret = -EINPROGRESS;
    acb->done = 0;
    acb->qiov = qiov;
    acb->buf = NULL;
    acb->verify = NULL;
    acb->finished = NULL;
    return acb;
}

static void blkverify_aio_bh(void *opaque)
{
    BlkverifyAIOCB *acb = opaque;

    qemu_bh_delete(acb->bh);
    if (acb->buf) {
        qemu_iovec_destroy(&acb->raw_qiov);
        qemu_vfree(acb->buf);
    }
    acb->common.cb(acb->common.opaque, acb->ret);
    if (acb->finished) {
        *acb->finished = true;
    }
    qemu_aio_release(acb);
}

static void blkverify_aio_cb(void *opaque, int ret)
{
    BlkverifyAIOCB *acb = opaque;

    switch (++acb->done) {
    case 1:
        acb->ret = ret;
        break;

    case 2:
        if (acb->ret != ret) {
            blkverify_err(acb, "return value mismatch %d != %d", acb->ret, ret);
        }

        if (acb->verify) {
            acb->verify(acb);
        }

        acb->bh = aio_bh_new(bdrv_get_aio_context(acb->common.bs),
                             blkverify_aio_bh, acb);
        qemu_bh_schedule(acb->bh);
        break;
    }
}

static void blkverify_verify_readv(BlkverifyAIOCB *acb)
{
    ssize_t offset = qemu_iovec_compare(acb->qiov, &acb->raw_qiov);
    if (offset != -1) {
        blkverify_err(acb, "contents mismatch in sector %" PRId64,
                      acb->sector_num + (int64_t)(offset / BDRV_SECTOR_SIZE));
    }
}

static BlockDriverAIOCB *blkverify_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;
    BlkverifyAIOCB *acb = blkverify_aio_get(bs, false, sector_num, qiov,
                                            nb_sectors, cb, opaque);

    acb->verify = blkverify_verify_readv;
    acb->buf = qemu_blockalign(bs->file, qiov->size);
    qemu_iovec_init(&acb->raw_qiov, acb->qiov->niov);
    qemu_iovec_clone(&acb->raw_qiov, qiov, acb->buf);

    bdrv_aio_readv(s->test_file, sector_num, qiov, nb_sectors,
                   blkverify_aio_cb, acb);
    bdrv_aio_readv(bs->file, sector_num, &acb->raw_qiov, nb_sectors,
                   blkverify_aio_cb, acb);
    return &acb->common;
}

static BlockDriverAIOCB *blkverify_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;
    BlkverifyAIOCB *acb = blkverify_aio_get(bs, true, sector_num, qiov,
                                            nb_sectors, cb, opaque);

    bdrv_aio_writev(s->test_file, sector_num, qiov, nb_sectors,
                    blkverify_aio_cb, acb);
    bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors,
                    blkverify_aio_cb, acb);
    return &acb->common;
}

static BlockDriverAIOCB *blkverify_aio_flush(BlockDriverState *bs,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    BDRVBlkverifyState *s = bs->opaque;

    /* Only flush test file, the raw file is not important */
    return bdrv_aio_flush(s->test_file, cb, opaque);
}

static bool blkverify_recurse_is_first_non_filter(BlockDriverState *bs,
                                                  BlockDriverState *candidate)
{
    BDRVBlkverifyState *s = bs->opaque;

    bool perm = bdrv_recurse_is_first_non_filter(bs->file, candidate);

    if (perm) {
        return true;
    }

    return bdrv_recurse_is_first_non_filter(s->test_file, candidate);
}

/* Propagate AioContext changes to ->test_file */
static void blkverify_detach_aio_context(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    bdrv_detach_aio_context(s->test_file);
}

static void blkverify_attach_aio_context(BlockDriverState *bs,
                                         AioContext *new_context)
{
    BDRVBlkverifyState *s = bs->opaque;

    bdrv_attach_aio_context(s->test_file, new_context);
}

static void blkverify_refresh_filename(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    /* bs->file has already been refreshed */
    bdrv_refresh_filename(s->test_file);

    if (bs->file->full_open_options && s->test_file->full_open_options) {
        QDict *opts = qdict_new();
        qdict_put_obj(opts, "driver", QOBJECT(qstring_from_str("blkverify")));

        QINCREF(bs->file->full_open_options);
        qdict_put_obj(opts, "raw", QOBJECT(bs->file->full_open_options));
        QINCREF(s->test_file->full_open_options);
        qdict_put_obj(opts, "test", QOBJECT(s->test_file->full_open_options));

        bs->full_open_options = opts;
    }

    if (bs->file->exact_filename[0] && s->test_file->exact_filename[0]) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "blkverify:%s:%s",
                 bs->file->exact_filename, s->test_file->exact_filename);
    }
}

static BlockDriver bdrv_blkverify = {
    .format_name                      = "blkverify",
    .protocol_name                    = "blkverify",
    .instance_size                    = sizeof(BDRVBlkverifyState),

    .bdrv_parse_filename              = blkverify_parse_filename,
    .bdrv_file_open                   = blkverify_open,
    .bdrv_close                       = blkverify_close,
    .bdrv_getlength                   = blkverify_getlength,
    .bdrv_refresh_filename            = blkverify_refresh_filename,

    .bdrv_aio_readv                   = blkverify_aio_readv,
    .bdrv_aio_writev                  = blkverify_aio_writev,
    .bdrv_aio_flush                   = blkverify_aio_flush,

    .bdrv_attach_aio_context          = blkverify_attach_aio_context,
    .bdrv_detach_aio_context          = blkverify_detach_aio_context,

    .is_filter                        = true,
    .bdrv_recurse_is_first_non_filter = blkverify_recurse_is_first_non_filter,
};

static void bdrv_blkverify_init(void)
{
    bdrv_register(&bdrv_blkverify);
}

block_init(bdrv_blkverify_init);
