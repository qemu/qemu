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
#include "qcow2.h"
#include "qemu/bswap.h"
#include "trace.h"

int qcow2_shrink_l1_table(BlockDriverState *bs, uint64_t exact_size)
{
    BDRVQcow2State *s = bs->opaque;
    int new_l1_size, i, ret;

    if (exact_size >= s->l1_size) {
        return 0;
    }

    new_l1_size = exact_size;

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "shrink l1_table from %d to %d\n", s->l1_size, new_l1_size);
#endif

    BLKDBG_EVENT(bs->file, BLKDBG_L1_SHRINK_WRITE_TABLE);
    ret = bdrv_pwrite_zeroes(bs->file, s->l1_table_offset +
                                       new_l1_size * L1E_SIZE,
                             (s->l1_size - new_l1_size) * L1E_SIZE, 0);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_flush(bs->file->bs);
    if (ret < 0) {
        goto fail;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_SHRINK_FREE_L2_CLUSTERS);
    for (i = s->l1_size - 1; i > new_l1_size - 1; i--) {
        if ((s->l1_table[i] & L1E_OFFSET_MASK) == 0) {
            continue;
        }
        qcow2_free_clusters(bs, s->l1_table[i] & L1E_OFFSET_MASK,
                            s->cluster_size, QCOW2_DISCARD_ALWAYS);
        s->l1_table[i] = 0;
    }
    return 0;

fail:
    /*
     * If the write in the l1_table failed the image may contain a partially
     * overwritten l1_table. In this case it would be better to clear the
     * l1_table in memory to avoid possible image corruption.
     */
    memset(s->l1_table + new_l1_size, 0,
           (s->l1_size - new_l1_size) * L1E_SIZE);
    return ret;
}

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
    if (min_size > INT_MAX / L1E_SIZE) {
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
            new_l1_size = DIV_ROUND_UP(new_l1_size * 3, 2);
        }
    }

    QEMU_BUILD_BUG_ON(QCOW_MAX_L1_SIZE > INT_MAX);
    if (new_l1_size > QCOW_MAX_L1_SIZE / L1E_SIZE) {
        return -EFBIG;
    }

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "grow l1_table from %d to %" PRId64 "\n",
            s->l1_size, new_l1_size);
#endif

    new_l1_size2 = L1E_SIZE * new_l1_size;
    new_l1_table = qemu_try_blockalign(bs->file->bs, new_l1_size2);
    if (new_l1_table == NULL) {
        return -ENOMEM;
    }
    memset(new_l1_table, 0, new_l1_size2);

    if (s->l1_size) {
        memcpy(new_l1_table, s->l1_table, s->l1_size * L1E_SIZE);
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
                                        new_l1_size2, false);
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
    qcow2_free_clusters(bs, old_l1_table_offset, old_l1_size * L1E_SIZE,
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
 * @bs: The BlockDriverState
 * @offset: A guest offset, used to calculate what slice of the L2
 *          table to load.
 * @l2_offset: Offset to the L2 table in the image file.
 * @l2_slice: Location to store the pointer to the L2 slice.
 *
 * Loads a L2 slice into memory (L2 slices are the parts of L2 tables
 * that are loaded by the qcow2 cache). If the slice is in the cache,
 * the cache is used; otherwise the L2 slice is loaded from the image
 * file.
 */
static int l2_load(BlockDriverState *bs, uint64_t offset,
                   uint64_t l2_offset, uint64_t **l2_slice)
{
    BDRVQcow2State *s = bs->opaque;
    int start_of_slice = l2_entry_size(s) *
        (offset_to_l2_index(s, offset) - offset_to_l2_slice_index(s, offset));

    return qcow2_cache_get(bs, s->l2_table_cache, l2_offset + start_of_slice,
                           (void **)l2_slice);
}

/*
 * Writes an L1 entry to disk (note that depending on the alignment
 * requirements this function may write more that just one entry in
 * order to prevent bdrv_pwrite from performing a read-modify-write)
 */
int qcow2_write_l1_entry(BlockDriverState *bs, int l1_index)
{
    BDRVQcow2State *s = bs->opaque;
    int l1_start_index;
    int i, ret;
    int bufsize = MAX(L1E_SIZE,
                      MIN(bs->file->bs->bl.request_alignment, s->cluster_size));
    int nentries = bufsize / L1E_SIZE;
    g_autofree uint64_t *buf = g_try_new0(uint64_t, nentries);

    if (buf == NULL) {
        return -ENOMEM;
    }

    l1_start_index = QEMU_ALIGN_DOWN(l1_index, nentries);
    for (i = 0; i < MIN(nentries, s->l1_size - l1_start_index); i++) {
        buf[i] = cpu_to_be64(s->l1_table[l1_start_index + i]);
    }

    ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_ACTIVE_L1,
            s->l1_table_offset + L1E_SIZE * l1_start_index, bufsize, false);
    if (ret < 0) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);
    ret = bdrv_pwrite_sync(bs->file,
                           s->l1_table_offset + L1E_SIZE * l1_start_index,
                           buf, bufsize);
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

static int l2_allocate(BlockDriverState *bs, int l1_index)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t old_l2_offset;
    uint64_t *l2_slice = NULL;
    unsigned slice, slice_size2, n_slices;
    int64_t l2_offset;
    int ret;

    old_l2_offset = s->l1_table[l1_index];

    trace_qcow2_l2_allocate(bs, l1_index);

    /* allocate a new l2 entry */

    l2_offset = qcow2_alloc_clusters(bs, s->l2_size * l2_entry_size(s));
    if (l2_offset < 0) {
        ret = l2_offset;
        goto fail;
    }

    /* The offset must fit in the offset field of the L1 table entry */
    assert((l2_offset & L1E_OFFSET_MASK) == l2_offset);

    /* If we're allocating the table at offset 0 then something is wrong */
    if (l2_offset == 0) {
        qcow2_signal_corruption(bs, true, -1, -1, "Preventing invalid "
                                "allocation of L2 table at offset 0");
        ret = -EIO;
        goto fail;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* allocate a new entry in the l2 cache */

    slice_size2 = s->l2_slice_size * l2_entry_size(s);
    n_slices = s->cluster_size / slice_size2;

    trace_qcow2_l2_allocate_get_empty(bs, l1_index);
    for (slice = 0; slice < n_slices; slice++) {
        ret = qcow2_cache_get_empty(bs, s->l2_table_cache,
                                    l2_offset + slice * slice_size2,
                                    (void **) &l2_slice);
        if (ret < 0) {
            goto fail;
        }

        if ((old_l2_offset & L1E_OFFSET_MASK) == 0) {
            /* if there was no old l2 table, clear the new slice */
            memset(l2_slice, 0, slice_size2);
        } else {
            uint64_t *old_slice;
            uint64_t old_l2_slice_offset =
                (old_l2_offset & L1E_OFFSET_MASK) + slice * slice_size2;

            /* if there was an old l2 table, read a slice from the disk */
            BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_COW_READ);
            ret = qcow2_cache_get(bs, s->l2_table_cache, old_l2_slice_offset,
                                  (void **) &old_slice);
            if (ret < 0) {
                goto fail;
            }

            memcpy(l2_slice, old_slice, slice_size2);

            qcow2_cache_put(s->l2_table_cache, (void **) &old_slice);
        }

        /* write the l2 slice to the file */
        BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_WRITE);

        trace_qcow2_l2_allocate_write_l2(bs, l1_index);
        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
        qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
    }

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

    trace_qcow2_l2_allocate_done(bs, l1_index, 0);
    return 0;

