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
#include "qapi/opts-visitor.h"
#include "qapi-visit.h"
#include "block/crypto.h"

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
#define  QCOW2_EXT_MAGIC_CRYPTO_HEADER 0x0537be77
#define  QCOW2_EXT_MAGIC_BITMAPS 0x23852875

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

    /* Zero fill remaining space in cluster so it has predictable
     * content in case of future spec changes */
    clusterlen = size_to_clusters(s, headerlen) * s->cluster_size;
    ret = bdrv_pwrite_zeroes(bs->file,
                             ret + headerlen,
                             clusterlen - headerlen, 0);
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
            be64_to_cpus(&s->crypto_header.offset);
            be64_to_cpus(&s->crypto_header.length);

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
                                           bs, cflags, errp);
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
                error_report("WARNING: a program lacking bitmap support "
                             "modified this file, so all bitmaps are now "
                             "considered inconsistent. Some clusters may be "
                             "leaked, run 'qemu-img check -r' on the image "
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

            be32_to_cpus(&bitmaps_ext.nb_bitmaps);
            be64_to_cpus(&bitmaps_ext.bitmap_directory_size);
            be64_to_cpus(&bitmaps_ext.bitmap_directory_offset);

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

            if (bitmaps_ext.bitmap_directory_offset & (s->cluster_size - 1)) {
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

        default:
            /* unknown magic - save it in case we need to rewrite the header */
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
    if (offset_into_cluster(s, offset) != 0) {
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
        BLOCK_CRYPTO_OPT_DEF_KEY_SECRET("encrypt.",
            "ID of secret providing qcow2 AES key or LUKS passphrase"),
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
    uint64_t l2_cache_size, refcount_cache_size;
    int i;
    const char *encryptfmt;
    QDict *encryptopts = NULL;
    Error *local_err = NULL;
    int ret;

    qdict_extract_subqdict(options, &encryptopts, "encrypt.");
    encryptfmt = qdict_get_try_str(encryptopts, "format");

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
        qdict_del(encryptopts, "format");
        r->crypto_opts = block_crypto_open_opts_init(
            Q_CRYPTO_BLOCK_FORMAT_QCOW, encryptopts, errp);
        break;

    case QCOW_CRYPT_LUKS:
        if (encryptfmt && !g_str_equal(encryptfmt, "luks")) {
            error_setg(errp,
                       "Header reported 'luks' encryption format but "
                       "options specify '%s'", encryptfmt);
            ret = -EINVAL;
            goto fail;
        }
        qdict_del(encryptopts, "format");
        r->crypto_opts = block_crypto_open_opts_init(
            Q_CRYPTO_BLOCK_FORMAT_LUKS, encryptopts, errp);
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
    QDECREF(encryptopts);
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

    qapi_free_QCryptoBlockOpenOptions(s->crypto_opts);
    s->crypto_opts = r->crypto_opts;
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

static int qcow2_do_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
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
        ret = bdrv_pread(bs->file, s->l1_table_offset, s->l1_table,
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
                              flags, &update_header, &local_err)) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
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
                                           NULL, NULL, cflags, errp);
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
    update_header |= s->autoclear_features & ~QCOW2_AUTOCLEAR_MASK;
    update_header =
        update_header && !bs->read_only && !(flags & BDRV_O_INACTIVE);
    if (update_header) {
        s->autoclear_features &= QCOW2_AUTOCLEAR_MASK;
    }

    if (qcow2_load_autoloading_dirty_bitmaps(bs, &local_err)) {
        update_header = false;
    }
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    if (update_header) {
        ret = qcow2_update_header(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not update qcow2 header");
            goto fail;
        }
    }

    /* Initialise locks */
    qemu_co_mutex_init(&s->lock);
    bs->supported_zero_flags = BDRV_REQ_MAY_UNMAP;

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
    qcrypto_block_free(s->crypto);
    qapi_free_QCryptoBlockOpenOptions(s->crypto_opts);
    return ret;
}

static int qcow2_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    return qcow2_do_open(bs, options, flags, errp);
}

static void qcow2_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;

    if (bs->encrypted) {
        /* Encryption works on a sector granularity */
        bs->bl.request_alignment = BDRV_SECTOR_SIZE;
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
    unsigned int bytes;
    int64_t status = 0;

    bytes = MIN(INT_MAX, nb_sectors * BDRV_SECTOR_SIZE);
    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_get_cluster_offset(bs, sector_num << 9, &bytes,
                                   &cluster_offset);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        return ret;
    }

    *pnum = bytes >> BDRV_SECTOR_BITS;

    if (cluster_offset != 0 && ret != QCOW2_CLUSTER_COMPRESSED &&
        !s->crypto) {
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        cluster_offset |= (index_in_cluster << BDRV_SECTOR_BITS);
        *file = bs->file->bs;
        status |= BDRV_BLOCK_OFFSET_VALID | cluster_offset;
    }
    if (ret == QCOW2_CLUSTER_ZERO_PLAIN || ret == QCOW2_CLUSTER_ZERO_ALLOC) {
        status |= BDRV_BLOCK_ZERO;
    } else if (ret != QCOW2_CLUSTER_UNALLOCATED) {
        status |= BDRV_BLOCK_DATA;
    }
    return status;
}

