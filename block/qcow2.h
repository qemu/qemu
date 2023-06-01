/*
 * Block driver for the QCOW version 2 format
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
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

#ifndef BLOCK_QCOW2_H
#define BLOCK_QCOW2_H

#include "crypto/block.h"
#include "qemu/coroutine.h"
#include "qemu/units.h"
#include "block/block_int.h"

//#define DEBUG_ALLOC
//#define DEBUG_ALLOC2
//#define DEBUG_EXT

#define QCOW_MAGIC (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xfb)

#define QCOW_CRYPT_NONE 0
#define QCOW_CRYPT_AES  1
#define QCOW_CRYPT_LUKS 2

#define QCOW_MAX_CRYPT_CLUSTERS 32
#define QCOW_MAX_SNAPSHOTS 65536

/* Field widths in qcow2 mean normal cluster offsets cannot reach
 * 64PB; depending on cluster size, compressed clusters can have a
 * smaller limit (64PB for up to 16k clusters, then ramps down to
 * 512TB for 2M clusters).  */
#define QCOW_MAX_CLUSTER_OFFSET ((1ULL << 56) - 1)

/* 8 MB refcount table is enough for 2 PB images at 64k cluster size
 * (128 GB for 512 byte clusters, 2 EB for 2 MB clusters) */
#define QCOW_MAX_REFTABLE_SIZE (8 * MiB)

/* 32 MB L1 table is enough for 2 PB images at 64k cluster size
 * (128 GB for 512 byte clusters, 2 EB for 2 MB clusters) */
#define QCOW_MAX_L1_SIZE (32 * MiB)

/* Allow for an average of 1k per snapshot table entry, should be plenty of
 * space for snapshot names and IDs */
#define QCOW_MAX_SNAPSHOTS_SIZE (1024 * QCOW_MAX_SNAPSHOTS)

/* Maximum amount of extra data per snapshot table entry to accept */
#define QCOW_MAX_SNAPSHOT_EXTRA_DATA 1024

/* Bitmap header extension constraints */
#define QCOW2_MAX_BITMAPS 65535
#define QCOW2_MAX_BITMAP_DIRECTORY_SIZE (1024 * QCOW2_MAX_BITMAPS)

/* Maximum of parallel sub-request per guest request */
#define QCOW2_MAX_WORKERS 8

/* indicate that the refcount of the referenced cluster is exactly one. */
#define QCOW_OFLAG_COPIED     (1ULL << 63)
/* indicate that the cluster is compressed (they never have the copied flag) */
#define QCOW_OFLAG_COMPRESSED (1ULL << 62)
/* The cluster reads as all zeros */
#define QCOW_OFLAG_ZERO (1ULL << 0)

#define QCOW_EXTL2_SUBCLUSTERS_PER_CLUSTER 32

/* The subcluster X [0..31] is allocated */
#define QCOW_OFLAG_SUB_ALLOC(X)   (1ULL << (X))
/* The subcluster X [0..31] reads as zeroes */
#define QCOW_OFLAG_SUB_ZERO(X)    (QCOW_OFLAG_SUB_ALLOC(X) << 32)
/* Subclusters [X, Y) (0 <= X <= Y <= 32) are allocated */
#define QCOW_OFLAG_SUB_ALLOC_RANGE(X, Y) \
    (QCOW_OFLAG_SUB_ALLOC(Y) - QCOW_OFLAG_SUB_ALLOC(X))
/* Subclusters [X, Y) (0 <= X <= Y <= 32) read as zeroes */
#define QCOW_OFLAG_SUB_ZERO_RANGE(X, Y) \
    (QCOW_OFLAG_SUB_ALLOC_RANGE(X, Y) << 32)
/* L2 entry bitmap with all allocation bits set */
#define QCOW_L2_BITMAP_ALL_ALLOC  (QCOW_OFLAG_SUB_ALLOC_RANGE(0, 32))
/* L2 entry bitmap with all "read as zeroes" bits set */
#define QCOW_L2_BITMAP_ALL_ZEROES (QCOW_OFLAG_SUB_ZERO_RANGE(0, 32))

/* Size of normal and extended L2 entries */
#define L2E_SIZE_NORMAL   (sizeof(uint64_t))
#define L2E_SIZE_EXTENDED (sizeof(uint64_t) * 2)

/* Size of L1 table entries */
#define L1E_SIZE (sizeof(uint64_t))

/* Size of reftable entries */
#define REFTABLE_ENTRY_SIZE (sizeof(uint64_t))

#define MIN_CLUSTER_BITS 9
#define MAX_CLUSTER_BITS 21

/* Defined in the qcow2 spec (compressed cluster descriptor) */
#define QCOW2_COMPRESSED_SECTOR_SIZE 512U

/* Must be at least 2 to cover COW */
#define MIN_L2_CACHE_SIZE 2 /* cache entries */

/* Must be at least 4 to cover all cases of refcount table growth */
#define MIN_REFCOUNT_CACHE_SIZE 4 /* clusters */

#ifdef CONFIG_LINUX
#define DEFAULT_L2_CACHE_MAX_SIZE (32 * MiB)
#define DEFAULT_CACHE_CLEAN_INTERVAL 600  /* seconds */
#else
#define DEFAULT_L2_CACHE_MAX_SIZE (8 * MiB)
/* Cache clean interval is currently available only on Linux, so must be 0 */
#define DEFAULT_CACHE_CLEAN_INTERVAL 0
#endif

#define DEFAULT_CLUSTER_SIZE 65536

