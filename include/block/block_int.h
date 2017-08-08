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

#include "block/accounting.h"
#include "block/block.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "qemu/coroutine.h"
#include "qemu/stats64.h"
#include "qemu/timer.h"
#include "qapi-types.h"
#include "qemu/hbitmap.h"
#include "block/snapshot.h"
#include "qemu/main-loop.h"
#include "qemu/throttle.h"

#define BLOCK_FLAG_LAZY_REFCOUNTS   8

#define BLOCK_OPT_SIZE              "size"
#define BLOCK_OPT_ENCRYPT           "encryption"
#define BLOCK_OPT_ENCRYPT_FORMAT    "encrypt.format"
#define BLOCK_OPT_COMPAT6           "compat6"
#define BLOCK_OPT_HWVERSION         "hwversion"
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
#define BLOCK_OPT_NOCOW             "nocow"
#define BLOCK_OPT_OBJECT_SIZE       "object_size"
#define BLOCK_OPT_REFCOUNT_BITS     "refcount_bits"

#define BLOCK_PROBE_BUF_SIZE        512

enum BdrvTrackedRequestType {
    BDRV_TRACKED_READ,
    BDRV_TRACKED_WRITE,
    BDRV_TRACKED_DISCARD,
};

typedef struct BdrvTrackedRequest {
    BlockDriverState *bs;
    int64_t offset;
    unsigned int bytes;
    enum BdrvTrackedRequestType type;

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

    /* Set if a driver can support backing files */
    bool supports_backing;

    /* For handling image reopen for split or non-split files */
    int (*bdrv_reopen_prepare)(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp);
    void (*bdrv_reopen_commit)(BDRVReopenState *reopen_state);
    void (*bdrv_reopen_abort)(BDRVReopenState *reopen_state);
    void (*bdrv_join_options)(QDict *options, QDict *old_options);

    int (*bdrv_open)(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp);
    int (*bdrv_file_open)(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp);
    void (*bdrv_close)(BlockDriverState *bs);
    int (*bdrv_create)(const char *filename, QemuOpts *opts, Error **errp);
    int (*bdrv_make_empty)(BlockDriverState *bs);

    void (*bdrv_refresh_filename)(BlockDriverState *bs, QDict *options);

    /* aio */
    BlockAIOCB *(*bdrv_aio_readv)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_writev)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_flush)(BlockDriverState *bs,
        BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_pdiscard)(BlockDriverState *bs,
        int64_t offset, int bytes,
        BlockCompletionFunc *cb, void *opaque);

    int coroutine_fn (*bdrv_co_readv)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_preadv)(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags);
    int coroutine_fn (*bdrv_co_writev)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_writev_flags)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov, int flags);
    int coroutine_fn (*bdrv_co_pwritev)(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, QEMUIOVector *qiov, int flags);

    /*
     * Efficiently zero a region of the disk image.  Typically an image format
     * would use a compact metadata representation to implement this.  This
     * function pointer may be NULL or return -ENOSUP and .bdrv_co_writev()
     * will be called instead.
     */
    int coroutine_fn (*bdrv_co_pwrite_zeroes)(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags);
    int coroutine_fn (*bdrv_co_pdiscard)(BlockDriverState *bs,
        int64_t offset, int bytes);

    /*
     * Building block for bdrv_block_status[_above]. The driver should
     * answer only according to the current layer, and should not
     * set BDRV_BLOCK_ALLOCATED, but may set BDRV_BLOCK_RAW.  See block.h
     * for the meaning of _DATA, _ZERO, and _OFFSET_VALID.
     */
    int64_t coroutine_fn (*bdrv_co_get_block_status)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum,
        BlockDriverState **file);

    /*
     * Invalidate any cached meta-data.
     */
    void (*bdrv_invalidate_cache)(BlockDriverState *bs, Error **errp);
    int (*bdrv_inactivate)(BlockDriverState *bs);

    /*
     * Flushes all data for all layers by calling bdrv_co_flush for underlying
     * layers, if needed. This function is needed for deterministic
     * synchronization of the flush finishing callback.
     */
    int coroutine_fn (*bdrv_co_flush)(BlockDriverState *bs);

    /*
     * Flushes all data that was already written to the OS all the way down to
     * the disk (for example file-posix.c calls fsync()).
     */
    int coroutine_fn (*bdrv_co_flush_to_disk)(BlockDriverState *bs);

    /*
     * Flushes all internal caches to the OS. The data may still sit in a
     * writeback cache of the host OS, but it will survive a crash of the qemu
     * process.
     */
    int coroutine_fn (*bdrv_co_flush_to_os)(BlockDriverState *bs);

    const char *protocol_name;
    int (*bdrv_truncate)(BlockDriverState *bs, int64_t offset,
                         PreallocMode prealloc, Error **errp);

    int64_t (*bdrv_getlength)(BlockDriverState *bs);
    bool has_variable_length;
    int64_t (*bdrv_get_allocated_file_size)(BlockDriverState *bs);
    BlockMeasureInfo *(*bdrv_measure)(QemuOpts *opts, BlockDriverState *in_bs,
                                      Error **errp);

    int coroutine_fn (*bdrv_co_pwritev_compressed)(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, QEMUIOVector *qiov);

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

    int coroutine_fn (*bdrv_save_vmstate)(BlockDriverState *bs,
                                          QEMUIOVector *qiov,
                                          int64_t pos);
    int coroutine_fn (*bdrv_load_vmstate)(BlockDriverState *bs,
                                          QEMUIOVector *qiov,
                                          int64_t pos);

    int (*bdrv_change_backing_file)(BlockDriverState *bs,
        const char *backing_file, const char *backing_fmt);

    /* removable device specific */
    bool (*bdrv_is_inserted)(BlockDriverState *bs);
    int (*bdrv_media_changed)(BlockDriverState *bs);
    void (*bdrv_eject)(BlockDriverState *bs, bool eject_flag);
    void (*bdrv_lock_medium)(BlockDriverState *bs, bool locked);

    /* to control generic scsi devices */
    BlockAIOCB *(*bdrv_aio_ioctl)(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque);
    int coroutine_fn (*bdrv_co_ioctl)(BlockDriverState *bs,
                                      unsigned long int req, void *buf);

    /* List of options for creating images, terminated by name == NULL */
    QemuOptsList *create_opts;

    /*
     * Returns 0 for completed check, -errno for internal errors.
     * The check results are stored in result.
     */
    int (*bdrv_check)(BlockDriverState *bs, BdrvCheckResult *result,
        BdrvCheckMode fix);

    int (*bdrv_amend_options)(BlockDriverState *bs, QemuOpts *opts,
                              BlockDriverAmendStatusCB *status_cb,
                              void *cb_opaque);

    void (*bdrv_debug_event)(BlockDriverState *bs, BlkdebugEvent event);

    /* TODO Better pass a option string/QDict/QemuOpts to add any rule? */
    int (*bdrv_debug_breakpoint)(BlockDriverState *bs, const char *event,
        const char *tag);
    int (*bdrv_debug_remove_breakpoint)(BlockDriverState *bs,
        const char *tag);
    int (*bdrv_debug_resume)(BlockDriverState *bs, const char *tag);
    bool (*bdrv_debug_is_suspended)(BlockDriverState *bs, const char *tag);

    void (*bdrv_refresh_limits)(BlockDriverState *bs, Error **errp);

    /*
     * Returns 1 if newly created images are guaranteed to contain only
     * zeros, 0 otherwise.
     */
    int (*bdrv_has_zero_init)(BlockDriverState *bs);

    /* Remove fd handlers, timers, and other event loop callbacks so the event
     * loop is no longer in use.  Called with no in-flight requests and in
     * depth-first traversal order with parents before child nodes.
     */
    void (*bdrv_detach_aio_context)(BlockDriverState *bs);

    /* Add fd handlers, timers, and other event loop callbacks so I/O requests
     * can be processed again.  Called with no in-flight requests and in
     * depth-first traversal order with child nodes before parent nodes.
     */
    void (*bdrv_attach_aio_context)(BlockDriverState *bs,
                                    AioContext *new_context);

    /* io queue for linux-aio */
    void (*bdrv_io_plug)(BlockDriverState *bs);
    void (*bdrv_io_unplug)(BlockDriverState *bs);

    /**
     * Try to get @bs's logical and physical block size.
     * On success, store them in @bsz and return zero.
     * On failure, return negative errno.
     */
    int (*bdrv_probe_blocksizes)(BlockDriverState *bs, BlockSizes *bsz);
    /**
     * Try to get @bs's geometry (cyls, heads, sectors)
     * On success, store them in @geo and return 0.
     * On failure return -errno.
     * Only drivers that want to override guest geometry implement this
     * callback; see hd_geometry_guess().
     */
    int (*bdrv_probe_geometry)(BlockDriverState *bs, HDGeometry *geo);

    /**
     * Drain and stop any internal sources of requests in the driver, and
     * remain so until next I/O callback (e.g. bdrv_co_writev) is called.
     */
    void coroutine_fn (*bdrv_co_drain)(BlockDriverState *bs);

    void (*bdrv_add_child)(BlockDriverState *parent, BlockDriverState *child,
                           Error **errp);
    void (*bdrv_del_child)(BlockDriverState *parent, BdrvChild *child,
                           Error **errp);

    /**
     * Informs the block driver that a permission change is intended. The
     * driver checks whether the change is permissible and may take other
     * preparations for the change (e.g. get file system locks). This operation
     * is always followed either by a call to either .bdrv_set_perm or
     * .bdrv_abort_perm_update.
     *
     * Checks whether the requested set of cumulative permissions in @perm
     * can be granted for accessing @bs and whether no other users are using
     * permissions other than those given in @shared (both arguments take
     * BLK_PERM_* bitmasks).
     *
     * If both conditions are met, 0 is returned. Otherwise, -errno is returned
     * and errp is set to an error describing the conflict.
     */
    int (*bdrv_check_perm)(BlockDriverState *bs, uint64_t perm,
                           uint64_t shared, Error **errp);

    /**
     * Called to inform the driver that the set of cumulative set of used
     * permissions for @bs has changed to @perm, and the set of sharable
     * permission to @shared. The driver can use this to propagate changes to
     * its children (i.e. request permissions only if a parent actually needs
     * them).
     *
     * This function is only invoked after bdrv_check_perm(), so block drivers
     * may rely on preparations made in their .bdrv_check_perm implementation.
     */
    void (*bdrv_set_perm)(BlockDriverState *bs, uint64_t perm, uint64_t shared);

    /*
     * Called to inform the driver that after a previous bdrv_check_perm()
     * call, the permission update is not performed and any preparations made
     * for it (e.g. taken file locks) need to be undone.
     *
     * This function can be called even for nodes that never saw a
     * bdrv_check_perm() call. It is a no-op then.
     */
    void (*bdrv_abort_perm_update)(BlockDriverState *bs);

    /**
     * Returns in @nperm and @nshared the permissions that the driver for @bs
     * needs on its child @c, based on the cumulative permissions requested by
     * the parents in @parent_perm and @parent_shared.
     *
     * If @c is NULL, return the permissions for attaching a new child for the
     * given @role.
     */
     void (*bdrv_child_perm)(BlockDriverState *bs, BdrvChild *c,
                             const BdrvChildRole *role,
                             uint64_t parent_perm, uint64_t parent_shared,
                             uint64_t *nperm, uint64_t *nshared);

    /**
     * Bitmaps should be marked as 'IN_USE' in the image on reopening image
     * as rw. This handler should realize it. It also should unset readonly
     * field of BlockDirtyBitmap's in case of success.
     */
    int (*bdrv_reopen_bitmaps_rw)(BlockDriverState *bs, Error **errp);
    bool (*bdrv_can_store_new_dirty_bitmap)(BlockDriverState *bs,
                                            const char *name,
                                            uint32_t granularity,
                                            Error **errp);
    void (*bdrv_remove_persistent_dirty_bitmap)(BlockDriverState *bs,
                                                const char *name,
                                                Error **errp);

    QLIST_ENTRY(BlockDriver) list;
};

