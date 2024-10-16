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
#include "block/coroutines.h"
#include "block/throttle-groups.h"
#include "hw/qdev-core.h"
#include "sysemu/blockdev.h"
#include "sysemu/runstate.h"
#include "sysemu/replay.h"
#include "qapi/error.h"
#include "qapi/qapi-events-block.h"
#include "qemu/id.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "trace.h"
#include "migration/misc.h"

/* Number of coroutines to reserve per attached device model */
#define COROUTINE_POOL_RESERVATION 64

#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */

typedef struct BlockBackendAioNotifier {
    void (*attached_aio_context)(AioContext *new_context, void *opaque);
    void (*detach_aio_context)(void *opaque);
    void *opaque;
    QLIST_ENTRY(BlockBackendAioNotifier) list;
} BlockBackendAioNotifier;

struct BlockBackend {
    char *name;
    int refcnt;
    BdrvChild *root;
    AioContext *ctx; /* access with atomic operations only */
    DriveInfo *legacy_dinfo;    /* null unless created by drive_new() */
    QTAILQ_ENTRY(BlockBackend) link;         /* for block_backends */
    QTAILQ_ENTRY(BlockBackend) monitor_link; /* for monitor_block_backends */
    BlockBackendPublic public;

    DeviceState *dev;           /* attached device model, if any */
    const BlockDevOps *dev_ops;
    void *dev_opaque;

    /* If the BDS tree is removed, some of its options are stored here (which
     * can be used to restore those options in the new BDS on insert) */
    BlockBackendRootState root_state;

    bool enable_write_cache;

    /* I/O stats (display with "info blockstats"). */
    BlockAcctStats stats;

    BlockdevOnError on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;

    uint64_t perm;
    uint64_t shared_perm;
    bool disable_perm;

    bool allow_aio_context_change;
    bool allow_write_beyond_eof;

    /* Protected by BQL */
    NotifierList remove_bs_notifiers, insert_bs_notifiers;
    QLIST_HEAD(, BlockBackendAioNotifier) aio_notifiers;

    int quiesce_counter; /* atomic: written under BQL, read by other threads */
    QemuMutex queued_requests_lock; /* protects queued_requests */
    CoQueue queued_requests;
    bool disable_request_queuing; /* atomic */

    VMChangeStateEntry *vmsh;
    bool force_allow_inactivate;

    /* Number of in-flight aio requests.  BlockDriverState also counts
     * in-flight requests but aio requests can exist even when blk->root is
     * NULL, so we cannot rely on its counter for that case.
     * Accessed with atomic ops.
     */
    unsigned int in_flight;
};

typedef struct BlockBackendAIOCB {
    BlockAIOCB common;
    BlockBackend *blk;
    int ret;
} BlockBackendAIOCB;

static const AIOCBInfo block_backend_aiocb_info = {
    .aiocb_size = sizeof(BlockBackendAIOCB),
};

static void drive_info_del(DriveInfo *dinfo);
static BlockBackend *bdrv_first_blk(BlockDriverState *bs);

/* All BlockBackends. Protected by BQL. */
static QTAILQ_HEAD(, BlockBackend) block_backends =
    QTAILQ_HEAD_INITIALIZER(block_backends);

/*
 * All BlockBackends referenced by the monitor and which are iterated through by
 * blk_next(). Protected by BQL.
 */
static QTAILQ_HEAD(, BlockBackend) monitor_block_backends =
    QTAILQ_HEAD_INITIALIZER(monitor_block_backends);

static int coroutine_mixed_fn GRAPH_RDLOCK
blk_set_perm_locked(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                    Error **errp);

static void blk_root_inherit_options(BdrvChildRole role, bool parent_is_format,
                                     int *child_flags, QDict *child_options,
                                     int parent_flags, QDict *parent_options)
{
    /* We're not supposed to call this function for root nodes */
    abort();
}
static void blk_root_drained_begin(BdrvChild *child);
static bool blk_root_drained_poll(BdrvChild *child);
static void blk_root_drained_end(BdrvChild *child);

static void blk_root_change_media(BdrvChild *child, bool load);
static void blk_root_resize(BdrvChild *child);

static bool blk_root_change_aio_ctx(BdrvChild *child, AioContext *ctx,
                                    GHashTable *visited, Transaction *tran,
                                    Error **errp);

static char *blk_root_get_parent_desc(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    g_autofree char *dev_id = NULL;

    if (blk->name) {
        return g_strdup_printf("block device '%s'", blk->name);
    }

    dev_id = blk_get_attached_dev_id(blk);
    if (*dev_id) {
        return g_strdup_printf("block device '%s'", dev_id);
    } else {
        /* TODO Callback into the BB owner for something more detailed */
        return g_strdup("an unnamed block device");
    }
}

static const char *blk_root_get_name(BdrvChild *child)
{
    return blk_name(child->opaque);
}

