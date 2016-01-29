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

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/throttle-groups.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "qapi-event.h"

/* Number of coroutines to reserve per attached device model */
#define COROUTINE_POOL_RESERVATION 64

static AioContext *blk_aiocb_get_aio_context(BlockAIOCB *acb);

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

    /* the block size for which the guest device expects atomicity */
    int guest_block_size;

    /* If the BDS tree is removed, some of its options are stored here (which
     * can be used to restore those options in the new BDS on insert) */
    BlockBackendRootState root_state;

    /* I/O stats (display with "info blockstats"). */
    BlockAcctStats stats;

    BlockdevOnError on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;

    NotifierList remove_bs_notifiers, insert_bs_notifiers;
};

typedef struct BlockBackendAIOCB {
    BlockAIOCB common;
    QEMUBH *bh;
    BlockBackend *blk;
    int ret;
} BlockBackendAIOCB;

static const AIOCBInfo block_backend_aiocb_info = {
    .get_aio_context = blk_aiocb_get_aio_context,
    .aiocb_size = sizeof(BlockBackendAIOCB),
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
    notifier_list_init(&blk->remove_bs_notifiers);
    notifier_list_init(&blk->insert_bs_notifiers);
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

/*
 * Calls blk_new_with_bs() and then calls bdrv_open() on the BlockDriverState.
 *
 * Just as with bdrv_open(), after having called this function the reference to
 * @options belongs to the block layer (even on failure).
 *
 * TODO: Remove @filename and @flags; it should be possible to specify a whole
 * BDS tree just by specifying the @options QDict (or @reference,
 * alternatively). At the time of adding this function, this is not possible,
 * though, so callers of this function have to be able to specify @filename and
 * @flags.
 */
BlockBackend *blk_new_open(const char *name, const char *filename,
                           const char *reference, QDict *options, int flags,
                           Error **errp)
{
    BlockBackend *blk;
    int ret;

    blk = blk_new_with_bs(name, errp);
    if (!blk) {
        QDECREF(options);
        return NULL;
    }

    ret = bdrv_open(&blk->bs, filename, reference, options, flags, errp);
    if (ret < 0) {
        blk_unref(blk);
        return NULL;
    }

    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    assert(!blk->dev);
    if (blk->bs) {
        blk_remove_bs(blk);
    }
    assert(QLIST_EMPTY(&blk->remove_bs_notifiers.notifiers));
    assert(QLIST_EMPTY(&blk->insert_bs_notifiers.notifiers));
    if (blk->root_state.throttle_state) {
        g_free(blk->root_state.throttle_group);
        throttle_group_unref(blk->root_state.throttle_state);
    }
    /* Avoid double-remove after blk_hide_on_behalf_of_hmp_drive_del() */
    if (blk->name[0]) {
        QTAILQ_REMOVE(&blk_backends, blk, link);
    }
    g_free(blk->name);
    drive_info_del(blk->legacy_dinfo);
    block_acct_cleanup(&blk->stats);
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

int blk_get_refcnt(BlockBackend *blk)
{
    return blk ? blk->refcnt : 0;
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

void blk_remove_all_bs(void)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &blk_backends, link) {
        AioContext *ctx = blk_get_aio_context(blk);

        aio_context_acquire(ctx);
        if (blk->bs) {
            blk_remove_bs(blk);
        }
        aio_context_release(ctx);
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
 * blk_hide_on_behalf_of_hmp_drive_del().
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
 * Changes the BlockDriverState attached to @blk
 */
void blk_set_bs(BlockBackend *blk, BlockDriverState *bs)
{
    bdrv_ref(bs);

    if (blk->bs) {
        blk->bs->blk = NULL;
        bdrv_unref(blk->bs);
    }
    assert(bs->blk == NULL);

    blk->bs = bs;
    bs->blk = blk;
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
void blk_hide_on_behalf_of_hmp_drive_del(BlockBackend *blk)
{
    QTAILQ_REMOVE(&blk_backends, blk, link);
    blk->name[0] = 0;
    if (blk->bs) {
        bdrv_make_anon(blk->bs);
    }
}

/*
 * Disassociates the currently associated BlockDriverState from @blk.
 */
void blk_remove_bs(BlockBackend *blk)
{
    assert(blk->bs->blk == blk);

    notifier_list_notify(&blk->remove_bs_notifiers, blk);

    blk_update_root_state(blk);

    blk->bs->blk = NULL;
    bdrv_unref(blk->bs);
    blk->bs = NULL;
}

/*
 * Associates a new BlockDriverState with @blk.
 */
void blk_insert_bs(BlockBackend *blk, BlockDriverState *bs)
{
    assert(!blk->bs && !bs->blk);
    bdrv_ref(bs);
    blk->bs = bs;
    bs->blk = blk;

    notifier_list_notify(&blk->insert_bs_notifiers, blk);
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
    blk_iostatus_reset(blk);
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
    blk->guest_block_size = 512;
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
        bool tray_was_open, tray_is_open;

        tray_was_open = blk_dev_is_tray_open(blk);
        blk->dev_ops->change_media_cb(blk->dev_opaque, load);
        tray_is_open = blk_dev_is_tray_open(blk);

        if (tray_was_open != tray_is_open) {
            qapi_event_send_device_tray_moved(blk_name(blk), tray_is_open,
                                              &error_abort);
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
 * Does @blk's attached device model have a tray?
 */
bool blk_dev_has_tray(BlockBackend *blk)
{
    return blk->dev_ops && blk->dev_ops->is_tray_open;
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
    if (blk_dev_has_tray(blk)) {
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
    blk->iostatus_enabled = true;
    blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
}

/* The I/O status is only enabled if the drive explicitly
 * enables it _and_ the VM is configured to stop on errors */
bool blk_iostatus_is_enabled(const BlockBackend *blk)
{
    return (blk->iostatus_enabled &&
           (blk->on_write_error == BLOCKDEV_ON_ERROR_ENOSPC ||
            blk->on_write_error == BLOCKDEV_ON_ERROR_STOP   ||
            blk->on_read_error == BLOCKDEV_ON_ERROR_STOP));
}

BlockDeviceIoStatus blk_iostatus(const BlockBackend *blk)
{
    return blk->iostatus;
}

void blk_iostatus_disable(BlockBackend *blk)
{
    blk->iostatus_enabled = false;
}

void blk_iostatus_reset(BlockBackend *blk)
{
    if (blk_iostatus_is_enabled(blk)) {
        blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
        if (blk->bs && blk->bs->job) {
            block_job_iostatus_reset(blk->bs->job);
        }
    }
}

void blk_iostatus_set_err(BlockBackend *blk, int error)
{
    assert(blk_iostatus_is_enabled(blk));
    if (blk->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        blk->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                          BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

static int blk_check_byte_request(BlockBackend *blk, int64_t offset,
                                  size_t size)
{
    int64_t len;

    if (size > INT_MAX) {
        return -EIO;
    }

    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    len = blk_getlength(blk);
    if (len < 0) {
        return len;
    }

    if (offset < 0) {
        return -EIO;
    }

    if (offset > len || len - offset < size) {
        return -EIO;
    }

    return 0;
}

static int blk_check_request(BlockBackend *blk, int64_t sector_num,
                             int nb_sectors)
{
    if (sector_num < 0 || sector_num > INT64_MAX / BDRV_SECTOR_SIZE) {
        return -EIO;
    }

    if (nb_sectors < 0 || nb_sectors > INT_MAX / BDRV_SECTOR_SIZE) {
        return -EIO;
    }

    return blk_check_byte_request(blk, sector_num * BDRV_SECTOR_SIZE,
                                  nb_sectors * BDRV_SECTOR_SIZE);
}

int blk_read(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
             int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_read(blk->bs, sector_num, buf, nb_sectors);
}

int blk_read_unthrottled(BlockBackend *blk, int64_t sector_num, uint8_t *buf,
                         int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_read_unthrottled(blk->bs, sector_num, buf, nb_sectors);
}

int blk_write(BlockBackend *blk, int64_t sector_num, const uint8_t *buf,
              int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_write(blk->bs, sector_num, buf, nb_sectors);
}

int blk_write_zeroes(BlockBackend *blk, int64_t sector_num,
                     int nb_sectors, BdrvRequestFlags flags)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_write_zeroes(blk->bs, sector_num, nb_sectors, flags);
}

static void error_callback_bh(void *opaque)
{
    struct BlockBackendAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_unref(acb);
}

BlockAIOCB *blk_abort_aio_request(BlockBackend *blk,
                                  BlockCompletionFunc *cb,
                                  void *opaque, int ret)
{
    struct BlockBackendAIOCB *acb;
    QEMUBH *bh;

    acb = blk_aio_get(&block_backend_aiocb_info, blk, cb, opaque);
    acb->blk = blk;
    acb->ret = ret;

    bh = aio_bh_new(blk_get_aio_context(blk), error_callback_bh, acb);
    acb->bh = bh;
    qemu_bh_schedule(bh);

    return &acb->common;
}

BlockAIOCB *blk_aio_write_zeroes(BlockBackend *blk, int64_t sector_num,
                                 int nb_sectors, BdrvRequestFlags flags,
                                 BlockCompletionFunc *cb, void *opaque)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return blk_abort_aio_request(blk, cb, opaque, ret);
    }

    return bdrv_aio_write_zeroes(blk->bs, sector_num, nb_sectors, flags,
                                 cb, opaque);
}

int blk_pread(BlockBackend *blk, int64_t offset, void *buf, int count)
{
    int ret = blk_check_byte_request(blk, offset, count);
    if (ret < 0) {
        return ret;
    }

    return bdrv_pread(blk->bs, offset, buf, count);
}

int blk_pwrite(BlockBackend *blk, int64_t offset, const void *buf, int count)
{
    int ret = blk_check_byte_request(blk, offset, count);
    if (ret < 0) {
        return ret;
    }

    return bdrv_pwrite(blk->bs, offset, buf, count);
}

int64_t blk_getlength(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_getlength(blk->bs);
}

void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr)
{
    if (!blk->bs) {
        *nb_sectors_ptr = 0;
    } else {
        bdrv_get_geometry(blk->bs, nb_sectors_ptr);
    }
}

int64_t blk_nb_sectors(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_nb_sectors(blk->bs);
}

BlockAIOCB *blk_aio_readv(BlockBackend *blk, int64_t sector_num,
                          QEMUIOVector *iov, int nb_sectors,
                          BlockCompletionFunc *cb, void *opaque)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return blk_abort_aio_request(blk, cb, opaque, ret);
    }

    return bdrv_aio_readv(blk->bs, sector_num, iov, nb_sectors, cb, opaque);
}

BlockAIOCB *blk_aio_writev(BlockBackend *blk, int64_t sector_num,
                           QEMUIOVector *iov, int nb_sectors,
                           BlockCompletionFunc *cb, void *opaque)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return blk_abort_aio_request(blk, cb, opaque, ret);
    }

    return bdrv_aio_writev(blk->bs, sector_num, iov, nb_sectors, cb, opaque);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque)
{
    if (!blk_is_available(blk)) {
        return blk_abort_aio_request(blk, cb, opaque, -ENOMEDIUM);
    }

    return bdrv_aio_flush(blk->bs, cb, opaque);
}

BlockAIOCB *blk_aio_discard(BlockBackend *blk,
                            int64_t sector_num, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return blk_abort_aio_request(blk, cb, opaque, ret);
    }

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
    int i, ret;

    for (i = 0; i < num_reqs; i++) {
        ret = blk_check_request(blk, reqs[i].sector, reqs[i].nb_sectors);
        if (ret < 0) {
            return ret;
        }
    }

    return bdrv_aio_multiwrite(blk->bs, reqs, num_reqs);
}