typedef struct BlockLimits {
    /* Alignment requirement, in bytes, for offset/length of I/O
     * requests. Must be a power of 2 less than INT_MAX; defaults to
     * 1 for drivers with modern byte interfaces, and to 512
     * otherwise. */
    uint32_t request_alignment;

    /* Maximum number of bytes that can be discarded at once (since it
     * is signed, it must be < 2G, if set). Must be multiple of
     * pdiscard_alignment, but need not be power of 2. May be 0 if no
     * inherent 32-bit limit */
    int32_t max_pdiscard;

    /* Optimal alignment for discard requests in bytes. A power of 2
     * is best but not mandatory.  Must be a multiple of
     * bl.request_alignment, and must be less than max_pdiscard if
     * that is set. May be 0 if bl.request_alignment is good enough */
    uint32_t pdiscard_alignment;

    /* Maximum number of bytes that can zeroized at once (since it is
     * signed, it must be < 2G, if set). Must be multiple of
     * pwrite_zeroes_alignment. May be 0 if no inherent 32-bit limit */
    int32_t max_pwrite_zeroes;

    /* Optimal alignment for write zeroes requests in bytes. A power
     * of 2 is best but not mandatory.  Must be a multiple of
     * bl.request_alignment, and must be less than max_pwrite_zeroes
     * if that is set. May be 0 if bl.request_alignment is good
     * enough */
    uint32_t pwrite_zeroes_alignment;

    /* Optimal transfer length in bytes.  A power of 2 is best but not
     * mandatory.  Must be a multiple of bl.request_alignment, or 0 if
     * no preferred size */
    uint32_t opt_transfer;

    /* Maximal transfer length in bytes.  Need not be power of 2, but
     * must be multiple of opt_transfer and bl.request_alignment, or 0
     * for no 32-bit limit.  For now, anything larger than INT_MAX is
     * clamped down. */
    uint32_t max_transfer;

    /* memory alignment, in bytes so that no bounce buffer is needed */
    size_t min_mem_alignment;

    /* memory alignment, in bytes, for bounce buffer */
    size_t opt_mem_alignment;

    /* maximum number of iovec elements */
    int max_iov;
} BlockLimits;

