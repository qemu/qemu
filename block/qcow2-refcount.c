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
static int QEMU_WARN_UNUSED_RESULT update_refcount(BlockDriverState *bs,
                            int64_t offset, int64_t length,
                            int addend);


/*********************************************************/
/* refcount handling */

int qcow2_refcount_init(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int ret, refcount_table_size2, i;

    refcount_table_size2 = s->refcount_table_size * sizeof(uint64_t);
    s->refcount_table = qemu_malloc(refcount_table_size2);
    if (s->refcount_table_size > 0) {
        BLKDBG_EVENT(bs->file, BLKDBG_REFTABLE_LOAD);
        ret = bdrv_pread(bs->file, s->refcount_table_offset,
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
    qemu_free(s->refcount_table);
}


static int load_refcount_block(BlockDriverState *bs,
                               int64_t refcount_block_offset,
                               void **refcount_block)
{
    BDRVQcowState *s = bs->opaque;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_LOAD);
    ret = qcow2_cache_get(bs, s->refcount_block_cache, refcount_block_offset,
        refcount_block);

    return ret;
}

/*
 * Returns the refcount of the cluster given by its index. Any non-negative
 * return value is the refcount of the cluster, negative values are -errno
 * and indicate an error.
 */
static int get_refcount(BlockDriverState *bs, int64_t cluster_index)
{
    BDRVQcowState *s = bs->opaque;
    int refcount_table_index, block_index;
    int64_t refcount_block_offset;
    int ret;
    uint16_t *refcount_block;
    uint16_t refcount;

    refcount_table_index = cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);
    if (refcount_table_index >= s->refcount_table_size)
        return 0;
    refcount_block_offset = s->refcount_table[refcount_table_index];
    if (!refcount_block_offset)
        return 0;

    ret = qcow2_cache_get(bs, s->refcount_block_cache, refcount_block_offset,
        (void**) &refcount_block);
    if (ret < 0) {
        return ret;
    }

    block_index = cluster_index &
        ((1 << (s->cluster_bits - REFCOUNT_SHIFT)) - 1);
    refcount = be16_to_cpu(refcount_block[block_index]);

    ret = qcow2_cache_put(bs, s->refcount_block_cache,
        (void**) &refcount_block);
    if (ret < 0) {
        return ret;
    }

    return refcount;
}

/*
 * Rounds the refcount table size up to avoid growing the table for each single
 * refcount block that is allocated.
 */
static unsigned int next_refcount_table_size(BDRVQcowState *s,
    unsigned int min_size)
{
    unsigned int min_clusters = (min_size >> (s->cluster_bits - 3)) + 1;
    unsigned int refcount_table_clusters =
        MAX(1, s->refcount_table_size >> (s->cluster_bits - 3));

    while (min_clusters > refcount_table_clusters) {
        refcount_table_clusters = (refcount_table_clusters * 3 + 1) / 2;
    }

    return refcount_table_clusters << (s->cluster_bits - 3);
}


/* Checks if two offsets are described by the same refcount block */
static int in_same_refcount_block(BDRVQcowState *s, uint64_t offset_a,
    uint64_t offset_b)
{
    uint64_t block_a = offset_a >> (2 * s->cluster_bits - REFCOUNT_SHIFT);
    uint64_t block_b = offset_b >> (2 * s->cluster_bits - REFCOUNT_SHIFT);

    return (block_a == block_b);
}

/*
 * Loads a refcount block. If it doesn't exist yet, it is allocated first
 * (including growing the refcount table if needed).
 *
 * Returns 0 on success or -errno in error case
 */