int blk_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_ioctl(blk->bs, req, buf);
}

BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque)
{
    if (!blk_is_available(blk)) {
        return blk_abort_aio_request(blk, cb, opaque, -ENOMEDIUM);
    }

    return bdrv_aio_ioctl(blk->bs, req, buf, cb, opaque);
}

int blk_co_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_discard(blk->bs, sector_num, nb_sectors);
}

int blk_co_flush(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_flush(blk->bs);
}

int blk_flush(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_flush(blk->bs);
}

int blk_flush_all(void)
{
    return bdrv_flush_all();
}

void blk_drain(BlockBackend *blk)
{
    if (blk->bs) {
        bdrv_drain(blk->bs);
    }
}

void blk_drain_all(void)
{
    bdrv_drain_all();
}

void blk_set_on_error(BlockBackend *blk, BlockdevOnError on_read_error,
                      BlockdevOnError on_write_error)
{
    blk->on_read_error = on_read_error;
    blk->on_write_error = on_write_error;
}

BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read)
{
    return is_read ? blk->on_read_error : blk->on_write_error;
}

BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error)
{
    BlockdevOnError on_err = blk_get_on_error(blk, is_read);

    switch (on_err) {
    case BLOCKDEV_ON_ERROR_ENOSPC:
        return (error == ENOSPC) ?
               BLOCK_ERROR_ACTION_STOP : BLOCK_ERROR_ACTION_REPORT;
    case BLOCKDEV_ON_ERROR_STOP:
        return BLOCK_ERROR_ACTION_STOP;
    case BLOCKDEV_ON_ERROR_REPORT:
        return BLOCK_ERROR_ACTION_REPORT;
    case BLOCKDEV_ON_ERROR_IGNORE:
        return BLOCK_ERROR_ACTION_IGNORE;
    default:
        abort();
    }
}