typedef struct BdrvOpBlocker BdrvOpBlocker;

typedef struct BdrvAioNotifier {
    void (*attached_aio_context)(AioContext *new_context, void *opaque);
    void (*detach_aio_context)(void *opaque);

    void *opaque;
    bool deleted;

    QLIST_ENTRY(BdrvAioNotifier) list;
} BdrvAioNotifier;

struct BdrvChildRole {
    /* If true, bdrv_replace_node() doesn't change the node this BdrvChild
     * points to. */
    bool stay_at_node;

    void (*inherit_options)(int *child_flags, QDict *child_options,
                            int parent_flags, QDict *parent_options);

    void (*change_media)(BdrvChild *child, bool load);
    void (*resize)(BdrvChild *child);

    /* Returns a name that is supposedly more useful for human users than the
     * node name for identifying the node in question (in particular, a BB
     * name), or NULL if the parent can't provide a better name. */
    const char *(*get_name)(BdrvChild *child);

    /* Returns a malloced string that describes the parent of the child for a
     * human reader. This could be a node-name, BlockBackend name, qdev ID or
     * QOM path of the device owning the BlockBackend, job type and ID etc. The
     * caller is responsible for freeing the memory. */
    char *(*get_parent_desc)(BdrvChild *child);

    /*
     * If this pair of functions is implemented, the parent doesn't issue new
     * requests after returning from .drained_begin() until .drained_end() is
     * called.
     *
     * Note that this can be nested. If drained_begin() was called twice, new
     * I/O is allowed only after drained_end() was called twice, too.
     */
    void (*drained_begin)(BdrvChild *child);
    void (*drained_end)(BdrvChild *child);

