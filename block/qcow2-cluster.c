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
#include <zlib.h>

#include "qapi/error.h"
#include "qemu-common.h"
#include "block/block_int.h"
#include "block/qcow2.h"
#include "qemu/bswap.h"
#include "trace.h"

int qcow2_grow_l1_table(BlockDriverState *bs, uint64_t min_size,
                        bool exact_size)
{
    BDRVQcow2State *s = bs->opaque;
    int new_l1_size2, ret, i;
    uint64_t *new_l1_table;
    int64_t old_l1_table_offset, old_l1_size;
    int64_t new_l1_table_offset, new_l1_size;
    uint8_t data[12];

    if (min_size <= s->l1_size)
        return 0;

    /* Do a sanity check on min_size before trying to calculate new_l1_size
     * (this prevents overflows during the while loop for the calculation of
     * new_l1_size) */
    if (min_size > INT_MAX / sizeof(uint64_t)) {
        return -EFBIG;
    }

    if (exact_size) {
        new_l1_size = min_size;
    } else {
        /* Bump size up to reduce the number of times we have to grow */
        new_l1_size = s->l1_size;
        if (new_l1_size == 0) {
            new_l1_size = 1;
        }
        while (min_size > new_l1_size) {
            new_l1_size = (new_l1_size * 3 + 1) / 2;
        }
    }

    QEMU_BUILD_BUG_ON(QCOW_MAX_L1_SIZE > INT_MAX);
    if (new_l1_size > QCOW_MAX_L1_SIZE / sizeof(uint64_t)) {
        return -EFBIG;
    }

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "grow l1_table from %d to %" PRId64 "\n",
            s->l1_size, new_l1_size);
#endif

    new_l1_size2 = sizeof(uint64_t) * new_l1_size;
    new_l1_table = qemu_try_blockalign(bs->file->bs,
                                       align_offset(new_l1_size2, 512));
    if (new_l1_table == NULL) {
        return -ENOMEM;
    }
    memset(new_l1_table, 0, align_offset(new_l1_size2, 512));

    if (s->l1_size) {
        memcpy(new_l1_table, s->l1_table, s->l1_size * sizeof(uint64_t));
    }

    /* write new table (align to cluster) */
    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_ALLOC_TABLE);
    new_l1_table_offset = qcow2_alloc_clusters(bs, new_l1_size2);
    if (new_l1_table_offset < 0) {
        qemu_vfree(new_l1_table);
        return new_l1_table_offset;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* the L1 position has not yet been updated, so these clusters must
     * indeed be completely free */
    ret = qcow2_pre_write_overlap_check(bs, 0, new_l1_table_offset,
                                        new_l1_size2);
    if (ret < 0) {
        goto fail;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_WRITE_TABLE);
    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = cpu_to_be64(new_l1_table[i]);
    ret = bdrv_pwrite_sync(bs->file, new_l1_table_offset,
                           new_l1_table, new_l1_size2);
    if (ret < 0)
        goto fail;
    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = be64_to_cpu(new_l1_table[i]);

    /* set new table */
    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_ACTIVATE_TABLE);
    stl_be_p(data, new_l1_size);
    stq_be_p(data + 4, new_l1_table_offset);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, l1_size),
                           data, sizeof(data));
    if (ret < 0) {
        goto fail;
    }
    qemu_vfree(s->l1_table);
    old_l1_table_offset = s->l1_table_offset;
    s->l1_table_offset = new_l1_table_offset;
    s->l1_table = new_l1_table;
    old_l1_size = s->l1_size;
    s->l1_size = new_l1_size;
    qcow2_free_clusters(bs, old_l1_table_offset, old_l1_size * sizeof(uint64_t),
                        QCOW2_DISCARD_OTHER);
    return 0;
 fail:
    qemu_vfree(new_l1_table);
    qcow2_free_clusters(bs, new_l1_table_offset, new_l1_size2,
                        QCOW2_DISCARD_OTHER);
    return ret;
}

/*
 * l2_load
 *
 * Loads a L2 table into memory. If the table is in the cache, the cache
 * is used; otherwise the L2 table is loaded from the image file.
 *
 * Returns a pointer to the L2 table on success, or NULL if the read from
 * the image file failed.
 */

static int l2_load(BlockDriverState *bs, uint64_t l2_offset,
    uint64_t **l2_table)
{
    BDRVQcow2State *s = bs->opaque;

    return qcow2_cache_get(bs, s->l2_table_cache, l2_offset,
                           (void **)l2_table);
}

/*
 * Writes one sector of the L1 table to the disk (can't update single entries
 * and we really don't want bdrv_pread to perform a read-modify-write)
 */
#define L1_ENTRIES_PER_SECTOR (512 / 8)
int qcow2_write_l1_entry(BlockDriverState *bs, int l1_index)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t buf[L1_ENTRIES_PER_SECTOR] = { 0 };
    int l1_start_index;
    int i, ret;

    l1_start_index = l1_index & ~(L1_ENTRIES_PER_SECTOR - 1);
    for (i = 0; i < L1_ENTRIES_PER_SECTOR && l1_start_index + i < s->l1_size;
         i++)
    {
        buf[i] = cpu_to_be64(s->l1_table[l1_start_index + i]);
    }

    ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_ACTIVE_L1,
            s->l1_table_offset + 8 * l1_start_index, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);
    ret = bdrv_pwrite_sync(bs->file,
                           s->l1_table_offset + 8 * l1_start_index,
                           buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/*
 * l2_allocate
 *
 * Allocate a new l2 entry in the file. If l1_index points to an already
 * used entry in the L2 table (i.e. we are doing a copy on write for the L2
 * table) copy the contents of the old L2 table into the newly allocated one.
 * Otherwise the new table is initialized with zeros.
 *
 */

static int l2_allocate(BlockDriverState *bs, int l1_index, uint64_t **table)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t old_l2_offset;
    uint64_t *l2_table = NULL;
    int64_t l2_offset;
    int ret;

    old_l2_offset = s->l1_table[l1_index];

    trace_qcow2_l2_allocate(bs, l1_index);

    /* allocate a new l2 entry */

    l2_offset = qcow2_alloc_clusters(bs, s->l2_size * sizeof(uint64_t));
    if (l2_offset < 0) {
        ret = l2_offset;
        goto fail;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* allocate a new entry in the l2 cache */

    trace_qcow2_l2_allocate_get_empty(bs, l1_index);
    ret = qcow2_cache_get_empty(bs, s->l2_table_cache, l2_offset, (void**) table);
    if (ret < 0) {
        goto fail;
    }

    l2_table = *table;

    if ((old_l2_offset & L1E_OFFSET_MASK) == 0) {
        /* if there was no old l2 table, clear the new table */
        memset(l2_table, 0, s->l2_size * sizeof(uint64_t));
    } else {
        uint64_t* old_table;

        /* if there was an old l2 table, read it from the disk */
        BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_COW_READ);
        ret = qcow2_cache_get(bs, s->l2_table_cache,
            old_l2_offset & L1E_OFFSET_MASK,
            (void**) &old_table);
        if (ret < 0) {
            goto fail;
        }

        memcpy(l2_table, old_table, s->cluster_size);

        qcow2_cache_put(bs, s->l2_table_cache, (void **) &old_table);
    }

    /* write the l2 table to the file */
    BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_WRITE);

    trace_qcow2_l2_allocate_write_l2(bs, l1_index);
    qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);
    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        goto fail;
    }

    /* update the L1 entry */
    trace_qcow2_l2_allocate_write_l1(bs, l1_index);
    s->l1_table[l1_index] = l2_offset | QCOW_OFLAG_COPIED;
    ret = qcow2_write_l1_entry(bs, l1_index);
    if (ret < 0) {
        goto fail;
    }

    *table = l2_table;
    trace_qcow2_l2_allocate_done(bs, l1_index, 0);
    return 0;

