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

#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qcow2.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-events-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "trace.h"
#include "qemu/option_int.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "crypto.h"
#include "block/aio_task.h"

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
#define  QCOW2_EXT_MAGIC_BACKING_FORMAT 0xe2792aca
#define  QCOW2_EXT_MAGIC_FEATURE_TABLE 0x6803f857
#define  QCOW2_EXT_MAGIC_CRYPTO_HEADER 0x0537be77
#define  QCOW2_EXT_MAGIC_BITMAPS 0x23852875
#define  QCOW2_EXT_MAGIC_DATA_FILE 0x44415441

static int coroutine_fn
qcow2_co_preadv_compressed(BlockDriverState *bs,
                           uint64_t file_cluster_offset,
                           uint64_t offset,
                           uint64_t bytes,
                           QEMUIOVector *qiov,
                           size_t qiov_offset);

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


static ssize_t qcow2_crypto_hdr_read_func(QCryptoBlock *block, size_t offset,
                                          uint8_t *buf, size_t buflen,
                                          void *opaque, Error **errp)
{
    BlockDriverState *bs = opaque;
    BDRVQcow2State *s = bs->opaque;
    ssize_t ret;

    if ((offset + buflen) > s->crypto_header.length) {
        error_setg(errp, "Request for data outside of extension header");
        return -1;
    }

    ret = bdrv_pread(bs->file,
                     s->crypto_header.offset + offset, buf, buflen);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read encryption header");
        return -1;
    }
    return ret;
}


static ssize_t qcow2_crypto_hdr_init_func(QCryptoBlock *block, size_t headerlen,
                                          void *opaque, Error **errp)
{
    BlockDriverState *bs = opaque;
    BDRVQcow2State *s = bs->opaque;
    int64_t ret;
    int64_t clusterlen;

    ret = qcow2_alloc_clusters(bs, headerlen);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Cannot allocate cluster for LUKS header size %zu",
                         headerlen);
        return -1;
    }

    s->crypto_header.length = headerlen;
    s->crypto_header.offset = ret;

    /*
     * Zero fill all space in cluster so it has predictable
     * content, as we may not initialize some regions of the
     * header (eg only 1 out of 8 key slots will be initialized)
     */
    clusterlen = size_to_clusters(s, headerlen) * s->cluster_size;
    assert(qcow2_pre_write_overlap_check(bs, 0, ret, clusterlen, false) == 0);
    ret = bdrv_pwrite_zeroes(bs->file,
                             ret,
                             clusterlen, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not zero fill encryption header");
        return -1;
    }

    return ret;
}


static ssize_t qcow2_crypto_hdr_write_func(QCryptoBlock *block, size_t offset,
                                           const uint8_t *buf, size_t buflen,
                                           void *opaque, Error **errp)
{
    BlockDriverState *bs = opaque;
    BDRVQcow2State *s = bs->opaque;
    ssize_t ret;

    if ((offset + buflen) > s->crypto_header.length) {
        error_setg(errp, "Request for data outside of extension header");
        return -1;
    }

    ret = bdrv_pwrite(bs->file,
                      s->crypto_header.offset + offset, buf, buflen);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read encryption header");
        return -1;
    }
    return ret;
}

static QDict*
qcow2_extract_crypto_opts(QemuOpts *opts, const char *fmt, Error **errp)
{
    QDict *cryptoopts_qdict;
    QDict *opts_qdict;

    /* Extract "encrypt." options into a qdict */
    opts_qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_extract_subqdict(opts_qdict, &cryptoopts_qdict, "encrypt.");
    qobject_unref(opts_qdict);
    qdict_put_str(cryptoopts_qdict, "format", fmt);
    return cryptoopts_qdict;
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
                                 int flags, bool *need_update_header,
                                 Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCowExtension ext;
    uint64_t offset;
    int ret;
    Qcow2BitmapHeaderExt bitmaps_ext;

    if (need_update_header != NULL) {
        *need_update_header = false;
    }

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

        ret = bdrv_pread(bs->file, offset, &ext, sizeof(ext));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "qcow2_read_extension: ERROR: "
                             "pread fail from offset %" PRIu64, offset);
            return 1;
        }
        ext.magic = be32_to_cpu(ext.magic);
        ext.len = be32_to_cpu(ext.len);
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
            ret = bdrv_pread(bs->file, offset, bs->backing_format, ext.len);
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
                ret = bdrv_pread(bs->file, offset , feature_table, ext.len);
                if (ret < 0) {
                    error_setg_errno(errp, -ret, "ERROR: ext_feature_table: "
                                     "Could not read table");
                    return ret;
                }

                *p_feature_table = feature_table;
            }
            break;

        case QCOW2_EXT_MAGIC_CRYPTO_HEADER: {
            unsigned int cflags = 0;
            if (s->crypt_method_header != QCOW_CRYPT_LUKS) {
                error_setg(errp, "CRYPTO header extension only "
                           "expected with LUKS encryption method");
                return -EINVAL;
            }
            if (ext.len != sizeof(Qcow2CryptoHeaderExtension)) {
                error_setg(errp, "CRYPTO header extension size %u, "
                           "but expected size %zu", ext.len,
                           sizeof(Qcow2CryptoHeaderExtension));
                return -EINVAL;
            }

            ret = bdrv_pread(bs->file, offset, &s->crypto_header, ext.len);
            if (ret < 0) {
                error_setg_errno(errp, -ret,
                                 "Unable to read CRYPTO header extension");
                return ret;
            }
            s->crypto_header.offset = be64_to_cpu(s->crypto_header.offset);
            s->crypto_header.length = be64_to_cpu(s->crypto_header.length);

            if ((s->crypto_header.offset % s->cluster_size) != 0) {
                error_setg(errp, "Encryption header offset '%" PRIu64 "' is "
                           "not a multiple of cluster size '%u'",
                           s->crypto_header.offset, s->cluster_size);
                return -EINVAL;
            }

            if (flags & BDRV_O_NO_IO) {
                cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
            }
            s->crypto = qcrypto_block_open(s->crypto_opts, "encrypt.",
                                           qcow2_crypto_hdr_read_func,
                                           bs, cflags, QCOW2_MAX_THREADS, errp);
            if (!s->crypto) {
                return -EINVAL;
            }
        }   break;

        case QCOW2_EXT_MAGIC_BITMAPS:
            if (ext.len != sizeof(bitmaps_ext)) {
                error_setg_errno(errp, -ret, "bitmaps_ext: "
                                 "Invalid extension length");
                return -EINVAL;
            }

            if (!(s->autoclear_features & QCOW2_AUTOCLEAR_BITMAPS)) {
                if (s->qcow_version < 3) {
                    /* Let's be a bit more specific */
                    warn_report("This qcow2 v2 image contains bitmaps, but "
                                "they may have been modified by a program "
                                "without persistent bitmap support; so now "
                                "they must all be considered inconsistent");
                } else {
                    warn_report("a program lacking bitmap support "
                                "modified this file, so all bitmaps are now "
                                "considered inconsistent");
                }
                error_printf("Some clusters may be leaked, "
                             "run 'qemu-img check -r' on the image "
                             "file to fix.");
                if (need_update_header != NULL) {
                    /* Updating is needed to drop invalid bitmap extension. */
                    *need_update_header = true;
                }
                break;
            }

            ret = bdrv_pread(bs->file, offset, &bitmaps_ext, ext.len);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "bitmaps_ext: "
                                 "Could not read ext header");
                return ret;
            }

            if (bitmaps_ext.reserved32 != 0) {
                error_setg_errno(errp, -ret, "bitmaps_ext: "
                                 "Reserved field is not zero");
                return -EINVAL;
            }

            bitmaps_ext.nb_bitmaps = be32_to_cpu(bitmaps_ext.nb_bitmaps);
            bitmaps_ext.bitmap_directory_size =
                be64_to_cpu(bitmaps_ext.bitmap_directory_size);
            bitmaps_ext.bitmap_directory_offset =
                be64_to_cpu(bitmaps_ext.bitmap_directory_offset);

            if (bitmaps_ext.nb_bitmaps > QCOW2_MAX_BITMAPS) {
                error_setg(errp,
                           "bitmaps_ext: Image has %" PRIu32 " bitmaps, "
                           "exceeding the QEMU supported maximum of %d",
                           bitmaps_ext.nb_bitmaps, QCOW2_MAX_BITMAPS);
                return -EINVAL;
            }

            if (bitmaps_ext.nb_bitmaps == 0) {
                error_setg(errp, "found bitmaps extension with zero bitmaps");
                return -EINVAL;
            }

            if (offset_into_cluster(s, bitmaps_ext.bitmap_directory_offset)) {
                error_setg(errp, "bitmaps_ext: "
                                 "invalid bitmap directory offset");
                return -EINVAL;
            }

            if (bitmaps_ext.bitmap_directory_size >
                QCOW2_MAX_BITMAP_DIRECTORY_SIZE) {
                error_setg(errp, "bitmaps_ext: "
                                 "bitmap directory size (%" PRIu64 ") exceeds "
                                 "the maximum supported size (%d)",
                                 bitmaps_ext.bitmap_directory_size,
                                 QCOW2_MAX_BITMAP_DIRECTORY_SIZE);
                return -EINVAL;
            }

            s->nb_bitmaps = bitmaps_ext.nb_bitmaps;
            s->bitmap_directory_offset =
                    bitmaps_ext.bitmap_directory_offset;
            s->bitmap_directory_size =
                    bitmaps_ext.bitmap_directory_size;

#ifdef DEBUG_EXT
            printf("Qcow2: Got bitmaps extension: "
                   "offset=%" PRIu64 " nb_bitmaps=%" PRIu32 "\n",
                   s->bitmap_directory_offset, s->nb_bitmaps);
#endif
            break;

        case QCOW2_EXT_MAGIC_DATA_FILE:
        {
            s->image_data_file = g_malloc0(ext.len + 1);
            ret = bdrv_pread(bs->file, offset, s->image_data_file, ext.len);
            if (ret < 0) {
                error_setg_errno(errp, -ret,
                                 "ERROR: Could not read data file name");
                return ret;
            }
#ifdef DEBUG_EXT
            printf("Qcow2: Got external data file %s\n", s->image_data_file);
#endif
            break;
        }

        default:
            /* unknown magic - save it in case we need to rewrite the header */
            /* If you add a new feature, make sure to also update the fast
             * path of qcow2_make_empty() to deal with it. */
            {
                Qcow2UnknownHeaderExtension *uext;

                uext = g_malloc0(sizeof(*uext)  + ext.len);
                uext->magic = ext.magic;
                uext->len = ext.len;
                QLIST_INSERT_HEAD(&s->unknown_header_ext, uext, next);

                ret = bdrv_pread(bs->file, offset , uext->data, uext->len);
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
    g_autoptr(GString) features = g_string_sized_new(60);

    while (table && table->name[0] != '\0') {
        if (table->type == QCOW2_FEAT_TYPE_INCOMPATIBLE) {
            if (mask & (1ULL << table->bit)) {
                if (features->len > 0) {
                    g_string_append(features, ", ");
                }
                g_string_append_printf(features, "%.46s", table->name);
                mask &= ~(1ULL << table->bit);
            }
        }
        table++;
    }

    if (mask) {
        if (features->len > 0) {
            g_string_append(features, ", ");
        }
        g_string_append_printf(features,
                               "Unknown incompatible feature: %" PRIx64, mask);
    }

    error_setg(errp, "Unsupported qcow2 feature(s): %s", features->str);
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
    ret = bdrv_pwrite(bs->file, offsetof(QCowHeader, incompatible_features),
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

        ret = qcow2_flush_caches(bs);
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
        int ret = qcow2_flush_caches(bs);
        if (ret < 0) {
            return ret;
        }

        s->incompatible_features &= ~QCOW2_INCOMPAT_CORRUPT;
        return qcow2_update_header(bs);
    }
    return 0;
}

static void qcow2_add_check_result(BdrvCheckResult *out,
                                   const BdrvCheckResult *src,
                                   bool set_allocation_info)
{
    out->corruptions += src->corruptions;
    out->leaks += src->leaks;
    out->check_errors += src->check_errors;
    out->corruptions_fixed += src->corruptions_fixed;
    out->leaks_fixed += src->leaks_fixed;

    if (set_allocation_info) {
        out->image_end_offset = src->image_end_offset;
        out->bfi = src->bfi;
    }
}

static int coroutine_fn qcow2_co_check_locked(BlockDriverState *bs,
                                              BdrvCheckResult *result,
                                              BdrvCheckMode fix)
{
    BdrvCheckResult snapshot_res = {};
    BdrvCheckResult refcount_res = {};
    int ret;

    memset(result, 0, sizeof(*result));

    ret = qcow2_check_read_snapshot_table(bs, &snapshot_res, fix);
    if (ret < 0) {
        qcow2_add_check_result(result, &snapshot_res, false);
        return ret;
    }

    ret = qcow2_check_refcounts(bs, &refcount_res, fix);
    qcow2_add_check_result(result, &refcount_res, true);
    if (ret < 0) {
        qcow2_add_check_result(result, &snapshot_res, false);
        return ret;
    }

    ret = qcow2_check_fix_snapshot_table(bs, &snapshot_res, fix);
    qcow2_add_check_result(result, &snapshot_res, false);
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

static int coroutine_fn qcow2_co_check(BlockDriverState *bs,
                                       BdrvCheckResult *result,
                                       BdrvCheckMode fix)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_co_check_locked(bs, result, fix);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

int qcow2_validate_table(BlockDriverState *bs, uint64_t offset,
                         uint64_t entries, size_t entry_len,
                         int64_t max_size_bytes, const char *table_name,
                         Error **errp)
{
    BDRVQcow2State *s = bs->opaque;

    if (entries > max_size_bytes / entry_len) {
        error_setg(errp, "%s too large", table_name);
        return -EFBIG;
    }

    /* Use signed INT64_MAX as the maximum even for uint64_t header fields,
     * because values will be passed to qemu functions taking int64_t. */
    if ((INT64_MAX - entries * entry_len < offset) ||
        (offset_into_cluster(s, offset) != 0)) {
        error_setg(errp, "%s offset invalid", table_name);
        return -EINVAL;
    }

    return 0;
}

static const char *const mutable_opts[] = {
    QCOW2_OPT_LAZY_REFCOUNTS,
    QCOW2_OPT_DISCARD_REQUEST,
    QCOW2_OPT_DISCARD_SNAPSHOT,
    QCOW2_OPT_DISCARD_OTHER,
    QCOW2_OPT_OVERLAP,
    QCOW2_OPT_OVERLAP_TEMPLATE,
    QCOW2_OPT_OVERLAP_MAIN_HEADER,
    QCOW2_OPT_OVERLAP_ACTIVE_L1,
    QCOW2_OPT_OVERLAP_ACTIVE_L2,
    QCOW2_OPT_OVERLAP_REFCOUNT_TABLE,
    QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK,
    QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE,
    QCOW2_OPT_OVERLAP_INACTIVE_L1,
    QCOW2_OPT_OVERLAP_INACTIVE_L2,
    QCOW2_OPT_OVERLAP_BITMAP_DIRECTORY,
    QCOW2_OPT_CACHE_SIZE,
    QCOW2_OPT_L2_CACHE_SIZE,
    QCOW2_OPT_L2_CACHE_ENTRY_SIZE,
    QCOW2_OPT_REFCOUNT_CACHE_SIZE,
    QCOW2_OPT_CACHE_CLEAN_INTERVAL,
    NULL
};

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
            .name = QCOW2_OPT_OVERLAP_BITMAP_DIRECTORY,
            .type = QEMU_OPT_BOOL,
            .help = "Check for unintended writes into the bitmap directory",
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
            .name = QCOW2_OPT_L2_CACHE_ENTRY_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Size of each entry in the L2 cache",
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
        BLOCK_CRYPTO_OPT_DEF_KEY_SECRET("encrypt.",
            "ID of secret providing qcow2 AES key or LUKS passphrase"),
        { /* end of list */ }
    },
};

static const char *overlap_bool_option_names[QCOW2_OL_MAX_BITNR] = {
    [QCOW2_OL_MAIN_HEADER_BITNR]      = QCOW2_OPT_OVERLAP_MAIN_HEADER,
    [QCOW2_OL_ACTIVE_L1_BITNR]        = QCOW2_OPT_OVERLAP_ACTIVE_L1,
    [QCOW2_OL_ACTIVE_L2_BITNR]        = QCOW2_OPT_OVERLAP_ACTIVE_L2,
    [QCOW2_OL_REFCOUNT_TABLE_BITNR]   = QCOW2_OPT_OVERLAP_REFCOUNT_TABLE,
    [QCOW2_OL_REFCOUNT_BLOCK_BITNR]   = QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK,
    [QCOW2_OL_SNAPSHOT_TABLE_BITNR]   = QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE,
    [QCOW2_OL_INACTIVE_L1_BITNR]      = QCOW2_OPT_OVERLAP_INACTIVE_L1,
    [QCOW2_OL_INACTIVE_L2_BITNR]      = QCOW2_OPT_OVERLAP_INACTIVE_L2,
    [QCOW2_OL_BITMAP_DIRECTORY_BITNR] = QCOW2_OPT_OVERLAP_BITMAP_DIRECTORY,
};

