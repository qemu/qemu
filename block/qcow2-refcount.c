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
#include "block_int.h"
#include "block/qcow2.h"

static int64_t alloc_clusters_noref(BlockDriverState *bs, int64_t size);
static int update_refcount(BlockDriverState *bs,
                            int64_t offset, int64_t length,
                            int addend);

/*********************************************************/
/* refcount handling */

int qcow2_refcount_init(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int ret, refcount_table_size2, i;

    s->refcount_block_cache = qemu_malloc(s->cluster_size);
    refcount_table_size2 = s->refcount_table_size * sizeof(uint64_t);
    s->refcount_table = qemu_malloc(refcount_table_size2);
    if (s->refcount_table_size > 0) {
        ret = bdrv_pread(s->hd, s->refcount_table_offset,
                         s->refcount_table, refcount_table_size2);
        if (ret != refcount_table_size2)
            goto fail;
        for(i = 0; i < s->refcount_table_size; i++)
            be64_to_cpus(&s->refcount_table[i]);
    }
    return 0;
 fail:
    return -ENOMEM;
}

void qcow2_refcount_close(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    qemu_free(s->refcount_block_cache);
    qemu_free(s->refcount_table);
}


static int load_refcount_block(BlockDriverState *bs,
                               int64_t refcount_block_offset)
{
    BDRVQcowState *s = bs->opaque;
    int ret;
    ret = bdrv_pread(s->hd, refcount_block_offset, s->refcount_block_cache,
                     s->cluster_size);
    if (ret != s->cluster_size)
        return -EIO;
    s->refcount_block_cache_offset = refcount_block_offset;
    return 0;
}

static int get_refcount(BlockDriverState *bs, int64_t cluster_index)
{
    BDRVQcowState *s = bs->opaque;
    int refcount_table_index, block_index;
    int64_t refcount_block_offset;

    refcount_table_index = cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);
    if (refcount_table_index >= s->refcount_table_size)
        return 0;
    refcount_block_offset = s->refcount_table[refcount_table_index];
    if (!refcount_block_offset)
        return 0;
    if (refcount_block_offset != s->refcount_block_cache_offset) {
        /* better than nothing: return allocated if read error */
        if (load_refcount_block(bs, refcount_block_offset) < 0)
            return 1;
    }
    block_index = cluster_index &
        ((1 << (s->cluster_bits - REFCOUNT_SHIFT)) - 1);
    return be16_to_cpu(s->refcount_block_cache[block_index]);
}

static int grow_refcount_table(BlockDriverState *bs, int min_size)
{
    BDRVQcowState *s = bs->opaque;
    int new_table_size, new_table_size2, refcount_table_clusters, i, ret;
    uint64_t *new_table;
    int64_t table_offset;
    uint8_t data[12];
    int old_table_size;
    int64_t old_table_offset;

    if (min_size <= s->refcount_table_size)
        return 0;
    /* compute new table size */
    refcount_table_clusters = s->refcount_table_size >> (s->cluster_bits - 3);
    for(;;) {
        if (refcount_table_clusters == 0) {
            refcount_table_clusters = 1;
        } else {
            refcount_table_clusters = (refcount_table_clusters * 3 + 1) / 2;
        }
        new_table_size = refcount_table_clusters << (s->cluster_bits - 3);
        if (min_size <= new_table_size)
            break;
    }
#ifdef DEBUG_ALLOC2
    printf("grow_refcount_table from %d to %d\n",
           s->refcount_table_size,
           new_table_size);
#endif
    new_table_size2 = new_table_size * sizeof(uint64_t);
    new_table = qemu_mallocz(new_table_size2);
    memcpy(new_table, s->refcount_table,
           s->refcount_table_size * sizeof(uint64_t));
    for(i = 0; i < s->refcount_table_size; i++)
        cpu_to_be64s(&new_table[i]);
    /* Note: we cannot update the refcount now to avoid recursion */
    table_offset = alloc_clusters_noref(bs, new_table_size2);
    ret = bdrv_pwrite(s->hd, table_offset, new_table, new_table_size2);
    if (ret != new_table_size2)
        goto fail;
    for(i = 0; i < s->refcount_table_size; i++)
        be64_to_cpus(&new_table[i]);

    cpu_to_be64w((uint64_t*)data, table_offset);
    cpu_to_be32w((uint32_t*)(data + 8), refcount_table_clusters);
    if (bdrv_pwrite(s->hd, offsetof(QCowHeader, refcount_table_offset),
                    data, sizeof(data)) != sizeof(data))
        goto fail;
    qemu_free(s->refcount_table);
    old_table_offset = s->refcount_table_offset;
    old_table_size = s->refcount_table_size;
    s->refcount_table = new_table;
    s->refcount_table_size = new_table_size;
    s->refcount_table_offset = table_offset;

    update_refcount(bs, table_offset, new_table_size2, 1);
    qcow2_free_clusters(bs, old_table_offset, old_table_size * sizeof(uint64_t));
    return 0;
 fail:
    qcow2_free_clusters(bs, table_offset, new_table_size2);
    qemu_free(new_table);
    return -EIO;
}


