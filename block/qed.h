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

    /* The image needs a consistency check before use */
    QED_F_NEED_CHECK = 0x02,

    /* The backing file format must not be probed, treat as raw image */
    QED_F_BACKING_FORMAT_NO_PROBE = 0x04,

    /* Feature bits must be used when the on-disk format changes */
    QED_FEATURE_MASK = QED_F_BACKING_FILE | /* supported feature bits */
                       QED_F_NEED_CHECK |
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

    /* Delay to flush and clean image after last allocating write completes */
    QED_NEED_CHECK_TIMEOUT = 5,    /* in seconds */
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
    uint64_t offsets[0];            /* in bytes */
} QEDTable;

/* The L2 cache is a simple write-through cache for L2 structures */
typedef struct CachedL2Table {
    QEDTable *table;
    uint64_t offset;    /* offset=0 indicates an invalidate entry */
    QTAILQ_ENTRY(CachedL2Table) node;
    int ref;
} CachedL2Table;

typedef struct {
    QTAILQ_HEAD(, CachedL2Table) entries;
    unsigned int n_entries;
} L2TableCache;

typedef struct QEDRequest {
    CachedL2Table *l2_table;
} QEDRequest;

typedef struct QEDAIOCB {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int bh_ret;                     /* final return status for completion bh */
    QSIMPLEQ_ENTRY(QEDAIOCB) next;  /* next request */
    bool is_write;                  /* false - read, true - write */
    bool *finished;                 /* signal for cancel completion */
    uint64_t end_pos;               /* request end on block device, in bytes */

    /* User scatter-gather list */
    QEMUIOVector *qiov;
    size_t qiov_offset;             /* byte count already processed */

    /* Current cluster scatter-gather list */
    QEMUIOVector cur_qiov;
    uint64_t cur_pos;               /* position on block device, in bytes */
    uint64_t cur_cluster;           /* cluster offset in image file */
    unsigned int cur_nclusters;     /* number of clusters being accessed */
    int find_cluster_ret;           /* used for L1/L2 update */

    QEDRequest request;
} QEDAIOCB;

typedef struct {
    BlockDriverState *bs;           /* device */
    uint64_t file_size;             /* length of image file, in bytes */

    QEDHeader header;               /* always cpu-endian */
    QEDTable *l1_table;
    L2TableCache l2_cache;          /* l2 table cache */
    uint32_t table_nelems;
    uint32_t l1_shift;
    uint32_t l2_shift;
    uint32_t l2_mask;

    /* Allocating write request queue */
    QSIMPLEQ_HEAD(, QEDAIOCB) allocating_write_reqs;
    bool allocating_write_reqs_plugged;

    /* Periodic flush and clear need check flag */
    QEMUTimer *need_check_timer;

    Error *migration_blocker;
} BDRVQEDState;

enum {
    QED_CLUSTER_FOUND,         /* cluster found */
    QED_CLUSTER_ZERO,          /* zero cluster found */
    QED_CLUSTER_L2,            /* cluster missing in L2 */
    QED_CLUSTER_L1,            /* cluster missing in L1 */
};

/**
 * qed_find_cluster() completion callback
 *
 * @opaque:     User data for completion callback
 * @ret:        QED_CLUSTER_FOUND   Success
 *              QED_CLUSTER_L2      Data cluster unallocated in L2
 *              QED_CLUSTER_L1      L2 unallocated in L1
 *              -errno              POSIX error occurred
 * @offset:     Data cluster offset
 * @len:        Contiguous bytes starting from cluster offset
 *
 * This function is invoked when qed_find_cluster() completes.
 *
 * On success ret is QED_CLUSTER_FOUND and offset/len are a contiguous range
 * in the image file.
 *
 * On failure ret is QED_CLUSTER_L2 or QED_CLUSTER_L1 for missing L2 or L1
 * table offset, respectively.  len is number of contiguous unallocated bytes.
 */
typedef void QEDFindClusterFunc(void *opaque, int ret, uint64_t offset, size_t len);

/**
 * Generic callback for chaining async callbacks
 */
typedef struct {
    BlockDriverCompletionFunc *cb;
    void *opaque;
} GenericCB;

void *gencb_alloc(size_t len, BlockDriverCompletionFunc *cb, void *opaque);
void gencb_complete(void *opaque, int ret);

/**
 * L2 cache functions
 */
void qed_init_l2_cache(L2TableCache *l2_cache);
void qed_free_l2_cache(L2TableCache *l2_cache);
CachedL2Table *qed_alloc_l2_cache_entry(L2TableCache *l2_cache);
void qed_unref_l2_cache_entry(CachedL2Table *entry);
CachedL2Table *qed_find_l2_cache_entry(L2TableCache *l2_cache, uint64_t offset);
void qed_commit_l2_cache_entry(L2TableCache *l2_cache, CachedL2Table *l2_table);

/**
 * Table I/O functions
 */
int qed_read_l1_table_sync(BDRVQEDState *s);
void qed_write_l1_table(BDRVQEDState *s, unsigned int index, unsigned int n,
                        BlockDriverCompletionFunc *cb, void *opaque);
int qed_write_l1_table_sync(BDRVQEDState *s, unsigned int index,
                            unsigned int n);
int qed_read_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                           uint64_t offset);
void qed_read_l2_table(BDRVQEDState *s, QEDRequest *request, uint64_t offset,
                       BlockDriverCompletionFunc *cb, void *opaque);
void qed_write_l2_table(BDRVQEDState *s, QEDRequest *request,
                        unsigned int index, unsigned int n, bool flush,
                        BlockDriverCompletionFunc *cb, void *opaque);
int qed_write_l2_table_sync(BDRVQEDState *s, QEDRequest *request,
                            unsigned int index, unsigned int n, bool flush);

/**
 * Cluster functions
 */
void qed_find_cluster(BDRVQEDState *s, QEDRequest *request, uint64_t pos,
                      size_t len, QEDFindClusterFunc *cb, void *opaque);

/**
 * Consistency check
 */
int qed_check(BDRVQEDState *s, BdrvCheckResult *result, bool fix);

QEDTable *qed_alloc_table(BDRVQEDState *s);

/**
 * Round down to the start of a cluster
 */
static inline uint64_t qed_start_of_cluster(BDRVQEDState *s, uint64_t offset)
{
    return offset & ~(uint64_t)(s->header.cluster_size - 1);
}

static inline uint64_t qed_offset_into_cluster(BDRVQEDState *s, uint64_t offset)
{
    return offset & (s->header.cluster_size - 1);
}

static inline uint64_t qed_bytes_to_clusters(BDRVQEDState *s, uint64_t bytes)
{
    return qed_start_of_cluster(s, bytes + (s->header.cluster_size - 1)) /
           (s->header.cluster_size - 1);
}

static inline unsigned int qed_l1_index(BDRVQEDState *s, uint64_t pos)
{
    return pos >> s->l1_shift;
}

static inline unsigned int qed_l2_index(BDRVQEDState *s, uint64_t pos)
{
    return (pos >> s->l2_shift) & s->l2_mask;
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

static inline bool qed_offset_is_cluster_aligned(BDRVQEDState *s,
                                                 uint64_t offset)
{
    if (qed_offset_into_cluster(s, offset)) {
        return false;
    }
    return true;
}

static inline bool qed_offset_is_unalloc_cluster(uint64_t offset)
{
    if (offset == 0) {
        return true;
    }
    return false;
}

static inline bool qed_offset_is_zero_cluster(uint64_t offset)
{
    if (offset == 1) {
        return true;
    }
    return false;
}

#endif /* BLOCK_QED_H */
