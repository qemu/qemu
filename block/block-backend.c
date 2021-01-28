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
#include "hw/qdev-core.h"
#include "sysemu/blockdev.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
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

static AioContext *blk_aiocb_get_aio_context(BlockAIOCB *acb);

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
    AioContext *ctx;
    DriveInfo *legacy_dinfo;    /* null unless created by drive_new() */
    QTAILQ_ENTRY(BlockBackend) link;         /* for block_backends */
    QTAILQ_ENTRY(BlockBackend) monitor_link; /* for monitor_block_backends */
    BlockBackendPublic public;

    DeviceState *dev;           /* attached device model, if any */
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

    uint64_t perm;
    uint64_t shared_perm;
    bool disable_perm;

    bool allow_aio_context_change;
    bool allow_write_beyond_eof;

    NotifierList remove_bs_notifiers, insert_bs_notifiers;
    QLIST_HEAD(, BlockBackendAioNotifier) aio_notifiers;

    int quiesce_counter;
    CoQueue queued_requests;
    bool disable_request_queuing;

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

static void blk_root_inherit_options(BdrvChildRole role, bool parent_is_format,
                                     int *child_flags, QDict *child_options,
                                     int parent_flags, QDict *parent_options)
{
    /* We're not supposed to call this function for root nodes */
    abort();
}
static void blk_root_drained_begin(BdrvChild *child);
static bool blk_root_drained_poll(BdrvChild *child);
static void blk_root_drained_end(BdrvChild *child, int *drained_end_counter);

static void blk_root_change_media(BdrvChild *child, bool load);
static void blk_root_resize(BdrvChild *child);

static bool blk_root_can_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                     GSList **ignore, Error **errp);
static void blk_root_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                 GSList **ignore);

static char *blk_root_get_parent_desc(BdrvChild *child)
{
    BlockBackend *blk = child->opaque;
    char *dev_id;

    if (blk->name) {
        return g_strdup(blk->name);
    }

    dev_id = blk_get_attached_dev_id(blk);
    if (*dev_id) {
        return dev_id;
    } else {
        /* TODO Callback into the BB owner for something more detailed */
        g_free(dev_id);
        return g_strdup("a block device");
    }
}

static const char *blk_root_get_name(BdrvChild *child)
{
    return blk_name(child->opaque);
}

static void blk_vm_state_changed(void *opaque, int running, RunState state)
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
static void blk_root_activate(BdrvChild *child, Error **errp)
{
    BlockBackend *blk = child->opaque;
    Error *local_err = NULL;

    if (!blk->disable_perm) {
        return;
    }

    blk->disable_perm = false;

    blk_set_perm(blk, blk->perm, BLK_PERM_ALL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        blk->disable_perm = true;
        return;
    }

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

    blk_set_perm(blk, blk->perm, blk->shared_perm, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        blk->disable_perm = true;
        return;
    }
}

void blk_set_force_allow_inactivate(BlockBackend *blk)
{
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

static int blk_root_inactivate(BdrvChild *child)
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

    .can_set_aio_ctx    = blk_root_can_set_aio_ctx,
    .set_aio_ctx        = blk_root_set_aio_ctx,
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

    blk = g_new0(BlockBackend, 1);
    blk->refcnt = 1;
    blk->ctx = ctx;
    blk->perm = perm;
    blk->shared_perm = shared_perm;
    blk_set_enable_write_cache(blk, true);

    blk->on_read_error = BLOCKDEV_ON_ERROR_REPORT;
    blk->on_write_error = BLOCKDEV_ON_ERROR_ENOSPC;

    block_acct_init(&blk->stats);

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

    if (blk_insert_bs(blk, bs, errp) < 0) {
        blk_unref(blk);
        return NULL;
    }
    return blk;
}