static int64_t alloc_refcount_block(BlockDriverState *bs, int64_t cluster_index)
{
    BDRVQcowState *s = bs->opaque;
    int64_t offset, refcount_block_offset;
    int ret, refcount_table_index;
    uint64_t data64;

    /* Find L1 index and grow refcount table if needed */
    refcount_table_index = cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);
    if (refcount_table_index >= s->refcount_table_size) {
        ret = grow_refcount_table(bs, refcount_table_index + 1);
        if (ret < 0)
            return ret;
    }

    /* Load or allocate the refcount block */
    refcount_block_offset = s->refcount_table[refcount_table_index];
    if (!refcount_block_offset) {
        /* create a new refcount block */
        /* Note: we cannot update the refcount now to avoid recursion */
        offset = alloc_clusters_noref(bs, s->cluster_size);
        memset(s->refcount_block_cache, 0, s->cluster_size);
        ret = bdrv_pwrite(s->hd, offset, s->refcount_block_cache, s->cluster_size);
        if (ret != s->cluster_size)
            return -EINVAL;
        s->refcount_table[refcount_table_index] = offset;
        data64 = cpu_to_be64(offset);
        ret = bdrv_pwrite(s->hd, s->refcount_table_offset +
                          refcount_table_index * sizeof(uint64_t),
                          &data64, sizeof(data64));
        if (ret != sizeof(data64))
            return -EINVAL;

        refcount_block_offset = offset;
        s->refcount_block_cache_offset = offset;
        update_refcount(bs, offset, s->cluster_size, 1);
    } else {
        if (refcount_block_offset != s->refcount_block_cache_offset) {
            if (load_refcount_block(bs, refcount_block_offset) < 0)
                return -EIO;
        }
    }

    return refcount_block_offset;
}

#define REFCOUNTS_PER_SECTOR (512 >> REFCOUNT_SHIFT)
static int write_refcount_block_entries(BDRVQcowState *s,
    int64_t refcount_block_offset, int first_index, int last_index)
{
    size_t size;

    first_index &= ~(REFCOUNTS_PER_SECTOR - 1);
    last_index = (last_index + REFCOUNTS_PER_SECTOR)
        & ~(REFCOUNTS_PER_SECTOR - 1);

    size = (last_index - first_index) << REFCOUNT_SHIFT;
    if (bdrv_pwrite(s->hd,
        refcount_block_offset + (first_index << REFCOUNT_SHIFT),
        &s->refcount_block_cache[first_index], size) != size)
    {
        return -EIO;
    }

    return 0;
}

