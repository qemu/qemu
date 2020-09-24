/*
 * Common block export infrastructure
 *
 * Copyright (c) 2012, 2020 Red Hat, Inc.
 *
 * Authors:
 * Paolo Bonzini <pbonzini@redhat.com>
 * Kevin Wolf <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "block/block.h"
#include "sysemu/block-backend.h"
#include "block/export.h"
#include "block/nbd.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-export.h"
#include "qapi/qapi-events-block-export.h"
#include "qemu/id.h"

static const BlockExportDriver *blk_exp_drivers[] = {
    &blk_exp_nbd,
};

/* Only accessed from the main thread */
static QLIST_HEAD(, BlockExport) block_exports =
    QLIST_HEAD_INITIALIZER(block_exports);

BlockExport *blk_exp_find(const char *id)
{
    BlockExport *exp;

    QLIST_FOREACH(exp, &block_exports, next) {
        if (strcmp(id, exp->id) == 0) {
            return exp;
        }
    }

    return NULL;
}

static const BlockExportDriver *blk_exp_find_driver(BlockExportType type)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(blk_exp_drivers); i++) {
        if (blk_exp_drivers[i]->type == type) {
            return blk_exp_drivers[i];
        }
    }
    return NULL;
}

BlockExport *blk_exp_add(BlockExportOptions *export, Error **errp)
{
    const BlockExportDriver *drv;
    BlockExport *exp = NULL;
    BlockDriverState *bs;
    BlockBackend *blk;
    AioContext *ctx;
    int ret;

    if (!id_wellformed(export->id)) {
        error_setg(errp, "Invalid block export id");
        return NULL;
    }
    if (blk_exp_find(export->id)) {
        error_setg(errp, "Block export id '%s' is already in use", export->id);
        return NULL;
    }

    drv = blk_exp_find_driver(export->type);
    if (!drv) {
        error_setg(errp, "No driver found for the requested export type");
        return NULL;
    }

    bs = bdrv_lookup_bs(NULL, export->node_name, errp);
    if (!bs) {
        return NULL;
    }

    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);

    /*
     * Block exports are used for non-shared storage migration. Make sure
     * that BDRV_O_INACTIVE is cleared and the image is ready for write
     * access since the export could be available before migration handover.
     * ctx was acquired in the caller.
     */
    bdrv_invalidate_cache(bs, NULL);

    blk = blk_new(ctx, BLK_PERM_CONSISTENT_READ, BLK_PERM_ALL);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto fail;
    }

    if (!export->has_writethrough) {
        export->writethrough = false;
    }
    blk_set_enable_write_cache(blk, !export->writethrough);

    assert(drv->instance_size >= sizeof(BlockExport));
    exp = g_malloc0(drv->instance_size);
    *exp = (BlockExport) {
        .drv        = drv,
        .refcount   = 1,
        .user_owned = true,
        .id         = g_strdup(export->id),
        .ctx        = ctx,
        .blk        = blk,
    };

    ret = drv->create(exp, export, errp);
    if (ret < 0) {
        goto fail;
    }

    assert(exp->blk != NULL);

    QLIST_INSERT_HEAD(&block_exports, exp, next);

    aio_context_release(ctx);
    return exp;

fail:
    blk_unref(blk);
    aio_context_release(ctx);
    if (exp) {
        g_free(exp->id);
        g_free(exp);
    }
    return NULL;
}

/* Callers must hold exp->ctx lock */
void blk_exp_ref(BlockExport *exp)
{
    assert(exp->refcount > 0);
    exp->refcount++;
}

/* Runs in the main thread */
static void blk_exp_delete_bh(void *opaque)
{
    BlockExport *exp = opaque;
    AioContext *aio_context = exp->ctx;

    aio_context_acquire(aio_context);

    assert(exp->refcount == 0);
    QLIST_REMOVE(exp, next);
    exp->drv->delete(exp);
    blk_unref(exp->blk);
    qapi_event_send_block_export_deleted(exp->id);
    g_free(exp->id);
    g_free(exp);

    aio_context_release(aio_context);
}

/* Callers must hold exp->ctx lock */
void blk_exp_unref(BlockExport *exp)
{
    assert(exp->refcount > 0);
    if (--exp->refcount == 0) {
        /* Touch the block_exports list only in the main thread */
        aio_bh_schedule_oneshot(qemu_get_aio_context(), blk_exp_delete_bh,
                                exp);
    }
}

/*
 * Drops the user reference to the export and requests that all client
 * connections and other internally held references start to shut down. When
 * the function returns, there may still be active references while the export
 * is in the process of shutting down.
 *
 * Acquires exp->ctx internally. Callers must *not* hold the lock.
 */
void blk_exp_request_shutdown(BlockExport *exp)
{
    AioContext *aio_context = exp->ctx;

    aio_context_acquire(aio_context);

    /*
     * If the user doesn't own the export any more, it is already shutting
     * down. We must not call .request_shutdown and decrease the refcount a
     * second time.
     */
    if (!exp->user_owned) {
        goto out;
    }

    exp->drv->request_shutdown(exp);

    assert(exp->user_owned);
    exp->user_owned = false;
    blk_exp_unref(exp);

out:
    aio_context_release(aio_context);
}

/*
 * Returns whether a block export of the given type exists.
 * type == BLOCK_EXPORT_TYPE__MAX checks for an export of any type.
 */
static bool blk_exp_has_type(BlockExportType type)
{
    BlockExport *exp;

    if (type == BLOCK_EXPORT_TYPE__MAX) {
        return !QLIST_EMPTY(&block_exports);
    }

    QLIST_FOREACH(exp, &block_exports, next) {
        if (exp->drv->type == type) {
            return true;
        }
    }

    return false;
}

/* type == BLOCK_EXPORT_TYPE__MAX for all types */
void blk_exp_close_all_type(BlockExportType type)
{
    BlockExport *exp, *next;

    assert(in_aio_context_home_thread(qemu_get_aio_context()));

    QLIST_FOREACH_SAFE(exp, &block_exports, next, next) {
        if (type != BLOCK_EXPORT_TYPE__MAX && exp->drv->type != type) {
            continue;
        }
        blk_exp_request_shutdown(exp);
    }

    AIO_WAIT_WHILE(NULL, blk_exp_has_type(type));
}

void blk_exp_close_all(void)
{
    blk_exp_close_all_type(BLOCK_EXPORT_TYPE__MAX);
}

void qmp_block_export_add(BlockExportOptions *export, Error **errp)
{
    blk_exp_add(export, errp);
}

void qmp_block_export_del(const char *id,
                          bool has_mode, BlockExportRemoveMode mode,
                          Error **errp)
{
    ERRP_GUARD();
    BlockExport *exp;

    exp = blk_exp_find(id);
    if (exp == NULL) {
        error_setg(errp, "Export '%s' is not found", id);
        return;
    }
    if (!exp->user_owned) {
        error_setg(errp, "Export '%s' is already shutting down", id);
        return;
    }

    if (!has_mode) {
        mode = BLOCK_EXPORT_REMOVE_MODE_SAFE;
    }
    if (mode == BLOCK_EXPORT_REMOVE_MODE_SAFE && exp->refcount > 1) {
        error_setg(errp, "export '%s' still in use", exp->id);
        error_append_hint(errp, "Use mode='hard' to force client "
                          "disconnect\n");
        return;
    }

    blk_exp_request_shutdown(exp);
}
