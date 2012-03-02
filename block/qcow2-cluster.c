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

#include <zlib.h>

#include "qemu-common.h"
#include "block_int.h"
#include "block/qcow2.h"
#include "trace.h"

int qcow2_grow_l1_table(BlockDriverState *bs, int min_size, bool exact_size)
{
    BDRVQcowState *s = bs->opaque;
    int new_l1_size, new_l1_size2, ret, i;
    uint64_t *new_l1_table;
    int64_t new_l1_table_offset;
    uint8_t data[12];

    if (min_size <= s->l1_size)
        return 0;

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

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "grow l1_table from %d to %d\n", s->l1_size, new_l1_size);
#endif

    new_l1_size2 = sizeof(uint64_t) * new_l1_size;
    new_l1_table = g_malloc0(align_offset(new_l1_size2, 512));
    memcpy(new_l1_table, s->l1_table, s->l1_size * sizeof(uint64_t));

    /* write new table (align to cluster) */
    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_ALLOC_TABLE);
    new_l1_table_offset = qcow2_alloc_clusters(bs, new_l1_size2);
    if (new_l1_table_offset < 0) {
        g_free(new_l1_table);
        return new_l1_table_offset;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_WRITE_TABLE);
    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = cpu_to_be64(new_l1_table[i]);
    ret = bdrv_pwrite_sync(bs->file, new_l1_table_offset, new_l1_table, new_l1_size2);
    if (ret < 0)
        goto fail;
    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = be64_to_cpu(new_l1_table[i]);

    /* set new table */
    BLKDBG_EVENT(bs->file, BLKDBG_L1_GROW_ACTIVATE_TABLE);
    cpu_to_be32w((uint32_t*)data, new_l1_size);
    cpu_to_be64wu((uint64_t*)(data + 4), new_l1_table_offset);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, l1_size), data,sizeof(data));
    if (ret < 0) {
        goto fail;
    }
    g_free(s->l1_table);
    qcow2_free_clusters(bs, s->l1_table_offset, s->l1_size * sizeof(uint64_t));
    s->l1_table_offset = new_l1_table_offset;
    s->l1_table = new_l1_table;
    s->l1_size = new_l1_size;
    return 0;
 fail:
    g_free(new_l1_table);
    qcow2_free_clusters(bs, new_l1_table_offset, new_l1_size2);
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
    BDRVQcowState *s = bs->opaque;
    int ret;

    ret = qcow2_cache_get(bs, s->l2_table_cache, l2_offset, (void**) l2_table);

    return ret;
}

/*
 * Writes one sector of the L1 table to the disk (can't update single entries
 * and we really don't want bdrv_pread to perform a read-modify-write)
 */
#define L1_ENTRIES_PER_SECTOR (512 / 8)
static int write_l1_entry(BlockDriverState *bs, int l1_index)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t buf[L1_ENTRIES_PER_SECTOR];
    int l1_start_index;
    int i, ret;

    l1_start_index = l1_index & ~(L1_ENTRIES_PER_SECTOR - 1);
    for (i = 0; i < L1_ENTRIES_PER_SECTOR; i++) {
        buf[i] = cpu_to_be64(s->l1_table[l1_start_index + i]);
    }

    BLKDBG_EVENT(bs->file, BLKDBG_L1_UPDATE);
    ret = bdrv_pwrite_sync(bs->file, s->l1_table_offset + 8 * l1_start_index,
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
    BDRVQcowState *s = bs->opaque;
    uint64_t old_l2_offset;
    uint64_t *l2_table;
    int64_t l2_offset;
    int ret;

    old_l2_offset = s->l1_table[l1_index];

    trace_qcow2_l2_allocate(bs, l1_index);

    /* allocate a new l2 entry */

    l2_offset = qcow2_alloc_clusters(bs, s->l2_size * sizeof(uint64_t));
    if (l2_offset < 0) {
        return l2_offset;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* allocate a new entry in the l2 cache */

    trace_qcow2_l2_allocate_get_empty(bs, l1_index);
    ret = qcow2_cache_get_empty(bs, s->l2_table_cache, l2_offset, (void**) table);
    if (ret < 0) {
        return ret;
    }

    l2_table = *table;

    if (old_l2_offset == 0) {
        /* if there was no old l2 table, clear the new table */
        memset(l2_table, 0, s->l2_size * sizeof(uint64_t));
    } else {
        uint64_t* old_table;

        /* if there was an old l2 table, read it from the disk */
        BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_COW_READ);
        ret = qcow2_cache_get(bs, s->l2_table_cache, old_l2_offset,
            (void**) &old_table);
        if (ret < 0) {
            goto fail;
        }

        memcpy(l2_table, old_table, s->cluster_size);

        ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &old_table);
        if (ret < 0) {
            goto fail;
        }
    }

    /* write the l2 table to the file */
    BLKDBG_EVENT(bs->file, BLKDBG_L2_ALLOC_WRITE);

    trace_qcow2_l2_allocate_write_l2(bs, l1_index);
    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_table);
    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        goto fail;
    }

    /* update the L1 entry */
    trace_qcow2_l2_allocate_write_l1(bs, l1_index);
    s->l1_table[l1_index] = l2_offset | QCOW_OFLAG_COPIED;
    ret = write_l1_entry(bs, l1_index);
    if (ret < 0) {
        goto fail;
    }

    *table = l2_table;
    trace_qcow2_l2_allocate_done(bs, l1_index, 0);
    return 0;

