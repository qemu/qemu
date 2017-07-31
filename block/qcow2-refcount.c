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
#include "qapi/error.h"
#include "qemu-common.h"
#include "block/block_int.h"
#include "block/qcow2.h"
#include "qemu/range.h"
#include "qemu/bswap.h"

static int64_t alloc_clusters_noref(BlockDriverState *bs, uint64_t size);
static int QEMU_WARN_UNUSED_RESULT update_refcount(BlockDriverState *bs,
                            int64_t offset, int64_t length, uint64_t addend,
                            bool decrease, enum qcow2_discard_type type);

static uint64_t get_refcount_ro0(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro1(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro2(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro3(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro4(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro5(const void *refcount_array, uint64_t index);
static uint64_t get_refcount_ro6(const void *refcount_array, uint64_t index);

static void set_refcount_ro0(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro1(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro2(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro3(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro4(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro5(void *refcount_array, uint64_t index,
                             uint64_t value);
static void set_refcount_ro6(void *refcount_array, uint64_t index,
                             uint64_t value);


static Qcow2GetRefcountFunc *const get_refcount_funcs[] = {
    &get_refcount_ro0,
    &get_refcount_ro1,
    &get_refcount_ro2,
    &get_refcount_ro3,
    &get_refcount_ro4,
    &get_refcount_ro5,
    &get_refcount_ro6
};

static Qcow2SetRefcountFunc *const set_refcount_funcs[] = {
    &set_refcount_ro0,
    &set_refcount_ro1,
    &set_refcount_ro2,
    &set_refcount_ro3,
    &set_refcount_ro4,
    &set_refcount_ro5,
    &set_refcount_ro6
};


/*********************************************************/
/* refcount handling */

static void update_max_refcount_table_index(BDRVQcow2State *s)
{
    unsigned i = s->refcount_table_size - 1;
    while (i > 0 && (s->refcount_table[i] & REFT_OFFSET_MASK) == 0) {
        i--;
    }
    /* Set s->max_refcount_table_index to the index of the last used entry */
    s->max_refcount_table_index = i;
}

int qcow2_refcount_init(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int refcount_table_size2, i;
    int ret;

    assert(s->refcount_order >= 0 && s->refcount_order <= 6);

    s->get_refcount = get_refcount_funcs[s->refcount_order];
    s->set_refcount = set_refcount_funcs[s->refcount_order];

    assert(s->refcount_table_size <= INT_MAX / sizeof(uint64_t));
    refcount_table_size2 = s->refcount_table_size * sizeof(uint64_t);
    s->refcount_table = g_try_malloc(refcount_table_size2);

    if (s->refcount_table_size > 0) {
        if (s->refcount_table == NULL) {
            ret = -ENOMEM;
            goto fail;
        }
        BLKDBG_EVENT(bs->file, BLKDBG_REFTABLE_LOAD);
        ret = bdrv_pread(bs->file, s->refcount_table_offset,
                         s->refcount_table, refcount_table_size2);
        if (ret < 0) {
            goto fail;
        }
        for(i = 0; i < s->refcount_table_size; i++)
            be64_to_cpus(&s->refcount_table[i]);
        update_max_refcount_table_index(s);
    }
    return 0;
 fail:
    return ret;
}

void qcow2_refcount_close(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    g_free(s->refcount_table);
}


static uint64_t get_refcount_ro0(const void *refcount_array, uint64_t index)
{
    return (((const uint8_t *)refcount_array)[index / 8] >> (index % 8)) & 0x1;
}

static void set_refcount_ro0(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 1));
    ((uint8_t *)refcount_array)[index / 8] &= ~(0x1 << (index % 8));
    ((uint8_t *)refcount_array)[index / 8] |= value << (index % 8);
}

static uint64_t get_refcount_ro1(const void *refcount_array, uint64_t index)
{
    return (((const uint8_t *)refcount_array)[index / 4] >> (2 * (index % 4)))
           & 0x3;
}

static void set_refcount_ro1(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 2));
    ((uint8_t *)refcount_array)[index / 4] &= ~(0x3 << (2 * (index % 4)));
    ((uint8_t *)refcount_array)[index / 4] |= value << (2 * (index % 4));
}

static uint64_t get_refcount_ro2(const void *refcount_array, uint64_t index)
{
    return (((const uint8_t *)refcount_array)[index / 2] >> (4 * (index % 2)))
           & 0xf;
}

static void set_refcount_ro2(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 4));
    ((uint8_t *)refcount_array)[index / 2] &= ~(0xf << (4 * (index % 2)));
    ((uint8_t *)refcount_array)[index / 2] |= value << (4 * (index % 2));
}

static uint64_t get_refcount_ro3(const void *refcount_array, uint64_t index)
{
    return ((const uint8_t *)refcount_array)[index];
}

static void set_refcount_ro3(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 8));
    ((uint8_t *)refcount_array)[index] = value;
}

static uint64_t get_refcount_ro4(const void *refcount_array, uint64_t index)
{
    return be16_to_cpu(((const uint16_t *)refcount_array)[index]);
}

static void set_refcount_ro4(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 16));
    ((uint16_t *)refcount_array)[index] = cpu_to_be16(value);
}

static uint64_t get_refcount_ro5(const void *refcount_array, uint64_t index)
{
    return be32_to_cpu(((const uint32_t *)refcount_array)[index]);
}

static void set_refcount_ro5(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    assert(!(value >> 32));
    ((uint32_t *)refcount_array)[index] = cpu_to_be32(value);
}

static uint64_t get_refcount_ro6(const void *refcount_array, uint64_t index)
{
    return be64_to_cpu(((const uint64_t *)refcount_array)[index]);
}

static void set_refcount_ro6(void *refcount_array, uint64_t index,
                             uint64_t value)
{
    ((uint64_t *)refcount_array)[index] = cpu_to_be64(value);
}


static int load_refcount_block(BlockDriverState *bs,
                               int64_t refcount_block_offset,
                               void **refcount_block)
{
    BDRVQcow2State *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_LOAD);
    return qcow2_cache_get(bs, s->refcount_block_cache, refcount_block_offset,
                           refcount_block);
}

/*
 * Retrieves the refcount of the cluster given by its index and stores it in
 * *refcount. Returns 0 on success and -errno on failure.
 */
int qcow2_get_refcount(BlockDriverState *bs, int64_t cluster_index,
                       uint64_t *refcount)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t refcount_table_index, block_index;
    int64_t refcount_block_offset;
    int ret;
    void *refcount_block;

    refcount_table_index = cluster_index >> s->refcount_block_bits;
    if (refcount_table_index >= s->refcount_table_size) {
        *refcount = 0;
        return 0;
    }
    refcount_block_offset =
        s->refcount_table[refcount_table_index] & REFT_OFFSET_MASK;
    if (!refcount_block_offset) {
        *refcount = 0;
        return 0;
    }

    if (offset_into_cluster(s, refcount_block_offset)) {
        qcow2_signal_corruption(bs, true, -1, -1, "Refblock offset %#" PRIx64
                                " unaligned (reftable index: %#" PRIx64 ")",
                                refcount_block_offset, refcount_table_index);
        return -EIO;
    }

    ret = qcow2_cache_get(bs, s->refcount_block_cache, refcount_block_offset,
                          &refcount_block);
    if (ret < 0) {
        return ret;
    }

    block_index = cluster_index & (s->refcount_block_size - 1);
    *refcount = s->get_refcount(refcount_block, block_index);

    qcow2_cache_put(bs, s->refcount_block_cache, &refcount_block);

    return 0;
}

/* Checks if two offsets are described by the same refcount block */
static int in_same_refcount_block(BDRVQcow2State *s, uint64_t offset_a,
    uint64_t offset_b)
{
    uint64_t block_a = offset_a >> (s->cluster_bits + s->refcount_block_bits);
    uint64_t block_b = offset_b >> (s->cluster_bits + s->refcount_block_bits);

    return (block_a == block_b);
}

/*
 * Loads a refcount block. If it doesn't exist yet, it is allocated first
 * (including growing the refcount table if needed).
 *
 * Returns 0 on success or -errno in error case
 */
static int alloc_refcount_block(BlockDriverState *bs,
                                int64_t cluster_index, void **refcount_block)
{
    BDRVQcow2State *s = bs->opaque;
    unsigned int refcount_table_index;
    int64_t ret;

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC);

    /* Find the refcount block for the given cluster */
    refcount_table_index = cluster_index >> s->refcount_block_bits;

    if (refcount_table_index < s->refcount_table_size) {

        uint64_t refcount_block_offset =
            s->refcount_table[refcount_table_index] & REFT_OFFSET_MASK;

        /* If it's already there, we're done */
        if (refcount_block_offset) {
            if (offset_into_cluster(s, refcount_block_offset)) {
                qcow2_signal_corruption(bs, true, -1, -1, "Refblock offset %#"
                                        PRIx64 " unaligned (reftable index: "
                                        "%#x)", refcount_block_offset,
                                        refcount_table_index);
                return -EIO;
            }

             return load_refcount_block(bs, refcount_block_offset,
                                        refcount_block);
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
     *   and potentially doing an initial refcount increase. This means that
     *   some clusters have already been allocated by the caller, but their
     *   refcount isn't accurate yet. If we allocate clusters for metadata, we
     *   need to return -EAGAIN to signal the caller that it needs to restart
     *   the search for free clusters.
     *
     * - alloc_clusters_noref and qcow2_free_clusters may load a different
     *   refcount block into the cache
     */

    *refcount_block = NULL;

    /* We write to the refcount table, so we might depend on L2 tables */
    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        return ret;
    }

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
                                    refcount_block);
        if (ret < 0) {
            goto fail;
        }

        memset(*refcount_block, 0, s->cluster_size);

        /* The block describes itself, need to update the cache */
        int block_index = (new_block >> s->cluster_bits) &
            (s->refcount_block_size - 1);
        s->set_refcount(*refcount_block, block_index, 1);
    } else {
        /* Described somewhere else. This can recurse at most twice before we
         * arrive at a block that describes itself. */
        ret = update_refcount(bs, new_block, s->cluster_size, 1, false,
                              QCOW2_DISCARD_NEVER);
        if (ret < 0) {
            goto fail;
        }

        ret = qcow2_cache_flush(bs, s->refcount_block_cache);
        if (ret < 0) {
            goto fail;
        }

        /* Initialize the new refcount block only after updating its refcount,
         * update_refcount uses the refcount cache itself */
        ret = qcow2_cache_get_empty(bs, s->refcount_block_cache, new_block,
                                    refcount_block);
        if (ret < 0) {
            goto fail;
        }

        memset(*refcount_block, 0, s->cluster_size);
    }

    /* Now the new refcount block needs to be written to disk */
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE);
    qcow2_cache_entry_mark_dirty(bs, s->refcount_block_cache, *refcount_block);
    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* If the refcount table is big enough, just hook the block up there */
    if (refcount_table_index < s->refcount_table_size) {
        uint64_t data64 = cpu_to_be64(new_block);
        BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_HOOKUP);
        ret = bdrv_pwrite_sync(bs->file,
            s->refcount_table_offset + refcount_table_index * sizeof(uint64_t),
            &data64, sizeof(data64));
        if (ret < 0) {
            goto fail;
        }

        s->refcount_table[refcount_table_index] = new_block;
        /* If there's a hole in s->refcount_table then it can happen
         * that refcount_table_index < s->max_refcount_table_index */
        s->max_refcount_table_index =
            MAX(s->max_refcount_table_index, refcount_table_index);

        /* The new refcount block may be where the caller intended to put its
         * data, so let it restart the search. */
        return -EAGAIN;
    }

    qcow2_cache_put(bs, s->refcount_block_cache, refcount_block);

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

    /* Calculate the number of refcount blocks needed so far; this will be the
     * basis for calculating the index of the first cluster used for the
     * self-describing refcount structures which we are about to create.
     *
     * Because we reached this point, there cannot be any refcount entries for
     * cluster_index or higher indices yet. However, because new_block has been
     * allocated to describe that cluster (and it will assume this role later
     * on), we cannot use that index; also, new_block may actually have a higher
     * cluster index than cluster_index, so it needs to be taken into account
     * here (and 1 needs to be added to its value because that cluster is used).
     */
    uint64_t blocks_used = DIV_ROUND_UP(MAX(cluster_index + 1,
                                            (new_block >> s->cluster_bits) + 1),
                                        s->refcount_block_size);

    /* Create the new refcount table and blocks */
    uint64_t meta_offset = (blocks_used * s->refcount_block_size) *
        s->cluster_size;

    ret = qcow2_refcount_area(bs, meta_offset, 0, false,
                              refcount_table_index, new_block);
    if (ret < 0) {
        return ret;
    }

    ret = load_refcount_block(bs, new_block, refcount_block);
    if (ret < 0) {
        return ret;
    }

    /* If we were trying to do the initial refcount update for some cluster
     * allocation, we might have used the same clusters to store newly
     * allocated metadata. Make the caller search some new space. */
    return -EAGAIN;

