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

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/throttle-groups.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "qapi-event.h"
#include "qemu/id.h"

/* Number of coroutines to reserve per attached device model */
#define COROUTINE_POOL_RESERVATION 64

#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */

static AioContext *blk_aiocb_get_aio_context(BlockAIOCB *acb);

struct BlockBackend {
    char *name;
    int refcnt;
    BdrvChild *root;
    DriveInfo *legacy_dinfo;    /* null unless created by drive_new() */
    QTAILQ_ENTRY(BlockBackend) link;         /* for block_backends */
    QTAILQ_ENTRY(BlockBackend) monitor_link; /* for monitor_block_backends */
    BlockBackendPublic public;

    void *dev;                  /* attached device model, if any */
    /* TODO change to DeviceState when all users are qdevified */
    const BlockDevOps *dev_ops;
    void *dev_opaque;

    /* the block size for which the guest device expects atomicity */
    int guest_block_size;

    /* If the BDS tree is removed, some of its options are stored here (which
     * can be used to restore those options in the new BDS on insert) */
    BlockBackendRootState root_state;

    bool enable_write_cache;

    /* I/O stats (display with "info blockstats"). */
    BlockAcctStats stats;

    BlockdevOnError on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;

    bool allow_write_beyond_eof;

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
static BlockBackend *bdrv_first_blk(BlockDriverState *bs);

/* All BlockBackends */
static QTAILQ_HEAD(, BlockBackend) block_backends =
    QTAILQ_HEAD_INITIALIZER(block_backends);

/* All BlockBackends referenced by the monitor and which are iterated through by
 * blk_next() */
static QTAILQ_HEAD(, BlockBackend) monitor_block_backends =
    QTAILQ_HEAD_INITIALIZER(monitor_block_backends);

static void blk_root_inherit_options(int *child_flags, QDict *child_options,
                                     int parent_flags, QDict *parent_options)
{
    /* We're not supposed to call this function for root nodes */
    abort();
}
static void blk_root_drained_begin(BdrvChild *child);
static void blk_root_drained_end(BdrvChild *child);

static void blk_root_change_media(BdrvChild *child, bool load);
static void blk_root_resize(BdrvChild *child);

static const char *blk_root_get_name(BdrvChild *child)
{
    return blk_name(child->opaque);
}

static const BdrvChildRole child_root = {
    .inherit_options    = blk_root_inherit_options,

    .change_media       = blk_root_change_media,
    .resize             = blk_root_resize,
    .get_name           = blk_root_get_name,

    .drained_begin      = blk_root_drained_begin,
    .drained_end        = blk_root_drained_end,
};

/*
 * Create a new BlockBackend with a reference count of one.
 * Store an error through @errp on failure, unless it's null.
 * Return the new BlockBackend on success, null on failure.
 */
BlockBackend *blk_new(void)
{
    BlockBackend *blk;

    blk = g_new0(BlockBackend, 1);
    blk->refcnt = 1;
    blk_set_enable_write_cache(blk, true);

    qemu_co_queue_init(&blk->public.throttled_reqs[0]);
    qemu_co_queue_init(&blk->public.throttled_reqs[1]);

    notifier_list_init(&blk->remove_bs_notifiers);
    notifier_list_init(&blk->insert_bs_notifiers);

    QTAILQ_INSERT_TAIL(&block_backends, blk, link);
    return blk;
}

/*
 * Creates a new BlockBackend, opens a new BlockDriverState, and connects both.
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
BlockBackend *blk_new_open(const char *filename, const char *reference,
                           QDict *options, int flags, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    blk = blk_new();
    bs = bdrv_open(filename, reference, options, flags, errp);
    if (!bs) {
        blk_unref(blk);
        return NULL;
    }

    blk->root = bdrv_root_attach_child(bs, "root", &child_root, blk);

    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    assert(!blk->name);
    assert(!blk->dev);
    if (blk->root) {
        blk_remove_bs(blk);
    }
    assert(QLIST_EMPTY(&blk->remove_bs_notifiers.notifiers));
    assert(QLIST_EMPTY(&blk->insert_bs_notifiers.notifiers));
    QTAILQ_REMOVE(&block_backends, blk, link);
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

/*
 * Behaves similarly to blk_next() but iterates over all BlockBackends, even the
 * ones which are hidden (i.e. are not referenced by the monitor).
 */
static BlockBackend *blk_all_next(BlockBackend *blk)
{
    return blk ? QTAILQ_NEXT(blk, link)
               : QTAILQ_FIRST(&block_backends);
}

void blk_remove_all_bs(void)
{
    BlockBackend *blk = NULL;

    while ((blk = blk_all_next(blk)) != NULL) {
        AioContext *ctx = blk_get_aio_context(blk);

        aio_context_acquire(ctx);
        if (blk->root) {
            blk_remove_bs(blk);
        }
        aio_context_release(ctx);
    }
}

/*
 * Return the monitor-owned BlockBackend after @blk.
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
    return blk ? QTAILQ_NEXT(blk, monitor_link)
               : QTAILQ_FIRST(&monitor_block_backends);
}

/* Iterates over all top-level BlockDriverStates, i.e. BDSs that are owned by
 * the monitor or attached to a BlockBackend */
BlockDriverState *bdrv_next(BdrvNextIterator *it)
{
    BlockDriverState *bs;

    /* First, return all root nodes of BlockBackends. In order to avoid
     * returning a BDS twice when multiple BBs refer to it, we only return it
     * if the BB is the first one in the parent list of the BDS. */
    if (it->phase == BDRV_NEXT_BACKEND_ROOTS) {
        do {
            it->blk = blk_all_next(it->blk);
            bs = it->blk ? blk_bs(it->blk) : NULL;
        } while (it->blk && (bs == NULL || bdrv_first_blk(bs) != it->blk));

        if (bs) {
            return bs;
        }
        it->phase = BDRV_NEXT_MONITOR_OWNED;
    }

    /* Then return the monitor-owned BDSes without a BB attached. Ignore all
     * BDSes that are attached to a BlockBackend here; they have been handled
     * by the above block already */
    do {
        it->bs = bdrv_next_monitor_owned(it->bs);
        bs = it->bs;
    } while (bs && bdrv_has_blk(bs));

    return bs;
}

BlockDriverState *bdrv_first(BdrvNextIterator *it)
{
    *it = (BdrvNextIterator) {
        .phase = BDRV_NEXT_BACKEND_ROOTS,
    };

    return bdrv_next(it);
}

/*
 * Add a BlockBackend into the list of backends referenced by the monitor, with
 * the given @name acting as the handle for the monitor.
 * Strictly for use by blockdev.c.
 *
 * @name must not be null or empty.
 *
 * Returns true on success and false on failure. In the latter case, an Error
 * object is returned through @errp.
 */
bool monitor_add_blk(BlockBackend *blk, const char *name, Error **errp)
{
    assert(!blk->name);
    assert(name && name[0]);

    if (!id_wellformed(name)) {
        error_setg(errp, "Invalid device name");
        return false;
    }
    if (blk_by_name(name)) {
        error_setg(errp, "Device with id '%s' already exists", name);
        return false;
    }
    if (bdrv_find_node(name)) {
        error_setg(errp,
                   "Device name '%s' conflicts with an existing node name",
                   name);
        return false;
    }

    blk->name = g_strdup(name);
    QTAILQ_INSERT_TAIL(&monitor_block_backends, blk, monitor_link);
    return true;
}

/*
 * Remove a BlockBackend from the list of backends referenced by the monitor.
 * Strictly for use by blockdev.c.
 */
void monitor_remove_blk(BlockBackend *blk)
{
    if (!blk->name) {
        return;
    }

    QTAILQ_REMOVE(&monitor_block_backends, blk, monitor_link);
    g_free(blk->name);
    blk->name = NULL;
}

/*
 * Return @blk's name, a non-null string.
 * Returns an empty string iff @blk is not referenced by the monitor.
 */
const char *blk_name(BlockBackend *blk)
{
    return blk->name ?: "";
}

/*
 * Return the BlockBackend with name @name if it exists, else null.
 * @name must not be null.
 */
BlockBackend *blk_by_name(const char *name)
{
    BlockBackend *blk = NULL;

    assert(name);
    while ((blk = blk_next(blk)) != NULL) {
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
    return blk->root ? blk->root->bs : NULL;
}

static BlockBackend *bdrv_first_blk(BlockDriverState *bs)
{
    BdrvChild *child;
    QLIST_FOREACH(child, &bs->parents, next_parent) {
        if (child->role == &child_root) {
            return child->opaque;
        }
    }

    return NULL;
}

/*
 * Returns true if @bs has an associated BlockBackend.
 */
bool bdrv_has_blk(BlockDriverState *bs)
{
    return bdrv_first_blk(bs) != NULL;
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
    BlockBackend *blk = NULL;

    while ((blk = blk_next(blk)) != NULL) {
        if (blk->legacy_dinfo == dinfo) {
            return blk;
        }
    }
    abort();
}

/*
 * Returns a pointer to the publicly accessible fields of @blk.
 */
BlockBackendPublic *blk_get_public(BlockBackend *blk)
{
    return &blk->public;
}

/*
 * Returns a BlockBackend given the associated @public fields.
 */
BlockBackend *blk_by_public(BlockBackendPublic *public)
{
    return container_of(public, BlockBackend, public);
}

/*
 * Disassociates the currently associated BlockDriverState from @blk.
 */
void blk_remove_bs(BlockBackend *blk)
{
    notifier_list_notify(&blk->remove_bs_notifiers, blk);
    if (blk->public.throttle_state) {
        throttle_timers_detach_aio_context(&blk->public.throttle_timers);
    }

    blk_update_root_state(blk);

    bdrv_root_unref_child(blk->root);
    blk->root = NULL;
}

/*
 * Associates a new BlockDriverState with @blk.
 */
void blk_insert_bs(BlockBackend *blk, BlockDriverState *bs)
{
    bdrv_ref(bs);
    blk->root = bdrv_root_attach_child(bs, "root", &child_root, blk);

    notifier_list_notify(&blk->insert_bs_notifiers, blk);
    if (blk->public.throttle_state) {
        throttle_timers_attach_aio_context(
            &blk->public.throttle_timers, bdrv_get_aio_context(bs));
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

static void blk_root_change_media(BdrvChild *child, bool load)
{
    blk_dev_change_media_cb(child->opaque, load);
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
static void blk_root_resize(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;

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
        BlockDriverState *bs = blk_bs(blk);
        blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
        if (bs && bs->job) {
            block_job_iostatus_reset(bs->job);
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

void blk_set_allow_write_beyond_eof(BlockBackend *blk, bool allow)
{
    blk->allow_write_beyond_eof = allow;
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

    if (offset < 0) {
        return -EIO;
    }

    if (!blk->allow_write_beyond_eof) {
        len = blk_getlength(blk);
        if (len < 0) {
            return len;
        }

        if (offset > len || len - offset < size) {
            return -EIO;
        }
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

static int coroutine_fn blk_co_preadv(BlockBackend *blk, int64_t offset,
                                      unsigned int bytes, QEMUIOVector *qiov,
                                      BdrvRequestFlags flags)
{
    int ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /* throttling disk I/O */
    if (blk->public.throttle_state) {
        throttle_group_co_io_limits_intercept(blk, bytes, false);
    }

    return bdrv_co_preadv(blk_bs(blk), offset, bytes, qiov, flags);
}

static int coroutine_fn blk_co_pwritev(BlockBackend *blk, int64_t offset,
                                      unsigned int bytes, QEMUIOVector *qiov,
                                      BdrvRequestFlags flags)
{
    int ret;

    ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /* throttling disk I/O */
    if (blk->public.throttle_state) {
        throttle_group_co_io_limits_intercept(blk, bytes, true);
    }

    if (!blk->enable_write_cache) {
        flags |= BDRV_REQ_FUA;
    }

    return bdrv_co_pwritev(blk_bs(blk), offset, bytes, qiov, flags);
}

typedef struct BlkRwCo {
    BlockBackend *blk;
    int64_t offset;
    QEMUIOVector *qiov;
    int ret;
    BdrvRequestFlags flags;
} BlkRwCo;

static void blk_read_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;

    rwco->ret = blk_co_preadv(rwco->blk, rwco->offset, rwco->qiov->size,
                              rwco->qiov, rwco->flags);
}

static void blk_write_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;

    rwco->ret = blk_co_pwritev(rwco->blk, rwco->offset, rwco->qiov->size,
                               rwco->qiov, rwco->flags);
}

static int blk_prw(BlockBackend *blk, int64_t offset, uint8_t *buf,
                   int64_t bytes, CoroutineEntry co_entry,
                   BdrvRequestFlags flags)
{
    AioContext *aio_context;
    QEMUIOVector qiov;
    struct iovec iov;
    Coroutine *co;
    BlkRwCo rwco;

    iov = (struct iovec) {
        .iov_base = buf,
        .iov_len = bytes,
    };
    qemu_iovec_init_external(&qiov, &iov, 1);

    rwco = (BlkRwCo) {
        .blk    = blk,
        .offset = offset,
        .qiov   = &qiov,
        .flags  = flags,
        .ret    = NOT_DONE,
    };

    co = qemu_coroutine_create(co_entry);
    qemu_coroutine_enter(co, &rwco);

    aio_context = blk_get_aio_context(blk);
    while (rwco.ret == NOT_DONE) {
        aio_poll(aio_context, true);
    }

    return rwco.ret;
}

int blk_pread_unthrottled(BlockBackend *blk, int64_t offset, uint8_t *buf,
                          int count)
{
    int ret;

    ret = blk_check_byte_request(blk, offset, count);
    if (ret < 0) {
        return ret;
    }

    blk_root_drained_begin(blk->root);
    ret = blk_pread(blk, offset, buf, count);
    blk_root_drained_end(blk->root);
    return ret;
}

int blk_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                      int count, BdrvRequestFlags flags)
{
    return blk_prw(blk, offset, NULL, count, blk_write_entry,
                   flags | BDRV_REQ_ZERO_WRITE);
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

typedef struct BlkAioEmAIOCB {
    BlockAIOCB common;
    BlkRwCo rwco;
    int bytes;
    bool has_returned;
    QEMUBH* bh;
} BlkAioEmAIOCB;

static const AIOCBInfo blk_aio_em_aiocb_info = {
    .aiocb_size         = sizeof(BlkAioEmAIOCB),
};

static void blk_aio_complete(BlkAioEmAIOCB *acb)
{
    if (acb->bh) {
        assert(acb->has_returned);
        qemu_bh_delete(acb->bh);
    }
    if (acb->has_returned) {
        acb->common.cb(acb->common.opaque, acb->rwco.ret);
        qemu_aio_unref(acb);
    }
}

static void blk_aio_complete_bh(void *opaque)
{
    blk_aio_complete(opaque);
}

static BlockAIOCB *blk_aio_prwv(BlockBackend *blk, int64_t offset, int bytes,
                                QEMUIOVector *qiov, CoroutineEntry co_entry,
                                BdrvRequestFlags flags,
                                BlockCompletionFunc *cb, void *opaque)
{
    BlkAioEmAIOCB *acb;
    Coroutine *co;

    acb = blk_aio_get(&blk_aio_em_aiocb_info, blk, cb, opaque);
    acb->rwco = (BlkRwCo) {
        .blk    = blk,
        .offset = offset,
        .qiov   = qiov,
        .flags  = flags,
        .ret    = NOT_DONE,
    };
    acb->bytes = bytes;
    acb->bh = NULL;
    acb->has_returned = false;

    co = qemu_coroutine_create(co_entry);
    qemu_coroutine_enter(co, acb);

    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        acb->bh = aio_bh_new(blk_get_aio_context(blk), blk_aio_complete_bh, acb);
        qemu_bh_schedule(acb->bh);
    }

    return &acb->common;
}

static void blk_aio_read_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    assert(rwco->qiov->size == acb->bytes);
    rwco->ret = blk_co_preadv(rwco->blk, rwco->offset, acb->bytes,
                              rwco->qiov, rwco->flags);
    blk_aio_complete(acb);
}

static void blk_aio_write_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    assert(!rwco->qiov || rwco->qiov->size == acb->bytes);
    rwco->ret = blk_co_pwritev(rwco->blk, rwco->offset, acb->bytes,
                               rwco->qiov, rwco->flags);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                                  int count, BdrvRequestFlags flags,
                                  BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, offset, count, NULL, blk_aio_write_entry,
                        flags | BDRV_REQ_ZERO_WRITE, cb, opaque);
}

int blk_pread(BlockBackend *blk, int64_t offset, void *buf, int count)
{
    int ret = blk_prw(blk, offset, buf, count, blk_read_entry, 0);
    if (ret < 0) {
        return ret;
    }
    return count;
}

int blk_pwrite(BlockBackend *blk, int64_t offset, const void *buf, int count,
               BdrvRequestFlags flags)
{
    int ret = blk_prw(blk, offset, (void *) buf, count, blk_write_entry,
                      flags);
    if (ret < 0) {
        return ret;
    }
    return count;
}

int64_t blk_getlength(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_getlength(blk_bs(blk));
}

void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr)
{
    if (!blk_bs(blk)) {
        *nb_sectors_ptr = 0;
    } else {
        bdrv_get_geometry(blk_bs(blk), nb_sectors_ptr);
    }
}

int64_t blk_nb_sectors(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_nb_sectors(blk_bs(blk));
}

BlockAIOCB *blk_aio_preadv(BlockBackend *blk, int64_t offset,
                           QEMUIOVector *qiov, BdrvRequestFlags flags,
                           BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, offset, qiov->size, qiov,
                        blk_aio_read_entry, flags, cb, opaque);
}

BlockAIOCB *blk_aio_pwritev(BlockBackend *blk, int64_t offset,
                            QEMUIOVector *qiov, BdrvRequestFlags flags,
                            BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, offset, qiov->size, qiov,
                        blk_aio_write_entry, flags, cb, opaque);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque)
{
    if (!blk_is_available(blk)) {
        return blk_abort_aio_request(blk, cb, opaque, -ENOMEDIUM);
    }

    return bdrv_aio_flush(blk_bs(blk), cb, opaque);
}

