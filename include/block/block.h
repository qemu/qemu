#ifndef BLOCK_H
#define BLOCK_H

#include "block/aio.h"
#include "qemu-common.h"
#include "qemu/option.h"
#include "block/coroutine.h"
#include "qapi/qmp/qobject.h"
#include "qapi-types.h"

/* block.c */
typedef struct BlockDriver BlockDriver;
typedef struct BlockJob BlockJob;

typedef struct BlockDriverInfo {
    /* in bytes, 0 if irrelevant */
    int cluster_size;
    /* offset at which the VM state can be saved (0 if not possible) */
    int64_t vm_state_offset;
    bool is_dirty;
    /*
     * True if unallocated blocks read back as zeroes. This is equivalent
     * to the the LBPRZ flag in the SCSI logical block provisioning page.
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

/* Callbacks for block device models */
typedef struct BlockDevOps {
    /*
     * Runs when virtual media changed (monitor commands eject, change)
     * Argument load is true on load and false on eject.
     * Beware: doesn't run when a host device's physical media
     * changes.  Sure would be useful if it did.
     * Device models with removable media must implement this callback.
     */
    void (*change_media_cb)(void *opaque, bool load);
    /*
     * Runs when an eject request is issued from the monitor, the tray
     * is closed, and the medium is locked.
     * Device models that do not implement is_medium_locked will not need
     * this callback.  Device models that can lock the medium or tray might
     * want to implement the callback and unlock the tray when "force" is
     * true, even if they do not support eject requests.
     */
    void (*eject_request_cb)(void *opaque, bool force);
    /*
     * Is the virtual tray open?
     * Device models implement this only when the device has a tray.
     */
    bool (*is_tray_open)(void *opaque);
    /*
     * Is the virtual medium locked into the device?
     * Device models implement this only when device has such a lock.
     */
    bool (*is_medium_locked)(void *opaque);
    /*
     * Runs when the size changed (e.g. monitor command block_resize)
     */
    void (*resize_cb)(void *opaque);
} BlockDevOps;

typedef enum {
    BDRV_REQ_COPY_ON_READ = 0x1,
    BDRV_REQ_ZERO_WRITE   = 0x2,
    /* The BDRV_REQ_MAY_UNMAP flag is used to indicate that the block driver
     * is allowed to optimize a write zeroes request by unmapping (discarding)
     * blocks if it is guaranteed that the result will read back as
     * zeroes. The flag is only passed to the driver if the block device is
     * opened with BDRV_O_UNMAP.
     */
    BDRV_REQ_MAY_UNMAP    = 0x4,
} BdrvRequestFlags;

#define BDRV_O_RDWR        0x0002
#define BDRV_O_SNAPSHOT    0x0008 /* open the file read only and save writes in a snapshot */
#define BDRV_O_TEMPORARY   0x0010 /* delete the file after use */
#define BDRV_O_NOCACHE     0x0020 /* do not use the host page cache */
#define BDRV_O_CACHE_WB    0x0040 /* use write-back caching */
#define BDRV_O_NATIVE_AIO  0x0080 /* use native AIO instead of the thread pool */
#define BDRV_O_NO_BACKING  0x0100 /* don't open the backing file */
#define BDRV_O_NO_FLUSH    0x0200 /* disable flushing on this disk */
#define BDRV_O_COPY_ON_READ 0x0400 /* copy read backing sectors into image */
#define BDRV_O_INCOMING    0x0800  /* consistency hint for incoming migration */
#define BDRV_O_CHECK       0x1000  /* open solely for consistency check */
#define BDRV_O_ALLOW_RDWR  0x2000  /* allow reopen to change from r/o to r/w */
#define BDRV_O_UNMAP       0x4000  /* execute guest UNMAP/TRIM operations */
#define BDRV_O_PROTOCOL    0x8000  /* if no block driver is explicitly given:
                                      select an appropriate protocol driver,
                                      ignoring the format layer */

#define BDRV_O_CACHE_MASK  (BDRV_O_NOCACHE | BDRV_O_CACHE_WB | BDRV_O_NO_FLUSH)

#define BDRV_SECTOR_BITS   9
#define BDRV_SECTOR_SIZE   (1ULL << BDRV_SECTOR_BITS)
#define BDRV_SECTOR_MASK   ~(BDRV_SECTOR_SIZE - 1)

/* BDRV_BLOCK_DATA: data is read from bs->file or another file
 * BDRV_BLOCK_ZERO: sectors read as zero
 * BDRV_BLOCK_OFFSET_VALID: sector stored in bs->file as raw data
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
    void *opaque;
} BDRVReopenState;

/*
 * Block operation types
 */
typedef enum BlockOpType {
    BLOCK_OP_TYPE_BACKUP_SOURCE,
    BLOCK_OP_TYPE_BACKUP_TARGET,
    BLOCK_OP_TYPE_CHANGE,
    BLOCK_OP_TYPE_COMMIT,
    BLOCK_OP_TYPE_DATAPLANE,
    BLOCK_OP_TYPE_DRIVE_DEL,
    BLOCK_OP_TYPE_EJECT,
    BLOCK_OP_TYPE_EXTERNAL_SNAPSHOT,
    BLOCK_OP_TYPE_INTERNAL_SNAPSHOT,
    BLOCK_OP_TYPE_INTERNAL_SNAPSHOT_DELETE,
    BLOCK_OP_TYPE_MIRROR,
    BLOCK_OP_TYPE_RESIZE,
    BLOCK_OP_TYPE_STREAM,
    BLOCK_OP_TYPE_REPLACE,
    BLOCK_OP_TYPE_MAX,
} BlockOpType;

void bdrv_iostatus_enable(BlockDriverState *bs);
void bdrv_iostatus_reset(BlockDriverState *bs);
void bdrv_iostatus_disable(BlockDriverState *bs);
bool bdrv_iostatus_is_enabled(const BlockDriverState *bs);
void bdrv_iostatus_set_err(BlockDriverState *bs, int error);
void bdrv_info_print(Monitor *mon, const QObject *data);
void bdrv_info(Monitor *mon, QObject **ret_data);
void bdrv_stats_print(Monitor *mon, const QObject *data);
void bdrv_info_stats(Monitor *mon, QObject **ret_data);

/* disk I/O throttling */
void bdrv_io_limits_enable(BlockDriverState *bs);
void bdrv_io_limits_disable(BlockDriverState *bs);

void bdrv_init(void);
void bdrv_init_with_whitelist(void);
BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix);
BlockDriver *bdrv_find_format(const char *format_name);
BlockDriver *bdrv_find_whitelisted_format(const char *format_name,
                                          bool readonly);
