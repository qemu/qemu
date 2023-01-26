/*
 * Block driver for the QCOW format
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include <zlib.h>
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "crypto/block.h"
#include "migration/blocker.h"
#include "crypto.h"

/**************************************************************/
/* QEMU COW block driver with compression and encryption support */

#define QCOW_MAGIC (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xfb)
#define QCOW_VERSION 1

#define QCOW_CRYPT_NONE 0
#define QCOW_CRYPT_AES  1

#define QCOW_OFLAG_COMPRESSED (1LL << 63)

typedef struct QCowHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t backing_file_offset;
    uint32_t backing_file_size;
    uint32_t mtime;
    uint64_t size; /* in bytes */
    uint8_t cluster_bits;
    uint8_t l2_bits;
    uint16_t padding;
    uint32_t crypt_method;
    uint64_t l1_table_offset;
} QEMU_PACKED QCowHeader;

#define L2_CACHE_SIZE 16

typedef struct BDRVQcowState {
    int cluster_bits;
    int cluster_size;
    int l2_bits;
    int l2_size;
    unsigned int l1_size;
    uint64_t cluster_offset_mask;
    uint64_t l1_table_offset;
    uint64_t *l1_table;
    uint64_t *l2_cache;
    uint64_t l2_cache_offsets[L2_CACHE_SIZE];
    uint32_t l2_cache_counts[L2_CACHE_SIZE];
    uint8_t *cluster_cache;
    uint8_t *cluster_data;
    uint64_t cluster_cache_offset;
    QCryptoBlock *crypto; /* Disk encryption format driver */
    uint32_t crypt_method_header;
    CoMutex lock;
    Error *migration_blocker;
} BDRVQcowState;

static QemuOptsList qcow_create_opts;

static int coroutine_fn decompress_cluster(BlockDriverState *bs,
                                           uint64_t cluster_offset);

static int qcow_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const QCowHeader *cow_header = (const void *)buf;

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) == QCOW_VERSION)
        return 100;
    else
        return 0;
}

