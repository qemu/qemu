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

#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "sysemu/blockdev.h"
#include "qapi-event.h"

/* Number of coroutines to reserve per attached device model */
#define COROUTINE_POOL_RESERVATION 64

struct BlockBackend {
    char *name;
    int refcnt;
    BlockDriverState *bs;
    DriveInfo *legacy_dinfo;    /* null unless created by drive_new() */
    QTAILQ_ENTRY(BlockBackend) link; /* for blk_backends */

    void *dev;                  /* attached device model, if any */
    /* TODO change to DeviceState when all users are qdevified */
    const BlockDevOps *dev_ops;
    void *dev_opaque;
};

static void drive_info_del(DriveInfo *dinfo);

/* All the BlockBackends (except for hidden ones) */
static QTAILQ_HEAD(, BlockBackend) blk_backends =
    QTAILQ_HEAD_INITIALIZER(blk_backends);

/*
 * Create a new BlockBackend with @name, with a reference count of one.
 * @name must not be null or empty.
 * Fail if a BlockBackend with this name already exists.
 * Store an error through @errp on failure, unless it's null.
 * Return the new BlockBackend on success, null on failure.
 */
BlockBackend *blk_new(const char *name, Error **errp)
{
    BlockBackend *blk;

    assert(name && name[0]);
    if (!id_wellformed(name)) {
        error_setg(errp, "Invalid device name");
        return NULL;
    }
    if (blk_by_name(name)) {
        error_setg(errp, "Device with id '%s' already exists", name);
        return NULL;
    }
    if (bdrv_find_node(name)) {
        error_setg(errp,
                   "Device name '%s' conflicts with an existing node name",
                   name);
        return NULL;
    }

    blk = g_new0(BlockBackend, 1);
    blk->name = g_strdup(name);
    blk->refcnt = 1;
    QTAILQ_INSERT_TAIL(&blk_backends, blk, link);
    return blk;
}

/*
 * Create a new BlockBackend with a new BlockDriverState attached.
 * Otherwise just like blk_new(), which see.
 */
BlockBackend *blk_new_with_bs(const char *name, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    blk = blk_new(name, errp);
    if (!blk) {
        return NULL;
    }

    bs = bdrv_new_root();
    blk->bs = bs;
    bs->blk = blk;
    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    assert(!blk->dev);
    if (blk->bs) {
        assert(blk->bs->blk == blk);
        blk->bs->blk = NULL;
        bdrv_unref(blk->bs);
        blk->bs = NULL;
    }
    /* Avoid double-remove after blk_hide_on_behalf_of_do_drive_del() */
    if (blk->name[0]) {
        QTAILQ_REMOVE(&blk_backends, blk, link);
    }
    g_free(blk->name);
    drive_info_del(blk->legacy_dinfo);
    g_free(blk);
}

static void drive_info_del(DriveInfo *dinfo)
{
    if (!dinfo) {
        return;
    }
    qemu_opts_del(dinfo->opts);
    g_free(dinfo->serial);
    g_free(dinfo);
}

/*
 * Increment @blk's reference count.
 * @blk must not be null.
 */
void blk_ref(BlockBackend *blk)
{
    blk->refcnt++;
}

/*
 * Decrement @blk's reference count.
 * If this drops it to zero, destroy @blk.
 * For convenience, do nothing if @blk is null.
 */
void blk_unref(BlockBackend *blk)
{
    if (blk) {
        assert(blk->refcnt > 0);
        if (!--blk->refcnt) {
            blk_delete(blk);
        }
    }
}

/*
 * Return the BlockBackend after @blk.
 * If @blk is null, return the first one.
 * Else, return @blk's next sibling, which may be null.
 *
 * To iterate over all BlockBackends, do
 * for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
 *     ...
 * }
 */
BlockBackend *blk_next(BlockBackend *blk)
{
    return blk ? QTAILQ_NEXT(blk, link) : QTAILQ_FIRST(&blk_backends);
}

/*
 * Return @blk's name, a non-null string.
 * Wart: the name is empty iff @blk has been hidden with
 * blk_hide_on_behalf_of_do_drive_del().
 */