static int alloc_refcount_block(BlockDriverState *bs,
    int64_t cluster_index, uint16_t **refcount_block)
{
    BDRVQcowState *s = bs->opaque;
    unsigned int refcount_table_index;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC);

    /* Find the refcount block for the given cluster */
    refcount_table_index = cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);

    if (refcount_table_index < s->refcount_table_size) {

        uint64_t refcount_block_offset =
            s->refcount_table[refcount_table_index];

        /* If it's already there, we're done */
        if (refcount_block_offset) {
             return load_refcount_block(bs, refcount_block_offset,
                 (void**) refcount_block);
        }
    }

    /*
     * If we came here, we need to allocate something. Something is at least
     * a cluster for the new refcount block. It may also include a new refcount
     * table if the old refcount table is too small.
     *
     * Note that allocating clusters here needs some special care:
     *
     * - We can't use the normal qcow2_alloc_clusters(), it would try to
     *   increase the refcount and very likely we would end up with an endless
     *   recursion. Instead we must place the refcount blocks in a way that
     *   they can describe them themselves.
     *
     * - We need to consider that at this point we are inside update_refcounts
     *   and doing the initial refcount increase. This means that some clusters
     *   have already been allocated by the caller, but their refcount isn't
     *   accurate yet. free_cluster_index tells us where this allocation ends
     *   as long as we don't overwrite it by freeing clusters.
     *
     * - alloc_clusters_noref and qcow2_free_clusters may load a different
     *   refcount block into the cache
     */

    *refcount_block = NULL;

    /* We write to the refcount table, so we might depend on L2 tables */
    qcow2_cache_flush(bs, s->l2_table_cache);

    /* Allocate the refcount block itself and mark it as used */
    int64_t new_block = alloc_clusters_noref(bs, s->cluster_size);
    if (new_block < 0) {
        return new_block;
    }

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "qcow2: Allocate refcount block %d for %" PRIx64
        " at %" PRIx64 "\n",
        refcount_table_index, cluster_index << s->cluster_bits, new_block);
#endif

    if (in_same_refcount_block(s, new_block, cluster_index << s->cluster_bits)) {
        /* Zero the new refcount block before updating it */
        ret = qcow2_cache_get_empty(bs, s->refcount_block_cache, new_block,
            (void**) refcount_block);
        if (ret < 0) {
            goto fail_block;
        }

        memset(*refcount_block, 0, s->cluster_size);

        /* The block describes itself, need to update the cache */
        int block_index = (new_block >> s->cluster_bits) &
            ((1 << (s->cluster_bits - REFCOUNT_SHIFT)) - 1);
        (*refcount_block)[block_index] = cpu_to_be16(1);
    } else {
        /* Described somewhere else. This can recurse at most twice before we
         * arrive at a block that describes itself. */
        ret = update_refcount(bs, new_block, s->cluster_size, 1);
        if (ret < 0) {
            goto fail_block;
        }

        bdrv_flush(bs->file);

        /* Initialize the new refcount block only after updating its refcount,
         * update_refcount uses the refcount cache itself */
        ret = qcow2_cache_get_empty(bs, s->refcount_block_cache, new_block,
            (void**) refcount_block);
        if (ret < 0) {
            goto fail_block;
        }

        memset(*refcount_block, 0, s->cluster_size);
    }

    /* Now the new refcount block needs to be written to disk */
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE);
    qcow2_cache_entry_mark_dirty(s->refcount_block_cache, *refcount_block);
    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail_block;
    }

    /* If the refcount table is big enough, just hook the block up there */
    if (refcount_table_index < s->refcount_table_size) {
        uint64_t data64 = cpu_to_be64(new_block);
        BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_HOOKUP);
        ret = bdrv_pwrite_sync(bs->file,
            s->refcount_table_offset + refcount_table_index * sizeof(uint64_t),
            &data64, sizeof(data64));
        if (ret < 0) {
            goto fail_block;
        }

        s->refcount_table[refcount_table_index] = new_block;
        return 0;
    }

    ret = qcow2_cache_put(bs, s->refcount_block_cache, (void**) refcount_block);
    if (ret < 0) {
        goto fail_block;
    }

    /*
     * If we come here, we need to grow the refcount table. Again, a new
     * refcount table needs some space and we can't simply allocate to avoid
     * endless recursion.
     *
     * Therefore let's grab new refcount blocks at the end of the image, which
     * will describe themselves and the new refcount table. This way we can
     * reference them only in the new table and do the switch to the new
     * refcount table at once without producing an inconsistent state in
     * between.
     */
    BLKDBG_EVENT(bs->file, BLKDBG_REFTABLE_GROW);

    /* Calculate the number of refcount blocks needed so far */
    uint64_t refcount_block_clusters = 1 << (s->cluster_bits - REFCOUNT_SHIFT);
    uint64_t blocks_used = (s->free_cluster_index +
        refcount_block_clusters - 1) / refcount_block_clusters;

    /* And now we need at least one block more for the new metadata */
    uint64_t table_size = next_refcount_table_size(s, blocks_used + 1);
    uint64_t last_table_size;
    uint64_t blocks_clusters;
    do {
        uint64_t table_clusters = size_to_clusters(s, table_size);
        blocks_clusters = 1 +
            ((table_clusters + refcount_block_clusters - 1)
            / refcount_block_clusters);
        uint64_t meta_clusters = table_clusters + blocks_clusters;

        last_table_size = table_size;
        table_size = next_refcount_table_size(s, blocks_used +
            ((meta_clusters + refcount_block_clusters - 1)
            / refcount_block_clusters));

    } while (last_table_size != table_size);

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "qcow2: Grow refcount table %" PRId32 " => %" PRId64 "\n",
        s->refcount_table_size, table_size);
