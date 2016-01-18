/*
 * QEMU System Emulator block write threshold notification
 *
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Francesco Romani <fromani@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qemu/coroutine.h"
#include "block/write-threshold.h"
#include "qemu/notify.h"
#include "qapi-event.h"
#include "qmp-commands.h"


uint64_t bdrv_write_threshold_get(const BlockDriverState *bs)
{
    return bs->write_threshold_offset;
}

bool bdrv_write_threshold_is_set(const BlockDriverState *bs)
{
    return bs->write_threshold_offset > 0;
}

static void write_threshold_disable(BlockDriverState *bs)
{
    if (bdrv_write_threshold_is_set(bs)) {
        notifier_with_return_remove(&bs->write_threshold_notifier);
        bs->write_threshold_offset = 0;
    }
}

uint64_t bdrv_write_threshold_exceeded(const BlockDriverState *bs,
                                       const BdrvTrackedRequest *req)
{
    if (bdrv_write_threshold_is_set(bs)) {
        if (req->offset > bs->write_threshold_offset) {
            return (req->offset - bs->write_threshold_offset) + req->bytes;
        }
        if ((req->offset + req->bytes) > bs->write_threshold_offset) {
            return (req->offset + req->bytes) - bs->write_threshold_offset;
        }
    }
    return 0;
}

static int coroutine_fn before_write_notify(NotifierWithReturn *notifier,
                                            void *opaque)
{
    BdrvTrackedRequest *req = opaque;
    BlockDriverState *bs = req->bs;
    uint64_t amount = 0;

    amount = bdrv_write_threshold_exceeded(bs, req);
    if (amount > 0) {
        qapi_event_send_block_write_threshold(
            bs->node_name,
            amount,
            bs->write_threshold_offset,
            &error_abort);

        /* autodisable to avoid flooding the monitor */
        write_threshold_disable(bs);
    }

    return 0; /* should always let other notifiers run */
}

static void write_threshold_register_notifier(BlockDriverState *bs)
{
    bs->write_threshold_notifier.notify = before_write_notify;
    notifier_with_return_list_add(&bs->before_write_notifiers,
                                  &bs->write_threshold_notifier);
}

static void write_threshold_update(BlockDriverState *bs,
                                   int64_t threshold_bytes)
{
    bs->write_threshold_offset = threshold_bytes;
}

void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes)
{
    if (bdrv_write_threshold_is_set(bs)) {
        if (threshold_bytes > 0) {
            write_threshold_update(bs, threshold_bytes);
        } else {
            write_threshold_disable(bs);
        }
    } else {
        if (threshold_bytes > 0) {
            /* avoid multiple registration */
            write_threshold_register_notifier(bs);
            write_threshold_update(bs, threshold_bytes);
        }
        /* discard bogus disable request */
    }
}

void qmp_block_set_write_threshold(const char *node_name,
                                   uint64_t threshold_bytes,
                                   Error **errp)
{
    BlockDriverState *bs;
    AioContext *aio_context;

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Device '%s' not found", node_name);
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    bdrv_write_threshold_set(bs, threshold_bytes);

    aio_context_release(aio_context);
}
