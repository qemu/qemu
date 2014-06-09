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

#include "trace.h"
#include "qemu/sockets.h" /* for EINPROGRESS on Windows */
#include "qed.h"

typedef struct {
    GenericCB gencb;
    BDRVQEDState *s;
    QEDTable *table;

    struct iovec iov;
    QEMUIOVector qiov;
} QEDReadTableCB;

static void qed_read_table_cb(void *opaque, int ret)
{
    QEDReadTableCB *read_table_cb = opaque;
    QEDTable *table = read_table_cb->table;
    int noffsets = read_table_cb->qiov.size / sizeof(uint64_t);
    int i;

    /* Handle I/O error */
    if (ret) {
        goto out;
    }

    /* Byteswap offsets */
    for (i = 0; i < noffsets; i++) {
        table->offsets[i] = le64_to_cpu(table->offsets[i]);
    }

out:
    /* Completion */
    trace_qed_read_table_cb(read_table_cb->s, read_table_cb->table, ret);
    gencb_complete(&read_table_cb->gencb, ret);
}

static void qed_read_table(BDRVQEDState *s, uint64_t offset, QEDTable *table,
                           BlockDriverCompletionFunc *cb, void *opaque)
{
    QEDReadTableCB *read_table_cb = gencb_alloc(sizeof(*read_table_cb),
                                                cb, opaque);
    QEMUIOVector *qiov = &read_table_cb->qiov;

    trace_qed_read_table(s, offset, table);

    read_table_cb->s = s;
    read_table_cb->table = table;
    read_table_cb->iov.iov_base = table->offsets,
    read_table_cb->iov.iov_len = s->header.cluster_size * s->header.table_size,

    qemu_iovec_init_external(qiov, &read_table_cb->iov, 1);
    bdrv_aio_readv(s->bs->file, offset / BDRV_SECTOR_SIZE, qiov,
                   qiov->size / BDRV_SECTOR_SIZE,
                   qed_read_table_cb, read_table_cb);
}

typedef struct {
    GenericCB gencb;
    BDRVQEDState *s;
    QEDTable *orig_table;
    QEDTable *table;
    bool flush;             /* flush after write? */

    struct iovec iov;
    QEMUIOVector qiov;
} QEDWriteTableCB;

static void qed_write_table_cb(void *opaque, int ret)
{
    QEDWriteTableCB *write_table_cb = opaque;

    trace_qed_write_table_cb(write_table_cb->s,
                             write_table_cb->orig_table,
                             write_table_cb->flush,
                             ret);

    if (ret) {
        goto out;
    }

    if (write_table_cb->flush) {
        /* We still need to flush first */
        write_table_cb->flush = false;
        bdrv_aio_flush(write_table_cb->s->bs, qed_write_table_cb,
                       write_table_cb);
        return;
    }

out:
    qemu_vfree(write_table_cb->table);
    gencb_complete(&write_table_cb->gencb, ret);
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
 * @cb:         Completion function
 * @opaque:     Argument for completion function
 */
static void qed_write_table(BDRVQEDState *s, uint64_t offset, QEDTable *table,
                            unsigned int index, unsigned int n, bool flush,
                            BlockDriverCompletionFunc *cb, void *opaque)
{
    QEDWriteTableCB *write_table_cb;
    unsigned int sector_mask = BDRV_SECTOR_SIZE / sizeof(uint64_t) - 1;
    unsigned int start, end, i;
    size_t len_bytes;

    trace_qed_write_table(s, offset, table, index, n);

    /* Calculate indices of the first and one after last elements */
    start = index & ~sector_mask;
    end = (index + n + sector_mask) & ~sector_mask;

    len_bytes = (end - start) * sizeof(uint64_t);

    write_table_cb = gencb_alloc(sizeof(*write_table_cb), cb, opaque);
    write_table_cb->s = s;
    write_table_cb->orig_table = table;
    write_table_cb->flush = flush;
    write_table_cb->table = qemu_blockalign(s->bs, len_bytes);
    write_table_cb->iov.iov_base = write_table_cb->table->offsets;
    write_table_cb->iov.iov_len = len_bytes;
    qemu_iovec_init_external(&write_table_cb->qiov, &write_table_cb->iov, 1);

    /* Byteswap table */
    for (i = start; i < end; i++) {
        uint64_t le_offset = cpu_to_le64(table->offsets[i]);
        write_table_cb->table->offsets[i - start] = le_offset;
    }

    /* Adjust for offset into table */
    offset += start * sizeof(uint64_t);

    bdrv_aio_writev(s->bs->file, offset / BDRV_SECTOR_SIZE,
                    &write_table_cb->qiov,
                    write_table_cb->qiov.size / BDRV_SECTOR_SIZE,
                    qed_write_table_cb, write_table_cb);
}

/**
 * Propagate return value from async callback
 */
static void qed_sync_cb(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

int qed_read_l1_table_sync(BDRVQEDState *s)
{
    int ret = -EINPROGRESS;

    qed_read_table(s, s->header.l1_table_offset,
                   s->l1_table, qed_sync_cb, &ret);
    while (ret == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(s->bs), true);
    }

    return ret;
}

void qed_write_l1_table(BDRVQEDState *s, unsigned int index, unsigned int n,
                        BlockDriverCompletionFunc *cb, void *opaque)
{
    BLKDBG_EVENT(s->bs->file, BLKDBG_L1_UPDATE);
    qed_write_table(s, s->header.l1_table_offset,
                    s->l1_table, index, n, false, cb, opaque);
}

int qed_write_l1_table_sync(BDRVQEDState *s, unsigned int index,
                            unsigned int n)
{
    int ret = -EINPROGRESS;

    qed_write_l1_table(s, index, n, qed_sync_cb, &ret);
    while (ret == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(s->bs), true);
    }

    return ret;
}

