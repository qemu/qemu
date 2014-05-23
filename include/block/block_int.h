/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#ifndef BLOCK_INT_H
#define BLOCK_INT_H

#include "block/block.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "block/coroutine.h"
#include "qemu/timer.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"
#include "monitor/monitor.h"
#include "qemu/hbitmap.h"
#include "block/snapshot.h"
#include "qemu/main-loop.h"
#include "qemu/throttle.h"

#define BLOCK_FLAG_ENCRYPT          1
#define BLOCK_FLAG_COMPAT6          4
#define BLOCK_FLAG_LAZY_REFCOUNTS   8

#define BLOCK_OPT_SIZE              "size"
#define BLOCK_OPT_ENCRYPT           "encryption"
#define BLOCK_OPT_COMPAT6           "compat6"
#define BLOCK_OPT_BACKING_FILE      "backing_file"
#define BLOCK_OPT_BACKING_FMT       "backing_fmt"
#define BLOCK_OPT_CLUSTER_SIZE      "cluster_size"
#define BLOCK_OPT_TABLE_SIZE        "table_size"
#define BLOCK_OPT_PREALLOC          "preallocation"
#define BLOCK_OPT_SUBFMT            "subformat"
#define BLOCK_OPT_COMPAT_LEVEL      "compat"
#define BLOCK_OPT_LAZY_REFCOUNTS    "lazy_refcounts"
#define BLOCK_OPT_ADAPTER_TYPE      "adapter_type"
#define BLOCK_OPT_REDUNDANCY        "redundancy"

typedef struct BdrvTrackedRequest {
    BlockDriverState *bs;
    int64_t offset;
    unsigned int bytes;
    bool is_write;

    bool serialising;
    int64_t overlap_offset;
    unsigned int overlap_bytes;

    QLIST_ENTRY(BdrvTrackedRequest) list;
    Coroutine *co; /* owner, used for deadlock detection */
    CoQueue wait_queue; /* coroutines blocked on this request */

    struct BdrvTrackedRequest *waiting_for;
} BdrvTrackedRequest;

struct BlockDriver {
    const char *format_name;
    int instance_size;

    /* set to true if the BlockDriver is a block filter */
    bool is_filter;
    /* for snapshots block filter like Quorum can implement the
     * following recursive callback.
     * It's purpose is to recurse on the filter children while calling
     * bdrv_recurse_is_first_non_filter on them.
     * For a sample implementation look in the future Quorum block filter.
     */
    bool (*bdrv_recurse_is_first_non_filter)(BlockDriverState *bs,
                                             BlockDriverState *candidate);

    int (*bdrv_probe)(const uint8_t *buf, int buf_size, const char *filename);
    int (*bdrv_probe_device)(const char *filename);

    /* Any driver implementing this callback is expected to be able to handle
     * NULL file names in its .bdrv_open() implementation */
    void (*bdrv_parse_filename)(const char *filename, QDict *options, Error **errp);
    /* Drivers not implementing bdrv_parse_filename nor bdrv_open should have
     * this field set to true, except ones that are defined only by their
     * child's bs.
     * An example of the last type will be the quorum block driver.
     */
    bool bdrv_needs_filename;

    /* For handling image reopen for split or non-split files */
    int (*bdrv_reopen_prepare)(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp);
    void (*bdrv_reopen_commit)(BDRVReopenState *reopen_state);
    void (*bdrv_reopen_abort)(BDRVReopenState *reopen_state);

