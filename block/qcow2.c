/*
 * Block driver for the QCOW version 2 format
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
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
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "qemu/module.h"
#include <zlib.h>
#include "block/qcow2.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qbool.h"
#include "qapi/util.h"
#include "qapi/qmp/types.h"
#include "qapi-event.h"
#include "trace.h"
#include "qemu/option_int.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"

/*
  Differences with QCOW:

  - Support for multiple incremental snapshots.
  - Memory management by reference counts.
  - Clusters which have a reference count of one have the bit
    QCOW_OFLAG_COPIED to optimize write performance.
  - Size of compressed clusters is stored in sectors to reduce bit usage
    in the cluster offsets.
  - Support for storing additional data (such as the VM state) in the
    snapshots.
  - If a backing store is used, the cluster size is not constrained
    (could be backported to QCOW).
  - L2 tables have always a size of one cluster.
*/


typedef struct {
    uint32_t magic;
    uint32_t len;
} QEMU_PACKED QCowExtension;

#define  QCOW2_EXT_MAGIC_END 0
#define  QCOW2_EXT_MAGIC_BACKING_FORMAT 0xE2792ACA
#define  QCOW2_EXT_MAGIC_FEATURE_TABLE 0x6803f857

static int qcow2_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const QCowHeader *cow_header = (const void *)buf;

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) >= 2)
        return 100;
    else
        return 0;
}


/* 
 * read qcow2 extension and fill bs
 * start reading from start_offset
 * finish reading upon magic of value 0 or when end_offset reached
 * unknown magic is skipped (future extension this version knows nothing about)
 * return 0 upon success, non-0 otherwise
 */
static int qcow2_read_extensions(BlockDriverState *bs, uint64_t start_offset,
                                 uint64_t end_offset, void **p_feature_table,
                                 Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCowExtension ext;
    uint64_t offset;
    int ret;

#ifdef DEBUG_EXT
    printf("qcow2_read_extensions: start=%ld end=%ld\n", start_offset, end_offset);
#endif
    offset = start_offset;
    while (offset < end_offset) {

#ifdef DEBUG_EXT
        /* Sanity check */
        if (offset > s->cluster_size)
            printf("qcow2_read_extension: suspicious offset %lu\n", offset);

        printf("attempting to read extended header in offset %lu\n", offset);
#endif

        ret = bdrv_pread(bs->file->bs, offset, &ext, sizeof(ext));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "qcow2_read_extension: ERROR: "
                             "pread fail from offset %" PRIu64, offset);
            return 1;
        }
        be32_to_cpus(&ext.magic);
        be32_to_cpus(&ext.len);
        offset += sizeof(ext);
#ifdef DEBUG_EXT
        printf("ext.magic = 0x%x\n", ext.magic);
#endif
        if (offset > end_offset || ext.len > end_offset - offset) {
            error_setg(errp, "Header extension too large");
            return -EINVAL;
        }

        switch (ext.magic) {
        case QCOW2_EXT_MAGIC_END:
            return 0;

        case QCOW2_EXT_MAGIC_BACKING_FORMAT:
            if (ext.len >= sizeof(bs->backing_format)) {
                error_setg(errp, "ERROR: ext_backing_format: len=%" PRIu32
                           " too large (>=%zu)", ext.len,
                           sizeof(bs->backing_format));
                return 2;
            }
            ret = bdrv_pread(bs->file->bs, offset, bs->backing_format, ext.len);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "ERROR: ext_backing_format: "
                                 "Could not read format name");
                return 3;
            }
            bs->backing_format[ext.len] = '\0';
            s->image_backing_format = g_strdup(bs->backing_format);
#ifdef DEBUG_EXT
            printf("Qcow2: Got format extension %s\n", bs->backing_format);
#endif
            break;

        case QCOW2_EXT_MAGIC_FEATURE_TABLE:
            if (p_feature_table != NULL) {
                void* feature_table = g_malloc0(ext.len + 2 * sizeof(Qcow2Feature));
                ret = bdrv_pread(bs->file->bs, offset , feature_table, ext.len);
                if (ret < 0) {
                    error_setg_errno(errp, -ret, "ERROR: ext_feature_table: "
                                     "Could not read table");
                    return ret;
                }

                *p_feature_table = feature_table;
            }
            break;

        default:
            /* unknown magic - save it in case we need to rewrite the header */
            {
                Qcow2UnknownHeaderExtension *uext;

                uext = g_malloc0(sizeof(*uext)  + ext.len);
                uext->magic = ext.magic;
                uext->len = ext.len;
                QLIST_INSERT_HEAD(&s->unknown_header_ext, uext, next);

                ret = bdrv_pread(bs->file->bs, offset , uext->data, uext->len);
                if (ret < 0) {
                    error_setg_errno(errp, -ret, "ERROR: unknown extension: "
                                     "Could not read data");
                    return ret;
                }
            }
            break;
        }

        offset += ((ext.len + 7) & ~7);
    }

    return 0;
}

static void cleanup_unknown_header_ext(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2UnknownHeaderExtension *uext, *next;

    QLIST_FOREACH_SAFE(uext, &s->unknown_header_ext, next, next) {
        QLIST_REMOVE(uext, next);
        g_free(uext);
    }
}

static void report_unsupported_feature(Error **errp, Qcow2Feature *table,
                                       uint64_t mask)
{
    char *features = g_strdup("");
    char *old;

    while (table && table->name[0] != '\0') {
        if (table->type == QCOW2_FEAT_TYPE_INCOMPATIBLE) {
            if (mask & (1ULL << table->bit)) {
                old = features;
                features = g_strdup_printf("%s%s%.46s", old, *old ? ", " : "",
                                           table->name);
                g_free(old);
                mask &= ~(1ULL << table->bit);
            }
        }
        table++;
    }

    if (mask) {
        old = features;
        features = g_strdup_printf("%s%sUnknown incompatible feature: %" PRIx64,
                                   old, *old ? ", " : "", mask);
        g_free(old);
    }

    error_setg(errp, "Unsupported qcow2 feature(s): %s", features);
    g_free(features);
}

/*
 * Sets the dirty bit and flushes afterwards if necessary.
 *
 * The incompatible_features bit is only set if the image file header was
 * updated successfully.  Therefore it is not required to check the return
 * value of this function.
 */
int qcow2_mark_dirty(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t val;
    int ret;

    assert(s->qcow_version >= 3);

    if (s->incompatible_features & QCOW2_INCOMPAT_DIRTY) {
        return 0; /* already dirty */
    }

    val = cpu_to_be64(s->incompatible_features | QCOW2_INCOMPAT_DIRTY);
    ret = bdrv_pwrite(bs->file->bs, offsetof(QCowHeader, incompatible_features),
                      &val, sizeof(val));
    if (ret < 0) {
        return ret;
    }
    ret = bdrv_flush(bs->file->bs);
    if (ret < 0) {
        return ret;
    }

    /* Only treat image as dirty if the header was updated successfully */
    s->incompatible_features |= QCOW2_INCOMPAT_DIRTY;
    return 0;
}

/*
 * Clears the dirty bit and flushes before if necessary.  Only call this
 * function when there are no pending requests, it does not guard against
 * concurrent requests dirtying the image.
 */
static int qcow2_mark_clean(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;

    if (s->incompatible_features & QCOW2_INCOMPAT_DIRTY) {
        int ret;

        s->incompatible_features &= ~QCOW2_INCOMPAT_DIRTY;

        ret = bdrv_flush(bs);
        if (ret < 0) {
            return ret;
        }

        return qcow2_update_header(bs);
    }
    return 0;
}

/*
 * Marks the image as corrupt.
 */
int qcow2_mark_corrupt(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;

    s->incompatible_features |= QCOW2_INCOMPAT_CORRUPT;
    return qcow2_update_header(bs);
}

/*
 * Marks the image as consistent, i.e., unsets the corrupt bit, and flushes
 * before if necessary.
 */
int qcow2_mark_consistent(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;

    if (s->incompatible_features & QCOW2_INCOMPAT_CORRUPT) {
        int ret = bdrv_flush(bs);
        if (ret < 0) {
            return ret;
        }

        s->incompatible_features &= ~QCOW2_INCOMPAT_CORRUPT;
        return qcow2_update_header(bs);
    }
    return 0;
}

static int qcow2_check(BlockDriverState *bs, BdrvCheckResult *result,
                       BdrvCheckMode fix)
{
    int ret = qcow2_check_refcounts(bs, result, fix);
    if (ret < 0) {
        return ret;
    }

    if (fix && result->check_errors == 0 && result->corruptions == 0) {
        ret = qcow2_mark_clean(bs);
        if (ret < 0) {
            return ret;
        }
        return qcow2_mark_consistent(bs);
    }
    return ret;
}

static int validate_table_offset(BlockDriverState *bs, uint64_t offset,
                                 uint64_t entries, size_t entry_len)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t size;

    /* Use signed INT64_MAX as the maximum even for uint64_t header fields,
     * because values will be passed to qemu functions taking int64_t. */
    if (entries > INT64_MAX / entry_len) {
        return -EINVAL;
    }

    size = entries * entry_len;

    if (INT64_MAX - size < offset) {
        return -EINVAL;
    }

    /* Tables must be cluster aligned */
    if (offset & (s->cluster_size - 1)) {
        return -EINVAL;
    }

    return 0;
}

static QemuOptsList qcow2_runtime_opts = {
    .name = "qcow2",
    .head = QTAILQ_HEAD_INITIALIZER(qcow2_runtime_opts.head),
    .desc = {
        {
            .name = QCOW2_OPT_LAZY_REFCOUNTS,
            .type = QEMU_OPT_BOOL,
            .help = "Postpone refcount updates",
        },
        {
            .name = QCOW2_OPT_DISCARD_REQUEST,
            .type = QEMU_OPT_BOOL,
            .help = "Pass guest discard requests to the layer below",
        },
        {
            .name = QCOW2_OPT_DISCARD_SNAPSHOT,
            .type = QEMU_OPT_BOOL,
            .help = "Generate discard requests when snapshot related space "
                    "is freed",
        },
        {
            .name = QCOW2_OPT_DISCARD_OTHER,
            .type = QEMU_OPT_BOOL,
            .help = "Generate discard requests when other clusters are freed",
        },
        {
            .name = QCOW2_OPT_OVERLAP,
            .type = QEMU_OPT_STRING,
            .help = "Selects which overlap checks to perform from a range of "
                    "templates (none, constant, cached, all)",
        },
        {
            .name = QCOW2_OPT_OVERLAP_TEMPLATE,
            .type = QEMU_OPT_STRING,
            .help = "Selects which overlap checks to perform from a range of "
                    "templates (none, constant, cached, all)",
        },
        {
            .name = QCOW2_OPT_OVERLAP_MAIN_HEADER,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into the main qcow2 header",
        },
        {
            .name = QCOW2_OPT_OVERLAP_ACTIVE_L1,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into the active L1 table",
        },
        {
            .name = QCOW2_OPT_OVERLAP_ACTIVE_L2,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into an active L2 table",
        },
        {
            .name = QCOW2_OPT_OVERLAP_REFCOUNT_TABLE,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into the refcount table",
        },
        {
            .name = QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into a refcount block",
        },
        {
            .name = QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into the snapshot table",
        },
        {
            .name = QCOW2_OPT_OVERLAP_INACTIVE_L1,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into an inactive L1 table",
        },
        {
            .name = QCOW2_OPT_OVERLAP_INACTIVE_L2,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into an inactive L2 table",
        },
        {
            .name = QCOW2_OPT_CACHE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Maximum combined metadata (L2 tables and refcount blocks) "
                    "cache size",
        },
        {
            .name = QCOW2_OPT_L2_CACHE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Maximum L2 table cache size",
        },
        {
            .name = QCOW2_OPT_REFCOUNT_CACHE_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Maximum refcount block cache size",
        },
        {
            .name = QCOW2_OPT_CACHE_CLEAN_INTERVAL,
            .type = QEMU_OPT_NUMBER,
            .help = "Clean unused cache entries after this time (in seconds)",
        },
        { /* end of list */ }
    },
};

static const char *overlap_bool_option_names[QCOW2_OL_MAX_BITNR] = {
    [QCOW2_OL_MAIN_HEADER_BITNR]    = QCOW2_OPT_OVERLAP_MAIN_HEADER,
    [QCOW2_OL_ACTIVE_L1_BITNR]      = QCOW2_OPT_OVERLAP_ACTIVE_L1,
    [QCOW2_OL_ACTIVE_L2_BITNR]      = QCOW2_OPT_OVERLAP_ACTIVE_L2,
    [QCOW2_OL_REFCOUNT_TABLE_BITNR] = QCOW2_OPT_OVERLAP_REFCOUNT_TABLE,
    [QCOW2_OL_REFCOUNT_BLOCK_BITNR] = QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK,
    [QCOW2_OL_SNAPSHOT_TABLE_BITNR] = QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE,
    [QCOW2_OL_INACTIVE_L1_BITNR]    = QCOW2_OPT_OVERLAP_INACTIVE_L1,
    [QCOW2_OL_INACTIVE_L2_BITNR]    = QCOW2_OPT_OVERLAP_INACTIVE_L2,
};