static void cache_clean_timer_cb(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVQcow2State *s = bs->opaque;
    qcow2_cache_clean_unused(s->l2_table_cache);
    qcow2_cache_clean_unused(s->refcount_block_cache);
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
                             uint64_t *l2_cache_entry_size,
                             uint64_t *refcount_cache_size, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t combined_cache_size, l2_cache_max_setting;
    bool l2_cache_size_set, refcount_cache_size_set, combined_cache_size_set;
    bool l2_cache_entry_size_set;
    int min_refcount_cache = MIN_REFCOUNT_CACHE_SIZE * s->cluster_size;
    uint64_t virtual_disk_size = bs->total_sectors * BDRV_SECTOR_SIZE;
    uint64_t max_l2_entries = DIV_ROUND_UP(virtual_disk_size, s->cluster_size);
    /* An L2 table is always one cluster in size so the max cache size
     * should be a multiple of the cluster size. */
    uint64_t max_l2_cache = ROUND_UP(max_l2_entries * sizeof(uint64_t),
                                     s->cluster_size);

    combined_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_CACHE_SIZE);
    l2_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_L2_CACHE_SIZE);
    refcount_cache_size_set = qemu_opt_get(opts, QCOW2_OPT_REFCOUNT_CACHE_SIZE);
    l2_cache_entry_size_set = qemu_opt_get(opts, QCOW2_OPT_L2_CACHE_ENTRY_SIZE);

    combined_cache_size = qemu_opt_get_size(opts, QCOW2_OPT_CACHE_SIZE, 0);
    l2_cache_max_setting = qemu_opt_get_size(opts, QCOW2_OPT_L2_CACHE_SIZE,
                                             DEFAULT_L2_CACHE_MAX_SIZE);
    *refcount_cache_size = qemu_opt_get_size(opts,
                                             QCOW2_OPT_REFCOUNT_CACHE_SIZE, 0);

    *l2_cache_entry_size = qemu_opt_get_size(
        opts, QCOW2_OPT_L2_CACHE_ENTRY_SIZE, s->cluster_size);

    *l2_cache_size = MIN(max_l2_cache, l2_cache_max_setting);

    if (combined_cache_size_set) {
        if (l2_cache_size_set && refcount_cache_size_set) {
            error_setg(errp, QCOW2_OPT_CACHE_SIZE ", " QCOW2_OPT_L2_CACHE_SIZE
                       " and " QCOW2_OPT_REFCOUNT_CACHE_SIZE " may not be set "
                       "at the same time");
            return;
        } else if (l2_cache_size_set &&
                   (l2_cache_max_setting > combined_cache_size)) {
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
            /* Assign as much memory as possible to the L2 cache, and
             * use the remainder for the refcount cache */
            if (combined_cache_size >= max_l2_cache + min_refcount_cache) {
                *l2_cache_size = max_l2_cache;
                *refcount_cache_size = combined_cache_size - *l2_cache_size;
            } else {
                *refcount_cache_size =
                    MIN(combined_cache_size, min_refcount_cache);
                *l2_cache_size = combined_cache_size - *refcount_cache_size;
            }
        }
    }

    /*
     * If the L2 cache is not enough to cover the whole disk then
     * default to 4KB entries. Smaller entries reduce the cost of
     * loads and evictions and increase I/O performance.
     */
    if (*l2_cache_size < max_l2_cache && !l2_cache_entry_size_set) {
        *l2_cache_entry_size = MIN(s->cluster_size, 4096);
    }

    /* l2_cache_size and refcount_cache_size are ensured to have at least
     * their minimum values in qcow2_update_options_prepare() */

    if (*l2_cache_entry_size < (1 << MIN_CLUSTER_BITS) ||
        *l2_cache_entry_size > s->cluster_size ||
        !is_power_of_2(*l2_cache_entry_size)) {
        error_setg(errp, "L2 cache entry size must be a power of two "
                   "between %d and the cluster size (%d)",
                   1 << MIN_CLUSTER_BITS, s->cluster_size);
        return;
    }
}