fail:
    if (*refcount_block != NULL) {
        qcow2_cache_put(bs, s->refcount_block_cache, refcount_block);
    }
    return ret;
}

/*
 * Starting at @start_offset, this function creates new self-covering refcount
 * structures: A new refcount table and refcount blocks which cover all of
 * themselves, and a number of @additional_clusters beyond their end.
 * @start_offset must be at the end of the image file, that is, there must be
 * only empty space beyond it.
 * If @exact_size is false, the refcount table will have 50 % more entries than
 * necessary so it will not need to grow again soon.
 * If @new_refblock_offset is not zero, it contains the offset of a refcount
 * block that should be entered into the new refcount table at index
 * @new_refblock_index.
 *
 * Returns: The offset after the new refcount structures (i.e. where the
 *          @additional_clusters may be placed) on success, -errno on error.
 */
int64_t qcow2_refcount_area(BlockDriverState *bs, uint64_t start_offset,
                            uint64_t additional_clusters, bool exact_size,
                            int new_refblock_index,
                            uint64_t new_refblock_offset)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t total_refblock_count_u64, additional_refblock_count;
    int total_refblock_count, table_size, area_reftable_index, table_clusters;
    int i;
    uint64_t table_offset, block_offset, end_offset;
    int ret;
    uint64_t *new_table;

    assert(!(start_offset % s->cluster_size));

    qcow2_refcount_metadata_size(start_offset / s->cluster_size +
                                 additional_clusters,
                                 s->cluster_size, s->refcount_order,
                                 !exact_size, &total_refblock_count_u64);
    if (total_refblock_count_u64 > QCOW_MAX_REFTABLE_SIZE) {
        return -EFBIG;
    }
    total_refblock_count = total_refblock_count_u64;

    /* Index in the refcount table of the first refcount block to cover the area
     * of refcount structures we are about to create; we know that
     * @total_refblock_count can cover @start_offset, so this will definitely
     * fit into an int. */
    area_reftable_index = (start_offset / s->cluster_size) /
                          s->refcount_block_size;

    if (exact_size) {
        table_size = total_refblock_count;
    } else {
        table_size = total_refblock_count +
                     DIV_ROUND_UP(total_refblock_count, 2);
    }
    /* The qcow2 file can only store the reftable size in number of clusters */
    table_size = ROUND_UP(table_size, s->cluster_size / sizeof(uint64_t));
    table_clusters = (table_size * sizeof(uint64_t)) / s->cluster_size;

    if (table_size > QCOW_MAX_REFTABLE_SIZE) {
        return -EFBIG;
    }

    new_table = g_try_new0(uint64_t, table_size);

    assert(table_size > 0);
    if (new_table == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    /* Fill the new refcount table */
    if (table_size > s->max_refcount_table_index) {
        /* We're actually growing the reftable */
        memcpy(new_table, s->refcount_table,
               (s->max_refcount_table_index + 1) * sizeof(uint64_t));
    } else {
        /* Improbable case: We're shrinking the reftable. However, the caller
         * has assured us that there is only empty space beyond @start_offset,
         * so we can simply drop all of the refblocks that won't fit into the
         * new reftable. */
        memcpy(new_table, s->refcount_table, table_size * sizeof(uint64_t));
    }

    if (new_refblock_offset) {
        assert(new_refblock_index < total_refblock_count);
        new_table[new_refblock_index] = new_refblock_offset;
    }

    /* Count how many new refblocks we have to create */
    additional_refblock_count = 0;
    for (i = area_reftable_index; i < total_refblock_count; i++) {
        if (!new_table[i]) {
            additional_refblock_count++;
        }
    }

    table_offset = start_offset + additional_refblock_count * s->cluster_size;
    end_offset = table_offset + table_clusters * s->cluster_size;

    /* Fill the refcount blocks, and create new ones, if necessary */
    block_offset = start_offset;
    for (i = area_reftable_index; i < total_refblock_count; i++) {
        void *refblock_data;
        uint64_t first_offset_covered;

        /* Reuse an existing refblock if possible, create a new one otherwise */
        if (new_table[i]) {
            ret = qcow2_cache_get(bs, s->refcount_block_cache, new_table[i],
                                  &refblock_data);
            if (ret < 0) {
                goto fail;
            }
        } else {
            ret = qcow2_cache_get_empty(bs, s->refcount_block_cache,
                                        block_offset, &refblock_data);
            if (ret < 0) {
                goto fail;
            }
            memset(refblock_data, 0, s->cluster_size);
            qcow2_cache_entry_mark_dirty(bs, s->refcount_block_cache,
                                         refblock_data);

            new_table[i] = block_offset;
            block_offset += s->cluster_size;
        }

        /* First host offset covered by this refblock */
        first_offset_covered = (uint64_t)i * s->refcount_block_size *
                               s->cluster_size;
        if (first_offset_covered < end_offset) {
            int j, end_index;

            /* Set the refcount of all of the new refcount structures to 1 */

            if (first_offset_covered < start_offset) {
                assert(i == area_reftable_index);
                j = (start_offset - first_offset_covered) / s->cluster_size;
                assert(j < s->refcount_block_size);
            } else {
                j = 0;
            }

            end_index = MIN((end_offset - first_offset_covered) /
                            s->cluster_size,
                            s->refcount_block_size);

            for (; j < end_index; j++) {
                /* The caller guaranteed us this space would be empty */
                assert(s->get_refcount(refblock_data, j) == 0);
                s->set_refcount(refblock_data, j, 1);
            }

            qcow2_cache_entry_mark_dirty(bs, s->refcount_block_cache,
                                         refblock_data);
        }

        qcow2_cache_put(bs, s->refcount_block_cache, &refblock_data);
    }

    assert(block_offset == table_offset);

    /* Write refcount blocks to disk */
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE_BLOCKS);
    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        goto fail;
    }

    /* Write refcount table to disk */
    for (i = 0; i < total_refblock_count; i++) {
        cpu_to_be64s(&new_table[i]);
    }

    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_WRITE_TABLE);
    ret = bdrv_pwrite_sync(bs->file, table_offset, new_table,
        table_size * sizeof(uint64_t));
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < total_refblock_count; i++) {
        be64_to_cpus(&new_table[i]);
    }

    /* Hook up the new refcount table in the qcow2 header */
    struct QEMU_PACKED {
        uint64_t d64;
        uint32_t d32;
    } data;
    data.d64 = cpu_to_be64(table_offset);
    data.d32 = cpu_to_be32(table_clusters);
    BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_ALLOC_SWITCH_TABLE);
    ret = bdrv_pwrite_sync(bs->file,
                           offsetof(QCowHeader, refcount_table_offset),
                           &data, sizeof(data));
    if (ret < 0) {
        goto fail;
    }

    /* And switch it in memory */
    uint64_t old_table_offset = s->refcount_table_offset;
    uint64_t old_table_size = s->refcount_table_size;

    g_free(s->refcount_table);
    s->refcount_table = new_table;
    s->refcount_table_size = table_size;
    s->refcount_table_offset = table_offset;
    update_max_refcount_table_index(s);

    /* Free old table. */
    qcow2_free_clusters(bs, old_table_offset, old_table_size * sizeof(uint64_t),
                        QCOW2_DISCARD_OTHER);

    return end_offset;

fail:
    g_free(new_table);
    return ret;
}

void qcow2_process_discards(BlockDriverState *bs, int ret)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2DiscardRegion *d, *next;

    QTAILQ_FOREACH_SAFE(d, &s->discards, next, next) {
        QTAILQ_REMOVE(&s->discards, d, next);

        /* Discard is optional, ignore the return value */
        if (ret >= 0) {
            bdrv_pdiscard(bs->file->bs, d->offset, d->bytes);
        }

        g_free(d);
    }
}

static void update_refcount_discard(BlockDriverState *bs,
                                    uint64_t offset, uint64_t length)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2DiscardRegion *d, *p, *next;

    QTAILQ_FOREACH(d, &s->discards, next) {
        uint64_t new_start = MIN(offset, d->offset);
        uint64_t new_end = MAX(offset + length, d->offset + d->bytes);

        if (new_end - new_start <= length + d->bytes) {
            /* There can't be any overlap, areas ending up here have no
             * references any more and therefore shouldn't get freed another
             * time. */
            assert(d->bytes + length == new_end - new_start);
            d->offset = new_start;
            d->bytes = new_end - new_start;
            goto found;
        }
    }

    d = g_malloc(sizeof(*d));
    *d = (Qcow2DiscardRegion) {
        .bs     = bs,
        .offset = offset,
        .bytes  = length,
    };
    QTAILQ_INSERT_TAIL(&s->discards, d, next);

found:
    /* Merge discard requests if they are adjacent now */
    QTAILQ_FOREACH_SAFE(p, &s->discards, next, next) {
        if (p == d
            || p->offset > d->offset + d->bytes
            || d->offset > p->offset + p->bytes)
        {
            continue;
        }

        /* Still no overlap possible */
        assert(p->offset == d->offset + d->bytes
            || d->offset == p->offset + p->bytes);

        QTAILQ_REMOVE(&s->discards, p, next);
        d->offset = MIN(d->offset, p->offset);
        d->bytes += p->bytes;
        g_free(p);
    }
}

/* XXX: cache several refcount block clusters ? */
/* @addend is the absolute value of the addend; if @decrease is set, @addend
 * will be subtracted from the current refcount, otherwise it will be added */
static int QEMU_WARN_UNUSED_RESULT update_refcount(BlockDriverState *bs,
                                                   int64_t offset,
                                                   int64_t length,
                                                   uint64_t addend,
                                                   bool decrease,
                                                   enum qcow2_discard_type type)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t start, last, cluster_offset;
    void *refcount_block = NULL;
    int64_t old_table_index = -1;
    int ret;

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "update_refcount: offset=%" PRId64 " size=%" PRId64
            " addend=%s%" PRIu64 "\n", offset, length, decrease ? "-" : "",
            addend);