static void send_qmp_error_event(BlockBackend *blk,
                                 BlockErrorAction action,
                                 bool is_read, int error)
{
    IoOperationType optype;

    optype = is_read ? IO_OPERATION_TYPE_READ : IO_OPERATION_TYPE_WRITE;
    qapi_event_send_block_io_error(blk_name(blk), optype, action,
                                   blk_iostatus_is_enabled(blk),
                                   error == ENOSPC, strerror(error),
                                   &error_abort);
}

/* This is done by device models because, while the block layer knows
 * about the error, it does not know whether an operation comes from
 * the device or the block layer (from a job, for example).
 */
void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error)
{
    assert(error >= 0);

    if (action == BLOCK_ERROR_ACTION_STOP) {
        /* First set the iostatus, so that "info block" returns an iostatus
         * that matches the events raised so far (an additional error iostatus
         * is fine, but not a lost one).
         */
        blk_iostatus_set_err(blk, error);

        /* Then raise the request to stop the VM and the event.
         * qemu_system_vmstop_request_prepare has two effects.  First,
         * it ensures that the STOP event always comes after the
         * BLOCK_IO_ERROR event.  Second, it ensures that even if management
         * can observe the STOP event and do a "cont" before the STOP
         * event is issued, the VM will not stop.  In this case, vm_start()
         * also ensures that the STOP/RESUME pair of events is emitted.
         */
        qemu_system_vmstop_request_prepare();
        send_qmp_error_event(blk, action, is_read, error);
        qemu_system_vmstop_request(RUN_STATE_IO_ERROR);
    } else {
        send_qmp_error_event(blk, action, is_read, error);
    }
}