#endif

    /* Create the new refcount table and blocks */
    uint64_t meta_offset = (blocks_used * refcount_block_clusters) *
        s->cluster_size;
    uint64_t table_offset = meta_offset + blocks_clusters * s->cluster_size;
    uint16_t *new_blocks = qemu_mallocz(blocks_clusters * s->cluster_size);
    uint64_t *new_table = qemu_mallocz(table_size * sizeof(uint64_t));

    assert(meta_offset >= (s->free_cluster_index * s->cluster_size));

    /* Fill the new refcount table */
    memcpy(new_table, s->refcount_table,
        s->refcount_table_size * sizeof(uint64_t));
    new_table[refcount_table_index] = new_block;

    int i;
    for (i = 0; i < blocks_clusters; i++) {
        new_table[blocks_used + i] = meta_offset + (i * s->cluster_size);
    }

    /* Fill the refcount blocks */
    uint64_t table_clusters = size_to_clusters(s, table_size * sizeof(uint64_t));
    int block = 0;
    for (i = 0; i < table_clusters + blocks_clusters; i++) {
        new_blocks[block++] = cpu_to_be16(1);
    }

    /* Write refcount blocks to disk */
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE_BLOCKS);
    ret = bdrv_pwrite_sync(bs->file, meta_offset, new_blocks,
        blocks_clusters * s->cluster_size);
    qemu_free(new_blocks);
    if (ret < 0) {
        goto fail_table;
    }

    /* Write refcount table to disk */
    for(i = 0; i < table_size; i++) {
        cpu_to_be64s(&new_table[i]);
    }

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE_TABLE);
    ret = bdrv_pwrite_sync(bs->file, table_offset, new_table,
        table_size * sizeof(uint64_t));
    if (ret < 0) {
        goto fail_table;
    }

    for(i = 0; i < table_size; i++) {
        cpu_to_be64s(&new_table[i]);
    }

    /* Hook up the new refcount table in the qcow2 header */
    uint8_t data[12];
    cpu_to_be64w((uint64_t*)data, table_offset);
    cpu_to_be32w((uint32_t*)(data + 8), table_clusters);
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_SWITCH_TABLE);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, refcount_table_offset),
        data, sizeof(data));
    if (ret < 0) {
        goto fail_table;
    }

    /* And switch it in memory */
    uint64_t old_table_offset = s->refcount_table_offset;
    uint64_t old_table_size = s->refcount_table_size;

    qemu_free(s->refcount_table);
    s->refcount_table = new_table;
    s->refcount_table_size = table_size;
    s->refcount_table_offset = table_offset;

    /* Free old table. Remember, we must not change free_cluster_index */
    uint64_t old_free_cluster_index = s->free_cluster_index;
    qcow2_free_clusters(bs, old_table_offset, old_table_size * sizeof(uint64_t));
    s->free_cluster_index = old_free_cluster_index;

    ret = load_refcount_block(bs, new_block, (void**) refcount_block);
    if (ret < 0) {
        return ret;
    }

    return new_block;

