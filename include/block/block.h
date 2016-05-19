#ifndef BLOCK_H
#define BLOCK_H

#include "block/aio.h"
#include "qemu/iov.h"
#include "qemu/option.h"
#include "qemu/coroutine.h"
#include "block/accounting.h"
#include "block/dirty-bitmap.h"
#include "qapi/qmp/qobject.h"
#include "qapi-types.h"
#include "qemu/hbitmap.h"

/* block.c */
typedef struct BlockDriver BlockDriver;
typedef struct BlockJob BlockJob;
typedef struct BdrvChild BdrvChild;
typedef struct BdrvChildRole BdrvChildRole;
typedef struct BlockJobTxn BlockJobTxn;
typedef struct BdrvNextIterator BdrvNextIterator;

typedef struct BlockDriverInfo {
    /* in bytes, 0 if irrelevant */
    int cluster_size;
    /* offset at which the VM state can be saved (0 if not possible) */
    int64_t vm_state_offset;
    bool is_dirty;
    /*
     * True if unallocated blocks read back as zeroes. This is equivalent
     * to the LBPRZ flag in the SCSI logical block provisioning page.
     */
    bool unallocated_blocks_are_zero;
    /*
     * True if the driver can optimize writing zeroes by unmapping
     * sectors. This is equivalent to the BLKDISCARDZEROES ioctl in Linux
     * with the difference that in qemu a discard is allowed to silently
     * fail. Therefore we have to use bdrv_write_zeroes with the
     * BDRV_REQ_MAY_UNMAP flag for an optimized zero write with unmapping.
     * After this call the driver has to guarantee that the contents read
     * back as zero. It is additionally required that the block device is
     * opened with BDRV_O_UNMAP flag for this to work.
     */
    bool can_write_zeroes_with_unmap;
    /*
     * True if this block driver only supports compressed writes
     */
    bool needs_compressed_writes;
} BlockDriverInfo;

typedef struct BlockFragInfo {
    uint64_t allocated_clusters;
    uint64_t total_clusters;
    uint64_t fragmented_clusters;
    uint64_t compressed_clusters;
} BlockFragInfo;

typedef enum {
    BDRV_REQ_COPY_ON_READ       = 0x1,
    BDRV_REQ_ZERO_WRITE         = 0x2,
    /* The BDRV_REQ_MAY_UNMAP flag is used to indicate that the block driver
     * is allowed to optimize a write zeroes request by unmapping (discarding)
     * blocks if it is guaranteed that the result will read back as
     * zeroes. The flag is only passed to the driver if the block device is
     * opened with BDRV_O_UNMAP.
     */
    BDRV_REQ_MAY_UNMAP          = 0x4,
    BDRV_REQ_NO_SERIALISING     = 0x8,
    BDRV_REQ_FUA                = 0x10,
} BdrvRequestFlags;

typedef struct BlockSizes {
    uint32_t phys;
    uint32_t log;
} BlockSizes;

typedef struct HDGeometry {
    uint32_t heads;
    uint32_t sectors;
    uint32_t cylinders;
} HDGeometry;

#define BDRV_O_RDWR        0x0002
#define BDRV_O_SNAPSHOT    0x0008 /* open the file read only and save writes in a snapshot */
#define BDRV_O_TEMPORARY   0x0010 /* delete the file after use */
#define BDRV_O_NOCACHE     0x0020 /* do not use the host page cache */
#define BDRV_O_NATIVE_AIO  0x0080 /* use native AIO instead of the thread pool */
#define BDRV_O_NO_BACKING  0x0100 /* don't open the backing file */
#define BDRV_O_NO_FLUSH    0x0200 /* disable flushing on this disk */
#define BDRV_O_COPY_ON_READ 0x0400 /* copy read backing sectors into image */
#define BDRV_O_INACTIVE    0x0800  /* consistency hint for migration handoff */
#define BDRV_O_CHECK       0x1000  /* open solely for consistency check */
#define BDRV_O_ALLOW_RDWR  0x2000  /* allow reopen to change from r/o to r/w */
#define BDRV_O_UNMAP       0x4000  /* execute guest UNMAP/TRIM operations */
#define BDRV_O_PROTOCOL    0x8000  /* if no block driver is explicitly given:
                                      select an appropriate protocol driver,
                                      ignoring the format layer */