fail:
    trace_qcow2_l2_allocate_done(bs, l1_index, ret);
    qcow2_cache_put(bs, s->l2_table_cache, (void**) table);
    s->l1_table[l1_index] = old_l2_offset;
    return ret;
}

static int count_contiguous_clusters(uint64_t nb_clusters, int cluster_size,
        uint64_t *l2_table, uint64_t start, uint64_t mask)
{
    int i;
    uint64_t offset = be64_to_cpu(l2_table[0]) & ~mask;

    if (!offset)
        return 0;

    for (i = start; i < start + nb_clusters; i++)
        if (offset + (uint64_t) i * cluster_size != (be64_to_cpu(l2_table[i]) & ~mask))
            break;

	return (i - start);
}

static int count_contiguous_free_clusters(uint64_t nb_clusters, uint64_t *l2_table)
{
    int i = 0;

    while(nb_clusters-- && l2_table[i] == 0)
        i++;

    return i;
}

/* The crypt function is compatible with the linux cryptoloop
   algorithm for < 4 GB images. NOTE: out_buf == in_buf is
   supported */
void qcow2_encrypt_sectors(BDRVQcowState *s, int64_t sector_num,
                           uint8_t *out_buf, const uint8_t *in_buf,
                           int nb_sectors, int enc,
                           const AES_KEY *key)
{
    union {
        uint64_t ll[2];
        uint8_t b[16];
    } ivec;
    int i;

    for(i = 0; i < nb_sectors; i++) {
        ivec.ll[0] = cpu_to_le64(sector_num);
        ivec.ll[1] = 0;
        AES_cbc_encrypt(in_buf, out_buf, 512, key,
                        ivec.b, enc);
        sector_num++;
        in_buf += 512;
        out_buf += 512;
    }
}

static int coroutine_fn copy_sectors(BlockDriverState *bs,
                                     uint64_t start_sect,
                                     uint64_t cluster_offset,
                                     int n_start, int n_end)
{
    BDRVQcowState *s = bs->opaque;
    QEMUIOVector qiov;
    struct iovec iov;
    int n, ret;

    /*
     * If this is the last cluster and it is only partially used, we must only
     * copy until the end of the image, or bdrv_check_request will fail for the
     * bdrv_read/write calls below.
     */
    if (start_sect + n_end > bs->total_sectors) {
        n_end = bs->total_sectors - start_sect;
    }

    n = n_end - n_start;
    if (n <= 0) {
        return 0;
    }

    iov.iov_len = n * BDRV_SECTOR_SIZE;
    iov.iov_base = qemu_blockalign(bs, iov.iov_len);

    qemu_iovec_init_external(&qiov, &iov, 1);

    BLKDBG_EVENT(bs->file, BLKDBG_COW_READ);

    /* Call .bdrv_co_readv() directly instead of using the public block-layer
     * interface.  This avoids double I/O throttling and request tracking,
     * which can lead to deadlock when block layer copy-on-read is enabled.
     */
    ret = bs->drv->bdrv_co_readv(bs, start_sect + n_start, n, &qiov);
    if (ret < 0) {
        goto out;
    }

    if (s->crypt_method) {
        qcow2_encrypt_sectors(s, start_sect + n_start,
                        iov.iov_base, iov.iov_base, n, 1,
                        &s->aes_encrypt_key);
    }

    BLKDBG_EVENT(bs->file, BLKDBG_COW_WRITE);
    ret = bdrv_co_writev(bs->file, (cluster_offset >> 9) + n_start, n, &qiov);
    if (ret < 0) {
        goto out;
    }

    ret = 0;
out:
    qemu_vfree(iov.iov_base);
    return ret;
}