fail:
    trace_qcow2_l2_allocate_done(bs, l1_index, ret);
    if (l2_table != NULL) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) table);
    }
    s->l1_table[l1_index] = old_l2_offset;
    if (l2_offset > 0) {
        qcow2_free_clusters(bs, l2_offset, s->l2_size * sizeof(uint64_t),
                            QCOW2_DISCARD_ALWAYS);
    }
    return ret;
}

/*
 * Checks how many clusters in a given L2 table are contiguous in the image
 * file. As soon as one of the flags in the bitmask stop_flags changes compared
 * to the first cluster, the search is stopped and the cluster is not counted
 * as contiguous. (This allows it, for example, to stop at the first compressed
 * cluster which may require a different handling)
 */
static int count_contiguous_clusters(int nb_clusters, int cluster_size,
        uint64_t *l2_table, uint64_t stop_flags)
{
    int i;
    QCow2ClusterType first_cluster_type;
    uint64_t mask = stop_flags | L2E_OFFSET_MASK | QCOW_OFLAG_COMPRESSED;
    uint64_t first_entry = be64_to_cpu(l2_table[0]);
    uint64_t offset = first_entry & mask;

    if (!offset) {
        return 0;
    }

    /* must be allocated */
    first_cluster_type = qcow2_get_cluster_type(first_entry);
    assert(first_cluster_type == QCOW2_CLUSTER_NORMAL ||
           first_cluster_type == QCOW2_CLUSTER_ZERO_ALLOC);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t l2_entry = be64_to_cpu(l2_table[i]) & mask;
        if (offset + (uint64_t) i * cluster_size != l2_entry) {
            break;
        }
    }

	return i;
}

/*
 * Checks how many consecutive unallocated clusters in a given L2
 * table have the same cluster type.
 */
static int count_contiguous_clusters_unallocated(int nb_clusters,
                                                 uint64_t *l2_table,
                                                 QCow2ClusterType wanted_type)
{
    int i;

    assert(wanted_type == QCOW2_CLUSTER_ZERO_PLAIN ||
           wanted_type == QCOW2_CLUSTER_UNALLOCATED);
    for (i = 0; i < nb_clusters; i++) {
        uint64_t entry = be64_to_cpu(l2_table[i]);
        QCow2ClusterType type = qcow2_get_cluster_type(entry);

        if (type != wanted_type) {
            break;
        }
    }

    return i;
}

static int coroutine_fn do_perform_cow_read(BlockDriverState *bs,
                                            uint64_t src_cluster_offset,
                                            unsigned offset_in_cluster,
                                            QEMUIOVector *qiov)
{
    int ret;

    if (qiov->size == 0) {
        return 0;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_COW_READ);

    if (!bs->drv) {
        return -ENOMEDIUM;
    }

    /* Call .bdrv_co_readv() directly instead of using the public block-layer
     * interface.  This avoids double I/O throttling and request tracking,
     * which can lead to deadlock when block layer copy-on-read is enabled.
     */
    ret = bs->drv->bdrv_co_preadv(bs, src_cluster_offset + offset_in_cluster,
                                  qiov->size, qiov, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static bool coroutine_fn do_perform_cow_encrypt(BlockDriverState *bs,
                                                uint64_t src_cluster_offset,
                                                uint64_t cluster_offset,
                                                unsigned offset_in_cluster,
                                                uint8_t *buffer,
                                                unsigned bytes)
{
    if (bytes && bs->encrypted) {
        BDRVQcow2State *s = bs->opaque;
        int64_t sector = (s->crypt_physical_offset ?
                          (cluster_offset + offset_in_cluster) :
                          (src_cluster_offset + offset_in_cluster))
                         >> BDRV_SECTOR_BITS;
        assert((offset_in_cluster & ~BDRV_SECTOR_MASK) == 0);
        assert((bytes & ~BDRV_SECTOR_MASK) == 0);
        assert(s->crypto);
        if (qcrypto_block_encrypt(s->crypto, sector, buffer,
                                  bytes, NULL) < 0) {
            return false;
        }
    }
    return true;
}

static int coroutine_fn do_perform_cow_write(BlockDriverState *bs,
                                             uint64_t cluster_offset,
                                             unsigned offset_in_cluster,
                                             QEMUIOVector *qiov)
{
    int ret;

    if (qiov->size == 0) {
        return 0;
    }

    ret = qcow2_pre_write_overlap_check(bs, 0,
            cluster_offset + offset_in_cluster, qiov->size);
    if (ret < 0) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_COW_WRITE);
    ret = bdrv_co_pwritev(bs->file, cluster_offset + offset_in_cluster,
                          qiov->size, qiov, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}


/*
 * get_cluster_offset
 *
 * For a given offset of the virtual disk, find the cluster type and offset in
 * the qcow2 file. The offset is stored in *cluster_offset.
 *
 * On entry, *bytes is the maximum number of contiguous bytes starting at
 * offset that we are interested in.
 *
 * On exit, *bytes is the number of bytes starting at offset that have the same
 * cluster type and (if applicable) are stored contiguously in the image file.
 * Compressed clusters are always returned one by one.
 *
 * Returns the cluster type (QCOW2_CLUSTER_*) on success, -errno in error
 * cases.
 */
int qcow2_get_cluster_offset(BlockDriverState *bs, uint64_t offset,
                             unsigned int *bytes, uint64_t *cluster_offset)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int l2_index;
    uint64_t l1_index, l2_offset, *l2_table;
    int l1_bits, c;
    unsigned int offset_in_cluster;
    uint64_t bytes_available, bytes_needed, nb_clusters;
    QCow2ClusterType type;
    int ret;

    offset_in_cluster = offset_into_cluster(s, offset);
    bytes_needed = (uint64_t) *bytes + offset_in_cluster;

    l1_bits = s->l2_bits + s->cluster_bits;

    /* compute how many bytes there are between the start of the cluster
     * containing offset and the end of the l1 entry */
    bytes_available = (1ULL << l1_bits) - (offset & ((1ULL << l1_bits) - 1))
                    + offset_in_cluster;

    if (bytes_needed > bytes_available) {
        bytes_needed = bytes_available;
    }

    *cluster_offset = 0;

    /* seek to the l2 offset in the l1 table */

    l1_index = offset >> l1_bits;
    if (l1_index >= s->l1_size) {
        type = QCOW2_CLUSTER_UNALLOCATED;
        goto out;
    }

    l2_offset = s->l1_table[l1_index] & L1E_OFFSET_MASK;
    if (!l2_offset) {
        type = QCOW2_CLUSTER_UNALLOCATED;
        goto out;
    }

    if (offset_into_cluster(s, l2_offset)) {
        qcow2_signal_corruption(bs, true, -1, -1, "L2 table offset %#" PRIx64
                                " unaligned (L1 index: %#" PRIx64 ")",
                                l2_offset, l1_index);
        return -EIO;
    }

    /* load the l2 table in memory */

    ret = l2_load(bs, l2_offset, &l2_table);
    if (ret < 0) {
        return ret;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = offset_to_l2_index(s, offset);
    *cluster_offset = be64_to_cpu(l2_table[l2_index]);

    nb_clusters = size_to_clusters(s, bytes_needed);
    /* bytes_needed <= *bytes + offset_in_cluster, both of which are unsigned
     * integers; the minimum cluster size is 512, so this assertion is always
     * true */
    assert(nb_clusters <= INT_MAX);

    type = qcow2_get_cluster_type(*cluster_offset);
    if (s->qcow_version < 3 && (type == QCOW2_CLUSTER_ZERO_PLAIN ||
                                type == QCOW2_CLUSTER_ZERO_ALLOC)) {
        qcow2_signal_corruption(bs, true, -1, -1, "Zero cluster entry found"
                                " in pre-v3 image (L2 offset: %#" PRIx64
                                ", L2 index: %#x)", l2_offset, l2_index);
        ret = -EIO;
        goto fail;
    }
    switch (type) {
    case QCOW2_CLUSTER_COMPRESSED:
        /* Compressed clusters can only be processed one by one */
        c = 1;
        *cluster_offset &= L2E_COMPRESSED_OFFSET_SIZE_MASK;
        break;
    case QCOW2_CLUSTER_ZERO_PLAIN:
    case QCOW2_CLUSTER_UNALLOCATED:
        /* how many empty clusters ? */
        c = count_contiguous_clusters_unallocated(nb_clusters,
                                                  &l2_table[l2_index], type);
        *cluster_offset = 0;
        break;
    case QCOW2_CLUSTER_ZERO_ALLOC:
    case QCOW2_CLUSTER_NORMAL:
        /* how many allocated clusters ? */
        c = count_contiguous_clusters(nb_clusters, s->cluster_size,
                                      &l2_table[l2_index], QCOW_OFLAG_ZERO);
        *cluster_offset &= L2E_OFFSET_MASK;
        if (offset_into_cluster(s, *cluster_offset)) {
            qcow2_signal_corruption(bs, true, -1, -1,
                                    "Cluster allocation offset %#"
                                    PRIx64 " unaligned (L2 offset: %#" PRIx64
                                    ", L2 index: %#x)", *cluster_offset,
                                    l2_offset, l2_index);
            ret = -EIO;
            goto fail;
        }
        break;
    default:
        abort();
    }

    qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);

    bytes_available = (int64_t)c * s->cluster_size;

out:
    if (bytes_available > bytes_needed) {
        bytes_available = bytes_needed;
    }

    /* bytes_available <= bytes_needed <= *bytes + offset_in_cluster;
     * subtracting offset_in_cluster will therefore definitely yield something
     * not exceeding UINT_MAX */
    assert(bytes_available - offset_in_cluster <= UINT_MAX);
    *bytes = bytes_available - offset_in_cluster;

    return type;

fail:
    qcow2_cache_put(bs, s->l2_table_cache, (void **)&l2_table);
    return ret;
}

/*
 * get_cluster_table
 *
 * for a given disk offset, load (and allocate if needed)
 * the l2 table.
 *
 * the l2 table offset in the qcow2 file and the cluster index
 * in the l2 table are given to the caller.
 *
 * Returns 0 on success, -errno in failure case
 */
static int get_cluster_table(BlockDriverState *bs, uint64_t offset,
                             uint64_t **new_l2_table,
                             int *new_l2_index)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int l2_index;
    uint64_t l1_index, l2_offset;
    uint64_t *l2_table = NULL;
    int ret;

    /* seek to the l2 offset in the l1 table */

    l1_index = offset >> (s->l2_bits + s->cluster_bits);
    if (l1_index >= s->l1_size) {
        ret = qcow2_grow_l1_table(bs, l1_index + 1, false);
        if (ret < 0) {
            return ret;
        }
    }

    assert(l1_index < s->l1_size);
    l2_offset = s->l1_table[l1_index] & L1E_OFFSET_MASK;
    if (offset_into_cluster(s, l2_offset)) {
        qcow2_signal_corruption(bs, true, -1, -1, "L2 table offset %#" PRIx64
                                " unaligned (L1 index: %#" PRIx64 ")",
                                l2_offset, l1_index);
        return -EIO;
    }

    /* seek the l2 table of the given l2 offset */

    if (s->l1_table[l1_index] & QCOW_OFLAG_COPIED) {
        /* load the l2 table in memory */
        ret = l2_load(bs, l2_offset, &l2_table);
        if (ret < 0) {
            return ret;
        }
    } else {
        /* First allocate a new L2 table (and do COW if needed) */
        ret = l2_allocate(bs, l1_index, &l2_table);
        if (ret < 0) {
            return ret;
        }

        /* Then decrease the refcount of the old table */
        if (l2_offset) {
            qcow2_free_clusters(bs, l2_offset, s->l2_size * sizeof(uint64_t),
                                QCOW2_DISCARD_OTHER);
        }
    }

    /* find the cluster offset for the given disk offset */

    l2_index = offset_to_l2_index(s, offset);

    *new_l2_table = l2_table;
    *new_l2_index = l2_index;

    return 0;
}

