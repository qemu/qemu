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
#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include <zlib.h>
#include "qemu/aes.h"
#include "block/qcow2.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qbool.h"
#include "trace.h"
#include "qemu/option_int.h"

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
    BDRVQcowState *s = bs->opaque;
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
        if (ext.len > end_offset - offset) {
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
    BDRVQcowState *s = bs->opaque;
    Qcow2UnknownHeaderExtension *uext, *next;

    QLIST_FOREACH_SAFE(uext, &s->unknown_header_ext, next, next) {
        QLIST_REMOVE(uext, next);
        g_free(uext);
    }
}

static void GCC_FMT_ATTR(3, 4) report_unsupported(BlockDriverState *bs,
    Error **errp, const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    error_set(errp, QERR_UNKNOWN_BLOCK_FORMAT_FEATURE, bs->device_name, "qcow2",
              msg);
}

static void report_unsupported_feature(BlockDriverState *bs,
    Error **errp, Qcow2Feature *table, uint64_t mask)
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

    report_unsupported(bs, errp, "%s", features);
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
    BDRVQcowState *s = bs->opaque;
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
    ret = bdrv_flush(bs->file);
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
    BDRVQcowState *s = bs->opaque;

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
    BDRVQcowState *s = bs->opaque;

    s->incompatible_features |= QCOW2_INCOMPAT_CORRUPT;
    return qcow2_update_header(bs);
}

/*
 * Marks the image as consistent, i.e., unsets the corrupt bit, and flushes
 * before if necessary.
 */
int qcow2_mark_consistent(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;

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
    BDRVQcowState *s = bs->opaque;
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

static int qcow2_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVQcowState *s = bs->opaque;
    unsigned int len, i;
    int ret = 0;
    QCowHeader header;
    QemuOpts *opts;
    Error *local_err = NULL;
    uint64_t ext_end;
    uint64_t l1_vm_state_index;
    const char *opt_overlap_check;
    int overlap_check_template = 0;

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
        report_unsupported(bs, errp, "QCOW version %" PRIu32, header.version);
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
                              &feature_table, NULL);
        report_unsupported_feature(bs, errp, feature_table,
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
    if (header.refcount_order != 4) {
        report_unsupported(bs, errp, "%d bit reference counts",
                           1 << header.refcount_order);
        ret = -ENOTSUP;
        goto fail;
    }
    s->refcount_order = header.refcount_order;

    if (header.crypt_method > QCOW_CRYPT_AES) {
        error_setg(errp, "Unsupported encryption method: %" PRIu32,
                   header.crypt_method);
        ret = -EINVAL;
        goto fail;
    }
    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header) {
        bs->encrypted = 1;
    }

    s->l2_bits = s->cluster_bits - 3; /* L2 is always one cluster */
    s->l2_size = 1 << s->l2_bits;
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
    if (header.l1_size > QCOW_MAX_L1_SIZE) {
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
        s->l1_table = g_malloc0(
            align_offset(s->l1_size * sizeof(uint64_t), 512));
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

    /* alloc L2 table/refcount block cache */
    s->l2_table_cache = qcow2_cache_create(bs, L2_CACHE_SIZE);
    s->refcount_block_cache = qcow2_cache_create(bs, REFCOUNT_CACHE_SIZE);

    s->cluster_cache = g_malloc(s->cluster_size);
    /* one more sector for decompressed data alignment */
    s->cluster_data = qemu_blockalign(bs, QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size
                                  + 512);
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
        if (len > MIN(1023, s->cluster_size - header.backing_file_offset)) {
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
    if (!bs->read_only && !(flags & BDRV_O_INCOMING) && s->autoclear_features) {
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
    if (!(flags & (BDRV_O_CHECK | BDRV_O_INCOMING)) && !bs->read_only &&
        (s->incompatible_features & QCOW2_INCOMPAT_DIRTY)) {
        BdrvCheckResult result = {0};

        ret = qcow2_check(bs, &result, BDRV_FIX_ERRORS);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not repair dirty image");
            goto fail;
        }
    }

    /* Enable lazy_refcounts according to image and command line options */
    opts = qemu_opts_create(&qcow2_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    s->use_lazy_refcounts = qemu_opt_get_bool(opts, QCOW2_OPT_LAZY_REFCOUNTS,
        (s->compatible_features & QCOW2_COMPAT_LAZY_REFCOUNTS));

    s->discard_passthrough[QCOW2_DISCARD_NEVER] = false;
    s->discard_passthrough[QCOW2_DISCARD_ALWAYS] = true;
    s->discard_passthrough[QCOW2_DISCARD_REQUEST] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_REQUEST,
                          flags & BDRV_O_UNMAP);
    s->discard_passthrough[QCOW2_DISCARD_SNAPSHOT] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_SNAPSHOT, true);
    s->discard_passthrough[QCOW2_DISCARD_OTHER] =
        qemu_opt_get_bool(opts, QCOW2_OPT_DISCARD_OTHER, false);

    opt_overlap_check = qemu_opt_get(opts, "overlap-check") ?: "cached";
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
                   "'overlap-check'. Allowed are either of the following: "
                   "none, constant, cached, all", opt_overlap_check);
        qemu_opts_del(opts);
        ret = -EINVAL;
        goto fail;
    }

    s->overlap_check = 0;
    for (i = 0; i < QCOW2_OL_MAX_BITNR; i++) {
        /* overlap-check defines a template bitmask, but every flag may be
         * overwritten through the associated boolean option */
        s->overlap_check |=
            qemu_opt_get_bool(opts, overlap_bool_option_names[i],
                              overlap_check_template & (1 << i)) << i;
    }

    qemu_opts_del(opts);

    if (s->use_lazy_refcounts && s->qcow_version < 3) {
        error_setg(errp, "Lazy refcounts require a qcow2 image with at least "
                   "qemu 1.1 compatibility level");
        ret = -EINVAL;
        goto fail;
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
    g_free(s->l1_table);
    /* else pre-write overlap checks in cache_destroy may crash */
    s->l1_table = NULL;
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
    BDRVQcowState *s = bs->opaque;

    bs->bl.write_zeroes_alignment = s->cluster_sectors;
}

