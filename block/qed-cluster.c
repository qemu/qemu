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

#include "qemu/osdep.h"
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

/**
 * Find the offset of a data cluster
 *
 * @s:          QED state
 * @request:    L2 cache entry
 * @pos:        Byte position in device
 * @len:        Number of bytes (may be shortened on return)
 * @img_offset: Contains offset in the image file on success
 *
 * This function translates a position in the block device to an offset in the
 * image file. The translated offset or unallocated range in the image file is
 * reported back in *img_offset and *len.
 *
 * If the L2 table exists, request->l2_table points to the L2 table cache entry
 * and the caller must free the reference when they are finished.  The cache
 * entry is exposed in this way to avoid callers having to read the L2 table
 * again later during request processing.  If request->l2_table is non-NULL it
 * will be unreferenced before taking on the new cache entry.
 *
 * On success QED_CLUSTER_FOUND is returned and img_offset/len are a contiguous
 * range in the image file.
 *
 * On failure QED_CLUSTER_L2 or QED_CLUSTER_L1 is returned for missing L2 or L1
 * table offset, respectively. len is number of contiguous unallocated bytes.
 *
 * Called with table_lock held.
 */
int coroutine_fn qed_find_cluster(BDRVQEDState *s, QEDRequest *request,
                                  uint64_t pos, size_t *len,
                                  uint64_t *img_offset)
{
    uint64_t l2_offset;
    uint64_t offset = 0;
    unsigned int index;
    unsigned int n;
    int ret;

    /* Limit length to L2 boundary.  Requests are broken up at the L2 boundary
     * so that a request acts on one L2 table at a time.
     */
    *len = MIN(*len, (((pos >> s->l1_shift) + 1) << s->l1_shift) - pos);

    l2_offset = s->l1_table->offsets[qed_l1_index(s, pos)];
    if (qed_offset_is_unalloc_cluster(l2_offset)) {
        *img_offset = 0;
        return QED_CLUSTER_L1;
    }
    if (!qed_check_table_offset(s, l2_offset)) {
        *img_offset = *len = 0;
        return -EINVAL;
    }

    ret = qed_read_l2_table(s, request, l2_offset);
    if (ret) {
        goto out;
    }

    index = qed_l2_index(s, pos);
    n = qed_bytes_to_clusters(s, qed_offset_into_cluster(s, pos) + *len);
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

    *len = MIN(*len,
               n * s->header.cluster_size - qed_offset_into_cluster(s, pos));

out:
    *img_offset = offset;
    return ret;
}