fail:
    trace_qcow2_l2_allocate_done(bs, l1_index, ret);
    if (l2_slice != NULL) {
        qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
    }
    s->l1_table[l1_index] = old_l2_offset;
    if (l2_offset > 0) {
        qcow2_free_clusters(bs, l2_offset, s->l2_size * l2_entry_size(s),
                            QCOW2_DISCARD_ALWAYS);
    }
    return ret;
}

/*
 * For a given L2 entry, count the number of contiguous subclusters of
 * the same type starting from @sc_from. Compressed clusters are
 * treated as if they were divided into subclusters of size
 * s->subcluster_size.
 *
 * Return the number of contiguous subclusters and set @type to the
 * subcluster type.
 *
 * If the L2 entry is invalid return -errno and set @type to
 * QCOW2_SUBCLUSTER_INVALID.
 */
static int qcow2_get_subcluster_range_type(BlockDriverState *bs,
                                           uint64_t l2_entry,
                                           uint64_t l2_bitmap,
                                           unsigned sc_from,
                                           QCow2SubclusterType *type)
{
    BDRVQcow2State *s = bs->opaque;
    uint32_t val;

    *type = qcow2_get_subcluster_type(bs, l2_entry, l2_bitmap, sc_from);

    if (*type == QCOW2_SUBCLUSTER_INVALID) {
        return -EINVAL;
    } else if (!has_subclusters(s) || *type == QCOW2_SUBCLUSTER_COMPRESSED) {
        return s->subclusters_per_cluster - sc_from;
    }

    switch (*type) {
    case QCOW2_SUBCLUSTER_NORMAL:
        val = l2_bitmap | QCOW_OFLAG_SUB_ALLOC_RANGE(0, sc_from);
        return cto32(val) - sc_from;

    case QCOW2_SUBCLUSTER_ZERO_PLAIN:
    case QCOW2_SUBCLUSTER_ZERO_ALLOC:
        val = (l2_bitmap | QCOW_OFLAG_SUB_ZERO_RANGE(0, sc_from)) >> 32;
        return cto32(val) - sc_from;

    case QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN:
    case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC:
        val = ((l2_bitmap >> 32) | l2_bitmap)
            & ~QCOW_OFLAG_SUB_ALLOC_RANGE(0, sc_from);
        return ctz32(val) - sc_from;

    default:
        g_assert_not_reached();
    }
}

/*
 * Return the number of contiguous subclusters of the exact same type
 * in a given L2 slice, starting from cluster @l2_index, subcluster
 * @sc_index. Allocated subclusters are required to be contiguous in
 * the image file.
 * At most @nb_clusters are checked (note that this means clusters,
 * not subclusters).
 * Compressed clusters are always processed one by one but for the
 * purpose of this count they are treated as if they were divided into
 * subclusters of size s->subcluster_size.
 * On failure return -errno and update @l2_index to point to the
 * invalid entry.
 */
static int count_contiguous_subclusters(BlockDriverState *bs, int nb_clusters,
                                        unsigned sc_index, uint64_t *l2_slice,
                                        unsigned *l2_index)
{
    BDRVQcow2State *s = bs->opaque;
    int i, count = 0;
    bool check_offset = false;
    uint64_t expected_offset = 0;
    QCow2SubclusterType expected_type = QCOW2_SUBCLUSTER_NORMAL, type;

    assert(*l2_index + nb_clusters <= s->l2_slice_size);

    for (i = 0; i < nb_clusters; i++) {
        unsigned first_sc = (i == 0) ? sc_index : 0;
        uint64_t l2_entry = get_l2_entry(s, l2_slice, *l2_index + i);
        uint64_t l2_bitmap = get_l2_bitmap(s, l2_slice, *l2_index + i);
        int ret = qcow2_get_subcluster_range_type(bs, l2_entry, l2_bitmap,
                                                  first_sc, &type);
        if (ret < 0) {
            *l2_index += i; /* Point to the invalid entry */
            return -EIO;
        }
        if (i == 0) {
            if (type == QCOW2_SUBCLUSTER_COMPRESSED) {
                /* Compressed clusters are always processed one by one */
                return ret;
            }
            expected_type = type;
            expected_offset = l2_entry & L2E_OFFSET_MASK;
            check_offset = (type == QCOW2_SUBCLUSTER_NORMAL ||
                            type == QCOW2_SUBCLUSTER_ZERO_ALLOC ||
                            type == QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC);
        } else if (type != expected_type) {
            break;
        } else if (check_offset) {
            expected_offset += s->cluster_size;
            if (expected_offset != (l2_entry & L2E_OFFSET_MASK)) {
                break;
            }
        }
        count += ret;
        /* Stop if there are type changes before the end of the cluster */
        if (first_sc + ret < s->subclusters_per_cluster) {
            break;
        }
    }

    return count;
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
    ret = bs->drv->bdrv_co_preadv_part(bs,
                                       src_cluster_offset + offset_in_cluster,
                                       qiov->size, qiov, 0, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int coroutine_fn do_perform_cow_write(BlockDriverState *bs,
                                             uint64_t cluster_offset,
                                             unsigned offset_in_cluster,
                                             QEMUIOVector *qiov)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    if (qiov->size == 0) {
        return 0;
    }

    ret = qcow2_pre_write_overlap_check(bs, 0,
            cluster_offset + offset_in_cluster, qiov->size, true);
    if (ret < 0) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_COW_WRITE);
    ret = bdrv_co_pwritev(s->data_file, cluster_offset + offset_in_cluster,
                          qiov->size, qiov, 0);
    if (ret < 0) {
        return ret;
    }

    return 0;
}


/*
 * get_host_offset
 *
 * For a given offset of the virtual disk find the equivalent host
 * offset in the qcow2 file and store it in *host_offset. Neither
 * offset needs to be aligned to a cluster boundary.
 *
 * If the cluster is unallocated then *host_offset will be 0.
 * If the cluster is compressed then *host_offset will contain the
 * complete compressed cluster descriptor.
 *
 * On entry, *bytes is the maximum number of contiguous bytes starting at
 * offset that we are interested in.
 *
 * On exit, *bytes is the number of bytes starting at offset that have the same
 * subcluster type and (if applicable) are stored contiguously in the image
 * file. The subcluster type is stored in *subcluster_type.
 * Compressed clusters are always processed one by one.
 *
 * Returns 0 on success, -errno in error cases.
 */