const char *blk_name(BlockBackend *blk)
{
    return blk->name;
}

/*
 * Return the BlockBackend with name @name if it exists, else null.
 * @name must not be null.
 */
BlockBackend *blk_by_name(const char *name)
{
    BlockBackend *blk;

    assert(name);
    QTAILQ_FOREACH(blk, &blk_backends, link) {
        if (!strcmp(name, blk->name)) {
            return blk;
        }
    }
    return NULL;
}

/*
 * Return the BlockDriverState attached to @blk if any, else null.
 */
BlockDriverState *blk_bs(BlockBackend *blk)
{
    return blk->bs;
}

/*
 * Return @blk's DriveInfo if any, else null.
 */
DriveInfo *blk_legacy_dinfo(BlockBackend *blk)
{
    return blk->legacy_dinfo;
}

/*
 * Set @blk's DriveInfo to @dinfo, and return it.
 * @blk must not have a DriveInfo set already.
 * No other BlockBackend may have the same DriveInfo set.
 */
DriveInfo *blk_set_legacy_dinfo(BlockBackend *blk, DriveInfo *dinfo)
{
    assert(!blk->legacy_dinfo);
    return blk->legacy_dinfo = dinfo;
}

/*
 * Return the BlockBackend with DriveInfo @dinfo.
 * It must exist.
 */
BlockBackend *blk_by_legacy_dinfo(DriveInfo *dinfo)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &blk_backends, link) {
        if (blk->legacy_dinfo == dinfo) {
            return blk;
        }
    }
    abort();
}

/*
 * Hide @blk.
 * @blk must not have been hidden already.
 * Make attached BlockDriverState, if any, anonymous.
 * Once hidden, @blk is invisible to all functions that don't receive
 * it as argument.  For example, blk_by_name() won't return it.
 * Strictly for use by do_drive_del().
 * TODO get rid of it!
 */
void blk_hide_on_behalf_of_do_drive_del(BlockBackend *blk)
{
    QTAILQ_REMOVE(&blk_backends, blk, link);
    blk->name[0] = 0;
    if (blk->bs) {
        bdrv_make_anon(blk->bs);
    }
}

/*
 * Attach device model @dev to @blk.
 * Return 0 on success, -EBUSY when a device model is attached already.
 */
int blk_attach_dev(BlockBackend *blk, void *dev)
/* TODO change to DeviceState *dev when all users are qdevified */
{
    if (blk->dev) {
        return -EBUSY;
    }
    blk_ref(blk);
    blk->dev = dev;
    bdrv_iostatus_reset(blk->bs);

    /* We're expecting I/O from the device so bump up coroutine pool size */
    qemu_coroutine_adjust_pool_size(COROUTINE_POOL_RESERVATION);
    return 0;
}

/*
 * Attach device model @dev to @blk.
 * @blk must not have a device model attached already.
 * TODO qdevified devices don't use this, remove when devices are qdevified
 */
void blk_attach_dev_nofail(BlockBackend *blk, void *dev)
{
    if (blk_attach_dev(blk, dev) < 0) {
        abort();
    }
}

/*
 * Detach device model @dev from @blk.
 * @dev must be currently attached to @blk.
 */
void blk_detach_dev(BlockBackend *blk, void *dev)
/* TODO change to DeviceState *dev when all users are qdevified */
{
    assert(blk->dev == dev);
    blk->dev = NULL;
    blk->dev_ops = NULL;
    blk->dev_opaque = NULL;
    bdrv_set_guest_block_size(blk->bs, 512);
    qemu_coroutine_adjust_pool_size(-COROUTINE_POOL_RESERVATION);
    blk_unref(blk);
}

/*
 * Return the device model attached to @blk if any, else null.
 */
void *blk_get_attached_dev(BlockBackend *blk)
/* TODO change to return DeviceState * when all users are qdevified */
{
    return blk->dev;
}

/*
 * Set @blk's device model callbacks to @ops.
 * @opaque is the opaque argument to pass to the callbacks.
 * This is for use by device models.
 */