/*
 * alloc_compressed_cluster_offset
 *
 * For a given offset of the disk image, return cluster offset in
 * qcow2 file.
 *
 * If the offset is not found, allocate a new compressed cluster.
 *
 * Return the cluster offset if successful,
 * Return 0, otherwise.
 *
 */

uint64_t qcow2_alloc_compressed_cluster_offset(BlockDriverState *bs,
                                               uint64_t offset,
                                               int compressed_size)
{
    BDRVQcow2State *s = bs->opaque;
    int l2_index, ret;
    uint64_t *l2_table;
    int64_t cluster_offset;
    int nb_csectors;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_index);
    if (ret < 0) {
        return 0;
    }

    /* Compression can't overwrite anything. Fail if the cluster was already
     * allocated. */
    cluster_offset = be64_to_cpu(l2_table[l2_index]);
    if (cluster_offset & L2E_OFFSET_MASK) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
        return 0;
    }

    cluster_offset = qcow2_alloc_bytes(bs, compressed_size);
    if (cluster_offset < 0) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
        return 0;
    }

    nb_csectors = ((cluster_offset + compressed_size - 1) >> 9) -
                  (cluster_offset >> 9);

    cluster_offset |= QCOW_OFLAG_COMPRESSED |
                      ((uint64_t)nb_csectors << s->csize_shift);

    /* update L2 table */

    /* compressed clusters never have the copied flag */

    BLKDBG_EVENT(bs->file, BLKDBG_L2_UPDATE_COMPRESSED);
    qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);
    l2_table[l2_index] = cpu_to_be64(cluster_offset);
    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    return cluster_offset;
}

