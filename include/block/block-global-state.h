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
#ifndef BLOCK_GLOBAL_STATE_H
#define BLOCK_GLOBAL_STATE_H

#include "block-common.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * If a function modifies the graph, it also uses drain and/or
 * aio_context_acquire/release to be sure it has unique access.
 * aio_context locking is needed together with BQL because of
 * the thread-safe I/O API that concurrently runs and accesses
 * the graph without the BQL.
 *
 * It is important to note that not all of these functions are
 * necessarily limited to running under the BQL, but they would
 * require additional auditing and many small thread-safety changes
 * to move them into the I/O API. Often it's not worth doing that
 * work since the APIs are only used with the BQL held at the
 * moment, so they have been placed in the GS API (for now).
 *
 * These functions can call any function from this and other categories
 * (I/O, "I/O or GS", Common), but must be invoked only by other GS APIs.
 *
 * All functions in this header must use the macro
 * GLOBAL_STATE_CODE();
 * to catch when they are accidentally called without the BQL.
 */

void bdrv_init(void);
BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix,
                                Error **errp);
BlockDriver *bdrv_find_format(const char *format_name);
int bdrv_create(BlockDriver *drv, const char* filename,
                QemuOpts *opts, Error **errp);
int bdrv_create_file(const char *filename, QemuOpts *opts, Error **errp);

BlockDriverState *bdrv_new(void);
int bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top,
                Error **errp);
int bdrv_replace_node(BlockDriverState *from, BlockDriverState *to,
                      Error **errp);
int bdrv_replace_child_bs(BdrvChild *child, BlockDriverState *new_bs,
                          Error **errp);
BlockDriverState *bdrv_insert_node(BlockDriverState *bs, QDict *node_options,
                                   int flags, Error **errp);
int bdrv_drop_filter(BlockDriverState *bs, Error **errp);

BdrvChild *bdrv_open_child(const char *filename,
                           QDict *options, const char *bdref_key,
                           BlockDriverState *parent,
                           const BdrvChildClass *child_class,
                           BdrvChildRole child_role,
                           bool allow_none, Error **errp);
BlockDriverState *bdrv_open_blockdev_ref(BlockdevRef *ref, Error **errp);
int bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd,
                        Error **errp);
int bdrv_open_backing_file(BlockDriverState *bs, QDict *parent_options,
                           const char *bdref_key, Error **errp);
BlockDriverState *bdrv_open(const char *filename, const char *reference,
                            QDict *options, int flags, Error **errp);
BlockDriverState *bdrv_new_open_driver_opts(BlockDriver *drv,
                                            const char *node_name,
                                            QDict *options, int flags,
                                            Error **errp);
BlockDriverState *bdrv_new_open_driver(BlockDriver *drv, const char *node_name,
                                       int flags, Error **errp);
BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs, QDict *options,
                                    bool keep_old_opts);
void bdrv_reopen_queue_free(BlockReopenQueue *bs_queue);
int bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp);
int bdrv_reopen(BlockDriverState *bs, QDict *opts, bool keep_old_opts,
                Error **errp);
int bdrv_reopen_set_read_only(BlockDriverState *bs, bool read_only,
                              Error **errp);
BlockDriverState *bdrv_find_backing_image(BlockDriverState *bs,
                                          const char *backing_file);
void bdrv_refresh_filename(BlockDriverState *bs);
void bdrv_refresh_limits(BlockDriverState *bs, Transaction *tran, Error **errp);
int bdrv_commit(BlockDriverState *bs);
int bdrv_make_empty(BdrvChild *c, Error **errp);
int bdrv_change_backing_file(BlockDriverState *bs, const char *backing_file,
                             const char *backing_fmt, bool warn);
void bdrv_register(BlockDriver *bdrv);
int bdrv_drop_intermediate(BlockDriverState *top, BlockDriverState *base,
                           const char *backing_file_str);
BlockDriverState *bdrv_find_overlay(BlockDriverState *active,
                                    BlockDriverState *bs);
BlockDriverState *bdrv_find_base(BlockDriverState *bs);
bool bdrv_is_backing_chain_frozen(BlockDriverState *bs, BlockDriverState *base,
                                  Error **errp);
int bdrv_freeze_backing_chain(BlockDriverState *bs, BlockDriverState *base,
                              Error **errp);
void bdrv_unfreeze_backing_chain(BlockDriverState *bs, BlockDriverState *base);

/*
 * The units of offset and total_work_size may be chosen arbitrarily by the
 * block driver; total_work_size may change during the course of the amendment
 * operation
 */
typedef void BlockDriverAmendStatusCB(BlockDriverState *bs, int64_t offset,
                                      int64_t total_work_size, void *opaque);
int bdrv_amend_options(BlockDriverState *bs_new, QemuOpts *opts,
                       BlockDriverAmendStatusCB *status_cb, void *cb_opaque,
                       bool force,
                       Error **errp);

/* check if a named node can be replaced when doing drive-mirror */
BlockDriverState *check_to_replace_node(BlockDriverState *parent_bs,
                                        const char *node_name, Error **errp);

