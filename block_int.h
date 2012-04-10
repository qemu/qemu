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

#include "block.h"
#include "qemu-option.h"
#include "qemu-queue.h"
#include "qemu-coroutine.h"
#include "qemu-timer.h"
#include "qapi-types.h"

#define BLOCK_FLAG_ENCRYPT	1
#define BLOCK_FLAG_COMPAT6	4

#define BLOCK_IO_LIMIT_READ     0
#define BLOCK_IO_LIMIT_WRITE    1
#define BLOCK_IO_LIMIT_TOTAL    2

#define BLOCK_IO_SLICE_TIME     100000000
#define NANOSECONDS_PER_SECOND  1000000000.0

#define BLOCK_OPT_SIZE          "size"
#define BLOCK_OPT_ENCRYPT       "encryption"
#define BLOCK_OPT_COMPAT6       "compat6"
#define BLOCK_OPT_BACKING_FILE  "backing_file"
#define BLOCK_OPT_BACKING_FMT   "backing_fmt"
#define BLOCK_OPT_CLUSTER_SIZE  "cluster_size"
#define BLOCK_OPT_TABLE_SIZE    "table_size"
#define BLOCK_OPT_PREALLOC      "preallocation"
#define BLOCK_OPT_SUBFMT        "subformat"

typedef struct BdrvTrackedRequest BdrvTrackedRequest;

typedef struct BlockIOLimit {
    int64_t bps[3];
    int64_t iops[3];
} BlockIOLimit;

typedef struct BlockIOBaseValue {
    uint64_t bytes[2];
    uint64_t ios[2];
} BlockIOBaseValue;

typedef struct BlockJob BlockJob;

/**
 * BlockJobType:
 *
 * A class type for block job objects.
 */
typedef struct BlockJobType {
    /** Derived BlockJob struct size */
    size_t instance_size;

    /** String describing the operation, part of query-block-jobs QMP API */
    const char *job_type;

    /** Optional callback for job types that support setting a speed limit */
    int (*set_speed)(BlockJob *job, int64_t value);
} BlockJobType;

/**
 * BlockJob:
 *
 * Long-running operation on a BlockDriverState.
 */
struct BlockJob {
    /** The job type, including the job vtable.  */
    const BlockJobType *job_type;

    /** The block device on which the job is operating.  */
    BlockDriverState *bs;

    /**
     * Set to true if the job should cancel itself.  The flag must
     * always be tested just before toggling the busy flag from false
     * to true.  After a job has detected that the cancelled flag is
     * true, it should not anymore issue any I/O operation to the
     * block device.
     */
    bool cancelled;

    /**
     * Set to false by the job while it is in a quiescent state, where
     * no I/O is pending and cancellation can be processed without
     * issuing new I/O.  The busy flag must be set to false when the
     * job goes to sleep on any condition that is not detected by
     * #qemu_aio_wait, such as a timer.
     */
    bool busy;

    /** Offset that is published by the query-block-jobs QMP API */
    int64_t offset;

    /** Length that is published by the query-block-jobs QMP API */
    int64_t len;

    /** Speed that was set with @block_job_set_speed.  */
    int64_t speed;

    /** The completion function that will be called when the job completes.  */
    BlockDriverCompletionFunc *cb;

    /** The opaque value that is passed to the completion function.  */
    void *opaque;
};