typedef struct Qcow2ReopenState {
    Qcow2Cache *l2_table_cache;
    Qcow2Cache *refcount_block_cache;
    int l2_slice_size; /* Number of entries in a slice of the L2 table */
    bool use_lazy_refcounts;
    int overlap_check;
    bool discard_passthrough[QCOW2_DISCARD_MAX];
    uint64_t cache_clean_interval;
    QCryptoBlockOpenOptions *crypto_opts; /* Disk encryption runtime options */
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
    uint64_t l2_cache_size, l2_cache_entry_size, refcount_cache_size;
    int i;
    const char *encryptfmt;
    QDict *encryptopts = NULL;
    Error *local_err = NULL;
    int ret;

    qdict_extract_subqdict(options, &encryptopts, "encrypt.");
    encryptfmt = qdict_get_try_str(encryptopts, "format");

    opts = qemu_opts_create(&qcow2_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* get L2 table/refcount block cache size from command line options */
    read_cache_sizes(bs, opts, &l2_cache_size, &l2_cache_entry_size,
                     &refcount_cache_size, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    l2_cache_size /= l2_cache_entry_size;
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

    r->l2_slice_size = l2_cache_entry_size / sizeof(uint64_t);
    r->l2_table_cache = qcow2_cache_create(bs, l2_cache_size,
                                           l2_cache_entry_size);
    r->refcount_block_cache = qcow2_cache_create(bs, refcount_cache_size,
                                                 s->cluster_size);
    if (r->l2_table_cache == NULL || r->refcount_block_cache == NULL) {
        error_setg(errp, "Could not allocate metadata caches");
        ret = -ENOMEM;
        goto fail;
    }

    /* New interval for cache cleanup timer */
    r->cache_clean_interval =
        qemu_opt_get_number(opts, QCOW2_OPT_CACHE_CLEAN_INTERVAL,
                            DEFAULT_CACHE_CLEAN_INTERVAL);
#ifndef CONFIG_LINUX
    if (r->cache_clean_interval != 0) {
        error_setg(errp, QCOW2_OPT_CACHE_CLEAN_INTERVAL
                   " not supported on this host");
        ret = -EINVAL;
        goto fail;
    }
#endif
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

    switch (s->crypt_method_header) {
    case QCOW_CRYPT_NONE:
        if (encryptfmt) {
            error_setg(errp, "No encryption in image header, but options "
                       "specified format '%s'", encryptfmt);
            ret = -EINVAL;
            goto fail;
        }
        break;

    case QCOW_CRYPT_AES:
        if (encryptfmt && !g_str_equal(encryptfmt, "aes")) {
            error_setg(errp,
                       "Header reported 'aes' encryption format but "
                       "options specify '%s'", encryptfmt);
            ret = -EINVAL;
            goto fail;
        }
        qdict_put_str(encryptopts, "format", "qcow");
        r->crypto_opts = block_crypto_open_opts_init(encryptopts, errp);
        break;

    case QCOW_CRYPT_LUKS:
        if (encryptfmt && !g_str_equal(encryptfmt, "luks")) {
            error_setg(errp,
                       "Header reported 'luks' encryption format but "
                       "options specify '%s'", encryptfmt);
            ret = -EINVAL;
            goto fail;
        }
        qdict_put_str(encryptopts, "format", "luks");
        r->crypto_opts = block_crypto_open_opts_init(encryptopts, errp);
        break;

    default:
        error_setg(errp, "Unsupported encryption method %d",
                   s->crypt_method_header);
        break;
    }
    if (s->crypt_method_header != QCOW_CRYPT_NONE && !r->crypto_opts) {
        ret = -EINVAL;
        goto fail;
    }

    ret = 0;
fail:
    qobject_unref(encryptopts);
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
        qcow2_cache_destroy(s->l2_table_cache);
    }
    if (s->refcount_block_cache) {
        qcow2_cache_destroy(s->refcount_block_cache);
    }
    s->l2_table_cache = r->l2_table_cache;
    s->refcount_block_cache = r->refcount_block_cache;
    s->l2_slice_size = r->l2_slice_size;

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

    qapi_free_QCryptoBlockOpenOptions(s->crypto_opts);
    s->crypto_opts = r->crypto_opts;
}

static void qcow2_update_options_abort(BlockDriverState *bs,
                                       Qcow2ReopenState *r)
{
    if (r->l2_table_cache) {
        qcow2_cache_destroy(r->l2_table_cache);
    }
    if (r->refcount_block_cache) {
        qcow2_cache_destroy(r->refcount_block_cache);
    }
    qapi_free_QCryptoBlockOpenOptions(r->crypto_opts);
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

static int validate_compression_type(BDRVQcow2State *s, Error **errp)
{
    switch (s->compression_type) {
    case QCOW2_COMPRESSION_TYPE_ZLIB:
#ifdef CONFIG_ZSTD
    case QCOW2_COMPRESSION_TYPE_ZSTD:
#endif
        break;

    default:
        error_setg(errp, "qcow2: unknown compression type: %u",
                   s->compression_type);
        return -ENOTSUP;
    }

    /*
     * if the compression type differs from QCOW2_COMPRESSION_TYPE_ZLIB
     * the incompatible feature flag must be set
     */
    if (s->compression_type == QCOW2_COMPRESSION_TYPE_ZLIB) {
        if (s->incompatible_features & QCOW2_INCOMPAT_COMPRESSION) {
            error_setg(errp, "qcow2: Compression type incompatible feature "
                             "bit must not be set");
            return -EINVAL;
        }
    } else {
        if (!(s->incompatible_features & QCOW2_INCOMPAT_COMPRESSION)) {
            error_setg(errp, "qcow2: Compression type incompatible feature "
                             "bit must be set");
            return -EINVAL;
        }
    }

    return 0;
}

/* Called with s->lock held.  */
static int coroutine_fn qcow2_do_open(BlockDriverState *bs, QDict *options,
                                      int flags, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int len, i;
    int ret = 0;
    QCowHeader header;
    Error *local_err = NULL;
    uint64_t ext_end;
    uint64_t l1_vm_state_index;
    bool update_header = false;

    ret = bdrv_pread(bs->file, 0, &header, sizeof(header));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read qcow2 header");
        goto fail;
    }
    header.magic = be32_to_cpu(header.magic);
    header.version = be32_to_cpu(header.version);
    header.backing_file_offset = be64_to_cpu(header.backing_file_offset);
    header.backing_file_size = be32_to_cpu(header.backing_file_size);
    header.size = be64_to_cpu(header.size);
    header.cluster_bits = be32_to_cpu(header.cluster_bits);
    header.crypt_method = be32_to_cpu(header.crypt_method);
    header.l1_table_offset = be64_to_cpu(header.l1_table_offset);
    header.l1_size = be32_to_cpu(header.l1_size);
    header.refcount_table_offset = be64_to_cpu(header.refcount_table_offset);
    header.refcount_table_clusters =
        be32_to_cpu(header.refcount_table_clusters);
    header.snapshots_offset = be64_to_cpu(header.snapshots_offset);
    header.nb_snapshots = be32_to_cpu(header.nb_snapshots);

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

    /* Initialise version 3 header fields */
    if (header.version == 2) {
        header.incompatible_features    = 0;
        header.compatible_features      = 0;
        header.autoclear_features       = 0;
        header.refcount_order           = 4;
        header.header_length            = 72;
    } else {
        header.incompatible_features =
            be64_to_cpu(header.incompatible_features);
        header.compatible_features = be64_to_cpu(header.compatible_features);
        header.autoclear_features = be64_to_cpu(header.autoclear_features);
        header.refcount_order = be32_to_cpu(header.refcount_order);
        header.header_length = be32_to_cpu(header.header_length);

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
        ret = bdrv_pread(bs->file, sizeof(header), s->unknown_header_fields,
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

    /*
     * Handle compression type
     * Older qcow2 images don't contain the compression type header.
     * Distinguish them by the header length and use
     * the only valid (default) compression type in that case
     */
    if (header.header_length > offsetof(QCowHeader, compression_type)) {
        s->compression_type = header.compression_type;
    } else {
        s->compression_type = QCOW2_COMPRESSION_TYPE_ZLIB;
    }

    ret = validate_compression_type(s, errp);
    if (ret) {
        goto fail;
    }

    if (s->incompatible_features & ~QCOW2_INCOMPAT_MASK) {
        void *feature_table = NULL;
        qcow2_read_extensions(bs, header.header_length, ext_end,
                              &feature_table, flags, NULL, NULL);
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

    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header) {
        if (bdrv_uses_whitelist() &&
            s->crypt_method_header == QCOW_CRYPT_AES) {
            error_setg(errp,
                       "Use of AES-CBC encrypted qcow2 images is no longer "
                       "supported in system emulators");
            error_append_hint(errp,
                              "You can use 'qemu-img convert' to convert your "
                              "image to an alternative supported format, such "
                              "as unencrypted qcow2, or raw with the LUKS "
                              "format instead.\n");
            ret = -ENOSYS;
            goto fail;
        }

        if (s->crypt_method_header == QCOW_CRYPT_AES) {
            s->crypt_physical_offset = false;
        } else {
            /* Assuming LUKS and any future crypt methods we
             * add will all use physical offsets, due to the
             * fact that the alternative is insecure...  */
            s->crypt_physical_offset = true;
        }

        bs->encrypted = true;
    }

    s->l2_bits = s->cluster_bits - 3; /* L2 is always one cluster */
    s->l2_size = 1 << s->l2_bits;
    /* 2^(s->refcount_order - 3) is the refcount width in bytes */
    s->refcount_block_bits = s->cluster_bits - (s->refcount_order - 3);
    s->refcount_block_size = 1 << s->refcount_block_bits;
    bs->total_sectors = header.size / BDRV_SECTOR_SIZE;
    s->csize_shift = (62 - (s->cluster_bits - 8));
    s->csize_mask = (1 << (s->cluster_bits - 8)) - 1;
    s->cluster_offset_mask = (1LL << s->csize_shift) - 1;

    s->refcount_table_offset = header.refcount_table_offset;
    s->refcount_table_size =
        header.refcount_table_clusters << (s->cluster_bits - 3);

    if (header.refcount_table_clusters == 0 && !(flags & BDRV_O_CHECK)) {
        error_setg(errp, "Image does not contain a reference count table");
        ret = -EINVAL;
        goto fail;
    }

    ret = qcow2_validate_table(bs, s->refcount_table_offset,
                               header.refcount_table_clusters,
                               s->cluster_size, QCOW_MAX_REFTABLE_SIZE,
                               "Reference count table", errp);
    if (ret < 0) {
        goto fail;
    }

    if (!(flags & BDRV_O_CHECK)) {
        /*
         * The total size in bytes of the snapshot table is checked in
         * qcow2_read_snapshots() because the size of each snapshot is
         * variable and we don't know it yet.
         * Here we only check the offset and number of snapshots.
         */
        ret = qcow2_validate_table(bs, header.snapshots_offset,
                                   header.nb_snapshots,
                                   sizeof(QCowSnapshotHeader),
                                   sizeof(QCowSnapshotHeader) *
                                       QCOW_MAX_SNAPSHOTS,
                                   "Snapshot table", errp);
        if (ret < 0) {
            goto fail;
        }
    }

    /* read the level 1 table */
    ret = qcow2_validate_table(bs, header.l1_table_offset,
                               header.l1_size, sizeof(uint64_t),
                               QCOW_MAX_L1_SIZE, "Active L1 table", errp);
    if (ret < 0) {
        goto fail;
    }
    s->l1_size = header.l1_size;
    s->l1_table_offset = header.l1_table_offset;

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

    if (s->l1_size > 0) {
        s->l1_table = qemu_try_blockalign(bs->file->bs,
                                          s->l1_size * sizeof(uint64_t));
        if (s->l1_table == NULL) {
            error_setg(errp, "Could not allocate L1 table");
            ret = -ENOMEM;
            goto fail;
        }
        ret = bdrv_pread(bs->file, s->l1_table_offset, s->l1_table,
                         s->l1_size * sizeof(uint64_t));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not read L1 table");
            goto fail;
        }
        for(i = 0;i < s->l1_size; i++) {
            s->l1_table[i] = be64_to_cpu(s->l1_table[i]);
        }
    }

    /* Parse driver-specific options */
    ret = qcow2_update_options(bs, options, flags, errp);
    if (ret < 0) {
        goto fail;
    }

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
                              flags, &update_header, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Open external data file */
    s->data_file = bdrv_open_child(NULL, options, "data-file", bs,
                                   &child_of_bds, BDRV_CHILD_DATA,
                                   true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    if (s->incompatible_features & QCOW2_INCOMPAT_DATA_FILE) {
        if (!s->data_file && s->image_data_file) {
            s->data_file = bdrv_open_child(s->image_data_file, options,
                                           "data-file", bs, &child_of_bds,
                                           BDRV_CHILD_DATA, false, errp);
            if (!s->data_file) {
                ret = -EINVAL;
                goto fail;
            }
        }
        if (!s->data_file) {
            error_setg(errp, "'data-file' is required for this image");
            ret = -EINVAL;
            goto fail;
        }

        /* No data here */
        bs->file->role &= ~BDRV_CHILD_DATA;

        /* Must succeed because we have given up permissions if anything */
        bdrv_child_refresh_perms(bs, bs->file, &error_abort);
    } else {
        if (s->data_file) {
            error_setg(errp, "'data-file' can only be set for images with an "
                             "external data file");
            ret = -EINVAL;
            goto fail;
        }

        s->data_file = bs->file;

        if (data_file_is_raw(bs)) {
            error_setg(errp, "data-file-raw requires a data file");
            ret = -EINVAL;
            goto fail;
        }
    }

    /* qcow2_read_extension may have set up the crypto context
     * if the crypt method needs a header region, some methods
     * don't need header extensions, so must check here
     */
    if (s->crypt_method_header && !s->crypto) {
        if (s->crypt_method_header == QCOW_CRYPT_AES) {
            unsigned int cflags = 0;
            if (flags & BDRV_O_NO_IO) {
                cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
            }
            s->crypto = qcrypto_block_open(s->crypto_opts, "encrypt.",
                                           NULL, NULL, cflags,
                                           QCOW2_MAX_THREADS, errp);
            if (!s->crypto) {
                ret = -EINVAL;
                goto fail;
            }
        } else if (!(flags & BDRV_O_NO_IO)) {
            error_setg(errp, "Missing CRYPTO header for crypt method %d",
                       s->crypt_method_header);
            ret = -EINVAL;
            goto fail;
        }
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
        ret = bdrv_pread(bs->file, header.backing_file_offset,
                         bs->auto_backing_file, len);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not read backing file name");
            goto fail;
        }
        bs->auto_backing_file[len] = '\0';
        pstrcpy(bs->backing_file, sizeof(bs->backing_file),
                bs->auto_backing_file);
        s->image_backing_file = g_strdup(bs->auto_backing_file);
    }

    /*
     * Internal snapshots; skip reading them in check mode, because
     * we do not need them then, and we do not want to abort because
     * of a broken table.
     */
    if (!(flags & BDRV_O_CHECK)) {
        s->snapshots_offset = header.snapshots_offset;
        s->nb_snapshots = header.nb_snapshots;

        ret = qcow2_read_snapshots(bs, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    /* Clear unknown autoclear feature bits */
    update_header |= s->autoclear_features & ~QCOW2_AUTOCLEAR_MASK;
    update_header =
        update_header && !bs->read_only && !(flags & BDRV_O_INACTIVE);
    if (update_header) {
        s->autoclear_features &= QCOW2_AUTOCLEAR_MASK;
    }

    /* == Handle persistent dirty bitmaps ==
     *
     * We want load dirty bitmaps in three cases:
     *
     * 1. Normal open of the disk in active mode, not related to invalidation
     *    after migration.
     *
     * 2. Invalidation of the target vm after pre-copy phase of migration, if
     *    bitmaps are _not_ migrating through migration channel, i.e.
     *    'dirty-bitmaps' capability is disabled.
     *
     * 3. Invalidation of source vm after failed or canceled migration.
     *    This is a very interesting case. There are two possible types of
     *    bitmaps:
     *
     *    A. Stored on inactivation and removed. They should be loaded from the
     *       image.
     *
     *    B. Not stored: not-persistent bitmaps and bitmaps, migrated through
     *       the migration channel (with dirty-bitmaps capability).
     *
     *    On the other hand, there are two possible sub-cases:
     *
     *    3.1 disk was changed by somebody else while were inactive. In this
     *        case all in-RAM dirty bitmaps (both persistent and not) are
     *        definitely invalid. And we don't have any method to determine
     *        this.
     *
     *        Simple and safe thing is to just drop all the bitmaps of type B on
     *        inactivation. But in this case we lose bitmaps in valid 4.2 case.
     *
     *        On the other hand, resuming source vm, if disk was already changed
     *        is a bad thing anyway: not only bitmaps, the whole vm state is
     *        out of sync with disk.
     *
     *        This means, that user or management tool, who for some reason
     *        decided to resume source vm, after disk was already changed by
     *        target vm, should at least drop all dirty bitmaps by hand.
     *
     *        So, we can ignore this case for now, but TODO: "generation"
     *        extension for qcow2, to determine, that image was changed after
     *        last inactivation. And if it is changed, we will drop (or at least
     *        mark as 'invalid' all the bitmaps of type B, both persistent
     *        and not).
     *
     *    3.2 disk was _not_ changed while were inactive. Bitmaps may be saved
     *        to disk ('dirty-bitmaps' capability disabled), or not saved
     *        ('dirty-bitmaps' capability enabled), but we don't need to care
     *        of: let's load bitmaps as always: stored bitmaps will be loaded,
     *        and not stored has flag IN_USE=1 in the image and will be skipped
     *        on loading.
     *
     * One remaining possible case when we don't want load bitmaps:
     *
     * 4. Open disk in inactive mode in target vm (bitmaps are migrating or
     *    will be loaded on invalidation, no needs try loading them before)
     */

    if (!(bdrv_get_flags(bs) & BDRV_O_INACTIVE)) {
        /* It's case 1, 2 or 3.2. Or 3.1 which is BUG in management layer. */
        bool header_updated = qcow2_load_dirty_bitmaps(bs, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto fail;
        }

        update_header = update_header && !header_updated;
    }

    if (update_header) {
        ret = qcow2_update_header(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not update qcow2 header");
            goto fail;
        }
    }

    bs->supported_zero_flags = header.version >= 3 ?
                               BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK : 0;
    bs->supported_truncate_flags = BDRV_REQ_ZERO_WRITE;

    /* Repair image if dirty */
    if (!(flags & (BDRV_O_CHECK | BDRV_O_INACTIVE)) && !bs->read_only &&
        (s->incompatible_features & QCOW2_INCOMPAT_DIRTY)) {
        BdrvCheckResult result = {0};

        ret = qcow2_co_check_locked(bs, &result,
                                    BDRV_FIX_ERRORS | BDRV_FIX_LEAKS);
        if (ret < 0 || result.check_errors) {
            if (ret >= 0) {
                ret = -EIO;
            }
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

    qemu_co_queue_init(&s->thread_task_queue);

    return ret;

 fail:
    g_free(s->image_data_file);
    if (has_data_file(bs)) {
        bdrv_unref_child(bs, s->data_file);
        s->data_file = NULL;
    }
    g_free(s->unknown_header_fields);
    cleanup_unknown_header_ext(bs);
    qcow2_free_snapshots(bs);
    qcow2_refcount_close(bs);
    qemu_vfree(s->l1_table);
    /* else pre-write overlap checks in cache_destroy may crash */
    s->l1_table = NULL;
    cache_clean_timer_del(bs);
    if (s->l2_table_cache) {
        qcow2_cache_destroy(s->l2_table_cache);
    }
    if (s->refcount_block_cache) {
        qcow2_cache_destroy(s->refcount_block_cache);
    }
    qcrypto_block_free(s->crypto);
    qapi_free_QCryptoBlockOpenOptions(s->crypto_opts);
    return ret;
}

typedef struct QCow2OpenCo {
    BlockDriverState *bs;
    QDict *options;
    int flags;
    Error **errp;
    int ret;
} QCow2OpenCo;

static void coroutine_fn qcow2_open_entry(void *opaque)
{
    QCow2OpenCo *qoc = opaque;
    BDRVQcow2State *s = qoc->bs->opaque;

    qemu_co_mutex_lock(&s->lock);
    qoc->ret = qcow2_do_open(qoc->bs, qoc->options, qoc->flags, qoc->errp);
    qemu_co_mutex_unlock(&s->lock);
}

static int qcow2_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCow2OpenCo qoc = {
        .bs = bs,
        .options = options,
        .flags = flags,
        .errp = errp,
        .ret = -EINPROGRESS
    };

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_IMAGE, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    /* Initialise locks */
    qemu_co_mutex_init(&s->lock);

    if (qemu_in_coroutine()) {
        /* From bdrv_co_create.  */
        qcow2_open_entry(&qoc);
    } else {
        assert(qemu_get_current_aio_context() == qemu_get_aio_context());
        qemu_coroutine_enter(qemu_coroutine_create(qcow2_open_entry, &qoc));
        BDRV_POLL_WHILE(bs, qoc.ret == -EINPROGRESS);
    }
    return qoc.ret;
}

static void qcow2_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;

    if (bs->encrypted) {
        /* Encryption works on a sector granularity */
        bs->bl.request_alignment = qcrypto_block_get_sector_size(s->crypto);
    }
    bs->bl.pwrite_zeroes_alignment = s->cluster_size;
    bs->bl.pdiscard_alignment = s->cluster_size;
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
        ret = qcow2_reopen_bitmaps_ro(state->bs, errp);
        if (ret < 0) {
            goto fail;
        }

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

static void qcow2_reopen_commit_post(BDRVReopenState *state)
{
    if (state->flags & BDRV_O_RDWR) {
        Error *local_err = NULL;

        if (qcow2_reopen_bitmaps_rw(state->bs, &local_err) < 0) {
            /*
             * This is not fatal, bitmaps just left read-only, so all following
             * writes will fail. User can remove read-only bitmaps to unblock
             * writes or retry reopen.
             */
            error_reportf_err(local_err,
                              "%s: Failed to make dirty bitmaps writable: ",
                              bdrv_get_node_name(state->bs));
        }
    }
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

static int coroutine_fn qcow2_co_block_status(BlockDriverState *bs,
                                              bool want_zero,
                                              int64_t offset, int64_t count,
                                              int64_t *pnum, int64_t *map,
                                              BlockDriverState **file)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t cluster_offset;
    unsigned int bytes;
    int ret, status = 0;

    qemu_co_mutex_lock(&s->lock);

    if (!s->metadata_preallocation_checked) {
        ret = qcow2_detect_metadata_preallocation(bs);
        s->metadata_preallocation = (ret == 1);
        s->metadata_preallocation_checked = true;
    }

    bytes = MIN(INT_MAX, count);
    ret = qcow2_get_cluster_offset(bs, offset, &bytes, &cluster_offset);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        return ret;
    }

    *pnum = bytes;

    if ((ret == QCOW2_CLUSTER_NORMAL || ret == QCOW2_CLUSTER_ZERO_ALLOC) &&
        !s->crypto) {
        *map = cluster_offset | offset_into_cluster(s, offset);
        *file = s->data_file->bs;
        status |= BDRV_BLOCK_OFFSET_VALID;
    }
    if (ret == QCOW2_CLUSTER_ZERO_PLAIN || ret == QCOW2_CLUSTER_ZERO_ALLOC) {
        status |= BDRV_BLOCK_ZERO;
    } else if (ret != QCOW2_CLUSTER_UNALLOCATED) {
        status |= BDRV_BLOCK_DATA;
    }
    if (s->metadata_preallocation && (status & BDRV_BLOCK_DATA) &&
        (status & BDRV_BLOCK_OFFSET_VALID))
    {
        status |= BDRV_BLOCK_RECURSE;
    }
    return status;
}

static coroutine_fn int qcow2_handle_l2meta(BlockDriverState *bs,
                                            QCowL2Meta **pl2meta,
                                            bool link_l2)
{
    int ret = 0;
    QCowL2Meta *l2meta = *pl2meta;

    while (l2meta != NULL) {
        QCowL2Meta *next;

        if (link_l2) {
            ret = qcow2_alloc_cluster_link_l2(bs, l2meta);
            if (ret) {
                goto out;
            }
        } else {
            qcow2_alloc_cluster_abort(bs, l2meta);
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
out:
    *pl2meta = l2meta;
    return ret;
}

static coroutine_fn int
qcow2_co_preadv_encrypted(BlockDriverState *bs,
                           uint64_t file_cluster_offset,
                           uint64_t offset,
                           uint64_t bytes,
                           QEMUIOVector *qiov,
                           uint64_t qiov_offset)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    uint8_t *buf;

    assert(bs->encrypted && s->crypto);
    assert(bytes <= QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);

    /*
     * For encrypted images, read everything into a temporary
     * contiguous buffer on which the AES functions can work.
     * Also, decryption in a separate buffer is better as it
     * prevents the guest from learning information about the
     * encrypted nature of the virtual disk.
     */

    buf = qemu_try_blockalign(s->data_file->bs, bytes);
    if (buf == NULL) {
        return -ENOMEM;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    ret = bdrv_co_pread(s->data_file,
                        file_cluster_offset + offset_into_cluster(s, offset),
                        bytes, buf, 0);
    if (ret < 0) {
        goto fail;
    }

    if (qcow2_co_decrypt(bs,
                         file_cluster_offset + offset_into_cluster(s, offset),
                         offset, buf, bytes) < 0)
    {
        ret = -EIO;
        goto fail;
    }
    qemu_iovec_from_buf(qiov, qiov_offset, buf, bytes);

fail:
    qemu_vfree(buf);

    return ret;
}

typedef struct Qcow2AioTask {
    AioTask task;

    BlockDriverState *bs;
    QCow2ClusterType cluster_type; /* only for read */
    uint64_t file_cluster_offset;
    uint64_t offset;
    uint64_t bytes;
    QEMUIOVector *qiov;
    uint64_t qiov_offset;
    QCowL2Meta *l2meta; /* only for write */
} Qcow2AioTask;

static coroutine_fn int qcow2_co_preadv_task_entry(AioTask *task);
static coroutine_fn int qcow2_add_task(BlockDriverState *bs,
                                       AioTaskPool *pool,
                                       AioTaskFunc func,
                                       QCow2ClusterType cluster_type,
                                       uint64_t file_cluster_offset,
                                       uint64_t offset,
                                       uint64_t bytes,
                                       QEMUIOVector *qiov,
                                       size_t qiov_offset,
                                       QCowL2Meta *l2meta)
{
    Qcow2AioTask local_task;
    Qcow2AioTask *task = pool ? g_new(Qcow2AioTask, 1) : &local_task;

    *task = (Qcow2AioTask) {
        .task.func = func,
        .bs = bs,
        .cluster_type = cluster_type,
        .qiov = qiov,
        .file_cluster_offset = file_cluster_offset,
        .offset = offset,
        .bytes = bytes,
        .qiov_offset = qiov_offset,
        .l2meta = l2meta,
    };

    trace_qcow2_add_task(qemu_coroutine_self(), bs, pool,
                         func == qcow2_co_preadv_task_entry ? "read" : "write",
                         cluster_type, file_cluster_offset, offset, bytes,
                         qiov, qiov_offset);

    if (!pool) {
        return func(&task->task);
    }

    aio_task_pool_start_task(pool, &task->task);

    return 0;
}

static coroutine_fn int qcow2_co_preadv_task(BlockDriverState *bs,
                                             QCow2ClusterType cluster_type,
                                             uint64_t file_cluster_offset,
                                             uint64_t offset, uint64_t bytes,
                                             QEMUIOVector *qiov,
                                             size_t qiov_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int offset_in_cluster = offset_into_cluster(s, offset);

    switch (cluster_type) {
    case QCOW2_CLUSTER_ZERO_PLAIN:
    case QCOW2_CLUSTER_ZERO_ALLOC:
        /* Both zero types are handled in qcow2_co_preadv_part */
        g_assert_not_reached();

    case QCOW2_CLUSTER_UNALLOCATED:
        assert(bs->backing); /* otherwise handled in qcow2_co_preadv_part */

        BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
        return bdrv_co_preadv_part(bs->backing, offset, bytes,
                                   qiov, qiov_offset, 0);

    case QCOW2_CLUSTER_COMPRESSED:
        return qcow2_co_preadv_compressed(bs, file_cluster_offset,
                                          offset, bytes, qiov, qiov_offset);

    case QCOW2_CLUSTER_NORMAL:
        assert(offset_into_cluster(s, file_cluster_offset) == 0);
        if (bs->encrypted) {
            return qcow2_co_preadv_encrypted(bs, file_cluster_offset,
                                             offset, bytes, qiov, qiov_offset);
        }

        BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
        return bdrv_co_preadv_part(s->data_file,
                                   file_cluster_offset + offset_in_cluster,
                                   bytes, qiov, qiov_offset, 0);

    default:
        g_assert_not_reached();
    }

    g_assert_not_reached();
}

static coroutine_fn int qcow2_co_preadv_task_entry(AioTask *task)
{
    Qcow2AioTask *t = container_of(task, Qcow2AioTask, task);

    assert(!t->l2meta);

    return qcow2_co_preadv_task(t->bs, t->cluster_type, t->file_cluster_offset,
                                t->offset, t->bytes, t->qiov, t->qiov_offset);
}

static coroutine_fn int qcow2_co_preadv_part(BlockDriverState *bs,
                                             uint64_t offset, uint64_t bytes,
                                             QEMUIOVector *qiov,
                                             size_t qiov_offset, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    int ret = 0;
    unsigned int cur_bytes; /* number of bytes in current iteration */
    uint64_t cluster_offset = 0;
    AioTaskPool *aio = NULL;

    while (bytes != 0 && aio_task_pool_status(aio) == 0) {
        /* prepare next request */
        cur_bytes = MIN(bytes, INT_MAX);
        if (s->crypto) {
            cur_bytes = MIN(cur_bytes,
                            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
        }

        qemu_co_mutex_lock(&s->lock);
        ret = qcow2_get_cluster_offset(bs, offset, &cur_bytes, &cluster_offset);
        qemu_co_mutex_unlock(&s->lock);
        if (ret < 0) {
            goto out;
        }

        if (ret == QCOW2_CLUSTER_ZERO_PLAIN ||
            ret == QCOW2_CLUSTER_ZERO_ALLOC ||
            (ret == QCOW2_CLUSTER_UNALLOCATED && !bs->backing))
        {
            qemu_iovec_memset(qiov, qiov_offset, 0, cur_bytes);
        } else {
            if (!aio && cur_bytes != bytes) {
                aio = aio_task_pool_new(QCOW2_MAX_WORKERS);
            }
            ret = qcow2_add_task(bs, aio, qcow2_co_preadv_task_entry, ret,
                                 cluster_offset, offset, cur_bytes,
                                 qiov, qiov_offset, NULL);
            if (ret < 0) {
                goto out;
            }
        }

        bytes -= cur_bytes;
        offset += cur_bytes;
        qiov_offset += cur_bytes;
    }

out:
    if (aio) {
        aio_task_pool_wait_all(aio);
        if (ret == 0) {
            ret = aio_task_pool_status(aio);
        }
        g_free(aio);
    }

    return ret;
}

/* Check if it's possible to merge a write request with the writing of
 * the data from the COW regions */
static bool merge_cow(uint64_t offset, unsigned bytes,
                      QEMUIOVector *qiov, size_t qiov_offset,
                      QCowL2Meta *l2meta)
{
    QCowL2Meta *m;

    for (m = l2meta; m != NULL; m = m->next) {
        /* If both COW regions are empty then there's nothing to merge */
        if (m->cow_start.nb_bytes == 0 && m->cow_end.nb_bytes == 0) {
            continue;
        }

        /* If COW regions are handled already, skip this too */
        if (m->skip_cow) {
            continue;
        }

        /* The data (middle) region must be immediately after the
         * start region */
        if (l2meta_cow_start(m) + m->cow_start.nb_bytes != offset) {
            continue;
        }

        /* The end region must be immediately after the data (middle)
         * region */
        if (m->offset + m->cow_end.offset != offset + bytes) {
            continue;
        }

        /* Make sure that adding both COW regions to the QEMUIOVector
         * does not exceed IOV_MAX */
        if (qemu_iovec_subvec_niov(qiov, qiov_offset, bytes) > IOV_MAX - 2) {
            continue;
        }

        m->data_qiov = qiov;
        m->data_qiov_offset = qiov_offset;
        return true;
    }

    return false;
}

static bool is_unallocated(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    int64_t nr;
    return !bytes ||
        (!bdrv_is_allocated_above(bs, NULL, false, offset, bytes, &nr) &&
         nr == bytes);
}

static bool is_zero_cow(BlockDriverState *bs, QCowL2Meta *m)
{
    /*
     * This check is designed for optimization shortcut so it must be
     * efficient.
     * Instead of is_zero(), use is_unallocated() as it is faster (but not
     * as accurate and can result in false negatives).
     */
    return is_unallocated(bs, m->offset + m->cow_start.offset,
                          m->cow_start.nb_bytes) &&
           is_unallocated(bs, m->offset + m->cow_end.offset,
                          m->cow_end.nb_bytes);
}

static int handle_alloc_space(BlockDriverState *bs, QCowL2Meta *l2meta)
{
    BDRVQcow2State *s = bs->opaque;
    QCowL2Meta *m;

    if (!(s->data_file->bs->supported_zero_flags & BDRV_REQ_NO_FALLBACK)) {
        return 0;
    }

    if (bs->encrypted) {
        return 0;
    }

    for (m = l2meta; m != NULL; m = m->next) {
        int ret;

        if (!m->cow_start.nb_bytes && !m->cow_end.nb_bytes) {
            continue;
        }

        if (!is_zero_cow(bs, m)) {
            continue;
        }

        /*
         * instead of writing zero COW buffers,
         * efficiently zero out the whole clusters
         */

        ret = qcow2_pre_write_overlap_check(bs, 0, m->alloc_offset,
                                            m->nb_clusters * s->cluster_size,
                                            true);
        if (ret < 0) {
            return ret;
        }

        BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC_SPACE);
        ret = bdrv_co_pwrite_zeroes(s->data_file, m->alloc_offset,
                                    m->nb_clusters * s->cluster_size,
                                    BDRV_REQ_NO_FALLBACK);
        if (ret < 0) {
            if (ret != -ENOTSUP && ret != -EAGAIN) {
                return ret;
            }
            continue;
        }

        trace_qcow2_skip_cow(qemu_coroutine_self(), m->offset, m->nb_clusters);
        m->skip_cow = true;
    }
    return 0;
}

/*
 * qcow2_co_pwritev_task
 * Called with s->lock unlocked
 * l2meta  - if not NULL, qcow2_co_pwritev_task() will consume it. Caller must
 *           not use it somehow after qcow2_co_pwritev_task() call
 */
static coroutine_fn int qcow2_co_pwritev_task(BlockDriverState *bs,
                                              uint64_t file_cluster_offset,
                                              uint64_t offset, uint64_t bytes,
                                              QEMUIOVector *qiov,
                                              uint64_t qiov_offset,
                                              QCowL2Meta *l2meta)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    void *crypt_buf = NULL;
    int offset_in_cluster = offset_into_cluster(s, offset);
    QEMUIOVector encrypted_qiov;

    if (bs->encrypted) {
        assert(s->crypto);
        assert(bytes <= QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
        crypt_buf = qemu_try_blockalign(bs->file->bs, bytes);
        if (crypt_buf == NULL) {
            ret = -ENOMEM;
            goto out_unlocked;
        }
        qemu_iovec_to_buf(qiov, qiov_offset, crypt_buf, bytes);

        if (qcow2_co_encrypt(bs, file_cluster_offset + offset_in_cluster,
                             offset, crypt_buf, bytes) < 0)
        {
            ret = -EIO;
            goto out_unlocked;
        }

        qemu_iovec_init_buf(&encrypted_qiov, crypt_buf, bytes);
        qiov = &encrypted_qiov;
        qiov_offset = 0;
    }

    /* Try to efficiently initialize the physical space with zeroes */
    ret = handle_alloc_space(bs, l2meta);
    if (ret < 0) {
        goto out_unlocked;
    }

    /*
     * If we need to do COW, check if it's possible to merge the
     * writing of the guest data together with that of the COW regions.
     * If it's not possible (or not necessary) then write the
     * guest data now.
     */
    if (!merge_cow(offset, bytes, qiov, qiov_offset, l2meta)) {
        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
        trace_qcow2_writev_data(qemu_coroutine_self(),
                                file_cluster_offset + offset_in_cluster);
        ret = bdrv_co_pwritev_part(s->data_file,
                                   file_cluster_offset + offset_in_cluster,
                                   bytes, qiov, qiov_offset, 0);
        if (ret < 0) {
            goto out_unlocked;
        }
    }

    qemu_co_mutex_lock(&s->lock);

    ret = qcow2_handle_l2meta(bs, &l2meta, true);
    goto out_locked;

out_unlocked:
    qemu_co_mutex_lock(&s->lock);

out_locked:
    qcow2_handle_l2meta(bs, &l2meta, false);
    qemu_co_mutex_unlock(&s->lock);

    qemu_vfree(crypt_buf);

    return ret;
}

static coroutine_fn int qcow2_co_pwritev_task_entry(AioTask *task)
{
    Qcow2AioTask *t = container_of(task, Qcow2AioTask, task);

    assert(!t->cluster_type);

    return qcow2_co_pwritev_task(t->bs, t->file_cluster_offset,
                                 t->offset, t->bytes, t->qiov, t->qiov_offset,
                                 t->l2meta);
}

static coroutine_fn int qcow2_co_pwritev_part(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    int offset_in_cluster;
    int ret;
    unsigned int cur_bytes; /* number of sectors in current iteration */
    uint64_t cluster_offset;
    QCowL2Meta *l2meta = NULL;
    AioTaskPool *aio = NULL;

    trace_qcow2_writev_start_req(qemu_coroutine_self(), offset, bytes);

    while (bytes != 0 && aio_task_pool_status(aio) == 0) {

        l2meta = NULL;

        trace_qcow2_writev_start_part(qemu_coroutine_self());
        offset_in_cluster = offset_into_cluster(s, offset);
        cur_bytes = MIN(bytes, INT_MAX);
        if (bs->encrypted) {
            cur_bytes = MIN(cur_bytes,
                            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size
                            - offset_in_cluster);
        }

        qemu_co_mutex_lock(&s->lock);

        ret = qcow2_alloc_cluster_offset(bs, offset, &cur_bytes,
                                         &cluster_offset, &l2meta);
        if (ret < 0) {
            goto out_locked;
        }

        assert(offset_into_cluster(s, cluster_offset) == 0);

        ret = qcow2_pre_write_overlap_check(bs, 0,
                                            cluster_offset + offset_in_cluster,
                                            cur_bytes, true);
        if (ret < 0) {
            goto out_locked;
        }

        qemu_co_mutex_unlock(&s->lock);

        if (!aio && cur_bytes != bytes) {
            aio = aio_task_pool_new(QCOW2_MAX_WORKERS);
        }
        ret = qcow2_add_task(bs, aio, qcow2_co_pwritev_task_entry, 0,
                             cluster_offset, offset, cur_bytes,
                             qiov, qiov_offset, l2meta);
        l2meta = NULL; /* l2meta is consumed by qcow2_co_pwritev_task() */
        if (ret < 0) {
            goto fail_nometa;
        }

        bytes -= cur_bytes;
        offset += cur_bytes;
        qiov_offset += cur_bytes;
        trace_qcow2_writev_done_part(qemu_coroutine_self(), cur_bytes);
    }
    ret = 0;

    qemu_co_mutex_lock(&s->lock);

out_locked:
    qcow2_handle_l2meta(bs, &l2meta, false);

    qemu_co_mutex_unlock(&s->lock);

fail_nometa:
    if (aio) {
        aio_task_pool_wait_all(aio);
        if (ret == 0) {
            ret = aio_task_pool_status(aio);
        }
        g_free(aio);
    }

    trace_qcow2_writev_done_req(qemu_coroutine_self(), ret);

    return ret;
}

static int qcow2_inactivate(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int ret, result = 0;
    Error *local_err = NULL;

    qcow2_store_persistent_dirty_bitmaps(bs, true, &local_err);
    if (local_err != NULL) {
        result = -EINVAL;
        error_reportf_err(local_err, "Lost persistent bitmaps during "
                          "inactivation of node '%s': ",
                          bdrv_get_device_or_node_name(bs));
    }

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
    qcow2_cache_destroy(s->l2_table_cache);
    qcow2_cache_destroy(s->refcount_block_cache);

    qcrypto_block_free(s->crypto);
    s->crypto = NULL;
    qapi_free_QCryptoBlockOpenOptions(s->crypto_opts);

    g_free(s->unknown_header_fields);
    cleanup_unknown_header_ext(bs);

    g_free(s->image_data_file);
    g_free(s->image_backing_file);
    g_free(s->image_backing_format);

    if (has_data_file(bs)) {
        bdrv_unref_child(bs, s->data_file);
        s->data_file = NULL;
    }

    qcow2_refcount_close(bs);
    qcow2_free_snapshots(bs);
}

static void coroutine_fn qcow2_co_invalidate_cache(BlockDriverState *bs,
                                                   Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int flags = s->flags;
    QCryptoBlock *crypto = NULL;
    QDict *options;
    Error *local_err = NULL;
    int ret;

    /*
     * Backing files are read-only which makes all of their metadata immutable,
     * that means we don't have to worry about reopening them here.
     */

    crypto = s->crypto;
    s->crypto = NULL;

    qcow2_close(bs);

    memset(s, 0, sizeof(BDRVQcow2State));
    options = qdict_clone_shallow(bs->options);

    flags &= ~BDRV_O_INACTIVE;
    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_do_open(bs, options, flags, &local_err);
    qemu_co_mutex_unlock(&s->lock);
    qobject_unref(options);
    if (local_err) {
        error_propagate_prepend(errp, local_err,
                                "Could not reopen qcow2 layer: ");
        bs->drv = NULL;
        return;
    } else if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not reopen qcow2 layer");
        bs->drv = NULL;
        return;
    }

    s->crypto = crypto;
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

    if (len) {
        memcpy(buf + sizeof(QCowExtension), s, len);
    }

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

    ret = validate_compression_type(s, NULL);
    if (ret) {
        goto fail;
    }

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
        .compression_type       = s->compression_type,
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

    /* External data file header extension */
    if (has_data_file(bs) && s->image_data_file) {
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_DATA_FILE,
                             s->image_data_file, strlen(s->image_data_file),
                             buflen);
        if (ret < 0) {
            goto fail;
        }

        buf += ret;
        buflen -= ret;
    }

    /* Full disk encryption header pointer extension */
    if (s->crypto_header.offset != 0) {
        s->crypto_header.offset = cpu_to_be64(s->crypto_header.offset);
        s->crypto_header.length = cpu_to_be64(s->crypto_header.length);
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_CRYPTO_HEADER,
                             &s->crypto_header, sizeof(s->crypto_header),
                             buflen);
        s->crypto_header.offset = be64_to_cpu(s->crypto_header.offset);
        s->crypto_header.length = be64_to_cpu(s->crypto_header.length);
        if (ret < 0) {
            goto fail;
        }
        buf += ret;
        buflen -= ret;
    }

    /*
     * Feature table.  A mere 8 feature names occupies 392 bytes, and
     * when coupled with the v3 minimum header of 104 bytes plus the
     * 8-byte end-of-extension marker, that would leave only 8 bytes
     * for a backing file name in an image with 512-byte clusters.
     * Thus, we choose to omit this header for cluster sizes 4k and
     * smaller.
     */
    if (s->qcow_version >= 3 && s->cluster_size > 4096) {
        static const Qcow2Feature features[] = {
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
                .type = QCOW2_FEAT_TYPE_INCOMPATIBLE,
                .bit  = QCOW2_INCOMPAT_DATA_FILE_BITNR,
                .name = "external data file",
            },
            {
                .type = QCOW2_FEAT_TYPE_INCOMPATIBLE,
                .bit  = QCOW2_INCOMPAT_COMPRESSION_BITNR,
                .name = "compression type",
            },
            {
                .type = QCOW2_FEAT_TYPE_COMPATIBLE,
                .bit  = QCOW2_COMPAT_LAZY_REFCOUNTS_BITNR,
                .name = "lazy refcounts",
            },
            {
                .type = QCOW2_FEAT_TYPE_AUTOCLEAR,
                .bit  = QCOW2_AUTOCLEAR_BITMAPS_BITNR,
                .name = "bitmaps",
            },
            {
                .type = QCOW2_FEAT_TYPE_AUTOCLEAR,
                .bit  = QCOW2_AUTOCLEAR_DATA_FILE_RAW_BITNR,
                .name = "raw external data",
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

    /* Bitmap extension */
    if (s->nb_bitmaps > 0) {
        Qcow2BitmapHeaderExt bitmaps_header = {
            .nb_bitmaps = cpu_to_be32(s->nb_bitmaps),
            .bitmap_directory_size =
                    cpu_to_be64(s->bitmap_directory_size),
            .bitmap_directory_offset =
                    cpu_to_be64(s->bitmap_directory_offset)
        };
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_BITMAPS,
                             &bitmaps_header, sizeof(bitmaps_header),
                             buflen);
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
    ret = bdrv_pwrite(bs->file, 0, header, s->cluster_size);
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

    /* Adding a backing file means that the external data file alone won't be
     * enough to make sense of the content */
    if (backing_file && data_file_is_raw(bs)) {
        return -EINVAL;
    }

    if (backing_file && strlen(backing_file) > 1023) {
        return -EINVAL;
    }

    pstrcpy(bs->auto_backing_file, sizeof(bs->auto_backing_file),
            backing_file ?: "");
    pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_file ?: "");
    pstrcpy(bs->backing_format, sizeof(bs->backing_format), backing_fmt ?: "");

    g_free(s->image_backing_file);
    g_free(s->image_backing_format);

    s->image_backing_file = backing_file ? g_strdup(bs->backing_file) : NULL;
    s->image_backing_format = backing_fmt ? g_strdup(bs->backing_format) : NULL;

    return qcow2_update_header(bs);
}

static int qcow2_set_up_encryption(BlockDriverState *bs,
                                   QCryptoBlockCreateOptions *cryptoopts,
                                   Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCryptoBlock *crypto = NULL;
    int fmt, ret;

    switch (cryptoopts->format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        fmt = QCOW_CRYPT_LUKS;
        break;
    case Q_CRYPTO_BLOCK_FORMAT_QCOW:
        fmt = QCOW_CRYPT_AES;
        break;
    default:
        error_setg(errp, "Crypto format not supported in qcow2");
        return -EINVAL;
    }

    s->crypt_method_header = fmt;

    crypto = qcrypto_block_create(cryptoopts, "encrypt.",
                                  qcow2_crypto_hdr_init_func,
                                  qcow2_crypto_hdr_write_func,
                                  bs, errp);
    if (!crypto) {
        return -EINVAL;
    }

    ret = qcow2_update_header(bs);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        goto out;
    }

    ret = 0;
 out:
    qcrypto_block_free(crypto);
    return ret;
}

/**
 * Preallocates metadata structures for data clusters between @offset (in the
 * guest disk) and @new_length (which is thus generally the new guest disk
 * size).
 *
 * Returns: 0 on success, -errno on failure.
 */
static int coroutine_fn preallocate_co(BlockDriverState *bs, uint64_t offset,
                                       uint64_t new_length, PreallocMode mode,
                                       Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t bytes;
    uint64_t host_offset = 0;
    int64_t file_length;
    unsigned int cur_bytes;
    int ret;
    QCowL2Meta *meta;

    assert(offset <= new_length);
    bytes = new_length - offset;

    while (bytes) {
        cur_bytes = MIN(bytes, QEMU_ALIGN_DOWN(INT_MAX, s->cluster_size));
        ret = qcow2_alloc_cluster_offset(bs, offset, &cur_bytes,
                                         &host_offset, &meta);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Allocating clusters failed");
            return ret;
        }

        while (meta) {
            QCowL2Meta *next = meta->next;

            ret = qcow2_alloc_cluster_link_l2(bs, meta);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Mapping clusters failed");
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

        bytes -= cur_bytes;
        offset += cur_bytes;
    }

    /*
     * It is expected that the image file is large enough to actually contain
     * all of the allocated clusters (otherwise we get failing reads after
     * EOF). Extend the image to the last allocated sector.
     */
    file_length = bdrv_getlength(s->data_file->bs);
    if (file_length < 0) {
        error_setg_errno(errp, -file_length, "Could not get file size");
        return file_length;
    }

    if (host_offset + cur_bytes > file_length) {
        if (mode == PREALLOC_MODE_METADATA) {
            mode = PREALLOC_MODE_OFF;
        }
        ret = bdrv_co_truncate(s->data_file, host_offset + cur_bytes, false,
                               mode, 0, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/* qcow2_refcount_metadata_size:
 * @clusters: number of clusters to refcount (including data and L1/L2 tables)
 * @cluster_size: size of a cluster, in bytes
 * @refcount_order: refcount bits power-of-2 exponent
 * @generous_increase: allow for the refcount table to be 1.5x as large as it
 *                     needs to be
 *
 * Returns: Number of bytes required for refcount blocks and table metadata.
 */
int64_t qcow2_refcount_metadata_size(int64_t clusters, size_t cluster_size,
                                     int refcount_order, bool generous_increase,
                                     uint64_t *refblock_count)
{
    /*
     * Every host cluster is reference-counted, including metadata (even
     * refcount metadata is recursively included).
     *
     * An accurate formula for the size of refcount metadata size is difficult
     * to derive.  An easier method of calculation is finding the fixed point
     * where no further refcount blocks or table clusters are required to
     * reference count every cluster.
     */
    int64_t blocks_per_table_cluster = cluster_size / sizeof(uint64_t);
    int64_t refcounts_per_block = cluster_size * 8 / (1 << refcount_order);
    int64_t table = 0;  /* number of refcount table clusters */
    int64_t blocks = 0; /* number of refcount block clusters */
    int64_t last;
    int64_t n = 0;

    do {
        last = n;
        blocks = DIV_ROUND_UP(clusters + table + blocks, refcounts_per_block);
        table = DIV_ROUND_UP(blocks, blocks_per_table_cluster);
        n = clusters + blocks + table;

        if (n == last && generous_increase) {
            clusters += DIV_ROUND_UP(table, 2);
            n = 0; /* force another loop */
            generous_increase = false;
        }
    } while (n != last);

    if (refblock_count) {
        *refblock_count = blocks;
    }

    return (blocks + table) * cluster_size;
}

/**
 * qcow2_calc_prealloc_size:
 * @total_size: virtual disk size in bytes
 * @cluster_size: cluster size in bytes
 * @refcount_order: refcount bits power-of-2 exponent
 *
 * Returns: Total number of bytes required for the fully allocated image
 * (including metadata).
 */
static int64_t qcow2_calc_prealloc_size(int64_t total_size,
                                        size_t cluster_size,
                                        int refcount_order)
{
    int64_t meta_size = 0;
    uint64_t nl1e, nl2e;
    int64_t aligned_total_size = ROUND_UP(total_size, cluster_size);

    /* header: 1 cluster */
    meta_size += cluster_size;

    /* total size of L2 tables */
    nl2e = aligned_total_size / cluster_size;
    nl2e = ROUND_UP(nl2e, cluster_size / sizeof(uint64_t));
    meta_size += nl2e * sizeof(uint64_t);

    /* total size of L1 tables */
    nl1e = nl2e * sizeof(uint64_t) / cluster_size;
    nl1e = ROUND_UP(nl1e, cluster_size / sizeof(uint64_t));
    meta_size += nl1e * sizeof(uint64_t);

    /* total size of refcount table and blocks */
    meta_size += qcow2_refcount_metadata_size(
            (meta_size + aligned_total_size) / cluster_size,
            cluster_size, refcount_order, false, NULL);

    return meta_size + aligned_total_size;
}

static bool validate_cluster_size(size_t cluster_size, Error **errp)
{
    int cluster_bits = ctz32(cluster_size);
    if (cluster_bits < MIN_CLUSTER_BITS || cluster_bits > MAX_CLUSTER_BITS ||
        (1 << cluster_bits) != cluster_size)
    {
        error_setg(errp, "Cluster size must be a power of two between %d and "
                   "%dk", 1 << MIN_CLUSTER_BITS, 1 << (MAX_CLUSTER_BITS - 10));
        return false;
    }
    return true;
}

static size_t qcow2_opt_get_cluster_size_del(QemuOpts *opts, Error **errp)
{
    size_t cluster_size;

    cluster_size = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE,
                                         DEFAULT_CLUSTER_SIZE);
    if (!validate_cluster_size(cluster_size, errp)) {
        return 0;
    }
    return cluster_size;
}

static int qcow2_opt_get_version_del(QemuOpts *opts, Error **errp)
{
    char *buf;
    int ret;

    buf = qemu_opt_get_del(opts, BLOCK_OPT_COMPAT_LEVEL);
    if (!buf) {
        ret = 3; /* default */
    } else if (!strcmp(buf, "0.10")) {
        ret = 2;
    } else if (!strcmp(buf, "1.1")) {
        ret = 3;
    } else {
        error_setg(errp, "Invalid compatibility level: '%s'", buf);
        ret = -EINVAL;
    }
    g_free(buf);
    return ret;
}

static uint64_t qcow2_opt_get_refcount_bits_del(QemuOpts *opts, int version,
                                                Error **errp)
{
    uint64_t refcount_bits;

    refcount_bits = qemu_opt_get_number_del(opts, BLOCK_OPT_REFCOUNT_BITS, 16);
    if (refcount_bits > 64 || !is_power_of_2(refcount_bits)) {
        error_setg(errp, "Refcount width must be a power of two and may not "
                   "exceed 64 bits");
        return 0;
    }

    if (version < 3 && refcount_bits != 16) {
        error_setg(errp, "Different refcount widths than 16 bits require "
                   "compatibility level 1.1 or above (use compat=1.1 or "
                   "greater)");
        return 0;
    }

    return refcount_bits;
}

static int coroutine_fn
qcow2_co_create(BlockdevCreateOptions *create_options, Error **errp)
{
    BlockdevCreateOptionsQcow2 *qcow2_opts;
    QDict *options;

    /*
     * Open the image file and write a minimal qcow2 header.
     *
     * We keep things simple and start with a zero-sized image. We also
     * do without refcount blocks or a L1 table for now. We'll fix the
     * inconsistency later.
     *
     * We do need a refcount table because growing the refcount table means
     * allocating two new refcount blocks - the second of which would be at
     * 2 GB for 64k clusters, and we don't want to have a 2 GB initial file
     * size for any qcow2 image.
     */
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    BlockDriverState *data_bs = NULL;
    QCowHeader *header;
    size_t cluster_size;
    int version;
    int refcount_order;
    uint64_t* refcount_table;
    int ret;
    uint8_t compression_type = QCOW2_COMPRESSION_TYPE_ZLIB;

    assert(create_options->driver == BLOCKDEV_DRIVER_QCOW2);
    qcow2_opts = &create_options->u.qcow2;

    bs = bdrv_open_blockdev_ref(qcow2_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    /* Validate options and set default values */
    if (!QEMU_IS_ALIGNED(qcow2_opts->size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Image size must be a multiple of %u bytes",
                   (unsigned) BDRV_SECTOR_SIZE);
        ret = -EINVAL;
        goto out;
    }

    if (qcow2_opts->has_version) {
        switch (qcow2_opts->version) {
        case BLOCKDEV_QCOW2_VERSION_V2:
            version = 2;
            break;
        case BLOCKDEV_QCOW2_VERSION_V3:
            version = 3;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        version = 3;
    }

    if (qcow2_opts->has_cluster_size) {
        cluster_size = qcow2_opts->cluster_size;
    } else {
        cluster_size = DEFAULT_CLUSTER_SIZE;
    }

    if (!validate_cluster_size(cluster_size, errp)) {
        ret = -EINVAL;
        goto out;
    }

    if (!qcow2_opts->has_preallocation) {
        qcow2_opts->preallocation = PREALLOC_MODE_OFF;
    }
    if (qcow2_opts->has_backing_file &&
        qcow2_opts->preallocation != PREALLOC_MODE_OFF)
    {
        error_setg(errp, "Backing file and preallocation cannot be used at "
                   "the same time");
        ret = -EINVAL;
        goto out;
    }
    if (qcow2_opts->has_backing_fmt && !qcow2_opts->has_backing_file) {
        error_setg(errp, "Backing format cannot be used without backing file");
        ret = -EINVAL;
        goto out;
    }

    if (!qcow2_opts->has_lazy_refcounts) {
        qcow2_opts->lazy_refcounts = false;
    }
    if (version < 3 && qcow2_opts->lazy_refcounts) {
        error_setg(errp, "Lazy refcounts only supported with compatibility "
                   "level 1.1 and above (use version=v3 or greater)");
        ret = -EINVAL;
        goto out;
    }

    if (!qcow2_opts->has_refcount_bits) {
        qcow2_opts->refcount_bits = 16;
    }
    if (qcow2_opts->refcount_bits > 64 ||
        !is_power_of_2(qcow2_opts->refcount_bits))
    {
        error_setg(errp, "Refcount width must be a power of two and may not "
                   "exceed 64 bits");
        ret = -EINVAL;
        goto out;
    }
    if (version < 3 && qcow2_opts->refcount_bits != 16) {
        error_setg(errp, "Different refcount widths than 16 bits require "
                   "compatibility level 1.1 or above (use version=v3 or "
                   "greater)");
        ret = -EINVAL;
        goto out;
    }
    refcount_order = ctz32(qcow2_opts->refcount_bits);

    if (qcow2_opts->data_file_raw && !qcow2_opts->data_file) {
        error_setg(errp, "data-file-raw requires data-file");
        ret = -EINVAL;
        goto out;
    }
    if (qcow2_opts->data_file_raw && qcow2_opts->has_backing_file) {
        error_setg(errp, "Backing file and data-file-raw cannot be used at "
                   "the same time");
        ret = -EINVAL;
        goto out;
    }

    if (qcow2_opts->data_file) {
        if (version < 3) {
            error_setg(errp, "External data files are only supported with "
                       "compatibility level 1.1 and above (use version=v3 or "
                       "greater)");
            ret = -EINVAL;
            goto out;
        }
        data_bs = bdrv_open_blockdev_ref(qcow2_opts->data_file, errp);
        if (data_bs == NULL) {
            ret = -EIO;
            goto out;
        }
    }

    if (qcow2_opts->has_compression_type &&
        qcow2_opts->compression_type != QCOW2_COMPRESSION_TYPE_ZLIB) {

        ret = -EINVAL;

        if (version < 3) {
            error_setg(errp, "Non-zlib compression type is only supported with "
                       "compatibility level 1.1 and above (use version=v3 or "
                       "greater)");
            goto out;
        }

        switch (qcow2_opts->compression_type) {
#ifdef CONFIG_ZSTD
        case QCOW2_COMPRESSION_TYPE_ZSTD:
            break;
#endif
        default:
            error_setg(errp, "Unknown compression type");
            goto out;
        }

        compression_type = qcow2_opts->compression_type;
    }

    /* Create BlockBackend to write to the image */
    blk = blk_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                          errp);
    if (!blk) {
        ret = -EPERM;
        goto out;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    /* Write the header */
    QEMU_BUILD_BUG_ON((1 << MIN_CLUSTER_BITS) < sizeof(*header));
    header = g_malloc0(cluster_size);
    *header = (QCowHeader) {
        .magic                      = cpu_to_be32(QCOW_MAGIC),
        .version                    = cpu_to_be32(version),
        .cluster_bits               = cpu_to_be32(ctz32(cluster_size)),
        .size                       = cpu_to_be64(0),
        .l1_table_offset            = cpu_to_be64(0),
        .l1_size                    = cpu_to_be32(0),
        .refcount_table_offset      = cpu_to_be64(cluster_size),
        .refcount_table_clusters    = cpu_to_be32(1),
        .refcount_order             = cpu_to_be32(refcount_order),
        /* don't deal with endianness since compression_type is 1 byte long */
        .compression_type           = compression_type,
        .header_length              = cpu_to_be32(sizeof(*header)),
    };

    /* We'll update this to correct value later */
    header->crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);

    if (qcow2_opts->lazy_refcounts) {
        header->compatible_features |=
            cpu_to_be64(QCOW2_COMPAT_LAZY_REFCOUNTS);
    }
    if (data_bs) {
        header->incompatible_features |=
            cpu_to_be64(QCOW2_INCOMPAT_DATA_FILE);
    }
    if (qcow2_opts->data_file_raw) {
        header->autoclear_features |=
            cpu_to_be64(QCOW2_AUTOCLEAR_DATA_FILE_RAW);
    }
    if (compression_type != QCOW2_COMPRESSION_TYPE_ZLIB) {
        header->incompatible_features |=
            cpu_to_be64(QCOW2_INCOMPAT_COMPRESSION);
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
    qdict_put_str(options, "driver", "qcow2");
    qdict_put_str(options, "file", bs->node_name);
    if (data_bs) {
        qdict_put_str(options, "data-file", data_bs->node_name);
    }
    blk = blk_new_open(NULL, NULL, options,
                       BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_NO_FLUSH,
                       errp);
    if (blk == NULL) {
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

    /* Set the external data file if necessary */
    if (data_bs) {
        BDRVQcow2State *s = blk_bs(blk)->opaque;
        s->image_data_file = g_strdup(data_bs->filename);
    }

    /* Create a full header (including things like feature table) */
    ret = qcow2_update_header(blk_bs(blk));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not update qcow2 header");
        goto out;
    }

    /* Okay, now that we have a valid image, let's give it the right size */
    ret = blk_truncate(blk, qcow2_opts->size, false, qcow2_opts->preallocation,
                       0, errp);
    if (ret < 0) {
        error_prepend(errp, "Could not resize image: ");
        goto out;
    }

    /* Want a backing file? There you go. */
    if (qcow2_opts->has_backing_file) {
        const char *backing_format = NULL;

        if (qcow2_opts->has_backing_fmt) {
            backing_format = BlockdevDriver_str(qcow2_opts->backing_fmt);
        }

        ret = bdrv_change_backing_file(blk_bs(blk), qcow2_opts->backing_file,
                                       backing_format, false);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not assign backing file '%s' "
                             "with format '%s'", qcow2_opts->backing_file,
                             backing_format);
            goto out;
        }
    }

    /* Want encryption? There you go. */
    if (qcow2_opts->has_encrypt) {
        ret = qcow2_set_up_encryption(blk_bs(blk), qcow2_opts->encrypt, errp);
        if (ret < 0) {
            goto out;
        }
    }

    blk_unref(blk);
    blk = NULL;

    /* Reopen the image without BDRV_O_NO_FLUSH to flush it before returning.
     * Using BDRV_O_NO_IO, since encryption is now setup we don't want to
     * have to setup decryption context. We're not doing any I/O on the top
     * level BlockDriverState, only lower layers, where BDRV_O_NO_IO does
     * not have effect.
     */
    options = qdict_new();
    qdict_put_str(options, "driver", "qcow2");
    qdict_put_str(options, "file", bs->node_name);
    if (data_bs) {
        qdict_put_str(options, "data-file", data_bs->node_name);
    }
    blk = blk_new_open(NULL, NULL, options,
                       BDRV_O_RDWR | BDRV_O_NO_BACKING | BDRV_O_NO_IO,
                       errp);
    if (blk == NULL) {
        ret = -EIO;
        goto out;
    }

    ret = 0;
out:
    blk_unref(blk);
    bdrv_unref(bs);
    bdrv_unref(data_bs);
    return ret;
}

static int coroutine_fn qcow2_co_create_opts(BlockDriver *drv,
                                             const char *filename,
                                             QemuOpts *opts,
                                             Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    QDict *qdict;
    Visitor *v;
    BlockDriverState *bs = NULL;
    BlockDriverState *data_bs = NULL;
    const char *val;
    int ret;

    /* Only the keyval visitor supports the dotted syntax needed for
     * encryption, so go through a QDict before getting a QAPI type. Ignore
     * options meant for the protocol layer so that the visitor doesn't
     * complain. */
    qdict = qemu_opts_to_qdict_filtered(opts, NULL, bdrv_qcow2.create_opts,
                                        true);

    /* Handle encryption options */
    val = qdict_get_try_str(qdict, BLOCK_OPT_ENCRYPT);
    if (val && !strcmp(val, "on")) {
        qdict_put_str(qdict, BLOCK_OPT_ENCRYPT, "qcow");
    } else if (val && !strcmp(val, "off")) {
        qdict_del(qdict, BLOCK_OPT_ENCRYPT);
    }

    val = qdict_get_try_str(qdict, BLOCK_OPT_ENCRYPT_FORMAT);
    if (val && !strcmp(val, "aes")) {
        qdict_put_str(qdict, BLOCK_OPT_ENCRYPT_FORMAT, "qcow");
    }

    /* Convert compat=0.10/1.1 into compat=v2/v3, to be renamed into
     * version=v2/v3 below. */
    val = qdict_get_try_str(qdict, BLOCK_OPT_COMPAT_LEVEL);
    if (val && !strcmp(val, "0.10")) {
        qdict_put_str(qdict, BLOCK_OPT_COMPAT_LEVEL, "v2");
    } else if (val && !strcmp(val, "1.1")) {
        qdict_put_str(qdict, BLOCK_OPT_COMPAT_LEVEL, "v3");
    }

    /* Change legacy command line options into QMP ones */
    static const QDictRenames opt_renames[] = {
        { BLOCK_OPT_BACKING_FILE,       "backing-file" },
        { BLOCK_OPT_BACKING_FMT,        "backing-fmt" },
        { BLOCK_OPT_CLUSTER_SIZE,       "cluster-size" },
        { BLOCK_OPT_LAZY_REFCOUNTS,     "lazy-refcounts" },
        { BLOCK_OPT_REFCOUNT_BITS,      "refcount-bits" },
        { BLOCK_OPT_ENCRYPT,            BLOCK_OPT_ENCRYPT_FORMAT },
        { BLOCK_OPT_COMPAT_LEVEL,       "version" },
        { BLOCK_OPT_DATA_FILE_RAW,      "data-file-raw" },
        { BLOCK_OPT_COMPRESSION_TYPE,   "compression-type" },
        { NULL, NULL },
    };

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto finish;
    }

    /* Create and open the file (protocol layer) */
    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        goto finish;
    }

    bs = bdrv_open(filename, NULL, NULL,
                   BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto finish;
    }

    /* Create and open an external data file (protocol layer) */
    val = qdict_get_try_str(qdict, BLOCK_OPT_DATA_FILE);
    if (val) {
        ret = bdrv_create_file(val, opts, errp);
        if (ret < 0) {
            goto finish;
        }

        data_bs = bdrv_open(val, NULL, NULL,
                            BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL,
                            errp);
        if (data_bs == NULL) {
            ret = -EIO;
            goto finish;
        }

        qdict_del(qdict, BLOCK_OPT_DATA_FILE);
        qdict_put_str(qdict, "data-file", data_bs->node_name);
    }

    /* Set 'driver' and 'node' options */
    qdict_put_str(qdict, "driver", "qcow2");
    qdict_put_str(qdict, "file", bs->node_name);

    /* Now get the QAPI type BlockdevCreateOptions */
    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto finish;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto finish;
    }

    /* Silently round up size */
    create_options->u.qcow2.size = ROUND_UP(create_options->u.qcow2.size,
                                            BDRV_SECTOR_SIZE);

    /* Create the qcow2 image (format layer) */
    ret = qcow2_co_create(create_options, errp);
    if (ret < 0) {
        goto finish;
    }

    ret = 0;
finish:
    qobject_unref(qdict);
    bdrv_unref(bs);
    bdrv_unref(data_bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}


static bool is_zero(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    int64_t nr;
    int res;

    /* Clamp to image length, before checking status of underlying sectors */
    if (offset + bytes > bs->total_sectors * BDRV_SECTOR_SIZE) {
        bytes = bs->total_sectors * BDRV_SECTOR_SIZE - offset;
    }

    if (!bytes) {
        return true;
    }
    res = bdrv_block_status_above(bs, NULL, offset, bytes, &nr, NULL, NULL);
    return res >= 0 && (res & BDRV_BLOCK_ZERO) && nr == bytes;
}

static coroutine_fn int qcow2_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int bytes, BdrvRequestFlags flags)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;

    uint32_t head = offset % s->cluster_size;
    uint32_t tail = (offset + bytes) % s->cluster_size;

    trace_qcow2_pwrite_zeroes_start_req(qemu_coroutine_self(), offset, bytes);
    if (offset + bytes == bs->total_sectors * BDRV_SECTOR_SIZE) {
        tail = 0;
    }

    if (head || tail) {
        uint64_t off;
        unsigned int nr;

        assert(head + bytes <= s->cluster_size);

        /* check whether remainder of cluster already reads as zero */
        if (!(is_zero(bs, offset - head, head) &&
              is_zero(bs, offset + bytes,
                      tail ? s->cluster_size - tail : 0))) {
            return -ENOTSUP;
        }

        qemu_co_mutex_lock(&s->lock);
        /* We can have new write after previous check */
        offset = QEMU_ALIGN_DOWN(offset, s->cluster_size);
        bytes = s->cluster_size;
        nr = s->cluster_size;
        ret = qcow2_get_cluster_offset(bs, offset, &nr, &off);
        if (ret != QCOW2_CLUSTER_UNALLOCATED &&
            ret != QCOW2_CLUSTER_ZERO_PLAIN &&
            ret != QCOW2_CLUSTER_ZERO_ALLOC) {
            qemu_co_mutex_unlock(&s->lock);
            return -ENOTSUP;
        }
    } else {
        qemu_co_mutex_lock(&s->lock);
    }

    trace_qcow2_pwrite_zeroes(qemu_coroutine_self(), offset, bytes);

    /* Whatever is left can use real zero clusters */
    ret = qcow2_cluster_zeroize(bs, offset, bytes, flags);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static coroutine_fn int qcow2_co_pdiscard(BlockDriverState *bs,
                                          int64_t offset, int bytes)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;

    /* If the image does not support QCOW_OFLAG_ZERO then discarding
     * clusters could expose stale data from the backing file. */
    if (s->qcow_version < 3 && bs->backing) {
        return -ENOTSUP;
    }

    if (!QEMU_IS_ALIGNED(offset | bytes, s->cluster_size)) {
        assert(bytes < s->cluster_size);
        /* Ignore partial clusters, except for the special case of the
         * complete partial cluster at the end of an unaligned file */
        if (!QEMU_IS_ALIGNED(offset, s->cluster_size) ||
            offset + bytes != bs->total_sectors * BDRV_SECTOR_SIZE) {
            return -ENOTSUP;
        }
    }

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_cluster_discard(bs, offset, bytes, QCOW2_DISCARD_REQUEST,
                                false);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int coroutine_fn
qcow2_co_copy_range_from(BlockDriverState *bs,
                         BdrvChild *src, uint64_t src_offset,
                         BdrvChild *dst, uint64_t dst_offset,
                         uint64_t bytes, BdrvRequestFlags read_flags,
                         BdrvRequestFlags write_flags)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;
    unsigned int cur_bytes; /* number of bytes in current iteration */
    BdrvChild *child = NULL;
    BdrvRequestFlags cur_write_flags;

    assert(!bs->encrypted);
    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {
        uint64_t copy_offset = 0;
        /* prepare next request */
        cur_bytes = MIN(bytes, INT_MAX);
        cur_write_flags = write_flags;

        ret = qcow2_get_cluster_offset(bs, src_offset, &cur_bytes, &copy_offset);
        if (ret < 0) {
            goto out;
        }

        switch (ret) {
        case QCOW2_CLUSTER_UNALLOCATED:
            if (bs->backing && bs->backing->bs) {
                int64_t backing_length = bdrv_getlength(bs->backing->bs);
                if (src_offset >= backing_length) {
                    cur_write_flags |= BDRV_REQ_ZERO_WRITE;
                } else {
                    child = bs->backing;
                    cur_bytes = MIN(cur_bytes, backing_length - src_offset);
                    copy_offset = src_offset;
                }
            } else {
                cur_write_flags |= BDRV_REQ_ZERO_WRITE;
            }
            break;

        case QCOW2_CLUSTER_ZERO_PLAIN:
        case QCOW2_CLUSTER_ZERO_ALLOC:
            cur_write_flags |= BDRV_REQ_ZERO_WRITE;
            break;

        case QCOW2_CLUSTER_COMPRESSED:
            ret = -ENOTSUP;
            goto out;

        case QCOW2_CLUSTER_NORMAL:
            child = s->data_file;
            copy_offset += offset_into_cluster(s, src_offset);
            break;

        default:
            abort();
        }
        qemu_co_mutex_unlock(&s->lock);
        ret = bdrv_co_copy_range_from(child,
                                      copy_offset,
                                      dst, dst_offset,
                                      cur_bytes, read_flags, cur_write_flags);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            goto out;
        }

        bytes -= cur_bytes;
        src_offset += cur_bytes;
        dst_offset += cur_bytes;
    }
    ret = 0;

out:
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int coroutine_fn
qcow2_co_copy_range_to(BlockDriverState *bs,
                       BdrvChild *src, uint64_t src_offset,
                       BdrvChild *dst, uint64_t dst_offset,
                       uint64_t bytes, BdrvRequestFlags read_flags,
                       BdrvRequestFlags write_flags)
{
    BDRVQcow2State *s = bs->opaque;
    int offset_in_cluster;
    int ret;
    unsigned int cur_bytes; /* number of sectors in current iteration */
    uint64_t cluster_offset;
    QCowL2Meta *l2meta = NULL;

    assert(!bs->encrypted);

    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {

        l2meta = NULL;

        offset_in_cluster = offset_into_cluster(s, dst_offset);
        cur_bytes = MIN(bytes, INT_MAX);

        /* TODO:
         * If src->bs == dst->bs, we could simply copy by incrementing
         * the refcnt, without copying user data.
         * Or if src->bs == dst->bs->backing->bs, we could copy by discarding. */
        ret = qcow2_alloc_cluster_offset(bs, dst_offset, &cur_bytes,
                                         &cluster_offset, &l2meta);
        if (ret < 0) {
            goto fail;
        }

        assert(offset_into_cluster(s, cluster_offset) == 0);

        ret = qcow2_pre_write_overlap_check(bs, 0,
                cluster_offset + offset_in_cluster, cur_bytes, true);
        if (ret < 0) {
            goto fail;
        }

        qemu_co_mutex_unlock(&s->lock);
        ret = bdrv_co_copy_range_to(src, src_offset,
                                    s->data_file,
                                    cluster_offset + offset_in_cluster,
                                    cur_bytes, read_flags, write_flags);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            goto fail;
        }

        ret = qcow2_handle_l2meta(bs, &l2meta, true);
        if (ret) {
            goto fail;
        }

        bytes -= cur_bytes;
        src_offset += cur_bytes;
        dst_offset += cur_bytes;
    }
    ret = 0;

fail:
    qcow2_handle_l2meta(bs, &l2meta, false);

    qemu_co_mutex_unlock(&s->lock);

    trace_qcow2_writev_done_req(qemu_coroutine_self(), ret);

    return ret;
}

