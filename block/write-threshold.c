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
#include "block/block-io.h"
#include "block/block_int.h"
#include "block/write-threshold.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-events-block-core.h"

uint64_t bdrv_write_threshold_get(const BlockDriverState *bs)
{
    return bs->write_threshold_offset;
}

void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes)
{
    bs->write_threshold_offset = threshold_bytes;
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

void bdrv_write_threshold_check_write(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes)
{
    int64_t end = offset + bytes;
    uint64_t wtr = bs->write_threshold_offset;

    if (wtr > 0 && end > wtr) {
        qapi_event_send_block_write_threshold(bs->node_name, end - wtr, wtr);

        /* autodisable to avoid flooding the monitor */
        bdrv_write_threshold_set(bs, 0);
    }
}