int qcow2_get_host_offset(BlockDriverState *bs, uint64_t offset,
                          unsigned int *bytes, uint64_t *host_offset,
                          QCow2SubclusterType *subcluster_type)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int l2_index, sc_index;
    uint64_t l1_index, l2_offset, *l2_slice, l2_entry, l2_bitmap;
    int sc;
    unsigned int offset_in_cluster;
    uint64_t bytes_available, bytes_needed, nb_clusters;
    QCow2SubclusterType type;
    int ret;

    offset_in_cluster = offset_into_cluster(s, offset);
    bytes_needed = (uint64_t) *bytes + offset_in_cluster;

    /* compute how many bytes there are between the start of the cluster
     * containing offset and the end of the l2 slice that contains
     * the entry pointing to it */
    bytes_available =
        ((uint64_t) (s->l2_slice_size - offset_to_l2_slice_index(s, offset)))
        << s->cluster_bits;

    if (bytes_needed > bytes_available) {
        bytes_needed = bytes_available;
    }

    *host_offset = 0;

    /* seek to the l2 offset in the l1 table */

    l1_index = offset_to_l1_index(s, offset);
    if (l1_index >= s->l1_size) {
        type = QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN;
        goto out;
    }

    l2_offset = s->l1_table[l1_index] & L1E_OFFSET_MASK;
    if (!l2_offset) {
        type = QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN;
        goto out;
    }

    if (offset_into_cluster(s, l2_offset)) {
        qcow2_signal_corruption(bs, true, -1, -1, "L2 table offset %#" PRIx64
                                " unaligned (L1 index: %#" PRIx64 ")",
                                l2_offset, l1_index);
        return -EIO;
    }

    /* load the l2 slice in memory */

    ret = l2_load(bs, offset, l2_offset, &l2_slice);
    if (ret < 0) {
        return ret;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = offset_to_l2_slice_index(s, offset);
    sc_index = offset_to_sc_index(s, offset);
    l2_entry = get_l2_entry(s, l2_slice, l2_index);
    l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index);

    nb_clusters = size_to_clusters(s, bytes_needed);
    /* bytes_needed <= *bytes + offset_in_cluster, both of which are unsigned
     * integers; the minimum cluster size is 512, so this assertion is always
     * true */
    assert(nb_clusters <= INT_MAX);

    type = qcow2_get_subcluster_type(bs, l2_entry, l2_bitmap, sc_index);
    if (s->qcow_version < 3 && (type == QCOW2_SUBCLUSTER_ZERO_PLAIN ||
                                type == QCOW2_SUBCLUSTER_ZERO_ALLOC)) {
        qcow2_signal_corruption(bs, true, -1, -1, "Zero cluster entry found"
                                " in pre-v3 image (L2 offset: %#" PRIx64
                                ", L2 index: %#x)", l2_offset, l2_index);
        ret = -EIO;
        goto fail;
    }
    switch (type) {
    case QCOW2_SUBCLUSTER_INVALID:
        break; /* This is handled by count_contiguous_subclusters() below */
    case QCOW2_SUBCLUSTER_COMPRESSED:
        if (has_data_file(bs)) {
            qcow2_signal_corruption(bs, true, -1, -1, "Compressed cluster "
                                    "entry found in image with external data "
                                    "file (L2 offset: %#" PRIx64 ", L2 index: "
                                    "%#x)", l2_offset, l2_index);
            ret = -EIO;
            goto fail;
        }
        *host_offset = l2_entry & L2E_COMPRESSED_OFFSET_SIZE_MASK;
        break;
    case QCOW2_SUBCLUSTER_ZERO_PLAIN:
    case QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN:
        break;
    case QCOW2_SUBCLUSTER_ZERO_ALLOC:
    case QCOW2_SUBCLUSTER_NORMAL:
    case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC: {
        uint64_t host_cluster_offset = l2_entry & L2E_OFFSET_MASK;
        *host_offset = host_cluster_offset + offset_in_cluster;
        if (offset_into_cluster(s, host_cluster_offset)) {
            qcow2_signal_corruption(bs, true, -1, -1,
                                    "Cluster allocation offset %#"
                                    PRIx64 " unaligned (L2 offset: %#" PRIx64
                                    ", L2 index: %#x)", host_cluster_offset,
                                    l2_offset, l2_index);
            ret = -EIO;
            goto fail;
        }
        if (has_data_file(bs) && *host_offset != offset) {
            qcow2_signal_corruption(bs, true, -1, -1,
                                    "External data file host cluster offset %#"
                                    PRIx64 " does not match guest cluster "
                                    "offset: %#" PRIx64
                                    ", L2 index: %#x)", host_cluster_offset,
                                    offset - offset_in_cluster, l2_index);
            ret = -EIO;
            goto fail;
        }
        break;
    }
    default:
        abort();
    }

    sc = count_contiguous_subclusters(bs, nb_clusters, sc_index,
                                      l2_slice, &l2_index);
    if (sc < 0) {
        qcow2_signal_corruption(bs, true, -1, -1, "Invalid cluster entry found "
                                " (L2 offset: %#" PRIx64 ", L2 index: %#x)",
                                l2_offset, l2_index);
        ret = -EIO;
        goto fail;
    }
    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    bytes_available = ((int64_t)sc + sc_index) << s->subcluster_bits;

out:
    if (bytes_available > bytes_needed) {
        bytes_available = bytes_needed;
    }

    /* bytes_available <= bytes_needed <= *bytes + offset_in_cluster;
     * subtracting offset_in_cluster will therefore definitely yield something
     * not exceeding UINT_MAX */
    assert(bytes_available - offset_in_cluster <= UINT_MAX);
    *bytes = bytes_available - offset_in_cluster;

    *subcluster_type = type;

    return 0;

fail:
    qcow2_cache_put(s->l2_table_cache, (void **)&l2_slice);
    return ret;
}

/*
 * get_cluster_table
 *
 * for a given disk offset, load (and allocate if needed)
 * the appropriate slice of its l2 table.
 *
 * the cluster index in the l2 slice is given to the caller.
 *
 * Returns 0 on success, -errno in failure case
 */
static int get_cluster_table(BlockDriverState *bs, uint64_t offset,
                             uint64_t **new_l2_slice,
                             int *new_l2_index)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int l2_index;
    uint64_t l1_index, l2_offset;
    uint64_t *l2_slice = NULL;
    int ret;

    /* seek to the l2 offset in the l1 table */

    l1_index = offset_to_l1_index(s, offset);
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

    if (!(s->l1_table[l1_index] & QCOW_OFLAG_COPIED)) {
        /* First allocate a new L2 table (and do COW if needed) */
        ret = l2_allocate(bs, l1_index);
        if (ret < 0) {
            return ret;
        }

        /* Then decrease the refcount of the old table */
        if (l2_offset) {
            qcow2_free_clusters(bs, l2_offset, s->l2_size * l2_entry_size(s),
                                QCOW2_DISCARD_OTHER);
        }

        /* Get the offset of the newly-allocated l2 table */
        l2_offset = s->l1_table[l1_index] & L1E_OFFSET_MASK;
        assert(offset_into_cluster(s, l2_offset) == 0);
    }

    /* load the l2 slice in memory */
    ret = l2_load(bs, offset, l2_offset, &l2_slice);
    if (ret < 0) {
        return ret;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = offset_to_l2_slice_index(s, offset);

    *new_l2_slice = l2_slice;
    *new_l2_index = l2_index;

    return 0;
}

/*
 * alloc_compressed_cluster_offset
 *
 * For a given offset on the virtual disk, allocate a new compressed cluster
 * and put the host offset of the cluster into *host_offset. If a cluster is
 * already allocated at the offset, return an error.
 *
 * Return 0 on success and -errno in error cases
 */