int bdrv_activate(BlockDriverState *bs, Error **errp);
void bdrv_activate_all(Error **errp);
int bdrv_inactivate_all(void);

int bdrv_flush_all(void);
void bdrv_close_all(void);
void bdrv_drain_all_begin(void);
void bdrv_drain_all_end(void);
void bdrv_drain_all(void);

int bdrv_has_zero_init_1(BlockDriverState *bs);
int bdrv_has_zero_init(BlockDriverState *bs);
BlockDriverState *bdrv_find_node(const char *node_name);
BlockDeviceInfoList *bdrv_named_nodes_list(bool flat, Error **errp);
XDbgBlockGraph *bdrv_get_xdbg_block_graph(Error **errp);
BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp);
bool bdrv_chain_contains(BlockDriverState *top, BlockDriverState *base);
BlockDriverState *bdrv_next_node(BlockDriverState *bs);
BlockDriverState *bdrv_next_all_states(BlockDriverState *bs);

typedef struct BdrvNextIterator {
    enum {
        BDRV_NEXT_BACKEND_ROOTS,
        BDRV_NEXT_MONITOR_OWNED,
    } phase;
    BlockBackend *blk;
    BlockDriverState *bs;
} BdrvNextIterator;

BlockDriverState *bdrv_first(BdrvNextIterator *it);
BlockDriverState *bdrv_next(BdrvNextIterator *it);
void bdrv_next_cleanup(BdrvNextIterator *it);

BlockDriverState *bdrv_next_monitor_owned(BlockDriverState *bs);
void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque, bool read_only);
int bdrv_get_flags(BlockDriverState *bs);
char *bdrv_get_full_backing_filename(BlockDriverState *bs, Error **errp);
char *bdrv_dirname(BlockDriverState *bs, Error **errp);

void bdrv_img_create(const char *filename, const char *fmt,
                     const char *base_filename, const char *base_fmt,
                     char *options, uint64_t img_size, int flags,
                     bool quiet, Error **errp);

void bdrv_ref(BlockDriverState *bs);
void bdrv_unref(BlockDriverState *bs);
void bdrv_unref_child(BlockDriverState *parent, BdrvChild *child);
BdrvChild *bdrv_attach_child(BlockDriverState *parent_bs,
                             BlockDriverState *child_bs,
                             const char *child_name,
                             const BdrvChildClass *child_class,
                             BdrvChildRole child_role,
                             Error **errp);

bool bdrv_op_is_blocked(BlockDriverState *bs, BlockOpType op, Error **errp);
void bdrv_op_block(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_unblock(BlockDriverState *bs, BlockOpType op, Error *reason);
void bdrv_op_block_all(BlockDriverState *bs, Error *reason);
void bdrv_op_unblock_all(BlockDriverState *bs, Error *reason);
bool bdrv_op_blocker_is_empty(BlockDriverState *bs);

int bdrv_debug_breakpoint(BlockDriverState *bs, const char *event,
                           const char *tag);
int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag);
int bdrv_debug_resume(BlockDriverState *bs, const char *tag);
bool bdrv_debug_is_suspended(BlockDriverState *bs, const char *tag);

/**
 * Locks the AioContext of @bs if it's not the current AioContext. This avoids
 * double locking which could lead to deadlocks: This is a coroutine_fn, so we
 * know we already own the lock of the current AioContext.
 *
 * May only be called in the main thread.
 */
void coroutine_fn bdrv_co_lock(BlockDriverState *bs);

/**
 * Unlocks the AioContext of @bs if it's not the current AioContext.
 */
void coroutine_fn bdrv_co_unlock(BlockDriverState *bs);

void bdrv_set_aio_context_ignore(BlockDriverState *bs,
                                 AioContext *new_context, GSList **ignore);
int bdrv_try_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                             Error **errp);
int bdrv_child_try_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                                   BdrvChild *ignore_child, Error **errp);
bool bdrv_child_can_set_aio_context(BdrvChild *c, AioContext *ctx,
                                    GSList **ignore, Error **errp);
bool bdrv_can_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                              GSList **ignore, Error **errp);
AioContext *bdrv_child_get_parent_aio_context(BdrvChild *c);

int bdrv_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz);
int bdrv_probe_geometry(BlockDriverState *bs, HDGeometry *geo);

void bdrv_add_child(BlockDriverState *parent, BlockDriverState *child,
                    Error **errp);
void bdrv_del_child(BlockDriverState *parent, BdrvChild *child, Error **errp);

/**
 *
 * bdrv_register_buf/bdrv_unregister_buf:
 *
 * Register/unregister a buffer for I/O. For example, VFIO drivers are
 * interested to know the memory areas that would later be used for I/O, so
 * that they can prepare IOMMU mapping etc., to get better performance.
 */
void bdrv_register_buf(BlockDriverState *bs, void *host, size_t size);
void bdrv_unregister_buf(BlockDriverState *bs, void *host);

void bdrv_cancel_in_flight(BlockDriverState *bs);

#endif /* BLOCK_GLOBAL_STATE_H */