#endif
    if (length < 0) {
        return -EINVAL;
    } else if (length == 0) {
        return 0;
    }

    if (decrease) {
        qcow2_cache_set_dependency(bs, s->refcount_block_cache,
            s->l2_table_cache);
    }

    start = start_of_cluster(s, offset);
    last = start_of_cluster(s, offset + length - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size)
    {
        int block_index;
        uint64_t refcount;
        int64_t cluster_index = cluster_offset >> s->cluster_bits;
        int64_t table_index = cluster_index >> s->refcount_block_bits;

        /* Load the refcount block and allocate it if needed */
        if (table_index != old_table_index) {
            if (refcount_block) {
                qcow2_cache_put(bs, s->refcount_block_cache, &refcount_block);
            }
            ret = alloc_refcount_block(bs, cluster_index, &refcount_block);
            if (ret < 0) {
                goto fail;
            }
        }
        old_table_index = table_index;

        qcow2_cache_entry_mark_dirty(bs, s->refcount_block_cache,
                                     refcount_block);

        /* we can update the count and save it */
        block_index = cluster_index & (s->refcount_block_size - 1);

        refcount = s->get_refcount(refcount_block, block_index);
        if (decrease ? (refcount - addend > refcount)
                     : (refcount + addend < refcount ||
                        refcount + addend > s->refcount_max))
        {
            ret = -EINVAL;
            goto fail;
        }
        if (decrease) {
            refcount -= addend;
        } else {
            refcount += addend;
        }
        if (refcount == 0 && cluster_index < s->free_cluster_index) {
            s->free_cluster_index = cluster_index;
        }
        s->set_refcount(refcount_block, block_index, refcount);

        if (refcount == 0 && s->discard_passthrough[type]) {
            update_refcount_discard(bs, cluster_offset, s->cluster_size);
        }
    }

    ret = 0;
fail:
    if (!s->cache_discards) {
        qcow2_process_discards(bs, ret);
    }

    /* Write last changed block to disk */
    if (refcount_block) {
        qcow2_cache_put(bs, s->refcount_block_cache, &refcount_block);
    }

    /*
     * Try do undo any updates if an error is returned (This may succeed in
     * some cases like ENOSPC for allocating a new refcount block)
     */
    if (ret < 0) {
        int dummy;
        dummy = update_refcount(bs, offset, cluster_offset - offset, addend,
                                !decrease, QCOW2_DISCARD_NEVER);
        (void)dummy;
    }

    return ret;
}

/*
 * Increases or decreases the refcount of a given cluster.
 *
 * @addend is the absolute value of the addend; if @decrease is set, @addend
 * will be subtracted from the current refcount, otherwise it will be added.
 *
 * On success 0 is returned; on failure -errno is returned.
 */
int qcow2_update_cluster_refcount(BlockDriverState *bs,
                                  int64_t cluster_index,
                                  uint64_t addend, bool decrease,
                                  enum qcow2_discard_type type)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    ret = update_refcount(bs, cluster_index << s->cluster_bits, 1, addend,
                          decrease, type);
    if (ret < 0) {
        return ret;
    }

    return 0;
}



/*********************************************************/
/* cluster allocation functions */



/* return < 0 if error */
static int64_t alloc_clusters_noref(BlockDriverState *bs, uint64_t size)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t i, nb_clusters, refcount;
    int ret;

    /* We can't allocate clusters if they may still be queued for discard. */
    if (s->cache_discards) {
        qcow2_process_discards(bs, 0);
    }

    nb_clusters = size_to_clusters(s, size);
retry:
    for(i = 0; i < nb_clusters; i++) {
        uint64_t next_cluster_index = s->free_cluster_index++;
        ret = qcow2_get_refcount(bs, next_cluster_index, &refcount);

        if (ret < 0) {
            return ret;
        } else if (refcount != 0) {
            goto retry;
        }
    }

    /* Make sure that all offsets in the "allocated" range are representable
     * in an int64_t */
    if (s->free_cluster_index > 0 &&
        s->free_cluster_index - 1 > (INT64_MAX >> s->cluster_bits))
    {
        return -EFBIG;
    }

#ifdef DEBUG_ALLOC2
    fprintf(stderr, "alloc_clusters: size=%" PRId64 " -> %" PRId64 "\n",
            size,
            (s->free_cluster_index - nb_clusters) << s->cluster_bits);
#endif
    return (s->free_cluster_index - nb_clusters) << s->cluster_bits;
}

int64_t qcow2_alloc_clusters(BlockDriverState *bs, uint64_t size)
{
    int64_t offset;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC);
    do {
        offset = alloc_clusters_noref(bs, size);
        if (offset < 0) {
            return offset;
        }

        ret = update_refcount(bs, offset, size, 1, false, QCOW2_DISCARD_NEVER);
    } while (ret == -EAGAIN);

    if (ret < 0) {
        return ret;
    }

    return offset;
}

int64_t qcow2_alloc_clusters_at(BlockDriverState *bs, uint64_t offset,
                                int64_t nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t cluster_index, refcount;
    uint64_t i;
    int ret;

    assert(nb_clusters >= 0);
    if (nb_clusters == 0) {
        return 0;
    }

    do {
        /* Check how many clusters there are free */
        cluster_index = offset >> s->cluster_bits;
        for(i = 0; i < nb_clusters; i++) {
            ret = qcow2_get_refcount(bs, cluster_index++, &refcount);
            if (ret < 0) {
                return ret;
            } else if (refcount != 0) {
                break;
            }
        }

        /* And then allocate them */
        ret = update_refcount(bs, offset, i << s->cluster_bits, 1, false,
                              QCOW2_DISCARD_NEVER);
    } while (ret == -EAGAIN);

    if (ret < 0) {
        return ret;
    }

    return i;
}

/* only used to allocate compressed sectors. We try to allocate
   contiguous sectors. size must be <= cluster_size */
int64_t qcow2_alloc_bytes(BlockDriverState *bs, int size)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t offset;
    size_t free_in_cluster;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_ALLOC_BYTES);
    assert(size > 0 && size <= s->cluster_size);
    assert(!s->free_byte_offset || offset_into_cluster(s, s->free_byte_offset));

    offset = s->free_byte_offset;

    if (offset) {
        uint64_t refcount;
        ret = qcow2_get_refcount(bs, offset >> s->cluster_bits, &refcount);
        if (ret < 0) {
            return ret;
        }

        if (refcount == s->refcount_max) {
            offset = 0;
        }
    }

    free_in_cluster = s->cluster_size - offset_into_cluster(s, offset);
    do {
        if (!offset || free_in_cluster < size) {
            int64_t new_cluster = alloc_clusters_noref(bs, s->cluster_size);
            if (new_cluster < 0) {
                return new_cluster;
            }

            if (!offset || ROUND_UP(offset, s->cluster_size) != new_cluster) {
                offset = new_cluster;
                free_in_cluster = s->cluster_size;
            } else {
                free_in_cluster += s->cluster_size;
            }
        }

        assert(offset);
        ret = update_refcount(bs, offset, size, 1, false, QCOW2_DISCARD_NEVER);
        if (ret < 0) {
            offset = 0;
        }
    } while (ret == -EAGAIN);
    if (ret < 0) {
        return ret;
    }

    /* The cluster refcount was incremented; refcount blocks must be flushed
     * before the caller's L2 table updates. */
    qcow2_cache_set_dependency(bs, s->l2_table_cache, s->refcount_block_cache);

    s->free_byte_offset = offset + size;
    if (!offset_into_cluster(s, s->free_byte_offset)) {
        s->free_byte_offset = 0;
    }

    return offset;
}

void qcow2_free_clusters(BlockDriverState *bs,
                          int64_t offset, int64_t size,
                          enum qcow2_discard_type type)
{
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_CLUSTER_FREE);
    ret = update_refcount(bs, offset, size, 1, true, type);
    if (ret < 0) {
        fprintf(stderr, "qcow2_free_clusters failed: %s\n", strerror(-ret));
        /* TODO Remember the clusters to free them later and avoid leaking */
    }
}

/*
 * Free a cluster using its L2 entry (handles clusters of all types, e.g.
 * normal cluster, compressed cluster, etc.)
 */
void qcow2_free_any_clusters(BlockDriverState *bs, uint64_t l2_entry,
                             int nb_clusters, enum qcow2_discard_type type)
{
    BDRVQcow2State *s = bs->opaque;

    switch (qcow2_get_cluster_type(l2_entry)) {
    case QCOW2_CLUSTER_COMPRESSED:
        {
            int nb_csectors;
            nb_csectors = ((l2_entry >> s->csize_shift) &
                           s->csize_mask) + 1;
            qcow2_free_clusters(bs,
                (l2_entry & s->cluster_offset_mask) & ~511,
                nb_csectors * 512, type);
        }
        break;
    case QCOW2_CLUSTER_NORMAL:
    case QCOW2_CLUSTER_ZERO_ALLOC:
        if (offset_into_cluster(s, l2_entry & L2E_OFFSET_MASK)) {
            qcow2_signal_corruption(bs, false, -1, -1,
                                    "Cannot free unaligned cluster %#llx",
                                    l2_entry & L2E_OFFSET_MASK);
        } else {
            qcow2_free_clusters(bs, l2_entry & L2E_OFFSET_MASK,
                                nb_clusters << s->cluster_bits, type);
        }
        break;
    case QCOW2_CLUSTER_ZERO_PLAIN:
    case QCOW2_CLUSTER_UNALLOCATED:
        break;
    default:
        abort();
    }
}



/*********************************************************/
/* snapshots and image creation */