static int perform_cow(BlockDriverState *bs, QCowL2Meta *m)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2COWRegion *start = &m->cow_start;
    Qcow2COWRegion *end = &m->cow_end;
    unsigned buffer_size;
    unsigned data_bytes = end->offset - (start->offset + start->nb_bytes);
    bool merge_reads;
    uint8_t *start_buffer, *end_buffer;
    QEMUIOVector qiov;
    int ret;

    assert(start->nb_bytes <= UINT_MAX - end->nb_bytes);
    assert(start->nb_bytes + end->nb_bytes <= UINT_MAX - data_bytes);
    assert(start->offset + start->nb_bytes <= end->offset);
    assert(!m->data_qiov || m->data_qiov->size == data_bytes);

    if (start->nb_bytes == 0 && end->nb_bytes == 0) {
        return 0;
    }

    /* If we have to read both the start and end COW regions and the
     * middle region is not too large then perform just one read
     * operation */
    merge_reads = start->nb_bytes && end->nb_bytes && data_bytes <= 16384;
    if (merge_reads) {
        buffer_size = start->nb_bytes + data_bytes + end->nb_bytes;
    } else {
        /* If we have to do two reads, add some padding in the middle
         * if necessary to make sure that the end region is optimally
         * aligned. */
        size_t align = bdrv_opt_mem_align(bs);
        assert(align > 0 && align <= UINT_MAX);
        assert(QEMU_ALIGN_UP(start->nb_bytes, align) <=
               UINT_MAX - end->nb_bytes);
        buffer_size = QEMU_ALIGN_UP(start->nb_bytes, align) + end->nb_bytes;
    }

    /* Reserve a buffer large enough to store all the data that we're
     * going to read */
    start_buffer = qemu_try_blockalign(bs, buffer_size);
    if (start_buffer == NULL) {
        return -ENOMEM;
    }
    /* The part of the buffer where the end region is located */
    end_buffer = start_buffer + buffer_size - end->nb_bytes;

    qemu_iovec_init(&qiov, 2 + (m->data_qiov ? m->data_qiov->niov : 0));

    qemu_co_mutex_unlock(&s->lock);
    /* First we read the existing data from both COW regions. We
     * either read the whole region in one go, or the start and end
     * regions separately. */
    if (merge_reads) {
        qemu_iovec_add(&qiov, start_buffer, buffer_size);
        ret = do_perform_cow_read(bs, m->offset, start->offset, &qiov);
    } else {
        qemu_iovec_add(&qiov, start_buffer, start->nb_bytes);
        ret = do_perform_cow_read(bs, m->offset, start->offset, &qiov);
        if (ret < 0) {
            goto fail;
        }

        qemu_iovec_reset(&qiov);
        qemu_iovec_add(&qiov, end_buffer, end->nb_bytes);
        ret = do_perform_cow_read(bs, m->offset, end->offset, &qiov);
    }
    if (ret < 0) {
        goto fail;
    }

    /* Encrypt the data if necessary before writing it */
    if (bs->encrypted) {
        if (!do_perform_cow_encrypt(bs, m->offset, m->alloc_offset,
                                    start->offset, start_buffer,
                                    start->nb_bytes) ||
            !do_perform_cow_encrypt(bs, m->offset, m->alloc_offset,
                                    end->offset, end_buffer, end->nb_bytes)) {
            ret = -EIO;
            goto fail;
        }
    }

    /* And now we can write everything. If we have the guest data we
     * can write everything in one single operation */
    if (m->data_qiov) {
        qemu_iovec_reset(&qiov);
        if (start->nb_bytes) {
            qemu_iovec_add(&qiov, start_buffer, start->nb_bytes);
        }
        qemu_iovec_concat(&qiov, m->data_qiov, 0, data_bytes);
        if (end->nb_bytes) {
            qemu_iovec_add(&qiov, end_buffer, end->nb_bytes);
        }
        /* NOTE: we have a write_aio blkdebug event here followed by
         * a cow_write one in do_perform_cow_write(), but there's only
         * one single I/O operation */
        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
        ret = do_perform_cow_write(bs, m->alloc_offset, start->offset, &qiov);
    } else {
        /* If there's no guest data then write both COW regions separately */
        qemu_iovec_reset(&qiov);
        qemu_iovec_add(&qiov, start_buffer, start->nb_bytes);
        ret = do_perform_cow_write(bs, m->alloc_offset, start->offset, &qiov);
        if (ret < 0) {
            goto fail;
        }

        qemu_iovec_reset(&qiov);
        qemu_iovec_add(&qiov, end_buffer, end->nb_bytes);
        ret = do_perform_cow_write(bs, m->alloc_offset, end->offset, &qiov);
    }

fail:
    qemu_co_mutex_lock(&s->lock);

    /*
     * Before we update the L2 table to actually point to the new cluster, we
     * need to be sure that the refcounts have been increased and COW was
     * handled.
     */
    if (ret == 0) {
        qcow2_cache_depends_on_flush(s->l2_table_cache);
    }

    qemu_vfree(start_buffer);
    qemu_iovec_destroy(&qiov);
    return ret;
}

int qcow2_alloc_cluster_link_l2(BlockDriverState *bs, QCowL2Meta *m)
{
    BDRVQcow2State *s = bs->opaque;
    int i, j = 0, l2_index, ret;
    uint64_t *old_cluster, *l2_table;
    uint64_t cluster_offset = m->alloc_offset;

    trace_qcow2_cluster_link_l2(qemu_coroutine_self(), m->nb_clusters);
    assert(m->nb_clusters > 0);

    old_cluster = g_try_new(uint64_t, m->nb_clusters);
    if (old_cluster == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    /* copy content of unmodified sectors */
    ret = perform_cow(bs, m);
    if (ret < 0) {
        goto err;
    }

    /* Update L2 table. */
    if (s->use_lazy_refcounts) {
        qcow2_mark_dirty(bs);
    }
    if (qcow2_need_accurate_refcounts(s)) {
        qcow2_cache_set_dependency(bs, s->l2_table_cache,
                                   s->refcount_block_cache);
    }

    ret = get_cluster_table(bs, m->offset, &l2_table, &l2_index);
    if (ret < 0) {
        goto err;
    }
    qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);

    assert(l2_index + m->nb_clusters <= s->l2_size);
    for (i = 0; i < m->nb_clusters; i++) {
        /* if two concurrent writes happen to the same unallocated cluster
         * each write allocates separate cluster and writes data concurrently.
         * The first one to complete updates l2 table with pointer to its
         * cluster the second one has to do RMW (which is done above by
         * perform_cow()), update l2 table with its cluster pointer and free
         * old cluster. This is what this loop does */
        if (l2_table[l2_index + i] != 0) {
            old_cluster[j++] = l2_table[l2_index + i];
        }

        l2_table[l2_index + i] = cpu_to_be64((cluster_offset +
                    (i << s->cluster_bits)) | QCOW_OFLAG_COPIED);
     }


    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    /*
     * If this was a COW, we need to decrease the refcount of the old cluster.
     *
     * Don't discard clusters that reach a refcount of 0 (e.g. compressed
     * clusters), the next write will reuse them anyway.
     */
    if (!m->keep_old_clusters && j != 0) {
        for (i = 0; i < j; i++) {
            qcow2_free_any_clusters(bs, be64_to_cpu(old_cluster[i]), 1,
                                    QCOW2_DISCARD_NEVER);
        }
    }

    ret = 0;
err:
    g_free(old_cluster);
    return ret;
 }

/*
 * Returns the number of contiguous clusters that can be used for an allocating
 * write, but require COW to be performed (this includes yet unallocated space,
 * which must copy from the backing file)
 */
static int count_cow_clusters(BDRVQcow2State *s, int nb_clusters,
    uint64_t *l2_table, int l2_index)
{
    int i;

    for (i = 0; i < nb_clusters; i++) {
        uint64_t l2_entry = be64_to_cpu(l2_table[l2_index + i]);
        QCow2ClusterType cluster_type = qcow2_get_cluster_type(l2_entry);

        switch(cluster_type) {
        case QCOW2_CLUSTER_NORMAL:
            if (l2_entry & QCOW_OFLAG_COPIED) {
                goto out;
            }
            break;
        case QCOW2_CLUSTER_UNALLOCATED:
        case QCOW2_CLUSTER_COMPRESSED:
        case QCOW2_CLUSTER_ZERO_PLAIN:
        case QCOW2_CLUSTER_ZERO_ALLOC:
            break;
        default:
            abort();
        }
    }

out:
    assert(i <= nb_clusters);
    return i;
}

/*
 * Check if there already is an AIO write request in flight which allocates
 * the same cluster. In this case we need to wait until the previous
 * request has completed and updated the L2 table accordingly.
 *
 * Returns:
 *   0       if there was no dependency. *cur_bytes indicates the number of
 *           bytes from guest_offset that can be read before the next
 *           dependency must be processed (or the request is complete)
 *
 *   -EAGAIN if we had to wait for another request, previously gathered
 *           information on cluster allocation may be invalid now. The caller
 *           must start over anyway, so consider *cur_bytes undefined.
 */