static void blk_vm_state_changed(void *opaque, bool running, RunState state)
{
    Error *local_err = NULL;
    BlockBackend *blk = opaque;

    if (state == RUN_STATE_INMIGRATE) {
        return;
    }

    qemu_del_vm_change_state_handler(blk->vmsh);
    blk->vmsh = NULL;
    blk_set_perm(blk, blk->perm, blk->shared_perm, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

/*
 * Notifies the user of the BlockBackend that migration has completed. qdev
 * devices can tighten their permissions in response (specifically revoke
 * shared write permissions that we needed for storage migration).
 *
 * If an error is returned, the VM cannot be allowed to be resumed.
 */
static void GRAPH_RDLOCK blk_root_activate(BdrvChild *child, Error **errp)
{
    BlockBackend *blk = child->opaque;
    Error *local_err = NULL;
    uint64_t saved_shared_perm;

    if (!blk->disable_perm) {
        return;
    }

    blk->disable_perm = false;

    /*
     * blk->shared_perm contains the permissions we want to share once
     * migration is really completely done.  For now, we need to share
     * all; but we also need to retain blk->shared_perm, which is
     * overwritten by a successful blk_set_perm() call.  Save it and
     * restore it below.
     */
    saved_shared_perm = blk->shared_perm;

    blk_set_perm_locked(blk, blk->perm, BLK_PERM_ALL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        blk->disable_perm = true;
        return;
    }
    blk->shared_perm = saved_shared_perm;

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        /* Activation can happen when migration process is still active, for
         * example when nbd_server_add is called during non-shared storage
         * migration. Defer the shared_perm update to migration completion. */
        if (!blk->vmsh) {
            blk->vmsh = qemu_add_vm_change_state_handler(blk_vm_state_changed,
                                                         blk);
        }
        return;
    }

    blk_set_perm_locked(blk, blk->perm, blk->shared_perm, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        blk->disable_perm = true;
        return;
    }
}

void blk_set_force_allow_inactivate(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    blk->force_allow_inactivate = true;
}

static bool blk_can_inactivate(BlockBackend *blk)
{
    /* If it is a guest device, inactivate is ok. */
    if (blk->dev || blk_name(blk)[0]) {
        return true;
    }

    /* Inactivating means no more writes to the image can be done,
     * even if those writes would be changes invisible to the
     * guest.  For block job BBs that satisfy this, we can just allow
     * it.  This is the case for mirror job source, which is required
     * by libvirt non-shared block migration. */
    if (!(blk->perm & (BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED))) {
        return true;
    }

    return blk->force_allow_inactivate;
}

static int GRAPH_RDLOCK blk_root_inactivate(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;

    if (blk->disable_perm) {
        return 0;
    }

    if (!blk_can_inactivate(blk)) {
        return -EPERM;
    }

    blk->disable_perm = true;
    if (blk->root) {
        bdrv_child_try_set_perm(blk->root, 0, BLK_PERM_ALL, &error_abort);
    }

    return 0;
}

static void blk_root_attach(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    BlockBackendAioNotifier *notifier;

    trace_blk_root_attach(child, blk, child->bs);

    QLIST_FOREACH(notifier, &blk->aio_notifiers, list) {
        bdrv_add_aio_context_notifier(child->bs,
                notifier->attached_aio_context,
                notifier->detach_aio_context,
                notifier->opaque);
    }
}

static void blk_root_detach(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    BlockBackendAioNotifier *notifier;

    trace_blk_root_detach(child, blk, child->bs);

    QLIST_FOREACH(notifier, &blk->aio_notifiers, list) {
        bdrv_remove_aio_context_notifier(child->bs,
                notifier->attached_aio_context,
                notifier->detach_aio_context,
                notifier->opaque);
    }
}

static AioContext *blk_root_get_parent_aio_context(BdrvChild *c)
{
    BlockBackend *blk = c->opaque;
    IO_CODE();

    return blk_get_aio_context(blk);
}

static const BdrvChildClass child_root = {
    .inherit_options    = blk_root_inherit_options,

    .change_media       = blk_root_change_media,
    .resize             = blk_root_resize,
    .get_name           = blk_root_get_name,
    .get_parent_desc    = blk_root_get_parent_desc,

    .drained_begin      = blk_root_drained_begin,
    .drained_poll       = blk_root_drained_poll,
    .drained_end        = blk_root_drained_end,

    .activate           = blk_root_activate,
    .inactivate         = blk_root_inactivate,

    .attach             = blk_root_attach,
    .detach             = blk_root_detach,

    .change_aio_ctx     = blk_root_change_aio_ctx,

    .get_parent_aio_context = blk_root_get_parent_aio_context,
};

/*
 * Create a new BlockBackend with a reference count of one.
 *
 * @perm is a bitmasks of BLK_PERM_* constants which describes the permissions
 * to request for a block driver node that is attached to this BlockBackend.
 * @shared_perm is a bitmask which describes which permissions may be granted
 * to other users of the attached node.
 * Both sets of permissions can be changed later using blk_set_perm().
 *
 * Return the new BlockBackend on success, null on failure.
 */
BlockBackend *blk_new(AioContext *ctx, uint64_t perm, uint64_t shared_perm)
{
    BlockBackend *blk;

    GLOBAL_STATE_CODE();

    blk = g_new0(BlockBackend, 1);
    blk->refcnt = 1;
    blk->ctx = ctx;
    blk->perm = perm;
    blk->shared_perm = shared_perm;
    blk_set_enable_write_cache(blk, true);

    blk->on_read_error = BLOCKDEV_ON_ERROR_REPORT;
    blk->on_write_error = BLOCKDEV_ON_ERROR_ENOSPC;

    block_acct_init(&blk->stats);

    qemu_mutex_init(&blk->queued_requests_lock);
    qemu_co_queue_init(&blk->queued_requests);
    notifier_list_init(&blk->remove_bs_notifiers);
    notifier_list_init(&blk->insert_bs_notifiers);
    QLIST_INIT(&blk->aio_notifiers);

    QTAILQ_INSERT_TAIL(&block_backends, blk, link);
    return blk;
}

/*
 * Create a new BlockBackend connected to an existing BlockDriverState.
 *
 * @perm is a bitmasks of BLK_PERM_* constants which describes the
 * permissions to request for @bs that is attached to this
 * BlockBackend.  @shared_perm is a bitmask which describes which
 * permissions may be granted to other users of the attached node.
 * Both sets of permissions can be changed later using blk_set_perm().
 *
 * Return the new BlockBackend on success, null on failure.
 */
BlockBackend *blk_new_with_bs(BlockDriverState *bs, uint64_t perm,
                              uint64_t shared_perm, Error **errp)
{
    BlockBackend *blk = blk_new(bdrv_get_aio_context(bs), perm, shared_perm);

    GLOBAL_STATE_CODE();

    if (blk_insert_bs(blk, bs, errp) < 0) {
        blk_unref(blk);
        return NULL;
    }
    return blk;
}

/*
 * Creates a new BlockBackend, opens a new BlockDriverState, and connects both.
 * By default, the new BlockBackend is in the main AioContext, but if the
 * parameters connect it with any existing node in a different AioContext, it
 * may end up there instead.
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
    uint64_t perm = 0;
    uint64_t shared = BLK_PERM_ALL;

    GLOBAL_STATE_CODE();

    /*
     * blk_new_open() is mainly used in .bdrv_create implementations and the
     * tools where sharing isn't a major concern because the BDS stays private
     * and the file is generally not supposed to be used by a second process,
     * so we just request permission according to the flags.
     *
     * The exceptions are xen_disk and blockdev_init(); in these cases, the
     * caller of blk_new_open() doesn't make use of the permissions, but they
     * shouldn't hurt either. We can still share everything here because the
     * guest devices will add their own blockers if they can't share.
     */
    if ((flags & BDRV_O_NO_IO) == 0) {
        perm |= BLK_PERM_CONSISTENT_READ;
        if (flags & BDRV_O_RDWR) {
            perm |= BLK_PERM_WRITE;
        }
    }
    if (flags & BDRV_O_RESIZE) {
        perm |= BLK_PERM_RESIZE;
    }
    if (flags & BDRV_O_NO_SHARE) {
        shared = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    }

    bs = bdrv_open(filename, reference, options, flags, errp);
    if (!bs) {
        return NULL;
    }

    /* bdrv_open() could have moved bs to a different AioContext */
    blk = blk_new(bdrv_get_aio_context(bs), perm, shared);
    blk->perm = perm;
    blk->shared_perm = shared;

    blk_insert_bs(blk, bs, errp);
    bdrv_unref(bs);

    if (!blk->root) {
        blk_unref(blk);
        return NULL;
    }

    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    assert(!blk->name);
    assert(!blk->dev);
    if (blk->public.throttle_group_member.throttle_state) {
        blk_io_limits_disable(blk);
    }
    if (blk->root) {
        blk_remove_bs(blk);
    }
    if (blk->vmsh) {
        qemu_del_vm_change_state_handler(blk->vmsh);
        blk->vmsh = NULL;
    }
    assert(QLIST_EMPTY(&blk->remove_bs_notifiers.notifiers));
    assert(QLIST_EMPTY(&blk->insert_bs_notifiers.notifiers));
    assert(QLIST_EMPTY(&blk->aio_notifiers));
    assert(qemu_co_queue_empty(&blk->queued_requests));
    qemu_mutex_destroy(&blk->queued_requests_lock);
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
    g_free(dinfo);
}

int blk_get_refcnt(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk ? blk->refcnt : 0;
}

/*
 * Increment @blk's reference count.
 * @blk must not be null.
 */
void blk_ref(BlockBackend *blk)
{
    assert(blk->refcnt > 0);
    GLOBAL_STATE_CODE();
    blk->refcnt++;
}

/*
 * Decrement @blk's reference count.
 * If this drops it to zero, destroy @blk.
 * For convenience, do nothing if @blk is null.
 */
void blk_unref(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    if (blk) {
        assert(blk->refcnt > 0);
        if (blk->refcnt > 1) {
            blk->refcnt--;
        } else {
            blk_drain(blk);
            /* blk_drain() cannot resurrect blk, nobody held a reference */
            assert(blk->refcnt == 1);
            blk->refcnt = 0;
            blk_delete(blk);
        }
    }
}

/*
 * Behaves similarly to blk_next() but iterates over all BlockBackends, even the
 * ones which are hidden (i.e. are not referenced by the monitor).
 */
BlockBackend *blk_all_next(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk ? QTAILQ_NEXT(blk, link)
               : QTAILQ_FIRST(&block_backends);
}

