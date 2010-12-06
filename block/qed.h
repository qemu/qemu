/*
 * QEMU Enhanced Disk Format
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

#ifndef BLOCK_QED_H
#define BLOCK_QED_H

#include "block_int.h"

/* The layout of a QED file is as follows:
 *
 * +--------+----------+----------+----------+-----+
 * | header | L1 table | cluster0 | cluster1 | ... |
 * +--------+----------+----------+----------+-----+
 *
 * There is a 2-level pagetable for cluster allocation:
 *
 *                     +----------+
 *                     | L1 table |
 *                     +----------+
 *                ,------'  |  '------.
 *           +----------+   |    +----------+
 *           | L2 table |  ...   | L2 table |
 *           +----------+        +----------+
 *       ,------'  |  '------.
 *  +----------+   |    +----------+
 *  |   Data   |  ...   |   Data   |
 *  +----------+        +----------+
 *
 * The L1 table is fixed size and always present.  L2 tables are allocated on
 * demand.  The L1 table size determines the maximum possible image size; it
 * can be influenced using the cluster_size and table_size values.
 *
 * All fields are little-endian on disk.
 */

enum {
    QED_MAGIC = 'Q' | 'E' << 8 | 'D' << 16 | '\0' << 24,

    /* The image supports a backing file */
    QED_F_BACKING_FILE = 0x01,

    /* The backing file format must not be probed, treat as raw image */
    QED_F_BACKING_FORMAT_NO_PROBE = 0x04,

    /* Feature bits must be used when the on-disk format changes */
    QED_FEATURE_MASK = QED_F_BACKING_FILE | /* supported feature bits */
                       QED_F_BACKING_FORMAT_NO_PROBE,
    QED_COMPAT_FEATURE_MASK = 0,            /* supported compat feature bits */
    QED_AUTOCLEAR_FEATURE_MASK = 0,         /* supported autoclear feature bits */

    /* Data is stored in groups of sectors called clusters.  Cluster size must
     * be large to avoid keeping too much metadata.  I/O requests that have
     * sub-cluster size will require read-modify-write.
     */
    QED_MIN_CLUSTER_SIZE = 4 * 1024, /* in bytes */
    QED_MAX_CLUSTER_SIZE = 64 * 1024 * 1024,
    QED_DEFAULT_CLUSTER_SIZE = 64 * 1024,

    /* Allocated clusters are tracked using a 2-level pagetable.  Table size is
     * a multiple of clusters so large maximum image sizes can be supported
     * without jacking up the cluster size too much.
     */
    QED_MIN_TABLE_SIZE = 1,        /* in clusters */
    QED_MAX_TABLE_SIZE = 16,
    QED_DEFAULT_TABLE_SIZE = 4,
};

typedef struct {
    uint32_t magic;                 /* QED\0 */

    uint32_t cluster_size;          /* in bytes */
    uint32_t table_size;            /* for L1 and L2 tables, in clusters */
    uint32_t header_size;           /* in clusters */

    uint64_t features;              /* format feature bits */
    uint64_t compat_features;       /* compatible feature bits */
    uint64_t autoclear_features;    /* self-resetting feature bits */

    uint64_t l1_table_offset;       /* in bytes */
    uint64_t image_size;            /* total logical image size, in bytes */

    /* if (features & QED_F_BACKING_FILE) */
    uint32_t backing_filename_offset; /* in bytes from start of header */
    uint32_t backing_filename_size;   /* in bytes */
} QEDHeader;

typedef struct {
    BlockDriverState *bs;           /* device */
    uint64_t file_size;             /* length of image file, in bytes */

    QEDHeader header;               /* always cpu-endian */
    uint32_t table_nelems;
    uint32_t l1_shift;
    uint32_t l2_shift;
    uint32_t l2_mask;
} BDRVQEDState;

/**
 * Round down to the start of a cluster
 */
static inline uint64_t qed_start_of_cluster(BDRVQEDState *s, uint64_t offset)
{
    return offset & ~(uint64_t)(s->header.cluster_size - 1);
}

/**
 * Test if a cluster offset is valid
 */
static inline bool qed_check_cluster_offset(BDRVQEDState *s, uint64_t offset)
{
    uint64_t header_size = (uint64_t)s->header.header_size *
                           s->header.cluster_size;

    if (offset & (s->header.cluster_size - 1)) {
        return false;
    }
    return offset >= header_size && offset < s->file_size;
}

/**
 * Test if a table offset is valid
 */
static inline bool qed_check_table_offset(BDRVQEDState *s, uint64_t offset)
{
    uint64_t end_offset = offset + (s->header.table_size - 1) *
                          s->header.cluster_size;

    /* Overflow check */
    if (end_offset <= offset) {
        return false;
    }

    return qed_check_cluster_offset(s, offset) &&
           qed_check_cluster_offset(s, end_offset);
}

#endif /* BLOCK_QED_H */