#define BDRV_O_NO_IO       0x10000 /* don't initialize for I/O */

#define BDRV_O_CACHE_MASK  (BDRV_O_NOCACHE | BDRV_O_NO_FLUSH)


/* Option names of options parsed by the block layer */

#define BDRV_OPT_CACHE_WB       "cache.writeback"
#define BDRV_OPT_CACHE_DIRECT   "cache.direct"
#define BDRV_OPT_CACHE_NO_FLUSH "cache.no-flush"


#define BDRV_SECTOR_BITS   9
#define BDRV_SECTOR_SIZE   (1ULL << BDRV_SECTOR_BITS)
#define BDRV_SECTOR_MASK   ~(BDRV_SECTOR_SIZE - 1)

#define BDRV_REQUEST_MAX_SECTORS MIN(SIZE_MAX >> BDRV_SECTOR_BITS, \
                                     INT_MAX >> BDRV_SECTOR_BITS)

/*
 * Allocation status flags
 * BDRV_BLOCK_DATA: data is read from a file returned by bdrv_get_block_status.
 * BDRV_BLOCK_ZERO: sectors read as zero
 * BDRV_BLOCK_OFFSET_VALID: sector stored as raw data in a file returned by
 *                          bdrv_get_block_status.
 * BDRV_BLOCK_ALLOCATED: the content of the block is determined by this
 *                       layer (as opposed to the backing file)
 * BDRV_BLOCK_RAW: used internally to indicate that the request
 *                 was answered by the raw driver and that one
 *                 should look in bs->file directly.
 *
 * If BDRV_BLOCK_OFFSET_VALID is set, bits 9-62 represent the offset in
 * bs->file where sector data can be read from as raw data.
 *
 * DATA == 0 && ZERO == 0 means that data is read from backing_hd if present.
 *
 * DATA ZERO OFFSET_VALID
 *  t    t        t       sectors read as zero, bs->file is zero at offset
 *  t    f        t       sectors read as valid from bs->file at offset
 *  f    t        t       sectors preallocated, read as zero, bs->file not
 *                        necessarily zero at offset
 *  f    f        t       sectors preallocated but read from backing_hd,
 *                        bs->file contains garbage at offset
 *  t    t        f       sectors preallocated, read as zero, unknown offset
 *  t    f        f       sectors read from unknown file or offset
 *  f    t        f       not allocated or unknown offset, read as zero
 *  f    f        f       not allocated or unknown offset, read from backing_hd
 */
#define BDRV_BLOCK_DATA         0x01
#define BDRV_BLOCK_ZERO         0x02
#define BDRV_BLOCK_OFFSET_VALID 0x04
#define BDRV_BLOCK_RAW          0x08
#define BDRV_BLOCK_ALLOCATED    0x10
#define BDRV_BLOCK_OFFSET_MASK  BDRV_SECTOR_MASK

typedef QSIMPLEQ_HEAD(BlockReopenQueue, BlockReopenQueueEntry) BlockReopenQueue;

typedef struct BDRVReopenState {
    BlockDriverState *bs;
    int flags;
    QDict *options;
    QDict *explicit_options;
    void *opaque;
} BDRVReopenState;

/*
 * Block operation types
 */