static int qcow2_set_key(BlockDriverState *bs, const char *key)
{
    BDRVQcowState *s = bs->opaque;
    uint8_t keybuf[16];
    int len, i;

    memset(keybuf, 0, 16);
    len = strlen(key);
    if (len > 16)
        len = 16;
    /* XXX: we could compress the chars to 7 bits to increase
       entropy */
    for(i = 0;i < len;i++) {
        keybuf[i] = key[i];
    }
    s->crypt_method = s->crypt_method_header;

    if (AES_set_encrypt_key(keybuf, 128, &s->aes_encrypt_key) != 0)
        return -1;
    if (AES_set_decrypt_key(keybuf, 128, &s->aes_decrypt_key) != 0)
        return -1;
#if 0
    /* test */
    {
        uint8_t in[16];
        uint8_t out[16];
        uint8_t tmp[16];
        for(i=0;i<16;i++)
            in[i] = i;
        AES_encrypt(in, tmp, &s->aes_encrypt_key);
        AES_decrypt(tmp, out, &s->aes_decrypt_key);
        for(i = 0; i < 16; i++)
            printf(" %02x", tmp[i]);
        printf("\n");
        for(i = 0; i < 16; i++)
            printf(" %02x", out[i]);
        printf("\n");
    }
#endif
    return 0;
}

/* We have no actual commit/abort logic for qcow2, but we need to write out any
 * unwritten data if we reopen read-only. */