/*
 * Creates a new BlockBackend, opens a new BlockDriverState, and connects both.
 * The new BlockBackend is in the main AioContext.
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

    /* blk_new_open() is mainly used in .bdrv_create implementations and the
     * tools where sharing isn't a concern because the BDS stays private, so we
     * just request permission according to the flags.
     *
     * The exceptions are xen_disk and blockdev_init(); in these cases, the
     * caller of blk_new_open() doesn't make use of the permissions, but they
     * shouldn't hurt either. We can still share everything here because the
     * guest devices will add their own blockers if they can't share. */
    if ((flags & BDRV_O_NO_IO) == 0) {
        perm |= BLK_PERM_CONSISTENT_READ;
        if (flags & BDRV_O_RDWR) {
            perm |= BLK_PERM_WRITE;
        }
    }
    if (flags & BDRV_O_RESIZE) {
        perm |= BLK_PERM_RESIZE;
    }

    blk = blk_new(qemu_get_aio_context(), perm, BLK_PERM_ALL);
    bs = bdrv_open(filename, reference, options, flags, errp);
    if (!bs) {
        blk_unref(blk);
        return NULL;
    }

    blk->root = bdrv_root_attach_child(bs, "root", &child_root,
                                       BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                                       blk->ctx, perm, BLK_PERM_ALL, blk, errp);
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
    return blk ? blk->refcnt : 0;
}

/*
 * Increment @blk's reference count.
 * @blk must not be null.
 */
void blk_ref(BlockBackend *blk)
{
    assert(blk->refcnt > 0);
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
    BlockDriverState *bs, *old_bs;

    /* Must be called from the main loop */
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    /* First, return all root nodes of BlockBackends. In order to avoid
     * returning a BDS twice when multiple BBs refer to it, we only return it
     * if the BB is the first one in the parent list of the BDS. */
    if (it->phase == BDRV_NEXT_BACKEND_ROOTS) {
        BlockBackend *old_blk = it->blk;

        old_bs = old_blk ? blk_bs(old_blk) : NULL;

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
            return bs;
        }
        it->phase = BDRV_NEXT_MONITOR_OWNED;
    } else {
        old_bs = it->bs;
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
    bdrv_next_reset(it);
    return bdrv_next(it);
}

/* Must be called when aborting a bdrv_next() iteration before
 * bdrv_next() returns NULL */