int bdrv_create(BlockDriver *drv, const char* filename,
                QemuOpts *opts, Error **errp);
int bdrv_create_file(const char *filename, QemuOpts *opts, Error **errp);
BlockDriverState *bdrv_new(const char *device_name, Error **errp);
void bdrv_make_anon(BlockDriverState *bs);
void bdrv_swap(BlockDriverState *bs_new, BlockDriverState *bs_old);
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top);
int bdrv_parse_cache_flags(const char *mode, int *flags);
int bdrv_parse_discard_flags(const char *mode, int *flags);
int bdrv_open_image(BlockDriverState **pbs, const char *filename,
                    QDict *options, const char *bdref_key, int flags,
                    bool allow_none, Error **errp);
void bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd);
int bdrv_open_backing_file(BlockDriverState *bs, QDict *options, Error **errp);
int bdrv_append_temp_snapshot(BlockDriverState *bs, int flags, Error **errp);
int bdrv_open(BlockDriverState **pbs, const char *filename,
              const char *reference, QDict *options, int flags,
              BlockDriver *drv, Error **errp);
BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs, int flags);
int bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp);
int bdrv_reopen(BlockDriverState *bs, int bdrv_flags, Error **errp);
int bdrv_reopen_prepare(BDRVReopenState *reopen_state,
                        BlockReopenQueue *queue, Error **errp);