    /* Notifies the parent that the child has been activated/inactivated (e.g.
     * when migration is completing) and it can start/stop requesting
     * permissions and doing I/O on it. */
    void (*activate)(BdrvChild *child, Error **errp);
    int (*inactivate)(BdrvChild *child);

    void (*attach)(BdrvChild *child);
    void (*detach)(BdrvChild *child);
};

extern const BdrvChildRole child_file;
extern const BdrvChildRole child_format;
extern const BdrvChildRole child_backing;

struct BdrvChild {
    BlockDriverState *bs;
    char *name;
    const BdrvChildRole *role;
    void *opaque;

    /**
     * Granted permissions for operating on this BdrvChild (BLK_PERM_* bitmask)
     */
    uint64_t perm;

    /**
     * Permissions that can still be granted to other users of @bs while this
     * BdrvChild is still attached to it. (BLK_PERM_* bitmask)
     */
    uint64_t shared_perm;

    QLIST_ENTRY(BdrvChild) next;
    QLIST_ENTRY(BdrvChild) next_parent;
};

/*
 * Note: the function bdrv_append() copies and swaps contents of
 * BlockDriverStates, so if you add new fields to this struct, please
 * inspect bdrv_append() to determine if the new fields need to be
 * copied as well.
 */
struct BlockDriverState {
    /* Protected by big QEMU lock or read-only after opening.  No special
     * locking needed during I/O...
     */
    int open_flags; /* flags used to open the file, re-used for re-open */
    bool read_only; /* if true, the media is read only */
    bool encrypted; /* if true, the media is encrypted */
    bool sg;        /* if true, the device is a /dev/sg* */
    bool probed;    /* if true, format was probed rather than specified */
    bool force_share; /* if true, always allow all shared permissions */
    bool implicit;  /* if true, this filter node was automatically inserted */