void bdrv_next_cleanup(BdrvNextIterator *it)
{
    /* Must be called from the main loop */
    assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    if (it->phase == BDRV_NEXT_BACKEND_ROOTS) {
        if (it->blk) {
            bdrv_unref(blk_bs(it->blk));
            blk_unref(it->blk);
        }
    } else {
        bdrv_unref(it->bs);
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
const char *blk_name(const BlockBackend *blk)
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
    return bdrv_first_blk(bs) != NULL;
}

/*
 * Returns true if @bs has only BlockBackends as parents.
 */
bool bdrv_is_root_node(BlockDriverState *bs)
{
    BdrvChild *c;

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
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    BlockDriverState *bs;
    BdrvChild *root;

    notifier_list_notify(&blk->remove_bs_notifiers, blk);
    if (tgm->throttle_state) {
        bs = blk_bs(blk);
        bdrv_drained_begin(bs);
        throttle_group_detach_aio_context(tgm);
        throttle_group_attach_aio_context(tgm, qemu_get_aio_context());
        bdrv_drained_end(bs);
    }

    blk_update_root_state(blk);

    /* bdrv_root_unref_child() will cause blk->root to become stale and may
     * switch to a completion coroutine later on. Let's drain all I/O here
     * to avoid that and a potential QEMU crash.
     */
    blk_drain(blk);
    root = blk->root;
    blk->root = NULL;
    bdrv_root_unref_child(root);
}

/*
 * Associates a new BlockDriverState with @blk.
 */
int blk_insert_bs(BlockBackend *blk, BlockDriverState *bs, Error **errp)
{
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    bdrv_ref(bs);
    blk->root = bdrv_root_attach_child(bs, "root", &child_root,
                                       BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                                       blk->ctx, blk->perm, blk->shared_perm,
                                       blk, errp);
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
 * Sets the permission bitmasks that the user of the BlockBackend needs.
 */
int blk_set_perm(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                 Error **errp)
{
    int ret;

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

void blk_get_perm(BlockBackend *blk, uint64_t *perm, uint64_t *shared_perm)
{
    *perm = blk->perm;
    *shared_perm = blk->shared_perm;
}

/*
 * Attach device model @dev to @blk.
 * Return 0 on success, -EBUSY when a device model is attached already.
 */
int blk_attach_dev(BlockBackend *blk, DeviceState *dev)
{
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
    blk->dev = NULL;
    blk->dev_ops = NULL;
    blk->dev_opaque = NULL;
    blk->guest_block_size = 512;
    blk_set_perm(blk, 0, BLK_PERM_ALL, &error_abort);
    blk_unref(blk);
}

/*
 * Return the device model attached to @blk if any, else null.
 */
DeviceState *blk_get_attached_dev(BlockBackend *blk)
{
    return blk->dev;
}

/* Return the qdev ID, or if no ID is assigned the QOM path, of the block
 * device attached to the BlockBackend. */
char *blk_get_attached_dev_id(BlockBackend *blk)
{
    DeviceState *dev = blk->dev;

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
    blk->dev_ops = ops;
    blk->dev_opaque = opaque;

    /* Are we currently quiesced? Should we enforce this right now? */
    if (blk->quiesce_counter && ops->drained_begin) {
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
        blk->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
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

void blk_set_allow_aio_context_change(BlockBackend *blk, bool allow)
{
    blk->allow_aio_context_change = allow;
}

void blk_set_disable_request_queuing(BlockBackend *blk, bool disable)
{
    blk->disable_request_queuing = disable;
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

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static void coroutine_fn blk_wait_while_drained(BlockBackend *blk)
{
    assert(blk->in_flight > 0);

    if (blk->quiesce_counter && !blk->disable_request_queuing) {
        blk_dec_in_flight(blk);
        qemu_co_queue_wait(&blk->queued_requests, NULL);
        blk_inc_in_flight(blk);
    }
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_do_preadv(BlockBackend *blk, int64_t offset, unsigned int bytes,
              QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;
    BlockDriverState *bs;

    blk_wait_while_drained(blk);

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
                bytes, false);
    }

    ret = bdrv_co_preadv(blk->root, offset, bytes, qiov, flags);
    bdrv_dec_in_flight(bs);
    return ret;
}

int coroutine_fn blk_co_preadv(BlockBackend *blk, int64_t offset,
                               unsigned int bytes, QEMUIOVector *qiov,
                               BdrvRequestFlags flags)
{
    int ret;

    blk_inc_in_flight(blk);
    ret = blk_do_preadv(blk, offset, bytes, qiov, flags);
    blk_dec_in_flight(blk);

    return ret;
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_do_pwritev_part(BlockBackend *blk, int64_t offset, unsigned int bytes,
                    QEMUIOVector *qiov, size_t qiov_offset,
                    BdrvRequestFlags flags)
{
    int ret;
    BlockDriverState *bs;

    blk_wait_while_drained(blk);

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
                bytes, true);
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
                                     unsigned int bytes,
                                     QEMUIOVector *qiov, size_t qiov_offset,
                                     BdrvRequestFlags flags)
{
    int ret;

    blk_inc_in_flight(blk);
    ret = blk_do_pwritev_part(blk, offset, bytes, qiov, qiov_offset, flags);
    blk_dec_in_flight(blk);

    return ret;
}

int coroutine_fn blk_co_pwritev(BlockBackend *blk, int64_t offset,
                                unsigned int bytes, QEMUIOVector *qiov,
                                BdrvRequestFlags flags)
{
    return blk_co_pwritev_part(blk, offset, bytes, qiov, 0, flags);
}

typedef struct BlkRwCo {
    BlockBackend *blk;
    int64_t offset;
    void *iobuf;
    int ret;
    BdrvRequestFlags flags;
} BlkRwCo;

static void blk_read_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_do_preadv(rwco->blk, rwco->offset, qiov->size,
                              qiov, rwco->flags);
    aio_wait_kick();
}

static void blk_write_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_do_pwritev_part(rwco->blk, rwco->offset, qiov->size,
                                    qiov, 0, rwco->flags);
    aio_wait_kick();
}

static int blk_prw(BlockBackend *blk, int64_t offset, uint8_t *buf,
                   int64_t bytes, CoroutineEntry co_entry,
                   BdrvRequestFlags flags)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    BlkRwCo rwco = {
        .blk    = blk,
        .offset = offset,
        .iobuf  = &qiov,
        .flags  = flags,
        .ret    = NOT_DONE,
    };

    blk_inc_in_flight(blk);
    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        co_entry(&rwco);
    } else {
        Coroutine *co = qemu_coroutine_create(co_entry, &rwco);
        bdrv_coroutine_enter(blk_bs(blk), co);
        BDRV_POLL_WHILE(blk_bs(blk), rwco.ret == NOT_DONE);
    }
    blk_dec_in_flight(blk);

    return rwco.ret;
}

int blk_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                      int bytes, BdrvRequestFlags flags)
{
    return blk_prw(blk, offset, NULL, bytes, blk_write_entry,
                   flags | BDRV_REQ_ZERO_WRITE);
}