static int coroutine_fn qcow2_co_truncate(BlockDriverState *bs, int64_t offset,
                                          bool exact, PreallocMode prealloc,
                                          BdrvRequestFlags flags, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t old_length;
    int64_t new_l1_size;
    int ret;
    QDict *options;

    if (prealloc != PREALLOC_MODE_OFF && prealloc != PREALLOC_MODE_METADATA &&
        prealloc != PREALLOC_MODE_FALLOC && prealloc != PREALLOC_MODE_FULL)
    {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    if (!QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "The new size must be a multiple of %u",
                   (unsigned) BDRV_SECTOR_SIZE);
        return -EINVAL;
    }

    qemu_co_mutex_lock(&s->lock);

    /*
     * Even though we store snapshot size for all images, it was not
     * required until v3, so it is not safe to proceed for v2.
     */
    if (s->nb_snapshots && s->qcow_version < 3) {
        error_setg(errp, "Can't resize a v2 image which has snapshots");
        ret = -ENOTSUP;
        goto fail;
    }

    /* See qcow2-bitmap.c for which bitmap scenarios prevent a resize. */
    if (qcow2_truncate_bitmaps_check(bs, errp)) {
        ret = -ENOTSUP;
        goto fail;
    }

    old_length = bs->total_sectors * BDRV_SECTOR_SIZE;
    new_l1_size = size_to_l1(s, offset);

    if (offset < old_length) {
        int64_t last_cluster, old_file_size;
        if (prealloc != PREALLOC_MODE_OFF) {
            error_setg(errp,
                       "Preallocation can't be used for shrinking an image");
            ret = -EINVAL;
            goto fail;
        }

        ret = qcow2_cluster_discard(bs, ROUND_UP(offset, s->cluster_size),
                                    old_length - ROUND_UP(offset,
                                                          s->cluster_size),
                                    QCOW2_DISCARD_ALWAYS, true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to discard cropped clusters");
            goto fail;
        }

        ret = qcow2_shrink_l1_table(bs, new_l1_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Failed to reduce the number of L2 tables");
            goto fail;
        }

        ret = qcow2_shrink_reftable(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Failed to discard unused refblocks");
            goto fail;
        }

        old_file_size = bdrv_getlength(bs->file->bs);
        if (old_file_size < 0) {
            error_setg_errno(errp, -old_file_size,
                             "Failed to inquire current file length");
            ret = old_file_size;
            goto fail;
        }
        last_cluster = qcow2_get_last_cluster(bs, old_file_size);
        if (last_cluster < 0) {
            error_setg_errno(errp, -last_cluster,
                             "Failed to find the last cluster");
            ret = last_cluster;
            goto fail;
        }
        if ((last_cluster + 1) * s->cluster_size < old_file_size) {
            Error *local_err = NULL;

            /*
             * Do not pass @exact here: It will not help the user if
             * we get an error here just because they wanted to shrink
             * their qcow2 image (on a block device) with qemu-img.
             * (And on the qcow2 layer, the @exact requirement is
             * always fulfilled, so there is no need to pass it on.)
             */
            bdrv_co_truncate(bs->file, (last_cluster + 1) * s->cluster_size,
                             false, PREALLOC_MODE_OFF, 0, &local_err);
            if (local_err) {
                warn_reportf_err(local_err,
                                 "Failed to truncate the tail of the image: ");
            }
        }
    } else {
        ret = qcow2_grow_l1_table(bs, new_l1_size, true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to grow the L1 table");
            goto fail;
        }
    }

    switch (prealloc) {
    case PREALLOC_MODE_OFF:
        if (has_data_file(bs)) {
            /*
             * If the caller wants an exact resize, the external data
             * file should be resized to the exact target size, too,
             * so we pass @exact here.
             */
            ret = bdrv_co_truncate(s->data_file, offset, exact, prealloc, 0,
                                   errp);
            if (ret < 0) {
                goto fail;
            }
        }
        break;

    case PREALLOC_MODE_METADATA:
        ret = preallocate_co(bs, old_length, offset, prealloc, errp);
        if (ret < 0) {
            goto fail;
        }
        break;

    case PREALLOC_MODE_FALLOC:
    case PREALLOC_MODE_FULL:
    {
        int64_t allocation_start, host_offset, guest_offset;
        int64_t clusters_allocated;
        int64_t old_file_size, last_cluster, new_file_size;
        uint64_t nb_new_data_clusters, nb_new_l2_tables;

        /* With a data file, preallocation means just allocating the metadata
         * and forwarding the truncate request to the data file */
        if (has_data_file(bs)) {
            ret = preallocate_co(bs, old_length, offset, prealloc, errp);
            if (ret < 0) {
                goto fail;
            }
            break;
        }

        old_file_size = bdrv_getlength(bs->file->bs);
        if (old_file_size < 0) {
            error_setg_errno(errp, -old_file_size,
                             "Failed to inquire current file length");
            ret = old_file_size;
            goto fail;
        }

        last_cluster = qcow2_get_last_cluster(bs, old_file_size);
        if (last_cluster >= 0) {
            old_file_size = (last_cluster + 1) * s->cluster_size;
        } else {
            old_file_size = ROUND_UP(old_file_size, s->cluster_size);
        }

        nb_new_data_clusters = (ROUND_UP(offset, s->cluster_size) -
            start_of_cluster(s, old_length)) >> s->cluster_bits;

        /* This is an overestimation; we will not actually allocate space for
         * these in the file but just make sure the new refcount structures are
         * able to cover them so we will not have to allocate new refblocks
         * while entering the data blocks in the potentially new L2 tables.
         * (We do not actually care where the L2 tables are placed. Maybe they
         *  are already allocated or they can be placed somewhere before
         *  @old_file_size. It does not matter because they will be fully
         *  allocated automatically, so they do not need to be covered by the
         *  preallocation. All that matters is that we will not have to allocate
         *  new refcount structures for them.) */
        nb_new_l2_tables = DIV_ROUND_UP(nb_new_data_clusters,
                                        s->cluster_size / sizeof(uint64_t));
        /* The cluster range may not be aligned to L2 boundaries, so add one L2
         * table for a potential head/tail */
        nb_new_l2_tables++;

        allocation_start = qcow2_refcount_area(bs, old_file_size,
                                               nb_new_data_clusters +
                                               nb_new_l2_tables,
                                               true, 0, 0);
        if (allocation_start < 0) {
            error_setg_errno(errp, -allocation_start,
                             "Failed to resize refcount structures");
            ret = allocation_start;
            goto fail;
        }

        clusters_allocated = qcow2_alloc_clusters_at(bs, allocation_start,
                                                     nb_new_data_clusters);
        if (clusters_allocated < 0) {
            error_setg_errno(errp, -clusters_allocated,
                             "Failed to allocate data clusters");
            ret = clusters_allocated;
            goto fail;
        }

        assert(clusters_allocated == nb_new_data_clusters);

        /* Allocate the data area */
        new_file_size = allocation_start +
                        nb_new_data_clusters * s->cluster_size;
        /*
         * Image file grows, so @exact does not matter.
         *
         * If we need to zero out the new area, try first whether the protocol
         * driver can already take care of this.
         */
        if (flags & BDRV_REQ_ZERO_WRITE) {
            ret = bdrv_co_truncate(bs->file, new_file_size, false, prealloc,
                                   BDRV_REQ_ZERO_WRITE, NULL);
            if (ret >= 0) {
                flags &= ~BDRV_REQ_ZERO_WRITE;
            }
        } else {
            ret = -1;
        }
        if (ret < 0) {
            ret = bdrv_co_truncate(bs->file, new_file_size, false, prealloc, 0,
                                   errp);
        }
        if (ret < 0) {
            error_prepend(errp, "Failed to resize underlying file: ");
            qcow2_free_clusters(bs, allocation_start,
                                nb_new_data_clusters * s->cluster_size,
                                QCOW2_DISCARD_OTHER);
            goto fail;
        }

        /* Create the necessary L2 entries */
        host_offset = allocation_start;
        guest_offset = old_length;
        while (nb_new_data_clusters) {
            int64_t nb_clusters = MIN(
                nb_new_data_clusters,
                s->l2_slice_size - offset_to_l2_slice_index(s, guest_offset));
            unsigned cow_start_length = offset_into_cluster(s, guest_offset);
            QCowL2Meta allocation;
            guest_offset = start_of_cluster(s, guest_offset);
            allocation = (QCowL2Meta) {
                .offset       = guest_offset,
                .alloc_offset = host_offset,
                .nb_clusters  = nb_clusters,
                .cow_start    = {
                    .offset       = 0,
                    .nb_bytes     = cow_start_length,
                },
                .cow_end      = {
                    .offset       = nb_clusters << s->cluster_bits,
                    .nb_bytes     = 0,
                },
            };
            qemu_co_queue_init(&allocation.dependent_requests);

            ret = qcow2_alloc_cluster_link_l2(bs, &allocation);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to update L2 tables");
                qcow2_free_clusters(bs, host_offset,
                                    nb_new_data_clusters * s->cluster_size,
                                    QCOW2_DISCARD_OTHER);
                goto fail;
            }

            guest_offset += nb_clusters * s->cluster_size;
            host_offset += nb_clusters * s->cluster_size;
            nb_new_data_clusters -= nb_clusters;
        }
        break;
    }

    default:
        g_assert_not_reached();
    }

    if ((flags & BDRV_REQ_ZERO_WRITE) && offset > old_length) {
        uint64_t zero_start = QEMU_ALIGN_UP(old_length, s->cluster_size);

        /*
         * Use zero clusters as much as we can. qcow2_cluster_zeroize()
         * requires a cluster-aligned start. The end may be unaligned if it is
         * at the end of the image (which it is here).
         */
        if (offset > zero_start) {
            ret = qcow2_cluster_zeroize(bs, zero_start, offset - zero_start, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to zero out new clusters");
                goto fail;
            }
        }

        /* Write explicit zeros for the unaligned head */
        if (zero_start > old_length) {
            uint64_t len = MIN(zero_start, offset) - old_length;
            uint8_t *buf = qemu_blockalign0(bs, len);
            QEMUIOVector qiov;
            qemu_iovec_init_buf(&qiov, buf, len);

            qemu_co_mutex_unlock(&s->lock);
            ret = qcow2_co_pwritev_part(bs, old_length, len, &qiov, 0, 0);
            qemu_co_mutex_lock(&s->lock);

            qemu_vfree(buf);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to zero out the new area");
                goto fail;
            }
        }
    }

    if (prealloc != PREALLOC_MODE_OFF) {
        /* Flush metadata before actually changing the image size */
        ret = qcow2_write_caches(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Failed to flush the preallocated area to disk");
            goto fail;
        }
    }

    bs->total_sectors = offset / BDRV_SECTOR_SIZE;

    /* write updated header.size */
    offset = cpu_to_be64(offset);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, size),
                           &offset, sizeof(uint64_t));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to update the image size");
        goto fail;
    }

    s->l1_vm_state_index = new_l1_size;

    /* Update cache sizes */
    options = qdict_clone_shallow(bs->options);
    ret = qcow2_update_options(bs, options, s->flags, errp);
    qobject_unref(options);
    if (ret < 0) {
        goto fail;
    }
    ret = 0;