    int (*bdrv_open)(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp);
    int (*bdrv_file_open)(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp);
    int (*bdrv_read)(BlockDriverState *bs, int64_t sector_num,
                     uint8_t *buf, int nb_sectors);
    int (*bdrv_write)(BlockDriverState *bs, int64_t sector_num,
                      const uint8_t *buf, int nb_sectors);
    void (*bdrv_close)(BlockDriverState *bs);
    void (*bdrv_rebind)(BlockDriverState *bs);
    int (*bdrv_create)(const char *filename, QEMUOptionParameter *options,
                       Error **errp);
    int (*bdrv_set_key)(BlockDriverState *bs, const char *key);
    int (*bdrv_make_empty)(BlockDriverState *bs);
    /* aio */
    BlockDriverAIOCB *(*bdrv_aio_readv)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_writev)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_flush)(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_discard)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);

    int coroutine_fn (*bdrv_co_readv)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_writev)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    /*
     * Efficiently zero a region of the disk image.  Typically an image format
     * would use a compact metadata representation to implement this.  This
     * function pointer may be NULL and .bdrv_co_writev() will be called
     * instead.
     */
    int coroutine_fn (*bdrv_co_write_zeroes)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, BdrvRequestFlags flags);
    int coroutine_fn (*bdrv_co_discard)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors);
    int64_t coroutine_fn (*bdrv_co_get_block_status)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum);

    /*
     * Invalidate any cached meta-data.
     */
    void (*bdrv_invalidate_cache)(BlockDriverState *bs, Error **errp);

    /*
     * Flushes all data that was already written to the OS all the way down to
     * the disk (for example raw-posix calls fsync()).
     */
    int coroutine_fn (*bdrv_co_flush_to_disk)(BlockDriverState *bs);

    /*
     * Flushes all internal caches to the OS. The data may still sit in a
     * writeback cache of the host OS, but it will survive a crash of the qemu
     * process.
     */
    int coroutine_fn (*bdrv_co_flush_to_os)(BlockDriverState *bs);

    const char *protocol_name;
    int (*bdrv_truncate)(BlockDriverState *bs, int64_t offset);

    int64_t (*bdrv_getlength)(BlockDriverState *bs);
    bool has_variable_length;
    int64_t (*bdrv_get_allocated_file_size)(BlockDriverState *bs);

    int (*bdrv_write_compressed)(BlockDriverState *bs, int64_t sector_num,
                                 const uint8_t *buf, int nb_sectors);

    int (*bdrv_snapshot_create)(BlockDriverState *bs,
                                QEMUSnapshotInfo *sn_info);
    int (*bdrv_snapshot_goto)(BlockDriverState *bs,
                              const char *snapshot_id);
    int (*bdrv_snapshot_delete)(BlockDriverState *bs,
                                const char *snapshot_id,
                                const char *name,
                                Error **errp);
    int (*bdrv_snapshot_list)(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_info);
    int (*bdrv_snapshot_load_tmp)(BlockDriverState *bs,
                                  const char *snapshot_id,
                                  const char *name,
                                  Error **errp);
    int (*bdrv_get_info)(BlockDriverState *bs, BlockDriverInfo *bdi);
    ImageInfoSpecific *(*bdrv_get_specific_info)(BlockDriverState *bs);

    int (*bdrv_save_vmstate)(BlockDriverState *bs, QEMUIOVector *qiov,
                             int64_t pos);
    int (*bdrv_load_vmstate)(BlockDriverState *bs, uint8_t *buf,
                             int64_t pos, int size);

    int (*bdrv_change_backing_file)(BlockDriverState *bs,
        const char *backing_file, const char *backing_fmt);

    /* removable device specific */
    int (*bdrv_is_inserted)(BlockDriverState *bs);
    int (*bdrv_media_changed)(BlockDriverState *bs);
    void (*bdrv_eject)(BlockDriverState *bs, bool eject_flag);
    void (*bdrv_lock_medium)(BlockDriverState *bs, bool locked);

    /* to control generic scsi devices */
    int (*bdrv_ioctl)(BlockDriverState *bs, unsigned long int req, void *buf);
    BlockDriverAIOCB *(*bdrv_aio_ioctl)(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque);

    /* List of options for creating images, terminated by name == NULL */
    QEMUOptionParameter *create_options;


    /*
     * Returns 0 for completed check, -errno for internal errors.
     * The check results are stored in result.
     */
    int (*bdrv_check)(BlockDriverState* bs, BdrvCheckResult *result,
        BdrvCheckMode fix);

    int (*bdrv_amend_options)(BlockDriverState *bs,
        QEMUOptionParameter *options);

    void (*bdrv_debug_event)(BlockDriverState *bs, BlkDebugEvent event);

    /* TODO Better pass a option string/QDict/QemuOpts to add any rule? */
    int (*bdrv_debug_breakpoint)(BlockDriverState *bs, const char *event,
        const char *tag);
    int (*bdrv_debug_remove_breakpoint)(BlockDriverState *bs,
        const char *tag);
    int (*bdrv_debug_resume)(BlockDriverState *bs, const char *tag);
    bool (*bdrv_debug_is_suspended)(BlockDriverState *bs, const char *tag);

    int (*bdrv_refresh_limits)(BlockDriverState *bs);

    /*
     * Returns 1 if newly created images are guaranteed to contain only
     * zeros, 0 otherwise.
     */
    int (*bdrv_has_zero_init)(BlockDriverState *bs);

    QLIST_ENTRY(BlockDriver) list;
};