    BlockDriver *drv; /* NULL means no media */
    void *opaque;

    AioContext *aio_context; /* event loop used for fd handlers, timers, etc */
    /* long-running tasks intended to always use the same AioContext as this
     * BDS may register themselves in this list to be notified of changes
     * regarding this BDS's context */
    QLIST_HEAD(, BdrvAioNotifier) aio_notifiers;
    bool walking_aio_notifiers; /* to make removal during iteration safe */

    char filename[PATH_MAX];
    char backing_file[PATH_MAX]; /* if non zero, the image is a diff of
                                    this file image */
    char backing_format[16]; /* if non-zero and backing_file exists */

    QDict *full_open_options;
    char exact_filename[PATH_MAX];

    BdrvChild *backing;
    BdrvChild *file;

    /* I/O Limits */
    BlockLimits bl;

    /* Flags honored during pwrite (so far: BDRV_REQ_FUA) */
    unsigned int supported_write_flags;
    /* Flags honored during pwrite_zeroes (so far: BDRV_REQ_FUA,
     * BDRV_REQ_MAY_UNMAP) */
    unsigned int supported_zero_flags;

    /* the following member gives a name to every node on the bs graph. */
    char node_name[32];
    /* element of the list of named nodes building the graph */
    QTAILQ_ENTRY(BlockDriverState) node_list;
    /* element of the list of all BlockDriverStates (all_bdrv_states) */
    QTAILQ_ENTRY(BlockDriverState) bs_list;
    /* element of the list of monitor-owned BDS */
    QTAILQ_ENTRY(BlockDriverState) monitor_list;
    int refcnt;

    /* operation blockers */
    QLIST_HEAD(, BdrvOpBlocker) op_blockers[BLOCK_OP_TYPE_MAX];

    /* long-running background operation */
    BlockJob *job;

    /* The node that this node inherited default options from (and a reopen on
     * which can affect this node by changing these defaults). This is always a
     * parent node of this node. */
    BlockDriverState *inherits_from;
    QLIST_HEAD(, BdrvChild) children;
    QLIST_HEAD(, BdrvChild) parents;

    QDict *options;
    QDict *explicit_options;
    BlockdevDetectZeroesOptions detect_zeroes;

    /* The error object in use for blocking operations on backing_hd */
    Error *backing_blocker;

    /* Protected by AioContext lock */

    /* If we are reading a disk image, give its size in sectors.
     * Generally read-only; it is written to by load_snapshot and
     * save_snaphost, but the block layer is quiescent during those.
     */
    int64_t total_sectors;

    /* Callback before write request is processed */
    NotifierWithReturnList before_write_notifiers;

    /* threshold limit for writes, in bytes. "High water mark". */
    uint64_t write_threshold_offset;
    NotifierWithReturn write_threshold_notifier;

    /* Writing to the list requires the BQL _and_ the dirty_bitmap_mutex.
     * Reading from the list can be done with either the BQL or the
     * dirty_bitmap_mutex.  Modifying a bitmap only requires
     * dirty_bitmap_mutex.  */
    QemuMutex dirty_bitmap_mutex;
    QLIST_HEAD(, BdrvDirtyBitmap) dirty_bitmaps;

    /* Offset after the highest byte written to */
    Stat64 wr_highest_offset;

    /* If true, copy read backing sectors into image.  Can be >1 if more
     * than one client has requested copy-on-read.  Accessed with atomic
     * ops.
     */
    int copy_on_read;

