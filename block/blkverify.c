/*
 * Block protocol for block driver correctness testing
 *
 * Copyright (C) 2010 IBM, Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/sockets.h" /* for EINPROGRESS on Windows */
#include "block/block_int.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/memalign.h"

typedef struct {
    BdrvChild *test_file;
} BDRVBlkverifyState;

typedef struct BlkverifyRequest {
    Coroutine *co;
    BlockDriverState *bs;

    /* Request metadata */
    bool is_write;
    uint64_t offset;
    uint64_t bytes;
    int flags;

    int (*request_fn)(BdrvChild *, int64_t, int64_t, QEMUIOVector *,
                      BdrvRequestFlags);

    int ret;                    /* test image result */
    int raw_ret;                /* raw image result */

    unsigned int done;          /* completion counter */

    QEMUIOVector *qiov;         /* user I/O vector */
    QEMUIOVector *raw_qiov;     /* cloned I/O vector for raw file */
} BlkverifyRequest;

static void G_GNUC_PRINTF(2, 3) blkverify_err(BlkverifyRequest *r,
                                             const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "blkverify: %s offset=%" PRId64 " bytes=%" PRId64 " ",
            r->is_write ? "write" : "read", r->offset, r->bytes);
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
        qdict_put_str(options, "x-image", filename);
        return;
    }

    /* Parse the raw image filename */
    c = strchr(filename, ':');
    if (c == NULL) {
        error_setg(errp, "blkverify requires raw copy and original image path");
        return;
    }

    /* TODO Implement option pass-through and set raw.filename here */
    raw_path = qstring_from_substr(filename, 0, c - filename);
    qdict_put(options, "x-raw", raw_path);

    /* TODO Allow multi-level nesting and set file.filename here */
    filename = c + 1;
    qdict_put_str(options, "x-image", filename);
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
    int ret;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Open the raw file */
    ret = bdrv_open_file_child(qemu_opt_get(opts, "x-raw"), options, "raw",
                               bs, errp);
    if (ret < 0) {
        goto fail;
    }

    /* Open the test file */
    s->test_file = bdrv_open_child(qemu_opt_get(opts, "x-image"), options,
                                   "test", bs, &child_of_bds, BDRV_CHILD_DATA,
                                   false, errp);
    if (!s->test_file) {
        ret = -EINVAL;
        goto fail;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED;
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED;

    ret = 0;
fail:
    qemu_opts_del(opts);
    return ret;
}

static void blkverify_close(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    bdrv_unref_child(bs, s->test_file);
    s->test_file = NULL;
}

static int64_t blkverify_getlength(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    return bdrv_getlength(s->test_file->bs);
}

static void coroutine_fn blkverify_do_test_req(void *opaque)
{
    BlkverifyRequest *r = opaque;
    BDRVBlkverifyState *s = r->bs->opaque;

    r->ret = r->request_fn(s->test_file, r->offset, r->bytes, r->qiov,
                           r->flags);
    r->done++;
    qemu_coroutine_enter_if_inactive(r->co);
}

static void coroutine_fn blkverify_do_raw_req(void *opaque)
{
    BlkverifyRequest *r = opaque;

    r->raw_ret = r->request_fn(r->bs->file, r->offset, r->bytes, r->raw_qiov,
                               r->flags);
    r->done++;
    qemu_coroutine_enter_if_inactive(r->co);
}

static int coroutine_fn
blkverify_co_prwv(BlockDriverState *bs, BlkverifyRequest *r, uint64_t offset,
                  uint64_t bytes, QEMUIOVector *qiov, QEMUIOVector *raw_qiov,
                  int flags, bool is_write)
{
    Coroutine *co_a, *co_b;

    *r = (BlkverifyRequest) {
        .co         = qemu_coroutine_self(),
        .bs         = bs,
        .offset     = offset,
        .bytes      = bytes,
        .qiov       = qiov,
        .raw_qiov   = raw_qiov,
        .flags      = flags,
        .is_write   = is_write,
        .request_fn = is_write ? bdrv_co_pwritev : bdrv_co_preadv,
    };

    co_a = qemu_coroutine_create(blkverify_do_test_req, r);
    co_b = qemu_coroutine_create(blkverify_do_raw_req, r);

    qemu_coroutine_enter(co_a);
    qemu_coroutine_enter(co_b);

    while (r->done < 2) {
        qemu_coroutine_yield();
    }

    if (r->ret != r->raw_ret) {
        blkverify_err(r, "return value mismatch %d != %d", r->ret, r->raw_ret);
    }

    return r->ret;
}