/* XXX: cache several refcount block clusters ? */
static int update_refcount(BlockDriverState *bs,
                            int64_t offset, int64_t length,
                            int addend)
{
    BDRVQcowState *s = bs->opaque;
    int64_t start, last, cluster_offset;
    int64_t refcount_block_offset = 0;
    int64_t table_index = -1, old_table_index;
    int first_index = -1, last_index = -1;

#ifdef DEBUG_ALLOC2
    printf("update_refcount: offset=%lld size=%lld addend=%d\n",
           offset, length, addend);
#endif
    if (length <= 0)
        return -EINVAL;
    start = offset & ~(s->cluster_size - 1);
    last = (offset + length - 1) & ~(s->cluster_size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size)
    {
        int block_index, refcount;
        int64_t cluster_index = cluster_offset >> s->cluster_bits;

        /* Only write refcount block to disk when we are done with it */
        old_table_index = table_index;
        table_index = cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);
        if ((old_table_index >= 0) && (table_index != old_table_index)) {

            if (write_refcount_block_entries(s, refcount_block_offset,
                first_index, last_index) < 0)
            {
                return -EIO;
            }

            first_index = -1;
            last_index = -1;
        }

        /* Load the refcount block and allocate it if needed */
        refcount_block_offset = alloc_refcount_block(bs, cluster_index);
        if (refcount_block_offset < 0) {
            return refcount_block_offset;
        }

        /* we can update the count and save it */
        block_index = cluster_index &
            ((1 << (s->cluster_bits - REFCOUNT_SHIFT)) - 1);
        if (first_index == -1 || block_index < first_index) {
            first_index = block_index;
        }
        if (block_index > last_index) {
            last_index = block_index;
        }

        refcount = be16_to_cpu(s->refcount_block_cache[block_index]);
        refcount += addend;
        if (refcount < 0 || refcount > 0xffff)
            return -EINVAL;
        if (refcount == 0 && cluster_index < s->free_cluster_index) {
            s->free_cluster_index = cluster_index;
        }
        s->refcount_block_cache[block_index] = cpu_to_be16(refcount);
    }

    /* Write last changed block to disk */
    if (refcount_block_offset != 0) {
        if (write_refcount_block_entries(s, refcount_block_offset,
            first_index, last_index) < 0)
        {
            return -EIO;
        }
    }

    return 0;
}

/* addend must be 1 or -1 */
static int update_cluster_refcount(BlockDriverState *bs,
                                   int64_t cluster_index,
                                   int addend)
{
    BDRVQcowState *s = bs->opaque;
    int ret;

    ret = update_refcount(bs, cluster_index << s->cluster_bits, 1, addend);
    if (ret < 0) {
        return ret;
    }

    return get_refcount(bs, cluster_index);
}



/*********************************************************/
/* cluster allocation functions */



/* return < 0 if error */
static int64_t alloc_clusters_noref(BlockDriverState *bs, int64_t size)
{
    BDRVQcowState *s = bs->opaque;
    int i, nb_clusters;

    nb_clusters = size_to_clusters(s, size);
retry:
    for(i = 0; i < nb_clusters; i++) {
        int64_t i = s->free_cluster_index++;
        if (get_refcount(bs, i) != 0)
            goto retry;
    }
#ifdef DEBUG_ALLOC2
    printf("alloc_clusters: size=%lld -> %lld\n",
            size,
            (s->free_cluster_index - nb_clusters) << s->cluster_bits);
#endif
    return (s->free_cluster_index - nb_clusters) << s->cluster_bits;
}

int64_t qcow2_alloc_clusters(BlockDriverState *bs, int64_t size)
{
    int64_t offset;

    offset = alloc_clusters_noref(bs, size);
    update_refcount(bs, offset, size, 1);
    return offset;
}

/* only used to allocate compressed sectors. We try to allocate
   contiguous sectors. size must be <= cluster_size */