void blk_remove_all_bs(void)
{
    BlockBackend *blk = NULL;

    GLOBAL_STATE_CODE();

    while ((blk = blk_all_next(blk)) != NULL) {
        if (blk->root) {
            blk_remove_bs(blk);
        }
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
    GLOBAL_STATE_CODE();
    return blk ? QTAILQ_NEXT(blk, monitor_link)
               : QTAILQ_FIRST(&monitor_block_backends);
}

/* Iterates over all top-level BlockDriverStates, i.e. BDSs that are owned by
 * the monitor or attached to a BlockBackend */
BlockDriverState *bdrv_next(BdrvNextIterator *it)
{
    BlockDriverState *bs, *old_bs;

    /* Must be called from the main loop */
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    old_bs = it->bs;

    /* First, return all root nodes of BlockBackends. In order to avoid
     * returning a BDS twice when multiple BBs refer to it, we only return it
     * if the BB is the first one in the parent list of the BDS. */
    if (it->phase == BDRV_NEXT_BACKEND_ROOTS) {
        BlockBackend *old_blk = it->blk;

        do {
            it->blk = blk_all_next(it->blk);
            bs = it->blk ? blk_bs(it->blk) : NULL;
        } while (it->blk && (bs == NULL || bdrv_first_blk(bs) != it->blk));

        if (it->blk) {
            blk_ref(it->blk);
        }
        blk_unref(old_blk);

        if (bs) {
            bdrv_ref(bs);
            bdrv_unref(old_bs);
            it->bs = bs;
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

    if (bs) {
        bdrv_ref(bs);
    }
    bdrv_unref(old_bs);

    return bs;
}

static void bdrv_next_reset(BdrvNextIterator *it)
{
    *it = (BdrvNextIterator) {
        .phase = BDRV_NEXT_BACKEND_ROOTS,
    };
}

BlockDriverState *bdrv_first(BdrvNextIterator *it)
{
    GLOBAL_STATE_CODE();
    bdrv_next_reset(it);
    return bdrv_next(it);
}

/* Must be called when aborting a bdrv_next() iteration before
 * bdrv_next() returns NULL */
void bdrv_next_cleanup(BdrvNextIterator *it)
{
    /* Must be called from the main loop */
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    bdrv_unref(it->bs);

    if (it->phase == BDRV_NEXT_BACKEND_ROOTS && it->blk) {
        blk_unref(it->blk);
    }

    bdrv_next_reset(it);
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
    GLOBAL_STATE_CODE();

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
    GLOBAL_STATE_CODE();

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
const char *blk_name(const BlockBackend *blk)
{
    IO_CODE();
    return blk->name ?: "";
}

/*
 * Return the BlockBackend with name @name if it exists, else null.
 * @name must not be null.
 */
BlockBackend *blk_by_name(const char *name)
{
    BlockBackend *blk = NULL;

    GLOBAL_STATE_CODE();
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
    IO_CODE();
    return blk->root ? blk->root->bs : NULL;
}

static BlockBackend * GRAPH_RDLOCK bdrv_first_blk(BlockDriverState *bs)
{
    BdrvChild *child;

    GLOBAL_STATE_CODE();
    assert_bdrv_graph_readable();

    QLIST_FOREACH(child, &bs->parents, next_parent) {
        if (child->klass == &child_root) {
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
    GLOBAL_STATE_CODE();
    return bdrv_first_blk(bs) != NULL;
}

/*
 * Returns true if @bs has only BlockBackends as parents.
 */
bool bdrv_is_root_node(BlockDriverState *bs)
{
    BdrvChild *c;

    GLOBAL_STATE_CODE();
    assert_bdrv_graph_readable();

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c->klass != &child_root) {
            return false;
        }
    }

    return true;
}

/*
 * Return @blk's DriveInfo if any, else null.
 */
DriveInfo *blk_legacy_dinfo(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
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
    GLOBAL_STATE_CODE();
    return blk->legacy_dinfo = dinfo;
}

/*
 * Return the BlockBackend with DriveInfo @dinfo.
 * It must exist.
 */
BlockBackend *blk_by_legacy_dinfo(DriveInfo *dinfo)
{
    BlockBackend *blk = NULL;
    GLOBAL_STATE_CODE();

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
    GLOBAL_STATE_CODE();
    return &blk->public;
}

/*
 * Disassociates the currently associated BlockDriverState from @blk.
 */
void blk_remove_bs(BlockBackend *blk)
{
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    BdrvChild *root;

    GLOBAL_STATE_CODE();

    notifier_list_notify(&blk->remove_bs_notifiers, blk);
    if (tgm->throttle_state) {
        BlockDriverState *bs = blk_bs(blk);

        /*
         * Take a ref in case blk_bs() changes across bdrv_drained_begin(), for
         * example, if a temporary filter node is removed by a blockjob.
         */
        bdrv_ref(bs);
        bdrv_drained_begin(bs);
        throttle_group_detach_aio_context(tgm);
        throttle_group_attach_aio_context(tgm, qemu_get_aio_context());
        bdrv_drained_end(bs);
        bdrv_unref(bs);
    }

    blk_update_root_state(blk);

    /* bdrv_root_unref_child() will cause blk->root to become stale and may
     * switch to a completion coroutine later on. Let's drain all I/O here
     * to avoid that and a potential QEMU crash.
     */
    blk_drain(blk);
    root = blk->root;
    blk->root = NULL;

    bdrv_graph_wrlock();
    bdrv_root_unref_child(root);
    bdrv_graph_wrunlock();
}

/*
 * Associates a new BlockDriverState with @blk.
 */
int blk_insert_bs(BlockBackend *blk, BlockDriverState *bs, Error **errp)
{
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;

    GLOBAL_STATE_CODE();
    bdrv_ref(bs);
    bdrv_graph_wrlock();
    blk->root = bdrv_root_attach_child(bs, "root", &child_root,
                                       BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                                       blk->perm, blk->shared_perm,
                                       blk, errp);
    bdrv_graph_wrunlock();
    if (blk->root == NULL) {
        return -EPERM;
    }

    notifier_list_notify(&blk->insert_bs_notifiers, blk);
    if (tgm->throttle_state) {
        throttle_group_detach_aio_context(tgm);
        throttle_group_attach_aio_context(tgm, bdrv_get_aio_context(bs));
    }

    return 0;
}

/*
 * Change BlockDriverState associated with @blk.
 */
int blk_replace_bs(BlockBackend *blk, BlockDriverState *new_bs, Error **errp)
{
    GLOBAL_STATE_CODE();
    return bdrv_replace_child_bs(blk->root, new_bs, errp);
}

/*
 * Sets the permission bitmasks that the user of the BlockBackend needs.
 */
static int coroutine_mixed_fn GRAPH_RDLOCK
blk_set_perm_locked(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                    Error **errp)
{
    int ret;
    GLOBAL_STATE_CODE();

    if (blk->root && !blk->disable_perm) {
        ret = bdrv_child_try_set_perm(blk->root, perm, shared_perm, errp);
        if (ret < 0) {
            return ret;
        }
    }

    blk->perm = perm;
    blk->shared_perm = shared_perm;

    return 0;
}

int blk_set_perm(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                 Error **errp)
{
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    return blk_set_perm_locked(blk, perm, shared_perm, errp);
}

void blk_get_perm(BlockBackend *blk, uint64_t *perm, uint64_t *shared_perm)
{
    GLOBAL_STATE_CODE();
    *perm = blk->perm;
    *shared_perm = blk->shared_perm;
}

/*
 * Attach device model @dev to @blk.
 * Return 0 on success, -EBUSY when a device model is attached already.
 */
int blk_attach_dev(BlockBackend *blk, DeviceState *dev)
{
    GLOBAL_STATE_CODE();
    if (blk->dev) {
        return -EBUSY;
    }

    /* While migration is still incoming, we don't need to apply the
     * permissions of guest device BlockBackends. We might still have a block
     * job or NBD server writing to the image for storage migration. */
    if (runstate_check(RUN_STATE_INMIGRATE)) {
        blk->disable_perm = true;
    }

    blk_ref(blk);
    blk->dev = dev;
    blk_iostatus_reset(blk);

    return 0;
}

/*
 * Detach device model @dev from @blk.
 * @dev must be currently attached to @blk.
 */
void blk_detach_dev(BlockBackend *blk, DeviceState *dev)
{
    assert(blk->dev == dev);
    GLOBAL_STATE_CODE();
    blk->dev = NULL;
    blk->dev_ops = NULL;
    blk->dev_opaque = NULL;
    blk_set_perm(blk, 0, BLK_PERM_ALL, &error_abort);
    blk_unref(blk);
}

/*
 * Return the device model attached to @blk if any, else null.
 */
DeviceState *blk_get_attached_dev(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk->dev;
}

/* Return the qdev ID, or if no ID is assigned the QOM path, of the block
 * device attached to the BlockBackend. */
char *blk_get_attached_dev_id(BlockBackend *blk)
{
    DeviceState *dev = blk->dev;
    IO_CODE();

    if (!dev) {
        return g_strdup("");
    } else if (dev->id) {
        return g_strdup(dev->id);
    }

    return object_get_canonical_path(OBJECT(dev)) ?: g_strdup("");
}

/*
 * Return the BlockBackend which has the device model @dev attached if it
 * exists, else null.
 *
 * @dev must not be null.
 */
BlockBackend *blk_by_dev(void *dev)
{
    BlockBackend *blk = NULL;

    GLOBAL_STATE_CODE();

    assert(dev != NULL);
    while ((blk = blk_all_next(blk)) != NULL) {
        if (blk->dev == dev) {
            return blk;
        }
    }
    return NULL;
}

/*
 * Set @blk's device model callbacks to @ops.
 * @opaque is the opaque argument to pass to the callbacks.
 * This is for use by device models.
 */
void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops,
                     void *opaque)
{
    GLOBAL_STATE_CODE();
    blk->dev_ops = ops;
    blk->dev_opaque = opaque;

    /* Are we currently quiesced? Should we enforce this right now? */
    if (qatomic_read(&blk->quiesce_counter) && ops && ops->drained_begin) {
        ops->drained_begin(opaque);
    }
}

/*
 * Notify @blk's attached device model of media change.
 *
 * If @load is true, notify of media load. This action can fail, meaning that
 * the medium cannot be loaded. @errp is set then.
 *
 * If @load is false, notify of media eject. This can never fail.
 *
 * Also send DEVICE_TRAY_MOVED events as appropriate.
 */
void blk_dev_change_media_cb(BlockBackend *blk, bool load, Error **errp)
{
    GLOBAL_STATE_CODE();
    if (blk->dev_ops && blk->dev_ops->change_media_cb) {
        bool tray_was_open, tray_is_open;
        Error *local_err = NULL;

        tray_was_open = blk_dev_is_tray_open(blk);
        blk->dev_ops->change_media_cb(blk->dev_opaque, load, &local_err);
        if (local_err) {
            assert(load == true);
            error_propagate(errp, local_err);
            return;
        }
        tray_is_open = blk_dev_is_tray_open(blk);

        if (tray_was_open != tray_is_open) {
            char *id = blk_get_attached_dev_id(blk);
            qapi_event_send_device_tray_moved(blk_name(blk), id, tray_is_open);
            g_free(id);
        }
    }
}

static void blk_root_change_media(BdrvChild *child, bool load)
{
    blk_dev_change_media_cb(child->opaque, load, NULL);
}

/*
 * Does @blk's attached device model have removable media?
 * %true if no device model is attached.
 */
bool blk_dev_has_removable_media(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return !blk->dev || (blk->dev_ops && blk->dev_ops->change_media_cb);
}

/*
 * Does @blk's attached device model have a tray?
 */
bool blk_dev_has_tray(BlockBackend *blk)
{
    IO_CODE();
    return blk->dev_ops && blk->dev_ops->is_tray_open;
}

/*
 * Notify @blk's attached device model of a media eject request.
 * If @force is true, the medium is about to be yanked out forcefully.
 */
void blk_dev_eject_request(BlockBackend *blk, bool force)
{
    GLOBAL_STATE_CODE();
    if (blk->dev_ops && blk->dev_ops->eject_request_cb) {
        blk->dev_ops->eject_request_cb(blk->dev_opaque, force);
    }
}

/*
 * Does @blk's attached device model have a tray, and is it open?
 */
bool blk_dev_is_tray_open(BlockBackend *blk)
{
    IO_CODE();
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
    GLOBAL_STATE_CODE();
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
    GLOBAL_STATE_CODE();
    blk->iostatus_enabled = true;
    blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
}

/* The I/O status is only enabled if the drive explicitly
 * enables it _and_ the VM is configured to stop on errors */
bool blk_iostatus_is_enabled(const BlockBackend *blk)
{
    IO_CODE();
    return (blk->iostatus_enabled &&
           (blk->on_write_error == BLOCKDEV_ON_ERROR_ENOSPC ||
            blk->on_write_error == BLOCKDEV_ON_ERROR_STOP   ||
            blk->on_read_error == BLOCKDEV_ON_ERROR_STOP));
}

BlockDeviceIoStatus blk_iostatus(const BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk->iostatus;
}

void blk_iostatus_reset(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    if (blk_iostatus_is_enabled(blk)) {
        blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
    }
}

void blk_iostatus_set_err(BlockBackend *blk, int error)
{
    IO_CODE();
    assert(blk_iostatus_is_enabled(blk));
    if (blk->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        blk->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                          BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

void blk_set_allow_write_beyond_eof(BlockBackend *blk, bool allow)
{
    IO_CODE();
    blk->allow_write_beyond_eof = allow;
}

void blk_set_allow_aio_context_change(BlockBackend *blk, bool allow)
{
    IO_CODE();
    blk->allow_aio_context_change = allow;
}

void blk_set_disable_request_queuing(BlockBackend *blk, bool disable)
{
    IO_CODE();
    qatomic_set(&blk->disable_request_queuing, disable);
}

static int coroutine_fn GRAPH_RDLOCK
blk_check_byte_request(BlockBackend *blk, int64_t offset, int64_t bytes)
{
    int64_t len;

    if (bytes < 0) {
        return -EIO;
    }

    if (!blk_co_is_available(blk)) {
        return -ENOMEDIUM;
    }

    if (offset < 0) {
        return -EIO;
    }

    if (!blk->allow_write_beyond_eof) {
        len = bdrv_co_getlength(blk_bs(blk));
        if (len < 0) {
            return len;
        }

        if (offset > len || len - offset < bytes) {
            return -EIO;
        }
    }

    return 0;
}

/* Are we currently in a drained section? */
bool blk_in_drain(BlockBackend *blk)
{
    GLOBAL_STATE_CODE(); /* change to IO_OR_GS_CODE(), if necessary */
    return qatomic_read(&blk->quiesce_counter);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static void coroutine_fn blk_wait_while_drained(BlockBackend *blk)
{
    assert(blk->in_flight > 0);

    if (qatomic_read(&blk->quiesce_counter) &&
        !qatomic_read(&blk->disable_request_queuing)) {
        /*
         * Take lock before decrementing in flight counter so main loop thread
         * waits for us to enqueue ourselves before it can leave the drained
         * section.
         */
        qemu_mutex_lock(&blk->queued_requests_lock);
        blk_dec_in_flight(blk);
        qemu_co_queue_wait(&blk->queued_requests, &blk->queued_requests_lock);
        blk_inc_in_flight(blk);
        qemu_mutex_unlock(&blk->queued_requests_lock);
    }
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_co_do_preadv_part(BlockBackend *blk, int64_t offset, int64_t bytes,
                      QEMUIOVector *qiov, size_t qiov_offset,
                      BdrvRequestFlags flags)
{
    int ret;
    BlockDriverState *bs;
    IO_CODE();

    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    /* Call blk_bs() only after waiting, the graph may have changed */
    bs = blk_bs(blk);
    trace_blk_co_preadv(blk, bs, offset, bytes, flags);

    ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    bdrv_inc_in_flight(bs);

    /* throttling disk I/O */
    if (blk->public.throttle_group_member.throttle_state) {
        throttle_group_co_io_limits_intercept(&blk->public.throttle_group_member,
                bytes, THROTTLE_READ);
    }

    ret = bdrv_co_preadv_part(blk->root, offset, bytes, qiov, qiov_offset,
                              flags);
    bdrv_dec_in_flight(bs);
    return ret;
}

int coroutine_fn blk_co_pread(BlockBackend *blk, int64_t offset, int64_t bytes,
                              void *buf, BdrvRequestFlags flags)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    IO_OR_GS_CODE();

    assert(bytes <= SIZE_MAX);

    return blk_co_preadv(blk, offset, bytes, &qiov, flags);
}

int coroutine_fn blk_co_preadv(BlockBackend *blk, int64_t offset,
                               int64_t bytes, QEMUIOVector *qiov,
                               BdrvRequestFlags flags)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_preadv_part(blk, offset, bytes, qiov, 0, flags);
    blk_dec_in_flight(blk);

    return ret;
}

int coroutine_fn blk_co_preadv_part(BlockBackend *blk, int64_t offset,
                                    int64_t bytes, QEMUIOVector *qiov,
                                    size_t qiov_offset, BdrvRequestFlags flags)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_preadv_part(blk, offset, bytes, qiov, qiov_offset, flags);
    blk_dec_in_flight(blk);

    return ret;
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_co_do_pwritev_part(BlockBackend *blk, int64_t offset, int64_t bytes,
                       QEMUIOVector *qiov, size_t qiov_offset,
                       BdrvRequestFlags flags)
{
    int ret;
    BlockDriverState *bs;
    IO_CODE();

    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    /* Call blk_bs() only after waiting, the graph may have changed */
    bs = blk_bs(blk);
    trace_blk_co_pwritev(blk, bs, offset, bytes, flags);

    ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    bdrv_inc_in_flight(bs);
    /* throttling disk I/O */
    if (blk->public.throttle_group_member.throttle_state) {
        throttle_group_co_io_limits_intercept(&blk->public.throttle_group_member,
                bytes, THROTTLE_WRITE);
    }

    if (!blk->enable_write_cache) {
        flags |= BDRV_REQ_FUA;
    }

    ret = bdrv_co_pwritev_part(blk->root, offset, bytes, qiov, qiov_offset,
                               flags);
    bdrv_dec_in_flight(bs);
    return ret;
}

int coroutine_fn blk_co_pwritev_part(BlockBackend *blk, int64_t offset,
                                     int64_t bytes,
                                     QEMUIOVector *qiov, size_t qiov_offset,
                                     BdrvRequestFlags flags)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_pwritev_part(blk, offset, bytes, qiov, qiov_offset, flags);
    blk_dec_in_flight(blk);

    return ret;
}

int coroutine_fn blk_co_pwrite(BlockBackend *blk, int64_t offset, int64_t bytes,
                               const void *buf, BdrvRequestFlags flags)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    IO_OR_GS_CODE();

    assert(bytes <= SIZE_MAX);

    return blk_co_pwritev(blk, offset, bytes, &qiov, flags);
}

int coroutine_fn blk_co_pwritev(BlockBackend *blk, int64_t offset,
                                int64_t bytes, QEMUIOVector *qiov,
                                BdrvRequestFlags flags)
{
    IO_OR_GS_CODE();
    return blk_co_pwritev_part(blk, offset, bytes, qiov, 0, flags);
}

int coroutine_fn blk_co_block_status_above(BlockBackend *blk,
                                           BlockDriverState *base,
                                           int64_t offset, int64_t bytes,
                                           int64_t *pnum, int64_t *map,
                                           BlockDriverState **file)
{
    IO_CODE();
    GRAPH_RDLOCK_GUARD();
    return bdrv_co_block_status_above(blk_bs(blk), base, offset, bytes, pnum,
                                      map, file);
}

int coroutine_fn blk_co_is_allocated_above(BlockBackend *blk,
                                           BlockDriverState *base,
                                           bool include_base, int64_t offset,
                                           int64_t bytes, int64_t *pnum)
{
    IO_CODE();
    GRAPH_RDLOCK_GUARD();
    return bdrv_co_is_allocated_above(blk_bs(blk), base, include_base, offset,
                                      bytes, pnum);
}

typedef struct BlkRwCo {
    BlockBackend *blk;
    int64_t offset;
    void *iobuf;
    int ret;
    BdrvRequestFlags flags;
} BlkRwCo;

int blk_make_zero(BlockBackend *blk, BdrvRequestFlags flags)
{
    GLOBAL_STATE_CODE();
    return bdrv_make_zero(blk->root, flags);
}

void blk_inc_in_flight(BlockBackend *blk)
{
    IO_CODE();
    qatomic_inc(&blk->in_flight);
}

void blk_dec_in_flight(BlockBackend *blk)
{
    IO_CODE();
    qatomic_dec(&blk->in_flight);
    aio_wait_kick();
}

static void error_callback_bh(void *opaque)
{
    struct BlockBackendAIOCB *acb = opaque;

    blk_dec_in_flight(acb->blk);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_unref(acb);
}

BlockAIOCB *blk_abort_aio_request(BlockBackend *blk,
                                  BlockCompletionFunc *cb,
                                  void *opaque, int ret)
{
    struct BlockBackendAIOCB *acb;
    IO_CODE();

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&block_backend_aiocb_info, blk, cb, opaque);
    acb->blk = blk;
    acb->ret = ret;

    replay_bh_schedule_oneshot_event(qemu_get_current_aio_context(),
                                     error_callback_bh, acb);
    return &acb->common;
}

typedef struct BlkAioEmAIOCB {
    BlockAIOCB common;
    BlkRwCo rwco;
    int64_t bytes;
    bool has_returned;
} BlkAioEmAIOCB;

static const AIOCBInfo blk_aio_em_aiocb_info = {
    .aiocb_size         = sizeof(BlkAioEmAIOCB),
};

static void blk_aio_complete(BlkAioEmAIOCB *acb)
{
    if (acb->has_returned) {
        acb->common.cb(acb->common.opaque, acb->rwco.ret);
        blk_dec_in_flight(acb->rwco.blk);
        qemu_aio_unref(acb);
    }
}

static void blk_aio_complete_bh(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    assert(acb->has_returned);
    blk_aio_complete(acb);
}

static BlockAIOCB *blk_aio_prwv(BlockBackend *blk, int64_t offset,
                                int64_t bytes,
                                void *iobuf, CoroutineEntry co_entry,
                                BdrvRequestFlags flags,
                                BlockCompletionFunc *cb, void *opaque)
{
    BlkAioEmAIOCB *acb;
    Coroutine *co;

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&blk_aio_em_aiocb_info, blk, cb, opaque);
    acb->rwco = (BlkRwCo) {
        .blk    = blk,
        .offset = offset,
        .iobuf  = iobuf,
        .flags  = flags,
        .ret    = NOT_DONE,
    };
    acb->bytes = bytes;
    acb->has_returned = false;

    co = qemu_coroutine_create(co_entry, acb);
    aio_co_enter(qemu_get_current_aio_context(), co);

    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        replay_bh_schedule_oneshot_event(qemu_get_current_aio_context(),
                                         blk_aio_complete_bh, acb);
    }

    return &acb->common;
}