static int qcow_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVQcowState *s = bs->opaque;
    unsigned int len, i, shift;
    int ret;
    QCowHeader header;
    QCryptoBlockOpenOptions *crypto_opts = NULL;
    unsigned int cflags = 0;
    QDict *encryptopts = NULL;
    const char *encryptfmt;

    qdict_extract_subqdict(options, &encryptopts, "encrypt.");
    encryptfmt = qdict_get_try_str(encryptopts, "format");

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_pread(bs->file, 0, sizeof(header), &header, 0);
    if (ret < 0) {
        goto fail;
    }
    header.magic = be32_to_cpu(header.magic);
    header.version = be32_to_cpu(header.version);
    header.backing_file_offset = be64_to_cpu(header.backing_file_offset);
    header.backing_file_size = be32_to_cpu(header.backing_file_size);
    header.mtime = be32_to_cpu(header.mtime);
    header.size = be64_to_cpu(header.size);
    header.crypt_method = be32_to_cpu(header.crypt_method);
    header.l1_table_offset = be64_to_cpu(header.l1_table_offset);

    if (header.magic != QCOW_MAGIC) {
        error_setg(errp, "Image not in qcow format");
        ret = -EINVAL;
        goto fail;
    }
    if (header.version != QCOW_VERSION) {
        error_setg(errp, "qcow (v%d) does not support qcow version %" PRIu32,
                   QCOW_VERSION, header.version);
        if (header.version == 2 || header.version == 3) {
            error_append_hint(errp, "Try the 'qcow2' driver instead.\n");
        }

        ret = -ENOTSUP;
        goto fail;
    }

    if (header.size <= 1) {
        error_setg(errp, "Image size is too small (must be at least 2 bytes)");
        ret = -EINVAL;
        goto fail;
    }
    if (header.cluster_bits < 9 || header.cluster_bits > 16) {
        error_setg(errp, "Cluster size must be between 512 and 64k");
        ret = -EINVAL;
        goto fail;
    }

    /* l2_bits specifies number of entries; storing a uint64_t in each entry,
     * so bytes = num_entries << 3. */
    if (header.l2_bits < 9 - 3 || header.l2_bits > 16 - 3) {
        error_setg(errp, "L2 table size must be between 512 and 64k");
        ret = -EINVAL;
        goto fail;
    }

    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header) {
        if (bdrv_uses_whitelist() &&
            s->crypt_method_header == QCOW_CRYPT_AES) {
            error_setg(errp,
                       "Use of AES-CBC encrypted qcow images is no longer "
                       "supported in system emulators");
            error_append_hint(errp,
                              "You can use 'qemu-img convert' to convert your "
                              "image to an alternative supported format, such "
                              "as unencrypted qcow, or raw with the LUKS "
                              "format instead.\n");
            ret = -ENOSYS;
            goto fail;
        }
        if (s->crypt_method_header == QCOW_CRYPT_AES) {
            if (encryptfmt && !g_str_equal(encryptfmt, "aes")) {
                error_setg(errp,
                           "Header reported 'aes' encryption format but "
                           "options specify '%s'", encryptfmt);
                ret = -EINVAL;
                goto fail;
            }
            qdict_put_str(encryptopts, "format", "qcow");
            crypto_opts = block_crypto_open_opts_init(encryptopts, errp);
            if (!crypto_opts) {
                ret = -EINVAL;
                goto fail;
            }

            if (flags & BDRV_O_NO_IO) {
                cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
            }
            s->crypto = qcrypto_block_open(crypto_opts, "encrypt.",
                                           NULL, NULL, cflags, 1, errp);
            if (!s->crypto) {
                ret = -EINVAL;
                goto fail;
            }
        } else {
            error_setg(errp, "invalid encryption method in qcow header");
            ret = -EINVAL;
            goto fail;
        }
        bs->encrypted = true;
    } else {
        if (encryptfmt) {
            error_setg(errp, "No encryption in image header, but options "
                       "specified format '%s'", encryptfmt);
            ret = -EINVAL;
            goto fail;
        }
    }
    s->cluster_bits = header.cluster_bits;
    s->cluster_size = 1 << s->cluster_bits;
    s->l2_bits = header.l2_bits;
    s->l2_size = 1 << s->l2_bits;
    bs->total_sectors = header.size / 512;
    s->cluster_offset_mask = (1LL << (63 - s->cluster_bits)) - 1;

    /* read the level 1 table */
    shift = s->cluster_bits + s->l2_bits;
    if (header.size > UINT64_MAX - (1LL << shift)) {
        error_setg(errp, "Image too large");
        ret = -EINVAL;
        goto fail;
    } else {
        uint64_t l1_size = (header.size + (1LL << shift) - 1) >> shift;
        if (l1_size > INT_MAX / sizeof(uint64_t)) {
            error_setg(errp, "Image too large");
            ret = -EINVAL;
            goto fail;
        }
        s->l1_size = l1_size;
    }

    s->l1_table_offset = header.l1_table_offset;
    s->l1_table = g_try_new(uint64_t, s->l1_size);
    if (s->l1_table == NULL) {
        error_setg(errp, "Could not allocate memory for L1 table");
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, s->l1_table_offset,
                     s->l1_size * sizeof(uint64_t), s->l1_table, 0);
    if (ret < 0) {
        goto fail;
    }

    for(i = 0;i < s->l1_size; i++) {
        s->l1_table[i] = be64_to_cpu(s->l1_table[i]);
    }

    /* alloc L2 cache (max. 64k * 16 * 8 = 8 MB) */
    s->l2_cache =
        qemu_try_blockalign(bs->file->bs,
                            s->l2_size * L2_CACHE_SIZE * sizeof(uint64_t));
    if (s->l2_cache == NULL) {
        error_setg(errp, "Could not allocate L2 table cache");
        ret = -ENOMEM;
        goto fail;
    }
    s->cluster_cache = g_malloc(s->cluster_size);
    s->cluster_data = g_malloc(s->cluster_size);
    s->cluster_cache_offset = -1;

    /* read the backing file name */
    if (header.backing_file_offset != 0) {
        len = header.backing_file_size;
        if (len > 1023 || len >= sizeof(bs->backing_file)) {
            error_setg(errp, "Backing file name too long");
            ret = -EINVAL;
            goto fail;
        }
        ret = bdrv_pread(bs->file, header.backing_file_offset, len,
                         bs->auto_backing_file, 0);
        if (ret < 0) {
            goto fail;
        }
        bs->auto_backing_file[len] = '\0';
        pstrcpy(bs->backing_file, sizeof(bs->backing_file),
                bs->auto_backing_file);
    }

    /* Disable migration when qcow images are used */
    error_setg(&s->migration_blocker, "The qcow format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker(s->migration_blocker, errp);
    if (ret < 0) {
        error_free(s->migration_blocker);
        goto fail;
    }

    qobject_unref(encryptopts);
    qapi_free_QCryptoBlockOpenOptions(crypto_opts);
    qemu_co_mutex_init(&s->lock);
    return 0;

 fail:
    g_free(s->l1_table);
    qemu_vfree(s->l2_cache);
    g_free(s->cluster_cache);
    g_free(s->cluster_data);
    qcrypto_block_free(s->crypto);
    qobject_unref(encryptopts);
    qapi_free_QCryptoBlockOpenOptions(crypto_opts);
    return ret;
}