int64_t qcow2_alloc_bytes(BlockDriverState *bs, int size)
{
    BDRVQcowState *s = bs->opaque;
    int64_t offset, cluster_offset;
    int free_in_cluster;

    assert(size > 0 && size <= s->cluster_size);
    if (s->free_byte_offset == 0) {
        s->free_byte_offset = qcow2_alloc_clusters(bs, s->cluster_size);
    }
 redo:
    free_in_cluster = s->cluster_size -
        (s->free_byte_offset & (s->cluster_size - 1));
    if (size <= free_in_cluster) {
        /* enough space in current cluster */
        offset = s->free_byte_offset;
        s->free_byte_offset += size;
        free_in_cluster -= size;
        if (free_in_cluster == 0)
            s->free_byte_offset = 0;
        if ((offset & (s->cluster_size - 1)) != 0)
            update_cluster_refcount(bs, offset >> s->cluster_bits, 1);
    } else {
        offset = qcow2_alloc_clusters(bs, s->cluster_size);
        cluster_offset = s->free_byte_offset & ~(s->cluster_size - 1);
        if ((cluster_offset + s->cluster_size) == offset) {
            /* we are lucky: contiguous data */
            offset = s->free_byte_offset;
            update_cluster_refcount(bs, offset >> s->cluster_bits, 1);
            s->free_byte_offset += size;
        } else {
            s->free_byte_offset = offset;
            goto redo;
        }
    }
    return offset;
}

void qcow2_free_clusters(BlockDriverState *bs,
                          int64_t offset, int64_t size)
{
    update_refcount(bs, offset, size, -1);
}

/*
 * free_any_clusters
 *
 * free clusters according to its type: compressed or not
 *
 */

void qcow2_free_any_clusters(BlockDriverState *bs,
    uint64_t cluster_offset, int nb_clusters)
{
    BDRVQcowState *s = bs->opaque;

    /* free the cluster */

    if (cluster_offset & QCOW_OFLAG_COMPRESSED) {
        int nb_csectors;
        nb_csectors = ((cluster_offset >> s->csize_shift) &
                       s->csize_mask) + 1;
        qcow2_free_clusters(bs,
            (cluster_offset & s->cluster_offset_mask) & ~511,
            nb_csectors * 512);
        return;
    }

    qcow2_free_clusters(bs, cluster_offset, nb_clusters << s->cluster_bits);

    return;
}



/*********************************************************/
/* snapshots and image creation */



void qcow2_create_refcount_update(QCowCreateState *s, int64_t offset,
    int64_t size)
{
    int refcount;
    int64_t start, last, cluster_offset;
    uint16_t *p;

    start = offset & ~(s->cluster_size - 1);
    last = (offset + size - 1)  & ~(s->cluster_size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size) {
        p = &s->refcount_block[cluster_offset >> s->cluster_bits];
        refcount = be16_to_cpu(*p);
        refcount++;
        *p = cpu_to_be16(refcount);
    }
}