/* update the refcounts of snapshots and the copied flag */
int qcow2_update_snapshot_refcount(BlockDriverState *bs,
    int64_t l1_table_offset, int l1_size, int addend)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l1_table, *l2_table, l2_offset, entry, l1_size2, refcount;
    bool l1_allocated = false;
    int64_t old_entry, old_l2_offset;
    int i, j, l1_modified = 0, nb_csectors;
    int ret;

    assert(addend >= -1 && addend <= 1);

    l2_table = NULL;
    l1_table = NULL;
    l1_size2 = l1_size * sizeof(uint64_t);

    s->cache_discards = true;

    /* WARNING: qcow2_snapshot_goto relies on this function not using the
     * l1_table_offset when it is the current s->l1_table_offset! Be careful
     * when changing this! */
    if (l1_table_offset != s->l1_table_offset) {
        l1_table = g_try_malloc0(align_offset(l1_size2, 512));
        if (l1_size2 && l1_table == NULL) {
            ret = -ENOMEM;
            goto fail;
        }
        l1_allocated = true;

        ret = bdrv_pread(bs->file, l1_table_offset, l1_table, l1_size2);
        if (ret < 0) {
            goto fail;
        }

        for (i = 0; i < l1_size; i++) {
            be64_to_cpus(&l1_table[i]);
        }
    } else {
        assert(l1_size == s->l1_size);
        l1_table = s->l1_table;
        l1_allocated = false;
    }

    for (i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            old_l2_offset = l2_offset;
            l2_offset &= L1E_OFFSET_MASK;

            if (offset_into_cluster(s, l2_offset)) {
                qcow2_signal_corruption(bs, true, -1, -1, "L2 table offset %#"
                                        PRIx64 " unaligned (L1 index: %#x)",
                                        l2_offset, i);
                ret = -EIO;
                goto fail;
            }

            ret = qcow2_cache_get(bs, s->l2_table_cache, l2_offset,
                (void**) &l2_table);
            if (ret < 0) {
                goto fail;
            }

            for (j = 0; j < s->l2_size; j++) {
                uint64_t cluster_index;
                uint64_t offset;

                entry = be64_to_cpu(l2_table[j]);
                old_entry = entry;
                entry &= ~QCOW_OFLAG_COPIED;
                offset = entry & L2E_OFFSET_MASK;

                switch (qcow2_get_cluster_type(entry)) {
                case QCOW2_CLUSTER_COMPRESSED:
                    nb_csectors = ((entry >> s->csize_shift) &
                                   s->csize_mask) + 1;
                    if (addend != 0) {
                        ret = update_refcount(bs,
                                (entry & s->cluster_offset_mask) & ~511,
                                nb_csectors * 512, abs(addend), addend < 0,
                                QCOW2_DISCARD_SNAPSHOT);
                        if (ret < 0) {
                            goto fail;
                        }
                    }
                    /* compressed clusters are never modified */
                    refcount = 2;
                    break;

                case QCOW2_CLUSTER_NORMAL:
                case QCOW2_CLUSTER_ZERO_ALLOC:
                    if (offset_into_cluster(s, offset)) {
                        qcow2_signal_corruption(bs, true, -1, -1, "Cluster "
                                                "allocation offset %#" PRIx64
                                                " unaligned (L2 offset: %#"
                                                PRIx64 ", L2 index: %#x)",
                                                offset, l2_offset, j);
                        ret = -EIO;
                        goto fail;
                    }

                    cluster_index = offset >> s->cluster_bits;
                    assert(cluster_index);
                    if (addend != 0) {
                        ret = qcow2_update_cluster_refcount(bs,
                                    cluster_index, abs(addend), addend < 0,
                                    QCOW2_DISCARD_SNAPSHOT);
                        if (ret < 0) {
                            goto fail;
                        }
                    }

                    ret = qcow2_get_refcount(bs, cluster_index, &refcount);
                    if (ret < 0) {
                        goto fail;
                    }
                    break;

                case QCOW2_CLUSTER_ZERO_PLAIN:
                case QCOW2_CLUSTER_UNALLOCATED:
                    refcount = 0;
                    break;

                default:
                    abort();
                }

                if (refcount == 1) {
                    entry |= QCOW_OFLAG_COPIED;
                }
                if (entry != old_entry) {
                    if (addend > 0) {
                        qcow2_cache_set_dependency(bs, s->l2_table_cache,
                            s->refcount_block_cache);
                    }
                    l2_table[j] = cpu_to_be64(entry);
                    qcow2_cache_entry_mark_dirty(bs, s->l2_table_cache,
                                                 l2_table);
                }
            }

            qcow2_cache_put(bs, s->l2_table_cache, (void **) &l2_table);

            if (addend != 0) {
                ret = qcow2_update_cluster_refcount(bs, l2_offset >>
                                                        s->cluster_bits,
                                                    abs(addend), addend < 0,
                                                    QCOW2_DISCARD_SNAPSHOT);
                if (ret < 0) {
                    goto fail;
                }
            }
            ret = qcow2_get_refcount(bs, l2_offset >> s->cluster_bits,
                                     &refcount);
            if (ret < 0) {
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

    ret = bdrv_flush(bs);
fail:
    if (l2_table) {
        qcow2_cache_put(bs, s->l2_table_cache, (void**) &l2_table);
    }

    s->cache_discards = false;
    qcow2_process_discards(bs, ret);

    /* Update L1 only if it isn't deleted anyway (addend = -1) */
    if (ret == 0 && addend >= 0 && l1_modified) {
        for (i = 0; i < l1_size; i++) {
            cpu_to_be64s(&l1_table[i]);
        }

        ret = bdrv_pwrite_sync(bs->file, l1_table_offset,
                               l1_table, l1_size2);

        for (i = 0; i < l1_size; i++) {
            be64_to_cpus(&l1_table[i]);
        }
    }
    if (l1_allocated)
        g_free(l1_table);
    return ret;
}




/*********************************************************/
/* refcount checking functions */


static uint64_t refcount_array_byte_size(BDRVQcow2State *s, uint64_t entries)
{
    /* This assertion holds because there is no way we can address more than
     * 2^(64 - 9) clusters at once (with cluster size 512 = 2^9, and because
     * offsets have to be representable in bytes); due to every cluster
     * corresponding to one refcount entry, we are well below that limit */
    assert(entries < (UINT64_C(1) << (64 - 9)));

    /* Thanks to the assertion this will not overflow, because
     * s->refcount_order < 7.
     * (note: x << s->refcount_order == x * s->refcount_bits) */
    return DIV_ROUND_UP(entries << s->refcount_order, 8);
}

/**
 * Reallocates *array so that it can hold new_size entries. *size must contain
 * the current number of entries in *array. If the reallocation fails, *array
 * and *size will not be modified and -errno will be returned. If the
 * reallocation is successful, *array will be set to the new buffer, *size
 * will be set to new_size and 0 will be returned. The size of the reallocated
 * refcount array buffer will be aligned to a cluster boundary, and the newly
 * allocated area will be zeroed.
 */
static int realloc_refcount_array(BDRVQcow2State *s, void **array,
                                  int64_t *size, int64_t new_size)
{
    int64_t old_byte_size, new_byte_size;
    void *new_ptr;

    /* Round to clusters so the array can be directly written to disk */
    old_byte_size = size_to_clusters(s, refcount_array_byte_size(s, *size))
                    * s->cluster_size;
    new_byte_size = size_to_clusters(s, refcount_array_byte_size(s, new_size))
                    * s->cluster_size;

    if (new_byte_size == old_byte_size) {
        *size = new_size;
        return 0;
    }

    assert(new_byte_size > 0);

    if (new_byte_size > SIZE_MAX) {
        return -ENOMEM;
    }

    new_ptr = g_try_realloc(*array, new_byte_size);
    if (!new_ptr) {
        return -ENOMEM;
    }

    if (new_byte_size > old_byte_size) {
        memset((char *)new_ptr + old_byte_size, 0,
               new_byte_size - old_byte_size);
    }

    *array = new_ptr;
    *size  = new_size;

    return 0;
}

/*
 * Increases the refcount for a range of clusters in a given refcount table.
 * This is used to construct a temporary refcount table out of L1 and L2 tables
 * which can be compared to the refcount table saved in the image.
 *
 * Modifies the number of errors in res.
 */
int qcow2_inc_refcounts_imrt(BlockDriverState *bs, BdrvCheckResult *res,
                             void **refcount_table,
                             int64_t *refcount_table_size,
                             int64_t offset, int64_t size)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t start, last, cluster_offset, k, refcount;
    int ret;

    if (size <= 0) {
        return 0;
    }

    start = start_of_cluster(s, offset);
    last = start_of_cluster(s, offset + size - 1);
    for(cluster_offset = start; cluster_offset <= last;
        cluster_offset += s->cluster_size) {
        k = cluster_offset >> s->cluster_bits;
        if (k >= *refcount_table_size) {
            ret = realloc_refcount_array(s, refcount_table,
                                         refcount_table_size, k + 1);
            if (ret < 0) {
                res->check_errors++;
                return ret;
            }
        }

        refcount = s->get_refcount(*refcount_table, k);
        if (refcount == s->refcount_max) {
            fprintf(stderr, "ERROR: overflow cluster offset=0x%" PRIx64
                    "\n", cluster_offset);
            fprintf(stderr, "Use qemu-img amend to increase the refcount entry "
                    "width or qemu-img convert to create a clean copy if the "
                    "image cannot be opened for writing\n");
            res->corruptions++;
            continue;
        }
        s->set_refcount(*refcount_table, k, refcount + 1);
    }

    return 0;
}

/* Flags for check_refcounts_l1() and check_refcounts_l2() */
enum {
    CHECK_FRAG_INFO = 0x2,      /* update BlockFragInfo counters */
};

/*
 * Increases the refcount in the given refcount table for the all clusters
 * referenced in the L2 table. While doing so, performs some checks on L2
 * entries.
 *
 * Returns the number of errors found by the checks or -errno if an internal
 * error occurred.
 */
static int check_refcounts_l2(BlockDriverState *bs, BdrvCheckResult *res,
                              void **refcount_table,
                              int64_t *refcount_table_size, int64_t l2_offset,
                              int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_table, l2_entry;
    uint64_t next_contiguous_offset = 0;
    int i, l2_size, nb_csectors, ret;

    /* Read L2 table from disk */
    l2_size = s->l2_size * sizeof(uint64_t);
    l2_table = g_malloc(l2_size);

    ret = bdrv_pread(bs->file, l2_offset, l2_table, l2_size);
    if (ret < 0) {
        fprintf(stderr, "ERROR: I/O error in check_refcounts_l2\n");
        res->check_errors++;
        goto fail;
    }

    /* Do the actual checks */
    for(i = 0; i < s->l2_size; i++) {
        l2_entry = be64_to_cpu(l2_table[i]);

        switch (qcow2_get_cluster_type(l2_entry)) {
        case QCOW2_CLUSTER_COMPRESSED:
            /* Compressed clusters don't have QCOW_OFLAG_COPIED */
            if (l2_entry & QCOW_OFLAG_COPIED) {
                fprintf(stderr, "ERROR: cluster %" PRId64 ": "
                    "copied flag must never be set for compressed "
                    "clusters\n", l2_entry >> s->cluster_bits);
                l2_entry &= ~QCOW_OFLAG_COPIED;
                res->corruptions++;
            }

            /* Mark cluster as used */
            nb_csectors = ((l2_entry >> s->csize_shift) &
                           s->csize_mask) + 1;
            l2_entry &= s->cluster_offset_mask;
            ret = qcow2_inc_refcounts_imrt(bs, res,
                                           refcount_table, refcount_table_size,
                                           l2_entry & ~511, nb_csectors * 512);
            if (ret < 0) {
                goto fail;
            }

            if (flags & CHECK_FRAG_INFO) {
                res->bfi.allocated_clusters++;
                res->bfi.compressed_clusters++;

                /* Compressed clusters are fragmented by nature.  Since they
                 * take up sub-sector space but we only have sector granularity
                 * I/O we need to re-read the same sectors even for adjacent
                 * compressed clusters.
                 */
                res->bfi.fragmented_clusters++;
            }
            break;

        case QCOW2_CLUSTER_ZERO_ALLOC:
        case QCOW2_CLUSTER_NORMAL:
        {
            uint64_t offset = l2_entry & L2E_OFFSET_MASK;

            if (flags & CHECK_FRAG_INFO) {
                res->bfi.allocated_clusters++;
                if (next_contiguous_offset &&
                    offset != next_contiguous_offset) {
                    res->bfi.fragmented_clusters++;
                }
                next_contiguous_offset = offset + s->cluster_size;
            }

            /* Mark cluster as used */
            ret = qcow2_inc_refcounts_imrt(bs, res,
                                           refcount_table, refcount_table_size,
                                           offset, s->cluster_size);
            if (ret < 0) {
                goto fail;
            }

            /* Correct offsets are cluster aligned */
            if (offset_into_cluster(s, offset)) {
                fprintf(stderr, "ERROR offset=%" PRIx64 ": Cluster is not "
                    "properly aligned; L2 entry corrupted.\n", offset);
                res->corruptions++;
            }
            break;
        }

        case QCOW2_CLUSTER_ZERO_PLAIN:
        case QCOW2_CLUSTER_UNALLOCATED:
            break;

        default:
            abort();
        }
    }

    g_free(l2_table);
    return 0;

fail:
    g_free(l2_table);
    return ret;
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
                              void **refcount_table,
                              int64_t *refcount_table_size,
                              int64_t l1_table_offset, int l1_size,
                              int flags)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l1_table = NULL, l2_offset, l1_size2;
    int i, ret;

    l1_size2 = l1_size * sizeof(uint64_t);

    /* Mark L1 table as used */
    ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, refcount_table_size,
                                   l1_table_offset, l1_size2);
    if (ret < 0) {
        goto fail;
    }

    /* Read L1 table entries from disk */
    if (l1_size2 > 0) {
        l1_table = g_try_malloc(l1_size2);
        if (l1_table == NULL) {
            ret = -ENOMEM;
            res->check_errors++;
            goto fail;
        }
        ret = bdrv_pread(bs->file, l1_table_offset, l1_table, l1_size2);
        if (ret < 0) {
            fprintf(stderr, "ERROR: I/O error in check_refcounts_l1\n");
            res->check_errors++;
            goto fail;
        }
        for(i = 0;i < l1_size; i++)
            be64_to_cpus(&l1_table[i]);
    }

    /* Do the actual checks */
    for(i = 0; i < l1_size; i++) {
        l2_offset = l1_table[i];
        if (l2_offset) {
            /* Mark L2 table as used */
            l2_offset &= L1E_OFFSET_MASK;
            ret = qcow2_inc_refcounts_imrt(bs, res,
                                           refcount_table, refcount_table_size,
                                           l2_offset, s->cluster_size);
            if (ret < 0) {
                goto fail;
            }

            /* L2 tables are cluster aligned */
            if (offset_into_cluster(s, l2_offset)) {
                fprintf(stderr, "ERROR l2_offset=%" PRIx64 ": Table is not "
                    "cluster aligned; L1 entry corrupted\n", l2_offset);
                res->corruptions++;
            }

            /* Process and check L2 entries */
            ret = check_refcounts_l2(bs, res, refcount_table,
                                     refcount_table_size, l2_offset, flags);
            if (ret < 0) {
                goto fail;
            }
        }
    }
    g_free(l1_table);
    return 0;