/* We have nothing to do for QCOW reopen, stubs just return
 * success */
static int qcow_reopen_prepare(BDRVReopenState *state,
                               BlockReopenQueue *queue, Error **errp)
{
    return 0;
}


/* 'allocate' is:
 *
 * 0 to not allocate.
 *
 * 1 to allocate a normal cluster (for sector-aligned byte offsets 'n_start'
 * to 'n_end' within the cluster)
 *
 * 2 to allocate a compressed cluster of size
 * 'compressed_size'. 'compressed_size' must be > 0 and <
 * cluster_size
 *
 * return 0 if not allocated, 1 if *result is assigned, and negative
 * errno on failure.
 */
static int coroutine_fn get_cluster_offset(BlockDriverState *bs,
                                           uint64_t offset, int allocate,
                                           int compressed_size,
                                           int n_start, int n_end,
                                           uint64_t *result)
{
    BDRVQcowState *s = bs->opaque;
    int min_index, i, j, l1_index, l2_index, ret;
    int64_t l2_offset;
    uint64_t *l2_table, cluster_offset, tmp;
    uint32_t min_count;
    int new_l2_table;

    *result = 0;
    l1_index = offset >> (s->l2_bits + s->cluster_bits);
    l2_offset = s->l1_table[l1_index];
    new_l2_table = 0;
    if (!l2_offset) {
        if (!allocate)
            return 0;
        /* allocate a new l2 entry */
        l2_offset = bdrv_getlength(bs->file->bs);
        if (l2_offset < 0) {
            return l2_offset;
        }
        /* round to cluster size */
        l2_offset = QEMU_ALIGN_UP(l2_offset, s->cluster_size);
        /* update the L1 entry */
        s->l1_table[l1_index] = l2_offset;
        tmp = cpu_to_be64(l2_offset);
        BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);
        ret = bdrv_co_pwrite_sync(bs->file,
                                  s->l1_table_offset + l1_index * sizeof(tmp),
                                  sizeof(tmp), &tmp, 0);
        if (ret < 0) {
            return ret;
        }
        new_l2_table = 1;
    }
    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (l2_offset == s->l2_cache_offsets[i]) {
            /* increment the hit count */
            if (++s->l2_cache_counts[i] == 0xffffffff) {
                for(j = 0; j < L2_CACHE_SIZE; j++) {
                    s->l2_cache_counts[j] >>= 1;
                }
            }
            l2_table = s->l2_cache + (i << s->l2_bits);
            goto found;
        }
    }
    /* not found: load a new entry in the least used one */
    min_index = 0;
    min_count = 0xffffffff;
    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (s->l2_cache_counts[i] < min_count) {
            min_count = s->l2_cache_counts[i];
            min_index = i;
        }
    }
    l2_table = s->l2_cache + (min_index << s->l2_bits);
    BLKDBG_EVENT(bs->file, BLKDBG_L2_LOAD);
    if (new_l2_table) {
        memset(l2_table, 0, s->l2_size * sizeof(uint64_t));
        ret = bdrv_co_pwrite_sync(bs->file, l2_offset,
                                  s->l2_size * sizeof(uint64_t), l2_table, 0);
        if (ret < 0) {
            return ret;
        }
    } else {
        ret = bdrv_co_pread(bs->file, l2_offset,
                            s->l2_size * sizeof(uint64_t), l2_table, 0);
        if (ret < 0) {
            return ret;
        }
    }
    s->l2_cache_offsets[min_index] = l2_offset;
    s->l2_cache_counts[min_index] = 1;
 found:
    l2_index = (offset >> s->cluster_bits) & (s->l2_size - 1);
    cluster_offset = be64_to_cpu(l2_table[l2_index]);
    if (!cluster_offset ||
        ((cluster_offset & QCOW_OFLAG_COMPRESSED) && allocate == 1)) {
        if (!allocate)
            return 0;
        BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC);
        assert(QEMU_IS_ALIGNED(n_start | n_end, BDRV_SECTOR_SIZE));
        /* allocate a new cluster */
        if ((cluster_offset & QCOW_OFLAG_COMPRESSED) &&
            (n_end - n_start) < s->cluster_size) {
            /* if the cluster is already compressed, we must
               decompress it in the case it is not completely
               overwritten */
            if (decompress_cluster(bs, cluster_offset) < 0) {
                return -EIO;
            }
            cluster_offset = bdrv_getlength(bs->file->bs);
            if ((int64_t) cluster_offset < 0) {
                return cluster_offset;
            }
            cluster_offset = QEMU_ALIGN_UP(cluster_offset, s->cluster_size);
            /* write the cluster content */
            BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
            ret = bdrv_co_pwrite(bs->file, cluster_offset, s->cluster_size,
                                 s->cluster_cache, 0);
            if (ret < 0) {
                return ret;
            }
        } else {
            cluster_offset = bdrv_getlength(bs->file->bs);
            if ((int64_t) cluster_offset < 0) {
                return cluster_offset;
            }
            if (allocate == 1) {
                /* round to cluster size */
                cluster_offset = QEMU_ALIGN_UP(cluster_offset, s->cluster_size);
                if (cluster_offset + s->cluster_size > INT64_MAX) {
                    return -E2BIG;
                }
                ret = bdrv_co_truncate(bs->file,
                                       cluster_offset + s->cluster_size,
                                       false, PREALLOC_MODE_OFF, 0, NULL);
                if (ret < 0) {
                    return ret;
                }
                /* if encrypted, we must initialize the cluster
                   content which won't be written */
                if (bs->encrypted &&
                    (n_end - n_start) < s->cluster_size) {
                    uint64_t start_offset;
                    assert(s->crypto);
                    start_offset = offset & ~(s->cluster_size - 1);
                    for (i = 0; i < s->cluster_size; i += BDRV_SECTOR_SIZE) {
                        if (i < n_start || i >= n_end) {
                            memset(s->cluster_data, 0x00, BDRV_SECTOR_SIZE);
                            if (qcrypto_block_encrypt(s->crypto,
                                                      start_offset + i,
                                                      s->cluster_data,
                                                      BDRV_SECTOR_SIZE,
                                                      NULL) < 0) {
                                return -EIO;
                            }
                            BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
                            ret = bdrv_co_pwrite(bs->file, cluster_offset + i,
                                                 BDRV_SECTOR_SIZE,
                                                 s->cluster_data, 0);
                            if (ret < 0) {
                                return ret;
                            }
                        }
                    }
                }
            } else if (allocate == 2) {
                cluster_offset |= QCOW_OFLAG_COMPRESSED |
                    (uint64_t)compressed_size << (63 - s->cluster_bits);
            }
        }
        /* update L2 table */
        tmp = cpu_to_be64(cluster_offset);
        l2_table[l2_index] = tmp;
        if (allocate == 2) {
            BLKDBG_EVENT(bs->file, BLKDBG_L2_UPDATE_COMPRESSED);
        } else {
            BLKDBG_EVENT(bs->file, BLKDBG_L2_UPDATE);
        }
        ret = bdrv_co_pwrite_sync(bs->file, l2_offset + l2_index * sizeof(tmp),
                                  sizeof(tmp), &tmp, 0);
        if (ret < 0) {
            return ret;
        }
    }
    *result = cluster_offset;
    return 1;
}