static int coroutine_fn
blkverify_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                    QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BlkverifyRequest r;
    QEMUIOVector raw_qiov;
    void *buf;
    ssize_t cmp_offset;
    int ret;

    buf = qemu_blockalign(bs->file->bs, qiov->size);
    qemu_iovec_init(&raw_qiov, qiov->niov);
    qemu_iovec_clone(&raw_qiov, qiov, buf);

    ret = blkverify_co_prwv(bs, &r, offset, bytes, qiov, &raw_qiov,
                            flags & ~BDRV_REQ_REGISTERED_BUF, false);

    cmp_offset = qemu_iovec_compare(qiov, &raw_qiov);
    if (cmp_offset != -1) {
        blkverify_err(&r, "contents mismatch at offset %" PRId64,
                      offset + cmp_offset);
    }

    qemu_iovec_destroy(&raw_qiov);
    qemu_vfree(buf);

    return ret;
}

static int coroutine_fn
blkverify_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                     QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BlkverifyRequest r;
    return blkverify_co_prwv(bs, &r, offset, bytes, qiov, qiov, flags, true);
}

static int coroutine_fn blkverify_co_flush(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    /* Only flush test file, the raw file is not important */
    return bdrv_co_flush(s->test_file->bs);
}

static bool blkverify_recurse_can_replace(BlockDriverState *bs,
                                          BlockDriverState *to_replace)
{
    BDRVBlkverifyState *s = bs->opaque;

    /*
     * blkverify quits the whole qemu process if there is a mismatch
     * between bs->file->bs and s->test_file->bs.  Therefore, we know
     * know that both must match bs and we can recurse down to either.
     */
    return bdrv_recurse_can_replace(bs->file->bs, to_replace) ||
           bdrv_recurse_can_replace(s->test_file->bs, to_replace);
}

static void blkverify_refresh_filename(BlockDriverState *bs)
{
    BDRVBlkverifyState *s = bs->opaque;

    if (bs->file->bs->exact_filename[0]
        && s->test_file->bs->exact_filename[0])
    {
        int ret = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                           "blkverify:%s:%s",
                           bs->file->bs->exact_filename,
                           s->test_file->bs->exact_filename);
        if (ret >= sizeof(bs->exact_filename)) {
            /* An overflow makes the filename unusable, so do not report any */
            bs->exact_filename[0] = 0;
        }
    }
}

static char *blkverify_dirname(BlockDriverState *bs, Error **errp)
{
    /* In general, there are two BDSs with different dirnames below this one;
     * so there is no unique dirname we could return (unless both are equal by
     * chance). Therefore, to be consistent, just always return NULL. */
    error_setg(errp, "Cannot generate a base directory for blkverify nodes");
    return NULL;
}

static BlockDriver bdrv_blkverify = {
    .format_name                      = "blkverify",
    .protocol_name                    = "blkverify",
    .instance_size                    = sizeof(BDRVBlkverifyState),

    .bdrv_parse_filename              = blkverify_parse_filename,
    .bdrv_file_open                   = blkverify_open,
    .bdrv_close                       = blkverify_close,
    .bdrv_child_perm                  = bdrv_default_perms,
    .bdrv_getlength                   = blkverify_getlength,
    .bdrv_refresh_filename            = blkverify_refresh_filename,
    .bdrv_dirname                     = blkverify_dirname,

    .bdrv_co_preadv                   = blkverify_co_preadv,
    .bdrv_co_pwritev                  = blkverify_co_pwritev,
    .bdrv_co_flush                    = blkverify_co_flush,

    .is_filter                        = true,
    .bdrv_recurse_can_replace         = blkverify_recurse_can_replace,
};

static void bdrv_blkverify_init(void)
{
    bdrv_register(&bdrv_blkverify);
}

block_init(bdrv_blkverify_init);