fail_table:
    qemu_free(new_table);
fail_block:
    if (*refcount_block != NULL) {
        qcow2_cache_put(bs, s->refcount_block_cache, (void**) refcount_block);
    }
    return ret;
}

/* XXX: cache several refcount block clusters ? */
static int QEMU_WARN_UNUSED_RESULT update_refcount(BlockDriverState *bs,
    int64_t offset, int64_t length, int addend)
{
    BDRVQcowState *s = bs->opaque;
    int64_t start, last, cluster_offset;
    uint16_t *refcount_block = NULL;
    int64_t old_table_index = -1;
    int ret;

#ifdef DEBUG_ALLOC2
    printf("update_refcount: offset=%" PRId64 " size=%" PRId64 " addend=%d\n",
           offset, length, addend);
#endif
    if (length < 0) {
        return -EINVAL;
    } else if (length == 0) {
        return 0;
    }

    if (addend < 0) {
        qcow2_cache_set_dependency(bs, s->refcount_block_cache,
            s->l2_table_cache);
    }

    start = offset & ~(s->cluster_size - 1);
    last = (offset + length - 1) & ~(s->cluster_size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size)
    {
        int block_index, refcount;
        int64_t cluster_index = cluster_offset >> s->cluster_bits;
        int64_t table_index =
            cluster_index >> (s->cluster_bits - REFCOUNT_SHIFT);

        /* Load the refcount block and allocate it if needed */
        if (table_index != old_table_index) {
            if (refcount_block) {
                ret = qcow2_cache_put(bs, s->refcount_block_cache,
                    (void**) &refcount_block);
                if (ret < 0) {
                    goto fail;
                }
            }

            ret = alloc_refcount_block(bs, cluster_index, &refcount_block);
            if (ret < 0) {
                goto fail;
            }
        }
        old_table_index = table_index;

        qcow2_cache_entry_mark_dirty(s->refcount_block_cache, refcount_block);

        /* we can update the count and save it */
        block_index = cluster_index &
            ((1 << (s->cluster_bits - REFCOUNT_SHIFT)) - 1);

        refcount = be16_to_cpu(refcount_block[block_index]);
        refcount += addend;
        if (refcount < 0 || refcount > 0xffff) {
            ret = -EINVAL;
            goto fail;
        }
        if (refcount == 0 && cluster_index < s->free_cluster_index) {
            s->free_cluster_index = cluster_index;
        }
        refcount_block[block_index] = cpu_to_be16(refcount);
    }

    ret = 0;
fail:
    /* Write last changed block to disk */
    if (refcount_block) {
        int wret;
        wret = qcow2_cache_put(bs, s->refcount_block_cache,
            (void**) &refcount_block);
        if (wret < 0) {
            return ret < 0 ? ret : wret;
        }
    }

    /*
     * Try do undo any updates if an error is returned (This may succeed in
     * some cases like ENOSPC for allocating a new refcount block)
     */
    if (ret < 0) {
        int dummy;
        dummy = update_refcount(bs, offset, cluster_offset - offset, -addend);
        (void)dummy;
    }

    return ret;
}

/*
 * Increases or decreases the refcount of a given cluster by one.
 * addend must be 1 or -1.
 *
 * If the return value is non-negative, it is the new refcount of the cluster.
 * If it is negative, it is -errno and indicates an error.
 */
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

    bdrv_flush(bs->file);

    return get_refcount(bs, cluster_index);
}