static void cache_clean_timer_cb(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVQcow2State *s = bs->opaque;
    qcow2_cache_clean_unused(bs, s->l2_table_cache);
    qcow2_cache_clean_unused(bs, s->refcount_block_cache);
    timer_mod(s->cache_clean_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              (int64_t) s->cache_clean_interval * 1000);
}

static void cache_clean_timer_init(BlockDriverState *bs, AioContext *context)
{
    BDRVQcow2State *s = bs->opaque;
    if (s->cache_clean_interval > 0) {
        s->cache_clean_timer = aio_timer_new(context, QEMU_CLOCK_VIRTUAL,
                                             SCALE_MS, cache_clean_timer_cb,
                                             bs);
        timer_mod(s->cache_clean_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  (int64_t) s->cache_clean_interval * 1000);
    }
}

static void cache_clean_timer_del(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    if (s->cache_clean_timer) {
        timer_del(s->cache_clean_timer);
        timer_free(s->cache_clean_timer);
        s->cache_clean_timer = NULL;
    }
}

static void qcow2_detach_aio_context(BlockDriverState *bs)
{
    cache_clean_timer_del(bs);
}

static void qcow2_attach_aio_context(BlockDriverState *bs,
                                     AioContext *new_context)
{
    cache_clean_timer_init(bs, new_context);
}

static void read_cache_sizes(BlockDriverState *bs, QemuOpts *opts,
                             uint64_t *l2_cache_size,
                             uint64_t *refcount_cache_size, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t combined_cache_size;
    bool l2_cache_size_set, refcount_cache_size_set, combined_cache_size_set;

    combined_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_CACHE_SIZE);
    l2_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_L2_CACHE_SIZE);
    refcount_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_REFCOUNT_CACHE_SIZE);

    combined_cache_size = qemu_opt_get_size(opts, QCOW2_OPT_CACHE_SIZE, 0);
    *l2_cache_size = qemu_opt_get_size(opts, QCOW2_OPT_L2_CACHE_SIZE, 0);
    *refcount_cache_size = qemu_opt_get_size(opts,
                                             QCOW2_OPT_REFCOUNT_CACHE_SIZE, 0);

    if (combined_cache_size_set) {
        if (l2_cache_size_set && refcount_cache_size_set) {
            error_setg(errp, QCOW2_OPT_CACHE_SIZE ", " QCOW2_OPT_L2_CACHE_SIZE
                       " and " QCOW2_OPT_REFCOUNT_CACHE_SIZE " may not be set "
                       "the same time");
            return;
        } else if (*l2_cache_size > combined_cache_size) {
            error_setg(errp, QCOW2_OPT_L2_CACHE_SIZE " may not exceed "
                       QCOW2_OPT_CACHE_SIZE);
            return;
        } else if (*refcount_cache_size > combined_cache_size) {
            error_setg(errp, QCOW2_OPT_REFCOUNT_CACHE_SIZE " may not exceed "
                       QCOW2_OPT_CACHE_SIZE);
            return;
        }

        if (l2_cache_size_set) {
            *refcount_cache_size = combined_cache_size - *l2_cache_size;
        } else if (refcount_cache_size_set) {
            *l2_cache_size = combined_cache_size - *refcount_cache_size;
        } else {
            *refcount_cache_size = combined_cache_size
                                 / (DEFAULT_L2_REFCOUNT_SIZE_RATIO + 1);
            *l2_cache_size = combined_cache_size - *refcount_cache_size;
        }
    } else {
        if (!l2_cache_size_set && !refcount_cache_size_set) {
            *l2_cache_size = MAX(DEFAULT_L2_CACHE_BYTE_SIZE,
                                 (uint64_t)DEFAULT_L2_CACHE_CLUSTERS
                                 * s->cluster_size);
            *refcount_cache_size = *l2_cache_size
                                 / DEFAULT_L2_REFCOUNT_SIZE_RATIO;
        } else if (!l2_cache_size_set) {
            *l2_cache_size = *refcount_cache_size
                           * DEFAULT_L2_REFCOUNT_SIZE_RATIO;
        } else if (!refcount_cache_size_set) {
            *refcount_cache_size = *l2_cache_size
                                 / DEFAULT_L2_REFCOUNT_SIZE_RATIO;
        }
    }
}

typedef struct Qcow2ReopenState {
    Qcow2Cache *l2_table_cache;
    Qcow2Cache *refcount_block_cache;
    bool use_lazy_refcounts;
    int overlap_check;
    bool discard_passthrough[QCOW2_DISCARD_MAX];
    uint64_t cache_clean_interval;
} Qcow2ReopenState;

static int qcow2_update_options_prepare(BlockDriverState *bs,
                                        Qcow2ReopenState *r,
                                        QDict *options, int flags,
                                        Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QemuOpts *opts = NULL;
    const char *opt_overlap_check, *opt_overlap_check_template;
    int overlap_check_template = 0;
    uint64_t l2_cache_size, refcount_cache_size;
    int i;
    Error *local_err = NULL;
    int ret;

    opts = qemu_opts_create(&qcow2_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    /* get L2 table/refcount block cache size from command line options */
    read_cache_sizes(bs, opts, &l2_cache_size, &refcount_cache_size,
                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    l2_cache_size /= s->cluster_size;
    if (l2_cache_size < MIN_L2_CACHE_SIZE) {
        l2_cache_size = MIN_L2_CACHE_SIZE;
    }
    if (l2_cache_size > INT_MAX) {
        error_setg(errp, "L2 cache size too big");
        ret = -EINVAL;
        goto fail;
    }

    refcount_cache_size /= s->cluster_size;
    if (refcount_cache_size < MIN_REFCOUNT_CACHE_SIZE) {
        refcount_cache_size = MIN_REFCOUNT_CACHE_SIZE;
    }
    if (refcount_cache_size > INT_MAX) {
        error_setg(errp, "Refcount cache size too big");
        ret = -EINVAL;
        goto fail;
    }

    /* alloc new L2 table/refcount block cache, flush old one */
    if (s->l2_table_cache) {
        ret = qcow2_cache_flush(bs, s->l2_table_cache);
        if (ret) {
            error_setg_errno(errp, -ret, "Failed to flush the L2 table cache");
            goto fail;
        }
    }

    if (s->refcount_block_cache) {
        ret = qcow2_cache_flush(bs, s->refcount_block_cache);
        if (ret) {
            error_setg_errno(errp, -ret,
                             "Failed to flush the refcount block cache");
            goto fail;
        }
    }

    r->l2_table_cache = qcow2_cache_create(bs, l2_cache_size);
    r->refcount_block_cache = qcow2_cache_create(bs, refcount_cache_size);
    if (r->l2_table_cache == NULL || r->refcount_block_cache == NULL) {
        error_setg(errp, "Could not allocate metadata caches");
        ret = -ENOMEM;
        goto fail;
    }

    /* New interval for cache cleanup timer */
    r->cache_clean_interval =
        qemu_opt_get_number(opts, QCOW2_OPT_CACHE_CLEAN_INTERVAL,
                            s->cache_clean_interval);
    if (r->cache_clean_interval > UINT_MAX) {
        error_setg(errp, "Cache clean interval too big");
        ret = -EINVAL;
        goto fail;
    }

    /* lazy-refcounts; flush if going from enabled to disabled */
    r->use_lazy_refcounts = qemu_opt_get_bool(opts, QCOW2_OPT_LAZY_REFCOUNTS,
        (s->compatible_features & QCOW2_COMPAT_LAZY_REFCOUNTS));
    if (r->use_lazy_refcounts && s->qcow_version < 3) {
        error_setg(errp, "Lazy refcounts require a qcow2 image with at least "
                   "qemu 1.1 compatibility level");
        ret = -EINVAL;
        goto fail;
    }

    if (s->use_lazy_refcounts && !r->use_lazy_refcounts) {
        ret = qcow2_mark_clean(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to disable lazy refcounts");
            goto fail;
        }
    }

    /* Overlap check options */
    opt_overlap_check = qemu_opt_get(opts, QCOW2_OPT_OVERLAP);
    opt_overlap_check_template = qemu_opt_get(opts, QCOW2_OPT_OVERLAP_TEMPLATE);
    if (opt_overlap_check_template && opt_overlap_check &&
        strcmp(opt_overlap_check_template, opt_overlap_check))
    {
        error_setg(errp, "Conflicting values for qcow2 options '"
                   QCOW2_OPT_OVERLAP "' ('%s') and '" QCOW2_OPT_OVERLAP_TEMPLATE
                   "' ('%s')", opt_overlap_check, opt_overlap_check_template);
        ret = -EINVAL;
        goto fail;
    }
    if (!opt_overlap_check) {
        opt_overlap_check = opt_overlap_check_template ?: "cached";
    }

    if (!strcmp(opt_overlap_check, "none")) {
        overlap_check_template = 0;
    } else if (!strcmp(opt_overlap_check, "constant")) {
        overlap_check_template = QCOW2_OL_CONSTANT;
    } else if (!strcmp(opt_overlap_check, "cached")) {
        overlap_check_template = QCOW2_OL_CACHED;
    } else if (!strcmp(opt_overlap_check, "all")) {
        overlap_check_template = QCOW2_OL_ALL;
    } else {
        error_setg(errp, "Unsupported value '%s' for qcow2 option "
                   "'overlap-check'. Allowed are any of the following: "
                   "none, constant, cached, all", opt_overlap_check);
        ret = -EINVAL;
        goto fail;
    }

    r->overlap_check = 0;
    for (i = 0; i < QCOW2_OL_MAX_BITNR; i++) {
        /* overlap-check defines a template bitmask, but every flag may be
         * overwritten through the associated boolean option */
        r->overlap_check |=
            qemu_opt_get_bool(opts, overlap_bool_option_names[i],
                              overlap_check_template & (1 << i)) << i;
    }

    r->discard_passthrough[QCOW2_DISCARD_NEVER] = false;
    r->discard_passthrough[QCOW2_DISCARD_ALWAYS] = true;
    r->discard_passthrough[QCOW2_DISCARD_REQUEST] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_REQUEST,
                          flags & BDRV_O_UNMAP);
    r->discard_passthrough[QCOW2_DISCARD_SNAPSHOT] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_SNAPSHOT, true);
    r->discard_passthrough[QCOW2_DISCARD_OTHER] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_OTHER, false);

    ret = 0;
fail:
    qemu_opts_del(opts);
    opts = NULL;
    return ret;
}

static void qcow2_update_options_commit(BlockDriverState *bs,
                                        Qcow2ReopenState *r)
{
    BDRVQcow2State *s = bs->opaque;
    int i;

    if (s->l2_table_cache) {
        qcow2_cache_destroy(bs, s->l2_table_cache);
    }
    if (s->refcount_block_cache) {
        qcow2_cache_destroy(bs, s->refcount_block_cache);
    }
    s->l2_table_cache = r->l2_table_cache;
    s->refcount_block_cache = r->refcount_block_cache;

    s->overlap_check = r->overlap_check;
    s->use_lazy_refcounts = r->use_lazy_refcounts;

    for (i = 0; i < QCOW2_DISCARD_MAX; i++) {
        s->discard_passthrough[i] = r->discard_passthrough[i];
    }

    if (s->cache_clean_interval != r->cache_clean_interval) {
        cache_clean_timer_del(bs);
        s->cache_clean_interval = r->cache_clean_interval;
        cache_clean_timer_init(bs, bdrv_get_aio_context(bs));
    }
}

static void qcow2_update_options_abort(BlockDriverState *bs,
                                       Qcow2ReopenState *r)
{
    if (r->l2_table_cache) {
        qcow2_cache_destroy(bs, r->l2_table_cache);
    }
    if (r->refcount_block_cache) {
        qcow2_cache_destroy(bs, r->refcount_block_cache);
    }
}

static int qcow2_update_options(BlockDriverState *bs, QDict *options,
                                int flags, Error **errp)
{
    Qcow2ReopenState r = {};
    int ret;

    ret = qcow2_update_options_prepare(bs, &r, options, flags, errp);
    if (ret >= 0) {
        qcow2_update_options_commit(bs, &r);
    } else {
        qcow2_update_options_abort(bs, &r);
    }

    return ret;
}