static int handle_dependencies(BlockDriverState *bs, uint64_t guest_offset,
    uint64_t *cur_bytes, QCowL2Meta **m)
{
    BDRVQcow2State *s = bs->opaque;
    QCowL2Meta *old_alloc;
    uint64_t bytes = *cur_bytes;

    QLIST_FOREACH(old_alloc, &s->cluster_allocs, next_in_flight) {

        uint64_t start = guest_offset;
        uint64_t end = start + bytes;
        uint64_t old_start = l2meta_cow_start(old_alloc);
        uint64_t old_end = l2meta_cow_end(old_alloc);

        if (end <= old_start || start >= old_end) {
            /* No intersection */
        } else {
            if (start < old_start) {
                /* Stop at the start of a running allocation */
                bytes = old_start - start;
            } else {
                bytes = 0;
            }

            /* Stop if already an l2meta exists. After yielding, it wouldn't
             * be valid any more, so we'd have to clean up the old L2Metas
             * and deal with requests depending on them before starting to
             * gather new ones. Not worth the trouble. */
            if (bytes == 0 && *m) {
                *cur_bytes = 0;
                return 0;
            }

            if (bytes == 0) {
                /* Wait for the dependency to complete. We need to recheck
                 * the free/allocated clusters when we continue. */
                qemu_co_queue_wait(&old_alloc->dependent_requests, &s->lock);
                return -EAGAIN;
            }
        }
    }

    /* Make sure that existing clusters and new allocations are only used up to
     * the next dependency if we shortened the request above */
    *cur_bytes = bytes;

    return 0;
}

/*
 * Checks how many already allocated clusters that don't require a copy on
 * write there are at the given guest_offset (up to *bytes). If
 * *host_offset is not zero, only physically contiguous clusters beginning at
 * this host offset are counted.
 *
 * Note that guest_offset may not be cluster aligned. In this case, the
 * returned *host_offset points to exact byte referenced by guest_offset and
 * therefore isn't cluster aligned as well.
 *
 * Returns:
 *   0:     if no allocated clusters are available at the given offset.
 *          *bytes is normally unchanged. It is set to 0 if the cluster
 *          is allocated and doesn't need COW, but doesn't have the right
 *          physical offset.
 *
 *   1:     if allocated clusters that don't require a COW are available at
 *          the requested offset. *bytes may have decreased and describes
 *          the length of the area that can be written to.
 *
 *  -errno: in error cases
 */
static int handle_copied(BlockDriverState *bs, uint64_t guest_offset,
    uint64_t *host_offset, uint64_t *bytes, QCowL2Meta **m)
{
    BDRVQcow2State *s = bs->opaque;
    int l2_index;
    uint64_t cluster_offset;
    uint64_t *l2_table;
    uint64_t nb_clusters;
    unsigned int keep_clusters;
    int ret;

    trace_qcow2_handle_copied(qemu_coroutine_self(), guest_offset, *host_offset,
                              *bytes);

    assert(*host_offset == 0 ||    offset_into_cluster(s, guest_offset)
                                == offset_into_cluster(s, *host_offset));

    /*
     * Calculate the number of clusters to look for. We stop at L2 table
     * boundaries to keep things simple.
     */
    nb_clusters =
        size_to_clusters(s, offset_into_cluster(s, guest_offset) + *bytes);

    l2_index = offset_to_l2_index(s, guest_offset);
    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    /* Find L2 entry for the first involved cluster */
    ret = get_cluster_table(bs, guest_offset, &l2_table, &l2_index);
    if (ret < 0) {
        return ret;
    }

    cluster_offset = be64_to_cpu(l2_table[l2_index]);

    /* Check how many clusters are already allocated and don't need COW */
    if (qcow2_get_cluster_type(cluster_offset) == QCOW2_CLUSTER_NORMAL
        && (cluster_offset & QCOW_OFLAG_COPIED))
    {
        /* If a specific host_offset is required, check it */
        bool offset_matches =
            (cluster_offset & L2E_OFFSET_MASK) == *host_offset;

        if (offset_into_cluster(s, cluster_offset & L2E_OFFSET_MASK)) {
            qcow2_signal_corruption(bs, true, -1, -1, "Data cluster offset "
                                    "%#llx unaligned (guest offset: %#" PRIx64
                                    ")", cluster_offset & L2E_OFFSET_MASK,
                                    guest_offset);
            ret = -EIO;
            goto out;
        }

        if (*host_offset != 0 && !offset_matches) {
            *bytes = 0;
            ret = 0;
            goto out;
        }

        /* We keep all QCOW_OFLAG_COPIED clusters */
        keep_clusters =
            count_contiguous_clusters(nb_clusters, s->cluster_size,
                                      &l2_table[l2_index],
                                      QCOW_OFLAG_COPIED | QCOW_OFLAG_ZERO);
        assert(keep_clusters <= nb_clusters);

        *bytes = MIN(*bytes,
                 keep_clusters * s->cluster_size
                 - offset_into_cluster(s, guest_offset));

        ret = 1;
    } else {
        ret = 0;
    }

    /* Cleanup */
out:
    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    /* Only return a host offset if we actually made progress. Otherwise we
     * would make requirements for handle_alloc() that it can't fulfill */
    if (ret > 0) {
        *host_offset = (cluster_offset & L2E_OFFSET_MASK)
                     + offset_into_cluster(s, guest_offset);
    }

    return ret;
}

/*
 * Allocates new clusters for the given guest_offset.
 *
 * At most *nb_clusters are allocated, and on return *nb_clusters is updated to
 * contain the number of clusters that have been allocated and are contiguous
 * in the image file.
 *
 * If *host_offset is non-zero, it specifies the offset in the image file at
 * which the new clusters must start. *nb_clusters can be 0 on return in this
 * case if the cluster at host_offset is already in use. If *host_offset is
 * zero, the clusters can be allocated anywhere in the image file.
 *
 * *host_offset is updated to contain the offset into the image file at which
 * the first allocated cluster starts.
 *
 * Return 0 on success and -errno in error cases. -EAGAIN means that the
 * function has been waiting for another request and the allocation must be
 * restarted, but the whole request should not be failed.
 */
static int do_alloc_cluster_offset(BlockDriverState *bs, uint64_t guest_offset,
                                   uint64_t *host_offset, uint64_t *nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;

    trace_qcow2_do_alloc_clusters_offset(qemu_coroutine_self(), guest_offset,
                                         *host_offset, *nb_clusters);

    /* Allocate new clusters */
    trace_qcow2_cluster_alloc_phys(qemu_coroutine_self());
    if (*host_offset == 0) {
        int64_t cluster_offset =
            qcow2_alloc_clusters(bs, *nb_clusters * s->cluster_size);
        if (cluster_offset < 0) {
            return cluster_offset;
        }
        *host_offset = cluster_offset;
        return 0;
    } else {
        int64_t ret = qcow2_alloc_clusters_at(bs, *host_offset, *nb_clusters);
        if (ret < 0) {
            return ret;
        }
        *nb_clusters = ret;
        return 0;
    }
}

/*
 * Allocates new clusters for an area that either is yet unallocated or needs a
 * copy on write. If *host_offset is non-zero, clusters are only allocated if
 * the new allocation can match the specified host offset.
 *
 * Note that guest_offset may not be cluster aligned. In this case, the
 * returned *host_offset points to exact byte referenced by guest_offset and
 * therefore isn't cluster aligned as well.
 *
 * Returns:
 *   0:     if no clusters could be allocated. *bytes is set to 0,
 *          *host_offset is left unchanged.
 *
 *   1:     if new clusters were allocated. *bytes may be decreased if the
 *          new allocation doesn't cover all of the requested area.
 *          *host_offset is updated to contain the host offset of the first
 *          newly allocated cluster.
 *
 *  -errno: in error cases
 */
