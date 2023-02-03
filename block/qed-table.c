/*
 * QEMU Enhanced Disk Format Table I/O
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "block/block-io.h"
#include "trace.h"
#include "qemu/sockets.h" /* for EINPROGRESS on Windows */
#include "qed.h"
#include "qemu/bswap.h"
#include "qemu/memalign.h"

/* Called with table_lock held.  */
static int coroutine_fn GRAPH_RDLOCK
qed_read_table(BDRVQEDState *s, uint64_t offset, QEDTable *table)
{
    unsigned int bytes = s->header.cluster_size * s->header.table_size;

    int noffsets;
    int i, ret;

    trace_qed_read_table(s, offset, table);

    qemu_co_mutex_unlock(&s->table_lock);
    ret = bdrv_co_pread(s->bs->file, offset, bytes, table->offsets, 0);
    qemu_co_mutex_lock(&s->table_lock);
    if (ret < 0) {
        goto out;
    }

    /* Byteswap offsets */
    noffsets = bytes / sizeof(uint64_t);
    for (i = 0; i < noffsets; i++) {
        table->offsets[i] = le64_to_cpu(table->offsets[i]);
    }

    ret = 0;
out:
    /* Completion */
    trace_qed_read_table_cb(s, table, ret);
    return ret;
}

/**
 * Write out an updated part or all of a table
 *
 * @s:          QED state
 * @offset:     Offset of table in image file, in bytes
 * @table:      Table
 * @index:      Index of first element
 * @n:          Number of elements
 * @flush:      Whether or not to sync to disk
 *
 * Called with table_lock held.
 */
static int coroutine_fn GRAPH_RDLOCK
qed_write_table(BDRVQEDState *s, uint64_t offset, QEDTable *table,
                unsigned int index, unsigned int n, bool flush)
{
    unsigned int sector_mask = BDRV_SECTOR_SIZE / sizeof(uint64_t) - 1;
    unsigned int start, end, i;
    QEDTable *new_table;
    size_t len_bytes;
    int ret;

    trace_qed_write_table(s, offset, table, index, n);

    /* Calculate indices of the first and one after last elements */
    start = index & ~sector_mask;
    end = (index + n + sector_mask) & ~sector_mask;

    len_bytes = (end - start) * sizeof(uint64_t);

    new_table = qemu_blockalign(s->bs, len_bytes);

    /* Byteswap table */
    for (i = start; i < end; i++) {
        uint64_t le_offset = cpu_to_le64(table->offsets[i]);
        new_table->offsets[i - start] = le_offset;
    }

    /* Adjust for offset into table */
    offset += start * sizeof(uint64_t);

    qemu_co_mutex_unlock(&s->table_lock);
    ret = bdrv_co_pwrite(s->bs->file, offset, len_bytes, new_table->offsets, 0);
    qemu_co_mutex_lock(&s->table_lock);
    trace_qed_write_table_cb(s, table, flush, ret);
    if (ret < 0) {
        goto out;
    }

    if (flush) {
        ret = bdrv_co_flush(s->bs);
        if (ret < 0) {
            goto out;
        }
    }

    ret = 0;
out:
    qemu_vfree(new_table);
    return ret;
}

int coroutine_fn qed_read_l1_table_sync(BDRVQEDState *s)
{
    return qed_read_table(s, s->header.l1_table_offset, s->l1_table);
}

/* Called with table_lock held.  */
int coroutine_fn qed_write_l1_table(BDRVQEDState *s, unsigned int index,
                                    unsigned int n)
{
    BLKDBG_EVENT(s->bs->file, BLKDBG_L1_UPDATE);
    return qed_write_table(s, s->header.l1_table_offset,
                           s->l1_table, index, n, false);
}

int coroutine_fn qed_write_l1_table_sync(BDRVQEDState *s, unsigned int index,
                                         unsigned int n)
{
    return qed_write_l1_table(s, index, n);
}

/* Called with table_lock held.  */
int coroutine_fn qed_read_l2_table(BDRVQEDState *s, QEDRequest *request,
                                   uint64_t offset)
{
    int ret;

    qed_unref_l2_cache_entry(request->l2_table);

    /* Check for cached L2 entry */
    request->l2_table = qed_find_l2_cache_entry(&s->l2_cache, offset);
    if (request->l2_table) {
        return 0;
    }

    request->l2_table = qed_alloc_l2_cache_entry(&s->l2_cache);
    request->l2_table->table = qed_alloc_table(s);

    BLKDBG_EVENT(s->bs->file, BLKDBG_L2_LOAD);
    ret = qed_read_table(s, offset, request->l2_table->table);

    if (ret) {
        /* can't trust loaded L2 table anymore */
        qed_unref_l2_cache_entry(request->l2_table);
        request->l2_table = NULL;
    } else {
        request->l2_table->offset = offset;

        qed_commit_l2_cache_entry(&s->l2_cache, request->l2_table);

        /* This is guaranteed to succeed because we just committed the entry
         * to the cache.
         */
        request->l2_table = qed_find_l2_cache_entry(&s->l2_cache, offset);
        assert(request->l2_table != NULL);
    }

    return ret;
}

int coroutine_fn qed_read_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                                        uint64_t offset)
{
    return qed_read_l2_table(s, request, offset);
}

/* Called with table_lock held.  */
int coroutine_fn qed_write_l2_table(BDRVQEDState *s, QEDRequest *request,
                                    unsigned int index, unsigned int n,
                                    bool flush)
{
    BLKDBG_EVENT(s->bs->file, BLKDBG_L2_UPDATE);
    return qed_write_table(s, request->l2_table->offset,
                           request->l2_table->table, index, n, flush);
}

int coroutine_fn qed_write_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                                         unsigned int index, unsigned int n,
                                         bool flush)
{
    return qed_write_l2_table(s, request, index, n, flush);
}