static int qcow2_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int len, i;
    int ret = 0;
    QCowHeader header;
    Error *local_err = NULL;
    uint64_t ext_end;
    uint64_t l1_vm_state_index;

    ret = bdrv_pread(bs->file->bs, 0, &header, sizeof(header));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read qcow2 header");
        goto fail;
    }
    be32_to_cpus(&header.magic);
    be32_to_cpus(&header.version);
    be64_to_cpus(&header.backing_file_offset);
    be32_to_cpus(&header.backing_file_size);
    be64_to_cpus(&header.size);
    be32_to_cpus(&header.cluster_bits);
    be32_to_cpus(&header.crypt_method);
    be64_to_cpus(&header.l1_table_offset);
    be32_to_cpus(&header.l1_size);
    be64_to_cpus(&header.refcount_table_offset);
    be32_to_cpus(&header.refcount_table_clusters);
    be64_to_cpus(&header.snapshots_offset);
    be32_to_cpus(&header.nb_snapshots);

    if (header.magic != QCOW_MAGIC) {
        error_setg(errp, "Image is not in qcow2 format");
        ret = -EINVAL;
        goto fail;
    }
    if (header.version < 2 || header.version > 3) {
        error_setg(errp, "Unsupported qcow2 version %" PRIu32, header.version);
        ret = -ENOTSUP;
        goto fail;
    }

    s->qcow_version = header.version;

    /* Initialise cluster size */
    if (header.cluster_bits < MIN_CLUSTER_BITS ||
        header.cluster_bits > MAX_CLUSTER_BITS) {
        error_setg(errp, "Unsupported cluster size: 2^%" PRIu32,
                   header.cluster_bits);
        ret = -EINVAL;
        goto fail;
    }

    s->cluster_bits = header.cluster_bits;
    s->cluster_size = 1 << s->cluster_bits;
    s->cluster_sectors = 1 << (s->cluster_bits - 9);

    /* Initialise version 3 header fields */
    if (header.version == 2) {
        header.incompatible_features    = 0;
        header.compatible_features      = 0;
        header.autoclear_features       = 0;
        header.refcount_order           = 4;
        header.header_length            = 72;
    } else {
        be64_to_cpus(&header.incompatible_features);
        be64_to_cpus(&header.compatible_features);
        be64_to_cpus(&header.autoclear_features);
        be32_to_cpus(&header.refcount_order);
        be32_to_cpus(&header.header_length);

        if (header.header_length < 104) {
            error_setg(errp, "qcow2 header too short");
            ret = -EINVAL;
            goto fail;
        }
    }

    if (header.header_length > s->cluster_size) {
        error_setg(errp, "qcow2 header exceeds cluster size");
        ret = -EINVAL;
        goto fail;
    }

    if (header.header_length > sizeof(header)) {
        s->unknown_header_fields_size = header.header_length - sizeof(header);
        s->unknown_header_fields = g_malloc(s->unknown_header_fields_size);
        ret = bdrv_pread(bs->file->bs, sizeof(header), s->unknown_header_fields,
                         s->unknown_header_fields_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not read unknown qcow2 header "
                             "fields");
            goto fail;
        }
    }

    if (header.backing_file_offset > s->cluster_size) {
        error_setg(errp, "Invalid backing file offset");
        ret = -EINVAL;
        goto fail;
    }

    if (header.backing_file_offset) {
        ext_end = header.backing_file_offset;
    } else {
        ext_end = 1 << header.cluster_bits;
    }

    /* Handle feature bits */
    s->incompatible_features    = header.incompatible_features;
    s->compatible_features      = header.compatible_features;
    s->autoclear_features       = header.autoclear_features;

    if (s->incompatible_features & ~QCOW2_INCOMPAT_MASK) {
        void *feature_table = NULL;
        qcow2_read_extensions(bs, header.header_length, ext_end,
                              &feature_table, NULL);
        report_unsupported_feature(errp, feature_table,
                                   s->incompatible_features &
                                   ~QCOW2_INCOMPAT_MASK);
        ret = -ENOTSUP;
        g_free(feature_table);
        goto fail;
    }

    if (s->incompatible_features & QCOW2_INCOMPAT_CORRUPT) {
        /* Corrupt images may not be written to unless they are being repaired
         */
        if ((flags & BDRV_O_RDWR) && !(flags & BDRV_O_CHECK)) {
            error_setg(errp, "qcow2: Image is corrupt; cannot be opened "
                       "read/write");
            ret = -EACCES;
            goto fail;
        }
    }

    /* Check support for various header values */
    if (header.refcount_order > 6) {
        error_setg(errp, "Reference count entry width too large; may not "
                   "exceed 64 bits");
        ret = -EINVAL;
        goto fail;
    }
    s->refcount_order = header.refcount_order;
    s->refcount_bits = 1 << s->refcount_order;
    s->refcount_max = UINT64_C(1) << (s->refcount_bits - 1);
    s->refcount_max += s->refcount_max - 1;

    if (header.crypt_method > QCOW_CRYPT_AES) {
        error_setg(errp, "Unsupported encryption method: %" PRIu32,
                   header.crypt_method);
        ret = -EINVAL;
        goto fail;
    }
    if (!qcrypto_cipher_supports(QCRYPTO_CIPHER_ALG_AES_128)) {
        error_setg(errp, "AES cipher not available");
        ret = -EINVAL;
        goto fail;
    }
    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header) {
        if (bdrv_uses_whitelist() &&
            s->crypt_method_header == QCOW_CRYPT_AES) {
            error_report("qcow2 built-in AES encryption is deprecated");
            error_printf("Support for it will be removed in a future release.\n"
                         "You can use 'qemu-img convert' to switch to an\n"
                         "unencrypted qcow2 image, or a LUKS raw image.\n");
        }

        bs->encrypted = 1;
    }

    s->l2_bits = s->cluster_bits - 3; /* L2 is always one cluster */
    s->l2_size = 1 << s->l2_bits;
    /* 2^(s->refcount_order - 3) is the refcount width in bytes */
    s->refcount_block_bits = s->cluster_bits - (s->refcount_order - 3);
    s->refcount_block_size = 1 << s->refcount_block_bits;
    bs->total_sectors = header.size / 512;
    s->csize_shift = (62 - (s->cluster_bits - 8));
    s->csize_mask = (1 << (s->cluster_bits - 8)) - 1;
    s->cluster_offset_mask = (1LL << s->csize_shift) - 1;

    s->refcount_table_offset = header.refcount_table_offset;
    s->refcount_table_size =
        header.refcount_table_clusters << (s->cluster_bits - 3);

    if (header.refcount_table_clusters > qcow2_max_refcount_clusters(s)) {
        error_setg(errp, "Reference count table too large");
        ret = -EINVAL;
        goto fail;
    }

    ret = validate_table_offset(bs, s->refcount_table_offset,
                                s->refcount_table_size, sizeof(uint64_t));
    if (ret < 0) {
        error_setg(errp, "Invalid reference count table offset");
        goto fail;
    }

    /* Snapshot table offset/length */
    if (header.nb_snapshots > QCOW_MAX_SNAPSHOTS) {
        error_setg(errp, "Too many snapshots");
        ret = -EINVAL;
        goto fail;
    }

    ret = validate_table_offset(bs, header.snapshots_offset,
                                header.nb_snapshots,
                                sizeof(QCowSnapshotHeader));
    if (ret < 0) {
        error_setg(errp, "Invalid snapshot table offset");
        goto fail;
    }

    /* read the level 1 table */
    if (header.l1_size > QCOW_MAX_L1_SIZE / sizeof(uint64_t)) {
        error_setg(errp, "Active L1 table too large");
        ret = -EFBIG;
        goto fail;
    }
    s->l1_size = header.l1_size;

    l1_vm_state_index = size_to_l1(s, header.size);
    if (l1_vm_state_index > INT_MAX) {
        error_setg(errp, "Image is too big");
        ret = -EFBIG;
        goto fail;
    }
    s->l1_vm_state_index = l1_vm_state_index;

    /* the L1 table must contain at least enough entries to put
       header.size bytes */
    if (s->l1_size < s->l1_vm_state_index) {
        error_setg(errp, "L1 table is too small");
        ret = -EINVAL;
        goto fail;
    }

    ret = validate_table_offset(bs, header.l1_table_offset,
                                header.l1_size, sizeof(uint64_t));
    if (ret < 0) {
        error_setg(errp, "Invalid L1 table offset");
        goto fail;
    }
    s->l1_table_offset = header.l1_table_offset;


    if (s->l1_size > 0) {
        s->l1_table = qemu_try_blockalign(bs->file->bs,
            align_offset(s->l1_size * sizeof(uint64_t), 512));
        if (s->l1_table == NULL) {
            error_setg(errp, "Could not allocate L1 table");
            ret = -ENOMEM;
            goto fail;
        }
        ret = bdrv_pread(bs->file->bs, s->l1_table_offset, s->l1_table,
                         s->l1_size * sizeof(uint64_t));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not read L1 table");
            goto fail;
        }
        for(i = 0;i < s->l1_size; i++) {
            be64_to_cpus(&s->l1_table[i]);
        }
    }

    /* Parse driver-specific options */
    ret = qcow2_update_options(bs, options, flags, errp);
    if (ret < 0) {
        goto fail;
    }

    s->cluster_cache = g_malloc(s->cluster_size);
    /* one more sector for decompressed data alignment */
    s->cluster_data = qemu_try_blockalign(bs->file->bs, QCOW_MAX_CRYPT_CLUSTERS
                                                    * s->cluster_size + 512);
    if (s->cluster_data == NULL) {
        error_setg(errp, "Could not allocate temporary cluster buffer");
        ret = -ENOMEM;
        goto fail;
    }

    s->cluster_cache_offset = -1;
    s->flags = flags;

    ret = qcow2_refcount_init(bs);
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Could not initialize refcount handling");
        goto fail;
    }

    QLIST_INIT(&s->cluster_allocs);
    QTAILQ_INIT(&s->discards);

    /* read qcow2 extensions */
    if (qcow2_read_extensions(bs, header.header_length, ext_end, NULL,
        &local_err)) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    /* read the backing file name */
    if (header.backing_file_offset != 0) {
        len = header.backing_file_size;
        if (len > MIN(1023, s->cluster_size - header.backing_file_offset) ||
            len >= sizeof(bs->backing_file)) {
            error_setg(errp, "Backing file name too long");
            ret = -EINVAL;
            goto fail;
        }
        ret = bdrv_pread(bs->file->bs, header.backing_file_offset,
                         bs->backing_file, len);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not read backing file name");
            goto fail;
        }
        bs->backing_file[len] = '\0';
        s->image_backing_file = g_strdup(bs->backing_file);
    }

    /* Internal snapshots */
    s->snapshots_offset = header.snapshots_offset;
    s->nb_snapshots = header.nb_snapshots;

    ret = qcow2_read_snapshots(bs);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read snapshots");
        goto fail;
    }

    /* Clear unknown autoclear feature bits */
    if (!bs->read_only && !(flags & BDRV_O_INACTIVE) && s->autoclear_features) {
        s->autoclear_features = 0;
        ret = qcow2_update_header(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not update qcow2 header");
            goto fail;
        }
    }

    /* Initialise locks */
    qemu_co_mutex_init(&s->lock);

    /* Repair image if dirty */
    if (!(flags & (BDRV_O_CHECK | BDRV_O_INACTIVE)) && !bs->read_only &&
        (s->incompatible_features & QCOW2_INCOMPAT_DIRTY)) {
        BdrvCheckResult result = {0};

        ret = qcow2_check(bs, &result, BDRV_FIX_ERRORS | BDRV_FIX_LEAKS);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not repair dirty image");
            goto fail;
        }
    }

#ifdef DEBUG_ALLOC
    {
        BdrvCheckResult result = {0};
        qcow2_check_refcounts(bs, &result, 0);
    }
#endif
    return ret;

 fail:
    g_free(s->unknown_header_fields);
    cleanup_unknown_header_ext(bs);
    qcow2_free_snapshots(bs);
    qcow2_refcount_close(bs);
    qemu_vfree(s->l1_table);
    /* else pre-write overlap checks in cache_destroy may crash */
    s->l1_table = NULL;
    cache_clean_timer_del(bs);
    if (s->l2_table_cache) {
        qcow2_cache_destroy(bs, s->l2_table_cache);
    }
    if (s->refcount_block_cache) {
        qcow2_cache_destroy(bs, s->refcount_block_cache);
    }
    g_free(s->cluster_cache);
    qemu_vfree(s->cluster_data);
    return ret;
}

static void qcow2_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;

    bs->bl.pwrite_zeroes_alignment = s->cluster_size;
}

static int qcow2_set_key(BlockDriverState *bs, const char *key)
{
    BDRVQcow2State *s = bs->opaque;
    uint8_t keybuf[16];
    int len, i;
    Error *err = NULL;

    memset(keybuf, 0, 16);
    len = strlen(key);
    if (len > 16)
        len = 16;
    /* XXX: we could compress the chars to 7 bits to increase
       entropy */
    for(i = 0;i < len;i++) {
        keybuf[i] = key[i];
    }
    assert(bs->encrypted);

    qcrypto_cipher_free(s->cipher);
    s->cipher = qcrypto_cipher_new(
        QCRYPTO_CIPHER_ALG_AES_128,
        QCRYPTO_CIPHER_MODE_CBC,
        keybuf, G_N_ELEMENTS(keybuf),
        &err);

    if (!s->cipher) {
        /* XXX would be nice if errors in this method could
         * be properly propagate to the caller. Would need
         * the bdrv_set_key() API signature to be fixed. */
        error_free(err);
        return -1;
    }
    return 0;
}

static int qcow2_reopen_prepare(BDRVReopenState *state,
                                BlockReopenQueue *queue, Error **errp)
{
    Qcow2ReopenState *r;
    int ret;

    r = g_new0(Qcow2ReopenState, 1);
    state->opaque = r;

    ret = qcow2_update_options_prepare(state->bs, r, state->options,
                                       state->flags, errp);
    if (ret < 0) {
        goto fail;
    }

    /* We need to write out any unwritten data if we reopen read-only. */
    if ((state->flags & BDRV_O_RDWR) == 0) {
        ret = bdrv_flush(state->bs);
        if (ret < 0) {
            goto fail;
        }

        ret = qcow2_mark_clean(state->bs);
        if (ret < 0) {
            goto fail;
        }
    }

    return 0;

fail:
    qcow2_update_options_abort(state->bs, r);
    g_free(r);
    return ret;
}