fail:
    g_free(l1_table);
    return ret;
}

/*
 * Checks the OFLAG_COPIED flag for all L1 and L2 entries.
 *
 * This function does not print an error message nor does it increment
 * check_errors if qcow2_get_refcount fails (this is because such an error will
 * have been already detected and sufficiently signaled by the calling function
 * (qcow2_check_refcounts) by the time this function is called).
 */
static int check_oflag_copied(BlockDriverState *bs, BdrvCheckResult *res,
                              BdrvCheckMode fix)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t *l2_table = qemu_blockalign(bs, s->cluster_size);
    int ret;
    uint64_t refcount;
    int i, j;

    for (i = 0; i < s->l1_size; i++) {
        uint64_t l1_entry = s->l1_table[i];
        uint64_t l2_offset = l1_entry & L1E_OFFSET_MASK;
        bool l2_dirty = false;

        if (!l2_offset) {
            continue;
        }

        ret = qcow2_get_refcount(bs, l2_offset >> s->cluster_bits,
                                 &refcount);
        if (ret < 0) {
            /* don't print message nor increment check_errors */
            continue;
        }
        if ((refcount == 1) != ((l1_entry & QCOW_OFLAG_COPIED) != 0)) {
            fprintf(stderr, "%s OFLAG_COPIED L2 cluster: l1_index=%d "
                    "l1_entry=%" PRIx64 " refcount=%" PRIu64 "\n",
                    fix & BDRV_FIX_ERRORS ? "Repairing" :
                                            "ERROR",
                    i, l1_entry, refcount);
            if (fix & BDRV_FIX_ERRORS) {
                s->l1_table[i] = refcount == 1
                               ? l1_entry |  QCOW_OFLAG_COPIED
                               : l1_entry & ~QCOW_OFLAG_COPIED;
                ret = qcow2_write_l1_entry(bs, i);
                if (ret < 0) {
                    res->check_errors++;
                    goto fail;
                }
                res->corruptions_fixed++;
            } else {
                res->corruptions++;
            }
        }

        ret = bdrv_pread(bs->file, l2_offset, l2_table,
                         s->l2_size * sizeof(uint64_t));
        if (ret < 0) {
            fprintf(stderr, "ERROR: Could not read L2 table: %s\n",
                    strerror(-ret));
            res->check_errors++;
            goto fail;
        }

        for (j = 0; j < s->l2_size; j++) {
            uint64_t l2_entry = be64_to_cpu(l2_table[j]);
            uint64_t data_offset = l2_entry & L2E_OFFSET_MASK;
            QCow2ClusterType cluster_type = qcow2_get_cluster_type(l2_entry);

            if (cluster_type == QCOW2_CLUSTER_NORMAL ||
                cluster_type == QCOW2_CLUSTER_ZERO_ALLOC) {
                ret = qcow2_get_refcount(bs,
                                         data_offset >> s->cluster_bits,
                                         &refcount);
                if (ret < 0) {
                    /* don't print message nor increment check_errors */
                    continue;
                }
                if ((refcount == 1) != ((l2_entry & QCOW_OFLAG_COPIED) != 0)) {
                    fprintf(stderr, "%s OFLAG_COPIED data cluster: "
                            "l2_entry=%" PRIx64 " refcount=%" PRIu64 "\n",
                            fix & BDRV_FIX_ERRORS ? "Repairing" :
                                                    "ERROR",
                            l2_entry, refcount);
                    if (fix & BDRV_FIX_ERRORS) {
                        l2_table[j] = cpu_to_be64(refcount == 1
                                    ? l2_entry |  QCOW_OFLAG_COPIED
                                    : l2_entry & ~QCOW_OFLAG_COPIED);
                        l2_dirty = true;
                        res->corruptions_fixed++;
                    } else {
                        res->corruptions++;
                    }
                }
            }
        }

        if (l2_dirty) {
            ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_ACTIVE_L2,
                                                l2_offset, s->cluster_size);
            if (ret < 0) {
                fprintf(stderr, "ERROR: Could not write L2 table; metadata "
                        "overlap check failed: %s\n", strerror(-ret));
                res->check_errors++;
                goto fail;
            }

            ret = bdrv_pwrite(bs->file, l2_offset, l2_table,
                              s->cluster_size);
            if (ret < 0) {
                fprintf(stderr, "ERROR: Could not write L2 table: %s\n",
                        strerror(-ret));
                res->check_errors++;
                goto fail;
            }
        }
    }

    ret = 0;

fail:
    qemu_vfree(l2_table);
    return ret;
}

/*
 * Checks consistency of refblocks and accounts for each refblock in
 * *refcount_table.
 */
static int check_refblocks(BlockDriverState *bs, BdrvCheckResult *res,
                           BdrvCheckMode fix, bool *rebuild,
                           void **refcount_table, int64_t *nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t i, size;
    int ret;

    for(i = 0; i < s->refcount_table_size; i++) {
        uint64_t offset, cluster;
        offset = s->refcount_table[i];
        cluster = offset >> s->cluster_bits;

        /* Refcount blocks are cluster aligned */
        if (offset_into_cluster(s, offset)) {
            fprintf(stderr, "ERROR refcount block %" PRId64 " is not "
                "cluster aligned; refcount table entry corrupted\n", i);
            res->corruptions++;
            *rebuild = true;
            continue;
        }

        if (cluster >= *nb_clusters) {
            fprintf(stderr, "%s refcount block %" PRId64 " is outside image\n",
                    fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR", i);

            if (fix & BDRV_FIX_ERRORS) {
                int64_t new_nb_clusters;
                Error *local_err = NULL;

                if (offset > INT64_MAX - s->cluster_size) {
                    ret = -EINVAL;
                    goto resize_fail;
                }

                ret = bdrv_truncate(bs->file, offset + s->cluster_size,
                                    PREALLOC_MODE_OFF, &local_err);
                if (ret < 0) {
                    error_report_err(local_err);
                    goto resize_fail;
                }
                size = bdrv_getlength(bs->file->bs);
                if (size < 0) {
                    ret = size;
                    goto resize_fail;
                }

                new_nb_clusters = size_to_clusters(s, size);
                assert(new_nb_clusters >= *nb_clusters);

                ret = realloc_refcount_array(s, refcount_table,
                                             nb_clusters, new_nb_clusters);
                if (ret < 0) {
                    res->check_errors++;
                    return ret;
                }

                if (cluster >= *nb_clusters) {
                    ret = -EINVAL;
                    goto resize_fail;
                }

                res->corruptions_fixed++;
                ret = qcow2_inc_refcounts_imrt(bs, res,
                                               refcount_table, nb_clusters,
                                               offset, s->cluster_size);
                if (ret < 0) {
                    return ret;
                }
                /* No need to check whether the refcount is now greater than 1:
                 * This area was just allocated and zeroed, so it can only be
                 * exactly 1 after qcow2_inc_refcounts_imrt() */
                continue;

resize_fail:
                res->corruptions++;
                *rebuild = true;
                fprintf(stderr, "ERROR could not resize image: %s\n",
                        strerror(-ret));
            } else {
                res->corruptions++;
            }
            continue;
        }

        if (offset != 0) {
            ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, nb_clusters,
                                           offset, s->cluster_size);
            if (ret < 0) {
                return ret;
            }
            if (s->get_refcount(*refcount_table, cluster) != 1) {
                fprintf(stderr, "ERROR refcount block %" PRId64
                        " refcount=%" PRIu64 "\n", i,
                        s->get_refcount(*refcount_table, cluster));
                res->corruptions++;
                *rebuild = true;
            }
        }
    }

    return 0;
}

/*
 * Calculates an in-memory refcount table.
 */
static int calculate_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                               BdrvCheckMode fix, bool *rebuild,
                               void **refcount_table, int64_t *nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t i;
    QCowSnapshot *sn;
    int ret;

    if (!*refcount_table) {
        int64_t old_size = 0;
        ret = realloc_refcount_array(s, refcount_table,
                                     &old_size, *nb_clusters);
        if (ret < 0) {
            res->check_errors++;
            return ret;
        }
    }

    /* header */
    ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, nb_clusters,
                                   0, s->cluster_size);
    if (ret < 0) {
        return ret;
    }

    /* current L1 table */
    ret = check_refcounts_l1(bs, res, refcount_table, nb_clusters,
                             s->l1_table_offset, s->l1_size, CHECK_FRAG_INFO);
    if (ret < 0) {
        return ret;
    }

    /* snapshots */
    for (i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        ret = check_refcounts_l1(bs, res, refcount_table, nb_clusters,
                                 sn->l1_table_offset, sn->l1_size, 0);
        if (ret < 0) {
            return ret;
        }
    }
    ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, nb_clusters,
                                   s->snapshots_offset, s->snapshots_size);
    if (ret < 0) {
        return ret;
    }

    /* refcount data */
    ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, nb_clusters,
                                   s->refcount_table_offset,
                                   s->refcount_table_size * sizeof(uint64_t));
    if (ret < 0) {
        return ret;
    }

    /* encryption */
    if (s->crypto_header.length) {
        ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, nb_clusters,
                                       s->crypto_header.offset,
                                       s->crypto_header.length);
        if (ret < 0) {
            return ret;
        }
    }

    /* bitmaps */
    ret = qcow2_check_bitmaps_refcounts(bs, res, refcount_table, nb_clusters);
    if (ret < 0) {
        return ret;
    }

    return check_refblocks(bs, res, fix, rebuild, refcount_table, nb_clusters);
}

/*
 * Compares the actual reference count for each cluster in the image against the
 * refcount as reported by the refcount structures on-disk.
 */
static void compare_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                              BdrvCheckMode fix, bool *rebuild,
                              int64_t *highest_cluster,
                              void *refcount_table, int64_t nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t i;
    uint64_t refcount1, refcount2;
    int ret;

    for (i = 0, *highest_cluster = 0; i < nb_clusters; i++) {
        ret = qcow2_get_refcount(bs, i, &refcount1);
        if (ret < 0) {
            fprintf(stderr, "Can't get refcount for cluster %" PRId64 ": %s\n",
                    i, strerror(-ret));
            res->check_errors++;
            continue;
        }

        refcount2 = s->get_refcount(refcount_table, i);

        if (refcount1 > 0 || refcount2 > 0) {
            *highest_cluster = i;
        }

        if (refcount1 != refcount2) {
            /* Check if we're allowed to fix the mismatch */
            int *num_fixed = NULL;
            if (refcount1 == 0) {
                *rebuild = true;
            } else if (refcount1 > refcount2 && (fix & BDRV_FIX_LEAKS)) {
                num_fixed = &res->leaks_fixed;
            } else if (refcount1 < refcount2 && (fix & BDRV_FIX_ERRORS)) {
                num_fixed = &res->corruptions_fixed;
            }

            fprintf(stderr, "%s cluster %" PRId64 " refcount=%" PRIu64
                    " reference=%" PRIu64 "\n",
                   num_fixed != NULL     ? "Repairing" :
                   refcount1 < refcount2 ? "ERROR" :
                                           "Leaked",
                   i, refcount1, refcount2);

            if (num_fixed) {
                ret = update_refcount(bs, i << s->cluster_bits, 1,
                                      refcount_diff(refcount1, refcount2),
                                      refcount1 > refcount2,
                                      QCOW2_DISCARD_ALWAYS);
                if (ret >= 0) {
                    (*num_fixed)++;
                    continue;
                }
            }

            /* And if we couldn't, print an error */
            if (refcount1 < refcount2) {
                res->corruptions++;
            } else {
                res->leaks++;
            }
        }
    }
}