typedef enum BlockOpType {
    BLOCK_OP_TYPE_BACKUP_SOURCE,
    BLOCK_OP_TYPE_BACKUP_TARGET,
    BLOCK_OP_TYPE_CHANGE,
    BLOCK_OP_TYPE_COMMIT_SOURCE,
    BLOCK_OP_TYPE_COMMIT_TARGET,
    BLOCK_OP_TYPE_DATAPLANE,
    BLOCK_OP_TYPE_DRIVE_DEL,
    BLOCK_OP_TYPE_EJECT,
    BLOCK_OP_TYPE_EXTERNAL_SNAPSHOT,
    BLOCK_OP_TYPE_INTERNAL_SNAPSHOT,
    BLOCK_OP_TYPE_INTERNAL_SNAPSHOT_DELETE,
    BLOCK_OP_TYPE_MIRROR_SOURCE,
    BLOCK_OP_TYPE_MIRROR_TARGET,
    BLOCK_OP_TYPE_RESIZE,
    BLOCK_OP_TYPE_STREAM,
    BLOCK_OP_TYPE_REPLACE,
    BLOCK_OP_TYPE_MAX,
} BlockOpType;

void bdrv_info_print(Monitor *mon, const QObject *data);
void bdrv_info(Monitor *mon, QObject **ret_data);
void bdrv_stats_print(Monitor *mon, const QObject *data);
void bdrv_info_stats(Monitor *mon, QObject **ret_data);

/* disk I/O throttling */
void bdrv_init(void);
void bdrv_init_with_whitelist(void);
bool bdrv_uses_whitelist(void);
BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix,
                                Error **errp);
BlockDriver *bdrv_find_format(const char *format_name);
int bdrv_create(BlockDriver *drv, const char* filename,
                QemuOpts *opts, Error **errp);
int bdrv_create_file(const char *filename, QemuOpts *opts, Error **errp);
BlockDriverState *bdrv_new_root(void);
BlockDriverState *bdrv_new(void);
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top);
void bdrv_replace_in_backing_chain(BlockDriverState *old,
                                   BlockDriverState *new);

int bdrv_parse_cache_mode(const char *mode, int *flags, bool *writethrough);
int bdrv_parse_discard_flags(const char *mode, int *flags);
BdrvChild *bdrv_open_child(const char *filename,
                           QDict *options, const char *bdref_key,
                           BlockDriverState* parent,
                           const BdrvChildRole *child_role,
                           bool allow_none, Error **errp);
void bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd);
int bdrv_open_backing_file(BlockDriverState *bs, QDict *parent_options,
                           const char *bdref_key, Error **errp);
int bdrv_open(BlockDriverState **pbs, const char *filename,
              const char *reference, QDict *options, int flags, Error **errp);
BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs,
                                    QDict *options, int flags);
int bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp);
int bdrv_reopen(BlockDriverState *bs, int bdrv_flags, Error **errp);
int bdrv_reopen_prepare(BDRVReopenState *reopen_state,
                        BlockReopenQueue *queue, Error **errp);
void bdrv_reopen_commit(BDRVReopenState *reopen_state);
void bdrv_reopen_abort(BDRVReopenState *reopen_state);
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors);
int bdrv_write_zeroes(BlockDriverState *bs, int64_t sector_num,
               int nb_sectors, BdrvRequestFlags flags);
BlockAIOCB *bdrv_aio_write_zeroes(BlockDriverState *bs, int64_t sector_num,
                                  int nb_sectors, BdrvRequestFlags flags,
                                  BlockCompletionFunc *cb, void *opaque);
int bdrv_make_zero(BlockDriverState *bs, BdrvRequestFlags flags);
int bdrv_pread(BlockDriverState *bs, int64_t offset,
               void *buf, int count);
int bdrv_pwrite(BlockDriverState *bs, int64_t offset,
                const void *buf, int count);
int bdrv_pwritev(BlockDriverState *bs, int64_t offset, QEMUIOVector *qiov);
int bdrv_pwrite_sync(BlockDriverState *bs, int64_t offset,
    const void *buf, int count);