static int coroutine_fn qcow_co_block_status(BlockDriverState *bs,
                                             bool want_zero,
                                             int64_t offset, int64_t bytes,
                                             int64_t *pnum, int64_t *map,
                                             BlockDriverState **file)
{
    BDRVQcowState *s = bs->opaque;
    int index_in_cluster, ret;
    int64_t n;
    uint64_t cluster_offset;

    qemu_co_mutex_lock(&s->lock);
    ret = get_cluster_offset(bs, offset, 0, 0, 0, 0, &cluster_offset);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        return ret;
    }
    index_in_cluster = offset & (s->cluster_size - 1);
    n = s->cluster_size - index_in_cluster;
    if (n > bytes) {
        n = bytes;
    }
    *pnum = n;
    if (!cluster_offset) {
        return 0;
    }
    if ((cluster_offset & QCOW_OFLAG_COMPRESSED) || s->crypto) {
        return BDRV_BLOCK_DATA;
    }
    *map = cluster_offset | index_in_cluster;
    *file = bs->file->bs;
    return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
}

static int decompress_buffer(uint8_t *out_buf, int out_buf_size,
                             const uint8_t *buf, int buf_size)
{
    z_stream strm1, *strm = &strm1;
    int ret, out_len;

    memset(strm, 0, sizeof(*strm));

    strm->next_in = (uint8_t *)buf;
    strm->avail_in = buf_size;
    strm->next_out = out_buf;
    strm->avail_out = out_buf_size;

    ret = inflateInit2(strm, -12);
    if (ret != Z_OK)
        return -1;
    ret = inflate(strm, Z_FINISH);
    out_len = strm->next_out - out_buf;
    if ((ret != Z_STREAM_END && ret != Z_BUF_ERROR) ||
        out_len != out_buf_size) {
        inflateEnd(strm);
        return -1;
    }
    inflateEnd(strm);
    return 0;
}