int blk_make_zero(BlockBackend *blk, BdrvRequestFlags flags)
{
    return bdrv_make_zero(blk->root, flags);
}

void blk_inc_in_flight(BlockBackend *blk)
{
    qatomic_inc(&blk->in_flight);
}

void blk_dec_in_flight(BlockBackend *blk)
{
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

    blk_inc_in_flight(blk);
    acb = blk_aio_get(&block_backend_aiocb_info, blk, cb, opaque);
    acb->blk = blk;
    acb->ret = ret;

    replay_bh_schedule_oneshot_event(blk_get_aio_context(blk),
                                     error_callback_bh, acb);
    return &acb->common;
}

typedef struct BlkAioEmAIOCB {
    BlockAIOCB common;
    BlkRwCo rwco;
    int bytes;
    bool has_returned;
} BlkAioEmAIOCB;

static AioContext *blk_aio_em_aiocb_get_aio_context(BlockAIOCB *acb_)
{
    BlkAioEmAIOCB *acb = container_of(acb_, BlkAioEmAIOCB, common);

    return blk_get_aio_context(acb->rwco.blk);
}

static const AIOCBInfo blk_aio_em_aiocb_info = {
    .aiocb_size         = sizeof(BlkAioEmAIOCB),
    .get_aio_context    = blk_aio_em_aiocb_get_aio_context,
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

static BlockAIOCB *blk_aio_prwv(BlockBackend *blk, int64_t offset, int bytes,
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
    bdrv_coroutine_enter(blk_bs(blk), co);

    acb->has_returned = true;
    if (acb->rwco.ret != NOT_DONE) {
        replay_bh_schedule_oneshot_event(blk_get_aio_context(blk),
                                         blk_aio_complete_bh, acb);
    }

    return &acb->common;
}

static void blk_aio_read_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;
    QEMUIOVector *qiov = rwco->iobuf;

    assert(qiov->size == acb->bytes);
    rwco->ret = blk_do_preadv(rwco->blk, rwco->offset, acb->bytes,
                              qiov, rwco->flags);
    blk_aio_complete(acb);
}

static void blk_aio_write_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;
    QEMUIOVector *qiov = rwco->iobuf;

    assert(!qiov || qiov->size == acb->bytes);
    rwco->ret = blk_do_pwritev_part(rwco->blk, rwco->offset, acb->bytes,
                                    qiov, 0, rwco->flags);
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