/*********************************************************/
/* cluster allocation functions */



/* return < 0 if error */
static int64_t alloc_clusters_noref(BlockDriverState *bs, int64_t size)
{
    BDRVQcowState *s = bs->opaque;
    int i, nb_clusters, refcount;

    nb_clusters = size_to_clusters(s, size);
retry:
    for(i = 0; i < nb_clusters; i++) {
        int64_t next_cluster_index = s->free_cluster_index++;
        refcount = get_refcount(bs, next_cluster_index);

        if (refcount < 0) {
            return refcount;
        } else if (refcount != 0) {
            goto retry;
        }
    }
#ifdef DEBUG_ALLOC2
    printf("alloc_clusters: size=%" PRId64 " -> %" PRId64 "\n",
            size,
            (s->free_cluster_index - nb_clusters) << s->cluster_bits);
#endif
    return (s->free_cluster_index - nb_clusters) << s->cluster_bits;
}

int64_t qcow2_alloc_clusters(BlockDriverState *bs, int64_t size)
{
    int64_t offset;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC);
    offset = alloc_clusters_noref(bs, size);
    if (offset < 0) {
        return offset;
    }

    ret = update_refcount(bs, offset, size, 1);
    if (ret < 0) {
        return ret;
    }

    return offset;
}

/* only used to allocate compressed sectors. We try to allocate
   contiguous sectors. size must be <= cluster_size */
int64_t qcow2_alloc_bytes(BlockDriverState *bs, int size)
{
    BDRVQcowState *s = bs->opaque;
    int64_t offset, cluster_offset;
    int free_in_cluster;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC_BYTES);
    assert(size > 0 && size <= s->cluster_size);
    if (s->free_byte_offset == 0) {
        s->free_byte_offset = qcow2_alloc_clusters(bs, s->cluster_size);
        if (s->free_byte_offset < 0) {
            return s->free_byte_offset;
        }
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
        if (offset < 0) {
            return offset;
        }
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

    bdrv_flush(bs->file);
    return offset;
}