static int coroutine_fn decompress_cluster(BlockDriverState *bs,
                                           uint64_t cluster_offset)
{
    BDRVQcowState *s = bs->opaque;
    int ret, csize;
    uint64_t coffset;

    coffset = cluster_offset & s->cluster_offset_mask;
    if (s->cluster_cache_offset != coffset) {
        csize = cluster_offset >> (63 - s->cluster_bits);
        csize &= (s->cluster_size - 1);
        BLKDBG_EVENT(bs->file, BLKDBG_READ_COMPRESSED);
        ret = bdrv_co_pread(bs->file, coffset, csize, s->cluster_data, 0);
        if (ret < 0)
            return -1;
        if (decompress_buffer(s->cluster_cache, s->cluster_size,
                              s->cluster_data, csize) < 0) {
            return -1;
        }
        s->cluster_cache_offset = coffset;
    }
    return 0;
}

static void qcow_refresh_limits(BlockDriverState *bs, Error **errp)
{
    /* At least encrypted images require 512-byte alignment. Apply the
     * limit universally, rather than just on encrypted images, as
     * it's easier to let the block layer handle rounding than to
     * audit this code further. */
    bs->bl.request_alignment = BDRV_SECTOR_SIZE;
}

static coroutine_fn int qcow_co_preadv(BlockDriverState *bs, int64_t offset,
                                       int64_t bytes, QEMUIOVector *qiov,
                                       BdrvRequestFlags flags)
{
    BDRVQcowState *s = bs->opaque;
    int offset_in_cluster;
    int ret = 0, n;
    uint64_t cluster_offset;
    uint8_t *buf;
    void *orig_buf;

    if (qiov->niov > 1) {
        buf = orig_buf = qemu_try_blockalign(bs, qiov->size);
        if (buf == NULL) {
            return -ENOMEM;
        }
    } else {
        orig_buf = NULL;
        buf = (uint8_t *)qiov->iov->iov_base;
    }

    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {
        /* prepare next request */
        ret = get_cluster_offset(bs, offset, 0, 0, 0, 0, &cluster_offset);
        if (ret < 0) {
            break;
        }
        offset_in_cluster = offset & (s->cluster_size - 1);
        n = s->cluster_size - offset_in_cluster;
        if (n > bytes) {
            n = bytes;
        }

        if (!cluster_offset) {
            if (bs->backing) {
                /* read from the base image */
                qemu_co_mutex_unlock(&s->lock);
                /* qcow2 emits this on bs->file instead of bs->backing */
                BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                ret = bdrv_co_pread(bs->backing, offset, n, buf, 0);
                qemu_co_mutex_lock(&s->lock);
                if (ret < 0) {
                    break;
                }
            } else {
                /* Note: in this case, no need to wait */
                memset(buf, 0, n);
            }
        } else if (cluster_offset & QCOW_OFLAG_COMPRESSED) {
            /* add AIO support for compressed blocks ? */
            if (decompress_cluster(bs, cluster_offset) < 0) {
                ret = -EIO;
                break;
            }
            memcpy(buf, s->cluster_cache + offset_in_cluster, n);
        } else {
            if ((cluster_offset & 511) != 0) {
                ret = -EIO;
                break;
            }
            qemu_co_mutex_unlock(&s->lock);
            BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
            ret = bdrv_co_pread(bs->file, cluster_offset + offset_in_cluster,
                                n, buf, 0);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                break;
            }
            if (bs->encrypted) {
                assert(s->crypto);
                if (qcrypto_block_decrypt(s->crypto,
                                          offset, buf, n, NULL) < 0) {
                    ret = -EIO;
                    break;
                }
            }
        }
        ret = 0;

        bytes -= n;
        offset += n;
        buf += n;
    }

    qemu_co_mutex_unlock(&s->lock);

    if (qiov->niov > 1) {
        qemu_iovec_from_buf(qiov, 0, orig_buf, qiov->size);
        qemu_vfree(orig_buf);
    }

    return ret;
}