BlockAIOCB *blk_aio_discard(BlockBackend *blk,
                            int64_t sector_num, int nb_sectors,
                            BlockCompletionFunc *cb, void *opaque)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return blk_abort_aio_request(blk, cb, opaque, ret);
    }

    return bdrv_aio_discard(blk_bs(blk), sector_num, nb_sectors, cb, opaque);
}

void blk_aio_cancel(BlockAIOCB *acb)
{
    bdrv_aio_cancel(acb);
}

void blk_aio_cancel_async(BlockAIOCB *acb)
{
    bdrv_aio_cancel_async(acb);
}

int blk_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_ioctl(blk_bs(blk), req, buf);
}

BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque)
{
    if (!blk_is_available(blk)) {
        return blk_abort_aio_request(blk, cb, opaque, -ENOMEDIUM);
    }

    return bdrv_aio_ioctl(blk_bs(blk), req, buf, cb, opaque);
}

int blk_co_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_discard(blk_bs(blk), sector_num, nb_sectors);
}

int blk_co_flush(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_flush(blk_bs(blk));
}

int blk_flush(BlockBackend *blk)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_flush(blk_bs(blk));
}

void blk_drain(BlockBackend *blk)
{
    if (blk_bs(blk)) {
        bdrv_drain(blk_bs(blk));
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
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        return bdrv_is_read_only(bs);
    } else {
        return blk->root_state.read_only;
    }
}