#define QCOW2_OPT_DATA_FILE "data-file"
#define QCOW2_OPT_LAZY_REFCOUNTS "lazy-refcounts"
#define QCOW2_OPT_DISCARD_REQUEST "pass-discard-request"
#define QCOW2_OPT_DISCARD_SNAPSHOT "pass-discard-snapshot"
#define QCOW2_OPT_DISCARD_OTHER "pass-discard-other"
#define QCOW2_OPT_DISCARD_NO_UNREF "discard-no-unref"
#define QCOW2_OPT_OVERLAP "overlap-check"
#define QCOW2_OPT_OVERLAP_TEMPLATE "overlap-check.template"
#define QCOW2_OPT_OVERLAP_MAIN_HEADER "overlap-check.main-header"
#define QCOW2_OPT_OVERLAP_ACTIVE_L1 "overlap-check.active-l1"
#define QCOW2_OPT_OVERLAP_ACTIVE_L2 "overlap-check.active-l2"
#define QCOW2_OPT_OVERLAP_REFCOUNT_TABLE "overlap-check.refcount-table"
#define QCOW2_OPT_OVERLAP_REFCOUNT_BLOCK "overlap-check.refcount-block"
#define QCOW2_OPT_OVERLAP_SNAPSHOT_TABLE "overlap-check.snapshot-table"
#define QCOW2_OPT_OVERLAP_INACTIVE_L1 "overlap-check.inactive-l1"
#define QCOW2_OPT_OVERLAP_INACTIVE_L2 "overlap-check.inactive-l2"
#define QCOW2_OPT_OVERLAP_BITMAP_DIRECTORY "overlap-check.bitmap-directory"
#define QCOW2_OPT_CACHE_SIZE "cache-size"
#define QCOW2_OPT_L2_CACHE_SIZE "l2-cache-size"
#define QCOW2_OPT_L2_CACHE_ENTRY_SIZE "l2-cache-entry-size"
#define QCOW2_OPT_REFCOUNT_CACHE_SIZE "refcount-cache-size"
#define QCOW2_OPT_CACHE_CLEAN_INTERVAL "cache-clean-interval"

typedef struct QCowHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t backing_file_offset;
    uint32_t backing_file_size;
    uint32_t cluster_bits;
    uint64_t size; /* in bytes */
    uint32_t crypt_method;
    uint32_t l1_size; /* XXX: save number of clusters instead ? */
    uint64_t l1_table_offset;
    uint64_t refcount_table_offset;
    uint32_t refcount_table_clusters;
    uint32_t nb_snapshots;
    uint64_t snapshots_offset;

    /* The following fields are only valid for version >= 3 */
    uint64_t incompatible_features;
    uint64_t compatible_features;
    uint64_t autoclear_features;

    uint32_t refcount_order;
    uint32_t header_length;

    /* Additional fields */
    uint8_t compression_type;

    /* header must be a multiple of 8 */
    uint8_t padding[7];
} QEMU_PACKED QCowHeader;

QEMU_BUILD_BUG_ON(!QEMU_IS_ALIGNED(sizeof(QCowHeader), 8));

typedef struct QEMU_PACKED QCowSnapshotHeader {
    /* header is 8 byte aligned */
    uint64_t l1_table_offset;

    uint32_t l1_size;
    uint16_t id_str_size;
    uint16_t name_size;

    uint32_t date_sec;
    uint32_t date_nsec;

    uint64_t vm_clock_nsec;

    uint32_t vm_state_size;
    uint32_t extra_data_size; /* for extension */
    /* extra data follows */
    /* id_str follows */
    /* name follows  */
} QCowSnapshotHeader;

typedef struct QEMU_PACKED QCowSnapshotExtraData {
    uint64_t vm_state_size_large;
    uint64_t disk_size;
    uint64_t icount;
} QCowSnapshotExtraData;


typedef struct QCowSnapshot {
    uint64_t l1_table_offset;
    uint32_t l1_size;
    char *id_str;
    char *name;
    uint64_t disk_size;
    uint64_t vm_state_size;
    uint32_t date_sec;
    uint32_t date_nsec;
    uint64_t vm_clock_nsec;
    /* icount value for the moment when snapshot was taken */
    uint64_t icount;
    /* Size of all extra data, including QCowSnapshotExtraData if available */
    uint32_t extra_data_size;
    /* Data beyond QCowSnapshotExtraData, if any */
    void *unknown_extra_data;
} QCowSnapshot;

struct Qcow2Cache;
typedef struct Qcow2Cache Qcow2Cache;

typedef struct Qcow2CryptoHeaderExtension {
    uint64_t offset;
    uint64_t length;
} QEMU_PACKED Qcow2CryptoHeaderExtension;

typedef struct Qcow2UnknownHeaderExtension {
    uint32_t magic;
    uint32_t len;
    QLIST_ENTRY(Qcow2UnknownHeaderExtension) next;
    uint8_t data[];
} Qcow2UnknownHeaderExtension;

enum {
    QCOW2_FEAT_TYPE_INCOMPATIBLE    = 0,
    QCOW2_FEAT_TYPE_COMPATIBLE      = 1,
    QCOW2_FEAT_TYPE_AUTOCLEAR       = 2,
};