static void qcow2_reopen_commit(BDRVReopenState *state)
{
    qcow2_update_options_commit(state->bs, state->opaque);
    g_free(state->opaque);
}

static void qcow2_reopen_abort(BDRVReopenState *state)
{
    qcow2_update_options_abort(state->bs, state->opaque);
    g_free(state->opaque);
}

static void qcow2_join_options(QDict *options, QDict *old_options)
{
    bool has_new_overlap_template =
        qdict_haskey(options, QCOW2_OPT_OVERLAP) ||
        qdict_haskey(options, QCOW2_OPT_OVERLAP_TEMPLATE);
    bool has_new_total_cache_size =
        qdict_haskey(options, QCOW2_OPT_CACHE_SIZE);
    bool has_all_cache_options;

    /* New overlap template overrides all old overlap options */
    if (has_new_overlap_template) {
        qdict_del(old_options, QCOW2_OPT_OVERLAP);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_TEMPLATE);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_MAIN_HEADER);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_ACTIVE_L1);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_ACTIVE_L2);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_REFCOUNT_TABLE);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_INACTIVE_L1);
        qdict_del(old_options, QCOW2_OPT_OVERLAP_INACTIVE_L2);
    }

    /* New total cache size overrides all old options */
    if (qdict_haskey(options, QCOW2_OPT_CACHE_SIZE)) {
        qdict_del(old_options, QCOW2_OPT_L2_CACHE_SIZE);
        qdict_del(old_options, QCOW2_OPT_REFCOUNT_CACHE_SIZE);
    }

    qdict_join(options, old_options, false);

    /*
     * If after merging all cache size options are set, an old total size is
     * overwritten. Do keep all options, however, if all three are new. The
     * resulting error message is what we want to happen.
     */
    has_all_cache_options =
        qdict_haskey(options, QCOW2_OPT_CACHE_SIZE) ||
        qdict_haskey(options, QCOW2_OPT_L2_CACHE_SIZE) ||
        qdict_haskey(options, QCOW2_OPT_REFCOUNT_CACHE_SIZE);

    if (has_all_cache_options && !has_new_total_cache_size) {
        qdict_del(options, QCOW2_OPT_CACHE_SIZE);
    }
}

static int64_t coroutine_fn qcow2_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum, BlockDriverState **file)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t cluster_offset;
    int index_in_cluster, ret;
    int64_t status = 0;

    *pnum = nb_sectors;
    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_get_cluster_offset(bs, sector_num << 9, pnum, &cluster_offset);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        return ret;
    }

    if (cluster_offset != 0 && ret != QCOW2_CLUSTER_COMPRESSED &&
        !s->cipher) {
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        cluster_offset |= (index_in_cluster << BDRV_SECTOR_BITS);
        *file = bs->file->bs;
        status |= BDRV_BLOCK_OFFSET_VALID | cluster_offset;
    }
    if (ret == QCOW2_CLUSTER_ZERO) {
        status |= BDRV_BLOCK_ZERO;
    } else if (ret != QCOW2_CLUSTER_UNALLOCATED) {
        status |= BDRV_BLOCK_DATA;
    }
    return status;
}

/* handle reading after the end of the backing file */
int qcow2_backing_read1(BlockDriverState *bs, QEMUIOVector *qiov,
                  int64_t sector_num, int nb_sectors)
{
    int n1;
    if ((sector_num + nb_sectors) <= bs->total_sectors)
        return nb_sectors;
    if (sector_num >= bs->total_sectors)
        n1 = 0;
    else
        n1 = bs->total_sectors - sector_num;

    qemu_iovec_memset(qiov, 512 * n1, 0, 512 * (nb_sectors - n1));

    return n1;
}

static coroutine_fn int qcow2_co_readv(BlockDriverState *bs, int64_t sector_num,
                          int remaining_sectors, QEMUIOVector *qiov)
{
    BDRVQcow2State *s = bs->opaque;
    int index_in_cluster, n1;
    int ret;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t cluster_offset = 0;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;
    uint8_t *cluster_data = NULL;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&s->lock);

    while (remaining_sectors != 0) {

        /* prepare next request */
        cur_nr_sectors = remaining_sectors;
        if (s->cipher) {
            cur_nr_sectors = MIN(cur_nr_sectors,
                QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors);
        }

        ret = qcow2_get_cluster_offset(bs, sector_num << 9,
            &cur_nr_sectors, &cluster_offset);
        if (ret < 0) {
            goto fail;
        }

        index_in_cluster = sector_num & (s->cluster_sectors - 1);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done,
            cur_nr_sectors * 512);

        switch (ret) {
        case QCOW2_CLUSTER_UNALLOCATED:

            if (bs->backing) {
                /* read from the base image */
                n1 = qcow2_backing_read1(bs->backing->bs, &hd_qiov,
                    sector_num, cur_nr_sectors);
                if (n1 > 0) {
                    QEMUIOVector local_qiov;

                    qemu_iovec_init(&local_qiov, hd_qiov.niov);
                    qemu_iovec_concat(&local_qiov, &hd_qiov, 0,
                                      n1 * BDRV_SECTOR_SIZE);

                    BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                    qemu_co_mutex_unlock(&s->lock);
                    ret = bdrv_co_readv(bs->backing->bs, sector_num,
                                        n1, &local_qiov);
                    qemu_co_mutex_lock(&s->lock);

                    qemu_iovec_destroy(&local_qiov);

                    if (ret < 0) {
                        goto fail;
                    }
                }
            } else {
                /* Note: in this case, no need to wait */
                qemu_iovec_memset(&hd_qiov, 0, 0, 512 * cur_nr_sectors);
            }
            break;

        case QCOW2_CLUSTER_ZERO:
            qemu_iovec_memset(&hd_qiov, 0, 0, 512 * cur_nr_sectors);
            break;

        case QCOW2_CLUSTER_COMPRESSED:
            /* add AIO support for compressed blocks ? */
            ret = qcow2_decompress_cluster(bs, cluster_offset);
            if (ret < 0) {
                goto fail;
            }

            qemu_iovec_from_buf(&hd_qiov, 0,
                s->cluster_cache + index_in_cluster * 512,
                512 * cur_nr_sectors);
            break;

        case QCOW2_CLUSTER_NORMAL:
            if ((cluster_offset & 511) != 0) {
                ret = -EIO;
                goto fail;
            }

            if (bs->encrypted) {
                assert(s->cipher);

                /*
                 * For encrypted images, read everything into a temporary
                 * contiguous buffer on which the AES functions can work.
                 */
                if (!cluster_data) {
                    cluster_data =
                        qemu_try_blockalign(bs->file->bs,
                                            QCOW_MAX_CRYPT_CLUSTERS
                                            * s->cluster_size);
                    if (cluster_data == NULL) {
                        ret = -ENOMEM;
                        goto fail;
                    }
                }

                assert(cur_nr_sectors <=
                    QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors);
                qemu_iovec_reset(&hd_qiov);
                qemu_iovec_add(&hd_qiov, cluster_data,
                    512 * cur_nr_sectors);
            }

            BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
            qemu_co_mutex_unlock(&s->lock);
            ret = bdrv_co_readv(bs->file->bs,
                                (cluster_offset >> 9) + index_in_cluster,
                                cur_nr_sectors, &hd_qiov);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                goto fail;
            }
            if (bs->encrypted) {
                assert(s->cipher);
                Error *err = NULL;
                if (qcow2_encrypt_sectors(s, sector_num,  cluster_data,
                                          cluster_data, cur_nr_sectors, false,
                                          &err) < 0) {
                    error_free(err);
                    ret = -EIO;
                    goto fail;
                }
                qemu_iovec_from_buf(qiov, bytes_done,
                    cluster_data, 512 * cur_nr_sectors);
            }
            break;

        default:
            g_assert_not_reached();
            ret = -EIO;
            goto fail;
        }

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
    }
    ret = 0;

fail:
    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cluster_data);

    return ret;
}

static coroutine_fn int qcow2_co_writev(BlockDriverState *bs,
                           int64_t sector_num,
                           int remaining_sectors,
                           QEMUIOVector *qiov)
{
    BDRVQcow2State *s = bs->opaque;
    int index_in_cluster;
    int ret;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t cluster_offset;
    QEMUIOVector hd_qiov;
    uint64_t bytes_done = 0;
    uint8_t *cluster_data = NULL;
    QCowL2Meta *l2meta = NULL;

    trace_qcow2_writev_start_req(qemu_coroutine_self(), sector_num,
                                 remaining_sectors);

    qemu_iovec_init(&hd_qiov, qiov->niov);

    s->cluster_cache_offset = -1; /* disable compressed cache */

    qemu_co_mutex_lock(&s->lock);

    while (remaining_sectors != 0) {

        l2meta = NULL;

        trace_qcow2_writev_start_part(qemu_coroutine_self());
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        cur_nr_sectors = remaining_sectors;
        if (bs->encrypted &&
            cur_nr_sectors >
            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors - index_in_cluster) {
            cur_nr_sectors =
                QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors - index_in_cluster;
        }

        ret = qcow2_alloc_cluster_offset(bs, sector_num << 9,
            &cur_nr_sectors, &cluster_offset, &l2meta);
        if (ret < 0) {
            goto fail;
        }

        assert((cluster_offset & 511) == 0);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done,
            cur_nr_sectors * 512);

        if (bs->encrypted) {
            Error *err = NULL;
            assert(s->cipher);
            if (!cluster_data) {
                cluster_data = qemu_try_blockalign(bs->file->bs,
                                                   QCOW_MAX_CRYPT_CLUSTERS
                                                   * s->cluster_size);
                if (cluster_data == NULL) {
                    ret = -ENOMEM;
                    goto fail;
                }
            }

            assert(hd_qiov.size <=
                   QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
            qemu_iovec_to_buf(&hd_qiov, 0, cluster_data, hd_qiov.size);

            if (qcow2_encrypt_sectors(s, sector_num, cluster_data,
                                      cluster_data, cur_nr_sectors,
                                      true, &err) < 0) {
                error_free(err);
                ret = -EIO;
                goto fail;
            }

            qemu_iovec_reset(&hd_qiov);
            qemu_iovec_add(&hd_qiov, cluster_data,
                cur_nr_sectors * 512);
        }

        ret = qcow2_pre_write_overlap_check(bs, 0,
                cluster_offset + index_in_cluster * BDRV_SECTOR_SIZE,
                cur_nr_sectors * BDRV_SECTOR_SIZE);
        if (ret < 0) {
            goto fail;
        }

        qemu_co_mutex_unlock(&s->lock);
        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
        trace_qcow2_writev_data(qemu_coroutine_self(),
                                (cluster_offset >> 9) + index_in_cluster);
        ret = bdrv_co_writev(bs->file->bs,
                             (cluster_offset >> 9) + index_in_cluster,
                             cur_nr_sectors, &hd_qiov);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            goto fail;
        }

        while (l2meta != NULL) {
            QCowL2Meta *next;

            ret = qcow2_alloc_cluster_link_l2(bs, l2meta);
            if (ret < 0) {
                goto fail;
            }

            /* Take the request off the list of running requests */
            if (l2meta->nb_clusters != 0) {
                QLIST_REMOVE(l2meta, next_in_flight);
            }

            qemu_co_queue_restart_all(&l2meta->dependent_requests);

            next = l2meta->next;
            g_free(l2meta);
            l2meta = next;
        }

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
        trace_qcow2_writev_done_part(qemu_coroutine_self(), cur_nr_sectors);
    }
    ret = 0;

fail:
    qemu_co_mutex_unlock(&s->lock);

    while (l2meta != NULL) {
        QCowL2Meta *next;

        if (l2meta->nb_clusters != 0) {
            QLIST_REMOVE(l2meta, next_in_flight);
        }
        qemu_co_queue_restart_all(&l2meta->dependent_requests);

        next = l2meta->next;
        g_free(l2meta);
        l2meta = next;
    }

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cluster_data);
    trace_qcow2_writev_done_req(qemu_coroutine_self(), ret);

    return ret;
}

static int qcow2_inactivate(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int ret, result = 0;

    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret) {
        result = ret;
        error_report("Failed to flush the L2 table cache: %s",
                     strerror(-ret));
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret) {
        result = ret;
        error_report("Failed to flush the refcount block cache: %s",
                     strerror(-ret));
    }

    if (result == 0) {
        qcow2_mark_clean(bs);
    }

    return result;
}

static void qcow2_close(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    qemu_vfree(s->l1_table);
    /* else pre-write overlap checks in cache_destroy may crash */
    s->l1_table = NULL;

    if (!(s->flags & BDRV_O_INACTIVE)) {
        qcow2_inactivate(bs);
    }

    cache_clean_timer_del(bs);
    qcow2_cache_destroy(bs, s->l2_table_cache);
    qcow2_cache_destroy(bs, s->refcount_block_cache);

    qcrypto_cipher_free(s->cipher);
    s->cipher = NULL;

    g_free(s->unknown_header_fields);
    cleanup_unknown_header_ext(bs);

    g_free(s->image_backing_file);
    g_free(s->image_backing_format);

    g_free(s->cluster_cache);
    qemu_vfree(s->cluster_data);
    qcow2_refcount_close(bs);
    qcow2_free_snapshots(bs);
}