    /* number of in-flight requests; overall and serialising.
     * Accessed with atomic ops.
     */
    unsigned int in_flight;
    unsigned int serialising_in_flight;

    /* Internal to BDRV_POLL_WHILE and bdrv_wakeup.  Accessed with atomic
     * ops.
     */
    bool wakeup;

    /* counter for nested bdrv_io_plug.
     * Accessed with atomic ops.
    */
    unsigned io_plugged;

    /* do we need to tell the quest if we have a volatile write cache? */
    int enable_write_cache;

    /* Accessed with atomic ops.  */
    int quiesce_counter;
    unsigned int write_gen;               /* Current data generation */

    /* Protected by reqs_lock.  */
    CoMutex reqs_lock;
    QLIST_HEAD(, BdrvTrackedRequest) tracked_requests;
    CoQueue flush_queue;                  /* Serializing flush queue */
    bool active_flush_req;                /* Flush request in flight? */

    /* Only read/written by whoever has set active_flush_req to true.  */
    unsigned int flushed_gen;             /* Flushed write generation */
};

struct BlockBackendRootState {
    int open_flags;
    bool read_only;
    BlockdevDetectZeroesOptions detect_zeroes;
};

typedef enum BlockMirrorBackingMode {
    /* Reuse the existing backing chain from the source for the target.
     * - sync=full: Set backing BDS to NULL.
     * - sync=top:  Use source's backing BDS.
     * - sync=none: Use source as the backing BDS. */
    MIRROR_SOURCE_BACKING_CHAIN,

    /* Open the target's backing chain completely anew */
    MIRROR_OPEN_BACKING_CHAIN,

    /* Do not change the target's backing BDS after job completion */
    MIRROR_LEAVE_BACKING_CHAIN,
} BlockMirrorBackingMode;

static inline BlockDriverState *backing_bs(BlockDriverState *bs)
{
    return bs->backing ? bs->backing->bs : NULL;
}


/* Essential block drivers which must always be statically linked into qemu, and
 * which therefore can be accessed without using bdrv_find_format() */
extern BlockDriver bdrv_file;
extern BlockDriver bdrv_raw;
extern BlockDriver bdrv_qcow2;

int coroutine_fn bdrv_co_preadv(BdrvChild *child,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
int coroutine_fn bdrv_co_pwritev(BdrvChild *child,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);

int get_tmp_filename(char *filename, int size);
BlockDriver *bdrv_probe_all(const uint8_t *buf, int buf_size,
                            const char *filename);

void bdrv_parse_filename_strip_prefix(const char *filename, const char *prefix,
                                      QDict *options);


/**
 * bdrv_add_before_write_notifier:
 *
 * Register a callback that is invoked before write requests are processed but
 * after any throttling or waiting for overlapping requests.
 */
void bdrv_add_before_write_notifier(BlockDriverState *bs,
                                    NotifierWithReturn *notifier);

/**
 * bdrv_detach_aio_context:
 *
 * May be called from .bdrv_detach_aio_context() to detach children from the
 * current #AioContext.  This is only needed by block drivers that manage their
 * own children.  Both ->file and ->backing are automatically handled and
 * block drivers should not call this function on them explicitly.
 */
void bdrv_detach_aio_context(BlockDriverState *bs);

/**
 * bdrv_attach_aio_context:
 *
 * May be called from .bdrv_attach_aio_context() to attach children to the new
 * #AioContext.  This is only needed by block drivers that manage their own
 * children.  Both ->file and ->backing are automatically handled and block
 * drivers should not call this function on them explicitly.
 */
void bdrv_attach_aio_context(BlockDriverState *bs,
                             AioContext *new_context);

/**
 * bdrv_add_aio_context_notifier:
 *
 * If a long-running job intends to be always run in the same AioContext as a
 * certain BDS, it may use this function to be notified of changes regarding the
 * association of the BDS to an AioContext.
 *
 * attached_aio_context() is called after the target BDS has been attached to a
 * new AioContext; detach_aio_context() is called before the target BDS is being
 * detached from its old AioContext.
 */
void bdrv_add_aio_context_notifier(BlockDriverState *bs,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque);

/**
 * bdrv_remove_aio_context_notifier:
 *
 * Unsubscribe of change notifications regarding the BDS's AioContext. The
 * parameters given here have to be the same as those given to
 * bdrv_add_aio_context_notifier().
 */
void bdrv_remove_aio_context_notifier(BlockDriverState *bs,
                                      void (*aio_context_attached)(AioContext *,
                                                                   void *),
                                      void (*aio_context_detached)(void *),
                                      void *opaque);

/**
 * bdrv_wakeup:
 * @bs: The BlockDriverState for which an I/O operation has been completed.
 *
 * Wake up the main thread if it is waiting on BDRV_POLL_WHILE.  During
 * synchronous I/O on a BlockDriverState that is attached to another
 * I/O thread, the main thread lets the I/O thread's event loop run,
 * waiting for the I/O operation to complete.  A bdrv_wakeup will wake
 * up the main thread if necessary.
 *
 * Manual calls to bdrv_wakeup are rarely necessary, because
 * bdrv_dec_in_flight already calls it.
 */
void bdrv_wakeup(BlockDriverState *bs);

#ifdef _WIN32
int is_windows_drive(const char *filename);
#endif

/**
 * stream_start:
 * @job_id: The id of the newly-created job, or %NULL to use the
 * device name of @bs.
 * @bs: Block device to operate on.
 * @base: Block device that will become the new base, or %NULL to
 * flatten the whole backing file chain onto @bs.
 * @backing_file_str: The file name that will be written to @bs as the
 * the new backing file if the job completes. Ignored if @base is %NULL.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @errp: Error object.
 *
 * Start a streaming operation on @bs.  Clusters that are unallocated
 * in @bs, but allocated in any image between @base and @bs (both
 * exclusive) will be written to @bs.  At the end of a successful
 * streaming job, the backing file of @bs will be changed to
 * @backing_file_str in the written image and to @base in the live
 * BlockDriverState.
 */
void stream_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, const char *backing_file_str,
                  int64_t speed, BlockdevOnError on_error, Error **errp);