void qcow2_free_clusters(BlockDriverState *bs,
                          int64_t offset, int64_t size)
{
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_FREE);
    ret = update_refcount(bs, offset, size, -1);
    if (ret < 0) {
        fprintf(stderr, "qcow2_free_clusters failed: %s\n", strerror(-ret));
        /* TODO Remember the clusters to free them later and avoid leaking */
    }
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
    int i, j, l1_modified, nb_csectors, refcount;
    int ret;

    l2_table = NULL;
    l1_table = NULL;
    l1_size2 = l1_size * sizeof(uint64_t);
    if (l1_table_offset != s->l1_table_offset) {
        if (l1_size2 != 0) {
            l1_table = qemu_mallocz(align_offset(l1_size2, 512));
        } else {
            l1_table = NULL;
        }
        l1_allocated = 1;
        if (bdrv_pread(bs->file, l1_table_offset,
                       l1_table, l1_size2) != l1_size2)
            goto fail;
        for(i = 0;i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    } else {
        assert(l1_size == s->l1_size);
        l1_table = s->l1_table;
        l1_allocated = 0;
    }

    l1_modified = 0;
    for(i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            old_l2_offset = l2_offset;
            l2_offset &= ~QCOW_OFLAG_COPIED;

            ret = qcow2_cache_get(bs, s->l2_table_cache, l2_offset,
                (void**) &l2_table);
            if (ret < 0) {
                goto fail;
            }

            for(j = 0; j < s->l2_size; j++) {
                offset = be64_to_cpu(l2_table[j]);
                if (offset != 0) {
                    old_offset = offset;
                    offset &= ~QCOW_OFLAG_COPIED;
                    if (offset & QCOW_OFLAG_COMPRESSED) {
                        nb_csectors = ((offset >> s->csize_shift) &
                                       s->csize_mask) + 1;
                        if (addend != 0) {
                            int ret;
                            ret = update_refcount(bs,
                                (offset & s->cluster_offset_mask) & ~511,
                                nb_csectors * 512, addend);
                            if (ret < 0) {
                                goto fail;
                            }

                            /* TODO Flushing once for the whole function should
                             * be enough */
                            bdrv_flush(bs->file);
                        }
                        /* compressed clusters are never modified */
                        refcount = 2;
                    } else {
                        if (addend != 0) {
                            refcount = update_cluster_refcount(bs, offset >> s->cluster_bits, addend);
                        } else {
                            refcount = get_refcount(bs, offset >> s->cluster_bits);
                        }

                        if (refcount < 0) {
                            goto fail;
                        }
                    }

                    if (refcount == 1) {
                        offset |= QCOW_OFLAG_COPIED;
                    }
                    if (offset != old_offset) {
                        if (addend > 0) {
                            qcow2_cache_set_dependency(bs, s->l2_table_cache,
                                s->refcount_block_cache);
                        }
                        l2_table[j] = cpu_to_be64(offset);
                        qcow2_cache_entry_mark_dirty(s->l2_table_cache, l2_table);
                    }
                }
            }

            ret = qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
            if (ret < 0) {
                goto fail;
            }


            if (addend != 0) {
                refcount = update_cluster_refcount(bs, l2_offset >> s->cluster_bits, addend);
            } else {
                refcount = get_refcount(bs, l2_offset >> s->cluster_bits);
            }
            if (refcount < 0) {
                goto fail;
            } else if (refcount == 1) {
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
        if (bdrv_pwrite_sync(bs->file, l1_table_offset, l1_table,
                        l1_size2) < 0)
            goto fail;
        for(i = 0; i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    }
    if (l1_allocated)
        qemu_free(l1_table);
    return 0;
 fail:
    if (l2_table) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    }

    if (l1_allocated)
        qemu_free(l1_table);
    return -EIO;
}




/*********************************************************/
/* refcount checking functions */



/*
 * Increases the refcount for a range of clusters in a given refcount table.
 * This is used to construct a temporary refcount table out of L1 and L2 tables
 * which can be compared the the refcount table saved in the image.
 *
 * Modifies the number of errors in res.
 */
static void inc_refcounts(BlockDriverState *bs,
                          BdrvCheckResult *res,
                          uint16_t *refcount_table,
                          int refcount_table_size,
                          int64_t offset, int64_t size)
{
    BDRVQcowState *s = bs->opaque;
    int64_t start, last, cluster_offset;
    int k;

    if (size <= 0)
        return;

    start = offset & ~(s->cluster_size - 1);
    last = (offset + size - 1) & ~(s->cluster_size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size) {
        k = cluster_offset >> s->cluster_bits;
        if (k < 0) {
            fprintf(stderr, "ERROR: invalid cluster offset=0x%" PRIx64 "\n",
                cluster_offset);
            res->corruptions++;
        } else if (k >= refcount_table_size) {
            fprintf(stderr, "Warning: cluster offset=0x%" PRIx64 " is after "
                "the end of the image file, can't properly check refcounts.\n",
                cluster_offset);
            res->check_errors++;
        } else {
            if (++refcount_table[k] == 0) {
                fprintf(stderr, "ERROR: overflow cluster offset=0x%" PRIx64
                    "\n", cluster_offset);
                res->corruptions++;
            }
        }
    }
}

/*
 * Increases the refcount in the given refcount table for the all clusters
 * referenced in the L2 table. While doing so, performs some checks on L2
 * entries.
 *
 * Returns the number of errors found by the checks or -errno if an internal
 * error occurred.
 */
static int check_refcounts_l2(BlockDriverState *bs, BdrvCheckResult *res,
    uint16_t *refcount_table, int refcount_table_size, int64_t l2_offset,
    int check_copied)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t *l2_table, offset;
    int i, l2_size, nb_csectors, refcount;

    /* Read L2 table from disk */
    l2_size = s->l2_size * sizeof(uint64_t);
    l2_table = qemu_malloc(l2_size);

    if (bdrv_pread(bs->file, l2_offset, l2_table, l2_size) != l2_size)
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
                    res->corruptions++;
                }

                /* Mark cluster as used */
                nb_csectors = ((offset >> s->csize_shift) &
                               s->csize_mask) + 1;
                offset &= s->cluster_offset_mask;
                inc_refcounts(bs, res, refcount_table, refcount_table_size,
                    offset & ~511, nb_csectors * 512);
            } else {
                /* QCOW_OFLAG_COPIED must be set iff refcount == 1 */
                if (check_copied) {
                    uint64_t entry = offset;
                    offset &= ~QCOW_OFLAG_COPIED;
                    refcount = get_refcount(bs, offset >> s->cluster_bits);
                    if (refcount < 0) {
                        fprintf(stderr, "Can't get refcount for offset %"
                            PRIx64 ": %s\n", entry, strerror(-refcount));
                        goto fail;
                    }
                    if ((refcount == 1) != ((entry & QCOW_OFLAG_COPIED) != 0)) {
                        fprintf(stderr, "ERROR OFLAG_COPIED: offset=%"
                            PRIx64 " refcount=%d\n", entry, refcount);
                        res->corruptions++;
                    }
                }

                /* Mark cluster as used */
                offset &= ~QCOW_OFLAG_COPIED;
                inc_refcounts(bs, res, refcount_table,refcount_table_size,
                    offset, s->cluster_size);

                /* Correct offsets are cluster aligned */
                if (offset & (s->cluster_size - 1)) {
                    fprintf(stderr, "ERROR offset=%" PRIx64 ": Cluster is not "
                        "properly aligned; L2 entry corrupted.\n", offset);
                    res->corruptions++;
                }
            }
        }
    }

    qemu_free(l2_table);
    return 0;