void bdrv_reopen_commit(BDRVReopenState *reopen_state);
void bdrv_reopen_abort(BDRVReopenState *reopen_state);
void bdrv_close(BlockDriverState *bs);
void bdrv_add_close_notifier(BlockDriverState *bs, Notifier *notify);
int bdrv_attach_dev(BlockDriverState *bs, void *dev);
void bdrv_attach_dev_nofail(BlockDriverState *bs, void *dev);
void bdrv_detach_dev(BlockDriverState *bs, void *dev);
void *bdrv_get_attached_dev(BlockDriverState *bs);
void bdrv_set_dev_ops(BlockDriverState *bs, const BlockDevOps *ops,
                      void *opaque);
void bdrv_dev_eject_request(BlockDriverState *bs, bool force);
bool bdrv_dev_has_removable_media(BlockDriverState *bs);
bool bdrv_dev_is_tray_open(BlockDriverState *bs);
bool bdrv_dev_is_medium_locked(BlockDriverState *bs);
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors);
int bdrv_read_unthrottled(BlockDriverState *bs, int64_t sector_num,
                          uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors);
int bdrv_write_zeroes(BlockDriverState *bs, int64_t sector_num,
               int nb_sectors, BdrvRequestFlags flags);
BlockDriverAIOCB *bdrv_aio_write_zeroes(BlockDriverState *bs, int64_t sector_num,
                                        int nb_sectors, BdrvRequestFlags flags,
                                        BlockDriverCompletionFunc *cb, void *opaque);
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
int bdrv_truncate(BlockDriverState *bs, int64_t offset);
int64_t bdrv_getlength(BlockDriverState *bs);
int64_t bdrv_get_allocated_file_size(BlockDriverState *bs);
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr);
int bdrv_refresh_limits(BlockDriverState *bs);
int bdrv_commit(BlockDriverState *bs);
int bdrv_commit_all(void);
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

int bdrv_amend_options(BlockDriverState *bs_new, QemuOpts *opts);

/* external snapshots */
bool bdrv_recurse_is_first_non_filter(BlockDriverState *bs,
                                      BlockDriverState *candidate);
bool bdrv_is_first_non_filter(BlockDriverState *candidate);

/* check if a named node can be replaced when doing drive-mirror */
BlockDriverState *check_to_replace_node(const char *node_name, Error **errp);

/* async block I/O */
typedef void BlockDriverDirtyHandler(BlockDriverState *bs, int64_t sector,
                                     int sector_num);
BlockDriverAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                 QEMUIOVector *iov, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                  QEMUIOVector *iov, int nb_sectors,
                                  BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_flush(BlockDriverState *bs,
                                 BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_discard(BlockDriverState *bs,
                                   int64_t sector_num, int nb_sectors,
                                   BlockDriverCompletionFunc *cb, void *opaque);
void bdrv_aio_cancel(BlockDriverAIOCB *acb);

typedef struct BlockRequest {
    /* Fields to be filled by multiwrite caller */
    int64_t sector;
    int nb_sectors;
    int flags;
    QEMUIOVector *qiov;
    BlockDriverCompletionFunc *cb;
    void *opaque;

    /* Filled by multiwrite implementation */
    int error;
} BlockRequest;

int bdrv_aio_multiwrite(BlockDriverState *bs, BlockRequest *reqs,
    int num_reqs);

/* sg packet commands */
int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf);
BlockDriverAIOCB *bdrv_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque);

/* Invalidate any cached metadata used by image formats */
void bdrv_invalidate_cache(BlockDriverState *bs, Error **errp);
void bdrv_invalidate_cache_all(Error **errp);

void bdrv_clear_incoming_migration_all(void);