/*
 * Allocates clusters using an in-memory refcount table (IMRT) in contrast to
 * the on-disk refcount structures.
 *
 * On input, *first_free_cluster tells where to start looking, and need not
 * actually be a free cluster; the returned offset will not be before that
 * cluster.  On output, *first_free_cluster points to the first gap found, even
 * if that gap was too small to be used as the returned offset.
 *
 * Note that *first_free_cluster is a cluster index whereas the return value is
 * an offset.
 */
static int64_t alloc_clusters_imrt(BlockDriverState *bs,
                                   int cluster_count,
                                   void **refcount_table,
                                   int64_t *imrt_nb_clusters,
                                   int64_t *first_free_cluster)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t cluster = *first_free_cluster, i;
    bool first_gap = true;
    int contiguous_free_clusters;
    int ret;

    /* Starting at *first_free_cluster, find a range of at least cluster_count
     * continuously free clusters */
    for (contiguous_free_clusters = 0;
         cluster < *imrt_nb_clusters &&
         contiguous_free_clusters < cluster_count;
         cluster++)
    {
        if (!s->get_refcount(*refcount_table, cluster)) {
            contiguous_free_clusters++;
            if (first_gap) {
                /* If this is the first free cluster found, update
                 * *first_free_cluster accordingly */
                *first_free_cluster = cluster;
                first_gap = false;
            }
        } else if (contiguous_free_clusters) {
            contiguous_free_clusters = 0;
        }
    }

    /* If contiguous_free_clusters is greater than zero, it contains the number
     * of continuously free clusters until the current cluster; the first free
     * cluster in the current "gap" is therefore
     * cluster - contiguous_free_clusters */

    /* If no such range could be found, grow the in-memory refcount table
     * accordingly to append free clusters at the end of the image */
    if (contiguous_free_clusters < cluster_count) {
        /* contiguous_free_clusters clusters are already empty at the image end;
         * we need cluster_count clusters; therefore, we have to allocate
         * cluster_count - contiguous_free_clusters new clusters at the end of
         * the image (which is the current value of cluster; note that cluster
         * may exceed old_imrt_nb_clusters if *first_free_cluster pointed beyond
         * the image end) */
        ret = realloc_refcount_array(s, refcount_table, imrt_nb_clusters,
                                     cluster + cluster_count
                                     - contiguous_free_clusters);
        if (ret < 0) {
            return ret;
        }
    }

    /* Go back to the first free cluster */
    cluster -= contiguous_free_clusters;
    for (i = 0; i < cluster_count; i++) {
        s->set_refcount(*refcount_table, cluster + i, 1);
    }

    return cluster << s->cluster_bits;
}

/*
 * Creates a new refcount structure based solely on the in-memory information
 * given through *refcount_table. All necessary allocations will be reflected
 * in that array.
 *
 * On success, the old refcount structure is leaked (it will be covered by the
 * new refcount structure).
 */
static int rebuild_refcount_structure(BlockDriverState *bs,
                                      BdrvCheckResult *res,
                                      void **refcount_table,
                                      int64_t *nb_clusters)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t first_free_cluster = 0, reftable_offset = -1, cluster = 0;
    int64_t refblock_offset, refblock_start, refblock_index;
    uint32_t reftable_size = 0;
    uint64_t *on_disk_reftable = NULL;
    void *on_disk_refblock;
    int ret = 0;
    struct {
        uint64_t reftable_offset;
        uint32_t reftable_clusters;
    } QEMU_PACKED reftable_offset_and_clusters;

    qcow2_cache_empty(bs, s->refcount_block_cache);

write_refblocks:
    for (; cluster < *nb_clusters; cluster++) {
        if (!s->get_refcount(*refcount_table, cluster)) {
            continue;
        }

        refblock_index = cluster >> s->refcount_block_bits;
        refblock_start = refblock_index << s->refcount_block_bits;

        /* Don't allocate a cluster in a refblock already written to disk */
        if (first_free_cluster < refblock_start) {
            first_free_cluster = refblock_start;
        }
        refblock_offset = alloc_clusters_imrt(bs, 1, refcount_table,
                                              nb_clusters, &first_free_cluster);
        if (refblock_offset < 0) {
            fprintf(stderr, "ERROR allocating refblock: %s\n",
                    strerror(-refblock_offset));
            res->check_errors++;
            ret = refblock_offset;
            goto fail;
        }

        if (reftable_size <= refblock_index) {
            uint32_t old_reftable_size = reftable_size;
            uint64_t *new_on_disk_reftable;

            reftable_size = ROUND_UP((refblock_index + 1) * sizeof(uint64_t),
                                     s->cluster_size) / sizeof(uint64_t);
            new_on_disk_reftable = g_try_realloc(on_disk_reftable,
                                                 reftable_size *
                                                 sizeof(uint64_t));
            if (!new_on_disk_reftable) {
                res->check_errors++;
                ret = -ENOMEM;
                goto fail;
            }
            on_disk_reftable = new_on_disk_reftable;

            memset(on_disk_reftable + old_reftable_size, 0,
                   (reftable_size - old_reftable_size) * sizeof(uint64_t));

            /* The offset we have for the reftable is now no longer valid;
             * this will leak that range, but we can easily fix that by running
             * a leak-fixing check after this rebuild operation */
            reftable_offset = -1;
        } else {
            assert(on_disk_reftable);
        }
        on_disk_reftable[refblock_index] = refblock_offset;

        /* If this is apparently the last refblock (for now), try to squeeze the
         * reftable in */
        if (refblock_index == (*nb_clusters - 1) >> s->refcount_block_bits &&
            reftable_offset < 0)
        {
            uint64_t reftable_clusters = size_to_clusters(s, reftable_size *
                                                          sizeof(uint64_t));
            reftable_offset = alloc_clusters_imrt(bs, reftable_clusters,
                                                  refcount_table, nb_clusters,
                                                  &first_free_cluster);
            if (reftable_offset < 0) {
                fprintf(stderr, "ERROR allocating reftable: %s\n",
                        strerror(-reftable_offset));
                res->check_errors++;
                ret = reftable_offset;
                goto fail;
            }
        }

        ret = qcow2_pre_write_overlap_check(bs, 0, refblock_offset,
                                            s->cluster_size);
        if (ret < 0) {
            fprintf(stderr, "ERROR writing refblock: %s\n", strerror(-ret));
            goto fail;
        }

        /* The size of *refcount_table is always cluster-aligned, therefore the
         * write operation will not overflow */
        on_disk_refblock = (void *)((char *) *refcount_table +
                                    refblock_index * s->cluster_size);

        ret = bdrv_write(bs->file, refblock_offset / BDRV_SECTOR_SIZE,
                         on_disk_refblock, s->cluster_sectors);
        if (ret < 0) {
            fprintf(stderr, "ERROR writing refblock: %s\n", strerror(-ret));
            goto fail;
        }

        /* Go to the end of this refblock */
        cluster = refblock_start + s->refcount_block_size - 1;
    }

    if (reftable_offset < 0) {
        uint64_t post_refblock_start, reftable_clusters;

        post_refblock_start = ROUND_UP(*nb_clusters, s->refcount_block_size);
        reftable_clusters = size_to_clusters(s,
                                             reftable_size * sizeof(uint64_t));
        /* Not pretty but simple */
        if (first_free_cluster < post_refblock_start) {
            first_free_cluster = post_refblock_start;
        }
        reftable_offset = alloc_clusters_imrt(bs, reftable_clusters,
                                              refcount_table, nb_clusters,
                                              &first_free_cluster);
        if (reftable_offset < 0) {
            fprintf(stderr, "ERROR allocating reftable: %s\n",
                    strerror(-reftable_offset));
            res->check_errors++;
            ret = reftable_offset;
            goto fail;
        }

        goto write_refblocks;
    }

    for (refblock_index = 0; refblock_index < reftable_size; refblock_index++) {
        cpu_to_be64s(&on_disk_reftable[refblock_index]);
    }

    ret = qcow2_pre_write_overlap_check(bs, 0, reftable_offset,
                                        reftable_size * sizeof(uint64_t));
    if (ret < 0) {
        fprintf(stderr, "ERROR writing reftable: %s\n", strerror(-ret));
        goto fail;
    }

    assert(reftable_size < INT_MAX / sizeof(uint64_t));
    ret = bdrv_pwrite(bs->file, reftable_offset, on_disk_reftable,
                      reftable_size * sizeof(uint64_t));
    if (ret < 0) {
        fprintf(stderr, "ERROR writing reftable: %s\n", strerror(-ret));
        goto fail;
    }

    /* Enter new reftable into the image header */
    reftable_offset_and_clusters.reftable_offset = cpu_to_be64(reftable_offset);
    reftable_offset_and_clusters.reftable_clusters =
        cpu_to_be32(size_to_clusters(s, reftable_size * sizeof(uint64_t)));
    ret = bdrv_pwrite_sync(bs->file,
                           offsetof(QCowHeader, refcount_table_offset),
                           &reftable_offset_and_clusters,
                           sizeof(reftable_offset_and_clusters));
    if (ret < 0) {
        fprintf(stderr, "ERROR setting reftable: %s\n", strerror(-ret));
        goto fail;
    }

    for (refblock_index = 0; refblock_index < reftable_size; refblock_index++) {
        be64_to_cpus(&on_disk_reftable[refblock_index]);
    }
    s->refcount_table = on_disk_reftable;
    s->refcount_table_offset = reftable_offset;
    s->refcount_table_size = reftable_size;
    update_max_refcount_table_index(s);

    return 0;

fail:
    g_free(on_disk_reftable);
    return ret;
}

/*
 * Checks an image for refcount consistency.
 *
 * Returns 0 if no errors are found, the number of errors in case the image is
 * detected as corrupted, and -errno when an internal error occurred.
 */