fail:
    fprintf(stderr, "ERROR: I/O error in check_refcounts_l2\n");
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
                              BdrvCheckResult *res,
                              uint16_t *refcount_table,
                              int refcount_table_size,
                              int64_t l1_table_offset, int l1_size,
                              int check_copied)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t *l1_table, l2_offset, l1_size2;
    int i, refcount, ret;

    l1_size2 = l1_size * sizeof(uint64_t);

    /* Mark L1 table as used */
    inc_refcounts(bs, res, refcount_table, refcount_table_size,
        l1_table_offset, l1_size2);

    /* Read L1 table entries from disk */
    if (l1_size2 == 0) {
        l1_table = NULL;
    } else {
        l1_table = qemu_malloc(l1_size2);
        if (bdrv_pread(bs->file, l1_table_offset,
                       l1_table, l1_size2) != l1_size2)
            goto fail;
        for(i = 0;i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    }

    /* Do the actual checks */
    for(i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            /* QCOW_OFLAG_COPIED must be set iff refcount == 1 */
            if (check_copied) {
                refcount = get_refcount(bs, (l2_offset & ~QCOW_OFLAG_COPIED)
                    >> s->cluster_bits);
                if (refcount < 0) {
                    fprintf(stderr, "Can't get refcount for l2_offset %"
                        PRIx64 ": %s\n", l2_offset, strerror(-refcount));
                    goto fail;
                }
                if ((refcount == 1) != ((l2_offset & QCOW_OFLAG_COPIED) != 0)) {
                    fprintf(stderr, "ERROR OFLAG_COPIED: l2_offset=%" PRIx64
                        " refcount=%d\n", l2_offset, refcount);
                    res->corruptions++;
                }
            }

            /* Mark L2 table as used */
            l2_offset &= ~QCOW_OFLAG_COPIED;
            inc_refcounts(bs, res, refcount_table, refcount_table_size,
                l2_offset, s->cluster_size);

            /* L2 tables are cluster aligned */
            if (l2_offset & (s->cluster_size - 1)) {
                fprintf(stderr, "ERROR l2_offset=%" PRIx64 ": Table is not "
                    "cluster aligned; L1 entry corrupted\n", l2_offset);
                res->corruptions++;
            }

            /* Process and check L2 entries */
            ret = check_refcounts_l2(bs, res, refcount_table,
                refcount_table_size, l2_offset, check_copied);
            if (ret < 0) {
                goto fail;
            }
        }
    }
    qemu_free(l1_table);
    return 0;