/* update the refcounts of snapshots and the copied flag */
int qcow2_update_snapshot_refcount(BlockDriverState *bs,
    int64_t l1_table_offset, int l1_size, int addend)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t *l1_table, *l2_table, l2_offset, offset, l1_size2, l1_allocated;
    int64_t old_offset, old_l2_offset;
    int l2_size, i, j, l1_modified, l2_modified, nb_csectors, refcount;

    qcow2_l2_cache_reset(bs);

    l2_table = NULL;
    l1_table = NULL;
    l1_size2 = l1_size * sizeof(uint64_t);
    l1_allocated = 0;
    if (l1_table_offset != s->l1_table_offset) {
        l1_table = qemu_malloc(l1_size2);
        l1_allocated = 1;
        if (bdrv_pread(s->hd, l1_table_offset,
                       l1_table, l1_size2) != l1_size2)
            goto fail;
        for(i = 0;i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    } else {
        assert(l1_size == s->l1_size);
        l1_table = s->l1_table;
        l1_allocated = 0;
    }

    l2_size = s->l2_size * sizeof(uint64_t);
    l2_table = qemu_malloc(l2_size);
    l1_modified = 0;
    for(i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            old_l2_offset = l2_offset;
            l2_offset &= ~QCOW_OFLAG_COPIED;
            l2_modified = 0;
            if (bdrv_pread(s->hd, l2_offset, l2_table, l2_size) != l2_size)
                goto fail;
            for(j = 0; j < s->l2_size; j++) {
                offset = be64_to_cpu(l2_table[j]);
                if (offset != 0) {
                    old_offset = offset;
                    offset &= ~QCOW_OFLAG_COPIED;
                    if (offset & QCOW_OFLAG_COMPRESSED) {
                        nb_csectors = ((offset >> s->csize_shift) &
                                       s->csize_mask) + 1;
                        if (addend != 0)
                            update_refcount(bs, (offset & s->cluster_offset_mask) & ~511,
                                            nb_csectors * 512, addend);
                        /* compressed clusters are never modified */
                        refcount = 2;
                    } else {
                        if (addend != 0) {
                            refcount = update_cluster_refcount(bs, offset >> s->cluster_bits, addend);
                        } else {
                            refcount = get_refcount(bs, offset >> s->cluster_bits);
                        }
                    }

                    if (refcount == 1) {
                        offset |= QCOW_OFLAG_COPIED;
                    }
                    if (offset != old_offset) {
                        l2_table[j] = cpu_to_be64(offset);
                        l2_modified = 1;
                    }
                }
            }
            if (l2_modified) {
                if (bdrv_pwrite(s->hd,
                                l2_offset, l2_table, l2_size) != l2_size)
                    goto fail;
            }

            if (addend != 0) {
                refcount = update_cluster_refcount(bs, l2_offset >> s->cluster_bits, addend);
            } else {
                refcount = get_refcount(bs, l2_offset >> s->cluster_bits);
            }
            if (refcount == 1) {
                l2_offset |= QCOW_OFLAG_COPIED;
            }
            if (l2_offset != old_l2_offset) {
                l1_table[i] = l2_offset;
                l1_modified = 1;
            }
        }
    }
    if (l1_modified) {
        for(i = 0; i < l1_size; i++)
            cpu_to_be64s(&l1_table[i]);
        if (bdrv_pwrite(s->hd, l1_table_offset, l1_table,
                        l1_size2) != l1_size2)
            goto fail;
        for(i = 0; i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    }
    if (l1_allocated)
        qemu_free(l1_table);
    qemu_free(l2_table);
    return 0;
 fail:
    if (l1_allocated)
        qemu_free(l1_table);
    qemu_free(l2_table);
    return -EIO;
}




/*********************************************************/
/* refcount checking functions */



/*
 * Increases the refcount for a range of clusters in a given refcount table.
 * This is used to construct a temporary refcount table out of L1 and L2 tables
 * which can be compared the the refcount table saved in the image.
 *
 * Returns the number of errors in the image that were found
 */
static int inc_refcounts(BlockDriverState *bs,
                          uint16_t *refcount_table,
                          int refcount_table_size,
                          int64_t offset, int64_t size)
{
    BDRVQcowState *s = bs->opaque;
    int64_t start, last, cluster_offset;
    int k;
    int errors = 0;

    if (size <= 0)
        return 0;

    start = offset & ~(s->cluster_size - 1);
    last = (offset + size - 1) & ~(s->cluster_size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size) {
        k = cluster_offset >> s->cluster_bits;
        if (k < 0 || k >= refcount_table_size) {
            fprintf(stderr, "ERROR: invalid cluster offset=0x%" PRIx64 "\n",
                cluster_offset);
            errors++;
        } else {
            if (++refcount_table[k] == 0) {
                fprintf(stderr, "ERROR: overflow cluster offset=0x%" PRIx64
                    "\n", cluster_offset);
                errors++;
            }
        }
    }

    return errors;
}