typedef struct BlockLimits {
    /* maximum number of sectors that can be discarded at once */
    int max_discard;

    /* optimal alignment for discard requests in sectors */
    int64_t discard_alignment;

    /* maximum number of sectors that can zeroized at once */
    int max_write_zeroes;

    /* optimal alignment for write zeroes requests in sectors */
    int64_t write_zeroes_alignment;

    /* optimal transfer length in sectors */
    int opt_transfer_length;

    /* memory alignment so that no bounce buffer is needed */
    size_t opt_mem_alignment;
} BlockLimits;

typedef struct BdrvOpBlocker BdrvOpBlocker;

/*
 * Note: the function bdrv_append() copies and swaps contents of
 * BlockDriverStates, so if you add new fields to this struct, please
 * inspect bdrv_append() to determine if the new fields need to be
 * copied as well.
 */
struct BlockDriverState {
    int64_t total_sectors; /* if we are reading a disk image, give its
                              size in sectors */
    int read_only; /* if true, the media is read only */
    int open_flags; /* flags used to open the file, re-used for re-open */
    int encrypted; /* if true, the media is encrypted */
    int valid_key; /* if true, a valid encryption key has been set */
    int sg;        /* if true, the device is a /dev/sg* */
    int copy_on_read; /* if true, copy read backing sectors into image
                         note this is a reference count */

    BlockDriver *drv; /* NULL means no media */
    void *opaque;

    void *dev;                  /* attached device model, if any */
    /* TODO change to DeviceState when all users are qdevified */
    const BlockDevOps *dev_ops;
    void *dev_opaque;

    char filename[1024];
    char backing_file[1024]; /* if non zero, the image is a diff of
                                this file image */
    char backing_format[16]; /* if non-zero and backing_file exists */

    BlockDriverState *backing_hd;
    BlockDriverState *file;

    NotifierList close_notifiers;

    /* Callback before write request is processed */
    NotifierWithReturnList before_write_notifiers;

    /* number of in-flight serialising requests */
    unsigned int serialising_in_flight;

    /* I/O throttling */
    ThrottleState throttle_state;
    CoQueue      throttled_reqs[2];
    bool         io_limits_enabled;

    /* I/O stats (display with "info blockstats"). */
    uint64_t nr_bytes[BDRV_MAX_IOTYPE];
    uint64_t nr_ops[BDRV_MAX_IOTYPE];
    uint64_t total_time_ns[BDRV_MAX_IOTYPE];
    uint64_t wr_highest_sector;

    /* I/O Limits */
    BlockLimits bl;

    /* Whether the disk can expand beyond total_sectors */
    int growable;

    /* Whether produces zeros when read beyond eof */
    bool zero_beyond_eof;

    /* Alignment requirement for offset/length of I/O requests */
    unsigned int request_alignment;

    /* the block size for which the guest device expects atomicity */
    int guest_block_size;

    /* do we need to tell the quest if we have a volatile write cache? */
    int enable_write_cache;

    /* NOTE: the following infos are only hints for real hardware
       drivers. They are not used by the block driver */
    BlockdevOnError on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;

    /* the following member gives a name to every node on the bs graph. */
    char node_name[32];
    /* element of the list of named nodes building the graph */
    QTAILQ_ENTRY(BlockDriverState) node_list;
    /* Device name is the name associated with the "drive" the guest sees */
    char device_name[32];
    /* element of the list of "drives" the guest sees */
    QTAILQ_ENTRY(BlockDriverState) device_list;
    QLIST_HEAD(, BdrvDirtyBitmap) dirty_bitmaps;
    int refcnt;
    int in_use; /* users other than guest access, eg. block migration */