static int qcow2_reopen_prepare(BDRVReopenState *state,
                                BlockReopenQueue *queue, Error **errp)
{
    int ret;

    if ((state->flags & BDRV_O_RDWR) == 0) {
        ret = bdrv_flush(state->bs);
        if (ret < 0) {
            return ret;
        }

        ret = qcow2_mark_clean(state->bs);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int64_t coroutine_fn qcow2_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum)
{
    BDRVQcowState *s = bs->opaque;
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
        !s->crypt_method) {
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        cluster_offset |= (index_in_cluster << BDRV_SECTOR_BITS);
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
    BDRVQcowState *s = bs->opaque;
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
        if (s->crypt_method) {
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

            if (bs->backing_hd) {
                /* read from the base image */
                n1 = qcow2_backing_read1(bs->backing_hd, &hd_qiov,
                    sector_num, cur_nr_sectors);
                if (n1 > 0) {
                    QEMUIOVector local_qiov;

                    qemu_iovec_init(&local_qiov, hd_qiov.niov);
                    qemu_iovec_concat(&local_qiov, &hd_qiov, 0,
                                      n1 * BDRV_SECTOR_SIZE);

                    BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                    qemu_co_mutex_unlock(&s->lock);
                    ret = bdrv_co_readv(bs->backing_hd, sector_num,
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

            if (s->crypt_method) {
                /*
                 * For encrypted images, read everything into a temporary
                 * contiguous buffer on which the AES functions can work.
                 */
                if (!cluster_data) {
                    cluster_data =
                        qemu_blockalign(bs, QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
                }

                assert(cur_nr_sectors <=
                    QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors);
                qemu_iovec_reset(&hd_qiov);
                qemu_iovec_add(&hd_qiov, cluster_data,
                    512 * cur_nr_sectors);
            }

            BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
            qemu_co_mutex_unlock(&s->lock);
            ret = bdrv_co_readv(bs->file,
                                (cluster_offset >> 9) + index_in_cluster,
                                cur_nr_sectors, &hd_qiov);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                goto fail;
            }
            if (s->crypt_method) {
                qcow2_encrypt_sectors(s, sector_num,  cluster_data,
                    cluster_data, cur_nr_sectors, 0, &s->aes_decrypt_key);
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
    BDRVQcowState *s = bs->opaque;
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
        if (s->crypt_method &&
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

        if (s->crypt_method) {
            if (!cluster_data) {
                cluster_data = qemu_blockalign(bs, QCOW_MAX_CRYPT_CLUSTERS *
                                                 s->cluster_size);
            }

            assert(hd_qiov.size <=
                   QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
            qemu_iovec_to_buf(&hd_qiov, 0, cluster_data, hd_qiov.size);

            qcow2_encrypt_sectors(s, sector_num, cluster_data,
                cluster_data, cur_nr_sectors, 1, &s->aes_encrypt_key);

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
        ret = bdrv_co_writev(bs->file,
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

static void qcow2_close(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    g_free(s->l1_table);
    /* else pre-write overlap checks in cache_destroy may crash */
    s->l1_table = NULL;

    if (!(bs->open_flags & BDRV_O_INCOMING)) {
        qcow2_cache_flush(bs, s->l2_table_cache);
        qcow2_cache_flush(bs, s->refcount_block_cache);

        qcow2_mark_clean(bs);
    }

    qcow2_cache_destroy(bs, s->l2_table_cache);
    qcow2_cache_destroy(bs, s->refcount_block_cache);

    g_free(s->unknown_header_fields);
    cleanup_unknown_header_ext(bs);

    g_free(s->cluster_cache);
    qemu_vfree(s->cluster_data);
    qcow2_refcount_close(bs);
    qcow2_free_snapshots(bs);
}

static void qcow2_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    BDRVQcowState *s = bs->opaque;
    int flags = s->flags;
    AES_KEY aes_encrypt_key;
    AES_KEY aes_decrypt_key;
    uint32_t crypt_method = 0;
    QDict *options;
    Error *local_err = NULL;
    int ret;

    /*
     * Backing files are read-only which makes all of their metadata immutable,
     * that means we don't have to worry about reopening them here.
     */

    if (s->crypt_method) {
        crypt_method = s->crypt_method;
        memcpy(&aes_encrypt_key, &s->aes_encrypt_key, sizeof(aes_encrypt_key));
        memcpy(&aes_decrypt_key, &s->aes_decrypt_key, sizeof(aes_decrypt_key));
    }

    qcow2_close(bs);

    bdrv_invalidate_cache(bs->file, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memset(s, 0, sizeof(BDRVQcowState));
    options = qdict_clone_shallow(bs->options);

    ret = qcow2_open(bs, options, flags, &local_err);
    QDECREF(options);
    if (local_err) {
        error_setg(errp, "Could not reopen qcow2 layer: %s",
                   error_get_pretty(local_err));
        error_free(local_err);
        return;
    } else if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not reopen qcow2 layer");
        return;
    }

    if (crypt_method) {
        s->crypt_method = crypt_method;
        memcpy(&s->aes_encrypt_key, &aes_encrypt_key, sizeof(aes_encrypt_key));
        memcpy(&s->aes_decrypt_key, &aes_decrypt_key, sizeof(aes_decrypt_key));
    }
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
    BDRVQcowState *s = bs->opaque;
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
    if (*bs->backing_format) {
        ret = header_ext_add(buf, QCOW2_EXT_MAGIC_BACKING_FORMAT,
                             bs->backing_format, strlen(bs->backing_format),
                             buflen);
        if (ret < 0) {
            goto fail;
        }

        buf += ret;
        buflen -= ret;
    }

    /* Feature table */
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
    if (*bs->backing_file) {
        size_t backing_file_len = strlen(bs->backing_file);

        if (buflen < backing_file_len) {
            ret = -ENOSPC;
            goto fail;
        }

        /* Using strncpy is ok here, since buf is not NUL-terminated. */
        strncpy(buf, bs->backing_file, buflen);

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
    pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_file ?: "");
    pstrcpy(bs->backing_format, sizeof(bs->backing_format), backing_fmt ?: "");

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
        ret = bdrv_write(bs->file, (host_offset >> BDRV_SECTOR_BITS) + num - 1,
                         buf, 1);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int qcow2_create2(const char *filename, int64_t total_size,
                         const char *backing_file, const char *backing_format,
                         int flags, size_t cluster_size, int prealloc,
                         QemuOpts *opts, int version,
                         Error **errp)
{
    /* Calculate cluster_bits */
    int cluster_bits;
    cluster_bits = ffs(cluster_size) - 1;
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
    BlockDriverState* bs;
    QCowHeader *header;
    uint64_t* refcount_table;
    Error *local_err = NULL;
    int ret;

    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }

    bs = NULL;
    ret = bdrv_open(&bs, filename, NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                    NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }

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
        .refcount_order             = cpu_to_be32(3 + REFCOUNT_SHIFT),
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

    ret = bdrv_pwrite(bs, 0, header, cluster_size);
    g_free(header);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write qcow2 header");
        goto out;
    }

    /* Write a refcount table with one refcount block */
    refcount_table = g_malloc0(2 * cluster_size);
    refcount_table[0] = cpu_to_be64(2 * cluster_size);
    ret = bdrv_pwrite(bs, cluster_size, refcount_table, 2 * cluster_size);
    g_free(refcount_table);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write refcount table");
        goto out;
    }

    bdrv_unref(bs);
    bs = NULL;

    /*
     * And now open the image and make it consistent first (i.e. increase the
     * refcount of the cluster that is occupied by the header and the refcount
     * table)
     */
    BlockDriver* drv = bdrv_find_format("qcow2");
    assert(drv != NULL);
    ret = bdrv_open(&bs, filename, NULL, NULL,
        BDRV_O_RDWR | BDRV_O_CACHE_WB | BDRV_O_NO_FLUSH, drv, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto out;
    }

    ret = qcow2_alloc_clusters(bs, 3 * cluster_size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not allocate clusters for qcow2 "
                         "header and refcount table");
        goto out;

    } else if (ret != 0) {
        error_report("Huh, first cluster in empty image is already in use?");
        abort();
    }

    /* Okay, now that we have a valid image, let's give it the right size */
    ret = bdrv_truncate(bs, total_size * BDRV_SECTOR_SIZE);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not resize image");
        goto out;
    }

    /* Want a backing file? There you go.*/
    if (backing_file) {
        ret = bdrv_change_backing_file(bs, backing_file, backing_format);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not assign backing file '%s' "
                             "with format '%s'", backing_file, backing_format);
            goto out;
        }
    }

    /* And if we're supposed to preallocate metadata, do that now */
    if (prealloc) {
        BDRVQcowState *s = bs->opaque;
        qemu_co_mutex_lock(&s->lock);
        ret = preallocate(bs);
        qemu_co_mutex_unlock(&s->lock);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not preallocate metadata");
            goto out;
        }
    }

    bdrv_unref(bs);
    bs = NULL;

    /* Reopen the image without BDRV_O_NO_FLUSH to flush it before returning */
    ret = bdrv_open(&bs, filename, NULL, NULL,
                    BDRV_O_RDWR | BDRV_O_CACHE_WB | BDRV_O_NO_BACKING,
                    drv, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    ret = 0;
out:
    if (bs) {
        bdrv_unref(bs);
    }
    return ret;
}

static int qcow2_create(const char *filename, QemuOpts *opts, Error **errp)
{
    char *backing_file = NULL;
    char *backing_fmt = NULL;
    char *buf = NULL;
    uint64_t sectors = 0;
    int flags = 0;
    size_t cluster_size = DEFAULT_CLUSTER_SIZE;
    int prealloc = 0;
    int version = 3;
    Error *local_err = NULL;
    int ret;

    /* Read out options */
    sectors = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0) / 512;
    backing_file = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    backing_fmt = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FMT);
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_ENCRYPT, false)) {
        flags |= BLOCK_FLAG_ENCRYPT;
    }
    cluster_size = qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE,
                                         DEFAULT_CLUSTER_SIZE);
    buf = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    if (!buf || !strcmp(buf, "off")) {
        prealloc = 0;
    } else if (!strcmp(buf, "metadata")) {
        prealloc = 1;
    } else {
        error_setg(errp, "Invalid preallocation mode: '%s'", buf);
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

    if (backing_file && prealloc) {
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

    ret = qcow2_create2(filename, sectors, backing_file, backing_fmt, flags,
                        cluster_size, prealloc, opts, version, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }

finish:
    g_free(backing_file);
    g_free(backing_fmt);
    g_free(buf);
    return ret;
}

static coroutine_fn int qcow2_co_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags)
{
    int ret;
    BDRVQcowState *s = bs->opaque;

    /* Emulate misaligned zero writes */
    if (sector_num % s->cluster_sectors || nb_sectors % s->cluster_sectors) {
        return -ENOTSUP;
    }

    /* Whatever is left can use real zero clusters */
    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_zero_clusters(bs, sector_num << BDRV_SECTOR_BITS,
        nb_sectors);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static coroutine_fn int qcow2_co_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors)
{
    int ret;
    BDRVQcowState *s = bs->opaque;

    qemu_co_mutex_lock(&s->lock);
    ret = qcow2_discard_clusters(bs, sector_num << BDRV_SECTOR_BITS,
        nb_sectors, QCOW2_DISCARD_REQUEST);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int qcow2_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVQcowState *s = bs->opaque;
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
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, size),
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
    BDRVQcowState *s = bs->opaque;
    z_stream strm;
    int ret, out_len;
    uint8_t *out_buf;
    uint64_t cluster_offset;

    if (nb_sectors == 0) {
        /* align end of file to a sector boundary to ease reading with
           sector based I/Os */
        cluster_offset = bdrv_getlength(bs->file);
        cluster_offset = (cluster_offset + 511) & ~511;
        bdrv_truncate(bs->file, cluster_offset);
        return 0;
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
        ret = bdrv_pwrite(bs->file, cluster_offset, out_buf, out_len);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;
fail:
    g_free(out_buf);
    return ret;
}