/* handle reading after the end of the backing file */
int qcow2_backing_read1(BlockDriverState *bs, QEMUIOVector *qiov,
                        int64_t offset, int bytes)
{
    uint64_t bs_size = bs->total_sectors * BDRV_SECTOR_SIZE;
    int n1;

    if ((offset + bytes) <= bs_size) {
        return bytes;
    }

    if (offset >= bs_size) {
        n1 = 0;
    } else {
        n1 = bs_size - offset;
    }

    qemu_iovec_memset(qiov, n1, 0, bytes - n1);

    return n1;
}

static coroutine_fn int qcow2_co_preadv(BlockDriverState *bs, uint64_t offset,
                                        uint64_t bytes, QEMUIOVector *qiov,
                                        int flags)
{
    BDRVQcow2State *s = bs->opaque;
    int offset_in_cluster, n1;
    int ret;
    unsigned int cur_bytes; /* number of bytes in current iteration */
    uint64_t cluster_offset = 0;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;
    uint8_t *cluster_data = NULL;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {

        /* prepare next request */
        cur_bytes = MIN(bytes, INT_MAX);
        if (s->crypto) {
            cur_bytes = MIN(cur_bytes,
                            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
        }

        ret = qcow2_get_cluster_offset(bs, offset, &cur_bytes, &cluster_offset);
        if (ret < 0) {
            goto fail;
        }

        offset_in_cluster = offset_into_cluster(s, offset);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, cur_bytes);

        switch (ret) {
        case QCOW2_CLUSTER_UNALLOCATED:

            if (bs->backing) {
                /* read from the base image */
                n1 = qcow2_backing_read1(bs->backing->bs, &hd_qiov,
                                         offset, cur_bytes);
                if (n1 > 0) {
                    QEMUIOVector local_qiov;

                    qemu_iovec_init(&local_qiov, hd_qiov.niov);
                    qemu_iovec_concat(&local_qiov, &hd_qiov, 0, n1);

                    BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                    qemu_co_mutex_unlock(&s->lock);
                    ret = bdrv_co_preadv(bs->backing, offset, n1,
                                         &local_qiov, 0);
                    qemu_co_mutex_lock(&s->lock);

                    qemu_iovec_destroy(&local_qiov);

                    if (ret < 0) {
                        goto fail;
                    }
                }
            } else {
                /* Note: in this case, no need to wait */
                qemu_iovec_memset(&hd_qiov, 0, 0, cur_bytes);
            }
            break;

        case QCOW2_CLUSTER_ZERO_PLAIN:
        case QCOW2_CLUSTER_ZERO_ALLOC:
            qemu_iovec_memset(&hd_qiov, 0, 0, cur_bytes);
            break;

        case QCOW2_CLUSTER_COMPRESSED:
            /* add AIO support for compressed blocks ? */
            ret = qcow2_decompress_cluster(bs, cluster_offset);
            if (ret < 0) {
                goto fail;
            }

            qemu_iovec_from_buf(&hd_qiov, 0,
                                s->cluster_cache + offset_in_cluster,
                                cur_bytes);
            break;

        case QCOW2_CLUSTER_NORMAL:
            if ((cluster_offset & 511) != 0) {
                ret = -EIO;
                goto fail;
            }

            if (bs->encrypted) {
                assert(s->crypto);

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

                assert(cur_bytes <= QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
                qemu_iovec_reset(&hd_qiov);
                qemu_iovec_add(&hd_qiov, cluster_data, cur_bytes);
            }

            BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
            qemu_co_mutex_unlock(&s->lock);
            ret = bdrv_co_preadv(bs->file,
                                 cluster_offset + offset_in_cluster,
                                 cur_bytes, &hd_qiov, 0);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                goto fail;
            }
            if (bs->encrypted) {
                assert(s->crypto);
                assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
                assert((cur_bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
                Error *err = NULL;
                if (qcrypto_block_decrypt(s->crypto,
                                          (s->crypt_physical_offset ?
                                           cluster_offset + offset_in_cluster :
                                           offset) >> BDRV_SECTOR_BITS,
                                          cluster_data,
                                          cur_bytes,
                                          &err) < 0) {
                    error_free(err);
                    ret = -EIO;
                    goto fail;
                }
                qemu_iovec_from_buf(qiov, bytes_done, cluster_data, cur_bytes);
            }
            break;

        default:
            g_assert_not_reached();
            ret = -EIO;
            goto fail;
        }

        bytes -= cur_bytes;
        offset += cur_bytes;
        bytes_done += cur_bytes;
    }
    ret = 0;

fail:
    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cluster_data);

    return ret;
}

/* Check if it's possible to merge a write request with the writing of
 * the data from the COW regions */
static bool merge_cow(uint64_t offset, unsigned bytes,
                      QEMUIOVector *hd_qiov, QCowL2Meta *l2meta)
{
    QCowL2Meta *m;

    for (m = l2meta; m != NULL; m = m->next) {
        /* If both COW regions are empty then there's nothing to merge */
        if (m->cow_start.nb_bytes == 0 && m->cow_end.nb_bytes == 0) {
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
        if (hd_qiov->niov > IOV_MAX - 2) {
            continue;
        }

        m->data_qiov = hd_qiov;
        return true;
    }

    return false;
}

static coroutine_fn int qcow2_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                         uint64_t bytes, QEMUIOVector *qiov,
                                         int flags)
{
    BDRVQcow2State *s = bs->opaque;
    int offset_in_cluster;
    int ret;
    unsigned int cur_bytes; /* number of sectors in current iteration */
    uint64_t cluster_offset;
    QEMUIOVector hd_qiov;
    uint64_t bytes_done = 0;
    uint8_t *cluster_data = NULL;
    QCowL2Meta *l2meta = NULL;

    trace_qcow2_writev_start_req(qemu_coroutine_self(), offset, bytes);

    qemu_iovec_init(&hd_qiov, qiov->niov);

    s->cluster_cache_offset = -1; /* disable compressed cache */

    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {

        l2meta = NULL;

        trace_qcow2_writev_start_part(qemu_coroutine_self());
        offset_in_cluster = offset_into_cluster(s, offset);
        cur_bytes = MIN(bytes, INT_MAX);
        if (bs->encrypted) {
            cur_bytes = MIN(cur_bytes,
                            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size
                            - offset_in_cluster);
        }

        ret = qcow2_alloc_cluster_offset(bs, offset, &cur_bytes,
                                         &cluster_offset, &l2meta);
        if (ret < 0) {
            goto fail;
        }

        assert((cluster_offset & 511) == 0);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, cur_bytes);

        if (bs->encrypted) {
            Error *err = NULL;
            assert(s->crypto);
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

            if (qcrypto_block_encrypt(s->crypto,
                                      (s->crypt_physical_offset ?
                                       cluster_offset + offset_in_cluster :
                                       offset) >> BDRV_SECTOR_BITS,
                                      cluster_data,
                                      cur_bytes, &err) < 0) {
                error_free(err);
                ret = -EIO;
                goto fail;
            }

            qemu_iovec_reset(&hd_qiov);
            qemu_iovec_add(&hd_qiov, cluster_data, cur_bytes);
        }

        ret = qcow2_pre_write_overlap_check(bs, 0,
                cluster_offset + offset_in_cluster, cur_bytes);
        if (ret < 0) {
            goto fail;
        }

        /* If we need to do COW, check if it's possible to merge the
         * writing of the guest data together with that of the COW regions.
         * If it's not possible (or not necessary) then write the
         * guest data now. */
        if (!merge_cow(offset, cur_bytes, &hd_qiov, l2meta)) {
            qemu_co_mutex_unlock(&s->lock);
            BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
            trace_qcow2_writev_data(qemu_coroutine_self(),
                                    cluster_offset + offset_in_cluster);
            ret = bdrv_co_pwritev(bs->file,
                                  cluster_offset + offset_in_cluster,
                                  cur_bytes, &hd_qiov, 0);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                goto fail;
            }
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

        bytes -= cur_bytes;
        offset += cur_bytes;
        bytes_done += cur_bytes;
        trace_qcow2_writev_done_part(qemu_coroutine_self(), cur_bytes);
    }
    ret = 0;

fail:
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

    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cluster_data);
    trace_qcow2_writev_done_req(qemu_coroutine_self(), ret);

    return ret;
}