int qcow2_alloc_compressed_cluster_offset(BlockDriverState *bs,
                                          uint64_t offset,
                                          int compressed_size,
                                          uint64_t *host_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int l2_index, ret;
    uint64_t *l2_slice;
    int64_t cluster_offset;
    int nb_csectors;

    if (has_data_file(bs)) {
        return 0;
    }

    ret = get_cluster_table(bs, offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Compression can't overwrite anything. Fail if the cluster was already
     * allocated. */
    cluster_offset = get_l2_entry(s, l2_slice, l2_index);
    if (cluster_offset & L2E_OFFSET_MASK) {
        qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
        return -EIO;
    }

    cluster_offset = qcow2_alloc_bytes(bs, compressed_size);
    if (cluster_offset < 0) {
        qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
        return cluster_offset;
    }

    nb_csectors =
        (cluster_offset + compressed_size - 1) / QCOW2_COMPRESSED_SECTOR_SIZE -
        (cluster_offset / QCOW2_COMPRESSED_SECTOR_SIZE);

    /* The offset and size must fit in their fields of the L2 table entry */
    assert((cluster_offset & s->cluster_offset_mask) == cluster_offset);
    assert((nb_csectors & s->csize_mask) == nb_csectors);

    cluster_offset |= QCOW_OFLAG_COMPRESSED |
                      ((uint64_t)nb_csectors << s->csize_shift);

    /* update L2 table */

    /* compressed clusters never have the copied flag */

    BLKDBG_EVENT(bs->file, BLKDBG_L2_UPDATE_COMPRESSED);
    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
    set_l2_entry(s, l2_slice, l2_index, cluster_offset);
    if (has_subclusters(s)) {
        set_l2_bitmap(s, l2_slice, l2_index, 0);
    }
    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    *host_offset = cluster_offset & s->cluster_offset_mask;
    return 0;
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

    if ((start->nb_bytes == 0 && end->nb_bytes == 0) || m->skip_cow) {
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

    qemu_iovec_init(&qiov, 2 + (m->data_qiov ?
                                qemu_iovec_subvec_niov(m->data_qiov,
                                                       m->data_qiov_offset,
                                                       data_bytes)
                                : 0));

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
        ret = qcow2_co_encrypt(bs,
                               m->alloc_offset + start->offset,
                               m->offset + start->offset,
                               start_buffer, start->nb_bytes);
        if (ret < 0) {
            goto fail;
        }

        ret = qcow2_co_encrypt(bs,
                               m->alloc_offset + end->offset,
                               m->offset + end->offset,
                               end_buffer, end->nb_bytes);
        if (ret < 0) {
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
        qemu_iovec_concat(&qiov, m->data_qiov, m->data_qiov_offset, data_bytes);
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
    uint64_t *old_cluster, *l2_slice;
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

    ret = get_cluster_table(bs, m->offset, &l2_slice, &l2_index);
    if (ret < 0) {
        goto err;
    }
    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);

    assert(l2_index + m->nb_clusters <= s->l2_slice_size);
    assert(m->cow_end.offset + m->cow_end.nb_bytes <=
           m->nb_clusters << s->cluster_bits);
    for (i = 0; i < m->nb_clusters; i++) {
        uint64_t offset = cluster_offset + ((uint64_t)i << s->cluster_bits);
        /* if two concurrent writes happen to the same unallocated cluster
         * each write allocates separate cluster and writes data concurrently.
         * The first one to complete updates l2 table with pointer to its
         * cluster the second one has to do RMW (which is done above by
         * perform_cow()), update l2 table with its cluster pointer and free
         * old cluster. This is what this loop does */
        if (get_l2_entry(s, l2_slice, l2_index + i) != 0) {
            old_cluster[j++] = get_l2_entry(s, l2_slice, l2_index + i);
        }

        /* The offset must fit in the offset field of the L2 table entry */
        assert((offset & L2E_OFFSET_MASK) == offset);

        set_l2_entry(s, l2_slice, l2_index + i, offset | QCOW_OFLAG_COPIED);

        /* Update bitmap with the subclusters that were just written */
        if (has_subclusters(s) && !m->prealloc) {
            uint64_t l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index + i);
            unsigned written_from = m->cow_start.offset;
            unsigned written_to = m->cow_end.offset + m->cow_end.nb_bytes;
            int first_sc, last_sc;
            /* Narrow written_from and written_to down to the current cluster */
            written_from = MAX(written_from, i << s->cluster_bits);
            written_to   = MIN(written_to, (i + 1) << s->cluster_bits);
            assert(written_from < written_to);
            first_sc = offset_to_sc_index(s, written_from);
            last_sc  = offset_to_sc_index(s, written_to - 1);
            l2_bitmap |= QCOW_OFLAG_SUB_ALLOC_RANGE(first_sc, last_sc + 1);
            l2_bitmap &= ~QCOW_OFLAG_SUB_ZERO_RANGE(first_sc, last_sc + 1);
            set_l2_bitmap(s, l2_slice, l2_index + i, l2_bitmap);
        }
     }


    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    /*
     * If this was a COW, we need to decrease the refcount of the old cluster.
     *
     * Don't discard clusters that reach a refcount of 0 (e.g. compressed
     * clusters), the next write will reuse them anyway.
     */
    if (!m->keep_old_clusters && j != 0) {
        for (i = 0; i < j; i++) {
            qcow2_free_any_cluster(bs, old_cluster[i], QCOW2_DISCARD_NEVER);
        }
    }

    ret = 0;
err:
    g_free(old_cluster);
    return ret;
 }

/**
 * Frees the allocated clusters because the request failed and they won't
 * actually be linked.
 */
void qcow2_alloc_cluster_abort(BlockDriverState *bs, QCowL2Meta *m)
{
    BDRVQcow2State *s = bs->opaque;
    if (!has_data_file(bs) && !m->keep_old_clusters) {
        qcow2_free_clusters(bs, m->alloc_offset,
                            m->nb_clusters << s->cluster_bits,
                            QCOW2_DISCARD_NEVER);
    }
}

/*
 * For a given write request, create a new QCowL2Meta structure, add
 * it to @m and the BDRVQcow2State.cluster_allocs list. If the write
 * request does not need copy-on-write or changes to the L2 metadata
 * then this function does nothing.
 *
 * @host_cluster_offset points to the beginning of the first cluster.
 *
 * @guest_offset and @bytes indicate the offset and length of the
 * request.
 *
 * @l2_slice contains the L2 entries of all clusters involved in this
 * write request.
 *
 * If @keep_old is true it means that the clusters were already
 * allocated and will be overwritten. If false then the clusters are
 * new and we have to decrease the reference count of the old ones.
 *
 * Returns 0 on success, -errno on failure.
 */
static int calculate_l2_meta(BlockDriverState *bs, uint64_t host_cluster_offset,
                             uint64_t guest_offset, unsigned bytes,
                             uint64_t *l2_slice, QCowL2Meta **m, bool keep_old)
{
    BDRVQcow2State *s = bs->opaque;
    int sc_index, l2_index = offset_to_l2_slice_index(s, guest_offset);
    uint64_t l2_entry, l2_bitmap;
    unsigned cow_start_from, cow_end_to;
    unsigned cow_start_to = offset_into_cluster(s, guest_offset);
    unsigned cow_end_from = cow_start_to + bytes;
    unsigned nb_clusters = size_to_clusters(s, cow_end_from);
    QCowL2Meta *old_m = *m;
    QCow2SubclusterType type;
    int i;
    bool skip_cow = keep_old;

    assert(nb_clusters <= s->l2_slice_size - l2_index);

    /* Check the type of all affected subclusters */
    for (i = 0; i < nb_clusters; i++) {
        l2_entry = get_l2_entry(s, l2_slice, l2_index + i);
        l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index + i);
        if (skip_cow) {
            unsigned write_from = MAX(cow_start_to, i << s->cluster_bits);
            unsigned write_to = MIN(cow_end_from, (i + 1) << s->cluster_bits);
            int first_sc = offset_to_sc_index(s, write_from);
            int last_sc = offset_to_sc_index(s, write_to - 1);
            int cnt = qcow2_get_subcluster_range_type(bs, l2_entry, l2_bitmap,
                                                      first_sc, &type);
            /* Is any of the subclusters of type != QCOW2_SUBCLUSTER_NORMAL ? */
            if (type != QCOW2_SUBCLUSTER_NORMAL || first_sc + cnt <= last_sc) {
                skip_cow = false;
            }
        } else {
            /* If we can't skip the cow we can still look for invalid entries */
            type = qcow2_get_subcluster_type(bs, l2_entry, l2_bitmap, 0);
        }
        if (type == QCOW2_SUBCLUSTER_INVALID) {
            int l1_index = offset_to_l1_index(s, guest_offset);
            uint64_t l2_offset = s->l1_table[l1_index] & L1E_OFFSET_MASK;
            qcow2_signal_corruption(bs, true, -1, -1, "Invalid cluster "
                                    "entry found (L2 offset: %#" PRIx64
                                    ", L2 index: %#x)",
                                    l2_offset, l2_index + i);
            return -EIO;
        }
    }

    if (skip_cow) {
        return 0;
    }

    /* Get the L2 entry of the first cluster */
    l2_entry = get_l2_entry(s, l2_slice, l2_index);
    l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index);
    sc_index = offset_to_sc_index(s, guest_offset);
    type = qcow2_get_subcluster_type(bs, l2_entry, l2_bitmap, sc_index);

    if (!keep_old) {
        switch (type) {
        case QCOW2_SUBCLUSTER_COMPRESSED:
            cow_start_from = 0;
            break;
        case QCOW2_SUBCLUSTER_NORMAL:
        case QCOW2_SUBCLUSTER_ZERO_ALLOC:
        case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC:
            if (has_subclusters(s)) {
                /* Skip all leading zero and unallocated subclusters */
                uint32_t alloc_bitmap = l2_bitmap & QCOW_L2_BITMAP_ALL_ALLOC;
                cow_start_from =
                    MIN(sc_index, ctz32(alloc_bitmap)) << s->subcluster_bits;
            } else {
                cow_start_from = 0;
            }
            break;
        case QCOW2_SUBCLUSTER_ZERO_PLAIN:
        case QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN:
            cow_start_from = sc_index << s->subcluster_bits;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (type) {
        case QCOW2_SUBCLUSTER_NORMAL:
            cow_start_from = cow_start_to;
            break;
        case QCOW2_SUBCLUSTER_ZERO_ALLOC:
        case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC:
            cow_start_from = sc_index << s->subcluster_bits;
            break;
        default:
            g_assert_not_reached();
        }
    }

    /* Get the L2 entry of the last cluster */
    l2_index += nb_clusters - 1;
    l2_entry = get_l2_entry(s, l2_slice, l2_index);
    l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index);
    sc_index = offset_to_sc_index(s, guest_offset + bytes - 1);
    type = qcow2_get_subcluster_type(bs, l2_entry, l2_bitmap, sc_index);

    if (!keep_old) {
        switch (type) {
        case QCOW2_SUBCLUSTER_COMPRESSED:
            cow_end_to = ROUND_UP(cow_end_from, s->cluster_size);
            break;
        case QCOW2_SUBCLUSTER_NORMAL:
        case QCOW2_SUBCLUSTER_ZERO_ALLOC:
        case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC:
            cow_end_to = ROUND_UP(cow_end_from, s->cluster_size);
            if (has_subclusters(s)) {
                /* Skip all trailing zero and unallocated subclusters */
                uint32_t alloc_bitmap = l2_bitmap & QCOW_L2_BITMAP_ALL_ALLOC;
                cow_end_to -=
                    MIN(s->subclusters_per_cluster - sc_index - 1,
                        clz32(alloc_bitmap)) << s->subcluster_bits;
            }
            break;
        case QCOW2_SUBCLUSTER_ZERO_PLAIN:
        case QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN:
            cow_end_to = ROUND_UP(cow_end_from, s->subcluster_size);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (type) {
        case QCOW2_SUBCLUSTER_NORMAL:
            cow_end_to = cow_end_from;
            break;
        case QCOW2_SUBCLUSTER_ZERO_ALLOC:
        case QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC:
            cow_end_to = ROUND_UP(cow_end_from, s->subcluster_size);
            break;
        default:
            g_assert_not_reached();
        }
    }

    *m = g_malloc0(sizeof(**m));
    **m = (QCowL2Meta) {
        .next           = old_m,

        .alloc_offset   = host_cluster_offset,
        .offset         = start_of_cluster(s, guest_offset),
        .nb_clusters    = nb_clusters,

        .keep_old_clusters = keep_old,

        .cow_start = {
            .offset     = cow_start_from,
            .nb_bytes   = cow_start_to - cow_start_from,
        },
        .cow_end = {
            .offset     = cow_end_from,
            .nb_bytes   = cow_end_to - cow_end_from,
        },
    };

    qemu_co_queue_init(&(*m)->dependent_requests);
    QLIST_INSERT_HEAD(&s->cluster_allocs, *m, next_in_flight);

    return 0;
}