static coroutine_fn int qcow_co_pwritev(BlockDriverState *bs, int64_t offset,
                                        int64_t bytes, QEMUIOVector *qiov,
                                        BdrvRequestFlags flags)
{
    BDRVQcowState *s = bs->opaque;
    int offset_in_cluster;
    uint64_t cluster_offset;
    int ret = 0, n;
    uint8_t *buf;
    void *orig_buf;

    s->cluster_cache_offset = -1; /* disable compressed cache */

    /* We must always copy the iov when encrypting, so we
     * don't modify the original data buffer during encryption */
    if (bs->encrypted || qiov->niov > 1) {
        buf = orig_buf = qemu_try_blockalign(bs, qiov->size);
        if (buf == NULL) {
            return -ENOMEM;
        }
        qemu_iovec_to_buf(qiov, 0, buf, qiov->size);
    } else {
        orig_buf = NULL;
        buf = (uint8_t *)qiov->iov->iov_base;
    }

    qemu_co_mutex_lock(&s->lock);

    while (bytes != 0) {
        offset_in_cluster = offset & (s->cluster_size - 1);
        n = s->cluster_size - offset_in_cluster;
        if (n > bytes) {
            n = bytes;
        }
        ret = get_cluster_offset(bs, offset, 1, 0, offset_in_cluster,
                                 offset_in_cluster + n, &cluster_offset);
        if (ret < 0) {
            break;
        }
        if (!cluster_offset || (cluster_offset & 511) != 0) {
            ret = -EIO;
            break;
        }
        if (bs->encrypted) {
            assert(s->crypto);
            if (qcrypto_block_encrypt(s->crypto, offset, buf, n, NULL) < 0) {
                ret = -EIO;
                break;
            }
        }

        qemu_co_mutex_unlock(&s->lock);
        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
        ret = bdrv_co_pwrite(bs->file, cluster_offset + offset_in_cluster,
                             n, buf, 0);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            break;
        }
        ret = 0;

        bytes -= n;
        offset += n;
        buf += n;
    }
    qemu_co_mutex_unlock(&s->lock);

    qemu_vfree(orig_buf);

    return ret;
}