/*
 * get_cluster_offset
 *
 * For a given offset of the disk image, find the cluster offset in
 * qcow2 file. The offset is stored in *cluster_offset.
 *
 * on entry, *num is the number of contiguous sectors we'd like to
 * access following offset.
 *
 * on exit, *num is the number of contiguous sectors we can read.
 *
 * Return 0, if the offset is found
 * Return -errno, otherwise.
 *
 */

int qcow2_get_cluster_offset(BlockDriverState *bs, uint64_t offset,
    int *num, uint64_t *cluster_offset)
{
    BDRVQcowState *s = bs->opaque;
    unsigned int l1_index, l2_index;
    uint64_t l2_offset, *l2_table;
    int l1_bits, c;
    unsigned int index_in_cluster, nb_clusters;
    uint64_t nb_available, nb_needed;
    int ret;

    index_in_cluster = (offset >> 9) & (s->cluster_sectors - 1);
    nb_needed = *num + index_in_cluster;

    l1_bits = s->l2_bits + s->cluster_bits;

    /* compute how many bytes there are between the offset and
     * the end of the l1 entry
     */

    nb_available = (1ULL << l1_bits) - (offset & ((1ULL << l1_bits) - 1));

    /* compute the number of available sectors */

    nb_available = (nb_available >> 9) + index_in_cluster;

    if (nb_needed > nb_available) {
        nb_needed = nb_available;
    }

    *cluster_offset = 0;

    /* seek the the l2 offset in the l1 table */

    l1_index = offset >> l1_bits;
    if (l1_index >= s->l1_size)
        goto out;

    l2_offset = s->l1_table[l1_index];

    /* seek the l2 table of the given l2 offset */

    if (!l2_offset)
        goto out;

    /* load the l2 table in memory */

    l2_offset &= ~QCOW_OFLAG_COPIED;
    ret = l2_load(bs, l2_offset, &l2_table);
    if (ret < 0) {
        return ret;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = (offset >> s->cluster_bits) & (s->l2_size - 1);
    *cluster_offset = be64_to_cpu(l2_table[l2_index]);
    nb_clusters = size_to_clusters(s, nb_needed << 9);

    if (!*cluster_offset) {
        /* how many empty clusters ? */
        c = count_contiguous_free_clusters(nb_clusters, &l2_table[l2_index]);
    } else {
        /* how many allocated clusters ? */
        c = count_contiguous_clusters(nb_clusters, s->cluster_size,
                &l2_table[l2_index], 0, QCOW_OFLAG_COPIED);
    }

    qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);

   nb_available = (c * s->cluster_sectors);
out:
    if (nb_available > nb_needed)
        nb_available = nb_needed;

    *num = nb_available - index_in_cluster;

    *cluster_offset &=~QCOW_OFLAG_COPIED;
    return 0;
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
                             uint64_t *new_l2_offset,
                             int *new_l2_index)
{
    BDRVQcowState *s = bs->opaque;
    unsigned int l1_index, l2_index;
    uint64_t l2_offset;
    uint64_t *l2_table = NULL;
    int ret;

    /* seek the the l2 offset in the l1 table */

    l1_index = offset >> (s->l2_bits + s->cluster_bits);
    if (l1_index >= s->l1_size) {
        ret = qcow2_grow_l1_table(bs, l1_index + 1, false);
        if (ret < 0) {
            return ret;
        }
    }
    l2_offset = s->l1_table[l1_index];

    /* seek the l2 table of the given l2 offset */

    if (l2_offset & QCOW_OFLAG_COPIED) {
        /* load the l2 table in memory */
        l2_offset &= ~QCOW_OFLAG_COPIED;
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
            qcow2_free_clusters(bs, l2_offset, s->l2_size * sizeof(uint64_t));
        }
        l2_offset = s->l1_table[l1_index] & ~QCOW_OFLAG_COPIED;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = (offset >> s->cluster_bits) & (s->l2_size - 1);

    *new_l2_table = l2_table;
    *new_l2_offset = l2_offset;
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
    BDRVQcowState *s = bs->opaque;
    int l2_index, ret;
    uint64_t l2_offset, *l2_table;
    int64_t cluster_offset;
    int nb_csectors;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_offset, &l2_index);
    if (ret < 0) {
        return 0;
    }

    cluster_offset = be64_to_cpu(l2_table[l2_index]);
    if (cluster_offset & QCOW_OFLAG_COPIED) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
        return 0;
    }

    if (cluster_offset)
        qcow2_free_any_clusters(bs, cluster_offset, 1);

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
    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_table);
    l2_table[l2_index] = cpu_to_be64(cluster_offset);
    ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    if (ret < 0) {
        return 0;
    }

    return cluster_offset;
}