    QLIST_HEAD(, BdrvTrackedRequest) tracked_requests;

    /* operation blockers */
    QLIST_HEAD(, BdrvOpBlocker) op_blockers[BLOCK_OP_TYPE_MAX];

    /* long-running background operation */
    BlockJob *job;

    QDict *options;
    BlockdevDetectZeroesOptions detect_zeroes;
};

int get_tmp_filename(char *filename, int size);

void bdrv_set_io_limits(BlockDriverState *bs,
                        ThrottleConfig *cfg);


/**
 * bdrv_add_before_write_notifier:
 *
 * Register a callback that is invoked before write requests are processed but
 * after any throttling or waiting for overlapping requests.
 */
void bdrv_add_before_write_notifier(BlockDriverState *bs,
                                    NotifierWithReturn *notifier);

/**
 * bdrv_get_aio_context:
 *
 * Returns: the currently bound #AioContext
 */
AioContext *bdrv_get_aio_context(BlockDriverState *bs);

#ifdef _WIN32
int is_windows_drive(const char *filename);
#endif
void bdrv_emit_qmp_error_event(const BlockDriverState *bdrv,
                               enum MonitorEvent ev,
                               BlockErrorAction action, bool is_read);

/**
 * stream_start:
 * @bs: Block device to operate on.
 * @base: Block device that will become the new base, or %NULL to
 * flatten the whole backing file chain onto @bs.
 * @base_id: The file name that will be written to @bs as the new
 * backing file if the job completes.  Ignored if @base is %NULL.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 *
 * Start a streaming operation on @bs.  Clusters that are unallocated
 * in @bs, but allocated in any image between @base and @bs (both
 * exclusive) will be written to @bs.  At the end of a successful
 * streaming job, the backing file of @bs will be changed to
 * @base_id in the written image and to @base in the live BlockDriverState.
 */
void stream_start(BlockDriverState *bs, BlockDriverState *base,
                  const char *base_id, int64_t speed, BlockdevOnError on_error,
                  BlockDriverCompletionFunc *cb,
                  void *opaque, Error **errp);

/**
 * commit_start:
 * @bs: Active block device.
 * @top: Top block device to be committed.
 * @base: Block device that will be written into, and become the new top.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 *
 */
void commit_start(BlockDriverState *bs, BlockDriverState *base,
                 BlockDriverState *top, int64_t speed,
                 BlockdevOnError on_error, BlockDriverCompletionFunc *cb,
                 void *opaque, Error **errp);
/**
 * commit_active_start:
 * @bs: Active block device to be committed.
 * @base: Block device that will be written into, and become the new top.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 *
 */
void commit_active_start(BlockDriverState *bs, BlockDriverState *base,
                         int64_t speed,
                         BlockdevOnError on_error,
                         BlockDriverCompletionFunc *cb,
                         void *opaque, Error **errp);
/*
 * mirror_start:
 * @bs: Block device to operate on.
 * @target: Block device to write to.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @granularity: The chosen granularity for the dirty bitmap.
 * @buf_size: The amount of data that can be in flight at one time.
 * @mode: Whether to collapse all images in the chain to the target.
 * @on_source_error: The action to take upon error reading from the source.
 * @on_target_error: The action to take upon error writing to the target.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 *
 * Start a mirroring operation on @bs.  Clusters that are allocated
 * in @bs will be written to @bs until the job is cancelled or
 * manually completed.  At the end of a successful mirroring job,
 * @bs will be switched to read from @target.
 */
void mirror_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, int64_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  BlockDriverCompletionFunc *cb,
                  void *opaque, Error **errp);

/*
 * backup_start:
 * @bs: Block device to operate on.
 * @target: Block device to write to.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @sync_mode: What parts of the disk image should be copied to the destination.
 * @on_source_error: The action to take upon error reading from the source.
 * @on_target_error: The action to take upon error writing to the target.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 *
 * Start a backup operation on @bs.  Clusters in @bs are written to @target
 * until the job is cancelled or manually completed.
 */
void backup_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, MirrorSyncMode sync_mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  BlockDriverCompletionFunc *cb, void *opaque,
                  Error **errp);

#endif /* BLOCK_INT_H */
