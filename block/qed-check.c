/*
 * QEMU Enhanced Disk Format Consistency Check
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qed.h"

typedef struct {
    BDRVQEDState *s;
    BdrvCheckResult *result;
    bool fix;                           /* whether to fix invalid offsets */

    uint64_t nclusters;
    uint32_t *used_clusters;            /* referenced cluster bitmap */

    QEDRequest request;
} QEDCheck;

static bool qed_test_bit(uint32_t *bitmap, uint64_t n) {
    return !!(bitmap[n / 32] & (1 << (n % 32)));
}

static void qed_set_bit(uint32_t *bitmap, uint64_t n) {
    bitmap[n / 32] |= 1 << (n % 32);
}

/**
 * Set bitmap bits for clusters
 *
 * @check:          Check structure
 * @offset:         Starting offset in bytes
 * @n:              Number of clusters
 */
static bool qed_set_used_clusters(QEDCheck *check, uint64_t offset,
                                  unsigned int n)
{
    uint64_t cluster = qed_bytes_to_clusters(check->s, offset);
    unsigned int corruptions = 0;

    while (n-- != 0) {
        /* Clusters should only be referenced once */
        if (qed_test_bit(check->used_clusters, cluster)) {
            corruptions++;
        }

        qed_set_bit(check->used_clusters, cluster);
        cluster++;
    }

    check->result->corruptions += corruptions;
    return corruptions == 0;
}

/**
 * Check an L2 table
 *
 * @ret:            Number of invalid cluster offsets
 */
static unsigned int qed_check_l2_table(QEDCheck *check, QEDTable *table)
{
    BDRVQEDState *s = check->s;
    unsigned int i, num_invalid = 0;

    for (i = 0; i < s->table_nelems; i++) {
        uint64_t offset = table->offsets[i];

        if (qed_offset_is_unalloc_cluster(offset) ||
            qed_offset_is_zero_cluster(offset)) {
            continue;
        }

        /* Detect invalid cluster offset */
        if (!qed_check_cluster_offset(s, offset)) {
            if (check->fix) {
                table->offsets[i] = 0;
            } else {
                check->result->corruptions++;
            }

            num_invalid++;
            continue;
        }

        qed_set_used_clusters(check, offset, 1);
    }

    return num_invalid;
}

/**
 * Descend tables and check each cluster is referenced once only
 */
static int qed_check_l1_table(QEDCheck *check, QEDTable *table)
{
    BDRVQEDState *s = check->s;
    unsigned int i, num_invalid_l1 = 0;
    int ret, last_error = 0;

    /* Mark L1 table clusters used */
    qed_set_used_clusters(check, s->header.l1_table_offset,
                          s->header.table_size);

    for (i = 0; i < s->table_nelems; i++) {
        unsigned int num_invalid_l2;
        uint64_t offset = table->offsets[i];

        if (qed_offset_is_unalloc_cluster(offset)) {
            continue;
        }

        /* Detect invalid L2 offset */
        if (!qed_check_table_offset(s, offset)) {
            /* Clear invalid offset */
            if (check->fix) {
                table->offsets[i] = 0;
            } else {
                check->result->corruptions++;
            }

            num_invalid_l1++;
            continue;
        }

        if (!qed_set_used_clusters(check, offset, s->header.table_size)) {
            continue; /* skip an invalid table */
        }

        ret = qed_read_l2_table_sync(s, &check->request, offset);
        if (ret) {
            check->result->check_errors++;
            last_error = ret;
            continue;
        }

        num_invalid_l2 = qed_check_l2_table(check,
                                            check->request.l2_table->table);

        /* Write out fixed L2 table */
        if (num_invalid_l2 > 0 && check->fix) {
            ret = qed_write_l2_table_sync(s, &check->request, 0,
                                          s->table_nelems, false);
            if (ret) {
                check->result->check_errors++;
                last_error = ret;
                continue;
            }
        }
    }

    /* Drop reference to final table */
    qed_unref_l2_cache_entry(check->request.l2_table);
    check->request.l2_table = NULL;

    /* Write out fixed L1 table */
    if (num_invalid_l1 > 0 && check->fix) {
        ret = qed_write_l1_table_sync(s, 0, s->table_nelems);
        if (ret) {
            check->result->check_errors++;
            last_error = ret;
        }
    }

    return last_error;
}

/**
 * Check for unreferenced (leaked) clusters
 */
static void qed_check_for_leaks(QEDCheck *check)
{
    BDRVQEDState *s = check->s;
    uint64_t i;

    for (i = s->header.header_size; i < check->nclusters; i++) {
        if (!qed_test_bit(check->used_clusters, i)) {
            check->result->leaks++;
        }
    }
}

int qed_check(BDRVQEDState *s, BdrvCheckResult *result, bool fix)
{
    QEDCheck check = {
        .s = s,
        .result = result,
        .nclusters = qed_bytes_to_clusters(s, s->file_size),
        .request = { .l2_table = NULL },
        .fix = fix,
    };
    int ret;

    check.used_clusters = qemu_mallocz(((check.nclusters + 31) / 32) *
                                       sizeof(check.used_clusters[0]));

    ret = qed_check_l1_table(&check, s->l1_table);
    if (ret == 0) {
        /* Only check for leaks if entire image was scanned successfully */
        qed_check_for_leaks(&check);
    }

    qemu_free(check.used_clusters);
    return ret;
}