int coroutine_fn bdrv_co_readv(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov);
int coroutine_fn bdrv_co_copy_on_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
int coroutine_fn bdrv_co_readv_no_serialising(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
int coroutine_fn bdrv_co_writev(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov);
/*
 * Efficiently zero a region of the disk image.  Note that this is a regular
 * I/O request like read or write and should have a reasonable size.  This
 * function is not suitable for zeroing the entire image in a single request
 * because it may allocate memory for the entire region.
 */
int coroutine_fn bdrv_co_write_zeroes(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, BdrvRequestFlags flags);
BlockDriverState *bdrv_find_backing_image(BlockDriverState *bs,
    const char *backing_file);
int bdrv_get_backing_file_depth(BlockDriverState *bs);
void bdrv_refresh_filename(BlockDriverState *bs);
int bdrv_truncate(BlockDriverState *bs, int64_t offset);
int64_t bdrv_nb_sectors(BlockDriverState *bs);
int64_t bdrv_getlength(BlockDriverState *bs);
int64_t bdrv_get_allocated_file_size(BlockDriverState *bs);
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr);
void bdrv_refresh_limits(BlockDriverState *bs, Error **errp);
int bdrv_commit(BlockDriverState *bs);
int bdrv_change_backing_file(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt);
void bdrv_register(BlockDriver *bdrv);
int bdrv_drop_intermediate(BlockDriverState *active, BlockDriverState *top,
                           BlockDriverState *base,
                           const char *backing_file_str);
BlockDriverState *bdrv_find_overlay(BlockDriverState *active,
                                    BlockDriverState *bs);
BlockDriverState *bdrv_find_base(BlockDriverState *bs);


typedef struct BdrvCheckResult {
    int corruptions;
    int leaks;
    int check_errors;
    int corruptions_fixed;
    int leaks_fixed;
    int64_t image_end_offset;
    BlockFragInfo bfi;
} BdrvCheckResult;

typedef enum {
    BDRV_FIX_LEAKS    = 1,
    BDRV_FIX_ERRORS   = 2,
} BdrvCheckMode;

int bdrv_check(BlockDriverState *bs, BdrvCheckResult *res, BdrvCheckMode fix);

/* The units of offset and total_work_size may be chosen arbitrarily by the
 * block driver; total_work_size may change during the course of the amendment
 * operation */
typedef void BlockDriverAmendStatusCB(BlockDriverState *bs, int64_t offset,
                                      int64_t total_work_size, void *opaque);
int bdrv_amend_options(BlockDriverState *bs_new, QemuOpts *opts,
                       BlockDriverAmendStatusCB *status_cb, void *cb_opaque);

/* external snapshots */
bool bdrv_recurse_is_first_non_filter(BlockDriverState *bs,
                                      BlockDriverState *candidate);
bool bdrv_is_first_non_filter(BlockDriverState *candidate);

/* check if a named node can be replaced when doing drive-mirror */
BlockDriverState *check_to_replace_node(BlockDriverState *parent_bs,
                                        const char *node_name, Error **errp);

/* async block I/O */
BlockAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                           QEMUIOVector *iov, int nb_sectors,
                           BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                            QEMUIOVector *iov, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *bdrv_aio_flush(BlockDriverState *bs,
                           BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *bdrv_aio_discard(BlockDriverState *bs,
                             int64_t sector_num, int nb_sectors,
                             BlockCompletionFunc *cb, void *opaque);
void bdrv_aio_cancel(BlockAIOCB *acb);
void bdrv_aio_cancel_async(BlockAIOCB *acb);

typedef struct BlockRequest {
    /* Fields to be filled by caller */
    union {
        struct {
            int64_t sector;
            int nb_sectors;
            int flags;
            QEMUIOVector *qiov;
        };
        struct {
            int req;
            void *buf;
        };
    };
    BlockCompletionFunc *cb;
    void *opaque;

    /* Filled by block layer */
    int error;
} BlockRequest;

/* sg packet commands */
int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf);
BlockAIOCB *bdrv_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque);

/* Invalidate any cached metadata used by image formats */
void bdrv_invalidate_cache(BlockDriverState *bs, Error **errp);
void bdrv_invalidate_cache_all(Error **errp);
int bdrv_inactivate_all(void);