int qcow2_alloc_cluster_link_l2(BlockDriverState *bs, QCowL2Meta *m)
{
    BDRVQcowState *s = bs->opaque;
    int i, j = 0, l2_index, ret;
    uint64_t *old_cluster, start_sect, l2_offset, *l2_table;
    uint64_t cluster_offset = m->alloc_offset;
    bool cow = false;

    trace_qcow2_cluster_link_l2(qemu_coroutine_self(), m->nb_clusters);

    if (m->nb_clusters == 0)
        return 0;

    old_cluster = g_malloc(m->nb_clusters * sizeof(uint64_t));

    /* copy content of unmodified sectors */
    start_sect = (m->offset & ~(s->cluster_size - 1)) >> 9;
    if (m->n_start) {
        cow = true;
        qemu_co_mutex_unlock(&s->lock);
        ret = copy_sectors(bs, start_sect, cluster_offset, 0, m->n_start);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0)
            goto err;
    }

    if (m->nb_available & (s->cluster_sectors - 1)) {
        uint64_t end = m->nb_available & ~(uint64_t)(s->cluster_sectors - 1);
        cow = true;
        qemu_co_mutex_unlock(&s->lock);
        ret = copy_sectors(bs, start_sect + end, cluster_offset + (end << 9),
                m->nb_available - end, s->cluster_sectors);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0)
            goto err;
    }

    /*
     * Update L2 table.
     *
     * Before we update the L2 table to actually point to the new cluster, we
     * need to be sure that the refcounts have been increased and COW was
     * handled.
     */
    if (cow) {
        qcow2_cache_depends_on_flush(s->l2_table_cache);
    }

    qcow2_cache_set_dependency(bs, s->l2_table_cache, s->refcount_block_cache);
    ret = get_cluster_table(bs, m->offset, &l2_table, &l2_offset, &l2_index);
    if (ret < 0) {
        goto err;
    }
    qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_table);

    for (i = 0; i < m->nb_clusters; i++) {
        /* if two concurrent writes happen to the same unallocated cluster
	 * each write allocates separate cluster and writes data concurrently.
	 * The first one to complete updates l2 table with pointer to its
	 * cluster the second one has to do RMW (which is done above by
	 * copy_sectors()), update l2 table with its cluster pointer and free
	 * old cluster. This is what this loop does */
        if(l2_table[l2_index + i] != 0)
            old_cluster[j++] = l2_table[l2_index + i];

        l2_table[l2_index + i] = cpu_to_be64((cluster_offset +
                    (i << s->cluster_bits)) | QCOW_OFLAG_COPIED);
     }


    ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    if (ret < 0) {
        goto err;
    }

    /*
     * If this was a COW, we need to decrease the refcount of the old cluster.
     * Also flush bs->file to get the right order for L2 and refcount update.
     */
    if (j != 0) {
        for (i = 0; i < j; i++) {
            qcow2_free_any_clusters(bs,
                be64_to_cpu(old_cluster[i]) & ~QCOW_OFLAG_COPIED, 1);
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
static int count_cow_clusters(BDRVQcowState *s, int nb_clusters,
    uint64_t *l2_table, int l2_index)
{
    int i = 0;
    uint64_t cluster_offset;

    while (i < nb_clusters) {
        i += count_contiguous_clusters(nb_clusters - i, s->cluster_size,
                &l2_table[l2_index], i, 0);
        if ((i >= nb_clusters) || be64_to_cpu(l2_table[l2_index + i])) {
            break;
        }

        i += count_contiguous_free_clusters(nb_clusters - i,
                &l2_table[l2_index + i]);
        if (i >= nb_clusters) {
            break;
        }

        cluster_offset = be64_to_cpu(l2_table[l2_index + i]);

        if ((cluster_offset & QCOW_OFLAG_COPIED) ||
                (cluster_offset & QCOW_OFLAG_COMPRESSED))
            break;
    }

    assert(i <= nb_clusters);
    return i;
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
    uint64_t *host_offset, unsigned int *nb_clusters, uint64_t *l2_table)
{
    BDRVQcowState *s = bs->opaque;
    int64_t cluster_offset;
    QCowL2Meta *old_alloc;

    trace_qcow2_do_alloc_clusters_offset(qemu_coroutine_self(), guest_offset,
                                         *host_offset, *nb_clusters);

    /*
     * Check if there already is an AIO write request in flight which allocates
     * the same cluster. In this case we need to wait until the previous
     * request has completed and updated the L2 table accordingly.
     */
    QLIST_FOREACH(old_alloc, &s->cluster_allocs, next_in_flight) {

        uint64_t start = guest_offset >> s->cluster_bits;
        uint64_t end = start + *nb_clusters;
        uint64_t old_start = old_alloc->offset >> s->cluster_bits;
        uint64_t old_end = old_start + old_alloc->nb_clusters;

        if (end < old_start || start > old_end) {
            /* No intersection */
        } else {
            if (start < old_start) {
                /* Stop at the start of a running allocation */
                *nb_clusters = old_start - start;
            } else {
                *nb_clusters = 0;
            }

            if (*nb_clusters == 0) {
                /* Wait for the dependency to complete. We need to recheck
                 * the free/allocated clusters when we continue. */
                qemu_co_mutex_unlock(&s->lock);
                qemu_co_queue_wait(&old_alloc->dependent_requests);
                qemu_co_mutex_lock(&s->lock);
                return -EAGAIN;
            }
        }
    }

    if (!*nb_clusters) {
        abort();
    }

    /* Allocate new clusters */
    trace_qcow2_cluster_alloc_phys(qemu_coroutine_self());
    if (*host_offset == 0) {
        cluster_offset = qcow2_alloc_clusters(bs, *nb_clusters * s->cluster_size);
    } else {
        cluster_offset = *host_offset;
        *nb_clusters = qcow2_alloc_clusters_at(bs, cluster_offset, *nb_clusters);
    }

    if (cluster_offset < 0) {
        return cluster_offset;
    }
    *host_offset = cluster_offset;
    return 0;
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
    int n_start, int n_end, int *num, QCowL2Meta *m)
{
    BDRVQcowState *s = bs->opaque;
    int l2_index, ret, sectors;
    uint64_t l2_offset, *l2_table;
    unsigned int nb_clusters, keep_clusters;
    uint64_t cluster_offset;

    trace_qcow2_alloc_clusters_offset(qemu_coroutine_self(), offset,
                                      n_start, n_end);

    /* Find L2 entry for the first involved cluster */
    ret = get_cluster_table(bs, offset, &l2_table, &l2_offset, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /*
     * Calculate the number of clusters to look for. We stop at L2 table
     * boundaries to keep things simple.
     */
again:
    nb_clusters = MIN(size_to_clusters(s, n_end << BDRV_SECTOR_BITS),
                      s->l2_size - l2_index);

    cluster_offset = be64_to_cpu(l2_table[l2_index]);

    /*
     * Check how many clusters are already allocated and don't need COW, and how
     * many need a new allocation.
     */
    if (cluster_offset & QCOW_OFLAG_COPIED) {
        /* We keep all QCOW_OFLAG_COPIED clusters */
        keep_clusters = count_contiguous_clusters(nb_clusters, s->cluster_size,
                                                  &l2_table[l2_index], 0, 0);
        assert(keep_clusters <= nb_clusters);
        nb_clusters -= keep_clusters;
    } else {
        /* For the moment, overwrite compressed clusters one by one */
        if (cluster_offset & QCOW_OFLAG_COMPRESSED) {
            nb_clusters = 1;
        } else {
            nb_clusters = count_cow_clusters(s, nb_clusters, l2_table, l2_index);
        }

        keep_clusters = 0;
        cluster_offset = 0;
    }

    cluster_offset &= ~QCOW_OFLAG_COPIED;

    /* If there is something left to allocate, do that now */
    *m = (QCowL2Meta) {
        .cluster_offset     = cluster_offset,
        .nb_clusters        = 0,
    };
    qemu_co_queue_init(&m->dependent_requests);

    if (nb_clusters > 0) {
        uint64_t alloc_offset;
        uint64_t alloc_cluster_offset;
        uint64_t keep_bytes = keep_clusters * s->cluster_size;

        /* Calculate start and size of allocation */
        alloc_offset = offset + keep_bytes;

        if (keep_clusters == 0) {
            alloc_cluster_offset = 0;
        } else {
            alloc_cluster_offset = cluster_offset + keep_bytes;
        }

        /* Allocate, if necessary at a given offset in the image file */
        ret = do_alloc_cluster_offset(bs, alloc_offset, &alloc_cluster_offset,
                                      &nb_clusters, l2_table);
        if (ret == -EAGAIN) {
            goto again;
        } else if (ret < 0) {
            goto fail;
        }

        /* save info needed for meta data update */
        if (nb_clusters > 0) {
            int requested_sectors = n_end - keep_clusters * s->cluster_sectors;
            int avail_sectors = (keep_clusters + nb_clusters)
                                << (s->cluster_bits - BDRV_SECTOR_BITS);

            *m = (QCowL2Meta) {
                .cluster_offset = keep_clusters == 0 ?
                                  alloc_cluster_offset : cluster_offset,
                .alloc_offset   = alloc_cluster_offset,
                .offset         = alloc_offset,
                .n_start        = keep_clusters == 0 ? n_start : 0,
                .nb_clusters    = nb_clusters,
                .nb_available   = MIN(requested_sectors, avail_sectors),
            };
            qemu_co_queue_init(&m->dependent_requests);
            QLIST_INSERT_HEAD(&s->cluster_allocs, m, next_in_flight);
        }
    }

    /* Some cleanup work */
    ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    if (ret < 0) {
        goto fail_put;
    }

    sectors = (keep_clusters + nb_clusters) << (s->cluster_bits - 9);
    if (sectors > n_end) {
        sectors = n_end;
    }

    assert(sectors > n_start);
    *num = sectors - n_start;

    return 0;

fail:
    qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
fail_put:
    if (nb_clusters > 0) {
        QLIST_REMOVE(m, next_in_flight);
    }
    return ret;
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
    BDRVQcowState *s = bs->opaque;
    int ret, csize, nb_csectors, sector_offset;
    uint64_t coffset;

    coffset = cluster_offset & s->cluster_offset_mask;
    if (s->cluster_cache_offset != coffset) {
        nb_csectors = ((cluster_offset >> s->csize_shift) & s->csize_mask) + 1;
        sector_offset = coffset & 511;
        csize = nb_csectors * 512 - sector_offset;
        BLKDBG_EVENT(bs->file, BLKDBG_READ_COMPRESSED);
        ret = bdrv_read(bs->file, coffset >> 9, s->cluster_data, nb_csectors);
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
    unsigned int nb_clusters)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t l2_offset, *l2_table;
    int l2_index;
    int ret;
    int i;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_offset, &l2_index);
    if (ret < 0) {
        return ret;
    }

    /* Limit nb_clusters to one L2 table */
    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);

    for (i = 0; i < nb_clusters; i++) {
        uint64_t old_offset;

        old_offset = be64_to_cpu(l2_table[l2_index + i]);
        old_offset &= ~QCOW_OFLAG_COPIED;

        if (old_offset == 0) {
            continue;
        }

        /* First remove L2 entries */
        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_table);
        l2_table[l2_index + i] = cpu_to_be64(0);

        /* Then decrease the refcount */
        qcow2_free_any_clusters(bs, old_offset, 1);
    }

    ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    if (ret < 0) {
        return ret;
    }

    return nb_clusters;
}

int qcow2_discard_clusters(BlockDriverState *bs, uint64_t offset,
    int nb_sectors)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t end_offset;
    unsigned int nb_clusters;
    int ret;

    end_offset = offset + (nb_sectors << BDRV_SECTOR_BITS);

    /* Round start up and end down */
    offset = align_offset(offset, s->cluster_size);
    end_offset &= ~(s->cluster_size - 1);

    if (offset > end_offset) {
        return 0;
    }

    nb_clusters = size_to_clusters(s, end_offset - offset);

    /* Each L2 table is handled by its own loop iteration */
    while (nb_clusters > 0) {
        ret = discard_single_l2(bs, offset, nb_clusters);
        if (ret < 0) {
            return ret;
        }

        nb_clusters -= ret;
        offset += (ret * s->cluster_size);
    }

    return 0;
}