int blk_is_sg(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (!bs) {
        return 0;
    }

    return bdrv_is_sg(bs);
}

int blk_enable_write_cache(BlockBackend *blk)
{
    return blk->enable_write_cache;
}

void blk_set_enable_write_cache(BlockBackend *blk, bool wce)
{
    blk->enable_write_cache = wce;
}

void blk_invalidate_cache(BlockBackend *blk, Error **errp)
{
    BlockDriverState *bs = blk_bs(blk);

    if (!bs) {
        error_setg(errp, "Device '%s' has no medium", blk->name);
        return;
    }

    bdrv_invalidate_cache(bs, errp);
}

bool blk_is_inserted(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    return bs && bdrv_is_inserted(bs);
}

bool blk_is_available(BlockBackend *blk)
{
    return blk_is_inserted(blk) && !blk_dev_is_tray_open(blk);
}

void blk_lock_medium(BlockBackend *blk, bool locked)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_lock_medium(bs, locked);
    }
}

void blk_eject(BlockBackend *blk, bool eject_flag)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_eject(bs, eject_flag);
    }
}

int blk_get_flags(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        return bdrv_get_flags(bs);
    } else {
        return blk->root_state.open_flags;
    }
}

int blk_get_max_transfer_length(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        return bs->bl.max_transfer_length;
    } else {
        return 0;
    }
}