/*
 * Increases the refcount in the given refcount table for the all clusters
 * referenced in the L2 table. While doing so, performs some checks on L2
 * entries.
 *
 * Returns the number of errors found by the checks or -errno if an internal
 * error occurred.
 */
static int check_refcounts_l2(BlockDriverState *bs,
    uint16_t *refcount_table, int refcount_table_size, int64_t l2_offset,
    int check_copied)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t *l2_table, offset;
    int i, l2_size, nb_csectors, refcount;
    int errors = 0;

    /* Read L2 table from disk */
    l2_size = s->l2_size * sizeof(uint64_t);
    l2_table = qemu_malloc(l2_size);

    if (bdrv_pread(s->hd, l2_offset, l2_table, l2_size) != l2_size)
        goto fail;

    /* Do the actual checks */
    for(i = 0; i < s->l2_size; i++) {
        offset = be64_to_cpu(l2_table[i]);
        if (offset != 0) {
            if (offset & QCOW_OFLAG_COMPRESSED) {
                /* Compressed clusters don't have QCOW_OFLAG_COPIED */
                if (offset & QCOW_OFLAG_COPIED) {
                    fprintf(stderr, "ERROR: cluster %" PRId64 ": "
                        "copied flag must never be set for compressed "
                        "clusters\n", offset >> s->cluster_bits);
                    offset &= ~QCOW_OFLAG_COPIED;
                    errors++;
                }

                /* Mark cluster as used */
                nb_csectors = ((offset >> s->csize_shift) &
                               s->csize_mask) + 1;
                offset &= s->cluster_offset_mask;
                errors += inc_refcounts(bs, refcount_table,
                              refcount_table_size,
                              offset & ~511, nb_csectors * 512);
            } else {
                /* QCOW_OFLAG_COPIED must be set iff refcount == 1 */
                if (check_copied) {
                    uint64_t entry = offset;
                    offset &= ~QCOW_OFLAG_COPIED;
                    refcount = get_refcount(bs, offset >> s->cluster_bits);
                    if ((refcount == 1) != ((entry & QCOW_OFLAG_COPIED) != 0)) {
                        fprintf(stderr, "ERROR OFLAG_COPIED: offset=%"
                            PRIx64 " refcount=%d\n", entry, refcount);
                        errors++;
                    }
                }

                /* Mark cluster as used */
                offset &= ~QCOW_OFLAG_COPIED;
                errors += inc_refcounts(bs, refcount_table,
                              refcount_table_size,
                              offset, s->cluster_size);

                /* Correct offsets are cluster aligned */
                if (offset & (s->cluster_size - 1)) {
                    fprintf(stderr, "ERROR offset=%" PRIx64 ": Cluster is not "
                        "properly aligned; L2 entry corrupted.\n", offset);
                    errors++;
                }
            }
        }
    }

    qemu_free(l2_table);
    return errors;

fail:
    fprintf(stderr, "ERROR: I/O error in check_refcounts_l1\n");
    qemu_free(l2_table);
    return -EIO;
}

/*
 * Increases the refcount for the L1 table, its L2 tables and all referenced
 * clusters in the given refcount table. While doing so, performs some checks
 * on L1 and L2 entries.
 *
 * Returns the number of errors found by the checks or -errno if an internal
 * error occurred.
 */