/**
 * commit_start:
 * @job_id: The id of the newly-created job, or %NULL to use the
 * device name of @bs.
 * @bs: Active block device.
 * @top: Top block device to be committed.
 * @base: Block device that will be written into, and become the new top.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @backing_file_str: String to use as the backing file in @top's overlay
 * @filter_node_name: The node name that should be assigned to the filter
 * driver that the commit job inserts into the graph above @top. NULL means
 * that a node name should be autogenerated.
 * @errp: Error object.
 *
 */
void commit_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *base, BlockDriverState *top, int64_t speed,
                  BlockdevOnError on_error, const char *backing_file_str,
                  const char *filter_node_name, Error **errp);
/**
 * commit_active_start:
 * @job_id: The id of the newly-created job, or %NULL to use the
 * device name of @bs.
 * @bs: Active block device to be committed.
 * @base: Block device that will be written into, and become the new top.
 * @creation_flags: Flags that control the behavior of the Job lifetime.
 *                  See @BlockJobCreateFlags
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @on_error: The action to take upon error.
 * @filter_node_name: The node name that should be assigned to the filter
 * driver that the commit job inserts into the graph above @bs. NULL means that
 * a node name should be autogenerated.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @auto_complete: Auto complete the job.
 * @errp: Error object.
 *
 */
void commit_active_start(const char *job_id, BlockDriverState *bs,
                         BlockDriverState *base, int creation_flags,
                         int64_t speed, BlockdevOnError on_error,
                         const char *filter_node_name,
                         BlockCompletionFunc *cb, void *opaque,
                         bool auto_complete, Error **errp);
/*
 * mirror_start:
 * @job_id: The id of the newly-created job, or %NULL to use the
 * device name of @bs.
 * @bs: Block device to operate on.
 * @target: Block device to write to.
 * @replaces: Block graph node name to replace once the mirror is done. Can
 *            only be used when full mirroring is selected.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @granularity: The chosen granularity for the dirty bitmap.
 * @buf_size: The amount of data that can be in flight at one time.
 * @mode: Whether to collapse all images in the chain to the target.
 * @backing_mode: How to establish the target's backing chain after completion.
 * @on_source_error: The action to take upon error reading from the source.
 * @on_target_error: The action to take upon error writing to the target.
 * @unmap: Whether to unmap target where source sectors only contain zeroes.
 * @filter_node_name: The node name that should be assigned to the filter
 * driver that the mirror job inserts into the graph above @bs. NULL means that
 * a node name should be autogenerated.
 * @errp: Error object.
 *
 * Start a mirroring operation on @bs.  Clusters that are allocated
 * in @bs will be written to @target until the job is cancelled or
 * manually completed.  At the end of a successful mirroring job,
 * @bs will be switched to read from @target.
 */