static void coroutine_fn blk_aio_read_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;
    QEMUIOVector *qiov = rwco->iobuf;

    assert(qiov->size == acb->bytes);
    rwco->ret = blk_co_do_preadv_part(rwco->blk, rwco->offset, acb->bytes, qiov,
                                      0, rwco->flags);
    blk_aio_complete(acb);
}

static void coroutine_fn blk_aio_write_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;
    QEMUIOVector *qiov = rwco->iobuf;

    assert(!qiov || qiov->size == acb->bytes);
    rwco->ret = blk_co_do_pwritev_part(rwco->blk, rwco->offset, acb->bytes,
                                       qiov, 0, rwco->flags);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                                  int64_t bytes, BdrvRequestFlags flags,
                                  BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    return blk_aio_prwv(blk, offset, bytes, NULL, blk_aio_write_entry,
                        flags | BDRV_REQ_ZERO_WRITE, cb, opaque);
}

int64_t coroutine_fn blk_co_getlength(BlockBackend *blk)
{
    IO_CODE();
    GRAPH_RDLOCK_GUARD();

    if (!blk_co_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_getlength(blk_bs(blk));
}

int64_t coroutine_fn blk_co_nb_sectors(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    IO_CODE();
    GRAPH_RDLOCK_GUARD();

    if (!bs) {
        return -ENOMEDIUM;
    } else {
        return bdrv_co_nb_sectors(bs);
    }
}

/*
 * This wrapper is written by hand because this function is in the hot I/O path,
 * via blk_get_geometry.
 */
int64_t coroutine_mixed_fn blk_nb_sectors(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    IO_CODE();

    if (!bs) {
        return -ENOMEDIUM;
    } else {
        return bdrv_nb_sectors(bs);
    }
}

/* return 0 as number of sectors if no device present or error */
void coroutine_fn blk_co_get_geometry(BlockBackend *blk,
                                      uint64_t *nb_sectors_ptr)
{
    int64_t ret = blk_co_nb_sectors(blk);
    *nb_sectors_ptr = ret < 0 ? 0 : ret;
}

/*
 * This wrapper is written by hand because this function is in the hot I/O path.
 */
void coroutine_mixed_fn blk_get_geometry(BlockBackend *blk,
                                         uint64_t *nb_sectors_ptr)
{
    int64_t ret = blk_nb_sectors(blk);
    *nb_sectors_ptr = ret < 0 ? 0 : ret;
}

BlockAIOCB *blk_aio_preadv(BlockBackend *blk, int64_t offset,
                           QEMUIOVector *qiov, BdrvRequestFlags flags,
                           BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    assert((uint64_t)qiov->size <= INT64_MAX);
    return blk_aio_prwv(blk, offset, qiov->size, qiov,
                        blk_aio_read_entry, flags, cb, opaque);
}

BlockAIOCB *blk_aio_pwritev(BlockBackend *blk, int64_t offset,
                            QEMUIOVector *qiov, BdrvRequestFlags flags,
                            BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    assert((uint64_t)qiov->size <= INT64_MAX);
    return blk_aio_prwv(blk, offset, qiov->size, qiov,
                        blk_aio_write_entry, flags, cb, opaque);
}

void blk_aio_cancel(BlockAIOCB *acb)
{
    GLOBAL_STATE_CODE();
    bdrv_aio_cancel(acb);
}

void blk_aio_cancel_async(BlockAIOCB *acb)
{
    IO_CODE();
    bdrv_aio_cancel_async(acb);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_co_do_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    IO_CODE();

    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    if (!blk_co_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_ioctl(blk_bs(blk), req, buf);
}

int coroutine_fn blk_co_ioctl(BlockBackend *blk, unsigned long int req,
                              void *buf)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_ioctl(blk, req, buf);
    blk_dec_in_flight(blk);

    return ret;
}

static void coroutine_fn blk_aio_ioctl_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_do_ioctl(rwco->blk, rwco->offset, rwco->iobuf);

    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    return blk_aio_prwv(blk, req, 0, buf, blk_aio_ioctl_entry, 0, cb, opaque);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_co_do_pdiscard(BlockBackend *blk, int64_t offset, int64_t bytes)
{
    int ret;
    IO_CODE();

    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(blk->root, offset, bytes);
}

static void coroutine_fn blk_aio_pdiscard_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_do_pdiscard(rwco->blk, rwco->offset, acb->bytes);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_pdiscard(BlockBackend *blk,
                             int64_t offset, int64_t bytes,
                             BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    return blk_aio_prwv(blk, offset, bytes, NULL, blk_aio_pdiscard_entry, 0,
                        cb, opaque);
}

int coroutine_fn blk_co_pdiscard(BlockBackend *blk, int64_t offset,
                                 int64_t bytes)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_pdiscard(blk, offset, bytes);
    blk_dec_in_flight(blk);

    return ret;
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn blk_co_do_flush(BlockBackend *blk)
{
    IO_CODE();
    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    if (!blk_co_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_flush(blk_bs(blk));
}

static void coroutine_fn blk_aio_flush_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_do_flush(rwco->blk);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    return blk_aio_prwv(blk, 0, 0, NULL, blk_aio_flush_entry, 0, cb, opaque);
}

int coroutine_fn blk_co_flush(BlockBackend *blk)
{
    int ret;
    IO_OR_GS_CODE();

    blk_inc_in_flight(blk);
    ret = blk_co_do_flush(blk);
    blk_dec_in_flight(blk);

    return ret;
}

static void coroutine_fn blk_aio_zone_report_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_zone_report(rwco->blk, rwco->offset,
                                   (unsigned int*)(uintptr_t)acb->bytes,
                                   rwco->iobuf);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_zone_report(BlockBackend *blk, int64_t offset,
                                unsigned int *nr_zones,
                                BlockZoneDescriptor  *zones,
                                BlockCompletionFunc *cb, void *opaque)
{
    BlkAioEmAIOCB *acb;
    Coroutine *co;
    IO_CODE();

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&blk_aio_em_aiocb_info, blk, cb, opaque);
    acb->rwco = (BlkRwCo) {
        .blk    = blk,
        .offset = offset,
        .iobuf  = zones,
        .ret    = NOT_DONE,
    };
    acb->bytes = (int64_t)(uintptr_t)nr_zones,
    acb->has_returned = false;

    co = qemu_coroutine_create(blk_aio_zone_report_entry, acb);
    aio_co_enter(qemu_get_current_aio_context(), co);

    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        replay_bh_schedule_oneshot_event(qemu_get_current_aio_context(),
                                         blk_aio_complete_bh, acb);
    }

    return &acb->common;
}