/*
 * Returns true if writing to the cluster pointed to by @l2_entry
 * requires a new allocation (that is, if the cluster is unallocated
 * or has refcount > 1 and therefore cannot be written in-place).
 */
static bool cluster_needs_new_alloc(BlockDriverState *bs, uint64_t l2_entry)
{
    switch (qcow2_get_cluster_type(bs, l2_entry)) {
    case QCOW2_CLUSTER_NORMAL:
    case QCOW2_CLUSTER_ZERO_ALLOC:
        if (l2_entry & QCOW_OFLAG_COPIED) {
            return false;
        }
        /* fallthrough */
    case QCOW2_CLUSTER_UNALLOCATED:
    case QCOW2_CLUSTER_COMPRESSED:
    case QCOW2_CLUSTER_ZERO_PLAIN:
        return true;
    default:
        abort();
    }
}

/*
 * Returns the number of contiguous clusters that can be written to
 * using one single write request, starting from @l2_index.
 * At most @nb_clusters are checked.
 *
 * If @new_alloc is true this counts clusters that are either
 * unallocated, or allocated but with refcount > 1 (so they need to be
 * newly allocated and COWed).
 *
 * If @new_alloc is false this counts clusters that are already
 * allocated and can be overwritten in-place (this includes clusters
 * of type QCOW2_CLUSTER_ZERO_ALLOC).
 */
static int count_single_write_clusters(BlockDriverState *bs, int nb_clusters,
                                       uint64_t *l2_slice, int l2_index,
                                       bool new_alloc)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t l2_entry = get_l2_entry(s, l2_slice, l2_index);
    uint64_t expected_offset = l2_entry & L2E_OFFSET_MASK;
    int i;

    for (i = 0; i < nb_clusters; i++) {
        l2_entry = get_l2_entry(s, l2_slice, l2_index + i);
        if (cluster_needs_new_alloc(bs, l2_entry) != new_alloc) {
            break;
        }
        if (!new_alloc) {
            if (expected_offset != (l2_entry & L2E_OFFSET_MASK)) {
                break;
            }
            expected_offset += s->cluster_size;
        }
    }

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
        uint64_t old_start = start_of_cluster(s, l2meta_cow_start(old_alloc));
        uint64_t old_end = ROUND_UP(l2meta_cow_end(old_alloc), s->cluster_size);

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
 * Checks how many already allocated clusters that don't require a new
 * allocation there are at the given guest_offset (up to *bytes).
 * If *host_offset is not INV_OFFSET, only physically contiguous clusters
 * beginning at this host offset are counted.
 *
 * Note that guest_offset may not be cluster aligned. In this case, the
 * returned *host_offset points to exact byte referenced by guest_offset and
 * therefore isn't cluster aligned as well.
 *
 * Returns:
 *   0:     if no allocated clusters are available at the given offset.
 *          *bytes is normally unchanged. It is set to 0 if the cluster
 *          is allocated and can be overwritten in-place but doesn't have
 *          the right physical offset.
 *
 *   1:     if allocated clusters that can be overwritten in place are
 *          available at the requested offset. *bytes may have decreased
 *          and describes the length of the area that can be written to.
 *
 *  -errno: in error cases
 */