int blk_is_read_only(BlockBackend *blk)
{
    if (blk->bs) {
        return bdrv_is_read_only(blk->bs);
    } else {
        return blk->root_state.read_only;
    }
}

int blk_is_sg(BlockBackend *blk)
{
    if (!blk->bs) {
        return 0;
    }

    return bdrv_is_sg(blk->bs);
}

int blk_enable_write_cache(BlockBackend *blk)
{
    if (blk->bs) {
        return bdrv_enable_write_cache(blk->bs);
    } else {
        return !!(blk->root_state.open_flags & BDRV_O_CACHE_WB);
    }
}

void blk_set_enable_write_cache(BlockBackend *blk, bool wce)
{
    if (blk->bs) {
        bdrv_set_enable_write_cache(blk->bs, wce);
    } else {
        if (wce) {
            blk->root_state.open_flags |= BDRV_O_CACHE_WB;
        } else {
            blk->root_state.open_flags &= ~BDRV_O_CACHE_WB;
        }
    }
}

void blk_invalidate_cache(BlockBackend *blk, Error **errp)
{
    if (!blk->bs) {
        error_setg(errp, "Device '%s' has no medium", blk->name);
        return;
    }

    bdrv_invalidate_cache(blk->bs, errp);
}

bool blk_is_inserted(BlockBackend *blk)
{
    return blk->bs && bdrv_is_inserted(blk->bs);
}

