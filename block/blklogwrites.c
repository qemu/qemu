/*
 * Write logging blk driver based on blkverify and blkdebug.
 *
 * Copyright (c) 2017 Tuomas Tynkkynen <tuomas@tuxera.com>
 * Copyright (c) 2018 Aapo Vienamo <aapo@tuxera.com>
 * Copyright (c) 2018 Ari Sundholm <ari@tuxera.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/sockets.h" /* for EINPROGRESS on Windows */
#include "block/block-io.h"
#include "block/block_int.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/option.h"

/* Disk format stuff - taken from Linux drivers/md/dm-log-writes.c */

#define LOG_FLUSH_FLAG   (1 << 0)
#define LOG_FUA_FLAG     (1 << 1)
#define LOG_DISCARD_FLAG (1 << 2)
#define LOG_MARK_FLAG    (1 << 3)
#define LOG_FLAG_MASK    (LOG_FLUSH_FLAG \
                         | LOG_FUA_FLAG \
                         | LOG_DISCARD_FLAG \
                         | LOG_MARK_FLAG)

#define WRITE_LOG_VERSION 1ULL
#define WRITE_LOG_MAGIC 0x6a736677736872ULL

/* All fields are little-endian. */
struct log_write_super {
    uint64_t magic;
    uint64_t version;
    uint64_t nr_entries;
    uint32_t sectorsize;
} QEMU_PACKED;

struct log_write_entry {
    uint64_t sector;
    uint64_t nr_sectors;
    uint64_t flags;
    uint64_t data_len;
} QEMU_PACKED;

/* End of disk format structures. */

typedef struct {
    BdrvChild *log_file;
    uint32_t sectorsize;
    uint32_t sectorbits;
    uint64_t cur_log_sector;
    uint64_t nr_entries;
    uint64_t update_interval;
} BDRVBlkLogWritesState;

static QemuOptsList runtime_opts = {
    .name = "blklogwrites",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "log-append",
            .type = QEMU_OPT_BOOL,
            .help = "Append to an existing log",
        },
        {
            .name = "log-sector-size",
            .type = QEMU_OPT_SIZE,
            .help = "Log sector size",
        },
        {
            .name = "log-super-update-interval",
            .type = QEMU_OPT_NUMBER,
            .help = "Log superblock update interval (# of write requests)",
        },
        { /* end of list */ }
    },
};

static inline uint32_t blk_log_writes_log2(uint32_t value)
{
    assert(value > 0);
    return 31 - clz32(value);
}

static inline bool blk_log_writes_sector_size_valid(uint32_t sector_size)
{
    return is_power_of_2(sector_size) &&
        sector_size >= sizeof(struct log_write_super) &&
        sector_size >= sizeof(struct log_write_entry) &&
        sector_size < (1ull << 24);
}

static uint64_t blk_log_writes_find_cur_log_sector(BdrvChild *log,
                                                   uint32_t sector_size,
                                                   uint64_t nr_entries,
                                                   Error **errp)
{
    uint64_t cur_sector = 1;
    uint64_t cur_idx = 0;
    uint32_t sector_bits = blk_log_writes_log2(sector_size);
    struct log_write_entry cur_entry;

    while (cur_idx < nr_entries) {
        int read_ret = bdrv_pread(log, cur_sector << sector_bits,
                                  sizeof(cur_entry), &cur_entry, 0);
        if (read_ret < 0) {
            error_setg_errno(errp, -read_ret,
                             "Failed to read log entry %"PRIu64, cur_idx);
            return (uint64_t)-1ull;
        }

        if (cur_entry.flags & ~cpu_to_le64(LOG_FLAG_MASK)) {
            error_setg(errp, "Invalid flags 0x%"PRIx64" in log entry %"PRIu64,
                       le64_to_cpu(cur_entry.flags), cur_idx);
            return (uint64_t)-1ull;
        }

        /* Account for the sector of the entry itself */
        ++cur_sector;

        /*
         * Account for the data of the write.
         * For discards, this data is not present.
         */
        if (!(cur_entry.flags & cpu_to_le64(LOG_DISCARD_FLAG))) {
            cur_sector += le64_to_cpu(cur_entry.nr_sectors);
        }

        ++cur_idx;
    }

    return cur_sector;
}