static void coroutine_fn blk_aio_zone_mgmt_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_zone_mgmt(rwco->blk,
                                 (BlockZoneOp)(uintptr_t)rwco->iobuf,
                                 rwco->offset, acb->bytes);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_zone_mgmt(BlockBackend *blk, BlockZoneOp op,
                              int64_t offset, int64_t len,
                              BlockCompletionFunc *cb, void *opaque) {
    BlkAioEmAIOCB *acb;
    Coroutine *co;
    IO_CODE();

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&blk_aio_em_aiocb_info, blk, cb, opaque);
    acb->rwco = (BlkRwCo) {
        .blk    = blk,
        .offset = offset,
        .iobuf  = (void *)(uintptr_t)op,
        .ret    = NOT_DONE,
    };
    acb->bytes = len;
    acb->has_returned = false;

    co = qemu_coroutine_create(blk_aio_zone_mgmt_entry, acb);
    aio_co_enter(qemu_get_current_aio_context(), co);

    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        replay_bh_schedule_oneshot_event(qemu_get_current_aio_context(),
                                         blk_aio_complete_bh, acb);
    }

    return &acb->common;
}

static void coroutine_fn blk_aio_zone_append_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_co_zone_append(rwco->blk, (int64_t *)(uintptr_t)acb->bytes,
                                   rwco->iobuf, rwco->flags);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_zone_append(BlockBackend *blk, int64_t *offset,
                                QEMUIOVector *qiov, BdrvRequestFlags flags,
                                BlockCompletionFunc *cb, void *opaque) {
    BlkAioEmAIOCB *acb;
    Coroutine *co;
    IO_CODE();

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&blk_aio_em_aiocb_info, blk, cb, opaque);
    acb->rwco = (BlkRwCo) {
        .blk    = blk,
        .ret    = NOT_DONE,
        .flags  = flags,
        .iobuf  = qiov,
    };
    acb->bytes = (int64_t)(uintptr_t)offset;
    acb->has_returned = false;

    co = qemu_coroutine_create(blk_aio_zone_append_entry, acb);
    aio_co_enter(qemu_get_current_aio_context(), co);
    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        replay_bh_schedule_oneshot_event(qemu_get_current_aio_context(),
                                         blk_aio_complete_bh, acb);
    }

    return &acb->common;
}