static void qcow2_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int flags = s->flags;
    QCryptoCipher *cipher = NULL;
    QDict *options;
    Error *local_err = NULL;
    int ret;

    /*
     * Backing files are read-only which makes all of their metadata immutable,
     * that means we don't have to worry about reopening them here.
     */

    cipher = s->cipher;
    s->cipher = NULL;

    qcow2_close(bs);

    memset(s, 0, sizeof(BDRVQcow2State));
    options = qdict_clone_shallow(bs->options);

    flags &= ~BDRV_O_INACTIVE;
    ret = qcow2_open(bs, options, flags, &local_err);
    QDECREF(options);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "Could not reopen qcow2 layer: ");
        bs->drv = NULL;
        return;
    } else if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not reopen qcow2 layer");
        bs->drv = NULL;
        return;
    }

    s->cipher = cipher;
}

static size_t header_ext_add(char *buf, uint32_t magic, const void *s,
    size_t len, size_t buflen)
{
    QCowExtension *ext_backing_fmt = (QCowExtension*) buf;
    size_t ext_len = sizeof(QCowExtension) + ((len + 7) & ~7);

    if (buflen < ext_len) {
        return -ENOSPC;
    }

    *ext_backing_fmt = (QCowExtension) {
        .magic  = cpu_to_be32(magic),
        .len    = cpu_to_be32(len),
    };
    memcpy(buf + sizeof(QCowExtension), s, len);

    return ext_len;
}

/*
 * Updates the qcow2 header, including the variable length parts of it, i.e.
 * the backing file name and all extensions. qcow2 was not designed to allow
 * such changes, so if we run out of space (we can only use the first cluster)
 * this function may fail.
 *
 * Returns 0 on success, -errno in error cases.
 */
int qcow2_update_header(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    QCowHeader *header;
    char *buf;
    size_t buflen = s->cluster_size;
    int ret;
    uint64_t total_size;
    uint32_t refcount_table_clusters;
    size_t header_length;
    Qcow2UnknownHeaderExtension *uext;

    buf = qemu_blockalign(bs, buflen);

    /* Header structure */
    header = (QCowHeader*) buf;

    if (buflen < sizeof(*header)) {
        ret = -ENOSPC;
        goto fail;
    }

    header_length = sizeof(*header) + s->unknown_header_fields_size;
    total_size = bs->total_sectors * BDRV_SECTOR_SIZE;
    refcount_table_clusters = s->refcount_table_size >> (s->cluster_bits - 3);

    *header = (QCowHeader) {
        /* Version 2 fields */
        .magic                  = cpu_to_be32(QCOW_MAGIC),
        .version                = cpu_to_be32(s->qcow_version),
        .backing_file_offset    = 0,
        .backing_file_size      = 0,
        .cluster_bits           = cpu_to_be32(s->cluster_bits),
        .size                   = cpu_to_be64(total_size),
        .crypt_method           = cpu_to_be32(s->crypt_method_header),
        .l1_size                = cpu_to_be32(s->l1_size),
        .l1_table_offset        = cpu_to_be64(s->l1_table_offset),
        .refcount_table_offset  = cpu_to_be64(s->refcount_table_offset),
        .refcount_table_clusters = cpu_to_be32(refcount_table_clusters),
        .nb_snapshots           = cpu_to_be32(s->nb_snapshots),
        .snapshots_offset       = cpu_to_be64(s->snapshots_offset),

        /* Version 3 fields */
        .incompatible_features  = cpu_to_be64(s->incompatible_features),
        .compatible_features    = cpu_to_be64(s->compatible_features),
        .autoclear_features     = cpu_to_be64(s->autoclear_features),
        .refcount_order         = cpu_to_be32(s->refcount_order),
        .header_length          = cpu_to_be32(header_length),
    };

    /* For older versions, write a shorter header */
    switch (s->qcow_version) {
    case 2:
        ret = offsetof(QCowHeader, incompatible_features);
        break;
    case 3:
        ret = sizeof(*header);
        break;
    default:
        ret = -EINVAL;
        goto fail;
    }

    buf += ret;
    buflen -= ret;
    memset(buf, 0, buflen);

    /* Preserve any unknown field in the header */
    if (s->unknown_header_fields_size) {
        if (buflen < s->unknown_header_fields_size) {
            ret = -ENOSPC;
            goto fail;
        }

        memcpy(buf, s->unknown_header_fields, s->unknown_header_fields_size);
        buf += s->unknown_header_fields_size;
        buflen -= s->unknown_header_fields_size;
    }

    /* Backing file format header extension */
    if (s->image_backing_format) {
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_BACKING_FORMAT,
                             s->image_backing_format,
                             strlen(s->image_backing_format),
                             buflen);
        if (ret < 0) {
            goto fail;
        }

        buf += ret;
        buflen -= ret;
    }

    /* Feature table */
    if (s->qcow_version >= 3) {
        Qcow2Feature features[] = {
            {
                .type = QCOW2_FEAT_TYPE_INCOMPATIBLE,
                .bit  = QCOW2_INCOMPAT_DIRTY_BITNR,
                .name = "dirty bit",
            },
            {
                .type = QCOW2_FEAT_TYPE_INCOMPATIBLE,
                .bit  = QCOW2_INCOMPAT_CORRUPT_BITNR,
                .name = "corrupt bit",
            },
            {
                .type = QCOW2_FEAT_TYPE_COMPATIBLE,
                .bit  = QCOW2_COMPAT_LAZY_REFCOUNTS_BITNR,
                .name = "lazy refcounts",
            },
        };

        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_FEATURE_TABLE,
                             features, sizeof(features), buflen);
        if (ret < 0) {
            goto fail;
        }
        buf += ret;
        buflen -= ret;
    }

    /* Keep unknown header extensions */
    QLIST_FOREACH(uext, &s->unknown_header_ext, next) {
        ret = header_ext_add(buf, uext->magic, uext->data, uext->len, buflen);
        if (ret < 0) {
            goto fail;
        }

        buf += ret;
        buflen -= ret;
    }

    /* End of header extensions */
    ret = header_ext_add(buf, QCOW2_EXT_MAGIC_END, NULL, 0, buflen);
    if (ret < 0) {
        goto fail;
    }

    buf += ret;
    buflen -= ret;

    /* Backing file name */
    if (s->image_backing_file) {
        size_t backing_file_len = strlen(s->image_backing_file);

        if (buflen < backing_file_len) {
            ret = -ENOSPC;
            goto fail;
        }

        /* Using strncpy is ok here, since buf is not NUL-terminated. */
        strncpy(buf, s->image_backing_file, buflen);

        header->backing_file_offset = cpu_to_be64(buf - ((char*) header));
        header->backing_file_size   = cpu_to_be32(backing_file_len);
    }

    /* Write the new header */
    ret = bdrv_pwrite(bs->file->bs, 0, header, s->cluster_size);
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
fail:
    qemu_vfree(header);
    return ret;
}

static int qcow2_change_backing_file(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt)
{
    BDRVQcow2State *s = bs->opaque;

    if (backing_file && strlen(backing_file) > 1023) {
        return -EINVAL;
    }

    pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_file ?: "");
    pstrcpy(bs->backing_format, sizeof(bs->backing_format), backing_fmt ?: "");

    g_free(s->image_backing_file);
    g_free(s->image_backing_format);

    s->image_backing_file = backing_file ? g_strdup(bs->backing_file) : NULL;
    s->image_backing_format = backing_fmt ? g_strdup(bs->backing_format) : NULL;

    return qcow2_update_header(bs);
}

static int preallocate(BlockDriverState *bs)
{
    uint64_t nb_sectors;
    uint64_t offset;
    uint64_t host_offset = 0;
    int num;
    int ret;
    QCowL2Meta *meta;

    nb_sectors = bdrv_nb_sectors(bs);
    offset = 0;

    while (nb_sectors) {
        num = MIN(nb_sectors, INT_MAX >> BDRV_SECTOR_BITS);
        ret = qcow2_alloc_cluster_offset(bs, offset, &num,
                                         &host_offset, &meta);
        if (ret < 0) {
            return ret;
        }

        while (meta) {
            QCowL2Meta *next = meta->next;

            ret = qcow2_alloc_cluster_link_l2(bs, meta);
            if (ret < 0) {
                qcow2_free_any_clusters(bs, meta->alloc_offset,
                                        meta->nb_clusters, QCOW2_DISCARD_NEVER);
                return ret;
            }

            /* There are no dependent requests, but we need to remove our
             * request from the list of in-flight requests */
            QLIST_REMOVE(meta, next_in_flight);

            g_free(meta);
            meta = next;
        }

        /* TODO Preallocate data if requested */

        nb_sectors -= num;
        offset += num << BDRV_SECTOR_BITS;
    }

    /*
     * It is expected that the image file is large enough to actually contain
     * all of the allocated clusters (otherwise we get failing reads after
     * EOF). Extend the image to the last allocated sector.
     */
    if (host_offset != 0) {
        uint8_t buf[BDRV_SECTOR_SIZE];
        memset(buf, 0, BDRV_SECTOR_SIZE);
        ret = bdrv_write(bs->file->bs,
                         (host_offset >> BDRV_SECTOR_BITS) + num - 1,
                         buf, 1);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int qcow2_create2(const char *filename, int64_t total_size,
                         const char *backing_file, const char *backing_format,
                         int flags, size_t cluster_size, PreallocMode prealloc,
                         QemuOpts *opts, int version, int refcount_order,
                         Error **errp)
{
    int cluster_bits;
    QDict *options;

    /* Calculate cluster_bits */
    cluster_bits = ctz32(cluster_size);
    if (cluster_bits < MIN_CLUSTER_BITS || cluster_bits > MAX_CLUSTER_BITS ||
        (1 << cluster_bits) != cluster_size)
    {
        error_setg(errp, "Cluster size must be a power of two between %d and "
                   "%dk", 1 << MIN_CLUSTER_BITS, 1 << (MAX_CLUSTER_BITS - 10));
        return -EINVAL;
    }

    /*
     * Open the image file and write a minimal qcow2 header.
     *
     * We keep things simple and start with a zero-sized image. We also
     * do without refcount blocks or a L1 table for now. We'll fix the
     * inconsistency later.
     *
     * We do need a refcount table because growing the refcount table means
     * allocating two new refcount blocks - the seconds of which would be at
     * 2 GB for 64k clusters, and we don't want to have a 2 GB initial file
     * size for any qcow2 image.
     */
    BlockBackend *blk;
    QCowHeader *header;
    uint64_t* refcount_table;
    Error *local_err = NULL;
    int ret;

    if (prealloc == PREALLOC_MODE_FULL || prealloc == PREALLOC_MODE_FALLOC) {
        /* Note: The following calculation does not need to be exact; if it is a
         * bit off, either some bytes will be "leaked" (which is fine) or we
         * will need to increase the file size by some bytes (which is fine,
         * too, as long as the bulk is allocated here). Therefore, using
         * floating point arithmetic is fine. */
        int64_t meta_size = 0;
        uint64_t nreftablee, nrefblocke, nl1e, nl2e;
        int64_t aligned_total_size = align_offset(total_size, cluster_size);
        int refblock_bits, refblock_size;
        /* refcount entry size in bytes */
        double rces = (1 << refcount_order) / 8.;

        /* see qcow2_open() */
        refblock_bits = cluster_bits - (refcount_order - 3);
        refblock_size = 1 << refblock_bits;

        /* header: 1 cluster */
        meta_size += cluster_size;

        /* total size of L2 tables */
        nl2e = aligned_total_size / cluster_size;
        nl2e = align_offset(nl2e, cluster_size / sizeof(uint64_t));
        meta_size += nl2e * sizeof(uint64_t);

        /* total size of L1 tables */
        nl1e = nl2e * sizeof(uint64_t) / cluster_size;
        nl1e = align_offset(nl1e, cluster_size / sizeof(uint64_t));
        meta_size += nl1e * sizeof(uint64_t);

        /* total size of refcount blocks
         *
         * note: every host cluster is reference-counted, including metadata
         * (even refcount blocks are recursively included).
         * Let:
         *   a = total_size (this is the guest disk size)
         *   m = meta size not including refcount blocks and refcount tables
         *   c = cluster size
         *   y1 = number of refcount blocks entries
         *   y2 = meta size including everything
         *   rces = refcount entry size in bytes
         * then,
         *   y1 = (y2 + a)/c
         *   y2 = y1 * rces + y1 * rces * sizeof(u64) / c + m
         * we can get y1:
         *   y1 = (a + m) / (c - rces - rces * sizeof(u64) / c)
         */
        nrefblocke = (aligned_total_size + meta_size + cluster_size)
                   / (cluster_size - rces - rces * sizeof(uint64_t)
                                                 / cluster_size);
        meta_size += DIV_ROUND_UP(nrefblocke, refblock_size) * cluster_size;

        /* total size of refcount tables */
        nreftablee = nrefblocke / refblock_size;
        nreftablee = align_offset(nreftablee, cluster_size / sizeof(uint64_t));
        meta_size += nreftablee * sizeof(uint64_t);

        qemu_opt_set_number(opts, BLOCK_OPT_SIZE,
                            aligned_total_size + meta_size, &error_abort);
        qemu_opt_set(opts, BLOCK_OPT_PREALLOC, PreallocMode_lookup[prealloc],
                     &error_abort);
    }

    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }

    blk = blk_new_open(filename, NULL, NULL,
                       BDRV_O_RDWR | BDRV_O_PROTOCOL, &local_err);
    if (blk == NULL) {
        error_propagate(errp, local_err);
        return -EIO;
    }

    blk_set_allow_write_beyond_eof(blk, true);

    /* Write the header */
    QEMU_BUILD_BUG_ON((1 << MIN_CLUSTER_BITS) < sizeof(*header));
    header = g_malloc0(cluster_size);
    *header = (QCowHeader) {
        .magic                      = cpu_to_be32(QCOW_MAGIC),
        .version                    = cpu_to_be32(version),
        .cluster_bits               = cpu_to_be32(cluster_bits),
        .size                       = cpu_to_be64(0),
        .l1_table_offset            = cpu_to_be64(0),
        .l1_size                    = cpu_to_be32(0),
        .refcount_table_offset      = cpu_to_be64(cluster_size),
        .refcount_table_clusters    = cpu_to_be32(1),
        .refcount_order             = cpu_to_be32(refcount_order),
        .header_length              = cpu_to_be32(sizeof(*header)),
    };

    if (flags & BLOCK_FLAG_ENCRYPT) {
        header->crypt_method = cpu_to_be32(QCOW_CRYPT_AES);
    } else {
        header->crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);
    }

    if (flags & BLOCK_FLAG_LAZY_REFCOUNTS) {
        header->compatible_features |=
            cpu_to_be64(QCOW2_COMPAT_LAZY_REFCOUNTS);
    }

    ret = blk_pwrite(blk, 0, header, cluster_size, 0);
    g_free(header);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write qcow2 header");
        goto out;
    }

    /* Write a refcount table with one refcount block */
    refcount_table = g_malloc0(2 * cluster_size);
    refcount_table[0] = cpu_to_be64(2 * cluster_size);
    ret = blk_pwrite(blk, cluster_size, refcount_table, 2 * cluster_size, 0);
    g_free(refcount_table);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write refcount table");
        goto out;
    }

    blk_unref(blk);
    blk = NULL;

    /*
     * And now open the image and make it consistent first (i.e. increase the
     * refcount of the cluster that is occupied by the header and the refcount
     * table)
     */
    options = qdict_new();
    qdict_put(options, "driver", qstring_from_str("qcow2"));
    blk = blk_new_open(filename, NULL, options,
                       BDRV_O_RDWR | BDRV_O_NO_FLUSH, &local_err);
    if (blk == NULL) {
        error_propagate(errp, local_err);
        ret = -EIO;
        goto out;
    }

    ret = qcow2_alloc_clusters(blk_bs(blk), 3 * cluster_size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not allocate clusters for qcow2 "
                         "header and refcount table");
        goto out;

    } else if (ret != 0) {
        error_report("Huh, first cluster in empty image is already in use?");
        abort();
    }

    /* Create a full header (including things like feature table) */
    ret = qcow2_update_header(blk_bs(blk));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not update qcow2 header");
        goto out;
    }

    /* Okay, now that we have a valid image, let's give it the right size */
    ret = blk_truncate(blk, total_size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not resize image");
        goto out;
    }

    /* Want a backing file? There you go.*/
    if (backing_file) {
        ret = bdrv_change_backing_file(blk_bs(blk), backing_file, backing_format);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not assign backing file '%s' "
                             "with format '%s'", backing_file, backing_format);
            goto out;
        }
    }

    /* And if we're supposed to preallocate metadata, do that now */
    if (prealloc != PREALLOC_MODE_OFF) {
        BDRVQcow2State *s = blk_bs(blk)->opaque;
        qemu_co_mutex_lock(&s->lock);
        ret = preallocate(blk_bs(blk));
        qemu_co_mutex_unlock(&s->lock);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not preallocate metadata");
            goto out;
        }
    }

    blk_unref(blk);
    blk = NULL;

    /* Reopen the image without BDRV_O_NO_FLUSH to flush it before returning */
    options = qdict_new();
    qdict_put(options, "driver", qstring_from_str("qcow2"));
    blk = blk_new_open(filename, NULL, options,
                       BDRV_O_RDWR | BDRV_O_NO_BACKING, &local_err);
    if (blk == NULL) {
        error_propagate(errp, local_err);
        ret = -EIO;
        goto out;
    }

    ret = 0;