static int handle_alloc(BlockDriverState *bs, uint64_t guest_offset,
    uint64_t *host_offset, uint64_t *bytes, QCowL2Meta **m)
{
    BDRVQcow2State *s = bs->opaque;
    int l2_index;
    uint64_t *l2_table;
    uint64_t entry;
    uint64_t nb_clusters;
    int ret;
    bool keep_old_clusters = false;

    uint64_t alloc_cluster_offset = 0;

    trace_qcow2_handle_alloc(qemu_coroutine_self(), guest_offset, *host_offset,
                             *bytes);
    assert(*bytes > 0);

    /*
     * Calculate the number of clusters to look for. We stop at L2 table
     * boundaries to keep things simple.
     */
    nb_clusters =
        size_to_clusters(s, offset_into_cluster(s, guest_offset) + *bytes);

    l2_index = offset_to_l2_index(s, guest_offset);
    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    /* Find L2 entry for the first involved cluster */
    ret = get_cluster_table(bs, guest_offset, &l2_table, &l2_index);
    if (ret < 0) {
        return ret;
    }

    entry = be64_to_cpu(l2_table[l2_index]);

    /* For the moment, overwrite compressed clusters one by one */
    if (entry & QCOW_OFLAG_COMPRESSED) {
        nb_clusters = 1;
    } else {
        nb_clusters = count_cow_clusters(s, nb_clusters, l2_table, l2_index);
    }

    /* This function is only called when there were no non-COW clusters, so if
     * we can't find any unallocated or COW clusters either, something is
     * wrong with our code. */
    assert(nb_clusters > 0);

    if (qcow2_get_cluster_type(entry) == QCOW2_CLUSTER_ZERO_ALLOC &&
        (entry & QCOW_OFLAG_COPIED) &&
        (!*host_offset ||
         start_of_cluster(s, *host_offset) == (entry & L2E_OFFSET_MASK)))
    {
        /* Try to reuse preallocated zero clusters; contiguous normal clusters
         * would be fine, too, but count_cow_clusters() above has limited
         * nb_clusters already to a range of COW clusters */
        int preallocated_nb_clusters =
            count_contiguous_clusters(nb_clusters, s->cluster_size,
                                      &l2_table[l2_index], QCOW_OFLAG_COPIED);
        assert(preallocated_nb_clusters > 0);

        nb_clusters = preallocated_nb_clusters;
        alloc_cluster_offset = entry & L2E_OFFSET_MASK;

        /* We want to reuse these clusters, so qcow2_alloc_cluster_link_l2()
         * should not free them. */
        keep_old_clusters = true;
    }

    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    if (!alloc_cluster_offset) {
        /* Allocate, if necessary at a given offset in the image file */
        alloc_cluster_offset = start_of_cluster(s, *host_offset);
        ret = do_alloc_cluster_offset(bs, guest_offset, &alloc_cluster_offset,
                                      &nb_clusters);
        if (ret < 0) {
            goto fail;
        }

        /* Can't extend contiguous allocation */
        if (nb_clusters == 0) {
            *bytes = 0;
            return 0;
        }

        /* !*host_offset would overwrite the image header and is reserved for
         * "no host offset preferred". If 0 was a valid host offset, it'd
         * trigger the following overlap check; do that now to avoid having an
         * invalid value in *host_offset. */
        if (!alloc_cluster_offset) {
            ret = qcow2_pre_write_overlap_check(bs, 0, alloc_cluster_offset,
                                                nb_clusters * s->cluster_size);
            assert(ret < 0);
            goto fail;
        }
    }

    /*
     * Save info needed for meta data update.
     *
     * requested_bytes: Number of bytes from the start of the first
     * newly allocated cluster to the end of the (possibly shortened
     * before) write request.
     *
     * avail_bytes: Number of bytes from the start of the first
     * newly allocated to the end of the last newly allocated cluster.
     *
     * nb_bytes: The number of bytes from the start of the first
     * newly allocated cluster to the end of the area that the write
     * request actually writes to (excluding COW at the end)
     */
    uint64_t requested_bytes = *bytes + offset_into_cluster(s, guest_offset);
    int avail_bytes = MIN(INT_MAX, nb_clusters << s->cluster_bits);
    int nb_bytes = MIN(requested_bytes, avail_bytes);
    QCowL2Meta *old_m = *m;

    *m = g_malloc0(sizeof(**m));

    **m = (QCowL2Meta) {
        .next           = old_m,

        .alloc_offset   = alloc_cluster_offset,
        .offset         = start_of_cluster(s, guest_offset),
        .nb_clusters    = nb_clusters,

        .keep_old_clusters  = keep_old_clusters,

        .cow_start = {
            .offset     = 0,
            .nb_bytes   = offset_into_cluster(s, guest_offset),
        },
        .cow_end = {
            .offset     = nb_bytes,
            .nb_bytes   = avail_bytes - nb_bytes,
        },
    };
    qemu_co_queue_init(&(*m)->dependent_requests);
    QLIST_INSERT_HEAD(&s->cluster_allocs, *m, next_in_flight);

    *host_offset = alloc_cluster_offset + offset_into_cluster(s, guest_offset);
    *bytes = MIN(*bytes, nb_bytes - offset_into_cluster(s, guest_offset));
    assert(*bytes != 0);

    return 1;

fail:
    if (*m && (*m)->nb_clusters > 0) {
        QLIST_REMOVE(*m, next_in_flight);
    }
    return ret;
}

/*
 * alloc_cluster_offset
 *
 * For a given offset on the virtual disk, find the cluster offset in qcow2
 * file. If the offset is not found, allocate a new cluster.
 *
 * If the cluster was already allocated, m->nb_clusters is set to 0 and
 * other fields in m are meaningless.
 *
 * If the cluster is newly allocated, m->nb_clusters is set to the number of
 * contiguous clusters that have been allocated. In this case, the other
 * fields of m are valid and contain information about the first allocated
 * cluster.
 *
 * If the request conflicts with another write request in flight, the coroutine
 * is queued and will be reentered when the dependency has completed.
 *
 * Return 0 on success and -errno in error cases
 */
int qcow2_alloc_cluster_offset(BlockDriverState *bs, uint64_t offset,
                               unsigned int *bytes, uint64_t *host_offset,
                               QCowL2Meta **m)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t start, remaining;
    uint64_t cluster_offset;
    uint64_t cur_bytes;
    int ret;

    trace_qcow2_alloc_clusters_offset(qemu_coroutine_self(), offset, *bytes);

again:
    start = offset;
    remaining = *bytes;
    cluster_offset = 0;
    *host_offset = 0;
    cur_bytes = 0;
    *m = NULL;

    while (true) {

        if (!*host_offset) {
            *host_offset = start_of_cluster(s, cluster_offset);
        }

        assert(remaining >= cur_bytes);

        start           += cur_bytes;
        remaining       -= cur_bytes;
        cluster_offset  += cur_bytes;

        if (remaining == 0) {
            break;
        }

        cur_bytes = remaining;

        /*
         * Now start gathering as many contiguous clusters as possible:
         *
         * 1. Check for overlaps with in-flight allocations
         *
         *      a) Overlap not in the first cluster -> shorten this request and
         *         let the caller handle the rest in its next loop iteration.
         *
         *      b) Real overlaps of two requests. Yield and restart the search
         *         for contiguous clusters (the situation could have changed
         *         while we were sleeping)
         *
         *      c) TODO: Request starts in the same cluster as the in-flight
         *         allocation ends. Shorten the COW of the in-fight allocation,
         *         set cluster_offset to write to the same cluster and set up
         *         the right synchronisation between the in-flight request and
         *         the new one.
         */
        ret = handle_dependencies(bs, start, &cur_bytes, m);
        if (ret == -EAGAIN) {
            /* Currently handle_dependencies() doesn't yield if we already had
             * an allocation. If it did, we would have to clean up the L2Meta
             * structs before starting over. */
            assert(*m == NULL);
            goto again;
        } else if (ret < 0) {
            return ret;
        } else if (cur_bytes == 0) {
            break;
        } else {
            /* handle_dependencies() may have decreased cur_bytes (shortened
             * the allocations below) so that the next dependency is processed
             * correctly during the next loop iteration. */
        }

        /*
         * 2. Count contiguous COPIED clusters.
         */
        ret = handle_copied(bs, start, &cluster_offset, &cur_bytes, m);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            continue;
        } else if (cur_bytes == 0) {
            break;
        }

        /*
         * 3. If the request still hasn't completed, allocate new clusters,
         *    considering any cluster_offset of steps 1c or 2.
         */
        ret = handle_alloc(bs, start, &cluster_offset, &cur_bytes, m);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            continue;
        } else {
            assert(cur_bytes == 0);
            break;
        }
    }

    *bytes -= remaining;
    assert(*bytes > 0);
    assert(*host_offset != 0);

    return 0;
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

