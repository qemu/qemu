/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this is the header of the FVD block device driver.
 *============================================================================*/

#include <sys/vfs.h>
#include <sys/mman.h>
#include <pthread.h>
#include <execinfo.h>
#include <sys/ioctl.h>
#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/option.h"
#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/timer.h"
#include "block/block.h"
#include "block/blksim.h"
#include "block/fvd-ext.h"

#define FVD_MAGIC         (('Q' << 24) | ('C' << 16) | (0xF5 << 8) | 0xA9)
#define FVD_VERSION         1

extern bool in_qemu_tool;

/* Profile-directed prefetch. (to be implemented). */
typedef struct __attribute__ ((__packed__)) PrefetchProfileEntry {
    int64_t offset;        /* in bytes */

    /* In the unit of FvdHeader.prefetch_profile_entry_len_unit, i.e.,
     * len_in_bytes = len * FvdHeader.unit_of_PrefetchProfileEntry_len. */
    uint32_t len;
} PrefetchProfileEntry;

/*
 * The FVD format consists of:
 *   + Header fields of FvdHeader.
 *   + Bitmap, starting on a 4KB page boundary at a location specified by
 *     FvdHeader.bitmap_offset.
 *   + Table, starting on a 4KB page boundary at a location specified by
 *     FvdHeader.table_offset.
 *   + Journal, starting on a 4KB page boundary at a location specified by
 *     FvdHeader.journal_offset.
 *   + Prefetch profile entries, starting on a 4KB page boundary at a location
 *     specified by FvdHeader.prefetch_profile_offset. (to be implemented)
 *   + Virtual disk data,  starting on a 4KB page boundary. Optionally, disk
 *     data can be stored in a separate data file specified by
 *     FvdHeader.data_file.
 */
typedef struct __attribute__ ((__packed__)) FvdHeader {
    uint32_t magic;
    uint32_t version;

    /* This field is set to TRUE after whole-image prefetching finishes. */
    int32_t all_data_in_fvd_img;

    int64_t virtual_disk_size;        /* in bytes. Disk size perceived by the VM. */
    int64_t metadata_size;        /* in bytes. */
    char base_img[1024];
    char base_img_fmt[16];
    int64_t base_img_size;        /* in bytes. */
    int64_t bitmap_offset;        /* in bytes. Aligned on DEF_PAGE_SIZE. */
    int64_t bitmap_size;        /* in bytes. Rounded up to DEF_PAGE_SIZE */
    int32_t block_size;                /* in bytes. */
    int32_t copy_on_read;        /* TRUE or FALSE */
    int64_t max_outstanding_copy_on_read_data;        /* in bytes. */

    /* If (data_file[0]==0), the FVD metadata and data are stored in one file.*/
    char data_file[1024];
    char data_file_fmt[16];

    /******** Begin: for prefetching. *******************************/
    /* in seconds. -1 means disable whole image prefetching. */
    int32_t prefetch_start_delay;

    /* in bytes. Aligned on DEF_PAGE_SIZE. (to be implemented) */
    int64_t prefetch_profile_offset;

    /* Number of PrefetchProfileEntry. (to be implemented) */
    int64_t prefetch_profile_entries;

    int32_t num_prefetch_slots;        /* Max number of oustanding prefetch writes. */
    int32_t bytes_per_prefetch;        /* For whole image prefetching. */
    int32_t prefetch_read_throughput_measure_time;        /* in milliseconds. */
    int32_t prefetch_write_throughput_measure_time;        /* in milliseconds. */

    /* Controls the calculation of the moving average of throughput. Must be a
     * value between [0,100].
     *   actual_normalized_alpha = * prefetch_perf_calc_alpha / 100.0 */
    int32_t prefetch_perf_calc_alpha;

    int32_t prefetch_min_read_throughput;        /* in KB/second. */
    int32_t prefetch_min_write_throughput;        /* in KB/second. */
    int32_t prefetch_max_read_throughput;        /* in KB/second. */
    int32_t prefetch_max_write_throughput;        /* in KB/second. */

    /* in milliseconds. When prefetch read/write throughput is low, prefetch
     * pauses for a random time uniformly distributed in
     * [0, prefetch_throttle_time]. */
    int32_t prefetch_throttle_time;
    /******** End: for prefetching. *******************************/

    /******** Begin: for compact image. *****************************/
    int32_t compact_image;        /* TRUE or FALSE */
    int64_t table_offset;        /* in bytes. */
    int64_t chunk_size;                /* in bytes. */
    int64_t storage_grow_unit;        /* in bytes. */
    char add_storage_cmd[2048];
    /******** End: for compact image. *******************************/

    /******** Begin: for journal. ***********************************/
    int64_t journal_offset;        /* in bytes. */
    int64_t journal_size;        /* in bytes. */
    int32_t clean_shutdown;        /* TRUE if VM's last shutdown was graceful. */
    /******** End: for journal. *************************************/

    /*
     * This field is TRUE if the image mandates that the storage layer
     * (BDRVFvdState.fvd_data) must return TRUE for bdrv_has_zero_init().
     * This is the case if the optimization described in Section 3.3.3 of the
     * FVD-cow paper is enabled (see function search_holes()). If 'qemu-img
     * create' sets need_zero_init to TRUE, 'qemu-img update' can be used to
     * manually reset it to FALSE, if the user always manually pre-fills the
     * storage (e.g., a raw partition) with zeros. If the image is stored on a
     * file system, it already supports zero_init, and hence there is no need
     * to manually manipulate this field.
     */
    int32_t need_zero_init;

    /* If TRUE, FVD dumps a prefetch profile after the VM shuts down.
     * (to be implemented) */
    int32_t generate_prefetch_profile;

    /* See the comment on PrefetchProfileEntry.len. (to be implemented) */
    int32_t unit_of_PrefetchProfileEntry_len;

    /* in seconds. -1 means disable profile-directed prefetching.
     * (to be implemented) */
    int32_t profile_directed_prefetch_start_delay;

    /* Possible values are "no", "writethrough", "writeback", or
     * "writenocache". (to be implemented) */
    char write_updates_base_img[16];
} FvdHeader;