fail:
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static coroutine_fn int
qcow2_co_pwritev_compressed_task(BlockDriverState *bs,
                                 uint64_t offset, uint64_t bytes,
                                 QEMUIOVector *qiov, size_t qiov_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;
    ssize_t out_len;
    uint8_t *buf, *out_buf;
    uint64_t cluster_offset;

    assert(bytes == s->cluster_size || (bytes < s->cluster_size &&
           (offset + bytes == bs->total_sectors << BDRV_SECTOR_BITS)));

    buf = qemu_blockalign(bs, s->cluster_size);
    if (bytes < s->cluster_size) {
        /* Zero-pad last write if image size is not cluster aligned */
        memset(buf + bytes, 0, s->cluster_size - bytes);
    }
    qemu_iovec_to_buf(qiov, qiov_offset, buf, bytes);

    out_buf = g_malloc(s->cluster_size);

    out_len = qcow2_co_compress(bs, out_buf, s->cluster_size - 1,
                                buf, s->cluster_size);
    if (out_len == -ENOMEM) {
        /* could not compress: write normal cluster */
        ret = qcow2_co_pwritev_part(bs, offset, bytes, qiov, qiov_offset, 0);
        if (ret < 0) {
            goto fail;
        }
        goto success;
    } else if (out_len < 0) {
        ret = -EINVAL;
        goto fail;
    }

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_alloc_compressed_cluster_offset(bs, offset, out_len,
                                                &cluster_offset);
    if (ret < 0) {
        qemu_co_mutex_unlock(&s->lock);
        goto fail;
    }

    ret = qcow2_pre_write_overlap_check(bs, 0, cluster_offset, out_len, true);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        goto fail;
    }

    BLKDBG_EVENT(s->data_file, BLKDBG_WRITE_COMPRESSED);
    ret = bdrv_co_pwrite(s->data_file, cluster_offset, out_len, out_buf, 0);
    if (ret < 0) {
        goto fail;
    }