static void qcow_close(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;

    qcrypto_block_free(s->crypto);
    s->crypto = NULL;
    g_free(s->l1_table);
    qemu_vfree(s->l2_cache);
    g_free(s->cluster_cache);
    g_free(s->cluster_data);

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static int coroutine_fn qcow_co_create(BlockdevCreateOptions *opts,
                                       Error **errp)
{
    BlockdevCreateOptionsQcow *qcow_opts;
    int header_size, backing_filename_len, l1_size, shift, i;
    QCowHeader header;
    uint8_t *tmp;
    int64_t total_size = 0;
    int ret;
    BlockDriverState *bs;
    BlockBackend *qcow_blk;
    QCryptoBlock *crypto = NULL;

    assert(opts->driver == BLOCKDEV_DRIVER_QCOW);
    qcow_opts = &opts->u.qcow;

    /* Sanity checks */
    total_size = qcow_opts->size;
    if (total_size == 0) {
        error_setg(errp, "Image size is too small, cannot be zero length");
        return -EINVAL;
    }

    if (qcow_opts->encrypt &&
        qcow_opts->encrypt->format != Q_CRYPTO_BLOCK_FORMAT_QCOW)
    {
        error_setg(errp, "Unsupported encryption format");
        return -EINVAL;
    }

    /* Create BlockBackend to write to the image */
    bs = bdrv_co_open_blockdev_ref(qcow_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    qcow_blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE,
                                  BLK_PERM_ALL, errp);
    if (!qcow_blk) {
        ret = -EPERM;
        goto exit;
    }
    blk_set_allow_write_beyond_eof(qcow_blk, true);

    /* Create image format */
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be32(QCOW_MAGIC);
    header.version = cpu_to_be32(QCOW_VERSION);
    header.size = cpu_to_be64(total_size);
    header_size = sizeof(header);
    backing_filename_len = 0;
    if (qcow_opts->backing_file) {
        if (strcmp(qcow_opts->backing_file, "fat:")) {
            header.backing_file_offset = cpu_to_be64(header_size);
            backing_filename_len = strlen(qcow_opts->backing_file);
            header.backing_file_size = cpu_to_be32(backing_filename_len);
            header_size += backing_filename_len;
        } else {
            /* special backing file for vvfat */
            qcow_opts->backing_file = NULL;
        }
        header.cluster_bits = 9; /* 512 byte cluster to avoid copying
                                    unmodified sectors */
        header.l2_bits = 12; /* 32 KB L2 tables */
    } else {
        header.cluster_bits = 12; /* 4 KB clusters */
        header.l2_bits = 9; /* 4 KB L2 tables */
    }
    header_size = (header_size + 7) & ~7;
    shift = header.cluster_bits + header.l2_bits;
    l1_size = (total_size + (1LL << shift) - 1) >> shift;

    header.l1_table_offset = cpu_to_be64(header_size);

    if (qcow_opts->encrypt) {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_AES);

        crypto = qcrypto_block_create(qcow_opts->encrypt, "encrypt.",
                                      NULL, NULL, NULL, errp);
        if (!crypto) {
            ret = -EINVAL;
            goto exit;
        }
    } else {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);
    }

    /* write all the data */
    ret = blk_co_pwrite(qcow_blk, 0, sizeof(header), &header, 0);
    if (ret < 0) {
        goto exit;
    }

    if (qcow_opts->backing_file) {
        ret = blk_co_pwrite(qcow_blk, sizeof(header), backing_filename_len,
                            qcow_opts->backing_file, 0);
        if (ret < 0) {
            goto exit;
        }
    }

    tmp = g_malloc0(BDRV_SECTOR_SIZE);
    for (i = 0; i < DIV_ROUND_UP(sizeof(uint64_t) * l1_size, BDRV_SECTOR_SIZE);
         i++) {
        ret = blk_co_pwrite(qcow_blk, header_size + BDRV_SECTOR_SIZE * i,
                            BDRV_SECTOR_SIZE, tmp, 0);
        if (ret < 0) {
            g_free(tmp);
            goto exit;
        }
    }

    g_free(tmp);
    ret = 0;
exit:
    blk_unref(qcow_blk);
    bdrv_unref(bs);
    qcrypto_block_free(crypto);
    return ret;
}