/*
 * Send a zone_report command.
 * offset is a byte offset from the start of the device. No alignment
 * required for offset.
 * nr_zones represents IN maximum and OUT actual.
 */
int coroutine_fn blk_co_zone_report(BlockBackend *blk, int64_t offset,
                                    unsigned int *nr_zones,
                                    BlockZoneDescriptor *zones)
{
    int ret;
    IO_CODE();

    blk_inc_in_flight(blk); /* increase before waiting */
    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();
    if (!blk_is_available(blk)) {
        blk_dec_in_flight(blk);
        return -ENOMEDIUM;
    }
    ret = bdrv_co_zone_report(blk_bs(blk), offset, nr_zones, zones);
    blk_dec_in_flight(blk);
    return ret;
}

/*
 * Send a zone_management command.
 * op is the zone operation;
 * offset is the byte offset from the start of the zoned device;
 * len is the maximum number of bytes the command should operate on. It
 * should be aligned with the device zone size.
 */
int coroutine_fn blk_co_zone_mgmt(BlockBackend *blk, BlockZoneOp op,
        int64_t offset, int64_t len)
{
    int ret;
    IO_CODE();

    blk_inc_in_flight(blk);
    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();

    ret = blk_check_byte_request(blk, offset, len);
    if (ret < 0) {
        blk_dec_in_flight(blk);
        return ret;
    }

    ret = bdrv_co_zone_mgmt(blk_bs(blk), op, offset, len);
    blk_dec_in_flight(blk);
    return ret;
}

/*
 * Send a zone_append command.
 */
int coroutine_fn blk_co_zone_append(BlockBackend *blk, int64_t *offset,
        QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;
    IO_CODE();

    blk_inc_in_flight(blk);
    blk_wait_while_drained(blk);
    GRAPH_RDLOCK_GUARD();
    if (!blk_is_available(blk)) {
        blk_dec_in_flight(blk);
        return -ENOMEDIUM;
    }

    ret = bdrv_co_zone_append(blk_bs(blk), offset, qiov, flags);
    blk_dec_in_flight(blk);
    return ret;
}

void blk_drain(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();

    if (bs) {
        bdrv_ref(bs);
        bdrv_drained_begin(bs);
    }

    /* We may have -ENOMEDIUM completions in flight */
    AIO_WAIT_WHILE(blk_get_aio_context(blk),
                   qatomic_read(&blk->in_flight) > 0);

    if (bs) {
        bdrv_drained_end(bs);
        bdrv_unref(bs);
    }
}

void blk_drain_all(void)
{
    BlockBackend *blk = NULL;

    GLOBAL_STATE_CODE();

    bdrv_drain_all_begin();

    while ((blk = blk_all_next(blk)) != NULL) {
        /* We may have -ENOMEDIUM completions in flight */
        AIO_WAIT_WHILE_UNLOCKED(NULL, qatomic_read(&blk->in_flight) > 0);
    }

    bdrv_drain_all_end();
}

void blk_set_on_error(BlockBackend *blk, BlockdevOnError on_read_error,
                      BlockdevOnError on_write_error)
{
    GLOBAL_STATE_CODE();
    blk->on_read_error = on_read_error;
    blk->on_write_error = on_write_error;
}

BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read)
{
    IO_CODE();
    return is_read ? blk->on_read_error : blk->on_write_error;
}

BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error)
{
    BlockdevOnError on_err = blk_get_on_error(blk, is_read);
    IO_CODE();

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
    case BLOCKDEV_ON_ERROR_AUTO:
    default:
        abort();
    }
}

static void send_qmp_error_event(BlockBackend *blk,
                                 BlockErrorAction action,
                                 bool is_read, int error)
{
    IoOperationType optype;
    BlockDriverState *bs = blk_bs(blk);

    optype = is_read ? IO_OPERATION_TYPE_READ : IO_OPERATION_TYPE_WRITE;
    qapi_event_send_block_io_error(blk_name(blk),
                                   bs ? bdrv_get_node_name(bs) : NULL, optype,
                                   action, blk_iostatus_is_enabled(blk),
                                   error == ENOSPC, strerror(error));
}

/* This is done by device models because, while the block layer knows
 * about the error, it does not know whether an operation comes from
 * the device or the block layer (from a job, for example).
 */
void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error)
{
    assert(error >= 0);
    IO_CODE();

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

/*
 * Returns true if the BlockBackend can support taking write permissions
 * (because its root node is not read-only).
 */
bool blk_supports_write_perm(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();

    if (bs) {
        return !bdrv_is_read_only(bs);
    } else {
        return blk->root_state.open_flags & BDRV_O_RDWR;
    }
}

/*
 * Returns true if the BlockBackend can be written to in its current
 * configuration (i.e. if write permission have been requested)
 */
bool blk_is_writable(BlockBackend *blk)
{
    IO_CODE();
    return blk->perm & BLK_PERM_WRITE;
}

bool blk_is_sg(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();

    if (!bs) {
        return false;
    }

    return bdrv_is_sg(bs);
}

bool blk_enable_write_cache(BlockBackend *blk)
{
    IO_CODE();
    return blk->enable_write_cache;
}

void blk_set_enable_write_cache(BlockBackend *blk, bool wce)
{
    IO_CODE();
    blk->enable_write_cache = wce;
}

bool coroutine_fn blk_co_is_inserted(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    IO_CODE();
    assert_bdrv_graph_readable();

    return bs && bdrv_co_is_inserted(bs);
}

bool coroutine_fn blk_co_is_available(BlockBackend *blk)
{
    IO_CODE();
    return blk_co_is_inserted(blk) && !blk_dev_is_tray_open(blk);
}

void coroutine_fn blk_co_lock_medium(BlockBackend *blk, bool locked)
{
    BlockDriverState *bs = blk_bs(blk);
    IO_CODE();
    GRAPH_RDLOCK_GUARD();

    if (bs) {
        bdrv_co_lock_medium(bs, locked);
    }
}

void coroutine_fn blk_co_eject(BlockBackend *blk, bool eject_flag)
{
    BlockDriverState *bs = blk_bs(blk);
    char *id;
    IO_CODE();
    GRAPH_RDLOCK_GUARD();

    if (bs) {
        bdrv_co_eject(bs, eject_flag);
    }

    /* Whether or not we ejected on the backend,
     * the frontend experienced a tray event. */
    id = blk_get_attached_dev_id(blk);
    qapi_event_send_device_tray_moved(blk_name(blk), id,
                                      eject_flag);
    g_free(id);
}

int blk_get_flags(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();

    if (bs) {
        return bdrv_get_flags(bs);
    } else {
        return blk->root_state.open_flags;
    }
}

/* Returns the minimum request alignment, in bytes; guaranteed nonzero */
uint32_t blk_get_request_alignment(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    IO_CODE();
    return bs ? bs->bl.request_alignment : BDRV_SECTOR_SIZE;
}

/* Returns the maximum hardware transfer length, in bytes; guaranteed nonzero */
uint64_t blk_get_max_hw_transfer(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    uint64_t max = INT_MAX;
    IO_CODE();

    if (bs) {
        max = MIN_NON_ZERO(max, bs->bl.max_hw_transfer);
        max = MIN_NON_ZERO(max, bs->bl.max_transfer);
    }
    return ROUND_DOWN(max, blk_get_request_alignment(blk));
}

/* Returns the maximum transfer length, in bytes; guaranteed nonzero */
uint32_t blk_get_max_transfer(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    uint32_t max = INT_MAX;
    IO_CODE();

    if (bs) {
        max = MIN_NON_ZERO(max, bs->bl.max_transfer);
    }
    return ROUND_DOWN(max, blk_get_request_alignment(blk));
}

int blk_get_max_hw_iov(BlockBackend *blk)
{
    IO_CODE();
    return MIN_NON_ZERO(blk->root->bs->bl.max_hw_iov,
                        blk->root->bs->bl.max_iov);
}

int blk_get_max_iov(BlockBackend *blk)
{
    IO_CODE();
    return blk->root->bs->bl.max_iov;
}

void *blk_try_blockalign(BlockBackend *blk, size_t size)
{
    IO_CODE();
    return qemu_try_blockalign(blk ? blk_bs(blk) : NULL, size);
}

void *blk_blockalign(BlockBackend *blk, size_t size)
{
    IO_CODE();
    return qemu_blockalign(blk ? blk_bs(blk) : NULL, size);
}

bool blk_op_is_blocked(BlockBackend *blk, BlockOpType op, Error **errp)
{
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!bs) {
        return false;
    }

    return bdrv_op_is_blocked(bs, op, errp);
}

/**
 * Return BB's current AioContext.  Note that this context may change
 * concurrently at any time, with one exception: If the BB has a root node
 * attached, its context will only change through bdrv_try_change_aio_context(),
 * which creates a drained section.  Therefore, incrementing such a BB's
 * in-flight counter will prevent its context from changing.
 */
AioContext *blk_get_aio_context(BlockBackend *blk)
{
    IO_CODE();

    if (!blk) {
        return qemu_get_aio_context();
    }

    return qatomic_read(&blk->ctx);
}

int blk_set_aio_context(BlockBackend *blk, AioContext *new_context,
                        Error **errp)
{
    bool old_allow_change;
    BlockDriverState *bs = blk_bs(blk);
    int ret;

    GLOBAL_STATE_CODE();

    if (!bs) {
        qatomic_set(&blk->ctx, new_context);
        return 0;
    }

    bdrv_ref(bs);

    old_allow_change = blk->allow_aio_context_change;
    blk->allow_aio_context_change = true;

    ret = bdrv_try_change_aio_context(bs, new_context, NULL, errp);

    blk->allow_aio_context_change = old_allow_change;

    bdrv_unref(bs);
    return ret;
}

typedef struct BdrvStateBlkRootContext {
    AioContext *new_ctx;
    BlockBackend *blk;
} BdrvStateBlkRootContext;

static void blk_root_set_aio_ctx_commit(void *opaque)
{
    BdrvStateBlkRootContext *s = opaque;
    BlockBackend *blk = s->blk;
    AioContext *new_context = s->new_ctx;
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;

    qatomic_set(&blk->ctx, new_context);
    if (tgm->throttle_state) {
        throttle_group_detach_aio_context(tgm);
        throttle_group_attach_aio_context(tgm, new_context);
    }
}

static TransactionActionDrv set_blk_root_context = {
    .commit = blk_root_set_aio_ctx_commit,
    .clean = g_free,
};

static bool blk_root_change_aio_ctx(BdrvChild *child, AioContext *ctx,
                                    GHashTable *visited, Transaction *tran,
                                    Error **errp)
{
    BlockBackend *blk = child->opaque;
    BdrvStateBlkRootContext *s;

    if (!blk->allow_aio_context_change) {
        /*
         * Manually created BlockBackends (those with a name) that are not
         * attached to anything can change their AioContext without updating
         * their user; return an error for others.
         */
        if (!blk->name || blk->dev) {
            /* TODO Add BB name/QOM path */
            error_setg(errp, "Cannot change iothread of active block backend");
            return false;
        }
    }

    s = g_new(BdrvStateBlkRootContext, 1);
    *s = (BdrvStateBlkRootContext) {
        .new_ctx = ctx,
        .blk = blk,
    };

    tran_add(tran, &set_blk_root_context, s);
    return true;
}