typedef struct BDRVFvdState {
    BlockDriverState *fvd_metadata;
    BlockDriverState *fvd_data;
    int64_t virtual_disk_size;        /*in bytes. */
    int64_t bitmap_offset;        /* in sectors */
    int64_t bitmap_size;        /* in bytes. */
    int64_t data_offset;        /* in sectors. Begin of real data. */
    int64_t nb_sectors_in_base_img;
    int32_t block_size;        /* in sectors. */
    int copy_on_read;        /* TRUE or FALSE */
    int64_t max_outstanding_copy_on_read_data;        /* in bytes. */
    int64_t outstanding_copy_on_read_data;        /* in bytes. */
    int data_region_prepared;        /* TRUE or FALSE */
     QLIST_HEAD(WriteLocks, FvdAIOCB) write_locks; /* All writes. */
     QLIST_HEAD(CopyLocks, FvdAIOCB) copy_locks; /* copy-on-read and CoW. */

    /* Keep two copies of bitmap to reduce the overhead of updating the
     * on-disk bitmap, i.e., copy-on-read and prefetching do not update the
     * on-disk bitmap. See Section 3.3.4 of the FVD-cow paper. */
    uint8_t *fresh_bitmap;
    uint8_t *stale_bitmap;

    /******** Begin: for prefetching. ***********************************/
    struct FvdAIOCB **prefetch_acb;
    int prefetch_state; /* PREFETCH_STATE_RUNNING, FINISHED, or DISABLED. */
    int prefetch_error;        /* TRUE or FALSE */
    int num_prefetch_slots;
    int num_filled_prefetch_slots;
    int next_prefetch_read_slot;
    int prefetch_read_active;                        /* TRUE or FALSE */
    int pause_prefetch_requested;                /* TRUE or FALSE */
    int prefetch_start_delay;        /* in seconds  */
    int64_t unclaimed_prefetch_region_start;
    int64_t prefetch_read_time;                        /* in milliseconds. */
    int64_t prefetch_write_time;                /* in milliseconds. */
    int64_t prefetch_data_read;                        /* in bytes. */
    int64_t prefetch_data_written;                /* in bytes. */
    double prefetch_read_throughput;                /* in bytes/millisecond. */
    double prefetch_write_throughput;                /* in bytes/millisecond. */
    double prefetch_min_read_throughput;        /* in bytes/millisecond. */
    double prefetch_min_write_throughput;        /* in bytes/millisecond. */
    int64_t prefetch_read_throughput_measure_time;        /* in millisecond. */
    int64_t prefetch_write_throughput_measure_time;        /* in millisecond.*/
    int prefetch_throttle_time;        /* in millisecond. */
    int sectors_per_prefetch;
    QEMUTimer *prefetch_timer;
    /* prefetch_perf_calc_alpha = FvdHeader.prefetch_perf_calc_alpha/100.0 */
    double prefetch_perf_calc_alpha;
    /******** End: for prefetching. ***********************************/

    /******** Begin: for compact image. *************************************/
    uint32_t *table;        /* Mapping table stored in memory in little endian. */
    int64_t data_storage;        /* in sectors. */
    int64_t used_storage;        /* in sectors. */
    int64_t chunk_size;        /* in sectors. */
    int64_t storage_grow_unit;        /* in sectors. */
    int64_t table_offset;        /* in sectors. */
    char *add_storage_cmd;
    /******** Begin: for compact image. *************************************/

    /******** Begin: for journal. *******************************************/
    int64_t journal_offset;        /* in sectors. */
    int64_t journal_size;        /* in sectors. */
    int64_t next_journal_sector;        /* in sector. */
    int ongoing_journal_updates;        /* Number of ongoing journal updates. */
    int dirty_image;        /* TRUE or FALSE. */

    /* Requests waiting for metadata flush and journal recycle to finish. */
    QLIST_HEAD(JournalFlush, FvdAIOCB) wait_for_journal;
    /******** End: for journal. ********************************************/

#ifdef FVD_DEBUG
    int64_t total_copy_on_read_data;                /* in bytes. */
    int64_t total_prefetch_data;                /* in bytes. */
#endif
} BDRVFvdState;

