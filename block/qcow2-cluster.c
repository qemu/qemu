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

int qcow2_grow_l1_table(BlockDriverState *bs, int min_size)
{
    BDRVQcowState *s = bs->opaque;
    int new_l1_size, new_l1_size2, ret, i;
    uint64_t *new_l1_table;
    uint64_t new_l1_table_offset;
    uint8_t data[12];

    new_l1_size = s->l1_size;
    if (min_size <= new_l1_size)
        return 0;
    while (min_size > new_l1_size) {
        new_l1_size = (new_l1_size * 3 + 1) / 2;
    }
#ifdef DEBUG_ALLOC2
    printf("grow l1_table from %d to %d\n", s->l1_size, new_l1_size);
#endif

    new_l1_size2 = sizeof(uint64_t) * new_l1_size;
    new_l1_table = qemu_mallocz(new_l1_size2);
    memcpy(new_l1_table, s->l1_table, s->l1_size * sizeof(uint64_t));

    /* write new table (align to cluster) */
    new_l1_table_offset = qcow2_alloc_clusters(bs, new_l1_size2);

    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = cpu_to_be64(new_l1_table[i]);
    ret = bdrv_pwrite(s->hd, new_l1_table_offset, new_l1_table, new_l1_size2);
    if (ret != new_l1_size2)
        goto fail;
    for(i = 0; i < s->l1_size; i++)
        new_l1_table[i] = be64_to_cpu(new_l1_table[i]);

    /* set new table */
    cpu_to_be32w((uint32_t*)data, new_l1_size);
    cpu_to_be64w((uint64_t*)(data + 4), new_l1_table_offset);
    if (bdrv_pwrite(s->hd, offsetof(QCowHeader, l1_size), data,
                sizeof(data)) != sizeof(data))
        goto fail;
    qemu_free(s->l1_table);
    qcow2_free_clusters(bs, s->l1_table_offset, s->l1_size * sizeof(uint64_t));
    s->l1_table_offset = new_l1_table_offset;
    s->l1_table = new_l1_table;
    s->l1_size = new_l1_size;
    return 0;
 fail:
    qemu_free(s->l1_table);
    return -EIO;
}

void qcow2_l2_cache_reset(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;

    memset(s->l2_cache, 0, s->l2_size * L2_CACHE_SIZE * sizeof(uint64_t));
    memset(s->l2_cache_offsets, 0, L2_CACHE_SIZE * sizeof(uint64_t));
    memset(s->l2_cache_counts, 0, L2_CACHE_SIZE * sizeof(uint32_t));
}

static inline int l2_cache_new_entry(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    uint32_t min_count;
    int min_index, i;

    /* find a new entry in the least used one */
    min_index = 0;
    min_count = 0xffffffff;
    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (s->l2_cache_counts[i] < min_count) {
            min_count = s->l2_cache_counts[i];
            min_index = i;
        }
    }
    return min_index;
}

/*
 * seek_l2_table
 *
 * seek l2_offset in the l2_cache table
 * if not found, return NULL,
 * if found,
 *   increments the l2 cache hit count of the entry,
 *   if counter overflow, divide by two all counters
 *   return the pointer to the l2 cache entry
 *
 */

static uint64_t *seek_l2_table(BDRVQcowState *s, uint64_t l2_offset)
{
    int i, j;

    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (l2_offset == s->l2_cache_offsets[i]) {
            /* increment the hit count */
            if (++s->l2_cache_counts[i] == 0xffffffff) {
                for(j = 0; j < L2_CACHE_SIZE; j++) {
                    s->l2_cache_counts[j] >>= 1;
                }
            }
            return s->l2_cache + (i << s->l2_bits);
        }
    }
    return NULL;
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

static uint64_t *l2_load(BlockDriverState *bs, uint64_t l2_offset)
{
    BDRVQcowState *s = bs->opaque;
    int min_index;
    uint64_t *l2_table;

    /* seek if the table for the given offset is in the cache */

    l2_table = seek_l2_table(s, l2_offset);
    if (l2_table != NULL)
        return l2_table;

    /* not found: load a new entry in the least used one */

    min_index = l2_cache_new_entry(bs);
    l2_table = s->l2_cache + (min_index << s->l2_bits);
    if (bdrv_pread(s->hd, l2_offset, l2_table, s->l2_size * sizeof(uint64_t)) !=
        s->l2_size * sizeof(uint64_t))
        return NULL;
    s->l2_cache_offsets[min_index] = l2_offset;
    s->l2_cache_counts[min_index] = 1;

    return l2_table;
}