void blk_aio_cancel(BlockAIOCB *acb)
{
    bdrv_aio_cancel(acb);
}

void blk_aio_cancel_async(BlockAIOCB *acb)
{
    bdrv_aio_cancel_async(acb);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_do_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    blk_wait_while_drained(blk);

    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_ioctl(blk_bs(blk), req, buf);
}

static void blk_ioctl_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_do_ioctl(rwco->blk, rwco->offset, qiov->iov[0].iov_base);
    aio_wait_kick();
}

int blk_ioctl(BlockBackend *blk, unsigned long int req, void *buf)
{
    return blk_prw(blk, req, buf, 0, blk_ioctl_entry, 0);
}

static void blk_aio_ioctl_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_do_ioctl(rwco->blk, rwco->offset, rwco->iobuf);

    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_ioctl(BlockBackend *blk, unsigned long int req, void *buf,
                          BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, req, 0, buf, blk_aio_ioctl_entry, 0, cb, opaque);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn
blk_do_pdiscard(BlockBackend *blk, int64_t offset, int bytes)
{
    int ret;

    blk_wait_while_drained(blk);

    ret = blk_check_byte_request(blk, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(blk->root, offset, bytes);
}

static void blk_aio_pdiscard_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_do_pdiscard(rwco->blk, rwco->offset, acb->bytes);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_pdiscard(BlockBackend *blk,
                             int64_t offset, int bytes,
                             BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, offset, bytes, NULL, blk_aio_pdiscard_entry, 0,
                        cb, opaque);
}

int coroutine_fn blk_co_pdiscard(BlockBackend *blk, int64_t offset, int bytes)
{
    int ret;

    blk_inc_in_flight(blk);
    ret = blk_do_pdiscard(blk, offset, bytes);
    blk_dec_in_flight(blk);

    return ret;
}

static void blk_pdiscard_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_do_pdiscard(rwco->blk, rwco->offset, qiov->size);
    aio_wait_kick();
}

int blk_pdiscard(BlockBackend *blk, int64_t offset, int bytes)
{
    return blk_prw(blk, offset, NULL, bytes, blk_pdiscard_entry, 0);
}

/* To be called between exactly one pair of blk_inc/dec_in_flight() */
static int coroutine_fn blk_do_flush(BlockBackend *blk)
{
    blk_wait_while_drained(blk);

    if (!blk_is_available(blk)) {
        return -ENOMEDIUM;
    }

    return bdrv_co_flush(blk_bs(blk));
}

static void blk_aio_flush_entry(void *opaque)
{
    BlkAioEmAIOCB *acb = opaque;
    BlkRwCo *rwco = &acb->rwco;

    rwco->ret = blk_do_flush(rwco->blk);
    blk_aio_complete(acb);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque)
{
    return blk_aio_prwv(blk, 0, 0, NULL, blk_aio_flush_entry, 0, cb, opaque);
}

int coroutine_fn blk_co_flush(BlockBackend *blk)
{
    int ret;

    blk_inc_in_flight(blk);
    ret = blk_do_flush(blk);
    blk_dec_in_flight(blk);

    return ret;
}

static void blk_flush_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    rwco->ret = blk_do_flush(rwco->blk);
    aio_wait_kick();
}

int blk_flush(BlockBackend *blk)
{
    return blk_prw(blk, 0, NULL, 0, blk_flush_entry, 0);
}

void blk_drain(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (bs) {
        bdrv_drained_begin(bs);
    }

    /* We may have -ENOMEDIUM completions in flight */
    AIO_WAIT_WHILE(blk_get_aio_context(blk),
                   qatomic_mb_read(&blk->in_flight) > 0);

    if (bs) {
        bdrv_drained_end(bs);
    }
}