void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops,
                     void *opaque)
{
    blk->dev_ops = ops;
    blk->dev_opaque = opaque;
}

/*
 * Notify @blk's attached device model of media change.
 * If @load is true, notify of media load.
 * Else, notify of media eject.
 * Also send DEVICE_TRAY_MOVED events as appropriate.
 */
void blk_dev_change_media_cb(BlockBackend *blk, bool load)
{
    if (blk->dev_ops && blk->dev_ops->change_media_cb) {
        bool tray_was_closed = !blk_dev_is_tray_open(blk);

        blk->dev_ops->change_media_cb(blk->dev_opaque, load);
        if (tray_was_closed) {
            /* tray open */
            qapi_event_send_device_tray_moved(blk_name(blk),
                                              true, &error_abort);
        }
        if (load) {
            /* tray close */
            qapi_event_send_device_tray_moved(blk_name(blk),
                                              false, &error_abort);
        }
    }
}

/*
 * Does @blk's attached device model have removable media?
 * %true if no device model is attached.
 */
bool blk_dev_has_removable_media(BlockBackend *blk)
{
    return !blk->dev || (blk->dev_ops && blk->dev_ops->change_media_cb);
}

/*
 * Notify @blk's attached device model of a media eject request.
 * If @force is true, the medium is about to be yanked out forcefully.
 */
void blk_dev_eject_request(BlockBackend *blk, bool force)
{
    if (blk->dev_ops && blk->dev_ops->eject_request_cb) {
        blk->dev_ops->eject_request_cb(blk->dev_opaque, force);
    }
}

/*
 * Does @blk's attached device model have a tray, and is it open?
 */
bool blk_dev_is_tray_open(BlockBackend *blk)
{
    if (blk->dev_ops && blk->dev_ops->is_tray_open) {
        return blk->dev_ops->is_tray_open(blk->dev_opaque);
    }
    return false;
}

/*
 * Does @blk's attached device model have the medium locked?
 * %false if the device model has no such lock.
 */
bool blk_dev_is_medium_locked(BlockBackend *blk)
{
    if (blk->dev_ops && blk->dev_ops->is_medium_locked) {
        return blk->dev_ops->is_medium_locked(blk->dev_opaque);
    }
    return false;
}

/*
 * Notify @blk's attached device model of a backend size change.
 */
void blk_dev_resize_cb(BlockBackend *blk)
{
    if (blk->dev_ops && blk->dev_ops->resize_cb) {
        blk->dev_ops->resize_cb(blk->dev_opaque);
    }
}

void blk_iostatus_enable(BlockBackend *blk)
{
    bdrv_iostatus_enable(blk->bs);
}

int blk_read(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
             int nb_sectors)
{
    return bdrv_read(blk->bs, sector_num, buf, nb_sectors);
}

int blk_read_unthrottled(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
                         int nb_sectors)
{
    return bdrv_read_unthrottled(blk->bs, sector_num, buf, nb_sectors);
}

int blk_write(BlockBackend *blk, int64_t sector_num, const uint8_t *buf,
              int nb_sectors)
{
    return bdrv_write(blk->bs, sector_num, buf, nb_sectors);
}

BlockAIOCB *blk_aio_write_zeroes(BlockBackend *blk, int64_t sector_num,
                                 int nb_sectors, BdrvRequestFlags flags,
                                 BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_write_zeroes(blk->bs, sector_num, nb_sectors, flags,
                                 cb, opaque);
}

int blk_pread(BlockBackend *blk, int64_t offset, void *buf, int count)
{
    return bdrv_pread(blk->bs, offset, buf, count);
}

int blk_pwrite(BlockBackend *blk, int64_t offset, const void *buf, int count)
{
    return bdrv_pwrite(blk->bs, offset, buf, count);
}

int64_t blk_getlength(BlockBackend *blk)
{
    return bdrv_getlength(blk->bs);
}

void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr)
{
    bdrv_get_geometry(blk->bs, nb_sectors_ptr);
}

BlockAIOCB *blk_aio_readv(BlockBackend *blk, int64_t sector_num,
                          QEMUIOVector *iov, int nb_sectors,
                          BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_readv(blk->bs, sector_num, iov, nb_sectors, cb, opaque);
}