/*
 * Writes one sector of the L1 table to the disk (can't update single entries
 * and we really don't want bdrv_pread to perform a read-modify-write)
 */
#define L1_ENTRIES_PER_SECTOR (512 / 8)
static int write_l1_entry(BDRVQcowState *s, int l1_index)
{
    uint64_t buf[L1_ENTRIES_PER_SECTOR];
    int l1_start_index;
    int i;

    l1_start_index = l1_index & ~(L1_ENTRIES_PER_SECTOR - 1);
    for (i = 0; i < L1_ENTRIES_PER_SECTOR; i++) {
        buf[i] = cpu_to_be64(s->l1_table[l1_start_index + i]);
    }

    if (bdrv_pwrite(s->hd, s->l1_table_offset + 8 * l1_start_index,
        buf, sizeof(buf)) != sizeof(buf))
    {
        return -1;
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

static uint64_t *l2_allocate(BlockDriverState *bs, int l1_index)
{
    BDRVQcowState *s = bs->opaque;
    int min_index;
    uint64_t old_l2_offset;
    uint64_t *l2_table, l2_offset;

    old_l2_offset = s->l1_table[l1_index];

    /* allocate a new l2 entry */

    l2_offset = qcow2_alloc_clusters(bs, s->l2_size * sizeof(uint64_t));

    /* update the L1 entry */

    s->l1_table[l1_index] = l2_offset | QCOW_OFLAG_COPIED;
    if (write_l1_entry(s, l1_index) < 0) {
        return NULL;
    }

    /* allocate a new entry in the l2 cache */

    min_index = l2_cache_new_entry(bs);
    l2_table = s->l2_cache + (min_index << s->l2_bits);

    if (old_l2_offset == 0) {
        /* if there was no old l2 table, clear the new table */
        memset(l2_table, 0, s->l2_size * sizeof(uint64_t));
    } else {
        /* if there was an old l2 table, read it from the disk */
        if (bdrv_pread(s->hd, old_l2_offset,
                       l2_table, s->l2_size * sizeof(uint64_t)) !=
            s->l2_size * sizeof(uint64_t))
            return NULL;
    }
    /* write the l2 table to the file */
    if (bdrv_pwrite(s->hd, l2_offset,
                    l2_table, s->l2_size * sizeof(uint64_t)) !=
        s->l2_size * sizeof(uint64_t))
        return NULL;

    /* update the l2 cache entry */

    s->l2_cache_offsets[min_index] = l2_offset;
    s->l2_cache_counts[min_index] = 1;

    return l2_table;
}

static int count_contiguous_clusters(uint64_t nb_clusters, int cluster_size,
        uint64_t *l2_table, uint64_t start, uint64_t mask)
{
    int i;
    uint64_t offset = be64_to_cpu(l2_table[0]) & ~mask;

    if (!offset)
        return 0;

    for (i = start; i < start + nb_clusters; i++)
        if (offset + i * cluster_size != (be64_to_cpu(l2_table[i]) & ~mask))
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


static int qcow_read(BlockDriverState *bs, int64_t sector_num,
                     uint8_t *buf, int nb_sectors)
{
    BDRVQcowState *s = bs->opaque;
    int ret, index_in_cluster, n, n1;
    uint64_t cluster_offset;

    while (nb_sectors > 0) {
        n = nb_sectors;
        cluster_offset = qcow2_get_cluster_offset(bs, sector_num << 9, &n);
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        if (!cluster_offset) {
            if (bs->backing_hd) {
                /* read from the base image */
                n1 = qcow2_backing_read1(bs->backing_hd, sector_num, buf, n);
                if (n1 > 0) {
                    ret = bdrv_read(bs->backing_hd, sector_num, buf, n1);
                    if (ret < 0)
                        return -1;
                }
            } else {
                memset(buf, 0, 512 * n);
            }
        } else if (cluster_offset & QCOW_OFLAG_COMPRESSED) {
            if (qcow2_decompress_cluster(s, cluster_offset) < 0)
                return -1;
            memcpy(buf, s->cluster_cache + index_in_cluster * 512, 512 * n);
        } else {
            ret = bdrv_pread(s->hd, cluster_offset + index_in_cluster * 512, buf, n * 512);
            if (ret != n * 512)
                return -1;
            if (s->crypt_method) {
                qcow2_encrypt_sectors(s, sector_num, buf, buf, n, 0,
                                &s->aes_decrypt_key);
            }
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;
    }
    return 0;
}

static int copy_sectors(BlockDriverState *bs, uint64_t start_sect,
                        uint64_t cluster_offset, int n_start, int n_end)
{
    BDRVQcowState *s = bs->opaque;
    int n, ret;

    n = n_end - n_start;
    if (n <= 0)
        return 0;
    ret = qcow_read(bs, start_sect + n_start, s->cluster_data, n);
    if (ret < 0)
        return ret;
    if (s->crypt_method) {
        qcow2_encrypt_sectors(s, start_sect + n_start,
                        s->cluster_data,
                        s->cluster_data, n, 1,
                        &s->aes_encrypt_key);
    }
    ret = bdrv_write(s->hd, (cluster_offset >> 9) + n_start,
                     s->cluster_data, n);
    if (ret < 0)
        return ret;
    return 0;
}


/*
 * get_cluster_offset
 *
 * For a given offset of the disk image, return cluster offset in
 * qcow2 file.
 *
 * on entry, *num is the number of contiguous clusters we'd like to
 * access following offset.
 *
 * on exit, *num is the number of contiguous clusters we can read.
 *
 * Return 1, if the offset is found
 * Return 0, otherwise.
 *
 */

uint64_t qcow2_get_cluster_offset(BlockDriverState *bs, uint64_t offset,
    int *num)
{
    BDRVQcowState *s = bs->opaque;
    int l1_index, l2_index;
    uint64_t l2_offset, *l2_table, cluster_offset;
    int l1_bits, c;
    int index_in_cluster, nb_available, nb_needed, nb_clusters;

    index_in_cluster = (offset >> 9) & (s->cluster_sectors - 1);
    nb_needed = *num + index_in_cluster;

    l1_bits = s->l2_bits + s->cluster_bits;

    /* compute how many bytes there are between the offset and
     * the end of the l1 entry
     */

    nb_available = (1 << l1_bits) - (offset & ((1 << l1_bits) - 1));

    /* compute the number of available sectors */

    nb_available = (nb_available >> 9) + index_in_cluster;

    if (nb_needed > nb_available) {
        nb_needed = nb_available;
    }

    cluster_offset = 0;

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
    l2_table = l2_load(bs, l2_offset);
    if (l2_table == NULL)
        return 0;

    /* find the cluster offset for the given disk offset */

    l2_index = (offset >> s->cluster_bits) & (s->l2_size - 1);
    cluster_offset = be64_to_cpu(l2_table[l2_index]);
    nb_clusters = size_to_clusters(s, nb_needed << 9);

    if (!cluster_offset) {
        /* how many empty clusters ? */
        c = count_contiguous_free_clusters(nb_clusters, &l2_table[l2_index]);
    } else {
        /* how many allocated clusters ? */
        c = count_contiguous_clusters(nb_clusters, s->cluster_size,
                &l2_table[l2_index], 0, QCOW_OFLAG_COPIED);
    }

   nb_available = (c * s->cluster_sectors);
out:
    if (nb_available > nb_needed)
        nb_available = nb_needed;

    *num = nb_available - index_in_cluster;

    return cluster_offset & ~QCOW_OFLAG_COPIED;
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
 */

static int get_cluster_table(BlockDriverState *bs, uint64_t offset,
                             uint64_t **new_l2_table,
                             uint64_t *new_l2_offset,
                             int *new_l2_index)
{
    BDRVQcowState *s = bs->opaque;
    int l1_index, l2_index, ret;
    uint64_t l2_offset, *l2_table;

    /* seek the the l2 offset in the l1 table */

    l1_index = offset >> (s->l2_bits + s->cluster_bits);
    if (l1_index >= s->l1_size) {
        ret = qcow2_grow_l1_table(bs, l1_index + 1);
        if (ret < 0)
            return 0;
    }
    l2_offset = s->l1_table[l1_index];

    /* seek the l2 table of the given l2 offset */

    if (l2_offset & QCOW_OFLAG_COPIED) {
        /* load the l2 table in memory */
        l2_offset &= ~QCOW_OFLAG_COPIED;
        l2_table = l2_load(bs, l2_offset);
        if (l2_table == NULL)
            return 0;
    } else {
        if (l2_offset)
            qcow2_free_clusters(bs, l2_offset, s->l2_size * sizeof(uint64_t));
        l2_table = l2_allocate(bs, l1_index);
        if (l2_table == NULL)
            return 0;
        l2_offset = s->l1_table[l1_index] & ~QCOW_OFLAG_COPIED;
    }

    /* find the cluster offset for the given disk offset */

    l2_index = (offset >> s->cluster_bits) & (s->l2_size - 1);

    *new_l2_table = l2_table;
    *new_l2_offset = l2_offset;
    *new_l2_index = l2_index;

    return 1;
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
    uint64_t l2_offset, *l2_table, cluster_offset;
    int nb_csectors;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_offset, &l2_index);
    if (ret == 0)
        return 0;

    cluster_offset = be64_to_cpu(l2_table[l2_index]);
    if (cluster_offset & QCOW_OFLAG_COPIED)
        return cluster_offset & ~QCOW_OFLAG_COPIED;

    if (cluster_offset)
        qcow2_free_any_clusters(bs, cluster_offset, 1);

    cluster_offset = qcow2_alloc_bytes(bs, compressed_size);
    nb_csectors = ((cluster_offset + compressed_size - 1) >> 9) -
                  (cluster_offset >> 9);

    cluster_offset |= QCOW_OFLAG_COMPRESSED |
                      ((uint64_t)nb_csectors << s->csize_shift);

    /* update L2 table */

    /* compressed clusters never have the copied flag */

    l2_table[l2_index] = cpu_to_be64(cluster_offset);
    if (bdrv_pwrite(s->hd,
                    l2_offset + l2_index * sizeof(uint64_t),
                    l2_table + l2_index,
                    sizeof(uint64_t)) != sizeof(uint64_t))
        return 0;

    return cluster_offset;
}

/*
 * Write L2 table updates to disk, writing whole sectors to avoid a
 * read-modify-write in bdrv_pwrite
 */
#define L2_ENTRIES_PER_SECTOR (512 / 8)
static int write_l2_entries(BDRVQcowState *s, uint64_t *l2_table,
    uint64_t l2_offset, int l2_index, int num)
{
    int l2_start_index = l2_index & ~(L1_ENTRIES_PER_SECTOR - 1);
    int start_offset = (8 * l2_index) & ~511;
    int end_offset = (8 * (l2_index + num) + 511) & ~511;
    size_t len = end_offset - start_offset;

    if (bdrv_pwrite(s->hd, l2_offset + start_offset, &l2_table[l2_start_index],
        len) != len)
    {
        return -1;
    }

    return 0;
}

int qcow2_alloc_cluster_link_l2(BlockDriverState *bs, uint64_t cluster_offset,
    QCowL2Meta *m)
{
    BDRVQcowState *s = bs->opaque;
    int i, j = 0, l2_index, ret;
    uint64_t *old_cluster, start_sect, l2_offset, *l2_table;

    if (m->nb_clusters == 0)
        return 0;

    old_cluster = qemu_malloc(m->nb_clusters * sizeof(uint64_t));

    /* copy content of unmodified sectors */
    start_sect = (m->offset & ~(s->cluster_size - 1)) >> 9;
    if (m->n_start) {
        ret = copy_sectors(bs, start_sect, cluster_offset, 0, m->n_start);
        if (ret < 0)
            goto err;
    }

    if (m->nb_available & (s->cluster_sectors - 1)) {
        uint64_t end = m->nb_available & ~(uint64_t)(s->cluster_sectors - 1);
        ret = copy_sectors(bs, start_sect + end, cluster_offset + (end << 9),
                m->nb_available - end, s->cluster_sectors);
        if (ret < 0)
            goto err;
    }

    ret = -EIO;
    /* update L2 table */
    if (!get_cluster_table(bs, m->offset, &l2_table, &l2_offset, &l2_index))
        goto err;

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

    if (write_l2_entries(s, l2_table, l2_offset, l2_index, m->nb_clusters) < 0) {
        ret = -1;
        goto err;
    }

    for (i = 0; i < j; i++)
        qcow2_free_any_clusters(bs,
            be64_to_cpu(old_cluster[i]) & ~QCOW_OFLAG_COPIED, 1);

    ret = 0;
err:
    qemu_free(old_cluster);
    return ret;
 }

/*
 * alloc_cluster_offset
 *
 * For a given offset of the disk image, return cluster offset in
 * qcow2 file.
 *
 * If the offset is not found, allocate a new cluster.
 *
 * Return the cluster offset if successful,
 * Return 0, otherwise.
 *
 */

uint64_t qcow2_alloc_cluster_offset(BlockDriverState *bs,
                                    uint64_t offset,
                                    int n_start, int n_end,
                                    int *num, QCowL2Meta *m)
{
    BDRVQcowState *s = bs->opaque;
    int l2_index, ret;
    uint64_t l2_offset, *l2_table, cluster_offset;
    int nb_clusters, i = 0;

    ret = get_cluster_table(bs, offset, &l2_table, &l2_offset, &l2_index);
    if (ret == 0)
        return 0;

    nb_clusters = size_to_clusters(s, n_end << 9);

    nb_clusters = MIN(nb_clusters, s->l2_size - l2_index);

    cluster_offset = be64_to_cpu(l2_table[l2_index]);

    /* We keep all QCOW_OFLAG_COPIED clusters */

    if (cluster_offset & QCOW_OFLAG_COPIED) {
        nb_clusters = count_contiguous_clusters(nb_clusters, s->cluster_size,
                &l2_table[l2_index], 0, 0);

        cluster_offset &= ~QCOW_OFLAG_COPIED;
        m->nb_clusters = 0;

        goto out;
    }

    /* for the moment, multiple compressed clusters are not managed */

    if (cluster_offset & QCOW_OFLAG_COMPRESSED)
        nb_clusters = 1;

    /* how many available clusters ? */

    while (i < nb_clusters) {
        i += count_contiguous_clusters(nb_clusters - i, s->cluster_size,
                &l2_table[l2_index], i, 0);

        if(be64_to_cpu(l2_table[l2_index + i]))
            break;

        i += count_contiguous_free_clusters(nb_clusters - i,
                &l2_table[l2_index + i]);

        cluster_offset = be64_to_cpu(l2_table[l2_index + i]);

        if ((cluster_offset & QCOW_OFLAG_COPIED) ||
                (cluster_offset & QCOW_OFLAG_COMPRESSED))
            break;
    }
    nb_clusters = i;

    /* allocate a new cluster */

    cluster_offset = qcow2_alloc_clusters(bs, nb_clusters * s->cluster_size);

    /* save info needed for meta data update */
    m->offset = offset;
    m->n_start = n_start;
    m->nb_clusters = nb_clusters;

out:
    m->nb_available = MIN(nb_clusters << (s->cluster_bits - 9), n_end);

    *num = m->nb_available - n_start;

    return cluster_offset;
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

int qcow2_decompress_cluster(BDRVQcowState *s, uint64_t cluster_offset)
{
    int ret, csize, nb_csectors, sector_offset;
    uint64_t coffset;

    coffset = cluster_offset & s->cluster_offset_mask;
    if (s->cluster_cache_offset != coffset) {
        nb_csectors = ((cluster_offset >> s->csize_shift) & s->csize_mask) + 1;
        sector_offset = coffset & 511;
        csize = nb_csectors * 512 - sector_offset;
        ret = bdrv_read(s->hd, coffset >> 9, s->cluster_data, nb_csectors);
        if (ret < 0) {
            return -1;
        }
        if (decompress_buffer(s->cluster_cache, s->cluster_size,
                              s->cluster_data + sector_offset, csize) < 0) {
            return -1;
        }
        s->cluster_cache_offset = coffset;
    }
    return 0;
}