/* Incompatible feature bits */
enum {
    QCOW2_INCOMPAT_DIRTY_BITNR      = 0,
    QCOW2_INCOMPAT_CORRUPT_BITNR    = 1,
    QCOW2_INCOMPAT_DATA_FILE_BITNR  = 2,
    QCOW2_INCOMPAT_COMPRESSION_BITNR = 3,
    QCOW2_INCOMPAT_EXTL2_BITNR      = 4,
    QCOW2_INCOMPAT_DIRTY            = 1 << QCOW2_INCOMPAT_DIRTY_BITNR,
    QCOW2_INCOMPAT_CORRUPT          = 1 << QCOW2_INCOMPAT_CORRUPT_BITNR,
    QCOW2_INCOMPAT_DATA_FILE        = 1 << QCOW2_INCOMPAT_DATA_FILE_BITNR,
    QCOW2_INCOMPAT_COMPRESSION      = 1 << QCOW2_INCOMPAT_COMPRESSION_BITNR,
    QCOW2_INCOMPAT_EXTL2            = 1 << QCOW2_INCOMPAT_EXTL2_BITNR,

    QCOW2_INCOMPAT_MASK             = QCOW2_INCOMPAT_DIRTY
                                    | QCOW2_INCOMPAT_CORRUPT
                                    | QCOW2_INCOMPAT_DATA_FILE
                                    | QCOW2_INCOMPAT_COMPRESSION
                                    | QCOW2_INCOMPAT_EXTL2,
};

/* Compatible feature bits */
enum {
    QCOW2_COMPAT_LAZY_REFCOUNTS_BITNR = 0,
    QCOW2_COMPAT_LAZY_REFCOUNTS       = 1 << QCOW2_COMPAT_LAZY_REFCOUNTS_BITNR,

    QCOW2_COMPAT_FEAT_MASK            = QCOW2_COMPAT_LAZY_REFCOUNTS,
};

/* Autoclear feature bits */
enum {
    QCOW2_AUTOCLEAR_BITMAPS_BITNR       = 0,
    QCOW2_AUTOCLEAR_DATA_FILE_RAW_BITNR = 1,
    QCOW2_AUTOCLEAR_BITMAPS             = 1 << QCOW2_AUTOCLEAR_BITMAPS_BITNR,
    QCOW2_AUTOCLEAR_DATA_FILE_RAW       = 1 << QCOW2_AUTOCLEAR_DATA_FILE_RAW_BITNR,

    QCOW2_AUTOCLEAR_MASK                = QCOW2_AUTOCLEAR_BITMAPS
                                        | QCOW2_AUTOCLEAR_DATA_FILE_RAW,
};

enum qcow2_discard_type {
    QCOW2_DISCARD_NEVER = 0,
    QCOW2_DISCARD_ALWAYS,
    QCOW2_DISCARD_REQUEST,
    QCOW2_DISCARD_SNAPSHOT,
    QCOW2_DISCARD_OTHER,
    QCOW2_DISCARD_MAX
};

typedef struct Qcow2Feature {
    uint8_t type;
    uint8_t bit;
    char    name[46];
} QEMU_PACKED Qcow2Feature;

typedef struct Qcow2DiscardRegion {
    BlockDriverState *bs;
    uint64_t offset;
    uint64_t bytes;
    QTAILQ_ENTRY(Qcow2DiscardRegion) next;
} Qcow2DiscardRegion;

typedef uint64_t Qcow2GetRefcountFunc(const void *refcount_array,
                                      uint64_t index);
typedef void Qcow2SetRefcountFunc(void *refcount_array,
                                  uint64_t index, uint64_t value);

typedef struct Qcow2BitmapHeaderExt {
    uint32_t nb_bitmaps;
    uint32_t reserved32;
    uint64_t bitmap_directory_size;
    uint64_t bitmap_directory_offset;
} QEMU_PACKED Qcow2BitmapHeaderExt;

#define QCOW2_MAX_THREADS 4

typedef struct BDRVQcow2State {
    int cluster_bits;
    int cluster_size;
    int l2_slice_size;
    int subcluster_bits;
    int subcluster_size;
    int subclusters_per_cluster;
    int l2_bits;
    int l2_size;
    int l1_size;
    int l1_vm_state_index;
    int refcount_block_bits;
    int refcount_block_size;
    int csize_shift;
    int csize_mask;
    uint64_t cluster_offset_mask;
    uint64_t l1_table_offset;
    uint64_t *l1_table;

    Qcow2Cache *l2_table_cache;
    Qcow2Cache *refcount_block_cache;
    QEMUTimer *cache_clean_timer;
    unsigned cache_clean_interval;

    QLIST_HEAD(, QCowL2Meta) cluster_allocs;

    uint64_t *refcount_table;
    uint64_t refcount_table_offset;
    uint32_t refcount_table_size;
    uint32_t max_refcount_table_index; /* Last used entry in refcount_table */
    uint64_t free_cluster_index;
    uint64_t free_byte_offset;

    CoMutex lock;

    Qcow2CryptoHeaderExtension crypto_header; /* QCow2 header extension */
    QCryptoBlockOpenOptions *crypto_opts; /* Disk encryption runtime options */
    QCryptoBlock *crypto; /* Disk encryption format driver */
    bool crypt_physical_offset; /* Whether to use virtual or physical offset
                                   for encryption initialization vector tweak */
    uint32_t crypt_method_header;
    uint64_t snapshots_offset;
    int snapshots_size;
    unsigned int nb_snapshots;
    QCowSnapshot *snapshots;

    uint32_t nb_bitmaps;
    uint64_t bitmap_directory_size;
    uint64_t bitmap_directory_offset;

    int flags;
    int qcow_version;
    bool use_lazy_refcounts;
    int refcount_order;
    int refcount_bits;
    uint64_t refcount_max;

    Qcow2GetRefcountFunc *get_refcount;
    Qcow2SetRefcountFunc *set_refcount;

    bool discard_passthrough[QCOW2_DISCARD_MAX];

    bool discard_no_unref;

    int overlap_check; /* bitmask of Qcow2MetadataOverlap values */
    bool signaled_corruption;

    uint64_t incompatible_features;
    uint64_t compatible_features;
    uint64_t autoclear_features;

    size_t unknown_header_fields_size;
    void *unknown_header_fields;
    QLIST_HEAD(, Qcow2UnknownHeaderExtension) unknown_header_ext;
    QTAILQ_HEAD (, Qcow2DiscardRegion) discards;
    bool cache_discards;

    /* Backing file path and format as stored in the image (this is not the
     * effective path/format, which may be the result of a runtime option
     * override) */
    char *image_backing_file;
    char *image_backing_format;
    char *image_data_file;

    CoQueue thread_task_queue;
    int nb_threads;

    BdrvChild *data_file;

    bool metadata_preallocation_checked;
    bool metadata_preallocation;
    /*
     * Compression type used for the image. Default: 0 - ZLIB
     * The image compression type is set on image creation.
     * For now, the only way to change the compression type
     * is to convert the image with the desired compression type set.
     */
    Qcow2CompressionType compression_type;
} BDRVQcow2State;