bool blk_is_available(BlockBackend *blk)
{
    return blk_is_inserted(blk) && !blk_dev_is_tray_open(blk);
}

void blk_lock_medium(BlockBackend *blk, bool locked)
{
    if (blk->bs) {
        bdrv_lock_medium(blk->bs, locked);
    }
}

void blk_eject(BlockBackend *blk, bool eject_flag)
{
    if (blk->bs) {
        bdrv_eject(blk->bs, eject_flag);
    }
}

int blk_get_flags(BlockBackend *blk)
{
    if (blk->bs) {
        return bdrv_get_flags(blk->bs);
    } else {
        return blk->root_state.open_flags;
    }
}

int blk_get_max_transfer_length(BlockBackend *blk)
{
    if (blk->bs) {
        return blk->bs->bl.max_transfer_length;
    } else {
        return 0;
    }
}

int blk_get_max_iov(BlockBackend *blk)
{
    return blk->bs->bl.max_iov;
}

void blk_set_guest_block_size(BlockBackend *blk, int align)
{
    blk->guest_block_size = align;
}

void *blk_try_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_try_blockalign(blk ? blk->bs : NULL, size);
}

void *blk_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_blockalign(blk ? blk->bs : NULL, size);
}

bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp)
{
    if (!blk->bs) {
        return false;
    }

    return bdrv_op_is_blocked(blk->bs, op, errp);
}

void blk_op_unblock(BlockBackend *blk, BlockOpType op, Error *reason)
{
    if (blk->bs) {
        bdrv_op_unblock(blk->bs, op, reason);
    }
}

void blk_op_block_all(BlockBackend *blk, Error *reason)
{
    if (blk->bs) {
        bdrv_op_block_all(blk->bs, reason);
    }
}

void blk_op_unblock_all(BlockBackend *blk, Error *reason)
{
    if (blk->bs) {
        bdrv_op_unblock_all(blk->bs, reason);
    }
}

AioContext *blk_get_aio_context(BlockBackend *blk)
{
    if (blk->bs) {
        return bdrv_get_aio_context(blk->bs);
    } else {
        return qemu_get_aio_context();
    }
}

static AioContext *blk_aiocb_get_aio_context(BlockAIOCB *acb)
{
    BlockBackendAIOCB *blk_acb = DO_UPCAST(BlockBackendAIOCB, common, acb);
    return blk_get_aio_context(blk_acb->blk);
}

void blk_set_aio_context(BlockBackend *blk, AioContext *new_context)
{
    if (blk->bs) {
        bdrv_set_aio_context(blk->bs, new_context);
    }
}

void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque)
{
    if (blk->bs) {
        bdrv_add_aio_context_notifier(blk->bs, attached_aio_context,
                                      detach_aio_context, opaque);
    }
}

void blk_remove_aio_context_notifier(BlockBackend *blk,
                                     void (*attached_aio_context)(AioContext *,
                                                                  void *),
                                     void (*detach_aio_context)(void *),
                                     void *opaque)
{
    if (blk->bs) {
        bdrv_remove_aio_context_notifier(blk->bs, attached_aio_context,
                                         detach_aio_context, opaque);
    }
}