static int qcow2_inactivate(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int ret, result = 0;
    Error *local_err = NULL;

    qcow2_store_persistent_dirty_bitmaps(bs, &local_err);
    if (local_err != NULL) {
        result = -EINVAL;
        error_report_err(local_err);
        error_report("Persistent bitmaps are lost for node '%s'",
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
    qcow2_cache_destroy(bs, s->l2_table_cache);
    qcow2_cache_destroy(bs, s->refcount_block_cache);

    qcrypto_block_free(s->crypto);
    s->crypto = NULL;

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
    ret = qcow2_do_open(bs, options, flags, &local_err);
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

    /* Full disk encryption header pointer extension */
    if (s->crypto_header.offset != 0) {
        cpu_to_be64s(&s->crypto_header.offset);
        cpu_to_be64s(&s->crypto_header.length);
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_CRYPTO_HEADER,
                             &s->crypto_header, sizeof(s->crypto_header),
                             buflen);
        be64_to_cpus(&s->crypto_header.offset);
        be64_to_cpus(&s->crypto_header.length);
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

static int qcow2_crypt_method_from_format(const char *encryptfmt)
{
    if (g_str_equal(encryptfmt, "luks")) {
        return QCOW_CRYPT_LUKS;
    } else if (g_str_equal(encryptfmt, "aes")) {
        return QCOW_CRYPT_AES;
    } else {
        return -EINVAL;
    }
}

static int qcow2_set_up_encryption(BlockDriverState *bs, const char *encryptfmt,
                                   QemuOpts *opts, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCryptoBlockCreateOptions *cryptoopts = NULL;
    QCryptoBlock *crypto = NULL;
    int ret = -EINVAL;
    QDict *options, *encryptopts;
    int fmt;

    options = qemu_opts_to_qdict(opts, NULL);
    qdict_extract_subqdict(options, &encryptopts, "encrypt.");
    QDECREF(options);

    fmt = qcow2_crypt_method_from_format(encryptfmt);

    switch (fmt) {
    case QCOW_CRYPT_LUKS:
        cryptoopts = block_crypto_create_opts_init(
            Q_CRYPTO_BLOCK_FORMAT_LUKS, encryptopts, errp);
        break;
    case QCOW_CRYPT_AES:
        cryptoopts = block_crypto_create_opts_init(
            Q_CRYPTO_BLOCK_FORMAT_QCOW, encryptopts, errp);
        break;
    default:
        error_setg(errp, "Unknown encryption format '%s'", encryptfmt);
        break;
    }
    if (!cryptoopts) {
        ret = -EINVAL;
        goto out;
    }
    s->crypt_method_header = fmt;

    crypto = qcrypto_block_create(cryptoopts, "encrypt.",
                                  qcow2_crypto_hdr_init_func,
                                  qcow2_crypto_hdr_write_func,
                                  bs, errp);
    if (!crypto) {
        ret = -EINVAL;
        goto out;
    }

    ret = qcow2_update_header(bs);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        goto out;
    }

 out:
    QDECREF(encryptopts);
    qcrypto_block_free(crypto);
    qapi_free_QCryptoBlockCreateOptions(cryptoopts);
    return ret;
}


/**
 * Preallocates metadata structures for data clusters between @offset (in the
 * guest disk) and @new_length (which is thus generally the new guest disk
 * size).
 *
 * Returns: 0 on success, -errno on failure.
 */
static int preallocate(BlockDriverState *bs,
                       uint64_t offset, uint64_t new_length)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t bytes;
    uint64_t host_offset = 0;
    unsigned int cur_bytes;
    int ret;
    QCowL2Meta *meta;

    if (qemu_in_coroutine()) {
        qemu_co_mutex_lock(&s->lock);
    }

    assert(offset <= new_length);
    bytes = new_length - offset;

    while (bytes) {
        cur_bytes = MIN(bytes, INT_MAX);
        ret = qcow2_alloc_cluster_offset(bs, offset, &cur_bytes,
                                         &host_offset, &meta);
        if (ret < 0) {
            goto done;
        }

        while (meta) {
            QCowL2Meta *next = meta->next;

            ret = qcow2_alloc_cluster_link_l2(bs, meta);
            if (ret < 0) {
                qcow2_free_any_clusters(bs, meta->alloc_offset,
                                        meta->nb_clusters, QCOW2_DISCARD_NEVER);
                goto done;
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
    if (host_offset != 0) {
        uint8_t data = 0;
        ret = bdrv_pwrite(bs->file, (host_offset + cur_bytes) - 1,
                          &data, 1);
        if (ret < 0) {
            goto done;
        }
    }

    ret = 0;

done:
    if (qemu_in_coroutine()) {
        qemu_co_mutex_unlock(&s->lock);
    }
    return ret;
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
    int64_t aligned_total_size = align_offset(total_size, cluster_size);

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

    /* total size of refcount table and blocks */
    meta_size += qcow2_refcount_metadata_size(
            (meta_size + aligned_total_size) / cluster_size,
            cluster_size, refcount_order, false, NULL);

    return meta_size + aligned_total_size;
}

static size_t qcow2_opt_get_cluster_size_del(QemuOpts *opts, Error **errp)
{
    size_t cluster_size;
    int cluster_bits;

    cluster_size = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE,
                                         DEFAULT_CLUSTER_SIZE);
    cluster_bits = ctz32(cluster_size);
    if (cluster_bits < MIN_CLUSTER_BITS || cluster_bits > MAX_CLUSTER_BITS ||
        (1 << cluster_bits) != cluster_size)
    {
        error_setg(errp, "Cluster size must be a power of two between %d and "
                   "%dk", 1 << MIN_CLUSTER_BITS, 1 << (MAX_CLUSTER_BITS - 10));
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

static int qcow2_create2(const char *filename, int64_t total_size,
                         const char *backing_file, const char *backing_format,
                         int flags, size_t cluster_size, PreallocMode prealloc,
                         QemuOpts *opts, int version, int refcount_order,
                         const char *encryptfmt, Error **errp)
{
    QDict *options;

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
        int64_t prealloc_size =
            qcow2_calc_prealloc_size(total_size, cluster_size, refcount_order);
        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, prealloc_size, &error_abort);
        qemu_opt_set(opts, BLOCK_OPT_PREALLOC, PreallocMode_lookup[prealloc],
                     &error_abort);
    }

    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }

    blk = blk_new_open(filename, NULL, NULL,
                       BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL,
                       &local_err);
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
        .cluster_bits               = cpu_to_be32(ctz32(cluster_size)),
        .size                       = cpu_to_be64(0),
        .l1_table_offset            = cpu_to_be64(0),
        .l1_size                    = cpu_to_be32(0),
        .refcount_table_offset      = cpu_to_be64(cluster_size),
        .refcount_table_clusters    = cpu_to_be32(1),
        .refcount_order             = cpu_to_be32(refcount_order),
        .header_length              = cpu_to_be32(sizeof(*header)),
    };

    /* We'll update this to correct value later */
    header->crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);

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
    qdict_put_str(options, "driver", "qcow2");
    blk = blk_new_open(filename, NULL, options,
                       BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_NO_FLUSH,
                       &local_err);
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
    ret = blk_truncate(blk, total_size, PREALLOC_MODE_OFF, errp);
    if (ret < 0) {
        error_prepend(errp, "Could not resize image: ");
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

    /* Want encryption? There you go. */
    if (encryptfmt) {
        ret = qcow2_set_up_encryption(blk_bs(blk), encryptfmt, opts, errp);
        if (ret < 0) {
            goto out;
        }
    }

    /* And if we're supposed to preallocate metadata, do that now */
    if (prealloc != PREALLOC_MODE_OFF) {
        ret = preallocate(blk_bs(blk), 0, total_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not preallocate metadata");
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
    blk = blk_new_open(filename, NULL, options,
                       BDRV_O_RDWR | BDRV_O_NO_BACKING | BDRV_O_NO_IO,
                       &local_err);
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
    int version;
    uint64_t refcount_bits;
    int refcount_order;
    char *encryptfmt = NULL;
    Error *local_err = NULL;
    int ret;

    /* Read out options */
    size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                    BDRV_SECTOR_SIZE);
    backing_file = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    backing_fmt = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FMT);
    encryptfmt = qemu_opt_get_del(opts, BLOCK_OPT_ENCRYPT_FORMAT);
    if (encryptfmt) {
        if (qemu_opt_get(opts, BLOCK_OPT_ENCRYPT)) {
            error_setg(errp, "Options " BLOCK_OPT_ENCRYPT " and "
                       BLOCK_OPT_ENCRYPT_FORMAT " are mutually exclusive");
            ret = -EINVAL;
            goto finish;
        }
    } else if (qemu_opt_get_bool_del(opts, BLOCK_OPT_ENCRYPT, false)) {
        encryptfmt = g_strdup("aes");
    }
    cluster_size = qcow2_opt_get_cluster_size_del(opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto finish;
    }
    buf = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    prealloc = qapi_enum_parse(PreallocMode_lookup, buf,
                               PREALLOC_MODE__MAX, PREALLOC_MODE_OFF,
                               &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto finish;
    }

    version = qcow2_opt_get_version_del(opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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

    refcount_bits = qcow2_opt_get_refcount_bits_del(opts, version, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto finish;
    }

    refcount_order = ctz32(refcount_bits);

    ret = qcow2_create2(filename, size, backing_file, backing_fmt, flags,
                        cluster_size, prealloc, opts, version, refcount_order,
                        encryptfmt, &local_err);
    error_propagate(errp, local_err);

finish:
    g_free(backing_file);
    g_free(backing_fmt);
    g_free(encryptfmt);
    g_free(buf);
    return ret;
}


static bool is_zero_sectors(BlockDriverState *bs, int64_t start,
                            uint32_t count)
{
    int nr;
    BlockDriverState *file;
    int64_t res;

    if (start + count > bs->total_sectors) {
        count = bs->total_sectors - start;
    }

    if (!count) {
        return true;
    }
    res = bdrv_get_block_status_above(bs, NULL, start, count,
                                      &nr, &file);
    return res >= 0 && (res & BDRV_BLOCK_ZERO) && nr == count;
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
        int64_t cl_start = (offset - head) >> BDRV_SECTOR_BITS;
        uint64_t off;
        unsigned int nr;

        assert(head + bytes <= s->cluster_size);

        /* check whether remainder of cluster already reads as zero */
        if (!(is_zero_sectors(bs, cl_start,
                              DIV_ROUND_UP(head, BDRV_SECTOR_SIZE)) &&
              is_zero_sectors(bs, (offset + bytes) >> BDRV_SECTOR_BITS,
                              DIV_ROUND_UP(-tail & (s->cluster_size - 1),
                                           BDRV_SECTOR_SIZE)))) {
            return -ENOTSUP;
        }

        qemu_co_mutex_lock(&s->lock);
        /* We can have new write after previous check */
        offset = cl_start << BDRV_SECTOR_BITS;
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

static int qcow2_truncate(BlockDriverState *bs, int64_t offset,
                          PreallocMode prealloc, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t old_length;
    int64_t new_l1_size;
    int ret;

    if (prealloc != PREALLOC_MODE_OFF && prealloc != PREALLOC_MODE_METADATA &&
        prealloc != PREALLOC_MODE_FALLOC && prealloc != PREALLOC_MODE_FULL)
    {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_lookup[prealloc]);
        return -ENOTSUP;
    }

    if (offset & 511) {
        error_setg(errp, "The new size must be a multiple of 512");
        return -EINVAL;
    }

    /* cannot proceed if image has snapshots */
    if (s->nb_snapshots) {
        error_setg(errp, "Can't resize an image which has snapshots");
        return -ENOTSUP;
    }

    /* cannot proceed if image has bitmaps */
    if (s->nb_bitmaps) {
        /* TODO: resize bitmaps in the image */
        error_setg(errp, "Can't resize an image which has bitmaps");
        return -ENOTSUP;
    }

    old_length = bs->total_sectors * 512;

    /* shrinking is currently not supported */
    if (offset < old_length) {
        error_setg(errp, "qcow2 doesn't support shrinking images yet");
        return -ENOTSUP;
    }

    new_l1_size = size_to_l1(s, offset);
    ret = qcow2_grow_l1_table(bs, new_l1_size, true);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to grow the L1 table");
        return ret;
    }

    switch (prealloc) {
    case PREALLOC_MODE_OFF:
        break;

    case PREALLOC_MODE_METADATA:
        ret = preallocate(bs, old_length, offset);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Preallocation failed");
            return ret;
        }
        break;

    case PREALLOC_MODE_FALLOC:
    case PREALLOC_MODE_FULL:
    {
        int64_t allocation_start, host_offset, guest_offset;
        int64_t clusters_allocated;
        int64_t old_file_size, new_file_size;
        uint64_t nb_new_data_clusters, nb_new_l2_tables;

        old_file_size = bdrv_getlength(bs->file->bs);
        if (old_file_size < 0) {
            error_setg_errno(errp, -old_file_size,
                             "Failed to inquire current file length");
            return ret;
        }

        nb_new_data_clusters = DIV_ROUND_UP(offset - old_length,
                                            s->cluster_size);

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
            return -allocation_start;
        }

        clusters_allocated = qcow2_alloc_clusters_at(bs, allocation_start,
                                                     nb_new_data_clusters);
        if (clusters_allocated < 0) {
            error_setg_errno(errp, -clusters_allocated,
                             "Failed to allocate data clusters");
            return -clusters_allocated;
        }

        assert(clusters_allocated == nb_new_data_clusters);

        /* Allocate the data area */
        new_file_size = allocation_start +
                        nb_new_data_clusters * s->cluster_size;
        ret = bdrv_truncate(bs->file, new_file_size, prealloc, errp);
        if (ret < 0) {
            error_prepend(errp, "Failed to resize underlying file: ");
            qcow2_free_clusters(bs, allocation_start,
                                nb_new_data_clusters * s->cluster_size,
                                QCOW2_DISCARD_OTHER);
            return ret;
        }

        /* Create the necessary L2 entries */
        host_offset = allocation_start;
        guest_offset = old_length;
        while (nb_new_data_clusters) {
            int64_t guest_cluster = guest_offset >> s->cluster_bits;
            int64_t nb_clusters = MIN(nb_new_data_clusters,
                                      s->l2_size - guest_cluster % s->l2_size);
            QCowL2Meta allocation = {
                .offset       = guest_offset,
                .alloc_offset = host_offset,
                .nb_clusters  = nb_clusters,
            };
            qemu_co_queue_init(&allocation.dependent_requests);

            ret = qcow2_alloc_cluster_link_l2(bs, &allocation);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to update L2 tables");
                qcow2_free_clusters(bs, host_offset,
                                    nb_new_data_clusters * s->cluster_size,
                                    QCOW2_DISCARD_OTHER);
                return ret;
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

    if (prealloc != PREALLOC_MODE_OFF) {
        /* Flush metadata before actually changing the image size */
        ret = bdrv_flush(bs);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Failed to flush the preallocated area to disk");
            return ret;
        }
    }

    /* write updated header.size */
    offset = cpu_to_be64(offset);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, size),
                           &offset, sizeof(uint64_t));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to update the image size");
        return ret;
    }

    s->l1_vm_state_index = new_l1_size;
    return 0;
}

