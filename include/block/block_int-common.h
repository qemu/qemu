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
#ifndef BLOCK_INT_COMMON_H
#define BLOCK_INT_COMMON_H

#include "block/aio.h"
#include "block/block-common.h"
#include "block/block-global-state.h"
#include "block/snapshot.h"
#include "qemu/iov.h"
#include "qemu/rcu.h"
#include "qemu/stats64.h"

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
#define BLOCK_OPT_EXTENT_SIZE_HINT  "extent_size_hint"
#define BLOCK_OPT_OBJECT_SIZE       "object_size"
#define BLOCK_OPT_REFCOUNT_BITS     "refcount_bits"
#define BLOCK_OPT_DATA_FILE         "data_file"
#define BLOCK_OPT_DATA_FILE_RAW     "data_file_raw"
#define BLOCK_OPT_COMPRESSION_TYPE  "compression_type"
#define BLOCK_OPT_EXTL2             "extended_l2"

#define BLOCK_PROBE_BUF_SIZE        512

enum BdrvTrackedRequestType {
    BDRV_TRACKED_READ,
    BDRV_TRACKED_WRITE,
    BDRV_TRACKED_DISCARD,
    BDRV_TRACKED_TRUNCATE,
};

/*
 * That is not quite good that BdrvTrackedRequest structure is public,
 * as block/io.c is very careful about incoming offset/bytes being
 * correct. Be sure to assert bdrv_check_request() succeeded after any
 * modification of BdrvTrackedRequest object out of block/io.c
 */
typedef struct BdrvTrackedRequest {
    BlockDriverState *bs;
    int64_t offset;
    int64_t bytes;
    enum BdrvTrackedRequestType type;

    bool serialising;
    int64_t overlap_offset;
    int64_t overlap_bytes;

    QLIST_ENTRY(BdrvTrackedRequest) list;
    Coroutine *co; /* owner, used for deadlock detection */
    CoQueue wait_queue; /* coroutines blocked on this request */

    struct BdrvTrackedRequest *waiting_for;
} BdrvTrackedRequest;


struct BlockDriver {
    /*
     * These fields are initialized when this object is created,
     * and are never changed afterwards.
     */

    const char *format_name;
    int instance_size;

    /*
     * Set to true if the BlockDriver is a block filter. Block filters pass
     * certain callbacks that refer to data (see block.c) to their bs->file
     * or bs->backing (whichever one exists) if the driver doesn't implement
     * them. Drivers that do not wish to forward must implement them and return
     * -ENOTSUP.
     * Note that filters are not allowed to modify data.
     *
     * Filters generally cannot have more than a single filtered child,
     * because the data they present must at all times be the same as
     * that on their filtered child.  That would be impossible to
     * achieve for multiple filtered children.
     * (And this filtered child must then be bs->file or bs->backing.)
     */
    bool is_filter;
    /*
     * Only make sense for filter drivers, for others must be false.
     * If true, filtered child is bs->backing. Otherwise it's bs->file.
     * Two internal filters use bs->backing as filtered child and has this
     * field set to true: mirror_top and commit_top. There also two such test
     * filters in tests/unit/test-bdrv-graph-mod.c.
     *
     * Never create any more such filters!
     *
     * TODO: imagine how to deprecate this behavior and make all filters work
     * similarly using bs->file as filtered child.
     */
    bool filtered_child_is_backing;

    /*
     * Set to true if the BlockDriver is a format driver.  Format nodes
     * generally do not expect their children to be other format nodes
     * (except for backing files), and so format probing is disabled
     * on those children.
     */
    bool is_format;

    /*
     * Drivers not implementing bdrv_parse_filename nor bdrv_open should have
     * this field set to true, except ones that are defined only by their
     * child's bs.
     * An example of the last type will be the quorum block driver.
     */
    bool bdrv_needs_filename;

    /*
     * Set if a driver can support backing files. This also implies the
     * following semantics:
     *
     *  - Return status 0 of .bdrv_co_block_status means that corresponding
     *    blocks are not allocated in this layer of backing-chain
     *  - For such (unallocated) blocks, read will:
     *    - fill buffer with zeros if there is no backing file
     *    - read from the backing file otherwise, where the block layer
     *      takes care of reading zeros beyond EOF if backing file is short
     */
    bool supports_backing;

    bool has_variable_length;

    /*
     * Drivers setting this field must be able to work with just a plain
     * filename with '<protocol_name>:' as a prefix, and no other options.
     * Options may be extracted from the filename by implementing
     * bdrv_parse_filename.
     */
    const char *protocol_name;

    /* List of options for creating images, terminated by name == NULL */
    QemuOptsList *create_opts;

    /* List of options for image amend */
    QemuOptsList *amend_opts;

    /*
     * If this driver supports reopening images this contains a
     * NULL-terminated list of the runtime options that can be
     * modified. If an option in this list is unspecified during
     * reopen then it _must_ be reset to its default value or return
     * an error.
     */
    const char *const *mutable_opts;