void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque)
{
    BlockBackendAioNotifier *notifier;
    BlockDriverState *bs = blk_bs(blk);
    GLOBAL_STATE_CODE();

    notifier = g_new(BlockBackendAioNotifier, 1);
    notifier->attached_aio_context = attached_aio_context;
    notifier->detach_aio_context = detach_aio_context;
    notifier->opaque = opaque;
    QLIST_INSERT_HEAD(&blk->aio_notifiers, notifier, list);

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
    BlockBackendAioNotifier *notifier;
    BlockDriverState *bs = blk_bs(blk);

    GLOBAL_STATE_CODE();

    if (bs) {
        bdrv_remove_aio_context_notifier(bs, attached_aio_context,
                                         detach_aio_context, opaque);
    }

    QLIST_FOREACH(notifier, &blk->aio_notifiers, list) {
        if (notifier->attached_aio_context == attached_aio_context &&
            notifier->detach_aio_context == detach_aio_context &&
            notifier->opaque == opaque) {
            QLIST_REMOVE(notifier, list);
            g_free(notifier);
            return;
        }
    }

    abort();
}

void blk_add_remove_bs_notifier(BlockBackend *blk, Notifier *notify)
{
    GLOBAL_STATE_CODE();
    notifier_list_add(&blk->remove_bs_notifiers, notify);
}

BlockAcctStats *blk_get_stats(BlockBackend *blk)
{
    IO_CODE();
    return &blk->stats;
}

void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque)
{
    IO_CODE();
    return qemu_aio_get(aiocb_info, blk_bs(blk), cb, opaque);
}

int coroutine_fn blk_co_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                                      int64_t bytes, BdrvRequestFlags flags)
{
    IO_OR_GS_CODE();
    return blk_co_pwritev(blk, offset, bytes, NULL,
                          flags | BDRV_REQ_ZERO_WRITE);
}

int coroutine_fn blk_co_pwrite_compressed(BlockBackend *blk, int64_t offset,
                                          int64_t bytes, const void *buf)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    IO_OR_GS_CODE();
    return blk_co_pwritev_part(blk, offset, bytes, &qiov, 0,
                               BDRV_REQ_WRITE_COMPRESSED);
}

int coroutine_fn blk_co_truncate(BlockBackend *blk, int64_t offset, bool exact,
                                 PreallocMode prealloc, BdrvRequestFlags flags,
                                 Error **errp)
{
    IO_OR_GS_CODE();
    GRAPH_RDLOCK_GUARD();
    if (!blk_co_is_available(blk)) {
        error_setg(errp, "No medium inserted");
        return -ENOMEDIUM;
    }

    return bdrv_co_truncate(blk->root, offset, exact, prealloc, flags, errp);
}

int blk_save_vmstate(BlockBackend *blk, const uint8_t *buf,
                     int64_t pos, int size)
{
    int ret;
    GLOBAL_STATE_CODE();

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
    GLOBAL_STATE_CODE();
    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_load_vmstate(blk_bs(blk), buf, pos, size);
}

int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz)
{
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_probe_blocksizes(blk_bs(blk), bsz);
}

int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo)
{
    GLOBAL_STATE_CODE();
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
    GLOBAL_STATE_CODE();
    assert(blk->root);

    blk->root_state.open_flags    = blk->root->bs->open_flags;
    blk->root_state.detect_zeroes = blk->root->bs->detect_zeroes;
}

/*
 * Returns the detect-zeroes setting to be used for bdrv_open() of a
 * BlockDriverState which is supposed to inherit the root state.
 */
bool blk_get_detect_zeroes_from_root_state(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk->root_state.detect_zeroes;
}

/*
 * Returns the flags to be used for bdrv_open() of a BlockDriverState which is
 * supposed to inherit the root state.
 */
int blk_get_open_flags_from_root_state(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk->root_state.open_flags;
}

BlockBackendRootState *blk_get_root_state(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return &blk->root_state;
}

int blk_commit_all(void)
{
    BlockBackend *blk = NULL;
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    while ((blk = blk_all_next(blk)) != NULL) {
        BlockDriverState *unfiltered_bs = bdrv_skip_filters(blk_bs(blk));

        if (blk_is_inserted(blk) && bdrv_cow_child(unfiltered_bs)) {
            int ret;

            ret = bdrv_commit(unfiltered_bs);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return 0;
}


/* throttling disk I/O limits */
void blk_set_io_limits(BlockBackend *blk, ThrottleConfig *cfg)
{
    GLOBAL_STATE_CODE();
    throttle_group_config(&blk->public.throttle_group_member, cfg);
}

void blk_io_limits_disable(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    assert(tgm->throttle_state);
    GLOBAL_STATE_CODE();
    if (bs) {
        bdrv_ref(bs);
        bdrv_drained_begin(bs);
    }
    throttle_group_unregister_tgm(tgm);
    if (bs) {
        bdrv_drained_end(bs);
        bdrv_unref(bs);
    }
}

/* should be called before blk_set_io_limits if a limit is set */
void blk_io_limits_enable(BlockBackend *blk, const char *group)
{
    assert(!blk->public.throttle_group_member.throttle_state);
    GLOBAL_STATE_CODE();
    throttle_group_register_tgm(&blk->public.throttle_group_member,
                                group, blk_get_aio_context(blk));
}

void blk_io_limits_update_group(BlockBackend *blk, const char *group)
{
    GLOBAL_STATE_CODE();
    /* this BB is not part of any group */
    if (!blk->public.throttle_group_member.throttle_state) {
        return;
    }

    /* this BB is a part of the same group than the one we want */
    if (!g_strcmp0(throttle_group_get_name(&blk->public.throttle_group_member),
                group)) {
        return;
    }

    /* need to change the group this bs belong to */
    blk_io_limits_disable(blk);
    blk_io_limits_enable(blk, group);
}

static void blk_root_drained_begin(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;

    if (qatomic_fetch_inc(&blk->quiesce_counter) == 0) {
        if (blk->dev_ops && blk->dev_ops->drained_begin) {
            blk->dev_ops->drained_begin(blk->dev_opaque);
        }
    }

    /* Note that blk->root may not be accessible here yet if we are just
     * attaching to a BlockDriverState that is drained. Use child instead. */

    if (qatomic_fetch_inc(&tgm->io_limits_disabled) == 0) {
        throttle_group_restart_tgm(tgm);
    }
}

static bool blk_root_drained_poll(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    bool busy = false;
    assert(qatomic_read(&blk->quiesce_counter));

    if (blk->dev_ops && blk->dev_ops->drained_poll) {
        busy = blk->dev_ops->drained_poll(blk->dev_opaque);
    }
    return busy || !!blk->in_flight;
}

static void blk_root_drained_end(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    assert(qatomic_read(&blk->quiesce_counter));

    assert(blk->public.throttle_group_member.io_limits_disabled);
    qatomic_dec(&blk->public.throttle_group_member.io_limits_disabled);

    if (qatomic_fetch_dec(&blk->quiesce_counter) == 1) {
        if (blk->dev_ops && blk->dev_ops->drained_end) {
            blk->dev_ops->drained_end(blk->dev_opaque);
        }
        qemu_mutex_lock(&blk->queued_requests_lock);
        while (qemu_co_enter_next(&blk->queued_requests,
                                  &blk->queued_requests_lock)) {
            /* Resume all queued requests */
        }
        qemu_mutex_unlock(&blk->queued_requests_lock);
    }
}

bool blk_register_buf(BlockBackend *blk, void *host, size_t size, Error **errp)
{
    BlockDriverState *bs = blk_bs(blk);

    GLOBAL_STATE_CODE();

    if (bs) {
        return bdrv_register_buf(bs, host, size, errp);
    }
    return true;
}

void blk_unregister_buf(BlockBackend *blk, void *host, size_t size)
{
    BlockDriverState *bs = blk_bs(blk);

    GLOBAL_STATE_CODE();

    if (bs) {
        bdrv_unregister_buf(bs, host, size);
    }
}

int coroutine_fn blk_co_copy_range(BlockBackend *blk_in, int64_t off_in,
                                   BlockBackend *blk_out, int64_t off_out,
                                   int64_t bytes, BdrvRequestFlags read_flags,
                                   BdrvRequestFlags write_flags)
{
    int r;
    IO_CODE();
    GRAPH_RDLOCK_GUARD();

    r = blk_check_byte_request(blk_in, off_in, bytes);
    if (r) {
        return r;
    }
    r = blk_check_byte_request(blk_out, off_out, bytes);
    if (r) {
        return r;
    }

    return bdrv_co_copy_range(blk_in->root, off_in,
                              blk_out->root, off_out,
                              bytes, read_flags, write_flags);
}

const BdrvChild *blk_root(BlockBackend *blk)
{
    GLOBAL_STATE_CODE();
    return blk->root;
}

int blk_make_empty(BlockBackend *blk, Error **errp)
{
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!blk_is_available(blk)) {
        error_setg(errp, "No medium inserted");
        return -ENOMEDIUM;
    }

    return bdrv_make_empty(blk->root, errp);
}