/* Begin of data type definitions. */
struct FvdAIOCB;

typedef struct JournalCB {
    BlockDriverAIOCB *hd_acb;
    QEMUIOVector qiov;
    struct iovec iov;
     QLIST_ENTRY(FvdAIOCB) next_wait_for_journal;
} JournalCB;

/* CopyLock is used by AIOWriteCB and AIOCopyCB. */
typedef struct CopyLock {
    QLIST_ENTRY(FvdAIOCB) next;
    int64_t begin;
    int64_t end;
     QLIST_HEAD(DependentWritesHead, FvdAIOCB) dependent_writes;
} CopyLock;

typedef struct ChildAIOReadCB {
    BlockDriverAIOCB *hd_acb;
    struct iovec iov;
    QEMUIOVector qiov;
    int64_t sector_num;
    int nb_sectors;
    int done;
} ChildAIOReadCB;

typedef struct AIOReadCB {
    QEMUIOVector *qiov;
    int ret;
    ChildAIOReadCB read_backing;
    ChildAIOReadCB read_fvd;
} AIOReadCB;

/* For copy-on-read and prefetching. */
typedef struct AIOCopyCB {
    BlockDriverAIOCB *hd_acb;
    struct iovec iov;
    QEMUIOVector qiov;
    uint8_t *buf;
    int64_t buffered_sector_begin;
    int64_t buffered_sector_end;
    int64_t last_prefetch_op_start_time;        /* For prefetch only. */
} AIOCopyCB;

typedef struct AIOWriteCB {
    BlockDriverAIOCB *hd_acb;
    QEMUIOVector *qiov;
    uint8_t *cow_buf;
    QEMUIOVector *cow_qiov;
    int64_t cow_start_sector;
    int update_table;        /* TRUE or FALSE. */
    int ret;
    QLIST_ENTRY(FvdAIOCB) next_write_lock;   /* See BDRVFvdState.write_locks */

    /* See FvdAIOCB.write.dependent_writes. */
    QLIST_ENTRY(FvdAIOCB) next_dependent_write;
} AIOWriteCB;

/* For AIOStoreCompactCB and AIOLoadCompactCB. */
typedef struct CompactChildCB {
    struct FvdAIOCB *acb;
    BlockDriverAIOCB *hd_acb;
} CompactChildCB;

/* For storing data to a compact image. */
typedef struct AIOStoreCompactCB {
    CompactChildCB one_child;
    CompactChildCB *children;
    int update_table;
    int num_children;
    int finished_children;
    struct FvdAIOCB *parent_acb;
    int ret;
    int soft_write; /*TRUE if the store is caused by copy-on-read or prefetch.*/
    QEMUIOVector *orig_qiov;
} AIOStoreCompactCB;

/* For loading data from a compact image. */
typedef struct AIOLoadCompactCB {
    CompactChildCB *children;
    CompactChildCB one_child;
    int num_children;
    int finished_children;
    struct FvdAIOCB *parent_acb;
    int ret;
    QEMUIOVector *orig_qiov;
} AIOLoadCompactCB;