void mirror_start(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, const char *replaces,
                  int64_t speed, uint32_t granularity, int64_t buf_size,
                  MirrorSyncMode mode, BlockMirrorBackingMode backing_mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  bool unmap, const char *filter_node_name, Error **errp);

/*
 * backup_job_create:
 * @job_id: The id of the newly-created job, or %NULL to use the
 * device name of @bs.
 * @bs: Block device to operate on.
 * @target: Block device to write to.
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @sync_mode: What parts of the disk image should be copied to the destination.
 * @sync_bitmap: The dirty bitmap if sync_mode is MIRROR_SYNC_MODE_INCREMENTAL.
 * @on_source_error: The action to take upon error reading from the source.
 * @on_target_error: The action to take upon error writing to the target.
 * @creation_flags: Flags that control the behavior of the Job lifetime.
 *                  See @BlockJobCreateFlags
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @txn: Transaction that this job is part of (may be NULL).
 *
 * Create a backup operation on @bs.  Clusters in @bs are written to @target
 * until the job is cancelled or manually completed.
 */
BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                            BlockDriverState *target, int64_t speed,
                            MirrorSyncMode sync_mode,
                            BdrvDirtyBitmap *sync_bitmap,
                            bool compress,
                            BlockdevOnError on_source_error,
                            BlockdevOnError on_target_error,
                            int creation_flags,
                            BlockCompletionFunc *cb, void *opaque,
                            BlockJobTxn *txn, Error **errp);

void hmp_drive_add_node(Monitor *mon, const char *optstr);

BdrvChild *bdrv_root_attach_child(BlockDriverState *child_bs,
                                  const char *child_name,
                                  const BdrvChildRole *child_role,
                                  uint64_t perm, uint64_t shared_perm,
                                  void *opaque, Error **errp);
void bdrv_root_unref_child(BdrvChild *child);

int bdrv_child_try_set_perm(BdrvChild *c, uint64_t perm, uint64_t shared,
                            Error **errp);

/* Default implementation for BlockDriver.bdrv_child_perm() that can be used by
 * block filters: Forward CONSISTENT_READ, WRITE, WRITE_UNCHANGED and RESIZE to
 * all children */
void bdrv_filter_default_perms(BlockDriverState *bs, BdrvChild *c,
                               const BdrvChildRole *role,
                               uint64_t perm, uint64_t shared,
                               uint64_t *nperm, uint64_t *nshared);

/* Default implementation for BlockDriver.bdrv_child_perm() that can be used by
 * (non-raw) image formats: Like above for bs->backing, but for bs->file it
 * requires WRITE | RESIZE for read-write images, always requires
 * CONSISTENT_READ and doesn't share WRITE. */
void bdrv_format_default_perms(BlockDriverState *bs, BdrvChild *c,
                               const BdrvChildRole *role,
                               uint64_t perm, uint64_t shared,
                               uint64_t *nperm, uint64_t *nshared);

const char *bdrv_get_parent_name(const BlockDriverState *bs);
void blk_dev_change_media_cb(BlockBackend *blk, bool load, Error **errp);
bool blk_dev_has_removable_media(BlockBackend *blk);
bool blk_dev_has_tray(BlockBackend *blk);
void blk_dev_eject_request(BlockBackend *blk, bool force);
bool blk_dev_is_tray_open(BlockBackend *blk);
bool blk_dev_is_medium_locked(BlockBackend *blk);

void bdrv_set_dirty(BlockDriverState *bs, int64_t cur_sector, int64_t nr_sect);
bool bdrv_requests_pending(BlockDriverState *bs);

void bdrv_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap **out);
void bdrv_undo_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap *in);

void bdrv_inc_in_flight(BlockDriverState *bs);
void bdrv_dec_in_flight(BlockDriverState *bs);

void blockdev_close_all_bdrv_states(void);

#endif /* BLOCK_INT_H */