static int coroutine_fn qcow_co_create_opts(BlockDriver *drv,
                                            const char *filename,
                                            QemuOpts *opts, Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    BlockDriverState *bs = NULL;
    QDict *qdict = NULL;
    Visitor *v;
    const char *val;
    int ret;
    char *backing_fmt;

    static const QDictRenames opt_renames[] = {
        { BLOCK_OPT_BACKING_FILE,       "backing-file" },
        { BLOCK_OPT_ENCRYPT,            BLOCK_OPT_ENCRYPT_FORMAT },
        { NULL, NULL },
    };

    /*
     * We can't actually store a backing format, but can check that
     * the user's request made sense.
     */
    backing_fmt = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FMT);
    if (backing_fmt && !bdrv_find_format(backing_fmt)) {
        error_setg(errp, "unrecognized backing format '%s'", backing_fmt);
        ret = -EINVAL;
        goto fail;
    }

    /* Parse options and convert legacy syntax */
    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &qcow_create_opts, true);

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

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Create and open the file (protocol layer) */
    ret = bdrv_co_create_file(filename, opts, errp);
    if (ret < 0) {
        goto fail;
    }

    bs = bdrv_co_open(filename, NULL, NULL,
                      BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    /* Now get the QAPI type BlockdevCreateOptions */
    qdict_put_str(qdict, "driver", "qcow");
    qdict_put_str(qdict, "file", bs->node_name);

    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto fail;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto fail;
    }

    /* Silently round up size */
    assert(create_options->driver == BLOCKDEV_DRIVER_QCOW);
    create_options->u.qcow.size =
        ROUND_UP(create_options->u.qcow.size, BDRV_SECTOR_SIZE);

    /* Create the qcow image (format layer) */
    ret = qcow_co_create(create_options, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
fail:
    g_free(backing_fmt);
    qobject_unref(qdict);
    bdrv_unref(bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static int qcow_make_empty(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    uint32_t l1_length = s->l1_size * sizeof(uint64_t);
    int ret;

    memset(s->l1_table, 0, l1_length);
    if (bdrv_pwrite_sync(bs->file, s->l1_table_offset, l1_length, s->l1_table,
                         0) < 0)
        return -1;
    ret = bdrv_truncate(bs->file, s->l1_table_offset + l1_length, false,
                        PREALLOC_MODE_OFF, 0, NULL);
    if (ret < 0)
        return ret;

    memset(s->l2_cache, 0, s->l2_size * L2_CACHE_SIZE * sizeof(uint64_t));
    memset(s->l2_cache_offsets, 0, L2_CACHE_SIZE * sizeof(uint64_t));
    memset(s->l2_cache_counts, 0, L2_CACHE_SIZE * sizeof(uint32_t));

    return 0;
}

/* XXX: put compressed sectors first, then all the cluster aligned
   tables to avoid losing bytes in alignment */
static coroutine_fn int
qcow_co_pwritev_compressed(BlockDriverState *bs, int64_t offset, int64_t bytes,
                           QEMUIOVector *qiov)
{
    BDRVQcowState *s = bs->opaque;
    z_stream strm;
    int ret, out_len;
    uint8_t *buf, *out_buf;
    uint64_t cluster_offset;

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
    qemu_iovec_to_buf(qiov, 0, buf, qiov->size);

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
        ret = qcow_co_pwritev(bs, offset, bytes, qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        goto success;
    }
    qemu_co_mutex_lock(&s->lock);
    ret = get_cluster_offset(bs, offset, 2, out_len, 0, 0, &cluster_offset);
    qemu_co_mutex_unlock(&s->lock);
    if (ret < 0) {
        goto fail;
    }
    if (cluster_offset == 0) {
        ret = -EIO;
        goto fail;
    }
    cluster_offset &= s->cluster_offset_mask;

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_COMPRESSED);
    ret = bdrv_co_pwrite(bs->file, cluster_offset, out_len, out_buf, 0);
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

static int coroutine_fn
qcow_co_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQcowState *s = bs->opaque;
    bdi->cluster_size = s->cluster_size;
    return 0;
}

static QemuOptsList qcow_create_opts = {
    .name = "qcow-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qcow_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        {
            .name = BLOCK_OPT_BACKING_FMT,
            .type = QEMU_OPT_STRING,
            .help = "Format of the backing image",
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
            .help = "Encrypt the image, format choices: 'aes'",
        },
        BLOCK_CRYPTO_OPT_DEF_QCOW_KEY_SECRET("encrypt."),
        { /* end of list */ }
    }
};

static const char *const qcow_strong_runtime_opts[] = {
    "encrypt." BLOCK_CRYPTO_OPT_QCOW_KEY_SECRET,

    NULL
};

static BlockDriver bdrv_qcow = {
    .format_name	= "qcow",
    .instance_size	= sizeof(BDRVQcowState),
    .bdrv_probe		= qcow_probe,
    .bdrv_open		= qcow_open,
    .bdrv_close		= qcow_close,
    .bdrv_child_perm        = bdrv_default_perms,
    .bdrv_reopen_prepare    = qcow_reopen_prepare,
    .bdrv_co_create         = qcow_co_create,
    .bdrv_co_create_opts    = qcow_co_create_opts,
    .bdrv_has_zero_init     = bdrv_has_zero_init_1,
    .is_format              = true,
    .supports_backing       = true,
    .bdrv_refresh_limits    = qcow_refresh_limits,

    .bdrv_co_preadv         = qcow_co_preadv,
    .bdrv_co_pwritev        = qcow_co_pwritev,
    .bdrv_co_block_status   = qcow_co_block_status,

    .bdrv_make_empty        = qcow_make_empty,
    .bdrv_co_pwritev_compressed = qcow_co_pwritev_compressed,
    .bdrv_co_get_info       = qcow_co_get_info,

    .create_opts            = &qcow_create_opts,
    .strong_runtime_opts    = qcow_strong_runtime_opts,
};

static void bdrv_qcow_init(void)
{
    bdrv_register(&bdrv_qcow);
}

block_init(bdrv_qcow_init);
