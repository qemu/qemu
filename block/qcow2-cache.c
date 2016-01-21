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

/* Needed for CONFIG_MADVISE */
#include "qemu/osdep.h"

#if defined(CONFIG_MADVISE) || defined(CONFIG_POSIX_MADVISE)
#include <sys/mman.h>
#endif

#include "block/block_int.h"
#include "qemu-common.h"
#include "qcow2.h"
#include "trace.h"

typedef struct Qcow2CachedTable {
    int64_t  offset;
    uint64_t lru_counter;
    int      ref;
    bool     dirty;
} Qcow2CachedTable;

struct Qcow2Cache {
    Qcow2CachedTable       *entries;
    struct Qcow2Cache      *depends;
    int                     size;
    bool                    depends_on_flush;
    void                   *table_array;
    uint64_t                lru_counter;
    uint64_t                cache_clean_lru_counter;
};

static inline void *qcow2_cache_get_table_addr(BlockDriverState *bs,
                    Qcow2Cache *c, int table)
{
    BDRVQcow2State *s = bs->opaque;
    return (uint8_t *) c->table_array + (size_t) table * s->cluster_size;
}

static inline int qcow2_cache_get_table_idx(BlockDriverState *bs,
                  Qcow2Cache *c, void *table)
{
    BDRVQcow2State *s = bs->opaque;
    ptrdiff_t table_offset = (uint8_t *) table - (uint8_t *) c->table_array;
    int idx = table_offset / s->cluster_size;
    assert(idx >= 0 && idx < c->size && table_offset % s->cluster_size == 0);
    return idx;
}

static void qcow2_cache_table_release(BlockDriverState *bs, Qcow2Cache *c,
                                      int i, int num_tables)
{
#if QEMU_MADV_DONTNEED != QEMU_MADV_INVALID
    BDRVQcow2State *s = bs->opaque;
    void *t = qcow2_cache_get_table_addr(bs, c, i);
    int align = getpagesize();
    size_t mem_size = (size_t) s->cluster_size * num_tables;
    size_t offset = QEMU_ALIGN_UP((uintptr_t) t, align) - (uintptr_t) t;
    size_t length = QEMU_ALIGN_DOWN(mem_size - offset, align);
    if (length > 0) {
        qemu_madvise((uint8_t *) t + offset, length, QEMU_MADV_DONTNEED);
    }
#endif
}

static inline bool can_clean_entry(Qcow2Cache *c, int i)
{
    Qcow2CachedTable *t = &c->entries[i];
    return t->ref == 0 && !t->dirty && t->offset != 0 &&
        t->lru_counter <= c->cache_clean_lru_counter;
}

void qcow2_cache_clean_unused(BlockDriverState *bs, Qcow2Cache *c)
{
    int i = 0;
    while (i < c->size) {
        int to_clean = 0;

        /* Skip the entries that we don't need to clean */
        while (i < c->size && !can_clean_entry(c, i)) {
            i++;
        }

        /* And count how many we can clean in a row */
        while (i < c->size && can_clean_entry(c, i)) {
            c->entries[i].offset = 0;
            c->entries[i].lru_counter = 0;
            i++;
            to_clean++;
        }

        if (to_clean > 0) {
            qcow2_cache_table_release(bs, c, i - to_clean, to_clean);
        }
    }

    c->cache_clean_lru_counter = c->lru_counter;
}

Qcow2Cache *qcow2_cache_create(BlockDriverState *bs, int num_tables)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2Cache *c;

    c = g_new0(Qcow2Cache, 1);
    c->size = num_tables;
    c->entries = g_try_new0(Qcow2CachedTable, num_tables);
    c->table_array = qemu_try_blockalign(bs->file->bs,
                                         (size_t) num_tables * s->cluster_size);

    if (!c->entries || !c->table_array) {
        qemu_vfree(c->table_array);
        g_free(c->entries);
        g_free(c);
        c = NULL;
    }

    return c;
}

int qcow2_cache_destroy(BlockDriverState *bs, Qcow2Cache *c)
{
    int i;

    for (i = 0; i < c->size; i++) {
        assert(c->entries[i].ref == 0);
    }

    qemu_vfree(c->table_array);
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
    BDRVQcow2State *s = bs->opaque;
    int ret = 0;

    if (!c->entries[i].dirty || !c->entries[i].offset) {
        return 0;
    }

    trace_qcow2_cache_entry_flush(qemu_coroutine_self(),
                                  c == s->l2_table_cache, i);

    if (c->depends) {
        ret = qcow2_cache_flush_dependency(bs, c);
    } else if (c->depends_on_flush) {
        ret = bdrv_flush(bs->file->bs);
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

    ret = bdrv_pwrite(bs->file->bs, c->entries[i].offset,
                      qcow2_cache_get_table_addr(bs, c, i), s->cluster_size);
    if (ret < 0) {
        return ret;
    }

    c->entries[i].dirty = false;

    return 0;
}

int qcow2_cache_flush(BlockDriverState *bs, Qcow2Cache *c)
{
    BDRVQcow2State *s = bs->opaque;
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
        ret = bdrv_flush(bs->file->bs);
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
        c->entries[i].lru_counter = 0;
    }

    qcow2_cache_table_release(bs, c, 0, c->size);

    c->lru_counter = 0;

    return 0;
}

static int qcow2_cache_do_get(BlockDriverState *bs, Qcow2Cache *c,
    uint64_t offset, void **table, bool read_from_disk)
{
    BDRVQcow2State *s = bs->opaque;
    int i;
    int ret;
    int lookup_index;
    uint64_t min_lru_counter = UINT64_MAX;
    int min_lru_index = -1;

    trace_qcow2_cache_get(qemu_coroutine_self(), c == s->l2_table_cache,
                          offset, read_from_disk);

    /* Check if the table is already cached */
    i = lookup_index = (offset / s->cluster_size * 4) % c->size;
    do {
        const Qcow2CachedTable *t = &c->entries[i];
        if (t->offset == offset) {
            goto found;
        }
        if (t->ref == 0 && t->lru_counter < min_lru_counter) {
            min_lru_counter = t->lru_counter;
            min_lru_index = i;
        }
        if (++i == c->size) {
            i = 0;
        }
    } while (i != lookup_index);

    if (min_lru_index == -1) {
        /* This can't happen in current synchronous code, but leave the check
         * here as a reminder for whoever starts using AIO with the cache */
        abort();
    }

    /* Cache miss: write a table back and replace it */
    i = min_lru_index;
    trace_qcow2_cache_get_replace_entry(qemu_coroutine_self(),
                                        c == s->l2_table_cache, i);

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

        ret = bdrv_pread(bs->file->bs, offset,
                         qcow2_cache_get_table_addr(bs, c, i),
                         s->cluster_size);
        if (ret < 0) {
            return ret;
        }
    }

    c->entries[i].offset = offset;

    /* And return the right table */
found:
    c->entries[i].ref++;
    *table = qcow2_cache_get_table_addr(bs, c, i);

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

void qcow2_cache_put(BlockDriverState *bs, Qcow2Cache *c, void **table)
{
    int i = qcow2_cache_get_table_idx(bs, c, *table);

    c->entries[i].ref--;
    *table = NULL;

    if (c->entries[i].ref == 0) {
        c->entries[i].lru_counter = ++c->lru_counter;
    }

    assert(c->entries[i].ref >= 0);
}

void qcow2_cache_entry_mark_dirty(BlockDriverState *bs, Qcow2Cache *c,
     void *table)
{
    int i = qcow2_cache_get_table_idx(bs, c, table);
    assert(c->entries[i].offset != 0);
    c->entries[i].dirty = true;
}