/* XXX: put compressed sectors first, then all the cluster aligned
   tables to avoid losing bytes in alignment */
static coroutine_fn int
qcow2_co_pwritev_compressed(BlockDriverState *bs, uint64_t offset,
                            uint64_t bytes, QEMUIOVector *qiov)
{
    BDRVQcow2State *s = bs->opaque;
    QEMUIOVector hd_qiov;
    struct iovec iov;
    z_stream strm;
    int ret, out_len;
    uint8_t *buf, *out_buf;
    int64_t cluster_offset;

    if (bytes == 0) {
        /* align end of file to a sector boundary to ease reading with
           sector based I/Os */
        cluster_offset = bdrv_getlength(bs->file->bs);
        if (cluster_offset < 0) {
            return cluster_offset;
        }
        return bdrv_truncate(bs->file, cluster_offset, PREALLOC_MODE_OFF, NULL);
    }

    buf = qemu_blockalign(bs, s->cluster_size);
    if (bytes != s->cluster_size) {
        if (bytes > s->cluster_size ||
            offset + bytes != bs->total_sectors << BDRV_SECTOR_BITS)
        {
            qemu_vfree(buf);
            return -EINVAL;
        }
        /* Zero-pad last write if image size is not cluster aligned */
        memset(buf + bytes, 0, s->cluster_size - bytes);
    }
    qemu_iovec_to_buf(qiov, 0, buf, bytes);

    out_buf = g_malloc(s->cluster_size);

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
        ret = qcow2_co_pwritev(bs, offset, bytes, qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        goto success;
    }

    qemu_co_mutex_lock(&s->lock);
    cluster_offset =
        qcow2_alloc_compressed_cluster_offset(bs, offset, out_len);
    if (!cluster_offset) {
        qemu_co_mutex_unlock(&s->lock);
        ret = -EIO;
        goto fail;
    }
    cluster_offset &= s->cluster_offset_mask;

    ret = qcow2_pre_write_overlap_check(bs, 0, cluster_offset, out_len);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        goto fail;
    }

    iov = (struct iovec) {
        .iov_base   = out_buf,
        .iov_len    = out_len,
    };
    qemu_iovec_init_external(&hd_qiov, &iov, 1);

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_COMPRESSED);
    ret = bdrv_co_pwritev(bs->file, cluster_offset, out_len, &hd_qiov, 0);
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

    ret = bdrv_truncate(bs->file, (3 + l1_clusters) * s->cluster_size,
                        PREALLOC_MODE_OFF, &local_err);
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
    ret = qcow2_cache_write(bs, s->l2_table_cache);
    if (ret < 0) {
        qemu_co_mutex_unlock(&s->lock);
        return ret;
    }

    if (qcow2_need_accurate_refcounts(s)) {
        ret = qcow2_cache_write(bs, s->refcount_block_cache);
        if (ret < 0) {
            qemu_co_mutex_unlock(&s->lock);
            return ret;
        }
    }
    qemu_co_mutex_unlock(&s->lock);

    return 0;
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
    size_t cluster_size;
    int version;
    char *optstr;
    PreallocMode prealloc;
    bool has_backing_file;

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
    prealloc = qapi_enum_parse(PreallocMode_lookup, optstr,
                               PREALLOC_MODE__MAX, PREALLOC_MODE_OFF,
                               &local_err);
    g_free(optstr);
    if (local_err) {
        goto err;
    }

    optstr = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    has_backing_file = !!optstr;
    g_free(optstr);

    virtual_size = align_offset(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                                cluster_size);

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

        virtual_size = align_offset(ssize, cluster_size);

        if (has_backing_file) {
            /* We don't how much of the backing chain is shared by the input
             * image and the new image file.  In the worst case the new image's
             * backing file has nothing in common with the input image.  Be
             * conservative and assume all clusters need to be written.
             */
            required = virtual_size;
        } else {
            int cluster_sectors = cluster_size / BDRV_SECTOR_SIZE;
            int64_t sector_num;
            int pnum = 0;

            for (sector_num = 0;
                 sector_num < ssize / BDRV_SECTOR_SIZE;
                 sector_num += pnum) {
                int nb_sectors = MIN(ssize / BDRV_SECTOR_SIZE - sector_num,
                                     BDRV_REQUEST_MAX_SECTORS);
                BlockDriverState *file;
                int64_t ret;

                ret = bdrv_get_block_status_above(in_bs, NULL,
                                                  sector_num, nb_sectors,
                                                  &pnum, &file);
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
                    pnum = ROUND_UP(sector_num + pnum, cluster_sectors) -
                           sector_num;

                    /* Count clusters we've seen */
                    required += (sector_num % cluster_sectors + pnum) *
                                BDRV_SECTOR_SIZE;
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

    info = g_new(BlockMeasureInfo, 1);
    info->fully_allocated =
        qcow2_calc_prealloc_size(virtual_size, cluster_size,
                                 ctz32(refcount_bits));

    /* Remove data clusters that are not required.  This overestimates the
     * required size because metadata needed for the fully allocated file is
     * still counted.
     */
    info->required = info->fully_allocated - virtual_size + required;
    return info;

err:
    error_propagate(errp, local_err);
    return NULL;
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
    ImageInfoSpecific *spec_info;
    QCryptoBlockInfo *encrypt_info = NULL;

    if (s->crypto != NULL) {
        encrypt_info = qcrypto_block_get_info(s->crypto, &error_abort);
    }

    spec_info = g_new(ImageInfoSpecific, 1);
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

    if (encrypt_info) {
        ImageInfoSpecificQCow2Encryption *qencrypt =
            g_new(ImageInfoSpecificQCow2Encryption, 1);
        switch (encrypt_info->format) {
        case Q_CRYPTO_BLOCK_FORMAT_QCOW:
            qencrypt->format = BLOCKDEV_QCOW2_ENCRYPTION_FORMAT_AES;
            qencrypt->u.aes = encrypt_info->u.qcow;
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

static int qcow2_save_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                              int64_t pos)
{
    BDRVQcow2State *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_SAVE);
    return bs->drv->bdrv_co_pwritev(bs, qcow2_vm_state_offset(s) + pos,
                                    qiov->size, qiov, 0);
}

static int qcow2_load_vmstate(BlockDriverState *bs, QEMUIOVector *qiov,
                              int64_t pos)
{
    BDRVQcow2State *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_LOAD);
    return bs->drv->bdrv_co_preadv(bs, qcow2_vm_state_offset(s) + pos,
                                   qiov->size, qiov, 0);
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
    int encformat;
    int refcount_bits = s->refcount_bits;
    Error *local_err = NULL;
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
                                        !!s->crypto);

            if (encrypt != !!s->crypto) {
                error_report("Changing the encryption flag is not supported");
                return -ENOTSUP;
            }
        } else if (!strcmp(desc->name, BLOCK_OPT_ENCRYPT_FORMAT)) {
            encformat = qcow2_crypt_method_from_format(
                qemu_opt_get(opts, BLOCK_OPT_ENCRYPT_FORMAT));

            if (encformat != s->crypt_method_header) {
                error_report("Changing the encryption format is not supported");
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

        if (new_version < 3 && refcount_bits != 16) {
            error_report("Different refcount widths than 16 bits require "
                         "compatibility level 1.1 or above (use compat=1.1 or "
                         "greater)");
            return -EINVAL;
        }

        helper_cb_info.current_operation = QCOW2_CHANGING_REFCOUNT_ORDER;
        ret = qcow2_change_refcount_order(bs, refcount_order,
                                          &qcow2_amend_helper_cb,
                                          &helper_cb_info, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
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
        BlockBackend *blk = blk_new(BLK_PERM_RESIZE, BLK_PERM_ALL);
        ret = blk_insert_bs(blk, bs, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            blk_unref(blk);
            return ret;
        }

        ret = blk_truncate(blk, new_size, PREALLOC_MODE_OFF, &local_err);
        blk_unref(blk);
        if (ret < 0) {
            error_report_err(local_err);
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
            .help = "Encrypt the image with format 'aes'. (Deprecated "
                    "in favor of " BLOCK_OPT_ENCRYPT_FORMAT "=aes)",
        },
        {
            .name = BLOCK_OPT_ENCRYPT_FORMAT,
            .type = QEMU_OPT_STRING,
            .help = "Encrypt the image, format choices: 'aes', 'luks'",
        },
        BLOCK_CRYPTO_OPT_DEF_KEY_SECRET("encrypt.",
            "ID of secret providing qcow AES key or LUKS passphrase"),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_ALG("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_MODE("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_ALG("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_HASH_ALG("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_HASH_ALG("encrypt."),
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME("encrypt."),
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
    .bdrv_child_perm      = bdrv_format_default_perms,
    .bdrv_create        = qcow2_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_co_get_block_status = qcow2_co_get_block_status,

    .bdrv_co_preadv         = qcow2_co_preadv,
    .bdrv_co_pwritev        = qcow2_co_pwritev,
    .bdrv_co_flush_to_os    = qcow2_co_flush_to_os,

    .bdrv_co_pwrite_zeroes  = qcow2_co_pwrite_zeroes,
    .bdrv_co_pdiscard       = qcow2_co_pdiscard,
    .bdrv_truncate          = qcow2_truncate,
    .bdrv_co_pwritev_compressed = qcow2_co_pwritev_compressed,
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

    .bdrv_reopen_bitmaps_rw = qcow2_reopen_bitmaps_rw,
    .bdrv_can_store_new_dirty_bitmap = qcow2_can_store_new_dirty_bitmap,
    .bdrv_remove_persistent_dirty_bitmap = qcow2_remove_persistent_dirty_bitmap,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