typedef struct AIOFlushCB {
    BlockDriverAIOCB *data_acb;
    BlockDriverAIOCB *metadata_acb;
    int num_finished;
    int ret;
} AIOFlushCB;

typedef struct AIOWrapperCB {
    QEMUBH *bh;
} AIOWrapperCB;

typedef enum { OP_READ = 1, OP_WRITE, OP_COPY, OP_STORE_COMPACT,
    OP_LOAD_COMPACT, OP_WRAPPER, OP_FLUSH } op_type;

#ifdef FVD_DEBUG
/* For debugging memory leadk. */
typedef struct alloc_tracer_t {
    int64_t magic;
    int alloc_tracer;
    const char *alloc_file;
    int alloc_line;
    size_t size;
} alloc_tracer_t;
#endif

typedef struct FvdAIOCB {
    BlockDriverAIOCB common;
    op_type type;
    int64_t sector_num;
    int nb_sectors;
    JournalCB jcb;        /* For AIOWriteCB and AIOStoreCompactCB. */
    CopyLock copy_lock;        /* For AIOWriteCB and AIOCopyCB. */

    /* Use a union so that all requests can efficiently share one big AIOCBInfo.*/
    union {
        AIOWrapperCB wrapper;
        AIOReadCB read;
        AIOWriteCB write;
        AIOCopyCB copy;
        AIOLoadCompactCB load;
        AIOStoreCompactCB store;
        AIOFlushCB flush;
    };

#ifdef FVD_DEBUG
    int64_t magic;
    alloc_tracer_t tracer;

    /* Uniquely identifies a request across all processing activities. */
    unsigned long long int uuid;
#endif
} FvdAIOCB;

static AIOCBInfo fvd_aio_pool;
static BlockDriver bdrv_fvd;
static QEMUOptionParameter fvd_create_options[];

/* Function prototypes. */
static int do_aio_write(struct FvdAIOCB *acb);
static void finish_write_data(void *opaque, int ret);
static void restart_dependent_writes(struct FvdAIOCB *acb);
static void finish_prefetch_read(void *opaque, int ret);
static int read_fvd_header(BDRVFvdState * s, FvdHeader * header);
static int update_fvd_header(BDRVFvdState * s, FvdHeader * header);
static void fvd_aio_cancel(BlockDriverAIOCB * blockacb);
static BlockDriverAIOCB *store_data_in_compact_image(struct FvdAIOCB *acb,
            int soft_write, struct FvdAIOCB *parent_acb, BlockDriverState * bs,
            int64_t sector_num, QEMUIOVector * qiov, int nb_sectors,
            BlockDriverCompletionFunc * cb, void *opaque);
static BlockDriverAIOCB *load_data_from_compact_image(struct FvdAIOCB *acb,
            struct FvdAIOCB *parent_acb, BlockDriverState * bs,
            int64_t sector_num, QEMUIOVector * qiov, int nb_sectors,
            BlockDriverCompletionFunc * cb, void *opaque);
static void free_write_resource(struct FvdAIOCB *acb);
static void write_metadata_to_journal(struct FvdAIOCB *acb);
static void flush_metadata_to_disk(BlockDriverState * bs);
static void free_journal_sectors(BDRVFvdState * s);
static int fvd_create(const char *filename, QEMUOptionParameter *options,
                      Error **errp);
static int fvd_probe(const uint8_t * buf, int buf_size, const char *filename);
static int64_t coroutine_fn fvd_get_block_status(BlockDriverState *bs,
                                                 int64_t sector_num,
                                                 int nb_sectors, int *pnum);
static int fvd_flush(BlockDriverState * bs);
static BlockDriverAIOCB *fvd_aio_readv(BlockDriverState * bs,
            int64_t sector_num, QEMUIOVector * qiov, int nb_sectors,
            BlockDriverCompletionFunc * cb, void *opaque);
static BlockDriverAIOCB *fvd_aio_writev(BlockDriverState * bs,
            int64_t sector_num, QEMUIOVector * qiov, int nb_sectors,
            BlockDriverCompletionFunc * cb, void *opaque);
static BlockDriverAIOCB *fvd_aio_flush(BlockDriverState * bs,
            BlockDriverCompletionFunc * cb, void *opaque);