void blk_drain_all(void)
{
    BlockBackend *blk = NULL;

    bdrv_drain_all_begin();

    while ((blk = blk_all_next(blk)) != NULL) {
        AioContext *ctx = blk_get_aio_context(blk);

        aio_context_acquire(ctx);

        /* We may have -ENOMEDIUM completions in flight */
        AIO_WAIT_WHILE(ctx, qatomic_mb_read(&blk->in_flight) > 0);

        aio_context_release(ctx);
    }

    bdrv_drain_all_end();
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
    qapi_event_send_block_io_error(blk_name(blk), !!bs,
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

    if (bs) {
        return !bdrv_is_read_only(bs);
    } else {
        return !blk->root_state.read_only;
    }
}

/*
 * Returns true if the BlockBackend can be written to in its current
 * configuration (i.e. if write permission have been requested)
 */
bool blk_is_writable(BlockBackend *blk)
{
    return blk->perm & BLK_PERM_WRITE;
}

bool blk_is_sg(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);

    if (!bs) {
        return false;
    }

    return bdrv_is_sg(bs);
}

bool blk_enable_write_cache(BlockBackend *blk)
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
    char *id;

    if (bs) {
        bdrv_eject(bs, eject_flag);
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
    return bs ? bs->bl.request_alignment : BDRV_SECTOR_SIZE;
}

/* Returns the maximum transfer length, in bytes; guaranteed nonzero */
uint32_t blk_get_max_transfer(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    uint32_t max = 0;

    if (bs) {
        max = bs->bl.max_transfer;
    }
    return MIN_NON_ZERO(max, INT_MAX);
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
        AioContext *ctx = bdrv_get_aio_context(blk_bs(blk));
        assert(ctx == blk->ctx);
    }

    return blk->ctx;
}

static AioContext *blk_aiocb_get_aio_context(BlockAIOCB *acb)
{
    BlockBackendAIOCB *blk_acb = DO_UPCAST(BlockBackendAIOCB, common, acb);
    return blk_get_aio_context(blk_acb->blk);
}

static int blk_do_set_aio_context(BlockBackend *blk, AioContext *new_context,
                                  bool update_root_node, Error **errp)
{
    BlockDriverState *bs = blk_bs(blk);
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    int ret;

    if (bs) {
        if (update_root_node) {
            ret = bdrv_child_try_set_aio_context(bs, new_context, blk->root,
                                                 errp);
            if (ret < 0) {
                return ret;
            }
        }
        if (tgm->throttle_state) {
            bdrv_drained_begin(bs);
            throttle_group_detach_aio_context(tgm);
            throttle_group_attach_aio_context(tgm, new_context);
            bdrv_drained_end(bs);
        }
    }

    blk->ctx = new_context;
    return 0;
}

int blk_set_aio_context(BlockBackend *blk, AioContext *new_context,
                        Error **errp)
{
    return blk_do_set_aio_context(blk, new_context, true, errp);
}

static bool blk_root_can_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                     GSList **ignore, Error **errp)
{
    BlockBackend *blk = child->opaque;

    if (blk->allow_aio_context_change) {
        return true;
    }

    /* Only manually created BlockBackends that are not attached to anything
     * can change their AioContext without updating their user. */
    if (!blk->name || blk->dev) {
        /* TODO Add BB name/QOM path */
        error_setg(errp, "Cannot change iothread of active block backend");
        return false;
    }

    return true;
}

static void blk_root_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                 GSList **ignore)
{
    BlockBackend *blk = child->opaque;
    blk_do_set_aio_context(blk, ctx, false, &error_abort);
}

void blk_add_aio_context_notifier(BlockBackend *blk,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque)
{
    BlockBackendAioNotifier *notifier;
    BlockDriverState *bs = blk_bs(blk);

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
                                      int bytes, BdrvRequestFlags flags)
{
    return blk_co_pwritev(blk, offset, bytes, NULL,
                          flags | BDRV_REQ_ZERO_WRITE);
}

int blk_pwrite_compressed(BlockBackend *blk, int64_t offset, const void *buf,
                          int count)
{
    return blk_prw(blk, offset, (void *) buf, count, blk_write_entry,
                   BDRV_REQ_WRITE_COMPRESSED);
}