    /*
     * Pointer to a NULL-terminated array of names of strong options
     * that can be specified for bdrv_open(). A strong option is one
     * that changes the data of a BDS.
     * If this pointer is NULL, the array is considered empty.
     * "filename" and "driver" are always considered strong.
     */
    const char *const *strong_runtime_opts;


    /*
     * Global state (GS) API. These functions run under the BQL.
     *
     * See include/block/block-global-state.h for more information about
     * the GS API.
     */

    /*
     * This function is invoked under BQL before .bdrv_co_amend()
     * (which in contrast does not necessarily run under the BQL)
     * to allow driver-specific initialization code that requires
     * the BQL, like setting up specific permission flags.
     */
    int (*bdrv_amend_pre_run)(BlockDriverState *bs, Error **errp);
    /*
     * This function is invoked under BQL after .bdrv_co_amend()
     * to allow cleaning up what was done in .bdrv_amend_pre_run().
     */
    void (*bdrv_amend_clean)(BlockDriverState *bs);

    /*
     * Return true if @to_replace can be replaced by a BDS with the
     * same data as @bs without it affecting @bs's behavior (that is,
     * without it being visible to @bs's parents).
     */
    bool (*bdrv_recurse_can_replace)(BlockDriverState *bs,
                                     BlockDriverState *to_replace);

    int (*bdrv_probe_device)(const char *filename);

    /*
     * Any driver implementing this callback is expected to be able to handle
     * NULL file names in its .bdrv_open() implementation.
     */
    void (*bdrv_parse_filename)(const char *filename, QDict *options,
                                Error **errp);

    /* For handling image reopen for split or non-split files. */
    int (*bdrv_reopen_prepare)(BDRVReopenState *reopen_state,
                               BlockReopenQueue *queue, Error **errp);
    void (*bdrv_reopen_commit)(BDRVReopenState *reopen_state);
    void (*bdrv_reopen_commit_post)(BDRVReopenState *reopen_state);
    void (*bdrv_reopen_abort)(BDRVReopenState *reopen_state);
    void (*bdrv_join_options)(QDict *options, QDict *old_options);

    int (*bdrv_open)(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp);

    /* Protocol drivers should implement this instead of bdrv_open */
    int (*bdrv_file_open)(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp);
    void (*bdrv_close)(BlockDriverState *bs);

    int coroutine_fn (*bdrv_co_create)(BlockdevCreateOptions *opts,
                                       Error **errp);
    int coroutine_fn (*bdrv_co_create_opts)(BlockDriver *drv,
                                            const char *filename,
                                            QemuOpts *opts,
                                            Error **errp);

    int (*bdrv_amend_options)(BlockDriverState *bs,
                              QemuOpts *opts,
                              BlockDriverAmendStatusCB *status_cb,
                              void *cb_opaque,
                              bool force,
                              Error **errp);

    int (*bdrv_make_empty)(BlockDriverState *bs);

    /*
     * Refreshes the bs->exact_filename field. If that is impossible,
     * bs->exact_filename has to be left empty.
     */
    void (*bdrv_refresh_filename)(BlockDriverState *bs);

    /*
     * Gathers the open options for all children into @target.
     * A simple format driver (without backing file support) might
     * implement this function like this:
     *
     *     QINCREF(bs->file->bs->full_open_options);
     *     qdict_put(target, "file", bs->file->bs->full_open_options);
     *
     * If not specified, the generic implementation will simply put
     * all children's options under their respective name.
     *
     * @backing_overridden is true when bs->backing seems not to be
     * the child that would result from opening bs->backing_file.
     * Therefore, if it is true, the backing child's options should be
     * gathered; otherwise, there is no need since the backing child
     * is the one implied by the image header.
     *
     * Note that ideally this function would not be needed.  Every
     * block driver which implements it is probably doing something
     * shady regarding its runtime option structure.
     */
    void (*bdrv_gather_child_options)(BlockDriverState *bs, QDict *target,
                                      bool backing_overridden);

    /*
     * Returns an allocated string which is the directory name of this BDS: It
     * will be used to make relative filenames absolute by prepending this
     * function's return value to them.
     */
    char *(*bdrv_dirname)(BlockDriverState *bs, Error **errp);

    /*
     * This informs the driver that we are no longer interested in the result
     * of in-flight requests, so don't waste the time if possible.
     *
     * One example usage is to avoid waiting for an nbd target node reconnect
     * timeout during job-cancel with force=true.
     */
    void (*bdrv_cancel_in_flight)(BlockDriverState *bs);

    int (*bdrv_inactivate)(BlockDriverState *bs);

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

    int (*bdrv_change_backing_file)(BlockDriverState *bs,
        const char *backing_file, const char *backing_fmt);

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