static coroutine_fn int qcow2_co_flush_to_os(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
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
    BDRVQcowState *s = bs->opaque;
    bdi->unallocated_blocks_are_zero = true;
    bdi->can_write_zeroes_with_unmap = (s->qcow_version >= 3);
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = qcow2_vm_state_offset(s);
    return 0;
}

static ImageInfoSpecific *qcow2_get_specific_info(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    ImageInfoSpecific *spec_info = g_new(ImageInfoSpecific, 1);

    *spec_info = (ImageInfoSpecific){
        .kind  = IMAGE_INFO_SPECIFIC_KIND_QCOW2,
        {
            .qcow2 = g_new(ImageInfoSpecificQCow2, 1),
        },
    };
    if (s->qcow_version == 2) {
        *spec_info->qcow2 = (ImageInfoSpecificQCow2){
            .compat = g_strdup("0.10"),
        };
    } else if (s->qcow_version == 3) {
        *spec_info->qcow2 = (ImageInfoSpecificQCow2){
            .compat             = g_strdup("1.1"),
            .lazy_refcounts     = s->compatible_features &
                                  QCOW2_COMPAT_LAZY_REFCOUNTS,
            .has_lazy_refcounts = true,
        };
    }

    return spec_info;
}

#if 0
static void dump_refcounts(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int64_t nb_clusters, k, k1, size;
    int refcount;

    size = bdrv_getlength(bs->file);
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
    BDRVQcowState *s = bs->opaque;
    int64_t total_sectors = bs->total_sectors;
    int growable = bs->growable;
    bool zero_beyond_eof = bs->zero_beyond_eof;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_SAVE);
    bs->growable = 1;
    bs->zero_beyond_eof = false;
    ret = bdrv_pwritev(bs, qcow2_vm_state_offset(s) + pos, qiov);
    bs->growable = growable;
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
    BDRVQcowState *s = bs->opaque;
    int growable = bs->growable;
    bool zero_beyond_eof = bs->zero_beyond_eof;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_LOAD);
    bs->growable = 1;
    bs->zero_beyond_eof = false;
    ret = bdrv_pread(bs, qcow2_vm_state_offset(s) + pos, buf, size);
    bs->growable = growable;
    bs->zero_beyond_eof = zero_beyond_eof;

    return ret;
}