success:
    ret = 0;
fail:
    qemu_vfree(buf);
    g_free(out_buf);
    return ret;
}

static coroutine_fn int qcow2_co_pwritev_compressed_task_entry(AioTask *task)
{
    Qcow2AioTask *t = container_of(task, Qcow2AioTask, task);

    assert(!t->cluster_type && !t->l2meta);

    return qcow2_co_pwritev_compressed_task(t->bs, t->offset, t->bytes, t->qiov,
                                            t->qiov_offset);
}

/*
 * XXX: put compressed sectors first, then all the cluster aligned
 * tables to avoid losing bytes in alignment
 */
static coroutine_fn int
qcow2_co_pwritev_compressed_part(BlockDriverState *bs,
                                 uint64_t offset, uint64_t bytes,
                                 QEMUIOVector *qiov, size_t qiov_offset)
{
    BDRVQcow2State *s = bs->opaque;
    AioTaskPool *aio = NULL;
    int ret = 0;

    if (has_data_file(bs)) {
        return -ENOTSUP;
    }

    if (bytes == 0) {
        /*
         * align end of file to a sector boundary to ease reading with
         * sector based I/Os
         */
        int64_t len = bdrv_getlength(bs->file->bs);
        if (len < 0) {
            return len;
        }
        return bdrv_co_truncate(bs->file, len, false, PREALLOC_MODE_OFF, 0,
                                NULL);
    }

    if (offset_into_cluster(s, offset)) {
        return -EINVAL;
    }

    if (offset_into_cluster(s, bytes) &&
        (offset + bytes) != (bs->total_sectors << BDRV_SECTOR_BITS)) {
        return -EINVAL;
    }

    while (bytes && aio_task_pool_status(aio) == 0) {
        uint64_t chunk_size = MIN(bytes, s->cluster_size);

        if (!aio && chunk_size != bytes) {
            aio = aio_task_pool_new(QCOW2_MAX_WORKERS);
        }

        ret = qcow2_add_task(bs, aio, qcow2_co_pwritev_compressed_task_entry,
                             0, 0, offset, chunk_size, qiov, qiov_offset, NULL);
        if (ret < 0) {
            break;
        }
        qiov_offset += chunk_size;
        offset += chunk_size;
        bytes -= chunk_size;
    }

    if (aio) {
        aio_task_pool_wait_all(aio);
        if (ret == 0) {
            ret = aio_task_pool_status(aio);
        }
        g_free(aio);
    }

    return ret;
}