    /*
     * Remove fd handlers, timers, and other event loop callbacks so the event
     * loop is no longer in use.  Called with no in-flight requests and in
     * depth-first traversal order with parents before child nodes.
     */
    void (*bdrv_detach_aio_context)(BlockDriverState *bs);

    /*
     * Add fd handlers, timers, and other event loop callbacks so I/O requests
     * can be processed again.  Called with no in-flight requests and in
     * depth-first traversal order with child nodes before parent nodes.
     */
    void (*bdrv_attach_aio_context)(BlockDriverState *bs,
                                    AioContext *new_context);

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
     * given @child_class and @role.
     *
     * If @reopen_queue is non-NULL, don't return the currently needed
     * permissions, but those that will be needed after applying the
     * @reopen_queue.
     */
     void (*bdrv_child_perm)(BlockDriverState *bs, BdrvChild *c,
                             BdrvChildRole role,
                             BlockReopenQueue *reopen_queue,
                             uint64_t parent_perm, uint64_t parent_shared,
                             uint64_t *nperm, uint64_t *nshared);

    /**
     * Register/unregister a buffer for I/O. For example, when the driver is
     * interested to know the memory areas that will later be used in iovs, so
     * that it can do IOMMU mapping with VFIO etc., in order to get better
     * performance. In the case of VFIO drivers, this callback is used to do
     * DMA mapping for hot buffers.
     *
     * Returns: true on success, false on failure
     */
    bool (*bdrv_register_buf)(BlockDriverState *bs, void *host, size_t size,
                              Error **errp);
    void (*bdrv_unregister_buf)(BlockDriverState *bs, void *host, size_t size);

    /*
     * This field is modified only under the BQL, and is part of
     * the global state.
     */
    QLIST_ENTRY(BlockDriver) list;

    /*
     * I/O API functions. These functions are thread-safe.
     *
     * See include/block/block-io.h for more information about
     * the I/O API.
     */

    int (*bdrv_probe)(const uint8_t *buf, int buf_size, const char *filename);

    int coroutine_fn (*bdrv_co_amend)(BlockDriverState *bs,
                                      BlockdevAmendOptions *opts,
                                      bool force,
                                      Error **errp);

