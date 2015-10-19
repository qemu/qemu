/*
 * QEMU Block backends
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_H
#define BLOCK_BACKEND_H

#include "qemu/typedefs.h"
#include "qapi/error.h"

/*
 * TODO Have to include block/block.h for a bunch of block layer
 * types.  Unfortunately, this pulls in the whole BlockDriverState
 * API, which we don't want used by many BlockBackend users.  Some of
 * the types belong here, and the rest should be split into a common
 * header and one for the BlockDriverState API.
 */
#include "block/block.h"

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

BlockBackend *blk_new(const char *name, Error **errp);
BlockBackend *blk_new_with_bs(const char *name, Error **errp);
BlockBackend *blk_new_open(const char *name, const char *filename,
                           const char *reference, QDict *options, int flags,
                           Error **errp);
void blk_ref(BlockBackend *blk);
void blk_unref(BlockBackend *blk);
const char *blk_name(BlockBackend *blk);
BlockBackend *blk_by_name(const char *name);
BlockBackend *blk_next(BlockBackend *blk);

BlockDriverState *blk_bs(BlockBackend *blk);

void blk_hide_on_behalf_of_hmp_drive_del(BlockBackend *blk);

void blk_iostatus_enable(BlockBackend *blk);
int blk_attach_dev(BlockBackend *blk, void *dev);
void blk_attach_dev_nofail(BlockBackend *blk, void *dev);
void blk_detach_dev(BlockBackend *blk, void *dev);
void *blk_get_attached_dev(BlockBackend *blk);
void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops, void *opaque);
int blk_read(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
             int nb_sectors);
int blk_read_unthrottled(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
                         int nb_sectors);
int blk_write(BlockBackend *blk, int64_t sector_num, const uint8_t *buf,
              int nb_sectors);
int blk_write_zeroes(BlockBackend *blk, int64_t sector_num,
                     int nb_sectors, BdrvRequestFlags flags);
BlockAIOCB *blk_aio_write_zeroes(BlockBackend *blk, int64_t sector_num,
                                 int nb_sectors, BdrvRequestFlags flags,
                                 BlockCompletionFunc *cb, void *opaque);
int blk_pread(BlockBackend *blk, int64_t offset, void *buf, int count);
int blk_pwrite(BlockBackend *blk, int64_t offset, const void *buf, int count);
int64_t blk_getlength(BlockBackend *blk);
void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr);
int64_t blk_nb_sectors(BlockBackend *blk);
BlockAIOCB *blk_aio_readv(BlockBackend *blk, int64_t sector_num,
                          QEMUIOVector *iov, int nb_sectors,
                          BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_writev(BlockBackend *blk, int64_t sector_num,
                           QEMUIOVector *iov, int nb_sectors,
                           BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_discard(BlockBackend *blk,
                            int64_t sector_num, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque);
void blk_aio_cancel(BlockAIOCB *acb);
void blk_aio_cancel_async(BlockAIOCB *acb);
int blk_aio_multiwrite(BlockBackend *blk, BlockRequest *reqs, int num_reqs);
int blk_ioctl(BlockBackend *blk, unsigned long int req, void *buf);
BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque);
int blk_co_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors);
int blk_co_flush(BlockBackend *blk);
int blk_flush(BlockBackend *blk);
int blk_flush_all(void);
void blk_drain(BlockBackend *blk);
void blk_drain_all(void);
BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read);
BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error);
void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error);
int blk_is_read_only(BlockBackend *blk);
int blk_is_sg(BlockBackend *blk);
int blk_enable_write_cache(BlockBackend *blk);
void blk_set_enable_write_cache(BlockBackend *blk, bool wce);
void blk_invalidate_cache(BlockBackend *blk, Error **errp);
bool blk_is_inserted(BlockBackend *blk);
bool blk_is_available(BlockBackend *blk);
void blk_lock_medium(BlockBackend *blk, bool locked);
void blk_eject(BlockBackend *blk, bool eject_flag);
int blk_get_flags(BlockBackend *blk);
int blk_get_max_transfer_length(BlockBackend *blk);
void blk_set_guest_block_size(BlockBackend *blk, int align);
void *blk_blockalign(BlockBackend *blk, size_t size);
bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp);
void blk_op_unblock(BlockBackend *blk, BlockOpType op, Error *reason);
void blk_op_block_all(BlockBackend *blk, Error *reason);
void blk_op_unblock_all(BlockBackend *blk, Error *reason);
AioContext *blk_get_aio_context(BlockBackend *blk);
void blk_set_aio_context(BlockBackend *blk, AioContext *new_context);
void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque);
void blk_remove_aio_context_notifier(BlockBackend *blk,
                                     void (*attached_aio_context)(AioContext *,
                                                                  void *),
                                     void (*detach_aio_context)(void *),
                                     void *opaque);
void blk_add_close_notifier(BlockBackend *blk, Notifier *notify);
void blk_io_plug(BlockBackend *blk);
void blk_io_unplug(BlockBackend *blk);
BlockAcctStats *blk_get_stats(BlockBackend *blk);

void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque);
int coroutine_fn blk_co_write_zeroes(BlockBackend *blk, int64_t sector_num,
                                     int nb_sectors, BdrvRequestFlags flags);
int blk_write_compressed(BlockBackend *blk, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors);
int blk_truncate(BlockBackend *blk, int64_t offset);
int blk_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors);
int blk_save_vmstate(BlockBackend *blk, const uint8_t *buf,
                     int64_t pos, int size);
int blk_load_vmstate(BlockBackend *blk, uint8_t *buf, int64_t pos, int size);
int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz);
int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo);

#endif