typedef struct Qcow2COWRegion {
    /**
     * Offset of the COW region in bytes from the start of the first cluster
     * touched by the request.
     */
    unsigned    offset;

    /** Number of bytes to copy */
    unsigned    nb_bytes;
} Qcow2COWRegion;

/**
 * Describes an in-flight (part of a) write request that writes to clusters
 * that need to have their L2 table entries updated (because they are
 * newly allocated or need changes in their L2 bitmaps)
 */
typedef struct QCowL2Meta
{
    /** Guest offset of the first updated cluster */
    uint64_t offset;

    /** Host offset of the first updated cluster */
    uint64_t alloc_offset;

    /** Number of updated clusters */
    int nb_clusters;

    /** Do not free the old clusters */
    bool keep_old_clusters;

    /**
     * Requests that overlap with this allocation and wait to be restarted
     * when the allocating request has completed.
     */
    CoQueue dependent_requests;

    /**
     * The COW Region immediately before the area the guest actually
     * writes to. This (part of the) write request starts at
     * cow_start.offset + cow_start.nb_bytes.
     */
    Qcow2COWRegion cow_start;

    /**
     * The COW Region immediately after the area the guest actually
     * writes to. This (part of the) write request ends at cow_end.offset
     * (which must always be set even when cow_end.nb_bytes is 0).
     */
    Qcow2COWRegion cow_end;

    /*
     * Indicates that COW regions are already handled and do not require
     * any more processing.
     */
    bool skip_cow;

    /**
     * Indicates that this is not a normal write request but a preallocation.
     * If the image has extended L2 entries this means that no new individual
     * subclusters will be marked as allocated in the L2 bitmap (but any
     * existing contents of that bitmap will be kept).
     */
    bool prealloc;

    /**
     * The I/O vector with the data from the actual guest write request.
     * If non-NULL, this is meant to be merged together with the data
     * from @cow_start and @cow_end into one single write operation.
     */
    QEMUIOVector *data_qiov;
    size_t data_qiov_offset;

    /** Pointer to next L2Meta of the same write request */
    struct QCowL2Meta *next;

    QLIST_ENTRY(QCowL2Meta) next_in_flight;
} QCowL2Meta;

/*
 * In images with standard L2 entries all clusters are treated as if
 * they had one subcluster so QCow2ClusterType and QCow2SubclusterType
 * can be mapped to each other and have the exact same meaning
 * (QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC cannot happen in these images).
 *
 * In images with extended L2 entries QCow2ClusterType refers to the
 * complete cluster and QCow2SubclusterType to each of the individual
 * subclusters, so there are several possible combinations:
 *
 *     |--------------+---------------------------|
 *     | Cluster type | Possible subcluster types |
 *     |--------------+---------------------------|
 *     | UNALLOCATED  |         UNALLOCATED_PLAIN |
 *     |              |                ZERO_PLAIN |
 *     |--------------+---------------------------|
 *     | NORMAL       |         UNALLOCATED_ALLOC |
 *     |              |                ZERO_ALLOC |
 *     |              |                    NORMAL |
 *     |--------------+---------------------------|
 *     | COMPRESSED   |                COMPRESSED |
 *     |--------------+---------------------------|
 *
 * QCOW2_SUBCLUSTER_INVALID means that the L2 entry is incorrect and
 * the image should be marked corrupt.
 */

typedef enum QCow2ClusterType {
    QCOW2_CLUSTER_UNALLOCATED,
    QCOW2_CLUSTER_ZERO_PLAIN,
    QCOW2_CLUSTER_ZERO_ALLOC,
    QCOW2_CLUSTER_NORMAL,
    QCOW2_CLUSTER_COMPRESSED,
} QCow2ClusterType;

typedef enum QCow2SubclusterType {
    QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN,
    QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC,
    QCOW2_SUBCLUSTER_ZERO_PLAIN,
    QCOW2_SUBCLUSTER_ZERO_ALLOC,
    QCOW2_SUBCLUSTER_NORMAL,
    QCOW2_SUBCLUSTER_COMPRESSED,
    QCOW2_SUBCLUSTER_INVALID,
} QCow2SubclusterType;