int qcow2_check_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                          BdrvCheckMode fix)
{
    BDRVQcow2State *s = bs->opaque;
    BdrvCheckResult pre_compare_res;
    int64_t size, highest_cluster, nb_clusters;
    void *refcount_table = NULL;
    bool rebuild = false;
    int ret;

    size = bdrv_getlength(bs->file->bs);
    if (size < 0) {
        res->check_errors++;
        return size;
    }

    nb_clusters = size_to_clusters(s, size);
    if (nb_clusters > INT_MAX) {
        res->check_errors++;
        return -EFBIG;
    }

    res->bfi.total_clusters =
        size_to_clusters(s, bs->total_sectors * BDRV_SECTOR_SIZE);

    ret = calculate_refcounts(bs, res, fix, &rebuild, &refcount_table,
                              &nb_clusters);
    if (ret < 0) {
        goto fail;
    }

    /* In case we don't need to rebuild the refcount structure (but want to fix
     * something), this function is immediately called again, in which case the
     * result should be ignored */
    pre_compare_res = *res;
    compare_refcounts(bs, res, 0, &rebuild, &highest_cluster, refcount_table,
                      nb_clusters);

    if (rebuild && (fix & BDRV_FIX_ERRORS)) {
        BdrvCheckResult old_res = *res;
        int fresh_leaks = 0;

        fprintf(stderr, "Rebuilding refcount structure\n");
        ret = rebuild_refcount_structure(bs, res, &refcount_table,
                                         &nb_clusters);
        if (ret < 0) {
            goto fail;
        }

        res->corruptions = 0;
        res->leaks = 0;

        /* Because the old reftable has been exchanged for a new one the
         * references have to be recalculated */
        rebuild = false;
        memset(refcount_table, 0, refcount_array_byte_size(s, nb_clusters));
        ret = calculate_refcounts(bs, res, 0, &rebuild, &refcount_table,
                                  &nb_clusters);
        if (ret < 0) {
            goto fail;
        }

        if (fix & BDRV_FIX_LEAKS) {
            /* The old refcount structures are now leaked, fix it; the result
             * can be ignored, aside from leaks which were introduced by
             * rebuild_refcount_structure() that could not be fixed */
            BdrvCheckResult saved_res = *res;
            *res = (BdrvCheckResult){ 0 };

            compare_refcounts(bs, res, BDRV_FIX_LEAKS, &rebuild,
                              &highest_cluster, refcount_table, nb_clusters);
            if (rebuild) {
                fprintf(stderr, "ERROR rebuilt refcount structure is still "
                        "broken\n");
            }

            /* Any leaks accounted for here were introduced by
             * rebuild_refcount_structure() because that function has created a
             * new refcount structure from scratch */
            fresh_leaks = res->leaks;
            *res = saved_res;
        }

        if (res->corruptions < old_res.corruptions) {
            res->corruptions_fixed += old_res.corruptions - res->corruptions;
        }
        if (res->leaks < old_res.leaks) {
            res->leaks_fixed += old_res.leaks - res->leaks;
        }
        res->leaks += fresh_leaks;
    } else if (fix) {
        if (rebuild) {
            fprintf(stderr, "ERROR need to rebuild refcount structures\n");
            res->check_errors++;
            ret = -EIO;
            goto fail;
        }

        if (res->leaks || res->corruptions) {
            *res = pre_compare_res;
            compare_refcounts(bs, res, fix, &rebuild, &highest_cluster,
                              refcount_table, nb_clusters);
        }
    }

    /* check OFLAG_COPIED */
    ret = check_oflag_copied(bs, res, fix);
    if (ret < 0) {
        goto fail;
    }

    res->image_end_offset = (highest_cluster + 1) * s->cluster_size;
    ret = 0;

fail:
    g_free(refcount_table);

    return ret;
}

#define overlaps_with(ofs, sz) \
    ranges_overlap(offset, size, ofs, sz)

/*
 * Checks if the given offset into the image file is actually free to use by
 * looking for overlaps with important metadata sections (L1/L2 tables etc.),
 * i.e. a sanity check without relying on the refcount tables.
 *
 * The ign parameter specifies what checks not to perform (being a bitmask of
 * QCow2MetadataOverlap values), i.e., what sections to ignore.
 *
 * Returns:
 * - 0 if writing to this offset will not affect the mentioned metadata
 * - a positive QCow2MetadataOverlap value indicating one overlapping section
 * - a negative value (-errno) indicating an error while performing a check,
 *   e.g. when bdrv_read failed on QCOW2_OL_INACTIVE_L2
 */
int qcow2_check_metadata_overlap(BlockDriverState *bs, int ign, int64_t offset,
                                 int64_t size)
{
    BDRVQcow2State *s = bs->opaque;
    int chk = s->overlap_check & ~ign;
    int i, j;

    if (!size) {
        return 0;
    }

    if (chk & QCOW2_OL_MAIN_HEADER) {
        if (offset < s->cluster_size) {
            return QCOW2_OL_MAIN_HEADER;
        }
    }

    /* align range to test to cluster boundaries */
    size = align_offset(offset_into_cluster(s, offset) + size, s->cluster_size);
    offset = start_of_cluster(s, offset);

    if ((chk & QCOW2_OL_ACTIVE_L1) && s->l1_size) {
        if (overlaps_with(s->l1_table_offset, s->l1_size * sizeof(uint64_t))) {
            return QCOW2_OL_ACTIVE_L1;
        }
    }

    if ((chk & QCOW2_OL_REFCOUNT_TABLE) && s->refcount_table_size) {
        if (overlaps_with(s->refcount_table_offset,
            s->refcount_table_size * sizeof(uint64_t))) {
            return QCOW2_OL_REFCOUNT_TABLE;
        }
    }

    if ((chk & QCOW2_OL_SNAPSHOT_TABLE) && s->snapshots_size) {
        if (overlaps_with(s->snapshots_offset, s->snapshots_size)) {
            return QCOW2_OL_SNAPSHOT_TABLE;
        }
    }

    if ((chk & QCOW2_OL_INACTIVE_L1) && s->snapshots) {
        for (i = 0; i < s->nb_snapshots; i++) {
            if (s->snapshots[i].l1_size &&
                overlaps_with(s->snapshots[i].l1_table_offset,
                s->snapshots[i].l1_size * sizeof(uint64_t))) {
                return QCOW2_OL_INACTIVE_L1;
            }
        }
    }

    if ((chk & QCOW2_OL_ACTIVE_L2) && s->l1_table) {
        for (i = 0; i < s->l1_size; i++) {
            if ((s->l1_table[i] & L1E_OFFSET_MASK) &&
                overlaps_with(s->l1_table[i] & L1E_OFFSET_MASK,
                s->cluster_size)) {
                return QCOW2_OL_ACTIVE_L2;
            }
        }
    }

    if ((chk & QCOW2_OL_REFCOUNT_BLOCK) && s->refcount_table) {
        unsigned last_entry = s->max_refcount_table_index;
        assert(last_entry < s->refcount_table_size);
        assert(last_entry + 1 == s->refcount_table_size ||
               (s->refcount_table[last_entry + 1] & REFT_OFFSET_MASK) == 0);
        for (i = 0; i <= last_entry; i++) {
            if ((s->refcount_table[i] & REFT_OFFSET_MASK) &&
                overlaps_with(s->refcount_table[i] & REFT_OFFSET_MASK,
                s->cluster_size)) {
                return QCOW2_OL_REFCOUNT_BLOCK;
            }
        }
    }

    if ((chk & QCOW2_OL_INACTIVE_L2) && s->snapshots) {
        for (i = 0; i < s->nb_snapshots; i++) {
            uint64_t l1_ofs = s->snapshots[i].l1_table_offset;
            uint32_t l1_sz  = s->snapshots[i].l1_size;
            uint64_t l1_sz2 = l1_sz * sizeof(uint64_t);
            uint64_t *l1 = g_try_malloc(l1_sz2);
            int ret;

            if (l1_sz2 && l1 == NULL) {
                return -ENOMEM;
            }

            ret = bdrv_pread(bs->file, l1_ofs, l1, l1_sz2);
            if (ret < 0) {
                g_free(l1);
                return ret;
            }

            for (j = 0; j < l1_sz; j++) {
                uint64_t l2_ofs = be64_to_cpu(l1[j]) & L1E_OFFSET_MASK;
                if (l2_ofs && overlaps_with(l2_ofs, s->cluster_size)) {
                    g_free(l1);
                    return QCOW2_OL_INACTIVE_L2;
                }
            }

            g_free(l1);
        }
    }

    return 0;
}

static const char *metadata_ol_names[] = {
    [QCOW2_OL_MAIN_HEADER_BITNR]    = "qcow2_header",
    [QCOW2_OL_ACTIVE_L1_BITNR]      = "active L1 table",
    [QCOW2_OL_ACTIVE_L2_BITNR]      = "active L2 table",
    [QCOW2_OL_REFCOUNT_TABLE_BITNR] = "refcount table",
    [QCOW2_OL_REFCOUNT_BLOCK_BITNR] = "refcount block",
    [QCOW2_OL_SNAPSHOT_TABLE_BITNR] = "snapshot table",
    [QCOW2_OL_INACTIVE_L1_BITNR]    = "inactive L1 table",
    [QCOW2_OL_INACTIVE_L2_BITNR]    = "inactive L2 table",
};

/*
 * First performs a check for metadata overlaps (through
 * qcow2_check_metadata_overlap); if that fails with a negative value (error
 * while performing a check), that value is returned. If an impending overlap
 * is detected, the BDS will be made unusable, the qcow2 file marked corrupt
 * and -EIO returned.
 *
 * Returns 0 if there were neither overlaps nor errors while checking for
 * overlaps; or a negative value (-errno) on error.
 */
int qcow2_pre_write_overlap_check(BlockDriverState *bs, int ign, int64_t offset,
                                  int64_t size)
{
    int ret = qcow2_check_metadata_overlap(bs, ign, offset, size);

    if (ret < 0) {
        return ret;
    } else if (ret > 0) {
        int metadata_ol_bitnr = ctz32(ret);
        assert(metadata_ol_bitnr < QCOW2_OL_MAX_BITNR);

        qcow2_signal_corruption(bs, true, offset, size, "Preventing invalid "
                                "write on metadata (overlaps with %s)",
                                metadata_ol_names[metadata_ol_bitnr]);
        return -EIO;
    }

    return 0;
}

/* A pointer to a function of this type is given to walk_over_reftable(). That
 * function will create refblocks and pass them to a RefblockFinishOp once they
 * are completed (@refblock). @refblock_empty is set if the refblock is
 * completely empty.
 *
 * Along with the refblock, a corresponding reftable entry is passed, in the
 * reftable @reftable (which may be reallocated) at @reftable_index.
 *
 * @allocated should be set to true if a new cluster has been allocated.
 */
typedef int (RefblockFinishOp)(BlockDriverState *bs, uint64_t **reftable,
                               uint64_t reftable_index, uint64_t *reftable_size,
                               void *refblock, bool refblock_empty,
                               bool *allocated, Error **errp);

/**
 * This "operation" for walk_over_reftable() allocates the refblock on disk (if
 * it is not empty) and inserts its offset into the new reftable. The size of
 * this new reftable is increased as required.
 */
static int alloc_refblock(BlockDriverState *bs, uint64_t **reftable,
                          uint64_t reftable_index, uint64_t *reftable_size,
                          void *refblock, bool refblock_empty, bool *allocated,
                          Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t offset;

    if (!refblock_empty && reftable_index >= *reftable_size) {
        uint64_t *new_reftable;
        uint64_t new_reftable_size;

        new_reftable_size = ROUND_UP(reftable_index + 1,
                                     s->cluster_size / sizeof(uint64_t));
        if (new_reftable_size > QCOW_MAX_REFTABLE_SIZE / sizeof(uint64_t)) {
            error_setg(errp,
                       "This operation would make the refcount table grow "
                       "beyond the maximum size supported by QEMU, aborting");
            return -ENOTSUP;
        }

        new_reftable = g_try_realloc(*reftable, new_reftable_size *
                                                sizeof(uint64_t));
        if (!new_reftable) {
            error_setg(errp, "Failed to increase reftable buffer size");
            return -ENOMEM;
        }

        memset(new_reftable + *reftable_size, 0,
               (new_reftable_size - *reftable_size) * sizeof(uint64_t));

        *reftable      = new_reftable;
        *reftable_size = new_reftable_size;
    }

    if (!refblock_empty && !(*reftable)[reftable_index]) {
        offset = qcow2_alloc_clusters(bs, s->cluster_size);
        if (offset < 0) {
            error_setg_errno(errp, -offset, "Failed to allocate refblock");
            return offset;
        }
        (*reftable)[reftable_index] = offset;
        *allocated = true;
    }

    return 0;
}

/**
 * This "operation" for walk_over_reftable() writes the refblock to disk at the
 * offset specified by the new reftable's entry. It does not modify the new
 * reftable or change any refcounts.
 */