typedef struct {
    GenericCB gencb;
    BDRVQEDState *s;
    uint64_t l2_offset;
    QEDRequest *request;
} QEDReadL2TableCB;

static void qed_read_l2_table_cb(void *opaque, int ret)
{
    QEDReadL2TableCB *read_l2_table_cb = opaque;
    QEDRequest *request = read_l2_table_cb->request;
    BDRVQEDState *s = read_l2_table_cb->s;
    CachedL2Table *l2_table = request->l2_table;
    uint64_t l2_offset = read_l2_table_cb->l2_offset;

    if (ret) {
        /* can't trust loaded L2 table anymore */
        qed_unref_l2_cache_entry(l2_table);
        request->l2_table = NULL;
    } else {
        l2_table->offset = l2_offset;

        qed_commit_l2_cache_entry(&s->l2_cache, l2_table);

        /* This is guaranteed to succeed because we just committed the entry
         * to the cache.
         */
        request->l2_table = qed_find_l2_cache_entry(&s->l2_cache, l2_offset);
        assert(request->l2_table != NULL);
    }

    gencb_complete(&read_l2_table_cb->gencb, ret);
}

void qed_read_l2_table(BDRVQEDState *s, QEDRequest *request, uint64_t offset,
                       BlockDriverCompletionFunc *cb, void *opaque)
{
    QEDReadL2TableCB *read_l2_table_cb;

    qed_unref_l2_cache_entry(request->l2_table);

    /* Check for cached L2 entry */
    request->l2_table = qed_find_l2_cache_entry(&s->l2_cache, offset);
    if (request->l2_table) {
        cb(opaque, 0);
        return;
    }

    request->l2_table = qed_alloc_l2_cache_entry(&s->l2_cache);
    request->l2_table->table = qed_alloc_table(s);

    read_l2_table_cb = gencb_alloc(sizeof(*read_l2_table_cb), cb, opaque);
    read_l2_table_cb->s = s;
    read_l2_table_cb->l2_offset = offset;
    read_l2_table_cb->request = request;

    BLKDBG_EVENT(s->bs->file, BLKDBG_L2_LOAD);
    qed_read_table(s, offset, request->l2_table->table,
                   qed_read_l2_table_cb, read_l2_table_cb);
}

int qed_read_l2_table_sync(BDRVQEDState *s, QEDRequest *request, uint64_t offset)
{
    int ret = -EINPROGRESS;

    qed_read_l2_table(s, request, offset, qed_sync_cb, &ret);
    while (ret == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(s->bs), true);
    }

    return ret;
}

void qed_write_l2_table(BDRVQEDState *s, QEDRequest *request,
                        unsigned int index, unsigned int n, bool flush,
                        BlockDriverCompletionFunc *cb, void *opaque)
{
    BLKDBG_EVENT(s->bs->file, BLKDBG_L2_UPDATE);
    qed_write_table(s, request->l2_table->offset,
                    request->l2_table->table, index, n, flush, cb, opaque);
}

int qed_write_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                            unsigned int index, unsigned int n, bool flush)
{
    int ret = -EINPROGRESS;

    qed_write_l2_table(s, request, index, n, flush, qed_sync_cb, &ret);
    while (ret == -EINPROGRESS) {
        aio_poll(bdrv_get_aio_context(s->bs), true);
    }

    return ret;
}