    /* aio */
    BlockAIOCB *(*bdrv_aio_preadv)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        BdrvRequestFlags flags, BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_pwritev)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        BdrvRequestFlags flags, BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_flush)(BlockDriverState *bs,
        BlockCompletionFunc *cb, void *opaque);
    BlockAIOCB *(*bdrv_aio_pdiscard)(BlockDriverState *bs,
        int64_t offset, int bytes,
        BlockCompletionFunc *cb, void *opaque);

    int coroutine_fn (*bdrv_co_readv)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);

    /**
     * @offset: position in bytes to read at
     * @bytes: number of bytes to read
     * @qiov: the buffers to fill with read data
     * @flags: currently unused, always 0
     *
     * @offset and @bytes will be a multiple of 'request_alignment',
     * but the length of individual @qiov elements does not have to
     * be a multiple.
     *
     * @bytes will always equal the total size of @qiov, and will be
     * no larger than 'max_transfer'.
     *
     * The buffer in @qiov may point directly to guest memory.
     */
    int coroutine_fn (*bdrv_co_preadv)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        BdrvRequestFlags flags);

    int coroutine_fn (*bdrv_co_preadv_part)(BlockDriverState *bs,
        int64_t offset, int64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset,
        BdrvRequestFlags flags);

    int coroutine_fn (*bdrv_co_writev)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
        int flags);
    /**
     * @offset: position in bytes to write at
     * @bytes: number of bytes to write
     * @qiov: the buffers containing data to write
     * @flags: zero or more bits allowed by 'supported_write_flags'
     *
     * @offset and @bytes will be a multiple of 'request_alignment',
     * but the length of individual @qiov elements does not have to
     * be a multiple.
     *
     * @bytes will always equal the total size of @qiov, and will be
     * no larger than 'max_transfer'.
     *
     * The buffer in @qiov may point directly to guest memory.
     */
    int coroutine_fn (*bdrv_co_pwritev)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        BdrvRequestFlags flags);
    int coroutine_fn (*bdrv_co_pwritev_part)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov, size_t qiov_offset,
        BdrvRequestFlags flags);

    /*
     * Efficiently zero a region of the disk image.  Typically an image format
     * would use a compact metadata representation to implement this.  This
     * function pointer may be NULL or return -ENOSUP and .bdrv_co_writev()
     * will be called instead.
     */
    int coroutine_fn (*bdrv_co_pwrite_zeroes)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, BdrvRequestFlags flags);
    int coroutine_fn (*bdrv_co_pdiscard)(BlockDriverState *bs,
        int64_t offset, int64_t bytes);

    /*
     * Map [offset, offset + nbytes) range onto a child of @bs to copy from,
     * and invoke bdrv_co_copy_range_from(child, ...), or invoke
     * bdrv_co_copy_range_to() if @bs is the leaf child to copy data from.
     *
     * See the comment of bdrv_co_copy_range for the parameter and return value
     * semantics.
     */
    int coroutine_fn (*bdrv_co_copy_range_from)(BlockDriverState *bs,
                                                BdrvChild *src,
                                                int64_t offset,
                                                BdrvChild *dst,
                                                int64_t dst_offset,
                                                int64_t bytes,
                                                BdrvRequestFlags read_flags,
                                                BdrvRequestFlags write_flags);

    /*
     * Map [offset, offset + nbytes) range onto a child of bs to copy data to,
     * and invoke bdrv_co_copy_range_to(child, src, ...), or perform the copy
     * operation if @bs is the leaf and @src has the same BlockDriver.  Return
     * -ENOTSUP if @bs is the leaf but @src has a different BlockDriver.
     *
     * See the comment of bdrv_co_copy_range for the parameter and return value
     * semantics.
     */
    int coroutine_fn (*bdrv_co_copy_range_to)(BlockDriverState *bs,
                                              BdrvChild *src,
                                              int64_t src_offset,
                                              BdrvChild *dst,
                                              int64_t dst_offset,
                                              int64_t bytes,
                                              BdrvRequestFlags read_flags,
                                              BdrvRequestFlags write_flags);

    /*
     * Building block for bdrv_block_status[_above] and
     * bdrv_is_allocated[_above].  The driver should answer only
     * according to the current layer, and should only need to set
     * BDRV_BLOCK_DATA, BDRV_BLOCK_ZERO, BDRV_BLOCK_OFFSET_VALID,
     * and/or BDRV_BLOCK_RAW; if the current layer defers to a backing
     * layer, the result should be 0 (and not BDRV_BLOCK_ZERO).  See
     * block.h for the overall meaning of the bits.  As a hint, the
     * flag want_zero is true if the caller cares more about precise
     * mappings (favor accurate _OFFSET_VALID/_ZERO) or false for
     * overall allocation (favor larger *pnum, perhaps by reporting
     * _DATA instead of _ZERO).  The block layer guarantees input
     * clamped to bdrv_getlength() and aligned to request_alignment,
     * as well as non-NULL pnum, map, and file; in turn, the driver
     * must return an error or set pnum to an aligned non-zero value.
     *
     * Note that @bytes is just a hint on how big of a region the
     * caller wants to inspect.  It is not a limit on *pnum.
     * Implementations are free to return larger values of *pnum if
     * doing so does not incur a performance penalty.
     *
     * block/io.c's bdrv_co_block_status() will utilize an unclamped
     * *pnum value for the block-status cache on protocol nodes, prior
     * to clamping *pnum for return to its caller.
     */
    int coroutine_fn (*bdrv_co_block_status)(BlockDriverState *bs,
        bool want_zero, int64_t offset, int64_t bytes, int64_t *pnum,
        int64_t *map, BlockDriverState **file);

    /*
     * Snapshot-access API.
     *
     * Block-driver may provide snapshot-access API: special functions to access
     * some internal "snapshot". The functions are similar with normal
     * read/block_status/discard handler, but don't have any specific handling
     * in generic block-layer: no serializing, no alignment, no tracked
     * requests. So, block-driver that realizes these APIs is fully responsible
     * for synchronization between snapshot-access API and normal IO requests.
     *
     * TODO: To be able to support qcow2's internal snapshots, this API will
     * need to be extended to:
     * - be able to select a specific snapshot
     * - receive the snapshot's actual length (which may differ from bs's
     *   length)
     */
    int coroutine_fn (*bdrv_co_preadv_snapshot)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov, size_t qiov_offset);
    int coroutine_fn (*bdrv_co_snapshot_block_status)(BlockDriverState *bs,
        bool want_zero, int64_t offset, int64_t bytes, int64_t *pnum,
        int64_t *map, BlockDriverState **file);
    int coroutine_fn (*bdrv_co_pdiscard_snapshot)(BlockDriverState *bs,
        int64_t offset, int64_t bytes);

    /*
     * Invalidate any cached meta-data.
     */
    void coroutine_fn GRAPH_RDLOCK_PTR (*bdrv_co_invalidate_cache)(
        BlockDriverState *bs, Error **errp);

    /*
     * Flushes all data for all layers by calling bdrv_co_flush for underlying
     * layers, if needed. This function is needed for deterministic
     * synchronization of the flush finishing callback.
     */
    int coroutine_fn (*bdrv_co_flush)(BlockDriverState *bs);

    /* Delete a created file. */
    int coroutine_fn (*bdrv_co_delete_file)(BlockDriverState *bs,
                                            Error **errp);

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

    /*
     * Truncate @bs to @offset bytes using the given @prealloc mode
     * when growing.  Modes other than PREALLOC_MODE_OFF should be
     * rejected when shrinking @bs.
     *
     * If @exact is true, @bs must be resized to exactly @offset.
     * Otherwise, it is sufficient for @bs (if it is a host block
     * device and thus there is no way to resize it) to be at least
     * @offset bytes in length.
     *
     * If @exact is true and this function fails but would succeed
     * with @exact = false, it should return -ENOTSUP.
     */
    int coroutine_fn (*bdrv_co_truncate)(BlockDriverState *bs, int64_t offset,
                                         bool exact, PreallocMode prealloc,
                                         BdrvRequestFlags flags, Error **errp);
    int64_t coroutine_fn (*bdrv_co_getlength)(BlockDriverState *bs);
    int64_t coroutine_fn (*bdrv_co_get_allocated_file_size)(
        BlockDriverState *bs);

    BlockMeasureInfo *(*bdrv_measure)(QemuOpts *opts, BlockDriverState *in_bs,
                                      Error **errp);

    int coroutine_fn (*bdrv_co_pwritev_compressed)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_pwritev_compressed_part)(BlockDriverState *bs,
        int64_t offset, int64_t bytes, QEMUIOVector *qiov,
        size_t qiov_offset);

    int coroutine_fn (*bdrv_co_get_info)(BlockDriverState *bs,
                                         BlockDriverInfo *bdi);

    ImageInfoSpecific *(*bdrv_get_specific_info)(BlockDriverState *bs,
                                                 Error **errp);
    BlockStatsSpecific *(*bdrv_get_specific_stats)(BlockDriverState *bs);

    int coroutine_fn GRAPH_RDLOCK_PTR (*bdrv_co_save_vmstate)(
        BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos);

    int coroutine_fn GRAPH_RDLOCK_PTR (*bdrv_co_load_vmstate)(
        BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos);

    /* removable device specific */
    bool coroutine_fn (*bdrv_co_is_inserted)(BlockDriverState *bs);
    void coroutine_fn (*bdrv_co_eject)(BlockDriverState *bs, bool eject_flag);
    void coroutine_fn (*bdrv_co_lock_medium)(BlockDriverState *bs, bool locked);

    /* to control generic scsi devices */
    BlockAIOCB *(*bdrv_aio_ioctl)(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque);
    int coroutine_fn (*bdrv_co_ioctl)(BlockDriverState *bs,
                                      unsigned long int req, void *buf);

    /*
     * Returns 0 for completed check, -errno for internal errors.
     * The check results are stored in result.
     */
    int coroutine_fn GRAPH_RDLOCK_PTR (*bdrv_co_check)(
        BlockDriverState *bs, BdrvCheckResult *result, BdrvCheckMode fix);

    void coroutine_fn (*bdrv_co_debug_event)(BlockDriverState *bs,
                                             BlkdebugEvent event);

    /* io queue for linux-aio */
    void coroutine_fn (*bdrv_co_io_plug)(BlockDriverState *bs);
    void coroutine_fn (*bdrv_co_io_unplug)(BlockDriverState *bs);

    /**
     * bdrv_drain_begin is called if implemented in the beginning of a
     * drain operation to drain and stop any internal sources of requests in
     * the driver.
     * bdrv_drain_end is called if implemented at the end of the drain.
     *
     * They should be used by the driver to e.g. manage scheduled I/O
     * requests, or toggle an internal state. After the end of the drain new
     * requests will continue normally.
     *
     * Implementations of both functions must not call aio_poll().
     */
    void (*bdrv_drain_begin)(BlockDriverState *bs);
    void (*bdrv_drain_end)(BlockDriverState *bs);

    bool (*bdrv_supports_persistent_dirty_bitmap)(BlockDriverState *bs);
    bool coroutine_fn (*bdrv_co_can_store_new_dirty_bitmap)(
        BlockDriverState *bs, const char *name, uint32_t granularity,
        Error **errp);
    int coroutine_fn (*bdrv_co_remove_persistent_dirty_bitmap)(
        BlockDriverState *bs, const char *name, Error **errp);
};