static int flush_refblock(BlockDriverState *bs, uint64_t **reftable,
                          uint64_t reftable_index, uint64_t *reftable_size,
                          void *refblock, bool refblock_empty, bool *allocated,
                          Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t offset;
    int ret;

    if (reftable_index < *reftable_size && (*reftable)[reftable_index]) {
        offset = (*reftable)[reftable_index];

        ret = qcow2_pre_write_overlap_check(bs, 0, offset, s->cluster_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Overlap check failed");
            return ret;
        }

        ret = bdrv_pwrite(bs->file, offset, refblock, s->cluster_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to write refblock");
            return ret;
        }
    } else {
        assert(refblock_empty);
    }

    return 0;
}

/**
 * This function walks over the existing reftable and every referenced refblock;
 * if @new_set_refcount is non-NULL, it is called for every refcount entry to
 * create an equal new entry in the passed @new_refblock. Once that
 * @new_refblock is completely filled, @operation will be called.
 *
 * @status_cb and @cb_opaque are used for the amend operation's status callback.
 * @index is the index of the walk_over_reftable() calls and @total is the total
 * number of walk_over_reftable() calls per amend operation. Both are used for
 * calculating the parameters for the status callback.
 *
 * @allocated is set to true if a new cluster has been allocated.
 */
static int walk_over_reftable(BlockDriverState *bs, uint64_t **new_reftable,
                              uint64_t *new_reftable_index,
                              uint64_t *new_reftable_size,
                              void *new_refblock, int new_refblock_size,
                              int new_refcount_bits,
                              RefblockFinishOp *operation, bool *allocated,
                              Qcow2SetRefcountFunc *new_set_refcount,
                              BlockDriverAmendStatusCB *status_cb,
                              void *cb_opaque, int index, int total,
                              Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t reftable_index;
    bool new_refblock_empty = true;
    int refblock_index;
    int new_refblock_index = 0;
    int ret;

    for (reftable_index = 0; reftable_index < s->refcount_table_size;
         reftable_index++)
    {
        uint64_t refblock_offset = s->refcount_table[reftable_index]
                                 & REFT_OFFSET_MASK;

        status_cb(bs, (uint64_t)index * s->refcount_table_size + reftable_index,
                  (uint64_t)total * s->refcount_table_size, cb_opaque);

        if (refblock_offset) {
            void *refblock;

            if (offset_into_cluster(s, refblock_offset)) {
                qcow2_signal_corruption(bs, true, -1, -1, "Refblock offset %#"
                                        PRIx64 " unaligned (reftable index: %#"
                                        PRIx64 ")", refblock_offset,
                                        reftable_index);
                error_setg(errp,
                           "Image is corrupt (unaligned refblock offset)");
                return -EIO;
            }

            ret = qcow2_cache_get(bs, s->refcount_block_cache, refblock_offset,
                                  &refblock);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Failed to retrieve refblock");
                return ret;
            }

            for (refblock_index = 0; refblock_index < s->refcount_block_size;
                 refblock_index++)
            {
                uint64_t refcount;

                if (new_refblock_index >= new_refblock_size) {
                    /* new_refblock is now complete */
                    ret = operation(bs, new_reftable, *new_reftable_index,
                                    new_reftable_size, new_refblock,
                                    new_refblock_empty, allocated, errp);
                    if (ret < 0) {
                        qcow2_cache_put(bs, s->refcount_block_cache, &refblock);
                        return ret;
                    }

                    (*new_reftable_index)++;
                    new_refblock_index = 0;
                    new_refblock_empty = true;
                }

                refcount = s->get_refcount(refblock, refblock_index);
                if (new_refcount_bits < 64 && refcount >> new_refcount_bits) {
                    uint64_t offset;

                    qcow2_cache_put(bs, s->refcount_block_cache, &refblock);

                    offset = ((reftable_index << s->refcount_block_bits)
                              + refblock_index) << s->cluster_bits;

                    error_setg(errp, "Cannot decrease refcount entry width to "
                               "%i bits: Cluster at offset %#" PRIx64 " has a "
                               "refcount of %" PRIu64, new_refcount_bits,
                               offset, refcount);
                    return -EINVAL;
                }

                if (new_set_refcount) {
                    new_set_refcount(new_refblock, new_refblock_index++,
                                     refcount);
                } else {
                    new_refblock_index++;
                }
                new_refblock_empty = new_refblock_empty && refcount == 0;
            }

            qcow2_cache_put(bs, s->refcount_block_cache, &refblock);
        } else {
            /* No refblock means every refcount is 0 */
            for (refblock_index = 0; refblock_index < s->refcount_block_size;
                 refblock_index++)
            {
                if (new_refblock_index >= new_refblock_size) {
                    /* new_refblock is now complete */
                    ret = operation(bs, new_reftable, *new_reftable_index,
                                    new_reftable_size, new_refblock,
                                    new_refblock_empty, allocated, errp);
                    if (ret < 0) {
                        return ret;
                    }

                    (*new_reftable_index)++;
                    new_refblock_index = 0;
                    new_refblock_empty = true;
                }

                if (new_set_refcount) {
                    new_set_refcount(new_refblock, new_refblock_index++, 0);
                } else {
                    new_refblock_index++;
                }
            }
        }
    }

    if (new_refblock_index > 0) {
        /* Complete the potentially existing partially filled final refblock */
        if (new_set_refcount) {
            for (; new_refblock_index < new_refblock_size;
                 new_refblock_index++)
            {
                new_set_refcount(new_refblock, new_refblock_index, 0);
            }
        }

        ret = operation(bs, new_reftable, *new_reftable_index,
                        new_reftable_size, new_refblock, new_refblock_empty,
                        allocated, errp);
        if (ret < 0) {
            return ret;
        }

        (*new_reftable_index)++;
    }

    status_cb(bs, (uint64_t)(index + 1) * s->refcount_table_size,
              (uint64_t)total * s->refcount_table_size, cb_opaque);

    return 0;
}

int qcow2_change_refcount_order(BlockDriverState *bs, int refcount_order,
                                BlockDriverAmendStatusCB *status_cb,
                                void *cb_opaque, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2GetRefcountFunc *new_get_refcount;
    Qcow2SetRefcountFunc *new_set_refcount;
    void *new_refblock = qemu_blockalign(bs->file->bs, s->cluster_size);
    uint64_t *new_reftable = NULL, new_reftable_size = 0;
    uint64_t *old_reftable, old_reftable_size, old_reftable_offset;
    uint64_t new_reftable_index = 0;
    uint64_t i;
    int64_t new_reftable_offset = 0, allocated_reftable_size = 0;
    int new_refblock_size, new_refcount_bits = 1 << refcount_order;
    int old_refcount_order;
    int walk_index = 0;
    int ret;
    bool new_allocation;

    assert(s->qcow_version >= 3);
    assert(refcount_order >= 0 && refcount_order <= 6);

    /* see qcow2_open() */
    new_refblock_size = 1 << (s->cluster_bits - (refcount_order - 3));

    new_get_refcount = get_refcount_funcs[refcount_order];
    new_set_refcount = set_refcount_funcs[refcount_order];


    do {
        int total_walks;

        new_allocation = false;

        /* At least we have to do this walk and the one which writes the
         * refblocks; also, at least we have to do this loop here at least
         * twice (normally), first to do the allocations, and second to
         * determine that everything is correctly allocated, this then makes
         * three walks in total */
        total_walks = MAX(walk_index + 2, 3);

        /* First, allocate the structures so they are present in the refcount
         * structures */
        ret = walk_over_reftable(bs, &new_reftable, &new_reftable_index,
                                 &new_reftable_size, NULL, new_refblock_size,
                                 new_refcount_bits, &alloc_refblock,
                                 &new_allocation, NULL, status_cb, cb_opaque,
                                 walk_index++, total_walks, errp);
        if (ret < 0) {
            goto done;
        }

        new_reftable_index = 0;

        if (new_allocation) {
            if (new_reftable_offset) {
                qcow2_free_clusters(bs, new_reftable_offset,
                                    allocated_reftable_size * sizeof(uint64_t),
                                    QCOW2_DISCARD_NEVER);
            }

            new_reftable_offset = qcow2_alloc_clusters(bs, new_reftable_size *
                                                           sizeof(uint64_t));
            if (new_reftable_offset < 0) {
                error_setg_errno(errp, -new_reftable_offset,
                                 "Failed to allocate the new reftable");
                ret = new_reftable_offset;
                goto done;
            }
            allocated_reftable_size = new_reftable_size;
        }
    } while (new_allocation);

    /* Second, write the new refblocks */
    ret = walk_over_reftable(bs, &new_reftable, &new_reftable_index,
                             &new_reftable_size, new_refblock,
                             new_refblock_size, new_refcount_bits,
                             &flush_refblock, &new_allocation, new_set_refcount,
                             status_cb, cb_opaque, walk_index, walk_index + 1,
                             errp);
    if (ret < 0) {
        goto done;
    }
    assert(!new_allocation);


    /* Write the new reftable */
    ret = qcow2_pre_write_overlap_check(bs, 0, new_reftable_offset,
                                        new_reftable_size * sizeof(uint64_t));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Overlap check failed");
        goto done;
    }

    for (i = 0; i < new_reftable_size; i++) {
        cpu_to_be64s(&new_reftable[i]);
    }

    ret = bdrv_pwrite(bs->file, new_reftable_offset, new_reftable,
                      new_reftable_size * sizeof(uint64_t));

    for (i = 0; i < new_reftable_size; i++) {
        be64_to_cpus(&new_reftable[i]);
    }

    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write the new reftable");
        goto done;
    }


    /* Empty the refcount cache */
    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to flush the refblock cache");
        goto done;
    }

    /* Update the image header to point to the new reftable; this only updates
     * the fields which are relevant to qcow2_update_header(); other fields
     * such as s->refcount_table or s->refcount_bits stay stale for now
     * (because we have to restore everything if qcow2_update_header() fails) */
    old_refcount_order  = s->refcount_order;
    old_reftable_size   = s->refcount_table_size;
    old_reftable_offset = s->refcount_table_offset;

    s->refcount_order        = refcount_order;
    s->refcount_table_size   = new_reftable_size;
    s->refcount_table_offset = new_reftable_offset;

    ret = qcow2_update_header(bs);
    if (ret < 0) {
        s->refcount_order        = old_refcount_order;
        s->refcount_table_size   = old_reftable_size;
        s->refcount_table_offset = old_reftable_offset;
        error_setg_errno(errp, -ret, "Failed to update the qcow2 header");
        goto done;
    }

    /* Now update the rest of the in-memory information */
    old_reftable = s->refcount_table;
    s->refcount_table = new_reftable;
    update_max_refcount_table_index(s);

    s->refcount_bits = 1 << refcount_order;
    s->refcount_max = UINT64_C(1) << (s->refcount_bits - 1);
    s->refcount_max += s->refcount_max - 1;

    s->refcount_block_bits = s->cluster_bits - (refcount_order - 3);
    s->refcount_block_size = 1 << s->refcount_block_bits;

    s->get_refcount = new_get_refcount;
    s->set_refcount = new_set_refcount;

    /* For cleaning up all old refblocks and the old reftable below the "done"
     * label */
    new_reftable        = old_reftable;
    new_reftable_size   = old_reftable_size;
    new_reftable_offset = old_reftable_offset;

done:
    if (new_reftable) {
        /* On success, new_reftable actually points to the old reftable (and
         * new_reftable_size is the old reftable's size); but that is just
         * fine */
        for (i = 0; i < new_reftable_size; i++) {
            uint64_t offset = new_reftable[i] & REFT_OFFSET_MASK;
            if (offset) {
                qcow2_free_clusters(bs, offset, s->cluster_size,
                                    QCOW2_DISCARD_OTHER);
            }
        }
        g_free(new_reftable);

        if (new_reftable_offset > 0) {
            qcow2_free_clusters(bs, new_reftable_offset,
                                new_reftable_size * sizeof(uint64_t),
                                QCOW2_DISCARD_OTHER);
        }
    }

    qemu_vfree(new_refblock);
    return ret;
}