static int check_refcounts_l1(BlockDriverState *bs,
                              uint16_t *refcount_table,
                              int refcount_table_size,
                              int64_t l1_table_offset, int l1_size,
                              int check_copied)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t *l1_table, l2_offset, l1_size2;
    int i, refcount, ret;
    int errors = 0;

    l1_size2 = l1_size * sizeof(uint64_t);

    /* Mark L1 table as used */
    errors += inc_refcounts(bs, refcount_table, refcount_table_size,
                  l1_table_offset, l1_size2);

    /* Read L1 table entries from disk */
    l1_table = qemu_malloc(l1_size2);
    if (bdrv_pread(s->hd, l1_table_offset,
                   l1_table, l1_size2) != l1_size2)
        goto fail;
    for(i = 0;i < l1_size; i++)
        be64_to_cpus(&l1_table[i]);

    /* Do the actual checks */
    for(i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            /* QCOW_OFLAG_COPIED must be set iff refcount == 1 */
            if (check_copied) {
                refcount = get_refcount(bs, (l2_offset & ~QCOW_OFLAG_COPIED)
                    >> s->cluster_bits);
                if ((refcount == 1) != ((l2_offset & QCOW_OFLAG_COPIED) != 0)) {
                    fprintf(stderr, "ERROR OFLAG_COPIED: l2_offset=%" PRIx64
                        " refcount=%d\n", l2_offset, refcount);
                    errors++;
                }
            }

            /* Mark L2 table as used */
            l2_offset &= ~QCOW_OFLAG_COPIED;
            errors += inc_refcounts(bs, refcount_table,
                          refcount_table_size,
                          l2_offset,
                          s->cluster_size);

            /* L2 tables are cluster aligned */
            if (l2_offset & (s->cluster_size - 1)) {
                fprintf(stderr, "ERROR l2_offset=%" PRIx64 ": Table is not "
                    "cluster aligned; L1 entry corrupted\n", l2_offset);
                errors++;
            }

            /* Process and check L2 entries */
            ret = check_refcounts_l2(bs, refcount_table, refcount_table_size,
                l2_offset, check_copied);
            if (ret < 0) {
                goto fail;
            }
            errors += ret;
        }
    }
    qemu_free(l1_table);
    return errors;

fail:
    fprintf(stderr, "ERROR: I/O error in check_refcounts_l1\n");
    qemu_free(l1_table);
    return -EIO;
}

/*
 * Checks an image for refcount consistency.
 *
 * Returns 0 if no errors are found, the number of errors in case the image is
 * detected as corrupted, and -errno when an internal error occured.
 */
int qcow2_check_refcounts(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int64_t size;
    int nb_clusters, refcount1, refcount2, i;
    QCowSnapshot *sn;
    uint16_t *refcount_table;
    int ret, errors = 0;

    size = bdrv_getlength(s->hd);
    nb_clusters = size_to_clusters(s, size);
    refcount_table = qemu_mallocz(nb_clusters * sizeof(uint16_t));

    /* header */
    errors += inc_refcounts(bs, refcount_table, nb_clusters,
                  0, s->cluster_size);

    /* current L1 table */
    ret = check_refcounts_l1(bs, refcount_table, nb_clusters,
                       s->l1_table_offset, s->l1_size, 1);
    if (ret < 0) {
        return ret;
    }
    errors += ret;

    /* snapshots */
    for(i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        check_refcounts_l1(bs, refcount_table, nb_clusters,
                           sn->l1_table_offset, sn->l1_size, 0);
    }
    errors += inc_refcounts(bs, refcount_table, nb_clusters,
                  s->snapshots_offset, s->snapshots_size);

    /* refcount data */
    errors += inc_refcounts(bs, refcount_table, nb_clusters,
                  s->refcount_table_offset,
                  s->refcount_table_size * sizeof(uint64_t));
    for(i = 0; i < s->refcount_table_size; i++) {
        int64_t offset;
        offset = s->refcount_table[i];
        if (offset != 0) {
            errors += inc_refcounts(bs, refcount_table, nb_clusters,
                          offset, s->cluster_size);
        }
    }

    /* compare ref counts */
    for(i = 0; i < nb_clusters; i++) {
        refcount1 = get_refcount(bs, i);
        refcount2 = refcount_table[i];
        if (refcount1 != refcount2) {
            fprintf(stderr, "ERROR cluster %d refcount=%d reference=%d\n",
                   i, refcount1, refcount2);
            errors++;
        }
    }

    qemu_free(refcount_table);

    return errors;
}