typedef enum QCow2MetadataOverlap {
    QCOW2_OL_MAIN_HEADER_BITNR      = 0,
    QCOW2_OL_ACTIVE_L1_BITNR        = 1,
    QCOW2_OL_ACTIVE_L2_BITNR        = 2,
    QCOW2_OL_REFCOUNT_TABLE_BITNR   = 3,
    QCOW2_OL_REFCOUNT_BLOCK_BITNR   = 4,
    QCOW2_OL_SNAPSHOT_TABLE_BITNR   = 5,
    QCOW2_OL_INACTIVE_L1_BITNR      = 6,
    QCOW2_OL_INACTIVE_L2_BITNR      = 7,
    QCOW2_OL_BITMAP_DIRECTORY_BITNR = 8,

    QCOW2_OL_MAX_BITNR              = 9,

    QCOW2_OL_NONE             = 0,
    QCOW2_OL_MAIN_HEADER      = (1 << QCOW2_OL_MAIN_HEADER_BITNR),
    QCOW2_OL_ACTIVE_L1        = (1 << QCOW2_OL_ACTIVE_L1_BITNR),
    QCOW2_OL_ACTIVE_L2        = (1 << QCOW2_OL_ACTIVE_L2_BITNR),
    QCOW2_OL_REFCOUNT_TABLE   = (1 << QCOW2_OL_REFCOUNT_TABLE_BITNR),
    QCOW2_OL_REFCOUNT_BLOCK   = (1 << QCOW2_OL_REFCOUNT_BLOCK_BITNR),
    QCOW2_OL_SNAPSHOT_TABLE   = (1 << QCOW2_OL_SNAPSHOT_TABLE_BITNR),
    QCOW2_OL_INACTIVE_L1      = (1 << QCOW2_OL_INACTIVE_L1_BITNR),
    /* NOTE: Checking overlaps with inactive L2 tables will result in bdrv
     * reads. */
    QCOW2_OL_INACTIVE_L2      = (1 << QCOW2_OL_INACTIVE_L2_BITNR),
    QCOW2_OL_BITMAP_DIRECTORY = (1 << QCOW2_OL_BITMAP_DIRECTORY_BITNR),
} QCow2MetadataOverlap;

/* Perform all overlap checks which can be done in constant time */
#define QCOW2_OL_CONSTANT \
    (QCOW2_OL_MAIN_HEADER | QCOW2_OL_ACTIVE_L1 | QCOW2_OL_REFCOUNT_TABLE | \
     QCOW2_OL_SNAPSHOT_TABLE | QCOW2_OL_BITMAP_DIRECTORY)

/* Perform all overlap checks which don't require disk access */
#define QCOW2_OL_CACHED \
    (QCOW2_OL_CONSTANT | QCOW2_OL_ACTIVE_L2 | QCOW2_OL_REFCOUNT_BLOCK | \
     QCOW2_OL_INACTIVE_L1)

/* Perform all overlap checks */
#define QCOW2_OL_ALL \
    (QCOW2_OL_CACHED | QCOW2_OL_INACTIVE_L2)

#define L1E_OFFSET_MASK 0x00fffffffffffe00ULL
#define L1E_RESERVED_MASK 0x7f000000000001ffULL
#define L2E_OFFSET_MASK 0x00fffffffffffe00ULL
#define L2E_STD_RESERVED_MASK 0x3f000000000001feULL

#define REFT_OFFSET_MASK 0xfffffffffffffe00ULL
#define REFT_RESERVED_MASK 0x1ffULL

#define INV_OFFSET (-1ULL)

static inline bool has_subclusters(BDRVQcow2State *s)
{
    return s->incompatible_features & QCOW2_INCOMPAT_EXTL2;
}

static inline size_t l2_entry_size(BDRVQcow2State *s)
{
    return has_subclusters(s) ? L2E_SIZE_EXTENDED : L2E_SIZE_NORMAL;
}

static inline uint64_t get_l2_entry(BDRVQcow2State *s, uint64_t *l2_slice,
                                    int idx)
{
    idx *= l2_entry_size(s) / sizeof(uint64_t);
    return be64_to_cpu(l2_slice[idx]);
}

static inline uint64_t get_l2_bitmap(BDRVQcow2State *s, uint64_t *l2_slice,
                                     int idx)
{
    if (has_subclusters(s)) {
        idx *= l2_entry_size(s) / sizeof(uint64_t);
        return be64_to_cpu(l2_slice[idx + 1]);
    } else {
        return 0; /* For convenience only; this value has no meaning. */
    }
}

static inline void set_l2_entry(BDRVQcow2State *s, uint64_t *l2_slice,
                                int idx, uint64_t entry)
{
    idx *= l2_entry_size(s) / sizeof(uint64_t);
    l2_slice[idx] = cpu_to_be64(entry);
}

static inline void set_l2_bitmap(BDRVQcow2State *s, uint64_t *l2_slice,
                                 int idx, uint64_t bitmap)
{
    assert(has_subclusters(s));
    idx *= l2_entry_size(s) / sizeof(uint64_t);
    l2_slice[idx + 1] = cpu_to_be64(bitmap);
}

static inline bool has_data_file(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    return (s->data_file != bs->file);
}

static inline bool data_file_is_raw(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    return !!(s->autoclear_features & QCOW2_AUTOCLEAR_DATA_FILE_RAW);
}

static inline int64_t start_of_cluster(BDRVQcow2State *s, int64_t offset)
{
    return offset & ~(s->cluster_size - 1);
}

static inline int64_t offset_into_cluster(BDRVQcow2State *s, int64_t offset)
{
    return offset & (s->cluster_size - 1);
}

static inline int64_t offset_into_subcluster(BDRVQcow2State *s, int64_t offset)
{
    return offset & (s->subcluster_size - 1);
}

static inline uint64_t size_to_clusters(BDRVQcow2State *s, uint64_t size)
{
    return (size + (s->cluster_size - 1)) >> s->cluster_bits;
}