static int blk_log_writes_open(BlockDriverState *bs, QDict *options, int flags,
                               Error **errp)
{
    BDRVBlkLogWritesState *s = bs->opaque;
    QemuOpts *opts;
    Error *local_err = NULL;
    int ret;
    uint64_t log_sector_size;
    bool log_append;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Open the file */
    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        goto fail;
    }

    /* Open the log file */
    s->log_file = bdrv_open_child(NULL, options, "log", bs, &child_of_bds,
                                  BDRV_CHILD_METADATA, false, errp);
    if (!s->log_file) {
        ret = -EINVAL;
        goto fail;
    }

    log_append = qemu_opt_get_bool(opts, "log-append", false);

    if (log_append) {
        struct log_write_super log_sb = { 0, 0, 0, 0 };

        if (qemu_opt_find(opts, "log-sector-size")) {
            ret = -EINVAL;
            error_setg(errp, "log-append and log-sector-size are mutually "
                       "exclusive");
            goto fail_log;
        }

        /* Read log superblock or fake one for an empty log */
        if (!bdrv_getlength(s->log_file->bs)) {
            log_sb.magic      = cpu_to_le64(WRITE_LOG_MAGIC);
            log_sb.version    = cpu_to_le64(WRITE_LOG_VERSION);
            log_sb.nr_entries = cpu_to_le64(0);
            log_sb.sectorsize = cpu_to_le32(BDRV_SECTOR_SIZE);
        } else {
            ret = bdrv_pread(s->log_file, 0, sizeof(log_sb), &log_sb, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Could not read log superblock");
                goto fail_log;
            }
        }

        if (log_sb.magic != cpu_to_le64(WRITE_LOG_MAGIC)) {
            ret = -EINVAL;
            error_setg(errp, "Invalid log superblock magic");
            goto fail_log;
        }

        if (log_sb.version != cpu_to_le64(WRITE_LOG_VERSION)) {
            ret = -EINVAL;
            error_setg(errp, "Unsupported log version %"PRIu64,
                       le64_to_cpu(log_sb.version));
            goto fail_log;
        }

        log_sector_size = le32_to_cpu(log_sb.sectorsize);
        s->cur_log_sector = 1;
        s->nr_entries = 0;

        if (blk_log_writes_sector_size_valid(log_sector_size)) {
            s->cur_log_sector =
                blk_log_writes_find_cur_log_sector(s->log_file, log_sector_size,
                                    le64_to_cpu(log_sb.nr_entries), &local_err);
            if (local_err) {
                ret = -EINVAL;
                error_propagate(errp, local_err);
                goto fail_log;
            }

            s->nr_entries = le64_to_cpu(log_sb.nr_entries);
        }
    } else {
        log_sector_size = qemu_opt_get_size(opts, "log-sector-size",
                                            BDRV_SECTOR_SIZE);
        s->cur_log_sector = 1;
        s->nr_entries = 0;
    }

    if (!blk_log_writes_sector_size_valid(log_sector_size)) {
        ret = -EINVAL;
        error_setg(errp, "Invalid log sector size %"PRIu64, log_sector_size);
        goto fail_log;
    }

    s->sectorsize = log_sector_size;
    s->sectorbits = blk_log_writes_log2(log_sector_size);
    s->update_interval = qemu_opt_get_number(opts, "log-super-update-interval",
                                             4096);
    if (!s->update_interval) {
        ret = -EINVAL;
        error_setg(errp, "Invalid log superblock update interval %"PRIu64,
                   s->update_interval);
        goto fail_log;
    }

    ret = 0;
fail_log:
    if (ret < 0) {
        bdrv_graph_wrlock(NULL);
        bdrv_unref_child(bs, s->log_file);
        bdrv_graph_wrunlock(NULL);
        s->log_file = NULL;
    }
fail:
    qemu_opts_del(opts);
    return ret;
}

static void blk_log_writes_close(BlockDriverState *bs)
{
    BDRVBlkLogWritesState *s = bs->opaque;

    bdrv_graph_wrlock(NULL);
    bdrv_unref_child(bs, s->log_file);
    s->log_file = NULL;
    bdrv_graph_wrunlock(NULL);
}

static int64_t coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_getlength(BlockDriverState *bs)
{
    return bdrv_co_getlength(bs->file->bs);
}

