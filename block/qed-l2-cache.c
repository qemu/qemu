/*
 * QEMU Enhanced Disk Format L2 Cache
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

/*
 * L2 table cache usage is as follows:
 *
 * An open image has one L2 table cache that is used to avoid accessing the
 * image file for recently referenced L2 tables.
 *
 * Cluster offset lookup translates the logical offset within the block device
 * to a cluster offset within the image file.  This is done by indexing into
 * the L1 and L2 tables which store cluster offsets.  It is here where the L2
 * table cache serves up recently referenced L2 tables.
 *
 * If there is a cache miss, that L2 table is read from the image file and
 * committed to the cache.  Subsequent accesses to that L2 table will be served
 * from the cache until the table is evicted from the cache.
 *
 * L2 tables are also committed to the cache when new L2 tables are allocated
 * in the image file.  Since the L2 table cache is write-through, the new L2
 * table is first written out to the image file and then committed to the
 * cache.
 *
 * Multiple I/O requests may be using an L2 table cache entry at any given
 * time.  That means an entry may be in use across several requests and
 * reference counting is needed to free the entry at the correct time.  In
 * particular, an entry evicted from the cache will only be freed once all
 * references are dropped.
 *
 * An in-flight I/O request will hold a reference to a L2 table cache entry for
 * the period during which it needs to access the L2 table.  This includes
 * cluster offset lookup, L2 table allocation, and L2 table update when a new
 * data cluster has been allocated.
 *
 * An interesting case occurs when two requests need to access an L2 table that
 * is not in the cache.  Since the operation to read the table from the image
 * file takes some time to complete, both requests may see a cache miss and
 * start reading the L2 table from the image file.  The first to finish will
 * commit its L2 table into the cache.  When the second tries to commit its
 * table will be deleted in favor of the existing cache entry.
 */

#include "qemu/osdep.h"
#include "qemu/memalign.h"
#include "trace.h"
#include "qed.h"

/* Each L2 holds 2GB so this let's us fully cache a 100GB disk */
#define MAX_L2_CACHE_SIZE 50

/**
 * Initialize the L2 cache
 */
void qed_init_l2_cache(L2TableCache *l2_cache)
{
    QTAILQ_INIT(&l2_cache->entries);
    l2_cache->n_entries = 0;
}

/**
 * Free the L2 cache
 */
void qed_free_l2_cache(L2TableCache *l2_cache)
{
    CachedL2Table *entry, *next_entry;

    QTAILQ_FOREACH_SAFE(entry, &l2_cache->entries, node, next_entry) {
        qemu_vfree(entry->table);
        g_free(entry);
    }
}

/**
 * Allocate an uninitialized entry from the cache
 *
 * The returned entry has a reference count of 1 and is owned by the caller.
 * The caller must allocate the actual table field for this entry and it must
 * be freeable using qemu_vfree().
 */
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache)
{
    CachedL2Table *entry;

    entry = g_malloc0(sizeof(*entry));
    entry->ref++;

    trace_qed_alloc_l2_cache_entry(l2_cache, entry);

    return entry;
}

/**
 * Decrease an entry's reference count and free if necessary when the reference
 * count drops to zero.
 *
 * Called with table_lock held.
 */
void qed_unref_l2_cache_entry(CachedL2Table *entry)
{
    if (!entry) {
        return;
    }

    entry->ref--;
    trace_qed_unref_l2_cache_entry(entry, entry->ref);
    if (entry->ref == 0) {
        qemu_vfree(entry->table);
        g_free(entry);
    }
}

/**
 * Find an entry in the L2 cache.  This may return NULL and it's up to the
 * caller to satisfy the cache miss.
 *
 * For a cached entry, this function increases the reference count and returns
 * the entry.
 *
 * Called with table_lock held.
 */
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset)
{
    CachedL2Table *entry;

    QTAILQ_FOREACH(entry, &l2_cache->entries, node) {
        if (entry->offset == offset) {
            trace_qed_find_l2_cache_entry(l2_cache, entry, offset, entry->ref);
            entry->ref++;
            return entry;
        }
    }
    return NULL;
}

/**
 * Commit an L2 cache entry into the cache.  This is meant to be used as part of
 * the process to satisfy a cache miss.  A caller would allocate an entry which
 * is not actually in the L2 cache and then once the entry was valid and
 * present on disk, the entry can be committed into the cache.
 *
 * Since the cache is write-through, it's important that this function is not
 * called until the entry is present on disk and the L1 has been updated to
 * point to the entry.
 *
 * N.B. This function steals a reference to the l2_table from the caller so the
 * caller must obtain a new reference by issuing a call to
 * qed_find_l2_cache_entry().
 *
 * Called with table_lock held.
 */
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table)
{
    CachedL2Table *entry;

    entry = qed_find_l2_cache_entry(l2_cache, l2_table->offset);
    if (entry) {
        qed_unref_l2_cache_entry(entry);
        qed_unref_l2_cache_entry(l2_table);
        return;
    }

    /* Evict an unused cache entry so we have space.  If all entries are in use
     * we can grow the cache temporarily and we try to shrink back down later.
     */
    if (l2_cache->n_entries >= MAX_L2_CACHE_SIZE) {
        CachedL2Table *next;
        QTAILQ_FOREACH_SAFE(entry, &l2_cache->entries, node, next) {
            if (entry->ref > 1) {
                continue;
            }

            QTAILQ_REMOVE(&l2_cache->entries, entry, node);
            l2_cache->n_entries--;
            qed_unref_l2_cache_entry(entry);

            /* Stop evicting when we've shrunk back to max size */
            if (l2_cache->n_entries < MAX_L2_CACHE_SIZE) {
                break;
            }
        }
    }

    l2_cache->n_entries++;
    QTAILQ_INSERT_TAIL(&l2_cache->entries, l2_table, node);
}