out:
    if (blk) {
        blk_unref(blk);
    }
    return ret;
}

static int qcow2_create(const char *filename, QemuOpts *opts, Error **errp)
{
    char *backing_file = NULL;
    char *backing_fmt = NULL;
    char *buf = NULL;
    uint64_t size = 0;
    int flags = 0;
    size_t cluster_size = DEFAULT_CLUSTER_SIZE;
    PreallocMode prealloc;
    int version = 3;
    uint64_t refcount_bits = 16;
    int refcount_order;
    Error *local_err = NULL;
    int ret;

    /* Read out options */
    size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                    BDRV_SECTOR_SIZE);
    backing_file = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    backing_fmt = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FMT);
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_ENCRYPT, false)) {
        flags |= BLOCK_FLAG_ENCRYPT;
    }
    cluster_size = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE,
                                         DEFAULT_CLUSTER_SIZE);
    buf = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    prealloc = qapi_enum_parse(PreallocMode_lookup, buf,
                               PREALLOC_MODE__MAX, PREALLOC_MODE_OFF,
                               &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto finish;
    }
    g_free(buf);
    buf = qemu_opt_get_del(opts, BLOCK_OPT_COMPAT_LEVEL);
    if (!buf) {
        /* keep the default */
    } else if (!strcmp(buf, "0.10")) {
        version = 2;
    } else if (!strcmp(buf, "1.1")) {
        version = 3;
    } else {
        error_setg(errp, "Invalid compatibility level: '%s'", buf);
        ret = -EINVAL;
        goto finish;
    }

    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_LAZY_REFCOUNTS, false)) {
        flags |= BLOCK_FLAG_LAZY_REFCOUNTS;
    }

    if (backing_file && prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Backing file and preallocation cannot be used at "
                   "the same time");
        ret = -EINVAL;
        goto finish;
    }

    if (version < 3 && (flags & BLOCK_FLAG_LAZY_REFCOUNTS)) {
        error_setg(errp, "Lazy refcounts only supported with compatibility "
                   "level 1.1 and above (use compat=1.1 or greater)");
        ret = -EINVAL;
        goto finish;
    }

    refcount_bits = qemu_opt_get_number_del(opts, BLOCK_OPT_REFCOUNT_BITS,
                                            refcount_bits);
    if (refcount_bits > 64 || !is_power_of_2(refcount_bits)) {
        error_setg(errp, "Refcount width must be a power of two and may not "
                   "exceed 64 bits");
        ret = -EINVAL;
        goto finish;
    }

    if (version < 3 && refcount_bits != 16) {
        error_setg(errp, "Different refcount widths than 16 bits require "
                   "compatibility level 1.1 or above (use compat=1.1 or "
                   "greater)");
        ret = -EINVAL;
        goto finish;
    }

    refcount_order = ctz32(refcount_bits);

    ret = qcow2_create2(filename, size, backing_file, backing_fmt, flags,
                        cluster_size, prealloc, opts, version, refcount_order,
                        &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }

finish:
    g_free(backing_file);
    g_free(backing_fmt);
    g_free(buf);
    return ret;
}


static bool is_zero_sectors(BlockDriverState *bs, int64_t start,
                            uint32_t count)
{
    int nr;
    BlockDriverState *file;
    int64_t res;

    if (!count) {
        return true;
    }
    res = bdrv_get_block_status_above(bs, NULL, start, count,
                                      &nr, &file);
    return res >= 0 && (res & BDRV_BLOCK_ZERO) && nr == count;
}

static coroutine_fn int qcow2_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int count, BdrvRequestFlags flags)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;

    uint32_t head = offset % s->cluster_size;
    uint32_t tail = (offset + count) % s->cluster_size;

    trace_qcow2_pwrite_zeroes_start_req(qemu_coroutine_self(), offset, count);

    if (head || tail) {
        int64_t cl_start = (offset - head) >> BDRV_SECTOR_BITS;
        uint64_t off;
        int nr;

        assert(head + count <= s->cluster_size);

        /* check whether remainder of cluster already reads as zero */
        if (!(is_zero_sectors(bs, cl_start,
                              DIV_ROUND_UP(head, BDRV_SECTOR_SIZE)) &&
              is_zero_sectors(bs, (offset + count) >> BDRV_SECTOR_BITS,
                              DIV_ROUND_UP(-tail & (s->cluster_size - 1),
                                           BDRV_SECTOR_SIZE)))) {
            return -ENOTSUP;
        }

        qemu_co_mutex_lock(&s->lock);
        /* We can have new write after previous check */
        offset = cl_start << BDRV_SECTOR_BITS;
        count = s->cluster_size;
        nr = s->cluster_sectors;
        ret = qcow2_get_cluster_offset(bs, offset, &nr, &off);
        if (ret != QCOW2_CLUSTER_UNALLOCATED && ret != QCOW2_CLUSTER_ZERO) {
            qemu_co_mutex_unlock(&s->lock);
            return -ENOTSUP;
        }
    } else {
        qemu_co_mutex_lock(&s->lock);
    }

    trace_qcow2_pwrite_zeroes(qemu_coroutine_self(), offset, count);

    /* Whatever is left can use real zero clusters */
    ret = qcow2_zero_clusters(bs, offset, count >> BDRV_SECTOR_BITS);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static coroutine_fn int qcow2_co_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_discard_clusters(bs, sector_num << BDRV_SECTOR_BITS,
        nb_sectors, QCOW2_DISCARD_REQUEST, false);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int qcow2_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t new_l1_size;
    int ret;

    if (offset & 511) {
        error_report("The new size must be a multiple of 512");
        return -EINVAL;
    }

    /* cannot proceed if image has snapshots */
    if (s->nb_snapshots) {
        error_report("Can't resize an image which has snapshots");
        return -ENOTSUP;
    }

    /* shrinking is currently not supported */
    if (offset < bs->total_sectors * 512) {
        error_report("qcow2 doesn't support shrinking images yet");
        return -ENOTSUP;
    }

    new_l1_size = size_to_l1(s, offset);
    ret = qcow2_grow_l1_table(bs, new_l1_size, true);
    if (ret < 0) {
        return ret;
    }

    /* write updated header.size */
    offset = cpu_to_be64(offset);
    ret = bdrv_pwrite_sync(bs->file->bs, offsetof(QCowHeader, size),
                           &offset, sizeof(uint64_t));
    if (ret < 0) {
        return ret;
    }

    s->l1_vm_state_index = new_l1_size;
    return 0;
}

/* XXX: put compressed sectors first, then all the cluster aligned
   tables to avoid losing bytes in alignment */
static int qcow2_write_compressed(BlockDriverState *bs, int64_t sector_num,
                                  const uint8_t *buf, int nb_sectors)
{
    BDRVQcow2State *s = bs->opaque;
    z_stream strm;
    int ret, out_len;
    uint8_t *out_buf;
    uint64_t cluster_offset;

    if (nb_sectors == 0) {
        /* align end of file to a sector boundary to ease reading with
           sector based I/Os */
        cluster_offset = bdrv_getlength(bs->file->bs);
        return bdrv_truncate(bs->file->bs, cluster_offset);
    }

    if (nb_sectors != s->cluster_sectors) {
        ret = -EINVAL;

        /* Zero-pad last write if image size is not cluster aligned */
        if (sector_num + nb_sectors == bs->total_sectors &&
            nb_sectors < s->cluster_sectors) {
            uint8_t *pad_buf = qemu_blockalign(bs, s->cluster_size);
            memset(pad_buf, 0, s->cluster_size);
            memcpy(pad_buf, buf, nb_sectors * BDRV_SECTOR_SIZE);
            ret = qcow2_write_compressed(bs, sector_num,
                                         pad_buf, s->cluster_sectors);
            qemu_vfree(pad_buf);
        }
        return ret;
    }

    out_buf = g_malloc(s->cluster_size + (s->cluster_size / 1000) + 128);

    /* best compression, small window, no zlib header */
    memset(&strm, 0, sizeof(strm));
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION,
                       Z_DEFLATED, -12,
                       9, Z_DEFAULT_STRATEGY);
    if (ret != 0) {
        ret = -EINVAL;
        goto fail;
    }

    strm.avail_in = s->cluster_size;
    strm.next_in = (uint8_t *)buf;
    strm.avail_out = s->cluster_size;
    strm.next_out = out_buf;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        deflateEnd(&strm);
        ret = -EINVAL;
        goto fail;
    }
    out_len = strm.next_out - out_buf;

    deflateEnd(&strm);

    if (ret != Z_STREAM_END || out_len >= s->cluster_size) {
        /* could not compress: write normal cluster */
        ret = bdrv_write(bs, sector_num, buf, s->cluster_sectors);
        if (ret < 0) {
            goto fail;
        }
    } else {
        cluster_offset = qcow2_alloc_compressed_cluster_offset(bs,
            sector_num << 9, out_len);
        if (!cluster_offset) {
            ret = -EIO;
            goto fail;
        }
        cluster_offset &= s->cluster_offset_mask;

        ret = qcow2_pre_write_overlap_check(bs, 0, cluster_offset, out_len);
        if (ret < 0) {
            goto fail;
        }

        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_COMPRESSED);
        ret = bdrv_pwrite(bs->file->bs, cluster_offset, out_buf, out_len);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;