static void blk_log_writes_child_perm(BlockDriverState *bs, BdrvChild *c,
                                      BdrvChildRole role,
                                      BlockReopenQueue *ro_q,
                                      uint64_t perm, uint64_t shrd,
                                      uint64_t *nperm, uint64_t *nshrd)
{
    if (!c) {
        *nperm = perm & DEFAULT_PERM_PASSTHROUGH;
        *nshrd = (shrd & DEFAULT_PERM_PASSTHROUGH) | DEFAULT_PERM_UNCHANGED;
        return;
    }

    bdrv_default_perms(bs, c, role, ro_q, perm, shrd,
                       nperm, nshrd);
}

static void blk_log_writes_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVBlkLogWritesState *s = bs->opaque;
    bs->bl.request_alignment = s->sectorsize;
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                         QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

typedef struct BlkLogWritesFileReq {
    BlockDriverState *bs;
    uint64_t offset;
    uint64_t bytes;
    int file_flags;
    QEMUIOVector *qiov;
    int GRAPH_RDLOCK_PTR (*func)(struct BlkLogWritesFileReq *r);
    int file_ret;
} BlkLogWritesFileReq;

typedef struct {
    BlockDriverState *bs;
    QEMUIOVector *qiov;
    struct log_write_entry entry;
    uint64_t zero_size;
    int log_ret;
} BlkLogWritesLogReq;

static void coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_log(BlkLogWritesLogReq *lr)
{
    BDRVBlkLogWritesState *s = lr->bs->opaque;
    uint64_t cur_log_offset = s->cur_log_sector << s->sectorbits;

    s->nr_entries++;
    s->cur_log_sector +=
            ROUND_UP(lr->qiov->size, s->sectorsize) >> s->sectorbits;

    lr->log_ret = bdrv_co_pwritev(s->log_file, cur_log_offset, lr->qiov->size,
                                  lr->qiov, 0);

    /* Logging for the "write zeroes" operation */
    if (lr->log_ret == 0 && lr->zero_size) {
        cur_log_offset = s->cur_log_sector << s->sectorbits;
        s->cur_log_sector +=
                ROUND_UP(lr->zero_size, s->sectorsize) >> s->sectorbits;

        lr->log_ret = bdrv_co_pwrite_zeroes(s->log_file, cur_log_offset,
                                            lr->zero_size, 0);
    }

    /* Update super block on flush or every update interval */
    if (lr->log_ret == 0 && ((lr->entry.flags & LOG_FLUSH_FLAG)
        || (s->nr_entries % s->update_interval == 0)))
    {
        struct log_write_super super = {
            .magic      = cpu_to_le64(WRITE_LOG_MAGIC),
            .version    = cpu_to_le64(WRITE_LOG_VERSION),
            .nr_entries = cpu_to_le64(s->nr_entries),
            .sectorsize = cpu_to_le32(s->sectorsize),
        };
        void *zeroes = g_malloc0(s->sectorsize - sizeof(super));
        QEMUIOVector qiov;

        qemu_iovec_init(&qiov, 2);
        qemu_iovec_add(&qiov, &super, sizeof(super));
        qemu_iovec_add(&qiov, zeroes, s->sectorsize - sizeof(super));

        lr->log_ret =
            bdrv_co_pwritev(s->log_file, 0, s->sectorsize, &qiov, 0);
        if (lr->log_ret == 0) {
            lr->log_ret = bdrv_co_flush(s->log_file->bs);
        }
        qemu_iovec_destroy(&qiov);
        g_free(zeroes);
    }
}