static int coroutine_fn
qcow2_co_preadv_compressed(BlockDriverState *bs,
                           uint64_t file_cluster_offset,
                           uint64_t offset,
                           uint64_t bytes,
                           QEMUIOVector *qiov,
                           size_t qiov_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int ret = 0, csize, nb_csectors;
    uint64_t coffset;
    uint8_t *buf, *out_buf;
    int offset_in_cluster = offset_into_cluster(s, offset);

    coffset = file_cluster_offset & s->cluster_offset_mask;
    nb_csectors = ((file_cluster_offset >> s->csize_shift) & s->csize_mask) + 1;
    csize = nb_csectors * QCOW2_COMPRESSED_SECTOR_SIZE -
        (coffset & ~QCOW2_COMPRESSED_SECTOR_MASK);

    buf = g_try_malloc(csize);
    if (!buf) {
        return -ENOMEM;
    }

    out_buf = qemu_blockalign(bs, s->cluster_size);

    BLKDBG_EVENT(bs->file, BLKDBG_READ_COMPRESSED);
    ret = bdrv_co_pread(bs->file, coffset, csize, buf, 0);
    if (ret < 0) {
        goto fail;
    }

    if (qcow2_co_decompress(bs, out_buf, s->cluster_size, buf, csize) < 0) {
        ret = -EIO;
        goto fail;
    }

    qemu_iovec_from_buf(qiov, qiov_offset, out_buf + offset_in_cluster, bytes);

fail:
    qemu_vfree(out_buf);
    g_free(buf);

    return ret;
}

static int make_completely_empty(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    Error *local_err = NULL;
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

    ret = bdrv_pwrite_zeroes(bs->file, s->l1_table_offset,
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
    ret = bdrv_pwrite_zeroes(bs->file, s->cluster_size,
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
    l1_ofs_rt_ofs_cls.l1_offset = cpu_to_be64(3 * s->cluster_size);
    l1_ofs_rt_ofs_cls.reftable_offset = cpu_to_be64(s->cluster_size);
    l1_ofs_rt_ofs_cls.reftable_clusters = cpu_to_be32(1);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, l1_table_offset),
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
    s->max_refcount_table_index = 0;

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
    ret = bdrv_pwrite_sync(bs->file, s->cluster_size,
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

    ret = bdrv_truncate(bs->file, (3 + l1_clusters) * s->cluster_size, false,
                        PREALLOC_MODE_OFF, 0, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
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
    uint64_t offset, end_offset;
    int step = QEMU_ALIGN_DOWN(INT_MAX, s->cluster_size);
    int l1_clusters, ret = 0;

    l1_clusters = DIV_ROUND_UP(s->l1_size, s->cluster_size / sizeof(uint64_t));

    if (s->qcow_version >= 3 && !s->snapshots && !s->nb_bitmaps &&
        3 + l1_clusters <= s->refcount_block_size &&
        s->crypt_method_header != QCOW_CRYPT_LUKS &&
        !has_data_file(bs)) {
        /* The following function only works for qcow2 v3 images (it
         * requires the dirty flag) and only as long as there are no
         * features that reserve extra clusters (such as snapshots,
         * LUKS header, or persistent bitmaps), because it completely
         * empties the image.  Furthermore, the L1 table and three
         * additional clusters (image header, refcount table, one
         * refcount block) have to fit inside one refcount block. It
         * only resets the image file, i.e. does not work with an
         * external data file. */
        return make_completely_empty(bs);
    }

    /* This fallback code simply discards every active cluster; this is slow,
     * but works in all cases */
    end_offset = bs->total_sectors * BDRV_SECTOR_SIZE;
    for (offset = 0; offset < end_offset; offset += step) {
        /* As this function is generally used after committing an external
         * snapshot, QCOW2_DISCARD_SNAPSHOT seems appropriate. Also, the
         * default action for this kind of discard is to pass the discard,
         * which will ideally result in an actually smaller image file, as
         * is probably desired. */
        ret = qcow2_cluster_discard(bs, offset, MIN(step, end_offset - offset),
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
    ret = qcow2_write_caches(bs);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static BlockMeasureInfo *qcow2_measure(QemuOpts *opts, BlockDriverState *in_bs,
                                       Error **errp)
{
    Error *local_err = NULL;
    BlockMeasureInfo *info;
    uint64_t required = 0; /* bytes that contribute to required size */
    uint64_t virtual_size; /* disk size as seen by guest */
    uint64_t refcount_bits;
    uint64_t l2_tables;
    uint64_t luks_payload_size = 0;
    size_t cluster_size;
    int version;
    char *optstr;
    PreallocMode prealloc;
    bool has_backing_file;
    bool has_luks;

    /* Parse image creation options */
    cluster_size = qcow2_opt_get_cluster_size_del(opts, &local_err);
    if (local_err) {
        goto err;
    }

    version = qcow2_opt_get_version_del(opts, &local_err);
    if (local_err) {
        goto err;
    }

    refcount_bits = qcow2_opt_get_refcount_bits_del(opts, version, &local_err);
    if (local_err) {
        goto err;
    }

    optstr = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    prealloc = qapi_enum_parse(&PreallocMode_lookup, optstr,
                               PREALLOC_MODE_OFF, &local_err);
    g_free(optstr);
    if (local_err) {
        goto err;
    }

    optstr = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    has_backing_file = !!optstr;
    g_free(optstr);

    optstr = qemu_opt_get_del(opts, BLOCK_OPT_ENCRYPT_FORMAT);
    has_luks = optstr && strcmp(optstr, "luks") == 0;
    g_free(optstr);

    if (has_luks) {
        g_autoptr(QCryptoBlockCreateOptions) create_opts = NULL;
        QDict *cryptoopts = qcow2_extract_crypto_opts(opts, "luks", errp);
        size_t headerlen;

        create_opts = block_crypto_create_opts_init(cryptoopts, errp);
        qobject_unref(cryptoopts);
        if (!create_opts) {
            goto err;
        }

        if (!qcrypto_block_calculate_payload_offset(create_opts,
                                                    "encrypt.",
                                                    &headerlen,
                                                    &local_err)) {
            goto err;
        }

        luks_payload_size = ROUND_UP(headerlen, cluster_size);
    }

    virtual_size = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);
    virtual_size = ROUND_UP(virtual_size, cluster_size);

    /* Check that virtual disk size is valid */
    l2_tables = DIV_ROUND_UP(virtual_size / cluster_size,
                             cluster_size / sizeof(uint64_t));
    if (l2_tables * sizeof(uint64_t) > QCOW_MAX_L1_SIZE) {
        error_setg(&local_err, "The image size is too large "
                               "(try using a larger cluster size)");
        goto err;
    }

    /* Account for input image */
    if (in_bs) {
        int64_t ssize = bdrv_getlength(in_bs);
        if (ssize < 0) {
            error_setg_errno(&local_err, -ssize,
                             "Unable to get image virtual_size");
            goto err;
        }

        virtual_size = ROUND_UP(ssize, cluster_size);

        if (has_backing_file) {
            /* We don't how much of the backing chain is shared by the input
             * image and the new image file.  In the worst case the new image's
             * backing file has nothing in common with the input image.  Be
             * conservative and assume all clusters need to be written.
             */
            required = virtual_size;
        } else {
            int64_t offset;
            int64_t pnum = 0;

            for (offset = 0; offset < ssize; offset += pnum) {
                int ret;

                ret = bdrv_block_status_above(in_bs, NULL, offset,
                                              ssize - offset, &pnum, NULL,
                                              NULL);
                if (ret < 0) {
                    error_setg_errno(&local_err, -ret,
                                     "Unable to get block status");
                    goto err;
                }

                if (ret & BDRV_BLOCK_ZERO) {
                    /* Skip zero regions (safe with no backing file) */
                } else if ((ret & (BDRV_BLOCK_DATA | BDRV_BLOCK_ALLOCATED)) ==
                           (BDRV_BLOCK_DATA | BDRV_BLOCK_ALLOCATED)) {
                    /* Extend pnum to end of cluster for next iteration */
                    pnum = ROUND_UP(offset + pnum, cluster_size) - offset;

                    /* Count clusters we've seen */
                    required += offset % cluster_size + pnum;
                }
            }
        }
    }

    /* Take into account preallocation.  Nothing special is needed for
     * PREALLOC_MODE_METADATA since metadata is always counted.
     */
    if (prealloc == PREALLOC_MODE_FULL || prealloc == PREALLOC_MODE_FALLOC) {
        required = virtual_size;
    }

    info = g_new0(BlockMeasureInfo, 1);
    info->fully_allocated =
        qcow2_calc_prealloc_size(virtual_size, cluster_size,
                                 ctz32(refcount_bits)) + luks_payload_size;

    /*
     * Remove data clusters that are not required.  This overestimates the
     * required size because metadata needed for the fully allocated file is
     * still counted.  Show bitmaps only if both source and destination
     * would support them.
     */
    info->required = info->fully_allocated - virtual_size + required;
    info->has_bitmaps = version >= 3 && in_bs &&
        bdrv_supports_persistent_dirty_bitmap(in_bs);
    if (info->has_bitmaps) {
        info->bitmaps = qcow2_get_persistent_dirty_bitmap_size(in_bs,
                                                               cluster_size);
    }
    return info;

err:
    error_propagate(errp, local_err);
    return NULL;
}

static int qcow2_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQcow2State *s = bs->opaque;
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = qcow2_vm_state_offset(s);
    return 0;
}

static ImageInfoSpecific *qcow2_get_specific_info(BlockDriverState *bs,
                                                  Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    ImageInfoSpecific *spec_info;
    QCryptoBlockInfo *encrypt_info = NULL;
    Error *local_err = NULL;

    if (s->crypto != NULL) {
        encrypt_info = qcrypto_block_get_info(s->crypto, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return NULL;
        }
    }

    spec_info = g_new(ImageInfoSpecific, 1);
    *spec_info = (ImageInfoSpecific){
        .type  = IMAGE_INFO_SPECIFIC_KIND_QCOW2,
        .u.qcow2.data = g_new0(ImageInfoSpecificQCow2, 1),
    };
    if (s->qcow_version == 2) {
        *spec_info->u.qcow2.data = (ImageInfoSpecificQCow2){
            .compat             = g_strdup("0.10"),
            .refcount_bits      = s->refcount_bits,
        };
    } else if (s->qcow_version == 3) {
        Qcow2BitmapInfoList *bitmaps;
        bitmaps = qcow2_get_bitmap_info_list(bs, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_ImageInfoSpecific(spec_info);
            qapi_free_QCryptoBlockInfo(encrypt_info);
            return NULL;
        }
        *spec_info->u.qcow2.data = (ImageInfoSpecificQCow2){
            .compat             = g_strdup("1.1"),
            .lazy_refcounts     = s->compatible_features &
                                  QCOW2_COMPAT_LAZY_REFCOUNTS,
            .has_lazy_refcounts = true,
            .corrupt            = s->incompatible_features &
                                  QCOW2_INCOMPAT_CORRUPT,
            .has_corrupt        = true,
            .refcount_bits      = s->refcount_bits,
            .has_bitmaps        = !!bitmaps,
            .bitmaps            = bitmaps,
            .has_data_file      = !!s->image_data_file,
            .data_file          = g_strdup(s->image_data_file),
            .has_data_file_raw  = has_data_file(bs),
            .data_file_raw      = data_file_is_raw(bs),
            .compression_type   = s->compression_type,
        };
    } else {
        /* if this assertion fails, this probably means a new version was
         * added without having it covered here */
        assert(false);
    }

    if (encrypt_info) {
        ImageInfoSpecificQCow2Encryption *qencrypt =
            g_new(ImageInfoSpecificQCow2Encryption, 1);
        switch (encrypt_info->format) {
        case Q_CRYPTO_BLOCK_FORMAT_QCOW:
            qencrypt->format = BLOCKDEV_QCOW2_ENCRYPTION_FORMAT_AES;
            break;
        case Q_CRYPTO_BLOCK_FORMAT_LUKS:
            qencrypt->format = BLOCKDEV_QCOW2_ENCRYPTION_FORMAT_LUKS;
            qencrypt->u.luks = encrypt_info->u.luks;
            break;
        default:
            abort();
        }
        /* Since we did shallow copy above, erase any pointers
         * in the original info */
        memset(&encrypt_info->u, 0, sizeof(encrypt_info->u));
        qapi_free_QCryptoBlockInfo(encrypt_info);

        spec_info->u.qcow2.data->has_encrypt = true;
        spec_info->u.qcow2.data->encrypt = qencrypt;
    }

    return spec_info;
}

static int qcow2_has_zero_init(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    bool preallocated;

    if (qemu_in_coroutine()) {
        qemu_co_mutex_lock(&s->lock);
    }
    /*
     * Check preallocation status: Preallocated images have all L2
     * tables allocated, nonpreallocated images have none.  It is
     * therefore enough to check the first one.
     */
    preallocated = s->l1_size > 0 && s->l1_table[0] != 0;
    if (qemu_in_coroutine()) {
        qemu_co_mutex_unlock(&s->lock);
    }

    if (!preallocated) {
        return 1;
    } else if (bs->encrypted) {
        return 0;
    } else {
        return bdrv_has_zero_init(s->data_file->bs);
    }
}

static int qcow2_save_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                              int64_t pos)
{
    BDRVQcow2State *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_SAVE);
    return bs->drv->bdrv_co_pwritev_part(bs, qcow2_vm_state_offset(s) + pos,
                                         qiov->size, qiov, 0, 0);
}

static int qcow2_load_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                              int64_t pos)
{
    BDRVQcow2State *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_LOAD);
    return bs->drv->bdrv_co_preadv_part(bs, qcow2_vm_state_offset(s) + pos,
                                        qiov->size, qiov, 0, 0);
}

/*
 * Downgrades an image's version. To achieve this, any incompatible features
 * have to be removed.
 */
static int qcow2_downgrade(BlockDriverState *bs, int target_version,
                           BlockDriverAmendStatusCB *status_cb, void *cb_opaque,
                           Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int current_version = s->qcow_version;
    int ret;
    int i;

    /* This is qcow2_downgrade(), not qcow2_upgrade() */
    assert(target_version < current_version);

    /* There are no other versions (now) that you can downgrade to */
    assert(target_version == 2);

    if (s->refcount_order != 4) {
        error_setg(errp, "compat=0.10 requires refcount_bits=16");
        return -ENOTSUP;
    }

    if (has_data_file(bs)) {
        error_setg(errp, "Cannot downgrade an image with a data file");
        return -ENOTSUP;
    }

    /*
     * If any internal snapshot has a different size than the current
     * image size, or VM state size that exceeds 32 bits, downgrading
     * is unsafe.  Even though we would still use v3-compliant output
     * to preserve that data, other v2 programs might not realize
     * those optional fields are important.
     */
    for (i = 0; i < s->nb_snapshots; i++) {
        if (s->snapshots[i].vm_state_size > UINT32_MAX ||
            s->snapshots[i].disk_size != bs->total_sectors * BDRV_SECTOR_SIZE) {
            error_setg(errp, "Internal snapshots prevent downgrade of image");
            return -ENOTSUP;
        }
    }

    /* clear incompatible features */
    if (s->incompatible_features & QCOW2_INCOMPAT_DIRTY) {
        ret = qcow2_mark_clean(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to make the image clean");
            return ret;
        }
    }

    /* with QCOW2_INCOMPAT_CORRUPT, it is pretty much impossible to get here in
     * the first place; if that happens nonetheless, returning -ENOTSUP is the
     * best thing to do anyway */

    if (s->incompatible_features) {
        error_setg(errp, "Cannot downgrade an image with incompatible features "
                   "%#" PRIx64 " set", s->incompatible_features);
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
        error_setg_errno(errp, -ret, "Failed to turn zero into data clusters");
        return ret;
    }

    s->qcow_version = target_version;
    ret = qcow2_update_header(bs);
    if (ret < 0) {
        s->qcow_version = current_version;
        error_setg_errno(errp, -ret, "Failed to update the image header");
        return ret;
    }
    return 0;
}

/*
 * Upgrades an image's version.  While newer versions encompass all
 * features of older versions, some things may have to be presented
 * differently.
 */