int blk_get_max_iov(BlockBackend *blk)
{
    return blk->root->bs->bl.max_iov;
}

void blk_set_guest_block_size(BlockBackend *blk, int align)
{
    blk->guest_block_size = align;
}

void *blk_try_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_try_blockalign(blk ? blk_bs(blk) : NULL, size);
}

void *blk_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_blockalign(blk ? blk_bs(blk) : NULL, size);
}

bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp)
{
    BlockDriverState *bs = blk_bs(blk);

    if (!bs) {
        return false;
    }

    return bdrv_op_is_blocked(bs, op, errp);
}

void blk_op_unblock(BlockBackend *blk, BlockOpType op, Error *reason)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_op_unblock(bs, op, reason);
    }
}

void blk_op_block_all(BlockBackend *blk, Error *reason)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_op_block_all(bs, reason);
    }
}

void blk_op_unblock_all(BlockBackend *blk, Error *reason)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_op_unblock_all(bs, reason);
    }
}

AioContext *blk_get_aio_context(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        return bdrv_get_aio_context(bs);
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
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        if (blk->public.throttle_state) {
            throttle_timers_detach_aio_context(&blk->public.throttle_timers);
        }
        bdrv_set_aio_context(bs, new_context);
        if (blk->public.throttle_state) {
            throttle_timers_attach_aio_context(&blk->public.throttle_timers,
                                               new_context);
        }
    }
}