static void coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_file(BlkLogWritesFileReq *fr)
{
    fr->file_ret = fr->func(fr);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_log(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                      QEMUIOVector *qiov, int flags,
                      int /*GRAPH_RDLOCK*/ (*file_func)(BlkLogWritesFileReq *r),
                      uint64_t entry_flags, bool is_zero_write)
{
    QEMUIOVector log_qiov;
    size_t niov = qiov ? qiov->niov : 0;
    BDRVBlkLogWritesState *s = bs->opaque;
    BlkLogWritesFileReq fr = {
        .bs         = bs,
        .offset     = offset,
        .bytes      = bytes,
        .file_flags = flags,
        .qiov       = qiov,
        .func       = file_func,
    };
    BlkLogWritesLogReq lr = {
        .bs             = bs,
        .qiov           = &log_qiov,
        .entry = {
            .sector     = cpu_to_le64(offset >> s->sectorbits),
            .nr_sectors = cpu_to_le64(bytes >> s->sectorbits),
            .flags      = cpu_to_le64(entry_flags),
            .data_len   = 0,
        },
        .zero_size = is_zero_write ? bytes : 0,
    };
    void *zeroes = g_malloc0(s->sectorsize - sizeof(lr.entry));

    assert((1 << s->sectorbits) == s->sectorsize);
    assert(bs->bl.request_alignment == s->sectorsize);
    assert(QEMU_IS_ALIGNED(offset, bs->bl.request_alignment));
    assert(QEMU_IS_ALIGNED(bytes, bs->bl.request_alignment));

    qemu_iovec_init(&log_qiov, niov + 2);
    qemu_iovec_add(&log_qiov, &lr.entry, sizeof(lr.entry));
    qemu_iovec_add(&log_qiov, zeroes, s->sectorsize - sizeof(lr.entry));
    if (qiov) {
        qemu_iovec_concat(&log_qiov, qiov, 0, qiov->size);
    }

    blk_log_writes_co_do_file(&fr);
    blk_log_writes_co_do_log(&lr);

    qemu_iovec_destroy(&log_qiov);
    g_free(zeroes);

    if (lr.log_ret < 0) {
        return lr.log_ret;
    }

    return fr.file_ret;
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_file_pwritev(BlkLogWritesFileReq *fr)
{
    return bdrv_co_pwritev(fr->bs->file, fr->offset, fr->bytes,
                           fr->qiov, fr->file_flags);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_file_pwrite_zeroes(BlkLogWritesFileReq *fr)
{
    return bdrv_co_pwrite_zeroes(fr->bs->file, fr->offset, fr->bytes,
                                 fr->file_flags);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_file_flush(BlkLogWritesFileReq *fr)
{
    return bdrv_co_flush(fr->bs->file->bs);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_do_file_pdiscard(BlkLogWritesFileReq *fr)
{
    return bdrv_co_pdiscard(fr->bs->file, fr->offset, fr->bytes);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                          QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return blk_log_writes_co_log(bs, offset, bytes, qiov, flags,
                                 blk_log_writes_co_do_file_pwritev, 0, false);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                int64_t bytes, BdrvRequestFlags flags)
{
    return blk_log_writes_co_log(bs, offset, bytes, NULL, flags,
                                 blk_log_writes_co_do_file_pwrite_zeroes, 0,
                                 true);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_flush_to_disk(BlockDriverState *bs)
{
    return blk_log_writes_co_log(bs, 0, 0, NULL, 0,
                                 blk_log_writes_co_do_file_flush,
                                 LOG_FLUSH_FLAG, false);
}

static int coroutine_fn GRAPH_RDLOCK
blk_log_writes_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    return blk_log_writes_co_log(bs, offset, bytes, NULL, 0,
                                 blk_log_writes_co_do_file_pdiscard,
                                 LOG_DISCARD_FLAG, false);
}

static const char *const blk_log_writes_strong_runtime_opts[] = {
    "log-append",
    "log-sector-size",

    NULL
};

static BlockDriver bdrv_blk_log_writes = {
    .format_name            = "blklogwrites",
    .instance_size          = sizeof(BDRVBlkLogWritesState),

    .bdrv_open              = blk_log_writes_open,
    .bdrv_close             = blk_log_writes_close,
    .bdrv_co_getlength      = blk_log_writes_co_getlength,
    .bdrv_child_perm        = blk_log_writes_child_perm,
    .bdrv_refresh_limits    = blk_log_writes_refresh_limits,

    .bdrv_co_preadv         = blk_log_writes_co_preadv,
    .bdrv_co_pwritev        = blk_log_writes_co_pwritev,
    .bdrv_co_pwrite_zeroes  = blk_log_writes_co_pwrite_zeroes,
    .bdrv_co_flush_to_disk  = blk_log_writes_co_flush_to_disk,
    .bdrv_co_pdiscard       = blk_log_writes_co_pdiscard,

    .is_filter              = true,
    .strong_runtime_opts    = blk_log_writes_strong_runtime_opts,
};

static void bdrv_blk_log_writes_init(void)
{
    bdrv_register(&bdrv_blk_log_writes);
}

block_init(bdrv_blk_log_writes_init);