/*
 * Downgrades an image's version. To achieve this, any incompatible features
 * have to be removed.
 */
static int qcow2_downgrade(BlockDriverState *bs, int target_version)
{
    BDRVQcowState *s = bs->opaque;
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
        /* we would have to convert the image to a refcount_order == 4 image
         * here; however, since qemu (at the time of writing this) does not
         * support anything different than 4 anyway, there is no point in doing
         * so right now; however, we should error out (if qemu supports this in
         * the future and this code has not been adapted) */
        error_report("qcow2_downgrade: Image refcount orders other than 4 are "
                     "currently not supported.");
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

    ret = qcow2_expand_zero_clusters(bs);
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

static int qcow2_amend_options(BlockDriverState *bs, QemuOpts *opts)
{
    BDRVQcowState *s = bs->opaque;
    int old_version = s->qcow_version, new_version = old_version;
    uint64_t new_size = 0;
    const char *backing_file = NULL, *backing_format = NULL;
    bool lazy_refcounts = s->use_lazy_refcounts;
    const char *compat = NULL;
    uint64_t cluster_size = s->cluster_size;
    bool encrypt;
    int ret;
    QemuOptDesc *desc = opts->list->desc;

    while (desc && desc->name) {
        if (!qemu_opt_find(opts, desc->name)) {
            /* only change explicitly defined options */
            desc++;
            continue;
        }

        if (!strcmp(desc->name, "compat")) {
            compat = qemu_opt_get(opts, "compat");
            if (!compat) {
                /* preserve default */
            } else if (!strcmp(compat, "0.10")) {
                new_version = 2;
            } else if (!strcmp(compat, "1.1")) {
                new_version = 3;
            } else {
                fprintf(stderr, "Unknown compatibility level %s.\n", compat);
                return -EINVAL;
            }
        } else if (!strcmp(desc->name, "preallocation")) {
            fprintf(stderr, "Cannot change preallocation mode.\n");
            return -ENOTSUP;
        } else if (!strcmp(desc->name, "size")) {
            new_size = qemu_opt_get_size(opts, "size", 0);
        } else if (!strcmp(desc->name, "backing_file")) {
            backing_file = qemu_opt_get(opts, "backing_file");
        } else if (!strcmp(desc->name, "backing_fmt")) {
            backing_format = qemu_opt_get(opts, "backing_fmt");
        } else if (!strcmp(desc->name, "encryption")) {
            encrypt = qemu_opt_get_bool(opts, "encryption", s->crypt_method);
            if (encrypt != !!s->crypt_method) {
                fprintf(stderr, "Changing the encryption flag is not "
                        "supported.\n");
                return -ENOTSUP;
            }
        } else if (!strcmp(desc->name, "cluster_size")) {
            cluster_size = qemu_opt_get_size(opts, "cluster_size",
                                             cluster_size);
            if (cluster_size != s->cluster_size) {
                fprintf(stderr, "Changing the cluster size is not "
                        "supported.\n");
                return -ENOTSUP;
            }
        } else if (!strcmp(desc->name, "lazy_refcounts")) {
            lazy_refcounts = qemu_opt_get_bool(opts, "lazy_refcounts",
                                               lazy_refcounts);
        } else {
            /* if this assertion fails, this probably means a new option was
             * added without having it covered here */
            assert(false);
        }

        desc++;
    }

    if (new_version != old_version) {
        if (new_version > old_version) {
            /* Upgrade */
            s->qcow_version = new_version;
            ret = qcow2_update_header(bs);
            if (ret < 0) {
                s->qcow_version = old_version;
                return ret;
            }
        } else {
            ret = qcow2_downgrade(bs, new_version);
            if (ret < 0) {
                return ret;
            }
        }
    }

    if (backing_file || backing_format) {
        ret = qcow2_change_backing_file(bs, backing_file ?: bs->backing_file,
                                        backing_format ?: bs->backing_format);
        if (ret < 0) {
            return ret;
        }
    }

    if (s->use_lazy_refcounts != lazy_refcounts) {
        if (lazy_refcounts) {
            if (s->qcow_version < 3) {
                fprintf(stderr, "Lazy refcounts only supported with compatibility "
                        "level 1.1 and above (use compat=1.1 or greater)\n");
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

    return 0;
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
            .help = "Preallocation mode (allowed values: off, metadata)"
        },
        {
            .name = BLOCK_OPT_LAZY_REFCOUNTS,
            .type = QEMU_OPT_BOOL,
            .help = "Postpone refcount updates",
            .def_value_str = "off"
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_qcow2 = {
    .format_name        = "qcow2",
    .instance_size      = sizeof(BDRVQcowState),
    .bdrv_probe         = qcow2_probe,
    .bdrv_open          = qcow2_open,
    .bdrv_close         = qcow2_close,
    .bdrv_reopen_prepare  = qcow2_reopen_prepare,
    .bdrv_create        = qcow2_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_co_get_block_status = qcow2_co_get_block_status,
    .bdrv_set_key       = qcow2_set_key,

    .bdrv_co_readv          = qcow2_co_readv,
    .bdrv_co_writev         = qcow2_co_writev,
    .bdrv_co_flush_to_os    = qcow2_co_flush_to_os,

    .bdrv_co_write_zeroes   = qcow2_co_write_zeroes,
    .bdrv_co_discard        = qcow2_co_discard,
    .bdrv_truncate          = qcow2_truncate,
    .bdrv_write_compressed  = qcow2_write_compressed,

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

    .create_opts         = &qcow2_create_opts,
    .bdrv_check          = qcow2_check,
    .bdrv_amend_options  = qcow2_amend_options,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