static int qcow2_upgrade(BlockDriverState *bs, int target_version,
                         BlockDriverAmendStatusCB *status_cb, void *cb_opaque,
                         Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    bool need_snapshot_update;
    int current_version = s->qcow_version;
    int i;
    int ret;

    /* This is qcow2_upgrade(), not qcow2_downgrade() */
    assert(target_version > current_version);

    /* There are no other versions (yet) that you can upgrade to */
    assert(target_version == 3);

    status_cb(bs, 0, 2, cb_opaque);

    /*
     * In v2, snapshots do not need to have extra data.  v3 requires
     * the 64-bit VM state size and the virtual disk size to be
     * present.
     * qcow2_write_snapshots() will always write the list in the
     * v3-compliant format.
     */
    need_snapshot_update = false;
    for (i = 0; i < s->nb_snapshots; i++) {
        if (s->snapshots[i].extra_data_size <
            sizeof_field(QCowSnapshotExtraData, vm_state_size_large) +
            sizeof_field(QCowSnapshotExtraData, disk_size))
        {
            need_snapshot_update = true;
            break;
        }
    }
    if (need_snapshot_update) {
        ret = qcow2_write_snapshots(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to update the snapshot table");
            return ret;
        }
    }
    status_cb(bs, 1, 2, cb_opaque);

    s->qcow_version = target_version;
    ret = qcow2_update_header(bs);
    if (ret < 0) {
        s->qcow_version = current_version;
        error_setg_errno(errp, -ret, "Failed to update the image header");
        return ret;
    }
    status_cb(bs, 2, 2, cb_opaque);

    return 0;
}

typedef enum Qcow2AmendOperation {
    /* This is the value Qcow2AmendHelperCBInfo::last_operation will be
     * statically initialized to so that the helper CB can discern the first
     * invocation from an operation change */
    QCOW2_NO_OPERATION = 0,

    QCOW2_UPGRADING,
    QCOW2_UPDATING_ENCRYPTION,
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
                               void *cb_opaque,
                               bool force,
                               Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int old_version = s->qcow_version, new_version = old_version;
    uint64_t new_size = 0;
    const char *backing_file = NULL, *backing_format = NULL, *data_file = NULL;
    bool lazy_refcounts = s->use_lazy_refcounts;
    bool data_file_raw = data_file_is_raw(bs);
    const char *compat = NULL;
    int refcount_bits = s->refcount_bits;
    int ret;
    QemuOptDesc *desc = opts->list->desc;
    Qcow2AmendHelperCBInfo helper_cb_info;
    bool encryption_update = false;

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
            } else if (!strcmp(compat, "0.10") || !strcmp(compat, "v2")) {
                new_version = 2;
            } else if (!strcmp(compat, "1.1") || !strcmp(compat, "v3")) {
                new_version = 3;
            } else {
                error_setg(errp, "Unknown compatibility level %s", compat);
                return -EINVAL;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_SIZE)) {
            new_size = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 0);
        } else if (!strcmp(desc->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = qemu_opt_get(opts, BLOCK_OPT_BACKING_FILE);
        } else if (!strcmp(desc->name, BLOCK_OPT_BACKING_FMT)) {
            backing_format = qemu_opt_get(opts, BLOCK_OPT_BACKING_FMT);
        } else if (g_str_has_prefix(desc->name, "encrypt.")) {
            if (!s->crypto) {
                error_setg(errp,
                           "Can't amend encryption options - encryption not present");
                return -EINVAL;
            }
            if (s->crypt_method_header != QCOW_CRYPT_LUKS) {
                error_setg(errp,
                           "Only LUKS encryption options can be amended");
                return -ENOTSUP;
            }
            encryption_update = true;
        } else if (!strcmp(desc->name, BLOCK_OPT_LAZY_REFCOUNTS)) {
            lazy_refcounts = qemu_opt_get_bool(opts, BLOCK_OPT_LAZY_REFCOUNTS,
                                               lazy_refcounts);
        } else if (!strcmp(desc->name, BLOCK_OPT_REFCOUNT_BITS)) {
            refcount_bits = qemu_opt_get_number(opts, BLOCK_OPT_REFCOUNT_BITS,
                                                refcount_bits);

            if (refcount_bits <= 0 || refcount_bits > 64 ||
                !is_power_of_2(refcount_bits))
            {
                error_setg(errp, "Refcount width must be a power of two and "
                           "may not exceed 64 bits");
                return -EINVAL;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_DATA_FILE)) {
            data_file = qemu_opt_get(opts, BLOCK_OPT_DATA_FILE);
            if (data_file && !has_data_file(bs)) {
                error_setg(errp, "data-file can only be set for images that "
                                 "use an external data file");
                return -EINVAL;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_DATA_FILE_RAW)) {
            data_file_raw = qemu_opt_get_bool(opts, BLOCK_OPT_DATA_FILE_RAW,
                                              data_file_raw);
            if (data_file_raw && !data_file_is_raw(bs)) {
                error_setg(errp, "data-file-raw cannot be set on existing "
                                 "images");
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
        .total_operations = (new_version != old_version)
                          + (s->refcount_bits != refcount_bits) +
                            (encryption_update == true)
    };

    /* Upgrade first (some features may require compat=1.1) */
    if (new_version > old_version) {
        helper_cb_info.current_operation = QCOW2_UPGRADING;
        ret = qcow2_upgrade(bs, new_version, &qcow2_amend_helper_cb,
                            &helper_cb_info, errp);
        if (ret < 0) {
            return ret;
        }
    }

    if (encryption_update) {
        QDict *amend_opts_dict;
        QCryptoBlockAmendOptions *amend_opts;

        helper_cb_info.current_operation = QCOW2_UPDATING_ENCRYPTION;
        amend_opts_dict = qcow2_extract_crypto_opts(opts, "luks", errp);
        if (!amend_opts_dict) {
            return -EINVAL;
        }
        amend_opts = block_crypto_amend_opts_init(amend_opts_dict, errp);
        qobject_unref(amend_opts_dict);
        if (!amend_opts) {
            return -EINVAL;
        }
        ret = qcrypto_block_amend_options(s->crypto,
                                          qcow2_crypto_hdr_read_func,
                                          qcow2_crypto_hdr_write_func,
                                          bs,
                                          amend_opts,
                                          force,
                                          errp);
        qapi_free_QCryptoBlockAmendOptions(amend_opts);
        if (ret < 0) {
            return ret;
        }
    }

    if (s->refcount_bits != refcount_bits) {
        int refcount_order = ctz32(refcount_bits);

        if (new_version < 3 && refcount_bits != 16) {
            error_setg(errp, "Refcount widths other than 16 bits require "
                       "compatibility level 1.1 or above (use compat=1.1 or "
                       "greater)");
            return -EINVAL;
        }

        helper_cb_info.current_operation = QCOW2_CHANGING_REFCOUNT_ORDER;
        ret = qcow2_change_refcount_order(bs, refcount_order,
                                          &qcow2_amend_helper_cb,
                                          &helper_cb_info, errp);
        if (ret < 0) {
            return ret;
        }
    }

    /* data-file-raw blocks backing files, so clear it first if requested */
    if (data_file_raw) {
        s->autoclear_features |= QCOW2_AUTOCLEAR_DATA_FILE_RAW;
    } else {
        s->autoclear_features &= ~QCOW2_AUTOCLEAR_DATA_FILE_RAW;
    }

    if (data_file) {
        g_free(s->image_data_file);
        s->image_data_file = *data_file ? g_strdup(data_file) : NULL;
    }

    ret = qcow2_update_header(bs);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to update the image header");
        return ret;
    }

    if (backing_file || backing_format) {
        if (g_strcmp0(backing_file, s->image_backing_file) ||
            g_strcmp0(backing_format, s->image_backing_format)) {
            warn_report("Deprecated use of amend to alter the backing file; "
                        "use qemu-img rebase instead");
        }
        ret = qcow2_change_backing_file(bs,
                    backing_file ?: s->image_backing_file,
                    backing_format ?: s->image_backing_format);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to change the backing file");
            return ret;
        }
    }

    if (s->use_lazy_refcounts != lazy_refcounts) {
        if (lazy_refcounts) {
            if (new_version < 3) {
                error_setg(errp, "Lazy refcounts only supported with "
                           "compatibility level 1.1 and above (use compat=1.1 "
                           "or greater)");
                return -EINVAL;
            }
            s->compatible_features |= QCOW2_COMPAT_LAZY_REFCOUNTS;
            ret = qcow2_update_header(bs);
            if (ret < 0) {
                s->compatible_features &= ~QCOW2_COMPAT_LAZY_REFCOUNTS;
                error_setg_errno(errp, -ret, "Failed to update the image header");
                return ret;
            }
            s->use_lazy_refcounts = true;
        } else {
            /* make image clean first */
            ret = qcow2_mark_clean(bs);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to make the image clean");
                return ret;
            }
            /* now disallow lazy refcounts */
            s->compatible_features &= ~QCOW2_COMPAT_LAZY_REFCOUNTS;
            ret = qcow2_update_header(bs);
            if (ret < 0) {
                s->compatible_features |= QCOW2_COMPAT_LAZY_REFCOUNTS;
                error_setg_errno(errp, -ret, "Failed to update the image header");
                return ret;
            }
            s->use_lazy_refcounts = false;
        }
    }

    if (new_size) {
        BlockBackend *blk = blk_new_with_bs(bs, BLK_PERM_RESIZE, BLK_PERM_ALL,
                                            errp);
        if (!blk) {
            return -EPERM;
        }

        /*
         * Amending image options should ensure that the image has
         * exactly the given new values, so pass exact=true here.
         */
        ret = blk_truncate(blk, new_size, true, PREALLOC_MODE_OFF, 0, errp);
        blk_unref(blk);
        if (ret < 0) {
            return ret;
        }
    }

    /* Downgrade last (so unsupported features can be removed before) */
    if (new_version < old_version) {
        helper_cb_info.current_operation = QCOW2_DOWNGRADING;
        ret = qcow2_downgrade(bs, new_version, &qcow2_amend_helper_cb,
                              &helper_cb_info, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int coroutine_fn qcow2_co_amend(BlockDriverState *bs,
                                       BlockdevAmendOptions *opts,
                                       bool force,
                                       Error **errp)
{
    BlockdevAmendOptionsQcow2 *qopts = &opts->u.qcow2;
    BDRVQcow2State *s = bs->opaque;
    int ret = 0;

    if (qopts->has_encrypt) {
        if (!s->crypto) {
            error_setg(errp, "image is not encrypted, can't amend");
            return -EOPNOTSUPP;
        }

        if (qopts->encrypt->format != Q_CRYPTO_BLOCK_FORMAT_LUKS) {
            error_setg(errp,
                       "Amend can't be used to change the qcow2 encryption format");
            return -EOPNOTSUPP;
        }

        if (s->crypt_method_header != QCOW_CRYPT_LUKS) {
            error_setg(errp,
                       "Only LUKS encryption options can be amended for qcow2 with blockdev-amend");
            return -EOPNOTSUPP;
        }

        ret = qcrypto_block_amend_options(s->crypto,
                                          qcow2_crypto_hdr_read_func,
                                          qcow2_crypto_hdr_write_func,
                                          bs,
                                          qopts->encrypt,
                                          force,
                                          errp);
    }
    return ret;
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

    fatal = fatal && bdrv_is_writable(bs);

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
                                          fatal);
    g_free(message);

    if (fatal) {
        qcow2_mark_corrupt(bs);
        bs->drv = NULL; /* make BDS unusable */
    }

    s->signaled_corruption = true;
}

#define QCOW_COMMON_OPTIONS                                         \
    {                                                               \
        .name = BLOCK_OPT_SIZE,                                     \
        .type = QEMU_OPT_SIZE,                                      \
        .help = "Virtual disk size"                                 \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_COMPAT_LEVEL,                             \
        .type = QEMU_OPT_STRING,                                    \
        .help = "Compatibility level (v2 [0.10] or v3 [1.1])"       \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_BACKING_FILE,                             \
        .type = QEMU_OPT_STRING,                                    \
        .help = "File name of a base image"                         \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_BACKING_FMT,                              \
        .type = QEMU_OPT_STRING,                                    \
        .help = "Image format of the base image"                    \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_DATA_FILE,                                \
        .type = QEMU_OPT_STRING,                                    \
        .help = "File name of an external data file"                \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_DATA_FILE_RAW,                            \
        .type = QEMU_OPT_BOOL,                                      \
        .help = "The external data file must stay valid "           \
                "as a raw image"                                    \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_LAZY_REFCOUNTS,                           \
        .type = QEMU_OPT_BOOL,                                      \
        .help = "Postpone refcount updates",                        \
        .def_value_str = "off"                                      \
    },                                                              \
    {                                                               \
        .name = BLOCK_OPT_REFCOUNT_BITS,                            \
        .type = QEMU_OPT_NUMBER,                                    \
        .help = "Width of a reference count entry in bits",         \
        .def_value_str = "16"                                       \
    }

static QemuOptsList qcow2_create_opts = {
    .name = "qcow2-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qcow2_create_opts.head),
    .desc = {
        {                                                               \
            .name = BLOCK_OPT_ENCRYPT,                                  \
            .type = QEMU_OPT_BOOL,                                      \
            .help = "Encrypt the image with format 'aes'. (Deprecated " \
                    "in favor of " BLOCK_OPT_ENCRYPT_FORMAT "=aes)",    \
        },                                                              \
        {                                                               \
            .name = BLOCK_OPT_ENCRYPT_FORMAT,                           \
            .type = QEMU_OPT_STRING,                                    \
            .help = "Encrypt the image, format choices: 'aes', 'luks'", \
        },                                                              \
        BLOCK_CRYPTO_OPT_DEF_KEY_SECRET("encrypt.",                     \
            "ID of secret providing qcow AES key or LUKS passphrase"),  \
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_ALG("encrypt."),               \
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_MODE("encrypt."),              \
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_ALG("encrypt."),                \
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_HASH_ALG("encrypt."),           \
        BLOCK_CRYPTO_OPT_DEF_LUKS_HASH_ALG("encrypt."),                 \
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME("encrypt."),                \
        {                                                               \
            .name = BLOCK_OPT_CLUSTER_SIZE,                             \
            .type = QEMU_OPT_SIZE,                                      \
            .help = "qcow2 cluster size",                               \
            .def_value_str = stringify(DEFAULT_CLUSTER_SIZE)            \
        },                                                              \
        {                                                               \
            .name = BLOCK_OPT_PREALLOC,                                 \
            .type = QEMU_OPT_STRING,                                    \
            .help = "Preallocation mode (allowed values: off, "         \
                    "metadata, falloc, full)"                           \
        },                                                              \
        {                                                               \
            .name = BLOCK_OPT_COMPRESSION_TYPE,                         \
            .type = QEMU_OPT_STRING,                                    \
            .help = "Compression method used for image cluster "        \
                    "compression",                                      \
            .def_value_str = "zlib"                                     \
        },
        QCOW_COMMON_OPTIONS,
        { /* end of list */ }
    }
};

static QemuOptsList qcow2_amend_opts = {
    .name = "qcow2-amend-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qcow2_amend_opts.head),
    .desc = {
        BLOCK_CRYPTO_OPT_DEF_LUKS_STATE("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_KEYSLOT("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_OLD_SECRET("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_NEW_SECRET("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME("encrypt."),
        QCOW_COMMON_OPTIONS,
        { /* end of list */ }
    }
};

static const char *const qcow2_strong_runtime_opts[] = {
    "encrypt." BLOCK_CRYPTO_OPT_QCOW_KEY_SECRET,

    NULL
};

BlockDriver bdrv_qcow2 = {
    .format_name        = "qcow2",
    .instance_size      = sizeof(BDRVQcow2State),
    .bdrv_probe         = qcow2_probe,
    .bdrv_open          = qcow2_open,
    .bdrv_close         = qcow2_close,
    .bdrv_reopen_prepare  = qcow2_reopen_prepare,
    .bdrv_reopen_commit   = qcow2_reopen_commit,
    .bdrv_reopen_commit_post = qcow2_reopen_commit_post,
    .bdrv_reopen_abort    = qcow2_reopen_abort,
    .bdrv_join_options    = qcow2_join_options,
    .bdrv_child_perm      = bdrv_default_perms,
    .bdrv_co_create_opts  = qcow2_co_create_opts,
    .bdrv_co_create       = qcow2_co_create,
    .bdrv_has_zero_init   = qcow2_has_zero_init,
    .bdrv_co_block_status = qcow2_co_block_status,

    .bdrv_co_preadv_part    = qcow2_co_preadv_part,
    .bdrv_co_pwritev_part   = qcow2_co_pwritev_part,
    .bdrv_co_flush_to_os    = qcow2_co_flush_to_os,

    .bdrv_co_pwrite_zeroes  = qcow2_co_pwrite_zeroes,
    .bdrv_co_pdiscard       = qcow2_co_pdiscard,
    .bdrv_co_copy_range_from = qcow2_co_copy_range_from,
    .bdrv_co_copy_range_to  = qcow2_co_copy_range_to,
    .bdrv_co_truncate       = qcow2_co_truncate,
    .bdrv_co_pwritev_compressed_part = qcow2_co_pwritev_compressed_part,
    .bdrv_make_empty        = qcow2_make_empty,

    .bdrv_snapshot_create   = qcow2_snapshot_create,
    .bdrv_snapshot_goto     = qcow2_snapshot_goto,
    .bdrv_snapshot_delete   = qcow2_snapshot_delete,
    .bdrv_snapshot_list     = qcow2_snapshot_list,
    .bdrv_snapshot_load_tmp = qcow2_snapshot_load_tmp,
    .bdrv_measure           = qcow2_measure,
    .bdrv_get_info          = qcow2_get_info,
    .bdrv_get_specific_info = qcow2_get_specific_info,

    .bdrv_save_vmstate    = qcow2_save_vmstate,
    .bdrv_load_vmstate    = qcow2_load_vmstate,

    .is_format                  = true,
    .supports_backing           = true,
    .bdrv_change_backing_file   = qcow2_change_backing_file,

    .bdrv_refresh_limits        = qcow2_refresh_limits,
    .bdrv_co_invalidate_cache   = qcow2_co_invalidate_cache,
    .bdrv_inactivate            = qcow2_inactivate,

    .create_opts         = &qcow2_create_opts,
    .amend_opts          = &qcow2_amend_opts,
    .strong_runtime_opts = qcow2_strong_runtime_opts,
    .mutable_opts        = mutable_opts,
    .bdrv_co_check       = qcow2_co_check,
    .bdrv_amend_options  = qcow2_amend_options,
    .bdrv_co_amend       = qcow2_co_amend,

    .bdrv_detach_aio_context  = qcow2_detach_aio_context,
    .bdrv_attach_aio_context  = qcow2_attach_aio_context,

    .bdrv_supports_persistent_dirty_bitmap =
            qcow2_supports_persistent_dirty_bitmap,
    .bdrv_co_can_store_new_dirty_bitmap = qcow2_co_can_store_new_dirty_bitmap,
    .bdrv_co_remove_persistent_dirty_bitmap =
            qcow2_co_remove_persistent_dirty_bitmap,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