static int handle_copied(BlockDriverState *bs, uint64_t guest_offset,
    uint64_t *host_offset, uint64_t *bytes, QCowL2Meta **m)
{
    BDRVQcow2State *s = bs->opaque;
    int l2_index;
    uint64_t l2_entry, cluster_offset;
    uint64_t *l2_slice;
    uint64_t nb_clusters;
    unsigned int keep_clusters;
    int ret;

    trace_qcow2_handle_copied(qemu_coroutine_self(), guest_offset, *host_offset,
                              *bytes);

    assert(*host_offset == INV_OFFSET || offset_into_cluster(s, guest_offset)
                                      == offset_into_cluster(s, *host_offset));

    /*
     * Calculate the number of clusters to look for. We stop at L2 slice
     * boundaries to keep things simple.
     */
    nb_clusters =
        size_to_clusters(s, offset_into_cluster(s, guest_offset) + *bytes);

    l2_index = offset_to_l2_slice_index(s, guest_offset);
    nb_clusters = MIN(nb_clusters, s->l2_slice_size - l2_index);
    /* Limit total byte count to BDRV_REQUEST_MAX_BYTES */
    nb_clusters = MIN(nb_clusters, BDRV_REQUEST_MAX_BYTES >> s->cluster_bits);

    /* Find L2 entry for the first involved cluster */
    ret = get_cluster_table(bs, guest_offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    l2_entry = get_l2_entry(s, l2_slice, l2_index);
    cluster_offset = l2_entry & L2E_OFFSET_MASK;

    if (!cluster_needs_new_alloc(bs, l2_entry)) {
        if (offset_into_cluster(s, cluster_offset)) {
            qcow2_signal_corruption(bs, true, -1, -1, "%s cluster offset "
                                    "%#" PRIx64 " unaligned (guest offset: %#"
                                    PRIx64 ")", l2_entry & QCOW_OFLAG_ZERO ?
                                    "Preallocated zero" : "Data",
                                    cluster_offset, guest_offset);
            ret = -EIO;
            goto out;
        }

        /* If a specific host_offset is required, check it */
        if (*host_offset != INV_OFFSET && cluster_offset != *host_offset) {
            *bytes = 0;
            ret = 0;
            goto out;
        }

        /* We keep all QCOW_OFLAG_COPIED clusters */
        keep_clusters = count_single_write_clusters(bs, nb_clusters, l2_slice,
                                                    l2_index, false);
        assert(keep_clusters <= nb_clusters);

        *bytes = MIN(*bytes,
                 keep_clusters * s->cluster_size
                 - offset_into_cluster(s, guest_offset));
        assert(*bytes != 0);

        ret = calculate_l2_meta(bs, cluster_offset, guest_offset,
                                *bytes, l2_slice, m, true);
        if (ret < 0) {
            goto out;
        }

        ret = 1;
    } else {
        ret = 0;
    }

    /* Cleanup */
out:
    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    /* Only return a host offset if we actually made progress. Otherwise we
     * would make requirements for handle_alloc() that it can't fulfill */
    if (ret > 0) {
        *host_offset = cluster_offset + offset_into_cluster(s, guest_offset);
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
 * If *host_offset is not INV_OFFSET, it specifies the offset in the image file
 * at which the new clusters must start. *nb_clusters can be 0 on return in
 * this case if the cluster at host_offset is already in use. If *host_offset
 * is INV_OFFSET, the clusters can be allocated anywhere in the image file.
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

    if (has_data_file(bs)) {
        assert(*host_offset == INV_OFFSET ||
               *host_offset == start_of_cluster(s, guest_offset));
        *host_offset = start_of_cluster(s, guest_offset);
        return 0;
    }

    /* Allocate new clusters */
    trace_qcow2_cluster_alloc_phys(qemu_coroutine_self());
    if (*host_offset == INV_OFFSET) {
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
 * Allocates new clusters for an area that is either still unallocated or
 * cannot be overwritten in-place. If *host_offset is not INV_OFFSET,
 * clusters are only allocated if the new allocation can match the specified
 * host offset.
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
    uint64_t *l2_slice;
    uint64_t nb_clusters;
    int ret;

    uint64_t alloc_cluster_offset;

    trace_qcow2_handle_alloc(qemu_coroutine_self(), guest_offset, *host_offset,
                             *bytes);
    assert(*bytes > 0);

    /*
     * Calculate the number of clusters to look for. We stop at L2 slice
     * boundaries to keep things simple.
     */
    nb_clusters =
        size_to_clusters(s, offset_into_cluster(s, guest_offset) + *bytes);

    l2_index = offset_to_l2_slice_index(s, guest_offset);
    nb_clusters = MIN(nb_clusters, s->l2_slice_size - l2_index);
    /* Limit total allocation byte count to BDRV_REQUEST_MAX_BYTES */
    nb_clusters = MIN(nb_clusters, BDRV_REQUEST_MAX_BYTES >> s->cluster_bits);

    /* Find L2 entry for the first involved cluster */
    ret = get_cluster_table(bs, guest_offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    nb_clusters = count_single_write_clusters(bs, nb_clusters,
                                              l2_slice, l2_index, true);

    /* This function is only called when there were no non-COW clusters, so if
     * we can't find any unallocated or COW clusters either, something is
     * wrong with our code. */
    assert(nb_clusters > 0);

    /* Allocate at a given offset in the image file */
    alloc_cluster_offset = *host_offset == INV_OFFSET ? INV_OFFSET :
        start_of_cluster(s, *host_offset);
    ret = do_alloc_cluster_offset(bs, guest_offset, &alloc_cluster_offset,
                                  &nb_clusters);
    if (ret < 0) {
        goto out;
    }

    /* Can't extend contiguous allocation */
    if (nb_clusters == 0) {
        *bytes = 0;
        ret = 0;
        goto out;
    }

    assert(alloc_cluster_offset != INV_OFFSET);

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
    int avail_bytes = nb_clusters << s->cluster_bits;
    int nb_bytes = MIN(requested_bytes, avail_bytes);

    *host_offset = alloc_cluster_offset + offset_into_cluster(s, guest_offset);
    *bytes = MIN(*bytes, nb_bytes - offset_into_cluster(s, guest_offset));
    assert(*bytes != 0);

    ret = calculate_l2_meta(bs, alloc_cluster_offset, guest_offset, *bytes,
                            l2_slice, m, false);
    if (ret < 0) {
        goto out;
    }

    ret = 1;

out:
    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
    return ret;
}

/*
 * For a given area on the virtual disk defined by @offset and @bytes,
 * find the corresponding area on the qcow2 image, allocating new
 * clusters (or subclusters) if necessary. The result can span a
 * combination of allocated and previously unallocated clusters.
 *
 * Note that offset may not be cluster aligned. In this case, the returned
 * *host_offset points to exact byte referenced by offset and therefore
 * isn't cluster aligned as well.
 *
 * On return, @host_offset is set to the beginning of the requested
 * area. This area is guaranteed to be contiguous on the qcow2 file
 * but it can be smaller than initially requested. In this case @bytes
 * is updated with the actual size.
 *
 * If any clusters or subclusters were allocated then @m contains a
 * list with the information of all the affected regions. Note that
 * this can happen regardless of whether this function succeeds or
 * not. The caller is responsible for updating the L2 metadata of the
 * allocated clusters (on success) or freeing them (on failure), and
 * for clearing the contents of @m afterwards in both cases.
 *
 * If the request conflicts with another write request in flight, the coroutine
 * is queued and will be reentered when the dependency has completed.
 *
 * Return 0 on success and -errno in error cases
 */
int qcow2_alloc_host_offset(BlockDriverState *bs, uint64_t offset,
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
    cluster_offset = INV_OFFSET;
    *host_offset = INV_OFFSET;
    cur_bytes = 0;
    *m = NULL;

    while (true) {

        if (*host_offset == INV_OFFSET && cluster_offset != INV_OFFSET) {
            *host_offset = cluster_offset;
        }

        assert(remaining >= cur_bytes);

        start           += cur_bytes;
        remaining       -= cur_bytes;

        if (cluster_offset != INV_OFFSET) {
            cluster_offset += cur_bytes;
        }

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
    assert(*host_offset != INV_OFFSET);
    assert(offset_into_cluster(s, *host_offset) ==
           offset_into_cluster(s, offset));

    return 0;
}

/*
 * This discards as many clusters of nb_clusters as possible at once (i.e.
 * all clusters in the same L2 slice) and returns the number of discarded
 * clusters.
 */
static int discard_in_l2_slice(BlockDriverState *bs, uint64_t offset,
                               uint64_t nb_clusters,
                               enum qcow2_discard_type type, bool full_discard)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_slice;
    int l2_index;
    int ret;
    int i;

    ret = get_cluster_table(bs, offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Limit nb_clusters to one L2 slice */
    nb_clusters = MIN(nb_clusters, s->l2_slice_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t old_l2_entry = get_l2_entry(s, l2_slice, l2_index + i);
        uint64_t old_l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index + i);
        uint64_t new_l2_entry = old_l2_entry;
        uint64_t new_l2_bitmap = old_l2_bitmap;
        QCow2ClusterType cluster_type =
            qcow2_get_cluster_type(bs, old_l2_entry);

        /*
         * If full_discard is true, the cluster should not read back as zeroes,
         * but rather fall through to the backing file.
         *
         * If full_discard is false, make sure that a discarded area reads back
         * as zeroes for v3 images (we cannot do it for v2 without actually
         * writing a zero-filled buffer). We can skip the operation if the
         * cluster is already marked as zero, or if it's unallocated and we
         * don't have a backing file.
         *
         * TODO We might want to use bdrv_block_status(bs) here, but we're
         * holding s->lock, so that doesn't work today.
         */
        if (full_discard) {
            new_l2_entry = new_l2_bitmap = 0;
        } else if (bs->backing || qcow2_cluster_is_allocated(cluster_type)) {
            if (has_subclusters(s)) {
                new_l2_entry = 0;
                new_l2_bitmap = QCOW_L2_BITMAP_ALL_ZEROES;
            } else {
                new_l2_entry = s->qcow_version >= 3 ? QCOW_OFLAG_ZERO : 0;
            }
        }

        if (old_l2_entry == new_l2_entry && old_l2_bitmap == new_l2_bitmap) {
            continue;
        }

        /* First remove L2 entries */
        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
        set_l2_entry(s, l2_slice, l2_index + i, new_l2_entry);
        if (has_subclusters(s)) {
            set_l2_bitmap(s, l2_slice, l2_index + i, new_l2_bitmap);
        }
        /* Then decrease the refcount */
        qcow2_free_any_cluster(bs, old_l2_entry, type);
    }

    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

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

    /* Each L2 slice is handled by its own loop iteration */
    while (nb_clusters > 0) {
        cleared = discard_in_l2_slice(bs, offset, nb_clusters, type,
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
 * all clusters in the same L2 slice) and returns the number of zeroed
 * clusters.
 */
static int zero_in_l2_slice(BlockDriverState *bs, uint64_t offset,
                            uint64_t nb_clusters, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_slice;
    int l2_index;
    int ret;
    int i;

    ret = get_cluster_table(bs, offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Limit nb_clusters to one L2 slice */
    nb_clusters = MIN(nb_clusters, s->l2_slice_size - l2_index);
    assert(nb_clusters <= INT_MAX);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t old_l2_entry = get_l2_entry(s, l2_slice, l2_index + i);
        uint64_t old_l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index + i);
        QCow2ClusterType type = qcow2_get_cluster_type(bs, old_l2_entry);
        bool unmap = (type == QCOW2_CLUSTER_COMPRESSED) ||
            ((flags & BDRV_REQ_MAY_UNMAP) && qcow2_cluster_is_allocated(type));
        uint64_t new_l2_entry = unmap ? 0 : old_l2_entry;
        uint64_t new_l2_bitmap = old_l2_bitmap;

        if (has_subclusters(s)) {
            new_l2_bitmap = QCOW_L2_BITMAP_ALL_ZEROES;
        } else {
            new_l2_entry |= QCOW_OFLAG_ZERO;
        }

        if (old_l2_entry == new_l2_entry && old_l2_bitmap == new_l2_bitmap) {
            continue;
        }

        /* First update L2 entries */
        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
        set_l2_entry(s, l2_slice, l2_index + i, new_l2_entry);
        if (has_subclusters(s)) {
            set_l2_bitmap(s, l2_slice, l2_index + i, new_l2_bitmap);
        }

        /* Then decrease the refcount */
        if (unmap) {
            qcow2_free_any_cluster(bs, old_l2_entry, QCOW2_DISCARD_REQUEST);
        }
    }

    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    return nb_clusters;
}

static int zero_l2_subclusters(BlockDriverState *bs, uint64_t offset,
                               unsigned nb_subclusters)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_slice;
    uint64_t old_l2_bitmap, l2_bitmap;
    int l2_index, ret, sc = offset_to_sc_index(s, offset);

    /* For full clusters use zero_in_l2_slice() instead */
    assert(nb_subclusters > 0 && nb_subclusters < s->subclusters_per_cluster);
    assert(sc + nb_subclusters <= s->subclusters_per_cluster);
    assert(offset_into_subcluster(s, offset) == 0);

    ret = get_cluster_table(bs, offset, &l2_slice, &l2_index);
    if (ret < 0) {
        return ret;
    }

    switch (qcow2_get_cluster_type(bs, get_l2_entry(s, l2_slice, l2_index))) {
    case QCOW2_CLUSTER_COMPRESSED:
        ret = -ENOTSUP; /* We cannot partially zeroize compressed clusters */
        goto out;
    case QCOW2_CLUSTER_NORMAL:
    case QCOW2_CLUSTER_UNALLOCATED:
        break;
    default:
        g_assert_not_reached();
    }

    old_l2_bitmap = l2_bitmap = get_l2_bitmap(s, l2_slice, l2_index);

    l2_bitmap |=  QCOW_OFLAG_SUB_ZERO_RANGE(sc, sc + nb_subclusters);
    l2_bitmap &= ~QCOW_OFLAG_SUB_ALLOC_RANGE(sc, sc + nb_subclusters);

    if (old_l2_bitmap != l2_bitmap) {
        set_l2_bitmap(s, l2_slice, l2_index, l2_bitmap);
        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
    }

    ret = 0;
out:
    qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);

    return ret;
}

int qcow2_subcluster_zeroize(BlockDriverState *bs, uint64_t offset,
                             uint64_t bytes, int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t end_offset = offset + bytes;
    uint64_t nb_clusters;
    unsigned head, tail;
    int64_t cleared;
    int ret;

    /* If we have to stay in sync with an external data file, zero out
     * s->data_file first. */
    if (data_file_is_raw(bs)) {
        assert(has_data_file(bs));
        ret = bdrv_co_pwrite_zeroes(s->data_file, offset, bytes, flags);
        if (ret < 0) {
            return ret;
        }
    }

    /* Caller must pass aligned values, except at image end */
    assert(offset_into_subcluster(s, offset) == 0);
    assert(offset_into_subcluster(s, end_offset) == 0 ||
           end_offset >= bs->total_sectors << BDRV_SECTOR_BITS);

    /*
     * The zero flag is only supported by version 3 and newer. However, if we
     * have no backing file, we can resort to discard in version 2.
     */
    if (s->qcow_version < 3) {
        if (!bs->backing) {
            return qcow2_cluster_discard(bs, offset, bytes,
                                         QCOW2_DISCARD_REQUEST, false);
        }
        return -ENOTSUP;
    }

    head = MIN(end_offset, ROUND_UP(offset, s->cluster_size)) - offset;
    offset += head;

    tail = (end_offset >= bs->total_sectors << BDRV_SECTOR_BITS) ? 0 :
        end_offset - MAX(offset, start_of_cluster(s, end_offset));
    end_offset -= tail;

    s->cache_discards = true;

    if (head) {
        ret = zero_l2_subclusters(bs, offset - head,
                                  size_to_subclusters(s, head));
        if (ret < 0) {
            goto fail;
        }
    }

    /* Each L2 slice is handled by its own loop iteration */
    nb_clusters = size_to_clusters(s, end_offset - offset);

    while (nb_clusters > 0) {
        cleared = zero_in_l2_slice(bs, offset, nb_clusters, flags);
        if (cleared < 0) {
            ret = cleared;
            goto fail;
        }

        nb_clusters -= cleared;
        offset += (cleared * s->cluster_size);
    }

    if (tail) {
        ret = zero_l2_subclusters(bs, end_offset, size_to_subclusters(s, tail));
        if (ret < 0) {
            goto fail;
        }
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
    uint64_t *l2_slice = NULL;
    unsigned slice, slice_size2, n_slices;
    int ret;
    int i, j;

    /* qcow2_downgrade() is not allowed in images with subclusters */
    assert(!has_subclusters(s));

    slice_size2 = s->l2_slice_size * l2_entry_size(s);
    n_slices = s->cluster_size / slice_size2;

    if (!is_active_l1) {
        /* inactive L2 tables require a buffer to be stored in when loading
         * them from disk */
        l2_slice = qemu_try_blockalign(bs->file->bs, slice_size2);
        if (l2_slice == NULL) {
            return -ENOMEM;
        }
    }

    for (i = 0; i < l1_size; i++) {
        uint64_t l2_offset = l1_table[i] & L1E_OFFSET_MASK;
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

        ret = qcow2_get_refcount(bs, l2_offset >> s->cluster_bits,
                                 &l2_refcount);
        if (ret < 0) {
            goto fail;
        }

        for (slice = 0; slice < n_slices; slice++) {
            uint64_t slice_offset = l2_offset + slice * slice_size2;
            bool l2_dirty = false;
            if (is_active_l1) {
                /* get active L2 tables from cache */
                ret = qcow2_cache_get(bs, s->l2_table_cache, slice_offset,
                                      (void **)&l2_slice);
            } else {
                /* load inactive L2 tables from disk */
                ret = bdrv_pread(bs->file, slice_offset, l2_slice, slice_size2);
            }
            if (ret < 0) {
                goto fail;
            }

            for (j = 0; j < s->l2_slice_size; j++) {
                uint64_t l2_entry = get_l2_entry(s, l2_slice, j);
                int64_t offset = l2_entry & L2E_OFFSET_MASK;
                QCow2ClusterType cluster_type =
                    qcow2_get_cluster_type(bs, l2_entry);

                if (cluster_type != QCOW2_CLUSTER_ZERO_PLAIN &&
                    cluster_type != QCOW2_CLUSTER_ZERO_ALLOC) {
                    continue;
                }

                if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                    if (!bs->backing) {
                        /*
                         * not backed; therefore we can simply deallocate the
                         * cluster. No need to call set_l2_bitmap(), this
                         * function doesn't support images with subclusters.
                         */
                        set_l2_entry(s, l2_slice, j, 0);
                        l2_dirty = true;
                        continue;
                    }

                    offset = qcow2_alloc_clusters(bs, s->cluster_size);
                    if (offset < 0) {
                        ret = offset;
                        goto fail;
                    }

                    /* The offset must fit in the offset field */
                    assert((offset & L2E_OFFSET_MASK) == offset);

                    if (l2_refcount > 1) {
                        /* For shared L2 tables, set the refcount accordingly
                         * (it is already 1 and needs to be l2_refcount) */
                        ret = qcow2_update_cluster_refcount(
                            bs, offset >> s->cluster_bits,
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
                    int l2_index = slice * s->l2_slice_size + j;
                    qcow2_signal_corruption(
                        bs, true, -1, -1,
                        "Cluster allocation offset "
                        "%#" PRIx64 " unaligned (L2 offset: %#"
                        PRIx64 ", L2 index: %#x)", offset,
                        l2_offset, l2_index);
                    if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                        qcow2_free_clusters(bs, offset, s->cluster_size,
                                            QCOW2_DISCARD_ALWAYS);
                    }
                    ret = -EIO;
                    goto fail;
                }

                ret = qcow2_pre_write_overlap_check(bs, 0, offset,
                                                    s->cluster_size, true);
                if (ret < 0) {
                    if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                        qcow2_free_clusters(bs, offset, s->cluster_size,
                                            QCOW2_DISCARD_ALWAYS);
                    }
                    goto fail;
                }

                ret = bdrv_pwrite_zeroes(s->data_file, offset,
                                         s->cluster_size, 0);
                if (ret < 0) {
                    if (cluster_type == QCOW2_CLUSTER_ZERO_PLAIN) {
                        qcow2_free_clusters(bs, offset, s->cluster_size,
                                            QCOW2_DISCARD_ALWAYS);
                    }
                    goto fail;
                }

                if (l2_refcount == 1) {
                    set_l2_entry(s, l2_slice, j, offset | QCOW_OFLAG_COPIED);
                } else {
                    set_l2_entry(s, l2_slice, j, offset);
                }
                /*
                 * No need to call set_l2_bitmap() after set_l2_entry() because
                 * this function doesn't support images with subclusters.
                 */
                l2_dirty = true;
            }

            if (is_active_l1) {
                if (l2_dirty) {
                    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_slice);
                    qcow2_cache_depends_on_flush(s->l2_table_cache);
                }
                qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
            } else {
                if (l2_dirty) {
                    ret = qcow2_pre_write_overlap_check(
                        bs, QCOW2_OL_INACTIVE_L2 | QCOW2_OL_ACTIVE_L2,
                        slice_offset, slice_size2, false);
                    if (ret < 0) {
                        goto fail;
                    }

                    ret = bdrv_pwrite(bs->file, slice_offset,
                                      l2_slice, slice_size2);
                    if (ret < 0) {
                        goto fail;
                    }
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
    if (l2_slice) {
        if (!is_active_l1) {
            qemu_vfree(l2_slice);
        } else {
            qcow2_cache_put(s->l2_table_cache, (void **) &l2_slice);
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
        int l1_size2;
        uint64_t *new_l1_table;
        Error *local_err = NULL;

        ret = qcow2_validate_table(bs, s->snapshots[i].l1_table_offset,
                                   s->snapshots[i].l1_size, L1E_SIZE,
                                   QCOW_MAX_L1_SIZE, "Snapshot L1 table",
                                   &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            goto fail;
        }

        l1_size2 = s->snapshots[i].l1_size * L1E_SIZE;
        new_l1_table = g_try_realloc(l1_table, l1_size2);

        if (!new_l1_table) {
            ret = -ENOMEM;
            goto fail;
        }

        l1_table = new_l1_table;

        ret = bdrv_pread(bs->file, s->snapshots[i].l1_table_offset,
                         l1_table, l1_size2);
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