void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_add_aio_context_notifier(bs, attached_aio_context,
                                      detach_aio_context, opaque);
    }
}

void blk_remove_aio_context_notifier(BlockBackend *blk,
                                     void (*attached_aio_context)(AioContext *,
                                                                  void *),
                                     void (*detach_aio_context)(void *),
                                     void *opaque)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_remove_aio_context_notifier(bs, attached_aio_context,
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
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_io_plug(bs);
    }
}

void blk_io_unplug(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_io_unplug(bs);
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

int coroutine_fn blk_co_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                                      int count, BdrvRequestFlags flags)
{
    return blk_co_pwritev(blk, offset, count, NULL,
                          flags | BDRV_REQ_ZERO_WRITE);
}

int blk_write_compressed(BlockBackend *blk, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_write_compressed(blk_bs(blk), sector_num, buf, nb_sectors);
}

int blk_truncate(BlockBackend *blk, int64_t offset)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_truncate(blk_bs(blk), offset);
}

int blk_discard(BlockBackend *blk, int64_t sector_num, int nb_sectors)
{
    int ret = blk_check_request(blk, sector_num, nb_sectors);
    if (ret < 0) {
        return ret;
    }

    return bdrv_discard(blk_bs(blk), sector_num, nb_sectors);
}

int blk_save_vmstate(BlockBackend *blk, const uint8_t *buf,
                     int64_t pos, int size)
{
    int ret;

    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    ret = bdrv_save_vmstate(blk_bs(blk), buf, pos, size);
    if (ret < 0) {
        return ret;
    }

    if (ret == size && !blk->enable_write_cache) {
        ret = bdrv_flush(blk_bs(blk));
    }

    return ret < 0 ? ret : size;
}