fail:
    fprintf(stderr, "ERROR: I/O error in check_refcounts_l1\n");
    res->check_errors++;
    qemu_free(l1_table);
    return -EIO;
}

/*
 * Checks an image for refcount consistency.
 *
 * Returns 0 if no errors are found, the number of errors in case the image is
 * detected as corrupted, and -errno when an internal error occured.
 */
int qcow2_check_refcounts(BlockDriverState *bs, BdrvCheckResult *res)
{
    BDRVQcowState *s = bs->opaque;
    int64_t size;
    int nb_clusters, refcount1, refcount2, i;
    QCowSnapshot *sn;
    uint16_t *refcount_table;
    int ret;

    size = bdrv_getlength(bs->file);
    nb_clusters = size_to_clusters(s, size);
    refcount_table = qemu_mallocz(nb_clusters * sizeof(uint16_t));

    /* header */
    inc_refcounts(bs, res, refcount_table, nb_clusters,
        0, s->cluster_size);

    /* current L1 table */
    ret = check_refcounts_l1(bs, res, refcount_table, nb_clusters,
                       s->l1_table_offset, s->l1_size, 1);
    if (ret < 0) {
        return ret;
    }

    /* snapshots */
    for(i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        ret = check_refcounts_l1(bs, res, refcount_table, nb_clusters,
            sn->l1_table_offset, sn->l1_size, 0);
        if (ret < 0) {
            return ret;
        }
    }
    inc_refcounts(bs, res, refcount_table, nb_clusters,
        s->snapshots_offset, s->snapshots_size);

    /* refcount data */
    inc_refcounts(bs, res, refcount_table, nb_clusters,
        s->refcount_table_offset,
        s->refcount_table_size * sizeof(uint64_t));

    for(i = 0; i < s->refcount_table_size; i++) {
        uint64_t offset, cluster;
        offset = s->refcount_table[i];
        cluster = offset >> s->cluster_bits;

        /* Refcount blocks are cluster aligned */
        if (offset & (s->cluster_size - 1)) {
            fprintf(stderr, "ERROR refcount block %d is not "
                "cluster aligned; refcount table entry corrupted\n", i);
            res->corruptions++;
            continue;
        }

        if (cluster >= nb_clusters) {
            fprintf(stderr, "ERROR refcount block %d is outside image\n", i);
            res->corruptions++;
            continue;
        }

        if (offset != 0) {
            inc_refcounts(bs, res, refcount_table, nb_clusters,
                offset, s->cluster_size);
            if (refcount_table[cluster] != 1) {
                fprintf(stderr, "ERROR refcount block %d refcount=%d\n",
                    i, refcount_table[cluster]);
                res->corruptions++;
            }
        }
    }

    /* compare ref counts */
    for(i = 0; i < nb_clusters; i++) {
        refcount1 = get_refcount(bs, i);
        if (refcount1 < 0) {
            fprintf(stderr, "Can't get refcount for cluster %d: %s\n",
                i, strerror(-refcount1));
            res->check_errors++;
            continue;
        }

        refcount2 = refcount_table[i];
        if (refcount1 != refcount2) {
            fprintf(stderr, "%s cluster %d refcount=%d reference=%d\n",
                   refcount1 < refcount2 ? "ERROR" : "Leaked",
                   i, refcount1, refcount2);
            if (refcount1 < refcount2) {
                res->corruptions++;
            } else {
                res->leaks++;
            }
        }
    }

    qemu_free(refcount_table);

    return 0;
}