/* Ensure contents are flushed to disk.  */
int bdrv_flush(BlockDriverState *bs);
int coroutine_fn bdrv_co_flush(BlockDriverState *bs);
int bdrv_flush_all(void);
void bdrv_close_all(void);
void bdrv_drain_all(void);

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors);
int bdrv_co_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors);
int bdrv_has_zero_init_1(BlockDriverState *bs);
int bdrv_has_zero_init(BlockDriverState *bs);
bool bdrv_unallocated_blocks_are_zero(BlockDriverState *bs);
bool bdrv_can_write_zeroes_with_unmap(BlockDriverState *bs);
int64_t bdrv_get_block_status(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors, int *pnum);
int bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                      int *pnum);
int bdrv_is_allocated_above(BlockDriverState *top, BlockDriverState *base,
                            int64_t sector_num, int nb_sectors, int *pnum);

void bdrv_set_on_error(BlockDriverState *bs, BlockdevOnError on_read_error,
                       BlockdevOnError on_write_error);
BlockdevOnError bdrv_get_on_error(BlockDriverState *bs, bool is_read);
BlockErrorAction bdrv_get_error_action(BlockDriverState *bs, bool is_read, int error);
void bdrv_error_action(BlockDriverState *bs, BlockErrorAction action,
                       bool is_read, int error);
int bdrv_is_read_only(BlockDriverState *bs);
int bdrv_is_sg(BlockDriverState *bs);
int bdrv_enable_write_cache(BlockDriverState *bs);
void bdrv_set_enable_write_cache(BlockDriverState *bs, bool wce);
int bdrv_is_inserted(BlockDriverState *bs);
int bdrv_media_changed(BlockDriverState *bs);
void bdrv_lock_medium(BlockDriverState *bs, bool locked);
void bdrv_eject(BlockDriverState *bs, bool eject_flag);
const char *bdrv_get_format_name(BlockDriverState *bs);
BlockDriverState *bdrv_find(const char *name);
BlockDriverState *bdrv_find_node(const char *node_name);
BlockDeviceInfoList *bdrv_named_nodes_list(void);
BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp);
bool bdrv_chain_contains(BlockDriverState *top, BlockDriverState *base);
BlockDriverState *bdrv_next(BlockDriverState *bs);
void bdrv_iterate(void (*it)(void *opaque, BlockDriverState *bs),
                  void *opaque);
int bdrv_is_encrypted(BlockDriverState *bs);
int bdrv_key_required(BlockDriverState *bs);
int bdrv_set_key(BlockDriverState *bs, const char *key);
int bdrv_query_missing_keys(void);
void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque);
const char *bdrv_get_device_name(BlockDriverState *bs);
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
                                    char *dest, size_t sz);
int bdrv_is_snapshot(BlockDriverState *bs);

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
size_t bdrv_opt_mem_align(BlockDriverState *bs);
void bdrv_set_guest_block_size(BlockDriverState *bs, int align);
void *qemu_blockalign(BlockDriverState *bs, size_t size);
bool bdrv_qiov_is_aligned(BlockDriverState *bs, QEMUIOVector *qiov);

struct HBitmapIter;
typedef struct BdrvDirtyBitmap BdrvDirtyBitmap;
BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs, int granularity,
                                          Error **errp);
void bdrv_release_dirty_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap);
BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs);
int bdrv_get_dirty(BlockDriverState *bs, BdrvDirtyBitmap *bitmap, int64_t sector);
void bdrv_set_dirty(BlockDriverState *bs, int64_t cur_sector, int nr_sectors);
void bdrv_reset_dirty(BlockDriverState *bs, int64_t cur_sector, int nr_sectors);
void bdrv_dirty_iter_init(BlockDriverState *bs,
                          BdrvDirtyBitmap *bitmap, struct HBitmapIter *hbi);
int64_t bdrv_get_dirty_count(BlockDriverState *bs, BdrvDirtyBitmap *bitmap);

void bdrv_enable_copy_on_read(BlockDriverState *bs);
void bdrv_disable_copy_on_read(BlockDriverState *bs);