static int fvd_get_info(BlockDriverState * bs, BlockDriverInfo * bdi);
static int fvd_update(BlockDriverState * bs, int argc, char **argv);
static int fvd_has_zero_init(BlockDriverState * bs);
static void fvd_read_cancel(FvdAIOCB * acb);
static void fvd_write_cancel(FvdAIOCB * acb);
static void fvd_copy_cancel(FvdAIOCB * acb);
static void fvd_load_compact_cancel(FvdAIOCB * acb);
static void fvd_store_compact_cancel(FvdAIOCB * acb);
static void fvd_wrapper_cancel(FvdAIOCB * acb);
static void flush_metadata_to_disk_on_exit (BlockDriverState *bs);
static inline BlockDriverAIOCB *load_data(FvdAIOCB * parent_acb,
            BlockDriverState * bs, int64_t sector_num, QEMUIOVector * orig_qiov,
            int nb_sectors, BlockDriverCompletionFunc * cb, void *opaque);
static inline BlockDriverAIOCB *store_data(int soft_write,
            FvdAIOCB * parent_acb, BlockDriverState * bs, int64_t sector_num,
            QEMUIOVector * orig_qiov, int nb_sectors,
            BlockDriverCompletionFunc * cb, void *opaque);

/* Default configurations. */
#define DEF_PAGE_SIZE                                 4096        /* bytes */
#define BYTES_PER_PREFETCH                        1048576        /* bytes */
#define PREFETCH_THROTTLING_TIME                30000        /* milliseconds */
#define NUM_PREFETCH_SLOTS                        2
#define PREFETCH_MIN_MEASURE_READ_TIME                 100        /* milliseconds */
#define PREFETCH_MIN_MEASURE_WRITE_TIME         100        /* milliseconds */
#define PREFETCH_MIN_READ_THROUGHPUT                 5120        /* KB/s */
#define PREFETCH_MIN_WRITE_THROUGHPUT                 5120        /* KB/s */
#define PREFETCH_MAX_READ_THROUGHPUT                 1000000000L        /* KB/s */
#define PREFETCH_MAX_WRITE_THROUGHPUT                 1000000000L        /* KB/s */
#define PREFETCH_PERF_CALC_ALPHA                80        /* in [0,100]. */
#define MAX_OUTSTANDING_COPY_ON_READ_DATA        2000000                /* bytes */
#define MODERATE_BITMAP_SIZE                         4194304L        /* bytes */
#define CHUNK_SIZE                                1048576LL        /* bytes */
#define JOURNAL_SIZE                                16777216LL        /* bytes */
#define STORAGE_GROW_UNIT                        104857600LL        /* bytes */

/* State of BDRVFvdState.prefetch_state. */
#define PREFETCH_STATE_RUNNING                        1
#define PREFETCH_STATE_FINISHED                        2
#define PREFETCH_STATE_DISABLED                        3

/* For convience. */
#undef ROUND_UP /* override definition from osdep.h */
#define ROUND_UP(x, base)           ((((x)+(base)-1) / (base)) * (base))
#define ROUND_DOWN(x, base)           ((((x) / (base)) * (base)))
#define BOOL(x)                 ((x) ? "true" : "false")
#define EMPTY_TABLE                ((uint32_t)0xFFFFFFFF)
#define DIRTY_TABLE                ((uint32_t)0x80000000)
#define READ_TABLE(entry)         (le32_to_cpu(entry) & ~DIRTY_TABLE)
# define FVDAIOCB_MAGIC         ((uint64_t)0x3A8FCE89325B976DULL)
# define FVD_ALLOC_MAGIC         ((uint64_t)0x4A7dCEF9925B976DULL)
#define IS_EMPTY(entry)         ((entry) == EMPTY_TABLE)
#define IS_DIRTY(entry)         (le32_to_cpu(entry) & DIRTY_TABLE)
#define WRITE_TABLE(entry,id)         ((entry) = cpu_to_le32(id))
#define READ_TABLE2(entry) \
    ((entry)==EMPTY_TABLE ? EMPTY_TABLE : (le32_to_cpu(entry) & ~DIRTY_TABLE))

#define CLEAN_DIRTY(entry) \
    do {  \
        if (!IS_EMPTY(entry))  \
            entry = cpu_to_le32(le32_to_cpu(entry) & ~DIRTY_TABLE);  \
    } while (0)

#define CLEAN_DIRTY2(entry) \
    do { \
        ASSERT(!IS_EMPTY(entry)); \
        entry = cpu_to_le32(le32_to_cpu(entry) & ~DIRTY_TABLE);  \
    } while (0)