/* Ensure contents are flushed to disk.  */
int bdrv_flush(BlockDriverState *bs);
int coroutine_fn bdrv_co_flush(BlockDriverState *bs);
void bdrv_close_all(void);
void bdrv_drain(BlockDriverState *bs);
void coroutine_fn bdrv_co_drain(BlockDriverState *bs);
void bdrv_drain_all(void);

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors);
int bdrv_co_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors);
int bdrv_has_zero_init_1(BlockDriverState *bs);
int bdrv_has_zero_init(BlockDriverState *bs);
bool bdrv_unallocated_blocks_are_zero(BlockDriverState *bs);
bool bdrv_can_write_zeroes_with_unmap(BlockDriverState *bs);
int64_t bdrv_get_block_status(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors, int *pnum,
                              BlockDriverState **file);
int64_t bdrv_get_block_status_above(BlockDriverState *bs,
                                    BlockDriverState *base,
                                    int64_t sector_num,
                                    int nb_sectors, int *pnum,
                                    BlockDriverState **file);
int bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                      int *pnum);
int bdrv_is_allocated_above(BlockDriverState *top, BlockDriverState *base,
                            int64_t sector_num, int nb_sectors, int *pnum);

int bdrv_is_read_only(BlockDriverState *bs);
int bdrv_is_sg(BlockDriverState *bs);
bool bdrv_is_inserted(BlockDriverState *bs);
int bdrv_media_changed(BlockDriverState *bs);
void bdrv_lock_medium(BlockDriverState *bs, bool locked);
void bdrv_eject(BlockDriverState *bs, bool eject_flag);
const char *bdrv_get_format_name(BlockDriverState *bs);
BlockDriverState *bdrv_find_node(const char *node_name);
BlockDeviceInfoList *bdrv_named_nodes_list(Error **errp);
BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp);
bool bdrv_chain_contains(BlockDriverState *top, BlockDriverState *base);
BlockDriverState *bdrv_next_node(BlockDriverState *bs);
BdrvNextIterator *bdrv_next(BdrvNextIterator *it, BlockDriverState **bs);
BlockDriverState *bdrv_next_monitor_owned(BlockDriverState *bs);
int bdrv_is_encrypted(BlockDriverState *bs);
int bdrv_key_required(BlockDriverState *bs);
int bdrv_set_key(BlockDriverState *bs, const char *key);
void bdrv_add_key(BlockDriverState *bs, const char *key, Error **errp);
int bdrv_query_missing_keys(void);
void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque);
const char *bdrv_get_node_name(const BlockDriverState *bs);
const char *bdrv_get_device_name(const BlockDriverState *bs);
const char *bdrv_get_device_or_node_name(const BlockDriverState *bs);
int bdrv_get_flags(BlockDriverState *bs);
int bdrv_write_compressed(BlockDriverState *bs, int64_t sector_num,
                          const uint8_t *buf, int nb_sectors);
int bdrv_get_info(BlockDriverState *bs, BlockDriverInfo *bdi);
ImageInfoSpecific *bdrv_get_specific_info(BlockDriverState *bs);
void bdrv_round_to_clusters(BlockDriverState *bs,
                            int64_t sector_num, int nb_sectors,
                            int64_t *cluster_sector_num,
                            int *cluster_nb_sectors);

const char *bdrv_get_encrypted_filename(BlockDriverState *bs);
void bdrv_get_backing_filename(BlockDriverState *bs,
                               char *filename, int filename_size);
void bdrv_get_full_backing_filename(BlockDriverState *bs,
                                    char *dest, size_t sz, Error **errp);
void bdrv_get_full_backing_filename_from_filename(const char *backed,
                                                  const char *backing,
                                                  char *dest, size_t sz,
                                                  Error **errp);
int bdrv_is_snapshot(BlockDriverState *bs);

int path_has_protocol(const char *path);
int path_is_absolute(const char *path);
void path_combine(char *dest, int dest_size,
                  const char *base_path,
                  const char *filename);

int bdrv_writev_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos);
int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size);