struct BlockDriver {
    const char *format_name;
    int instance_size;
    int (*bdrv_probe)(const uint8_t *buf, int buf_size, const char *filename);
    int (*bdrv_probe_device)(const char *filename);
    int (*bdrv_open)(BlockDriverState *bs, int flags);
    int (*bdrv_file_open)(BlockDriverState *bs, const char *filename, int flags);
    int (*bdrv_read)(BlockDriverState *bs, int64_t sector_num,
                     uint8_t *buf, int nb_sectors);
    int (*bdrv_write)(BlockDriverState *bs, int64_t sector_num,
                      const uint8_t *buf, int nb_sectors);
    void (*bdrv_close)(BlockDriverState *bs);
    int (*bdrv_create)(const char *filename, QEMUOptionParameter *options);
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
        int64_t sector_num, int nb_sectors);
    int coroutine_fn (*bdrv_co_discard)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors);
    int coroutine_fn (*bdrv_co_is_allocated)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum);

    /*
     * Invalidate any cached meta-data.
     */
    void (*bdrv_invalidate_cache)(BlockDriverState *bs);

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
    int64_t (*bdrv_get_allocated_file_size)(BlockDriverState *bs);
    int (*bdrv_write_compressed)(BlockDriverState *bs, int64_t sector_num,
                                 const uint8_t *buf, int nb_sectors);

    int (*bdrv_snapshot_create)(BlockDriverState *bs,
                                QEMUSnapshotInfo *sn_info);
    int (*bdrv_snapshot_goto)(BlockDriverState *bs,
                              const char *snapshot_id);
    int (*bdrv_snapshot_delete)(BlockDriverState *bs, const char *snapshot_id);
    int (*bdrv_snapshot_list)(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_info);
    int (*bdrv_snapshot_load_tmp)(BlockDriverState *bs,
                                  const char *snapshot_name);
    int (*bdrv_get_info)(BlockDriverState *bs, BlockDriverInfo *bdi);

    int (*bdrv_save_vmstate)(BlockDriverState *bs, const uint8_t *buf,
                             int64_t pos, int size);
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
    int (*bdrv_check)(BlockDriverState* bs, BdrvCheckResult *result);

    void (*bdrv_debug_event)(BlockDriverState *bs, BlkDebugEvent event);

    /*
     * Returns 1 if newly created images are guaranteed to contain only
     * zeros, 0 otherwise.
     */
    int (*bdrv_has_zero_init)(BlockDriverState *bs);

    QLIST_ENTRY(BlockDriver) list;
};

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
    int keep_read_only; /* if true, the media was requested to stay read only */
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
    int is_temporary;

    BlockDriverState *backing_hd;
    BlockDriverState *file;

    /* number of in-flight copy-on-read requests */
    unsigned int copy_on_read_in_flight;

    /* the time for latest disk I/O */
    int64_t slice_time;
    int64_t slice_start;
    int64_t slice_end;
    BlockIOLimit io_limits;
    BlockIOBaseValue  io_base;
    CoQueue      throttled_reqs;
    QEMUTimer    *block_timer;
    bool         io_limits_enabled;

    /* I/O stats (display with "info blockstats"). */
    uint64_t nr_bytes[BDRV_MAX_IOTYPE];
    uint64_t nr_ops[BDRV_MAX_IOTYPE];
    uint64_t total_time_ns[BDRV_MAX_IOTYPE];
    uint64_t wr_highest_sector;

    /* Whether the disk can expand beyond total_sectors */
    int growable;

    /* the memory alignment required for the buffers handled by this driver */
    int buffer_alignment;

    /* do we need to tell the quest if we have a volatile write cache? */
    int enable_write_cache;

    /* NOTE: the following infos are only hints for real hardware
       drivers. They are not used by the block driver */
    int cyls, heads, secs, translation;
    BlockErrorAction on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;
    char device_name[32];
    unsigned long *dirty_bitmap;
    int64_t dirty_count;
    int in_use; /* users other than guest access, eg. block migration */
    QTAILQ_ENTRY(BlockDriverState) list;

    QLIST_HEAD(, BdrvTrackedRequest) tracked_requests;

    /* long-running background operation */
    BlockJob *job;
};

void get_tmp_filename(char *filename, int size);

void bdrv_set_io_limits(BlockDriverState *bs,
                        BlockIOLimit *io_limits);

#ifdef _WIN32
int is_windows_drive(const char *filename);
#endif

/**
 * block_job_create:
 * @job_type: The class object for the newly-created job.
 * @bs: The block
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 *
 * Create a new long-running block device job and return it.  The job
 * will call @cb asynchronously when the job completes.  Note that
 * @bs may have been closed at the time the @cb it is called.  If
 * this is the case, the job may be reported as either cancelled or
 * completed.
 *
 * This function is not part of the public job interface; it should be
 * called from a wrapper that is specific to the job type.
 */
void *block_job_create(const BlockJobType *job_type, BlockDriverState *bs,
                       BlockDriverCompletionFunc *cb, void *opaque);

/**
 * block_job_complete:
 * @job: The job being completed.
 * @ret: The status code.
 *
 * Call the completion function that was registered at creation time, and
 * free @job.
 */
void block_job_complete(BlockJob *job, int ret);

/**
 * block_job_set_speed:
 * @job: The job to set the speed for.
 * @speed: The new value
 *
 * Set a rate-limiting parameter for the job; the actual meaning may
 * vary depending on the job type.
 */
int block_job_set_speed(BlockJob *job, int64_t value);

/**
 * block_job_cancel:
 * @job: The job to be canceled.
 *
 * Asynchronously cancel the specified job.
 */
void block_job_cancel(BlockJob *job);

/**
 * block_job_is_cancelled:
 * @job: The job being queried.
 *
 * Returns whether the job is scheduled for cancellation.
 */
bool block_job_is_cancelled(BlockJob *job);

/**
 * block_job_cancel:
 * @job: The job to be canceled.
 *
 * Asynchronously cancel the job and wait for it to reach a quiescent
 * state.  Note that the completion callback will still be called
 * asynchronously, hence it is *not* valid to call #bdrv_delete
 * immediately after #block_job_cancel_sync.  Users of block jobs
 * will usually protect the BlockDriverState objects with a reference
 * count, should this be a concern.
 */
void block_job_cancel_sync(BlockJob *job);

/**
 * stream_start:
 * @bs: Block device to operate on.
 * @base: Block device that will become the new base, or %NULL to
 * flatten the whole backing file chain onto @bs.
 * @base_id: The file name that will be written to @bs as the new
 * backing file if the job completes.  Ignored if @base is %NULL.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 *
 * Start a streaming operation on @bs.  Clusters that are unallocated
 * in @bs, but allocated in any image between @base and @bs (both
 * exclusive) will be written to @bs.  At the end of a successful
 * streaming job, the backing file of @bs will be changed to
 * @base_id in the written image and to @base in the live BlockDriverState.
 */
int stream_start(BlockDriverState *bs, BlockDriverState *base,
                 const char *base_id, BlockDriverCompletionFunc *cb,
                 void *opaque);

#endif /* BLOCK_INT_H */