static inline bool block_driver_can_compress(BlockDriver *drv)
{
    return drv->bdrv_co_pwritev_compressed ||
           drv->bdrv_co_pwritev_compressed_part;
}

typedef struct BlockLimits {
    /*
     * Alignment requirement, in bytes, for offset/length of I/O
     * requests. Must be a power of 2 less than INT_MAX; defaults to
     * 1 for drivers with modern byte interfaces, and to 512
     * otherwise.
     */
    uint32_t request_alignment;

    /*
     * Maximum number of bytes that can be discarded at once. Must be multiple
     * of pdiscard_alignment, but need not be power of 2. May be 0 if no
     * inherent 64-bit limit.
     */
    int64_t max_pdiscard;

    /*
     * Optimal alignment for discard requests in bytes. A power of 2
     * is best but not mandatory.  Must be a multiple of
     * bl.request_alignment, and must be less than max_pdiscard if
     * that is set. May be 0 if bl.request_alignment is good enough
     */
    uint32_t pdiscard_alignment;

    /*
     * Maximum number of bytes that can zeroized at once. Must be multiple of
     * pwrite_zeroes_alignment. 0 means no limit.
     */
    int64_t max_pwrite_zeroes;

    /*
     * Optimal alignment for write zeroes requests in bytes. A power
     * of 2 is best but not mandatory.  Must be a multiple of
     * bl.request_alignment, and must be less than max_pwrite_zeroes
     * if that is set. May be 0 if bl.request_alignment is good
     * enough
     */
    uint32_t pwrite_zeroes_alignment;

    /*
     * Optimal transfer length in bytes.  A power of 2 is best but not
     * mandatory.  Must be a multiple of bl.request_alignment, or 0 if
     * no preferred size
     */
    uint32_t opt_transfer;

    /*
     * Maximal transfer length in bytes.  Need not be power of 2, but
     * must be multiple of opt_transfer and bl.request_alignment, or 0
     * for no 32-bit limit.  For now, anything larger than INT_MAX is
     * clamped down.
     */
    uint32_t max_transfer;

    /*
     * Maximal hardware transfer length in bytes.  Applies whenever
     * transfers to the device bypass the kernel I/O scheduler, for
     * example with SG_IO.  If larger than max_transfer or if zero,
     * blk_get_max_hw_transfer will fall back to max_transfer.
     */
    uint64_t max_hw_transfer;

    /*
     * Maximal number of scatter/gather elements allowed by the hardware.
     * Applies whenever transfers to the device bypass the kernel I/O
     * scheduler, for example with SG_IO.  If larger than max_iov
     * or if zero, blk_get_max_hw_iov will fall back to max_iov.
     */
    int max_hw_iov;


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

struct BdrvChildClass {
    /*
     * If true, bdrv_replace_node() doesn't change the node this BdrvChild
     * points to.
     */
    bool stay_at_node;

    /*
     * If true, the parent is a BlockDriverState and bdrv_next_all_states()
     * will return it. This information is used for drain_all, where every node
     * will be drained separately, so the drain only needs to be propagated to
     * non-BDS parents.
     */
    bool parent_is_bds;

    /*
     * Global state (GS) API. These functions run under the BQL.
     *
     * See include/block/block-global-state.h for more information about
     * the GS API.
     */
    void (*inherit_options)(BdrvChildRole role, bool parent_is_format,
                            int *child_flags, QDict *child_options,
                            int parent_flags, QDict *parent_options);
    void (*change_media)(BdrvChild *child, bool load);

    /*
     * Returns a malloced string that describes the parent of the child for a
     * human reader. This could be a node-name, BlockBackend name, qdev ID or
     * QOM path of the device owning the BlockBackend, job type and ID etc. The
     * caller is responsible for freeing the memory.
     */
    char *(*get_parent_desc)(BdrvChild *child);

    /*
     * Notifies the parent that the child has been activated/inactivated (e.g.
     * when migration is completing) and it can start/stop requesting
     * permissions and doing I/O on it.
     */
    void (*activate)(BdrvChild *child, Error **errp);
    int (*inactivate)(BdrvChild *child);

    void GRAPH_WRLOCK_PTR (*attach)(BdrvChild *child);
    void GRAPH_WRLOCK_PTR (*detach)(BdrvChild *child);

    /*
     * Notifies the parent that the filename of its child has changed (e.g.
     * because the direct child was removed from the backing chain), so that it
     * can update its reference.
     */
    int (*update_filename)(BdrvChild *child, BlockDriverState *new_base,
                           const char *filename, Error **errp);

    bool (*change_aio_ctx)(BdrvChild *child, AioContext *ctx,
                           GHashTable *visited, Transaction *tran,
                           Error **errp);

    /*
     * I/O API functions. These functions are thread-safe.
     *
     * See include/block/block-io.h for more information about
     * the I/O API.
     */

    void (*resize)(BdrvChild *child);

    /*
     * Returns a name that is supposedly more useful for human users than the
     * node name for identifying the node in question (in particular, a BB
     * name), or NULL if the parent can't provide a better name.
     */
    const char *(*get_name)(BdrvChild *child);

    AioContext *(*get_parent_aio_context)(BdrvChild *child);

    /*
     * If this pair of functions is implemented, the parent doesn't issue new
     * requests after returning from .drained_begin() until .drained_end() is
     * called.
     *
     * These functions must not change the graph (and therefore also must not
     * call aio_poll(), which could change the graph indirectly).
     *
     * Note that this can be nested. If drained_begin() was called twice, new
     * I/O is allowed only after drained_end() was called twice, too.
     */
    void (*drained_begin)(BdrvChild *child);
    void (*drained_end)(BdrvChild *child);

    /*
     * Returns whether the parent has pending requests for the child. This
     * callback is polled after .drained_begin() has been called until all
     * activity on the child has stopped.
     */
    bool (*drained_poll)(BdrvChild *child);
};

extern const BdrvChildClass child_of_bds;

struct BdrvChild {
    BlockDriverState *bs;
    char *name;
    const BdrvChildClass *klass;
    BdrvChildRole role;
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

    /*
     * This link is frozen: the child can neither be replaced nor
     * detached from the parent.
     */
    bool frozen;

    /*
     * True if the parent of this child has been drained by this BdrvChild
     * (through klass->drained_*).
     *
     * It is generally true if bs->quiesce_counter > 0. It may differ while the
     * child is entering or leaving a drained section.
     */
    bool quiesced_parent;

    QLIST_ENTRY(BdrvChild) next;
    QLIST_ENTRY(BdrvChild) next_parent;
};

/*
 * Allows bdrv_co_block_status() to cache one data region for a
 * protocol node.
 *
 * @valid: Whether the cache is valid (should be accessed with atomic
 *         functions so this can be reset by RCU readers)
 * @data_start: Offset where we know (or strongly assume) is data
 * @data_end: Offset where the data region ends (which is not necessarily
 *            the start of a zeroed region)
 */
typedef struct BdrvBlockStatusCache {
    struct rcu_head rcu;

    bool valid;
    int64_t data_start;
    int64_t data_end;
} BdrvBlockStatusCache;

struct BlockDriverState {
    /*
     * Protected by big QEMU lock or read-only after opening.  No special
     * locking needed during I/O...
     */
    int open_flags; /* flags used to open the file, re-used for re-open */
    bool encrypted; /* if true, the media is encrypted */
    bool sg;        /* if true, the device is a /dev/sg* */
    bool probed;    /* if true, format was probed rather than specified */
    bool force_share; /* if true, always allow all shared permissions */
    bool implicit;  /* if true, this filter node was automatically inserted */

    BlockDriver *drv; /* NULL means no media */
    void *opaque;

    AioContext *aio_context; /* event loop used for fd handlers, timers, etc */
    /*
     * long-running tasks intended to always use the same AioContext as this
     * BDS may register themselves in this list to be notified of changes
     * regarding this BDS's context
     */
    QLIST_HEAD(, BdrvAioNotifier) aio_notifiers;
    bool walking_aio_notifiers; /* to make removal during iteration safe */

    char filename[PATH_MAX];
    /*
     * If not empty, this image is a diff in relation to backing_file.
     * Note that this is the name given in the image header and
     * therefore may or may not be equal to .backing->bs->filename.
     * If this field contains a relative path, it is to be resolved
     * relatively to the overlay's location.
     */
    char backing_file[PATH_MAX];
    /*
     * The backing filename indicated by the image header.  Contrary
     * to backing_file, if we ever open this file, auto_backing_file
     * is replaced by the resulting BDS's filename (i.e. after a
     * bdrv_refresh_filename() run).
     */
    char auto_backing_file[PATH_MAX];
    char backing_format[16]; /* if non-zero and backing_file exists */

    QDict *full_open_options;
    char exact_filename[PATH_MAX];

    /* I/O Limits */
    BlockLimits bl;

    /*
     * Flags honored during pread
     */
    BdrvRequestFlags supported_read_flags;
    /*
     * Flags honored during pwrite (so far: BDRV_REQ_FUA,
     * BDRV_REQ_WRITE_UNCHANGED).
     * If a driver does not support BDRV_REQ_WRITE_UNCHANGED, those
     * writes will be issued as normal writes without the flag set.
     * This is important to note for drivers that do not explicitly
     * request a WRITE permission for their children and instead take
     * the same permissions as their parent did (this is commonly what
     * block filters do).  Such drivers have to be aware that the
     * parent may have taken a WRITE_UNCHANGED permission only and is
     * issuing such requests.  Drivers either must make sure that
     * these requests do not result in plain WRITE accesses (usually
     * by supporting BDRV_REQ_WRITE_UNCHANGED, and then forwarding
     * every incoming write request as-is, including potentially that
     * flag), or they have to explicitly take the WRITE permission for
     * their children.
     */
    BdrvRequestFlags supported_write_flags;
    /*
     * Flags honored during pwrite_zeroes (so far: BDRV_REQ_FUA,
     * BDRV_REQ_MAY_UNMAP, BDRV_REQ_WRITE_UNCHANGED)
     */
    BdrvRequestFlags supported_zero_flags;
    /*
     * Flags honoured during truncate (so far: BDRV_REQ_ZERO_WRITE).
     *
     * If BDRV_REQ_ZERO_WRITE is given, the truncate operation must make sure
     * that any added space reads as all zeros. If this can't be guaranteed,
     * the operation must fail.
     */
    BdrvRequestFlags supported_truncate_flags;

    /* the following member gives a name to every node on the bs graph. */
    char node_name[32];
    /* element of the list of named nodes building the graph */
    QTAILQ_ENTRY(BlockDriverState) node_list;
    /* element of the list of all BlockDriverStates (all_bdrv_states) */
    QTAILQ_ENTRY(BlockDriverState) bs_list;
    /* element of the list of monitor-owned BDS */
    QTAILQ_ENTRY(BlockDriverState) monitor_list;
    int refcnt;

    /* operation blockers. Protected by BQL. */
    QLIST_HEAD(, BdrvOpBlocker) op_blockers[BLOCK_OP_TYPE_MAX];

    /*
     * The node that this node inherited default options from (and a reopen on
     * which can affect this node by changing these defaults). This is always a
     * parent node of this node.
     */
    BlockDriverState *inherits_from;

    /*
     * @backing and @file are some of @children or NULL. All these three fields
     * (@file, @backing and @children) are modified only in
     * bdrv_child_cb_attach() and bdrv_child_cb_detach().
     *
     * See also comment in include/block/block.h, to learn how backing and file
     * are connected with BdrvChildRole.
     */
    QLIST_HEAD(, BdrvChild) children;
    BdrvChild *backing;
    BdrvChild *file;

    QLIST_HEAD(, BdrvChild) parents;

    QDict *options;
    QDict *explicit_options;
    BlockdevDetectZeroesOptions detect_zeroes;

    /* The error object in use for blocking operations on backing_hd */
    Error *backing_blocker;

    /* Protected by AioContext lock */

    /*
     * If we are reading a disk image, give its size in sectors.
     * Generally read-only; it is written to by load_snapshot and
     * save_snaphost, but the block layer is quiescent during those.
     */
    int64_t total_sectors;

    /* threshold limit for writes, in bytes. "High water mark". */
    uint64_t write_threshold_offset;

    /*
     * Writing to the list requires the BQL _and_ the dirty_bitmap_mutex.
     * Reading from the list can be done with either the BQL or the
     * dirty_bitmap_mutex.  Modifying a bitmap only requires
     * dirty_bitmap_mutex.
     */
    QemuMutex dirty_bitmap_mutex;
    QLIST_HEAD(, BdrvDirtyBitmap) dirty_bitmaps;

    /* Offset after the highest byte written to */
    Stat64 wr_highest_offset;

    /*
     * If true, copy read backing sectors into image.  Can be >1 if more
     * than one client has requested copy-on-read.  Accessed with atomic
     * ops.
     */
    int copy_on_read;

    /*
     * number of in-flight requests; overall and serialising.
     * Accessed with atomic ops.
     */
    unsigned int in_flight;
    unsigned int serialising_in_flight;

    /*
     * counter for nested bdrv_io_plug.
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

    /* BdrvChild links to this node may never be frozen */
    bool never_freeze;

    /* Lock for block-status cache RCU writers */
    CoMutex bsc_modify_lock;
    /* Always non-NULL, but must only be dereferenced under an RCU read guard */
    BdrvBlockStatusCache *block_status_cache;
};

struct BlockBackendRootState {
    int open_flags;
    BlockdevDetectZeroesOptions detect_zeroes;
};

typedef enum BlockMirrorBackingMode {
    /*
     * Reuse the existing backing chain from the source for the target.
     * - sync=full: Set backing BDS to NULL.
     * - sync=top:  Use source's backing BDS.
     * - sync=none: Use source as the backing BDS.
     */
    MIRROR_SOURCE_BACKING_CHAIN,

    /* Open the target's backing chain completely anew */
    MIRROR_OPEN_BACKING_CHAIN,

    /* Do not change the target's backing BDS after job completion */
    MIRROR_LEAVE_BACKING_CHAIN,
} BlockMirrorBackingMode;


/*
 * Essential block drivers which must always be statically linked into qemu, and
 * which therefore can be accessed without using bdrv_find_format()
 */
extern BlockDriver bdrv_file;
extern BlockDriver bdrv_raw;
extern BlockDriver bdrv_qcow2;

extern unsigned int bdrv_drain_all_count;
extern QemuOptsList bdrv_create_opts_simple;

/*
 * Common functions that are neither I/O nor Global State.
 *
 * See include/block/block-commmon.h for more information about
 * the Common API.
 */

static inline BlockDriverState *child_bs(BdrvChild *child)
{
    return child ? child->bs : NULL;
}

int bdrv_check_request(int64_t offset, int64_t bytes, Error **errp);
char *create_tmp_file(Error **errp);
void bdrv_parse_filename_strip_prefix(const char *filename, const char *prefix,
                                      QDict *options);


int bdrv_check_qiov_request(int64_t offset, int64_t bytes,
                            QEMUIOVector *qiov, size_t qiov_offset,
                            Error **errp);

#ifdef _WIN32
int is_windows_drive(const char *filename);
#endif

#endif /* BLOCK_INT_COMMON_H */