static inline uint64_t size_to_subclusters(BDRVQcow2State *s, uint64_t size)
{
    return (size + (s->subcluster_size - 1)) >> s->subcluster_bits;
}

static inline int64_t size_to_l1(BDRVQcow2State *s, int64_t size)
{
    int shift = s->cluster_bits + s->l2_bits;
    return (size + (1ULL << shift) - 1) >> shift;
}

static inline int offset_to_l1_index(BDRVQcow2State *s, uint64_t offset)
{
    return offset >> (s->l2_bits + s->cluster_bits);
}

static inline int offset_to_l2_index(BDRVQcow2State *s, int64_t offset)
{
    return (offset >> s->cluster_bits) & (s->l2_size - 1);
}

static inline int offset_to_l2_slice_index(BDRVQcow2State *s, int64_t offset)
{
    return (offset >> s->cluster_bits) & (s->l2_slice_size - 1);
}

static inline int offset_to_sc_index(BDRVQcow2State *s, int64_t offset)
{
    return (offset >> s->subcluster_bits) & (s->subclusters_per_cluster - 1);
}

static inline int64_t qcow2_vm_state_offset(BDRVQcow2State *s)
{
    return (int64_t)s->l1_vm_state_index << (s->cluster_bits + s->l2_bits);
}

static inline QCow2ClusterType qcow2_get_cluster_type(BlockDriverState *bs,
                                                      uint64_t l2_entry)
{
    BDRVQcow2State *s = bs->opaque;

    if (l2_entry & QCOW_OFLAG_COMPRESSED) {
        return QCOW2_CLUSTER_COMPRESSED;
    } else if ((l2_entry & QCOW_OFLAG_ZERO) && !has_subclusters(s)) {
        if (l2_entry & L2E_OFFSET_MASK) {
            return QCOW2_CLUSTER_ZERO_ALLOC;
        }
        return QCOW2_CLUSTER_ZERO_PLAIN;
    } else if (!(l2_entry & L2E_OFFSET_MASK)) {
        /* Offset 0 generally means unallocated, but it is ambiguous with
         * external data files because 0 is a valid offset there. However, all
         * clusters in external data files always have refcount 1, so we can
         * rely on QCOW_OFLAG_COPIED to disambiguate. */
        if (has_data_file(bs) && (l2_entry & QCOW_OFLAG_COPIED)) {
            return QCOW2_CLUSTER_NORMAL;
        } else {
            return QCOW2_CLUSTER_UNALLOCATED;
        }
    } else {
        return QCOW2_CLUSTER_NORMAL;
    }
}

/*
 * In an image without subsclusters @l2_bitmap is ignored and
 * @sc_index must be 0.
 * Return QCOW2_SUBCLUSTER_INVALID if an invalid l2 entry is detected
 * (this checks the whole entry and bitmap, not only the bits related
 * to subcluster @sc_index).
 */
static inline
QCow2SubclusterType qcow2_get_subcluster_type(BlockDriverState *bs,
                                              uint64_t l2_entry,
                                              uint64_t l2_bitmap,
                                              unsigned sc_index)
{
    BDRVQcow2State *s = bs->opaque;
    QCow2ClusterType type = qcow2_get_cluster_type(bs, l2_entry);
    assert(sc_index < s->subclusters_per_cluster);

    if (has_subclusters(s)) {
        switch (type) {
        case QCOW2_CLUSTER_COMPRESSED:
            return QCOW2_SUBCLUSTER_COMPRESSED;
        case QCOW2_CLUSTER_NORMAL:
            if ((l2_bitmap >> 32) & l2_bitmap) {
                return QCOW2_SUBCLUSTER_INVALID;
            } else if (l2_bitmap & QCOW_OFLAG_SUB_ZERO(sc_index)) {
                return QCOW2_SUBCLUSTER_ZERO_ALLOC;
            } else if (l2_bitmap & QCOW_OFLAG_SUB_ALLOC(sc_index)) {
                return QCOW2_SUBCLUSTER_NORMAL;
            } else {
                return QCOW2_SUBCLUSTER_UNALLOCATED_ALLOC;
            }
        case QCOW2_CLUSTER_UNALLOCATED:
            if (l2_bitmap & QCOW_L2_BITMAP_ALL_ALLOC) {
                return QCOW2_SUBCLUSTER_INVALID;
            } else if (l2_bitmap & QCOW_OFLAG_SUB_ZERO(sc_index)) {
                return QCOW2_SUBCLUSTER_ZERO_PLAIN;
            } else {
                return QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN;
            }
        default:
            g_assert_not_reached();
        }
    } else {
        switch (type) {
        case QCOW2_CLUSTER_COMPRESSED:
            return QCOW2_SUBCLUSTER_COMPRESSED;
        case QCOW2_CLUSTER_ZERO_PLAIN:
            return QCOW2_SUBCLUSTER_ZERO_PLAIN;
        case QCOW2_CLUSTER_ZERO_ALLOC:
            return QCOW2_SUBCLUSTER_ZERO_ALLOC;
        case QCOW2_CLUSTER_NORMAL:
            return QCOW2_SUBCLUSTER_NORMAL;
        case QCOW2_CLUSTER_UNALLOCATED:
            return QCOW2_SUBCLUSTER_UNALLOCATED_PLAIN;
        default:
            g_assert_not_reached();
        }
    }
}

static inline bool qcow2_cluster_is_allocated(QCow2ClusterType type)
{
    return (type == QCOW2_CLUSTER_COMPRESSED || type == QCOW2_CLUSTER_NORMAL ||
            type == QCOW2_CLUSTER_ZERO_ALLOC);
}