fail:
    g_free(out_buf);
    return ret;
}

static int make_completely_empty(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int ret, l1_clusters;
    int64_t offset;
    uint64_t *new_reftable = NULL;
    uint64_t rt_entry, l1_size2;
    struct {
        uint64_t l1_offset;
        uint64_t reftable_offset;
        uint32_t reftable_clusters;
    } QEMU_PACKED l1_ofs_rt_ofs_cls;

    ret = qcow2_cache_empty(bs, s->l2_table_cache);
    if (ret < 0) {
        goto fail;
    }

    ret = qcow2_cache_empty(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* Refcounts will be broken utterly */
    ret = qcow2_mark_dirty(bs);
    if (ret < 0) {
        goto fail;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);

    l1_clusters = DIV_ROUND_UP(s->l1_size, s->cluster_size / sizeof(uint64_t));
    l1_size2 = (uint64_t)s->l1_size * sizeof(uint64_t);

    /* After this call, neither the in-memory nor the on-disk refcount
     * information accurately describe the actual references */

    ret = bdrv_pwrite_zeroes(bs->file->bs, s->l1_table_offset,
                             l1_clusters * s->cluster_size, 0);
    if (ret < 0) {
        goto fail_broken_refcounts;
    }
    memset(s->l1_table, 0, l1_size2);

    BLKDBG_EVENT(bs->file, BLKDBG_EMPTY_IMAGE_PREPARE);

    /* Overwrite enough clusters at the beginning of the sectors to place
     * the refcount table, a refcount block and the L1 table in; this may
     * overwrite parts of the existing refcount and L1 table, which is not
     * an issue because the dirty flag is set, complete data loss is in fact
     * desired and partial data loss is consequently fine as well */
    ret = bdrv_pwrite_zeroes(bs->file->bs, s->cluster_size,
                             (2 + l1_clusters) * s->cluster_size, 0);
    /* This call (even if it failed overall) may have overwritten on-disk
     * refcount structures; in that case, the in-memory refcount information
     * will probably differ from the on-disk information which makes the BDS
     * unusable */
    if (ret < 0) {
        goto fail_broken_refcounts;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);
    BLKDBG_EVENT(bs->file, BLKDBG_REFTABLE_UPDATE);

    /* "Create" an empty reftable (one cluster) directly after the image
     * header and an empty L1 table three clusters after the image header;
     * the cluster between those two will be used as the first refblock */
    cpu_to_be64w(&l1_ofs_rt_ofs_cls.l1_offset, 3 * s->cluster_size);
    cpu_to_be64w(&l1_ofs_rt_ofs_cls.reftable_offset, s->cluster_size);
    cpu_to_be32w(&l1_ofs_rt_ofs_cls.reftable_clusters, 1);
    ret = bdrv_pwrite_sync(bs->file->bs, offsetof(QCowHeader, l1_table_offset),
                           &l1_ofs_rt_ofs_cls, sizeof(l1_ofs_rt_ofs_cls));
    if (ret < 0) {
        goto fail_broken_refcounts;
    }

    s->l1_table_offset = 3 * s->cluster_size;

    new_reftable = g_try_new0(uint64_t, s->cluster_size / sizeof(uint64_t));
    if (!new_reftable) {
        ret = -ENOMEM;
        goto fail_broken_refcounts;
    }

    s->refcount_table_offset = s->cluster_size;
    s->refcount_table_size   = s->cluster_size / sizeof(uint64_t);

    g_free(s->refcount_table);
    s->refcount_table = new_reftable;
    new_reftable = NULL;

    /* Now the in-memory refcount information again corresponds to the on-disk
     * information (reftable is empty and no refblocks (the refblock cache is
     * empty)); however, this means some clusters (e.g. the image header) are
     * referenced, but not refcounted, but the normal qcow2 code assumes that
     * the in-memory information is always correct */

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC);

    /* Enter the first refblock into the reftable */
    rt_entry = cpu_to_be64(2 * s->cluster_size);
    ret = bdrv_pwrite_sync(bs->file->bs, s->cluster_size,
                           &rt_entry, sizeof(rt_entry));
    if (ret < 0) {
        goto fail_broken_refcounts;
    }
    s->refcount_table[0] = 2 * s->cluster_size;

    s->free_cluster_index = 0;
    assert(3 + l1_clusters <= s->refcount_block_size);
    offset = qcow2_alloc_clusters(bs, 3 * s->cluster_size + l1_size2);
    if (offset < 0) {
        ret = offset;
        goto fail_broken_refcounts;
    } else if (offset > 0) {
        error_report("First cluster in emptied image is in use");
        abort();
    }

    /* Now finally the in-memory information corresponds to the on-disk
     * structures and is correct */
    ret = qcow2_mark_clean(bs);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_truncate(bs->file->bs, (3 + l1_clusters) * s->cluster_size);
    if (ret < 0) {
        goto fail;
    }

    return 0;

fail_broken_refcounts:
    /* The BDS is unusable at this point. If we wanted to make it usable, we
     * would have to call qcow2_refcount_close(), qcow2_refcount_init(),
     * qcow2_check_refcounts(), qcow2_refcount_close() and qcow2_refcount_init()
     * again. However, because the functions which could have caused this error
     * path to be taken are used by those functions as well, it's very likely
     * that that sequence will fail as well. Therefore, just eject the BDS. */
    bs->drv = NULL;

fail:
    g_free(new_reftable);
    return ret;
}

static int qcow2_make_empty(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t start_sector;
    int sector_step = INT_MAX / BDRV_SECTOR_SIZE;
    int l1_clusters, ret = 0;

    l1_clusters = DIV_ROUND_UP(s->l1_size, s->cluster_size / sizeof(uint64_t));

    if (s->qcow_version >= 3 && !s->snapshots &&
        3 + l1_clusters <= s->refcount_block_size) {
        /* The following function only works for qcow2 v3 images (it requires
         * the dirty flag) and only as long as there are no snapshots (because
         * it completely empties the image). Furthermore, the L1 table and three
         * additional clusters (image header, refcount table, one refcount
         * block) have to fit inside one refcount block. */
        return make_completely_empty(bs);
    }

    /* This fallback code simply discards every active cluster; this is slow,
     * but works in all cases */
    for (start_sector = 0; start_sector < bs->total_sectors;
         start_sector += sector_step)
    {
        /* As this function is generally used after committing an external
         * snapshot, QCOW2_DISCARD_SNAPSHOT seems appropriate. Also, the
         * default action for this kind of discard is to pass the discard,
         * which will ideally result in an actually smaller image file, as
         * is probably desired. */
        ret = qcow2_discard_clusters(bs, start_sector * BDRV_SECTOR_SIZE,
                                     MIN(sector_step,
                                         bs->total_sectors - start_sector),
                                     QCOW2_DISCARD_SNAPSHOT, true);
        if (ret < 0) {
            break;
        }
    }

    return ret;
}

static coroutine_fn int qcow2_co_flush_to_os(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        qemu_co_mutex_unlock(&s->lock);
        return ret;
    }

    if (qcow2_need_accurate_refcounts(s)) {
        ret = qcow2_cache_flush(bs, s->refcount_block_cache);
        if (ret < 0) {
            qemu_co_mutex_unlock(&s->lock);
            return ret;
        }
    }
    qemu_co_mutex_unlock(&s->lock);

    return 0;
}

static int qcow2_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQcow2State *s = bs->opaque;
    bdi->unallocated_blocks_are_zero = true;
    bdi->can_write_zeroes_with_unmap = (s->qcow_version >= 3);
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = qcow2_vm_state_offset(s);
    return 0;
}

static ImageInfoSpecific *qcow2_get_specific_info(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    ImageInfoSpecific *spec_info = g_new(ImageInfoSpecific, 1);

    *spec_info = (ImageInfoSpecific){
        .type  = IMAGE_INFO_SPECIFIC_KIND_QCOW2,
        .u.qcow2.data = g_new(ImageInfoSpecificQCow2, 1),
    };
    if (s->qcow_version == 2) {
        *spec_info->u.qcow2.data = (ImageInfoSpecificQCow2){
            .compat             = g_strdup("0.10"),
            .refcount_bits      = s->refcount_bits,
        };
    } else if (s->qcow_version == 3) {
        *spec_info->u.qcow2.data = (ImageInfoSpecificQCow2){
            .compat             = g_strdup("1.1"),
            .lazy_refcounts     = s->compatible_features &
                                  QCOW2_COMPAT_LAZY_REFCOUNTS,
            .has_lazy_refcounts = true,
            .corrupt            = s->incompatible_features &
                                  QCOW2_INCOMPAT_CORRUPT,
            .has_corrupt        = true,
            .refcount_bits      = s->refcount_bits,
        };
    } else {
        /* if this assertion fails, this probably means a new version was
         * added without having it covered here */
        assert(false);
    }

    return spec_info;
}

#if 0
static void dump_refcounts(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t nb_clusters, k, k1, size;
    int refcount;

    size = bdrv_getlength(bs->file->bs);
    nb_clusters = size_to_clusters(s, size);
    for(k = 0; k < nb_clusters;) {
        k1 = k;
        refcount = get_refcount(bs, k);
        k++;
        while (k < nb_clusters && get_refcount(bs, k) == refcount)
            k++;
        printf("%" PRId64 ": refcount=%d nb=%" PRId64 "\n", k, refcount,
               k - k1);
    }
}
#endif

static int qcow2_save_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                              int64_t pos)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t total_sectors = bs->total_sectors;
    bool zero_beyond_eof = bs->zero_beyond_eof;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_SAVE);
    bs->zero_beyond_eof = false;
    ret = bdrv_pwritev(bs, qcow2_vm_state_offset(s) + pos, qiov);
    bs->zero_beyond_eof = zero_beyond_eof;

    /* bdrv_co_do_writev will have increased the total_sectors value to include
     * the VM state - the VM state is however not an actual part of the block
     * device, therefore, we need to restore the old value. */
    bs->total_sectors = total_sectors;

    return ret;
}

static int qcow2_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                              int64_t pos, int size)
{
    BDRVQcow2State *s = bs->opaque;
    bool zero_beyond_eof = bs->zero_beyond_eof;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_LOAD);
    bs->zero_beyond_eof = false;
    ret = bdrv_pread(bs, qcow2_vm_state_offset(s) + pos, buf, size);
    bs->zero_beyond_eof = zero_beyond_eof;

    return ret;
}

/*
 * Downgrades an image's version. To achieve this, any incompatible features
 * have to be removed.
 */
static int qcow2_downgrade(BlockDriverState *bs, int target_version,
                           BlockDriverAmendStatusCB *status_cb, void *cb_opaque)
{
    BDRVQcow2State *s = bs->opaque;
    int current_version = s->qcow_version;
    int ret;

    if (target_version == current_version) {
        return 0;
    } else if (target_version > current_version) {
        return -EINVAL;
    } else if (target_version != 2) {
        return -EINVAL;
    }

    if (s->refcount_order != 4) {
        error_report("compat=0.10 requires refcount_bits=16");
        return -ENOTSUP;
    }

    /* clear incompatible features */
    if (s->incompatible_features & QCOW2_INCOMPAT_DIRTY) {
        ret = qcow2_mark_clean(bs);
        if (ret < 0) {
            return ret;
        }
    }

    /* with QCOW2_INCOMPAT_CORRUPT, it is pretty much impossible to get here in
     * the first place; if that happens nonetheless, returning -ENOTSUP is the
     * best thing to do anyway */

    if (s->incompatible_features) {
        return -ENOTSUP;
    }

    /* since we can ignore compatible features, we can set them to 0 as well */
    s->compatible_features = 0;
    /* if lazy refcounts have been used, they have already been fixed through
     * clearing the dirty flag */

    /* clearing autoclear features is trivial */
    s->autoclear_features = 0;

    ret = qcow2_expand_zero_clusters(bs, status_cb, cb_opaque);
    if (ret < 0) {
        return ret;
    }

    s->qcow_version = target_version;
    ret = qcow2_update_header(bs);
    if (ret < 0) {
        s->qcow_version = current_version;
        return ret;
    }
    return 0;
}

typedef enum Qcow2AmendOperation {
    /* This is the value Qcow2AmendHelperCBInfo::last_operation will be
     * statically initialized to so that the helper CB can discern the first
     * invocation from an operation change */
    QCOW2_NO_OPERATION = 0,

    QCOW2_CHANGING_REFCOUNT_ORDER,
    QCOW2_DOWNGRADING,
} Qcow2AmendOperation;