int qcow2_decompress_cluster(BlockDriverState *bs, uint64_t cluster_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int ret, csize, nb_csectors, sector_offset;
    uint64_t coffset;

    coffset = cluster_offset & s->cluster_offset_mask;
    if (s->cluster_cache_offset != coffset) {
        nb_csectors = ((cluster_offset >> s->csize_shift) & s->csize_mask) + 1;
        sector_offset = coffset & 511;
        csize = nb_csectors * 512 - sector_offset;
        BLKDBG_EVENT(bs->file, BLKDBG_READ_COMPRESSED);
        ret = bdrv_read(bs->file, coffset >> 9, s->cluster_data,
                        nb_csectors);
        if (ret < 0) {
            return ret;
        }
        if (decompress_buffer(s->cluster_cache, s->cluster_size,
                              s->cluster_data + sector_offset, csize) < 0) {
            return -EIO;
        }
        s->cluster_cache_offset = coffset;
    }
    return 0;
}

/*
 * This discards as many clusters of nb_clusters as possible at once (i.e.
 * all clusters in the same L2 table) and returns the number of discarded
 * clusters.
 */
static int discard_single_l2(BlockDriverState *bs, uint64_t offset,
                             uint64_t nb_clusters, enum qcow2_discard_type type,
                             bool full_discard)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_table;
    int l2_index;
    int ret;
    int i;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Limit nb_clusters to one L2 table */
    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t old_l2_entry;

        old_l2_entry = be64_to_cpu(l2_table[l2_index + i]);

        /*
         * If full_discard is false, make sure that a discarded area reads back
         * as zeroes for v3 images (we cannot do it for v2 without actually
         * writing a zero-filled buffer). We can skip the operation if the
         * cluster is already marked as zero, or if it's unallocated and we
         * don't have a backing file.
         *
         * TODO We might want to use bdrv_get_block_status(bs) here, but we're
         * holding s->lock, so that doesn't work today.
         *
         * If full_discard is true, the sector should not read back as zeroes,
         * but rather fall through to the backing file.
         */
        switch (qcow2_get_cluster_type(old_l2_entry)) {
        case QCOW2_CLUSTER_UNALLOCATED:
            if (full_discard || !bs->backing) {
                continue;
            }
            break;

        case QCOW2_CLUSTER_ZERO_PLAIN:
            if (!full_discard) {
                continue;
            }
            break;

        case QCOW2_CLUSTER_ZERO_ALLOC:
        case QCOW2_CLUSTER_NORMAL:
        case QCOW2_CLUSTER_COMPRESSED:
            break;

        default:
            abort();
        }

        /* First remove L2 entries */
        qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);
        if (!full_discard && s->qcow_version >= 3) {
            l2_table[l2_index + i] = cpu_to_be64(QCOW_OFLAG_ZERO);
        } else {
            l2_table[l2_index + i] = cpu_to_be64(0);
        }

        /* Then decrease the refcount */
        qcow2_free_any_clusters(bs, old_l2_entry, 1, type);
    }

    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    return nb_clusters;
}

int qcow2_cluster_discard(BlockDriverState *bs, uint64_t offset,
                          uint64_t bytes, enum qcow2_discard_type type,
                          bool full_discard)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t end_offset = offset + bytes;
    uint64_t nb_clusters;
    int64_t cleared;
    int ret;

    /* Caller must pass aligned values, except at image end */
    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(end_offset, s->cluster_size) ||
           end_offset == bs->total_sectors << BDRV_SECTOR_BITS);

    nb_clusters = size_to_clusters(s, bytes);

    s->cache_discards = true;

    /* Each L2 table is handled by its own loop iteration */
    while (nb_clusters > 0) {
        cleared = discard_single_l2(bs, offset, nb_clusters, type,
                                    full_discard);
        if (cleared < 0) {
            ret = cleared;
            goto fail;
        }

        nb_clusters -= cleared;
        offset += (cleared * s->cluster_size);
    }

    ret = 0;
fail:
    s->cache_discards = false;
    qcow2_process_discards(bs, ret);

    return ret;
}

/*
 * This zeroes as many clusters of nb_clusters as possible at once (i.e.
 * all clusters in the same L2 table) and returns the number of zeroed
 * clusters.
 */
static int zero_single_l2(BlockDriverState *bs, uint64_t offset,
                          uint64_t nb_clusters, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_table;
    int l2_index;
    int ret;
    int i;
    bool unmap = !!(flags & BDRV_REQ_MAY_UNMAP);

    ret = get_cluster_table(bs, offset, &l2_table, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Limit nb_clusters to one L2 table */
    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t old_offset;
        QCow2ClusterType cluster_type;

        old_offset = be64_to_cpu(l2_table[l2_index + i]);

        /*
         * Minimize L2 changes if the cluster already reads back as
         * zeroes with correct allocation.
         */
        cluster_type = qcow2_get_cluster_type(old_offset);
        if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN ||
            (cluster_type == QCOW2_CLUSTER_ZERO_ALLOC && !unmap)) {
            continue;
        }

        qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);
        if (cluster_type == QCOW2_CLUSTER_COMPRESSED || unmap) {
            l2_table[l2_index + i] = cpu_to_be64(QCOW_OFLAG_ZERO);
            qcow2_free_any_clusters(bs, old_offset, 1, QCOW2_DISCARD_REQUEST);
        } else {
            l2_table[l2_index + i] |= cpu_to_be64(QCOW_OFLAG_ZERO);
        }
    }

    qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

    return nb_clusters;
}

int qcow2_cluster_zeroize(BlockDriverState *bs, uint64_t offset,
                          uint64_t bytes, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t end_offset = offset + bytes;
    uint64_t nb_clusters;
    int64_t cleared;
    int ret;

    /* Caller must pass aligned values, except at image end */
    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(end_offset, s->cluster_size) ||
           end_offset == bs->total_sectors << BDRV_SECTOR_BITS);

    /* The zero flag is only supported by version 3 and newer */
    if (s->qcow_version < 3) {
        return -ENOTSUP;
    }

    /* Each L2 table is handled by its own loop iteration */
    nb_clusters = size_to_clusters(s, bytes);

    s->cache_discards = true;

    while (nb_clusters > 0) {
        cleared = zero_single_l2(bs, offset, nb_clusters, flags);
        if (cleared < 0) {
            ret = cleared;
            goto fail;
        }

        nb_clusters -= cleared;
        offset += (cleared * s->cluster_size);
    }

    ret = 0;
fail:
    s->cache_discards = false;
    qcow2_process_discards(bs, ret);

    return ret;
}

/*
 * Expands all zero clusters in a specific L1 table (or deallocates them, for
 * non-backed non-pre-allocated zero clusters).
 *
 * l1_entries and *visited_l1_entries are used to keep track of progress for
 * status_cb(). l1_entries contains the total number of L1 entries and
 * *visited_l1_entries counts all visited L1 entries.
 */