void blk_add_remove_bs_notifier(BlockBackend *blk, Notifier *notify)
{
    notifier_list_add(&blk->remove_bs_notifiers, notify);
}

void blk_add_insert_bs_notifier(BlockBackend *blk, Notifier *notify)
{
    notifier_list_add(&blk->insert_bs_notifiers, notify);
}

void blk_io_plug(BlockBackend *blk)
{
    if (blk->bs) {
        bdrv_io_plug(blk->bs);
    }
}

void blk_io_unplug(BlockBackend *blk)
{
    if (blk->bs) {
        bdrv_io_unplug(blk->bs);
    }
}

BlockAcctStats *blk_get_stats(BlockBackend *blk)
{
    return &blk->stats;
}

void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque)
{
    return qemu_aio_get(aiocb_info, blk_bs(blk), cb, opaque);
}

int coroutine_fn blk_co_write_zeroes(BlockBackend *blk, int64_t sector_num,
                                     int nb_sectors, BdrvRequestFlags flags)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_write_zeroes(blk->bs, sector_num, nb_sectors, flags);
}

int blk_write_compressed(BlockBackend *blk, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_write_compressed(blk->bs, sector_num, buf, nb_sectors);
}

int blk_truncate(BlockBackend *blk, int64_t offset)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_truncate(blk->bs, offset);
}

int blk_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_discard(blk->bs, sector_num, nb_sectors);
}

int blk_save_vmstate(BlockBackend *blk, const uint8_t *buf,
                     int64_t pos, int size)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_save_vmstate(blk->bs, buf, pos, size);
}

int blk_load_vmstate(BlockBackend *blk, uint8_t *buf, int64_t pos, int size)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_load_vmstate(blk->bs, buf, pos, size);
}

int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_probe_blocksizes(blk->bs, bsz);
}

int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_probe_geometry(blk->bs, geo);
}

/*
 * Updates the BlockBackendRootState object with data from the currently
 * attached BlockDriverState.
 */
void blk_update_root_state(BlockBackend *blk)
{
    assert(blk->bs);

    blk->root_state.open_flags    = blk->bs->open_flags;
    blk->root_state.read_only     = blk->bs->read_only;
    blk->root_state.detect_zeroes = blk->bs->detect_zeroes;

    if (blk->root_state.throttle_group) {
        g_free(blk->root_state.throttle_group);
        throttle_group_unref(blk->root_state.throttle_state);
    }
    if (blk->bs->throttle_state) {
        const char *name = throttle_group_get_name(blk->bs);
        blk->root_state.throttle_group = g_strdup(name);
        blk->root_state.throttle_state = throttle_group_incref(name);
    } else {
        blk->root_state.throttle_group = NULL;
        blk->root_state.throttle_state = NULL;
    }
}

/*
 * Applies the information in the root state to the given BlockDriverState. This
 * does not include the flags which have to be specified for bdrv_open(), use
 * blk_get_open_flags_from_root_state() to inquire them.
 */
void blk_apply_root_state(BlockBackend *blk, BlockDriverState *bs)
{
    bs->detect_zeroes = blk->root_state.detect_zeroes;
    if (blk->root_state.throttle_group) {
        bdrv_io_limits_enable(bs, blk->root_state.throttle_group);
    }
}

/*
 * Returns the flags to be used for bdrv_open() of a BlockDriverState which is
 * supposed to inherit the root state.
 */
int blk_get_open_flags_from_root_state(BlockBackend *blk)
{
    int bs_flags;

    bs_flags = blk->root_state.read_only ? 0 : BDRV_O_RDWR;
    bs_flags |= blk->root_state.open_flags & ~BDRV_O_RDWR;

    return bs_flags;
}

BlockBackendRootState *blk_get_root_state(BlockBackend *blk)
{
    return &blk->root_state;
}