typedef struct Qcow2AmendHelperCBInfo {
    /* The code coordinating the amend operations should only modify
     * these four fields; the rest will be managed by the CB */
    BlockDriverAmendStatusCB *original_status_cb;
    void *original_cb_opaque;

    Qcow2AmendOperation current_operation;

    /* Total number of operations to perform (only set once) */
    int total_operations;

    /* The following fields are managed by the CB */

    /* Number of operations completed */
    int operations_completed;

    /* Cumulative offset of all completed operations */
    int64_t offset_completed;

    Qcow2AmendOperation last_operation;
    int64_t last_work_size;
} Qcow2AmendHelperCBInfo;

static void qcow2_amend_helper_cb(BlockDriverState *bs,
                                  int64_t operation_offset,
                                  int64_t operation_work_size, void *opaque)
{
    Qcow2AmendHelperCBInfo *info = opaque;
    int64_t current_work_size;
    int64_t projected_work_size;

    if (info->current_operation != info->last_operation) {
        if (info->last_operation != QCOW2_NO_OPERATION) {
            info->offset_completed += info->last_work_size;
            info->operations_completed++;
        }

        info->last_operation = info->current_operation;
    }

    assert(info->total_operations > 0);
    assert(info->operations_completed < info->total_operations);

    info->last_work_size = operation_work_size;

    current_work_size = info->offset_completed + operation_work_size;

    /* current_work_size is the total work size for (operations_completed + 1)
     * operations (which includes this one), so multiply it by the number of
     * operations not covered and divide it by the number of operations
     * covered to get a projection for the operations not covered */
    projected_work_size = current_work_size * (info->total_operations -
                                               info->operations_completed - 1)
                                            / (info->operations_completed + 1);

    info->original_status_cb(bs, info->offset_completed + operation_offset,
                             current_work_size + projected_work_size,
                             info->original_cb_opaque);
}

static int qcow2_amend_options(BlockDriverState *bs, QemuOpts *opts,
                               BlockDriverAmendStatusCB *status_cb,
                               void *cb_opaque)
{
    BDRVQcow2State *s = bs->opaque;
    int old_version = s->qcow_version, new_version = old_version;
    uint64_t new_size = 0;
    const char *backing_file = NULL, *backing_format = NULL;
    bool lazy_refcounts = s->use_lazy_refcounts;
    const char *compat = NULL;
    uint64_t cluster_size = s->cluster_size;
    bool encrypt;
    int refcount_bits = s->refcount_bits;
    int ret;
    QemuOptDesc *desc = opts->list->desc;
    Qcow2AmendHelperCBInfo helper_cb_info;

    while (desc && desc->name) {
        if (!qemu_opt_find(opts, desc->name)) {
            /* only change explicitly defined options */
            desc++;
            continue;
        }

        if (!strcmp(desc->name, BLOCK_OPT_COMPAT_LEVEL)) {
            compat = qemu_opt_get(opts, BLOCK_OPT_COMPAT_LEVEL);
            if (!compat) {
                /* preserve default */
            } else if (!strcmp(compat, "0.10")) {
                new_version = 2;
            } else if (!strcmp(compat, "1.1")) {
                new_version = 3;
            } else {
                error_report("Unknown compatibility level %s", compat);
                return -EINVAL;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_PREALLOC)) {
            error_report("Cannot change preallocation mode");
            return -ENOTSUP;
        } else if (!strcmp(desc->name, BLOCK_OPT_SIZE)) {
            new_size = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 0);
        } else if (!strcmp(desc->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = qemu_opt_get(opts, BLOCK_OPT_BACKING_FILE);
        } else if (!strcmp(desc->name, BLOCK_OPT_BACKING_FMT)) {
            backing_format = qemu_opt_get(opts, BLOCK_OPT_BACKING_FMT);
        } else if (!strcmp(desc->name, BLOCK_OPT_ENCRYPT)) {
            encrypt = qemu_opt_get_bool(opts, BLOCK_OPT_ENCRYPT,
                                        !!s->cipher);

            if (encrypt != !!s->cipher) {
                error_report("Changing the encryption flag is not supported");
                return -ENOTSUP;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_CLUSTER_SIZE)) {
            cluster_size = qemu_opt_get_size(opts, BLOCK_OPT_CLUSTER_SIZE,
                                             cluster_size);
            if (cluster_size != s->cluster_size) {
                error_report("Changing the cluster size is not supported");
                return -ENOTSUP;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_LAZY_REFCOUNTS)) {
            lazy_refcounts = qemu_opt_get_bool(opts, BLOCK_OPT_LAZY_REFCOUNTS,
                                               lazy_refcounts);
        } else if (!strcmp(desc->name, BLOCK_OPT_REFCOUNT_BITS)) {
            refcount_bits = qemu_opt_get_number(opts, BLOCK_OPT_REFCOUNT_BITS,
                                                refcount_bits);

            if (refcount_bits <= 0 || refcount_bits > 64 ||
                !is_power_of_2(refcount_bits))
            {
                error_report("Refcount width must be a power of two and may "
                             "not exceed 64 bits");
                return -EINVAL;
            }
        } else {
            /* if this point is reached, this probably means a new option was
             * added without having it covered here */
            abort();
        }

        desc++;
    }

    helper_cb_info = (Qcow2AmendHelperCBInfo){
        .original_status_cb = status_cb,
        .original_cb_opaque = cb_opaque,
        .total_operations = (new_version < old_version)
                          + (s->refcount_bits != refcount_bits)
    };

    /* Upgrade first (some features may require compat=1.1) */
    if (new_version > old_version) {
        s->qcow_version = new_version;
        ret = qcow2_update_header(bs);
        if (ret < 0) {
            s->qcow_version = old_version;
            return ret;
        }
    }

    if (s->refcount_bits != refcount_bits) {
        int refcount_order = ctz32(refcount_bits);
        Error *local_error = NULL;

        if (new_version < 3 && refcount_bits != 16) {
            error_report("Different refcount widths than 16 bits require "
                         "compatibility level 1.1 or above (use compat=1.1 or "
                         "greater)");
            return -EINVAL;
        }

        helper_cb_info.current_operation = QCOW2_CHANGING_REFCOUNT_ORDER;
        ret = qcow2_change_refcount_order(bs, refcount_order,
                                          &qcow2_amend_helper_cb,
                                          &helper_cb_info, &local_error);
        if (ret < 0) {
            error_report_err(local_error);
            return ret;
        }
    }

    if (backing_file || backing_format) {
        ret = qcow2_change_backing_file(bs,
                    backing_file ?: s->image_backing_file,
                    backing_format ?: s->image_backing_format);
        if (ret < 0) {
            return ret;
        }
    }

    if (s->use_lazy_refcounts != lazy_refcounts) {
        if (lazy_refcounts) {
            if (new_version < 3) {
                error_report("Lazy refcounts only supported with compatibility "
                             "level 1.1 and above (use compat=1.1 or greater)");
                return -EINVAL;
            }
            s->compatible_features |= QCOW2_COMPAT_LAZY_REFCOUNTS;
            ret = qcow2_update_header(bs);
            if (ret < 0) {
                s->compatible_features &= ~QCOW2_COMPAT_LAZY_REFCOUNTS;
                return ret;
            }
            s->use_lazy_refcounts = true;
        } else {
            /* make image clean first */
            ret = qcow2_mark_clean(bs);
            if (ret < 0) {
                return ret;
            }
            /* now disallow lazy refcounts */
            s->compatible_features &= ~QCOW2_COMPAT_LAZY_REFCOUNTS;
            ret = qcow2_update_header(bs);
            if (ret < 0) {
                s->compatible_features |= QCOW2_COMPAT_LAZY_REFCOUNTS;
                return ret;
            }
            s->use_lazy_refcounts = false;
        }
    }

    if (new_size) {
        ret = bdrv_truncate(bs, new_size);
        if (ret < 0) {
            return ret;
        }
    }

    /* Downgrade last (so unsupported features can be removed before) */
    if (new_version < old_version) {
        helper_cb_info.current_operation = QCOW2_DOWNGRADING;
        ret = qcow2_downgrade(bs, new_version, &qcow2_amend_helper_cb,
                              &helper_cb_info);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/*
 * If offset or size are negative, respectively, they will not be included in
 * the BLOCK_IMAGE_CORRUPTED event emitted.
 * fatal will be ignored for read-only BDS; corruptions found there will always
 * be considered non-fatal.
 */
void qcow2_signal_corruption(BlockDriverState *bs, bool fatal, int64_t offset,
                             int64_t size, const char *message_format, ...)
{
    BDRVQcow2State *s = bs->opaque;
    const char *node_name;
    char *message;
    va_list ap;

    fatal = fatal && !bs->read_only;

    if (s->signaled_corruption &&
        (!fatal || (s->incompatible_features & QCOW2_INCOMPAT_CORRUPT)))
    {
        return;
    }

    va_start(ap, message_format);
    message = g_strdup_vprintf(message_format, ap);
    va_end(ap);

    if (fatal) {
        fprintf(stderr, "qcow2: Marking image as corrupt: %s; further "
                "corruption events will be suppressed\n", message);
    } else {
        fprintf(stderr, "qcow2: Image is corrupt: %s; further non-fatal "
                "corruption events will be suppressed\n", message);
    }

    node_name = bdrv_get_node_name(bs);
    qapi_event_send_block_image_corrupted(bdrv_get_device_name(bs),
                                          *node_name != '\0', node_name,
                                          message, offset >= 0, offset,
                                          size >= 0, size,
                                          fatal, &error_abort);
    g_free(message);

    if (fatal) {
        qcow2_mark_corrupt(bs);
        bs->drv = NULL; /* make BDS unusable */
    }

    s->signaled_corruption = true;
}

static QemuOptsList qcow2_create_opts = {
    .name = "qcow2-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qcow2_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_COMPAT_LEVEL,
            .type = QEMU_OPT_STRING,
            .help = "Compatibility level (0.10 or 1.1)"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        {
            .name = BLOCK_OPT_BACKING_FMT,
            .type = QEMU_OPT_STRING,
            .help = "Image format of the base image"
        },
        {
            .name = BLOCK_OPT_ENCRYPT,
            .type = QEMU_OPT_BOOL,
            .help = "Encrypt the image",
            .def_value_str = "off"
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "qcow2 cluster size",
            .def_value_str = stringify(DEFAULT_CLUSTER_SIZE)
        },
        {
            .name = BLOCK_OPT_PREALLOC,
            .type = QEMU_OPT_STRING,
            .help = "Preallocation mode (allowed values: off, metadata, "
                    "falloc, full)"
        },
        {
            .name = BLOCK_OPT_LAZY_REFCOUNTS,
            .type = QEMU_OPT_BOOL,
            .help = "Postpone refcount updates",
            .def_value_str = "off"
        },
        {
            .name = BLOCK_OPT_REFCOUNT_BITS,
            .type = QEMU_OPT_NUMBER,
            .help = "Width of a reference count entry in bits",
            .def_value_str = "16"
        },
        { /* end of list */ }
    }
};

BlockDriver bdrv_qcow2 = {
    .format_name        = "qcow2",
    .instance_size      = sizeof(BDRVQcow2State),
    .bdrv_probe         = qcow2_probe,
    .bdrv_open          = qcow2_open,
    .bdrv_close         = qcow2_close,
    .bdrv_reopen_prepare  = qcow2_reopen_prepare,
    .bdrv_reopen_commit   = qcow2_reopen_commit,
    .bdrv_reopen_abort    = qcow2_reopen_abort,
    .bdrv_join_options    = qcow2_join_options,
    .bdrv_create        = qcow2_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_co_get_block_status = qcow2_co_get_block_status,
    .bdrv_set_key       = qcow2_set_key,

    .bdrv_co_readv          = qcow2_co_readv,
    .bdrv_co_writev         = qcow2_co_writev,
    .bdrv_co_flush_to_os    = qcow2_co_flush_to_os,

    .bdrv_co_pwrite_zeroes  = qcow2_co_pwrite_zeroes,
    .bdrv_co_discard        = qcow2_co_discard,
    .bdrv_truncate          = qcow2_truncate,
    .bdrv_write_compressed  = qcow2_write_compressed,
    .bdrv_make_empty        = qcow2_make_empty,

    .bdrv_snapshot_create   = qcow2_snapshot_create,
    .bdrv_snapshot_goto     = qcow2_snapshot_goto,
    .bdrv_snapshot_delete   = qcow2_snapshot_delete,
    .bdrv_snapshot_list     = qcow2_snapshot_list,
    .bdrv_snapshot_load_tmp = qcow2_snapshot_load_tmp,
    .bdrv_get_info          = qcow2_get_info,
    .bdrv_get_specific_info = qcow2_get_specific_info,

    .bdrv_save_vmstate    = qcow2_save_vmstate,
    .bdrv_load_vmstate    = qcow2_load_vmstate,

    .supports_backing           = true,
    .bdrv_change_backing_file   = qcow2_change_backing_file,

    .bdrv_refresh_limits        = qcow2_refresh_limits,
    .bdrv_invalidate_cache      = qcow2_invalidate_cache,
    .bdrv_inactivate            = qcow2_inactivate,

    .create_opts         = &qcow2_create_opts,
    .bdrv_check          = qcow2_check,
    .bdrv_amend_options  = qcow2_amend_options,

    .bdrv_detach_aio_context  = qcow2_detach_aio_context,
    .bdrv_attach_aio_context  = qcow2_attach_aio_context,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