static int expand_zero_clusters_in_l1(BlockDriverState *bs, uint64_t *l1_table,
                                      int l1_size, int64_t *visited_l1_entries,
                                      int64_t l1_entries,
                                      BlockDriverAmendStatusCB *status_cb,
                                      void *cb_opaque)
{
    BDRVQcow2State *s = bs->opaque;
    bool is_active_l1 = (l1_table == s->l1_table);
    uint64_t *l2_table = NULL;
    int ret;
    int i, j;

    if (!is_active_l1) {
        /* inactive L2 tables require a buffer to be stored in when loading
         * them from disk */
        l2_table = qemu_try_blockalign(bs->file->bs, s->cluster_size);
        if (l2_table == NULL) {
            return -ENOMEM;
        }
    }

    for (i = 0; i < l1_size; i++) {
        uint64_t l2_offset = l1_table[i] & L1E_OFFSET_MASK;
        bool l2_dirty = false;
        uint64_t l2_refcount;

        if (!l2_offset) {
            /* unallocated */
            (*visited_l1_entries)++;
            if (status_cb) {
                status_cb(bs, *visited_l1_entries, l1_entries, cb_opaque);
            }
            continue;
        }

        if (offset_into_cluster(s, l2_offset)) {
            qcow2_signal_corruption(bs, true, -1, -1, "L2 table offset %#"
                                    PRIx64 " unaligned (L1 index: %#x)",
                                    l2_offset, i);
            ret = -EIO;
            goto fail;
        }

        if (is_active_l1) {
            /* get active L2 tables from cache */
            ret = qcow2_cache_get(bs, s->l2_table_cache, l2_offset,
                    (void **)&l2_table);
        } else {
            /* load inactive L2 tables from disk */
            ret = bdrv_read(bs->file, l2_offset / BDRV_SECTOR_SIZE,
                            (void *)l2_table, s->cluster_sectors);
        }
        if (ret < 0) {
            goto fail;
        }

        ret = qcow2_get_refcount(bs, l2_offset >> s->cluster_bits,
                                 &l2_refcount);
        if (ret < 0) {
            goto fail;
        }

        for (j = 0; j < s->l2_size; j++) {
            uint64_t l2_entry = be64_to_cpu(l2_table[j]);
            int64_t offset = l2_entry & L2E_OFFSET_MASK;
            QCow2ClusterType cluster_type = qcow2_get_cluster_type(l2_entry);

            if (cluster_type != QCOW2_CLUSTER_ZERO_PLAIN &&
                cluster_type != QCOW2_CLUSTER_ZERO_ALLOC) {
                continue;
            }

            if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                if (!bs->backing) {
                    /* not backed; therefore we can simply deallocate the
                     * cluster */
                    l2_table[j] = 0;
                    l2_dirty = true;
                    continue;
                }

                offset = qcow2_alloc_clusters(bs, s->cluster_size);
                if (offset < 0) {
                    ret = offset;
                    goto fail;
                }

                if (l2_refcount > 1) {
                    /* For shared L2 tables, set the refcount accordingly (it is
                     * already 1 and needs to be l2_refcount) */
                    ret = qcow2_update_cluster_refcount(bs,
                            offset >> s->cluster_bits,
                            refcount_diff(1, l2_refcount), false,
                            QCOW2_DISCARD_OTHER);
                    if (ret < 0) {
                        qcow2_free_clusters(bs, offset, s->cluster_size,
                                            QCOW2_DISCARD_OTHER);
                        goto fail;
                    }
                }
            }

            if (offset_into_cluster(s, offset)) {
                qcow2_signal_corruption(bs, true, -1, -1,
                                        "Cluster allocation offset "
                                        "%#" PRIx64 " unaligned (L2 offset: %#"
                                        PRIx64 ", L2 index: %#x)", offset,
                                        l2_offset, j);
                if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                    qcow2_free_clusters(bs, offset, s->cluster_size,
                                        QCOW2_DISCARD_ALWAYS);
                }
                ret = -EIO;
                goto fail;
            }

            ret = qcow2_pre_write_overlap_check(bs, 0, offset, s->cluster_size);
            if (ret < 0) {
                if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                    qcow2_free_clusters(bs, offset, s->cluster_size,
                                        QCOW2_DISCARD_ALWAYS);
                }
                goto fail;
            }

            ret = bdrv_pwrite_zeroes(bs->file, offset, s->cluster_size, 0);
            if (ret < 0) {
                if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                    qcow2_free_clusters(bs, offset, s->cluster_size,
                                        QCOW2_DISCARD_ALWAYS);
                }
                goto fail;
            }

            if (l2_refcount == 1) {
                l2_table[j] = cpu_to_be64(offset | QCOW_OFLAG_COPIED);
            } else {
                l2_table[j] = cpu_to_be64(offset);
            }
            l2_dirty = true;
        }

        if (is_active_l1) {
            if (l2_dirty) {
                qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache, l2_table);
                qcow2_cache_depends_on_flush(s->l2_table_cache);
            }
            qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);
        } else {
            if (l2_dirty) {
                ret = qcow2_pre_write_overlap_check(bs,
                        QCOW2_OL_INACTIVE_L2 | QCOW2_OL_ACTIVE_L2, l2_offset,
                        s->cluster_size);
                if (ret < 0) {
                    goto fail;
                }

                ret = bdrv_write(bs->file, l2_offset / BDRV_SECTOR_SIZE,
                                 (void *)l2_table, s->cluster_sectors);
                if (ret < 0) {
                    goto fail;
                }
            }
        }

        (*visited_l1_entries)++;
        if (status_cb) {
            status_cb(bs, *visited_l1_entries, l1_entries, cb_opaque);
        }
    }

    ret = 0;

fail:
    if (l2_table) {
        if (!is_active_l1) {
            qemu_vfree(l2_table);
        } else {
            qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);
        }
    }
    return ret;
}

/*
 * For backed images, expands all zero clusters on the image. For non-backed
 * images, deallocates all non-pre-allocated zero clusters (and claims the
 * allocation for pre-allocated ones). This is important for downgrading to a
 * qcow2 version which doesn't yet support metadata zero clusters.
 */
int qcow2_expand_zero_clusters(BlockDriverState *bs,
                               BlockDriverAmendStatusCB *status_cb,
                               void *cb_opaque)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l1_table = NULL;
    int64_t l1_entries = 0, visited_l1_entries = 0;
    int ret;
    int i, j;

    if (status_cb) {
        l1_entries = s->l1_size;
        for (i = 0; i < s->nb_snapshots; i++) {
            l1_entries += s->snapshots[i].l1_size;
        }
    }

    ret = expand_zero_clusters_in_l1(bs, s->l1_table, s->l1_size,
                                     &visited_l1_entries, l1_entries,
                                     status_cb, cb_opaque);
    if (ret < 0) {
        goto fail;
    }

    /* Inactive L1 tables may point to active L2 tables - therefore it is
     * necessary to flush the L2 table cache before trying to access the L2
     * tables pointed to by inactive L1 entries (else we might try to expand
     * zero clusters that have already been expanded); furthermore, it is also
     * necessary to empty the L2 table cache, since it may contain tables which
     * are now going to be modified directly on disk, bypassing the cache.
     * qcow2_cache_empty() does both for us. */
    ret = qcow2_cache_empty(bs, s->l2_table_cache);
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->nb_snapshots; i++) {
        int l1_sectors = DIV_ROUND_UP(s->snapshots[i].l1_size *
                                      sizeof(uint64_t), BDRV_SECTOR_SIZE);

        l1_table = g_realloc(l1_table, l1_sectors * BDRV_SECTOR_SIZE);

        ret = bdrv_read(bs->file,
                        s->snapshots[i].l1_table_offset / BDRV_SECTOR_SIZE,
                        (void *)l1_table, l1_sectors);
        if (ret < 0) {
            goto fail;
        }

        for (j = 0; j < s->snapshots[i].l1_size; j++) {
            be64_to_cpus(&l1_table[j]);
        }

        ret = expand_zero_clusters_in_l1(bs, l1_table, s->snapshots[i].l1_size,
                                         &visited_l1_entries, l1_entries,
                                         status_cb, cb_opaque);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;

fail:
    g_free(l1_table);
    return ret;
}