/* Check whether refcounts are eager or lazy */
static inline bool qcow2_need_accurate_refcounts(BDRVQcow2State *s)
{
    return !(s->incompatible_features & QCOW2_INCOMPAT_DIRTY);
}

static inline uint64_t l2meta_cow_start(QCowL2Meta *m)
{
    return m->offset + m->cow_start.offset;
}

static inline uint64_t l2meta_cow_end(QCowL2Meta *m)
{
    return m->offset + m->cow_end.offset + m->cow_end.nb_bytes;
}

static inline uint64_t refcount_diff(uint64_t r1, uint64_t r2)
{
    return r1 > r2 ? r1 - r2 : r2 - r1;
}

static inline
uint32_t offset_to_reftable_index(BDRVQcow2State *s, uint64_t offset)
{
    return offset >> (s->refcount_block_bits + s->cluster_bits);
}

/* qcow2.c functions */
int64_t qcow2_refcount_metadata_size(int64_t clusters, size_t cluster_size,
                                     int refcount_order, bool generous_increase,
                                     uint64_t *refblock_count);

int qcow2_mark_dirty(BlockDriverState *bs);
int qcow2_mark_corrupt(BlockDriverState *bs);
int qcow2_update_header(BlockDriverState *bs);

void qcow2_signal_corruption(BlockDriverState *bs, bool fatal, int64_t offset,
                             int64_t size, const char *message_format, ...)
                             G_GNUC_PRINTF(5, 6);

int qcow2_validate_table(BlockDriverState *bs, uint64_t offset,
                         uint64_t entries, size_t entry_len,
                         int64_t max_size_bytes, const char *table_name,
                         Error **errp);

/* qcow2-refcount.c functions */
int coroutine_fn GRAPH_RDLOCK qcow2_refcount_init(BlockDriverState *bs);
void qcow2_refcount_close(BlockDriverState *bs);

int qcow2_get_refcount(BlockDriverState *bs, int64_t cluster_index,
                       uint64_t *refcount);

int qcow2_update_cluster_refcount(BlockDriverState *bs, int64_t cluster_index,
                                  uint64_t addend, bool decrease,
                                  enum qcow2_discard_type type);

int64_t qcow2_refcount_area(BlockDriverState *bs, uint64_t offset,
                            uint64_t additional_clusters, bool exact_size,
                            int new_refblock_index,
                            uint64_t new_refblock_offset);

int64_t qcow2_alloc_clusters(BlockDriverState *bs, uint64_t size);
int64_t coroutine_fn qcow2_alloc_clusters_at(BlockDriverState *bs, uint64_t offset,
                                             int64_t nb_clusters);
int64_t coroutine_fn GRAPH_RDLOCK qcow2_alloc_bytes(BlockDriverState *bs, int size);
void qcow2_free_clusters(BlockDriverState *bs,
                          int64_t offset, int64_t size,
                          enum qcow2_discard_type type);
void qcow2_free_any_cluster(BlockDriverState *bs, uint64_t l2_entry,
                            enum qcow2_discard_type type);

int qcow2_update_snapshot_refcount(BlockDriverState *bs,
    int64_t l1_table_offset, int l1_size, int addend);

int qcow2_flush_caches(BlockDriverState *bs);
int qcow2_write_caches(BlockDriverState *bs);
int coroutine_fn qcow2_check_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                                       BdrvCheckMode fix);

void qcow2_process_discards(BlockDriverState *bs, int ret);

int qcow2_check_metadata_overlap(BlockDriverState *bs, int ign, int64_t offset,
                                 int64_t size);
int qcow2_pre_write_overlap_check(BlockDriverState *bs, int ign, int64_t offset,
                                  int64_t size, bool data_file);
int coroutine_fn qcow2_inc_refcounts_imrt(BlockDriverState *bs, BdrvCheckResult *res,
                                          void **refcount_table,
                                          int64_t *refcount_table_size,
                                          int64_t offset, int64_t size);

int qcow2_change_refcount_order(BlockDriverState *bs, int refcount_order,
                                BlockDriverAmendStatusCB *status_cb,
                                void *cb_opaque, Error **errp);
int coroutine_fn GRAPH_RDLOCK qcow2_shrink_reftable(BlockDriverState *bs);
int64_t coroutine_fn qcow2_get_last_cluster(BlockDriverState *bs, int64_t size);

int coroutine_fn GRAPH_RDLOCK
qcow2_detect_metadata_preallocation(BlockDriverState *bs);

/* qcow2-cluster.c functions */
int qcow2_grow_l1_table(BlockDriverState *bs, uint64_t min_size,
                        bool exact_size);

int coroutine_fn GRAPH_RDLOCK
qcow2_shrink_l1_table(BlockDriverState *bs, uint64_t max_size);

int qcow2_write_l1_entry(BlockDriverState *bs, int l1_index);
int qcow2_encrypt_sectors(BDRVQcow2State *s, int64_t sector_num,
                          uint8_t *buf, int nb_sectors, bool enc, Error **errp);

int qcow2_get_host_offset(BlockDriverState *bs, uint64_t offset,
                          unsigned int *bytes, uint64_t *host_offset,
                          QCow2SubclusterType *subcluster_type);
int coroutine_fn qcow2_alloc_host_offset(BlockDriverState *bs, uint64_t offset,
                                         unsigned int *bytes,
                                         uint64_t *host_offset, QCowL2Meta **m);
