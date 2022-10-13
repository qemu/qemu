/*
 * QEMU Block backends
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_GLOBAL_STATE_H
#define BLOCK_BACKEND_GLOBAL_STATE_H

#include "block-backend-common.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * See include/block/block-global-state.h for more information about
 * the GS API.
 */

BlockBackend *blk_new(AioContext *ctx, uint64_t perm, uint64_t shared_perm);
BlockBackend *blk_new_with_bs(BlockDriverState *bs, uint64_t perm,
                              uint64_t shared_perm, Error **errp);
BlockBackend *blk_new_open(const char *filename, const char *reference,
                           QDict *options, int flags, Error **errp);
int blk_get_refcnt(BlockBackend *blk);
void blk_ref(BlockBackend *blk);
void blk_unref(BlockBackend *blk);
void blk_remove_all_bs(void);
BlockBackend *blk_by_name(const char *name);
BlockBackend *blk_next(BlockBackend *blk);
BlockBackend *blk_all_next(BlockBackend *blk);
bool monitor_add_blk(BlockBackend *blk, const char *name, Error **errp);
void monitor_remove_blk(BlockBackend *blk);

BlockBackendPublic *blk_get_public(BlockBackend *blk);
BlockBackend *blk_by_public(BlockBackendPublic *public);

void blk_remove_bs(BlockBackend *blk);
int blk_insert_bs(BlockBackend *blk, BlockDriverState *bs, Error **errp);
int blk_replace_bs(BlockBackend *blk, BlockDriverState *new_bs, Error **errp);
bool bdrv_has_blk(BlockDriverState *bs);
bool bdrv_is_root_node(BlockDriverState *bs);
int blk_set_perm(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                 Error **errp);
void blk_get_perm(BlockBackend *blk, uint64_t *perm, uint64_t *shared_perm);

void blk_iostatus_enable(BlockBackend *blk);
BlockDeviceIoStatus blk_iostatus(const BlockBackend *blk);
void blk_iostatus_disable(BlockBackend *blk);
void blk_iostatus_reset(BlockBackend *blk);
int blk_attach_dev(BlockBackend *blk, DeviceState *dev);
void blk_detach_dev(BlockBackend *blk, DeviceState *dev);
DeviceState *blk_get_attached_dev(BlockBackend *blk);
BlockBackend *blk_by_dev(void *dev);
BlockBackend *blk_by_qdev_id(const char *id, Error **errp);
void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops, void *opaque);

void blk_activate(BlockBackend *blk, Error **errp);

int blk_make_zero(BlockBackend *blk, BdrvRequestFlags flags);
void blk_aio_cancel(BlockAIOCB *acb);
int blk_commit_all(void);
void blk_drain(BlockBackend *blk);
void blk_drain_all(void);
void blk_set_on_error(BlockBackend *blk, BlockdevOnError on_read_error,
                      BlockdevOnError on_write_error);
bool blk_supports_write_perm(BlockBackend *blk);
bool blk_is_sg(BlockBackend *blk);
void blk_set_enable_write_cache(BlockBackend *blk, bool wce);
int blk_get_flags(BlockBackend *blk);
bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp);
void blk_op_unblock(BlockBackend *blk, BlockOpType op, Error *reason);
void blk_op_block_all(BlockBackend *blk, Error *reason);
void blk_op_unblock_all(BlockBackend *blk, Error *reason);
int blk_set_aio_context(BlockBackend *blk, AioContext *new_context,
                        Error **errp);
void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque);
void blk_remove_aio_context_notifier(BlockBackend *blk,
                                     void (*attached_aio_context)(AioContext *,
                                                                  void *),
                                     void (*detach_aio_context)(void *),
                                     void *opaque);
void blk_add_remove_bs_notifier(BlockBackend *blk, Notifier *notify);
void blk_add_insert_bs_notifier(BlockBackend *blk, Notifier *notify);
BlockBackendRootState *blk_get_root_state(BlockBackend *blk);
void blk_update_root_state(BlockBackend *blk);
bool blk_get_detect_zeroes_from_root_state(BlockBackend *blk);
int blk_get_open_flags_from_root_state(BlockBackend *blk);

int blk_save_vmstate(BlockBackend *blk, const uint8_t *buf,
                     int64_t pos, int size);
int blk_load_vmstate(BlockBackend *blk, uint8_t *buf, int64_t pos, int size);
int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz);
int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo);

void blk_set_io_limits(BlockBackend *blk, ThrottleConfig *cfg);
void blk_io_limits_disable(BlockBackend *blk);
void blk_io_limits_enable(BlockBackend *blk, const char *group);
void blk_io_limits_update_group(BlockBackend *blk, const char *group);
void blk_set_force_allow_inactivate(BlockBackend *blk);

bool blk_register_buf(BlockBackend *blk, void *host, size_t size, Error **errp);
void blk_unregister_buf(BlockBackend *blk, void *host, size_t size);

const BdrvChild *blk_root(BlockBackend *blk);

int blk_make_empty(BlockBackend *blk, Error **errp);

#endif /* BLOCK_BACKEND_GLOBAL_STATE_H */
