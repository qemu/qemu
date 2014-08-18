/*
 * L2/refcount table cache for the QCOW2 format
 *
 * Copyright (c) 2010 Kevin Wolf <kwolf@redhat.com>
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

#include "block/block_int.h"
#include "qemu-common.h"
#include "qcow2.h"
#include "trace.h"

typedef struct Qcow2CachedTable {
    void*   table;
    int64_t offset;
    bool    dirty;
    int     cache_hits;
    int     ref;
} Qcow2CachedTable;

struct Qcow2Cache {
    Qcow2CachedTable*       entries;
    struct Qcow2Cache*      depends;
    int                     size;
    bool                    depends_on_flush;
};

Qcow2Cache *qcow2_cache_create(BlockDriverState *bs, int num_tables)
{
    BDRVQcowState *s = bs->opaque;
    Qcow2Cache *c;
    int i;

    c = g_new0(Qcow2Cache, 1);
    c->size = num_tables;
    c->entries = g_try_new0(Qcow2CachedTable, num_tables);
    if (!c->entries) {
        goto fail;
    }

    for (i = 0; i < c->size; i++) {
        c->entries[i].table = qemu_try_blockalign(bs->file, s->cluster_size);
        if (c->entries[i].table == NULL) {
            goto fail;
        }
    }

    return c;

fail:
    if (c->entries) {
        for (i = 0; i < c->size; i++) {
            qemu_vfree(c->entries[i].table);
        }
    }
    g_free(c->entries);
    g_free(c);
    return NULL;
}

int qcow2_cache_destroy(BlockDriverState* bs, Qcow2Cache *c)
{
    int i;

    for (i = 0; i < c->size; i++) {
        assert(c->entries[i].ref == 0);
        qemu_vfree(c->entries[i].table);
    }

    g_free(c->entries);
    g_free(c);

    return 0;
}

static int qcow2_cache_flush_dependency(BlockDriverState *bs, Qcow2Cache *c)
{
    int ret;

    ret = qcow2_cache_flush(bs, c->depends);
    if (ret < 0) {
        return ret;
    }

    c->depends = NULL;
    c->depends_on_flush = false;

    return 0;
}

static int qcow2_cache_entry_flush(BlockDriverState *bs, Qcow2Cache *c, int i)
{
    BDRVQcowState *s = bs->opaque;
    int ret = 0;

    if (!c->entries[i].dirty || !c->entries[i].offset) {
        return 0;
    }

    trace_qcow2_cache_entry_flush(qemu_coroutine_self(),
                                  c == s->l2_table_cache, i);

    if (c->depends) {
        ret = qcow2_cache_flush_dependency(bs, c);
    } else if (c->depends_on_flush) {
        ret = bdrv_flush(bs->file);
        if (ret >= 0) {
            c->depends_on_flush = false;
        }
    }

    if (ret < 0) {
        return ret;
    }

    if (c == s->refcount_block_cache) {
        ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_REFCOUNT_BLOCK,
                c->entries[i].offset, s->cluster_size);
    } else if (c == s->l2_table_cache) {
        ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_ACTIVE_L2,
                c->entries[i].offset, s->cluster_size);
    } else {
        ret = qcow2_pre_write_overlap_check(bs, 0,
                c->entries[i].offset, s->cluster_size);
    }

    if (ret < 0) {
        return ret;
    }

    if (c == s->refcount_block_cache) {
        BLKDBG_EVENT(bs->file, BLKDBG_REFBLOCK_UPDATE_PART);
    } else if (c == s->l2_table_cache) {
        BLKDBG_EVENT(bs->file, BLKDBG_L2_UPDATE);
    }

    ret = bdrv_pwrite(bs->file, c->entries[i].offset, c->entries[i].table,
        s->cluster_size);
    if (ret < 0) {
        return ret;
    }

    c->entries[i].dirty = false;

    return 0;
}

int qcow2_cache_flush(BlockDriverState *bs, Qcow2Cache *c)
{
    BDRVQcowState *s = bs->opaque;
    int result = 0;
    int ret;
    int i;

    trace_qcow2_cache_flush(qemu_coroutine_self(), c == s->l2_table_cache);

    for (i = 0; i < c->size; i++) {
        ret = qcow2_cache_entry_flush(bs, c, i);
        if (ret < 0 && result != -ENOSPC) {
            result = ret;
        }
    }

    if (result == 0) {
        ret = bdrv_flush(bs->file);
        if (ret < 0) {
            result = ret;
        }
    }

    return result;
}

int qcow2_cache_set_dependency(BlockDriverState *bs, Qcow2Cache *c,
    Qcow2Cache *dependency)
{
    int ret;

    if (dependency->depends) {
        ret = qcow2_cache_flush_dependency(bs, dependency);
        if (ret < 0) {
            return ret;
        }
    }

    if (c->depends && (c->depends != dependency)) {
        ret = qcow2_cache_flush_dependency(bs, c);
        if (ret < 0) {
            return ret;
        }
    }

    c->depends = dependency;
    return 0;
}

void qcow2_cache_depends_on_flush(Qcow2Cache *c)
{
    c->depends_on_flush = true;
}

int qcow2_cache_empty(BlockDriverState *bs, Qcow2Cache *c)
{
    int ret, i;

    ret = qcow2_cache_flush(bs, c);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < c->size; i++) {
        assert(c->entries[i].ref == 0);
        c->entries[i].offset = 0;
        c->entries[i].cache_hits = 0;
    }

    return 0;
}

static int qcow2_cache_find_entry_to_replace(Qcow2Cache *c)
{
    int i;
    int min_count = INT_MAX;
    int min_index = -1;


    for (i = 0; i < c->size; i++) {
        if (c->entries[i].ref) {
            continue;
        }

        if (c->entries[i].cache_hits < min_count) {
            min_index = i;
            min_count = c->entries[i].cache_hits;
        }

        /* Give newer hits priority */
        /* TODO Check how to optimize the replacement strategy */
        c->entries[i].cache_hits /= 2;
    }

    if (min_index == -1) {
        /* This can't happen in current synchronous code, but leave the check
         * here as a reminder for whoever starts using AIO with the cache */
        abort();
    }
    return min_index;
}