int bdrv_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                      int64_t pos, int size);

void bdrv_img_create(const char *filename, const char *fmt,
                     const char *base_filename, const char *base_fmt,
                     char *options, uint64_t img_size, int flags,
                     Error **errp, bool quiet);

/* Returns the alignment in bytes that is required so that no bounce buffer
 * is required throughout the stack */
size_t bdrv_min_mem_align(BlockDriverState *bs);
/* Returns optimal alignment in bytes for bounce buffer */
size_t bdrv_opt_mem_align(BlockDriverState *bs);
void *qemu_blockalign(BlockDriverState *bs, size_t size);
void *qemu_blockalign0(BlockDriverState *bs, size_t size);
void *qemu_try_blockalign(BlockDriverState *bs, size_t size);
void *qemu_try_blockalign0(BlockDriverState *bs, size_t size);
bool bdrv_qiov_is_aligned(BlockDriverState *bs, QEMUIOVector *qiov);

void bdrv_enable_copy_on_read(BlockDriverState *bs);
void bdrv_disable_copy_on_read(BlockDriverState *bs);

void bdrv_ref(BlockDriverState *bs);
void bdrv_unref(BlockDriverState *bs);
void bdrv_unref_child(BlockDriverState *parent, BdrvChild *child);
BdrvChild *bdrv_attach_child(BlockDriverState *parent_bs,
                             BlockDriverState *child_bs,
                             const char *child_name,
                             const BdrvChildRole *child_role);

bool bdrv_op_is_blocked(BlockDriverState *bs, BlockOpType op, Error **errp);
void bdrv_op_block(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_unblock(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_block_all(BlockDriverState *bs, Error *reason);
void bdrv_op_unblock_all(BlockDriverState *bs, Error *reason);
bool bdrv_op_blocker_is_empty(BlockDriverState *bs);

#define BLKDBG_EVENT(child, evt) \
    do { \
        if (child) { \
            bdrv_debug_event(child->bs, evt); \
        } \
    } while (0)

void bdrv_debug_event(BlockDriverState *bs, BlkdebugEvent event);

int bdrv_debug_breakpoint(BlockDriverState *bs, const char *event,
                           const char *tag);
int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag);
int bdrv_debug_resume(BlockDriverState *bs, const char *tag);
bool bdrv_debug_is_suspended(BlockDriverState *bs, const char *tag);

/**
 * bdrv_get_aio_context:
 *
 * Returns: the currently bound #AioContext
 */
AioContext *bdrv_get_aio_context(BlockDriverState *bs);

/**
 * bdrv_set_aio_context:
 *
 * Changes the #AioContext used for fd handlers, timers, and BHs by this
 * BlockDriverState and all its children.
 *
 * This function must be called with iothread lock held.
 */
void bdrv_set_aio_context(BlockDriverState *bs, AioContext *new_context);
int bdrv_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz);
int bdrv_probe_geometry(BlockDriverState *bs, HDGeometry *geo);

void bdrv_io_plug(BlockDriverState *bs);
void bdrv_io_unplug(BlockDriverState *bs);
void bdrv_io_unplugged_begin(BlockDriverState *bs);
void bdrv_io_unplugged_end(BlockDriverState *bs);

/**
 * bdrv_drained_begin:
 *
 * Begin a quiesced section for exclusive access to the BDS, by disabling
 * external request sources including NBD server and device model. Note that
 * this doesn't block timers or coroutines from submitting more requests, which
 * means block_job_pause is still necessary.
 *
 * This function can be recursive.
 */
void bdrv_drained_begin(BlockDriverState *bs);

/**
 * bdrv_drained_end:
 *
 * End a quiescent section started by bdrv_drained_begin().
 */
void bdrv_drained_end(BlockDriverState *bs);

void bdrv_add_child(BlockDriverState *parent, BlockDriverState *child,
                    Error **errp);
void bdrv_del_child(BlockDriverState *parent, BdrvChild *child, Error **errp);

#endif