int coroutine_fn GRAPH_RDLOCK
qcow2_alloc_compressed_cluster_offset(BlockDriverState *bs, uint64_t offset,
                                      int compressed_size, uint64_t *host_offset);
void qcow2_parse_compressed_l2_entry(BlockDriverState *bs, uint64_t l2_entry,
                                     uint64_t *coffset, int *csize);

int coroutine_fn GRAPH_RDLOCK
qcow2_alloc_cluster_link_l2(BlockDriverState *bs, QCowL2Meta *m);

void coroutine_fn qcow2_alloc_cluster_abort(BlockDriverState *bs, QCowL2Meta *m);
int qcow2_cluster_discard(BlockDriverState *bs, uint64_t offset,
                          uint64_t bytes, enum qcow2_discard_type type,
                          bool full_discard);

int coroutine_fn GRAPH_RDLOCK
qcow2_subcluster_zeroize(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                         int flags);

int qcow2_expand_zero_clusters(BlockDriverState *bs,
                               BlockDriverAmendStatusCB *status_cb,
                               void *cb_opaque);

/* qcow2-snapshot.c functions */
int qcow2_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info);
int qcow2_snapshot_goto(BlockDriverState *bs, const char *snapshot_id);
int qcow2_snapshot_delete(BlockDriverState *bs,
                          const char *snapshot_id,
                          const char *name,
                          Error **errp);
int qcow2_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_tab);
int qcow2_snapshot_load_tmp(BlockDriverState *bs,
                            const char *snapshot_id,
                            const char *name,
                            Error **errp);

void qcow2_free_snapshots(BlockDriverState *bs);
int coroutine_fn GRAPH_RDLOCK
qcow2_read_snapshots(BlockDriverState *bs, Error **errp);
int qcow2_write_snapshots(BlockDriverState *bs);

int coroutine_fn GRAPH_RDLOCK
qcow2_check_read_snapshot_table(BlockDriverState *bs, BdrvCheckResult *result,
                                BdrvCheckMode fix);

int coroutine_fn qcow2_check_fix_snapshot_table(BlockDriverState *bs,
                                                BdrvCheckResult *result,
                                                BdrvCheckMode fix);

/* qcow2-cache.c functions */
Qcow2Cache *qcow2_cache_create(BlockDriverState *bs, int num_tables,
                               unsigned table_size);
int qcow2_cache_destroy(Qcow2Cache *c);

void qcow2_cache_entry_mark_dirty(Qcow2Cache *c, void *table);
int qcow2_cache_flush(BlockDriverState *bs, Qcow2Cache *c);
int qcow2_cache_write(BlockDriverState *bs, Qcow2Cache *c);
int qcow2_cache_set_dependency(BlockDriverState *bs, Qcow2Cache *c,
    Qcow2Cache *dependency);
void qcow2_cache_depends_on_flush(Qcow2Cache *c);

void qcow2_cache_clean_unused(Qcow2Cache *c);
int qcow2_cache_empty(BlockDriverState *bs, Qcow2Cache *c);

int qcow2_cache_get(BlockDriverState *bs, Qcow2Cache *c, uint64_t offset,
    void **table);
int qcow2_cache_get_empty(BlockDriverState *bs, Qcow2Cache *c, uint64_t offset,
    void **table);
void qcow2_cache_put(Qcow2Cache *c, void **table);
void *qcow2_cache_is_table_offset(Qcow2Cache *c, uint64_t offset);
void qcow2_cache_discard(Qcow2Cache *c, void *table);

/* qcow2-bitmap.c functions */
int coroutine_fn
qcow2_check_bitmaps_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                              void **refcount_table,
                              int64_t *refcount_table_size);
bool coroutine_fn GRAPH_RDLOCK
qcow2_load_dirty_bitmaps(BlockDriverState *bs, bool *header_updated, Error **errp);
bool qcow2_get_bitmap_info_list(BlockDriverState *bs,
                                Qcow2BitmapInfoList **info_list, Error **errp);
int qcow2_reopen_bitmaps_rw(BlockDriverState *bs, Error **errp);
int coroutine_fn qcow2_truncate_bitmaps_check(BlockDriverState *bs, Error **errp);
bool qcow2_store_persistent_dirty_bitmaps(BlockDriverState *bs,
                                          bool release_stored, Error **errp);
int qcow2_reopen_bitmaps_ro(BlockDriverState *bs, Error **errp);
bool coroutine_fn qcow2_co_can_store_new_dirty_bitmap(BlockDriverState *bs,
                                                      const char *name,
                                                      uint32_t granularity,
                                                      Error **errp);
int coroutine_fn qcow2_co_remove_persistent_dirty_bitmap(BlockDriverState *bs,
                                                         const char *name,
                                                         Error **errp);
bool qcow2_supports_persistent_dirty_bitmap(BlockDriverState *bs);
uint64_t qcow2_get_persistent_dirty_bitmap_size(BlockDriverState *bs,
                                                uint32_t cluster_size);

ssize_t coroutine_fn
qcow2_co_compress(BlockDriverState *bs, void *dest, size_t dest_size,
                  const void *src, size_t src_size);
ssize_t coroutine_fn
qcow2_co_decompress(BlockDriverState *bs, void *dest, size_t dest_size,
                    const void *src, size_t src_size);
int coroutine_fn
qcow2_co_encrypt(BlockDriverState *bs, uint64_t host_offset,
                 uint64_t guest_offset, void *buf, size_t len);
int coroutine_fn
qcow2_co_decrypt(BlockDriverState *bs, uint64_t host_offset,
                 uint64_t guest_offset, void *buf, size_t len);

#endif