static int qcow2_cache_do_get(BlockDriverState *bs, Qcow2Cache *c,
    uint64_t offset, void **table, bool read_from_disk)
{
    BDRVQcowState *s = bs->opaque;
    int i;
    int ret;

    trace_qcow2_cache_get(qemu_coroutine_self(), c == s->l2_table_cache,
                          offset, read_from_disk);

    /* Check if the table is already cached */
    for (i = 0; i < c->size; i++) {
        if (c->entries[i].offset == offset) {
            goto found;
        }
    }

    /* If not, write a table back and replace it */
    i = qcow2_cache_find_entry_to_replace(c);
    trace_qcow2_cache_get_replace_entry(qemu_coroutine_self(),
                                        c == s->l2_table_cache, i);
    if (i < 0) {
        return i;
    }

    ret = qcow2_cache_entry_flush(bs, c, i);
    if (ret < 0) {
        return ret;
    }

    trace_qcow2_cache_get_read(qemu_coroutine_self(),
                               c == s->l2_table_cache, i);
    c->entries[i].offset = 0;
    if (read_from_disk) {
        if (c == s->l2_table_cache) {
            BLKDBG_EVENT(bs->file, BLKDBG_L2_LOAD);
        }

        ret = bdrv_pread(bs->file, offset, c->entries[i].table, s->cluster_size);
        if (ret < 0) {
            return ret;
        }
    }

    /* Give the table some hits for the start so that it won't be replaced
     * immediately. The number 32 is completely arbitrary. */
    c->entries[i].cache_hits = 32;
    c->entries[i].offset = offset;

    /* And return the right table */
found:
    c->entries[i].cache_hits++;
    c->entries[i].ref++;
    *table = c->entries[i].table;

    trace_qcow2_cache_get_done(qemu_coroutine_self(),
                               c == s->l2_table_cache, i);

    return 0;
}

int qcow2_cache_get(BlockDriverState *bs, Qcow2Cache *c, uint64_t offset,
    void **table)
{
    return qcow2_cache_do_get(bs, c, offset, table, true);
}

int qcow2_cache_get_empty(BlockDriverState *bs, Qcow2Cache *c, uint64_t offset,
    void **table)
{
    return qcow2_cache_do_get(bs, c, offset, table, false);
}

int qcow2_cache_put(BlockDriverState *bs, Qcow2Cache *c, void **table)
{
    int i;

    for (i = 0; i < c->size; i++) {
        if (c->entries[i].table == *table) {
            goto found;
        }
    }
    return -ENOENT;

found:
    c->entries[i].ref--;
    *table = NULL;

    assert(c->entries[i].ref >= 0);
    return 0;
}

void qcow2_cache_entry_mark_dirty(Qcow2Cache *c, void *table)
{
    int i;

    for (i = 0; i < c->size; i++) {
        if (c->entries[i].table == table) {
            goto found;
        }
    }
    abort();

found:
    c->entries[i].dirty = true;
}
