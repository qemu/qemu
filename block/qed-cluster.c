/*
 * QEMU Enhanced Disk Format Cluster functions
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

#include "qed.h"

/**
 * Count the number of contiguous data clusters
 *
 * @s:              QED state
 * @table:          L2 table
 * @index:          First cluster index
 * @n:              Maximum number of clusters
 * @offset:         Set to first cluster offset
 *
 * This function scans tables for contiguous clusters.  A contiguous run of
 * clusters may be allocated, unallocated, or zero.
 */
static unsigned int qed_count_contiguous_clusters(BDRVQEDState *s,
                                                  QEDTable *table,
                                                  unsigned int index,
                                                  unsigned int n,
                                                  uint64_t *offset)
{
    unsigned int end = MIN(index + n, s->table_nelems);
    uint64_t last = table->offsets[index];
    unsigned int i;

    *offset = last;

    for (i = index + 1; i < end; i++) {
        if (qed_offset_is_unalloc_cluster(last)) {
            /* Counting unallocated clusters */
            if (!qed_offset_is_unalloc_cluster(table->offsets[i])) {
                break;
            }
        } else if (qed_offset_is_zero_cluster(last)) {
            /* Counting zero clusters */
            if (!qed_offset_is_zero_cluster(table->offsets[i])) {
                break;
            }
        } else {
            /* Counting allocated clusters */
            if (table->offsets[i] != last + s->header.cluster_size) {
                break;
            }
            last = table->offsets[i];
        }
    }
    return i - index;
}

typedef struct {
    BDRVQEDState *s;
    uint64_t pos;
    size_t len;

    QEDRequest *request;

    /* User callback */
    QEDFindClusterFunc *cb;
    void *opaque;
} QEDFindClusterCB;

static void qed_find_cluster_cb(void *opaque, int ret)
{
    QEDFindClusterCB *find_cluster_cb = opaque;
    BDRVQEDState *s = find_cluster_cb->s;
    QEDRequest *request = find_cluster_cb->request;
    uint64_t offset = 0;
    size_t len = 0;
    unsigned int index;
    unsigned int n;

    if (ret) {
        goto out;
    }

    index = qed_l2_index(s, find_cluster_cb->pos);
    n = qed_bytes_to_clusters(s,
                              qed_offset_into_cluster(s, find_cluster_cb->pos) +
                              find_cluster_cb->len);
    n = qed_count_contiguous_clusters(s, request->l2_table->table,
                                      index, n, &offset);

    if (qed_offset_is_unalloc_cluster(offset)) {
        ret = QED_CLUSTER_L2;
    } else if (qed_offset_is_zero_cluster(offset)) {
        ret = QED_CLUSTER_ZERO;
    } else if (qed_check_cluster_offset(s, offset)) {
        ret = QED_CLUSTER_FOUND;
    } else {
        ret = -EINVAL;
    }

    len = MIN(find_cluster_cb->len, n * s->header.cluster_size -
              qed_offset_into_cluster(s, find_cluster_cb->pos));

out:
    find_cluster_cb->cb(find_cluster_cb->opaque, ret, offset, len);
    g_free(find_cluster_cb);
}

/**
 * Find the offset of a data cluster
 *
 * @s:          QED state
 * @request:    L2 cache entry
 * @pos:        Byte position in device
 * @len:        Number of bytes
 * @cb:         Completion function
 * @opaque:     User data for completion function
 *
 * This function translates a position in the block device to an offset in the
 * image file.  It invokes the cb completion callback to report back the
 * translated offset or unallocated range in the image file.
 *
 * If the L2 table exists, request->l2_table points to the L2 table cache entry
 * and the caller must free the reference when they are finished.  The cache
 * entry is exposed in this way to avoid callers having to read the L2 table
 * again later during request processing.  If request->l2_table is non-NULL it
 * will be unreferenced before taking on the new cache entry.
 */
void qed_find_cluster(BDRVQEDState *s, QEDRequest *request, uint64_t pos,
                      size_t len, QEDFindClusterFunc *cb, void *opaque)
{
    QEDFindClusterCB *find_cluster_cb;
    uint64_t l2_offset;

    /* Limit length to L2 boundary.  Requests are broken up at the L2 boundary
     * so that a request acts on one L2 table at a time.
     */
    len = MIN(len, (((pos >> s->l1_shift) + 1) << s->l1_shift) - pos);

    l2_offset = s->l1_table->offsets[qed_l1_index(s, pos)];
    if (qed_offset_is_unalloc_cluster(l2_offset)) {
        cb(opaque, QED_CLUSTER_L1, 0, len);
        return;
    }
    if (!qed_check_table_offset(s, l2_offset)) {
        cb(opaque, -EINVAL, 0, 0);
        return;
    }

    find_cluster_cb = g_malloc(sizeof(*find_cluster_cb));
    find_cluster_cb->s = s;
    find_cluster_cb->pos = pos;
    find_cluster_cb->len = len;
    find_cluster_cb->cb = cb;
    find_cluster_cb->opaque = opaque;
    find_cluster_cb->request = request;

    qed_read_l2_table(s, request, l2_offset,
                      qed_find_cluster_cb, find_cluster_cb);
}