void bdrv_ref(BlockDriverState *bs);
void bdrv_unref(BlockDriverState *bs);

bool bdrv_op_is_blocked(BlockDriverState *bs, BlockOpType op, Error **errp);
void bdrv_op_block(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_unblock(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_block_all(BlockDriverState *bs, Error *reason);
void bdrv_op_unblock_all(BlockDriverState *bs, Error *reason);
bool bdrv_op_blocker_is_empty(BlockDriverState *bs);

enum BlockAcctType {
    BDRV_ACCT_READ,
    BDRV_ACCT_WRITE,
    BDRV_ACCT_FLUSH,
    BDRV_MAX_IOTYPE,
};

typedef struct BlockAcctCookie {
    int64_t bytes;
    int64_t start_time_ns;
    enum BlockAcctType type;
} BlockAcctCookie;

void bdrv_acct_start(BlockDriverState *bs, BlockAcctCookie *cookie,
        int64_t bytes, enum BlockAcctType type);
void bdrv_acct_done(BlockDriverState *bs, BlockAcctCookie *cookie);

typedef enum {
    BLKDBG_L1_UPDATE,

    BLKDBG_L1_GROW_ALLOC_TABLE,
    BLKDBG_L1_GROW_WRITE_TABLE,
    BLKDBG_L1_GROW_ACTIVATE_TABLE,

    BLKDBG_L2_LOAD,
    BLKDBG_L2_UPDATE,
    BLKDBG_L2_UPDATE_COMPRESSED,
    BLKDBG_L2_ALLOC_COW_READ,
    BLKDBG_L2_ALLOC_WRITE,

    BLKDBG_READ_AIO,
    BLKDBG_READ_BACKING_AIO,
    BLKDBG_READ_COMPRESSED,

    BLKDBG_WRITE_AIO,
    BLKDBG_WRITE_COMPRESSED,

    BLKDBG_VMSTATE_LOAD,
    BLKDBG_VMSTATE_SAVE,

    BLKDBG_COW_READ,
    BLKDBG_COW_WRITE,

    BLKDBG_REFTABLE_LOAD,
    BLKDBG_REFTABLE_GROW,
    BLKDBG_REFTABLE_UPDATE,

    BLKDBG_REFBLOCK_LOAD,
    BLKDBG_REFBLOCK_UPDATE,
    BLKDBG_REFBLOCK_UPDATE_PART,
    BLKDBG_REFBLOCK_ALLOC,
    BLKDBG_REFBLOCK_ALLOC_HOOKUP,
    BLKDBG_REFBLOCK_ALLOC_WRITE,
    BLKDBG_REFBLOCK_ALLOC_WRITE_BLOCKS,
    BLKDBG_REFBLOCK_ALLOC_WRITE_TABLE,
    BLKDBG_REFBLOCK_ALLOC_SWITCH_TABLE,

    BLKDBG_CLUSTER_ALLOC,
    BLKDBG_CLUSTER_ALLOC_BYTES,
    BLKDBG_CLUSTER_FREE,

    BLKDBG_FLUSH_TO_OS,
    BLKDBG_FLUSH_TO_DISK,

    BLKDBG_PWRITEV_RMW_HEAD,
    BLKDBG_PWRITEV_RMW_AFTER_HEAD,
    BLKDBG_PWRITEV_RMW_TAIL,
    BLKDBG_PWRITEV_RMW_AFTER_TAIL,
    BLKDBG_PWRITEV,
    BLKDBG_PWRITEV_ZERO,
    BLKDBG_PWRITEV_DONE,

    BLKDBG_EVENT_MAX,
} BlkDebugEvent;

#define BLKDBG_EVENT(bs, evt) bdrv_debug_event(bs, evt)
void bdrv_debug_event(BlockDriverState *bs, BlkDebugEvent event);

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
 * This function must be called from the old #AioContext or with a lock held so
 * the old #AioContext is not executing.
 */
void bdrv_set_aio_context(BlockDriverState *bs, AioContext *new_context);

#endif