int blk_load_vmstate(BlockBackend *blk, uint8_t *buf, int64_t pos, int size)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_load_vmstate(blk_bs(blk), buf, pos, size);
}

int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_probe_blocksizes(blk_bs(blk), bsz);
}

int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo)
{
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_probe_geometry(blk_bs(blk), geo);
}

/*
 * Updates the BlockBackendRootState object with data from the currently
 * attached BlockDriverState.
 */
void blk_update_root_state(BlockBackend *blk)
{
    assert(blk->root);

    blk->root_state.open_flags    = blk->root->bs->open_flags;
    blk->root_state.read_only     = blk->root->bs->read_only;
    blk->root_state.detect_zeroes = blk->root->bs->detect_zeroes;
}

/*
 * Applies the information in the root state to the given BlockDriverState. This
 * does not include the flags which have to be specified for bdrv_open(), use
 * blk_get_open_flags_from_root_state() to inquire them.
 */
void blk_apply_root_state(BlockBackend *blk, BlockDriverState *bs)
{
    bs->detect_zeroes = blk->root_state.detect_zeroes;
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

int blk_commit_all(void)
{
    BlockBackend *blk = NULL;

    while ((blk = blk_all_next(blk)) != NULL) {
        AioContext *aio_context = blk_get_aio_context(blk);

        aio_context_acquire(aio_context);
        if (blk_is_inserted(blk) && blk->root->bs->backing) {
            int ret = bdrv_commit(blk->root->bs);
            if (ret < 0) {
                aio_context_release(aio_context);
                return ret;
            }
        }
        aio_context_release(aio_context);
    }
    return 0;
}

int blk_flush_all(void)
{
    BlockBackend *blk = NULL;
    int result = 0;

    while ((blk = blk_all_next(blk)) != NULL) {
        AioContext *aio_context = blk_get_aio_context(blk);
        int ret;

        aio_context_acquire(aio_context);
        if (blk_is_inserted(blk)) {
            ret = blk_flush(blk);
            if (ret < 0 && !result) {
                result = ret;
            }
        }
        aio_context_release(aio_context);
    }

    return result;
}


/* throttling disk I/O limits */
void blk_set_io_limits(BlockBackend *blk, ThrottleConfig *cfg)
{
    throttle_group_config(blk, cfg);
}

void blk_io_limits_disable(BlockBackend *blk)
{
    assert(blk->public.throttle_state);
    bdrv_drained_begin(blk_bs(blk));
    throttle_group_unregister_blk(blk);
    bdrv_drained_end(blk_bs(blk));
}

/* should be called before blk_set_io_limits if a limit is set */
void blk_io_limits_enable(BlockBackend *blk, const char *group)
{
    assert(!blk->public.throttle_state);
    throttle_group_register_blk(blk, group);
}

void blk_io_limits_update_group(BlockBackend *blk, const char *group)
{
    /* this BB is not part of any group */
    if (!blk->public.throttle_state) {
        return;
    }

    /* this BB is a part of the same group than the one we want */
    if (!g_strcmp0(throttle_group_get_name(blk), group)) {
        return;
    }

    /* need to change the group this bs belong to */
    blk_io_limits_disable(blk);
    blk_io_limits_enable(blk, group);
}

static void blk_root_drained_begin(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;

    /* Note that blk->root may not be accessible here yet if we are just
     * attaching to a BlockDriverState that is drained. Use child instead. */

    if (blk->public.io_limits_disabled++ == 0) {
        throttle_group_restart_blk(blk);
    }
}

static void blk_root_drained_end(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;

    assert(blk->public.io_limits_disabled);
    --blk->public.io_limits_disabled;
}