int blk_truncate(BlockBackend *blk, int64_t offset, bool exact,
                 PreallocMode prealloc, BdrvRequestFlags flags, Error **errp)
{
    if (!blk_is_available(blk)) {
        error_setg(errp, "No medium inserted");
        return -ENOMEDIUM;
    }

    return bdrv_truncate(blk->root, offset, exact, prealloc, flags, errp);
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
 * Returns the detect-zeroes setting to be used for bdrv_open() of a
 * BlockDriverState which is supposed to inherit the root state.
 */
bool blk_get_detect_zeroes_from_root_state(BlockBackend *blk)
{
    return blk->root_state.detect_zeroes;
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
        BlockDriverState *unfiltered_bs = bdrv_skip_filters(blk_bs(blk));

        aio_context_acquire(aio_context);
        if (blk_is_inserted(blk) && bdrv_cow_child(unfiltered_bs)) {
            int ret;

            ret = bdrv_commit(unfiltered_bs);
            if (ret < 0) {
                aio_context_release(aio_context);
                return ret;
            }
        }
        aio_context_release(aio_context);
    }
    return 0;
}


/* throttling disk I/O limits */
void blk_set_io_limits(BlockBackend *blk, ThrottleConfig *cfg)
{
    throttle_group_config(&blk->public.throttle_group_member, cfg);
}

void blk_io_limits_disable(BlockBackend *blk)
{
    BlockDriverState *bs = blk_bs(blk);
    ThrottleGroupMember *tgm = &blk->public.throttle_group_member;
    assert(tgm->throttle_state);
    if (bs) {
        bdrv_drained_begin(bs);
    }
    throttle_group_unregister_tgm(tgm);
    if (bs) {
        bdrv_drained_end(bs);
    }
}

/* should be called before blk_set_io_limits if a limit is set */
void blk_io_limits_enable(BlockBackend *blk, const char *group)
{
    assert(!blk->public.throttle_group_member.throttle_state);
    throttle_group_register_tgm(&blk->public.throttle_group_member,
                                group, blk_get_aio_context(blk));
}

void blk_io_limits_update_group(BlockBackend *blk, const char *group)
{
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

    if (++blk->quiesce_counter == 1) {
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
    assert(blk->quiesce_counter);
    return !!blk->in_flight;
}

static void blk_root_drained_end(BdrvChild *child, int *drained_end_counter)
{
    BlockBackend *blk = child->opaque;
    assert(blk->quiesce_counter);

    assert(blk->public.throttle_group_member.io_limits_disabled);
    qatomic_dec(&blk->public.throttle_group_member.io_limits_disabled);

    if (--blk->quiesce_counter == 0) {
        if (blk->dev_ops && blk->dev_ops->drained_end) {
            blk->dev_ops->drained_end(blk->dev_opaque);
        }
        while (qemu_co_enter_next(&blk->queued_requests, NULL)) {
            /* Resume all queued requests */
        }
    }
}

void blk_register_buf(BlockBackend *blk, void *host, size_t size)
{
    bdrv_register_buf(blk_bs(blk), host, size);
}

void blk_unregister_buf(BlockBackend *blk, void *host)
{
    bdrv_unregister_buf(blk_bs(blk), host);
}

int coroutine_fn blk_co_copy_range(BlockBackend *blk_in, int64_t off_in,
                                   BlockBackend *blk_out, int64_t off_out,
                                   int bytes, BdrvRequestFlags read_flags,
                                   BdrvRequestFlags write_flags)
{
    int r;
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
    return blk->root;
}

int blk_make_empty(BlockBackend *blk, Error **errp)
{
    if (!blk_is_available(blk)) {
        error_setg(errp, "No medium inserted");
        return -ENOMEDIUM;
    }

    return bdrv_make_empty(blk->root, errp);
}