BlockAIOCB *blk_aio_writev(BlockBackend *blk, int64_t sector_num,
                           QEMUIOVector *iov, int nb_sectors,
                           BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_writev(blk->bs, sector_num, iov, nb_sectors, cb, opaque);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_flush(blk->bs, cb, opaque);
}

BlockAIOCB *blk_aio_discard(BlockBackend *blk,
                            int64_t sector_num, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_discard(blk->bs, sector_num, nb_sectors, cb, opaque);
}

void blk_aio_cancel(BlockAIOCB *acb)
{
    bdrv_aio_cancel(acb);
}

void blk_aio_cancel_async(BlockAIOCB *acb)
{
    bdrv_aio_cancel_async(acb);
}

int blk_aio_multiwrite(BlockBackend *blk, BlockRequest *reqs, int num_reqs)
{
    return bdrv_aio_multiwrite(blk->bs, reqs, num_reqs);
}

int blk_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    return bdrv_ioctl(blk->bs, req, buf);
}

BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_ioctl(blk->bs, req, buf, cb, opaque);
}

int blk_flush(BlockBackend *blk)
{
    return bdrv_flush(blk->bs);
}

int blk_flush_all(void)
{
    return bdrv_flush_all();
}

void blk_drain_all(void)
{
    bdrv_drain_all();
}

BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read)
{
    return bdrv_get_on_error(blk->bs, is_read);
}

BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error)
{
    return bdrv_get_error_action(blk->bs, is_read, error);
}

void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error)
{
    bdrv_error_action(blk->bs, action, is_read, error);
}

int blk_is_read_only(BlockBackend *blk)
{
    return bdrv_is_read_only(blk->bs);
}

int blk_is_sg(BlockBackend *blk)
{
    return bdrv_is_sg(blk->bs);
}

int blk_enable_write_cache(BlockBackend *blk)
{
    return bdrv_enable_write_cache(blk->bs);
}

void blk_set_enable_write_cache(BlockBackend *blk, bool wce)
{
    bdrv_set_enable_write_cache(blk->bs, wce);
}

int blk_is_inserted(BlockBackend *blk)
{
    return bdrv_is_inserted(blk->bs);
}

void blk_lock_medium(BlockBackend *blk, bool locked)
{
    bdrv_lock_medium(blk->bs, locked);
}

void blk_eject(BlockBackend *blk, bool eject_flag)
{
    bdrv_eject(blk->bs, eject_flag);
}

int blk_get_flags(BlockBackend *blk)
{
    return bdrv_get_flags(blk->bs);
}

void blk_set_guest_block_size(BlockBackend *blk, int align)
{
    bdrv_set_guest_block_size(blk->bs, align);
}

void *blk_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_blockalign(blk ? blk->bs : NULL, size);
}

bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp)
{
    return bdrv_op_is_blocked(blk->bs, op, errp);
}

void blk_op_unblock(BlockBackend *blk, BlockOpType op, Error *reason)
{
    bdrv_op_unblock(blk->bs, op, reason);
}

void blk_op_block_all(BlockBackend *blk, Error *reason)
{
    bdrv_op_block_all(blk->bs, reason);
}

void blk_op_unblock_all(BlockBackend *blk, Error *reason)
{
    bdrv_op_unblock_all(blk->bs, reason);
}

AioContext *blk_get_aio_context(BlockBackend *blk)
{
    return bdrv_get_aio_context(blk->bs);
}

void blk_set_aio_context(BlockBackend *blk, AioContext *new_context)
{
    bdrv_set_aio_context(blk->bs, new_context);
}

void blk_io_plug(BlockBackend *blk)
{
    bdrv_io_plug(blk->bs);
}

void blk_io_unplug(BlockBackend *blk)
{
    bdrv_io_unplug(blk->bs);
}

BlockAcctStats *blk_get_stats(BlockBackend *blk)
{
    return bdrv_get_stats(blk->bs);
}

void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque)
{
    return qemu_aio_get(aiocb_info, blk_bs(blk), cb, opaque);
}
