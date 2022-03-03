/*
 * QEMU block dirty bitmap QMP commands
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "block/block_int.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/error.h"

/**
 * block_dirty_bitmap_lookup:
 * Return a dirty bitmap (if present), after validating
 * the node reference and bitmap names.
 *
 * @node: The name of the BDS node to search for bitmaps
 * @name: The name of the bitmap to search for
 * @pbs: Output pointer for BDS lookup, if desired. Can be NULL.
 * @errp: Output pointer for error information. Can be NULL.
 *
 * @return: A bitmap object on success, or NULL on failure.
 */
BdrvDirtyBitmap *block_dirty_bitmap_lookup(const char *node,
                                           const char *name,
                                           BlockDriverState **pbs,
                                           Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;

    GLOBAL_STATE_CODE();

    if (!node) {
        error_setg(errp, "Node cannot be NULL");
        return NULL;
    }
    if (!name) {
        error_setg(errp, "Bitmap name cannot be NULL");
        return NULL;
    }
    bs = bdrv_lookup_bs(node, node, NULL);
    if (!bs) {
        error_setg(errp, "Node '%s' not found", node);
        return NULL;
    }

    bitmap = bdrv_find_dirty_bitmap(bs, name);
    if (!bitmap) {
        error_setg(errp, "Dirty bitmap '%s' not found", name);
        return NULL;
    }

    if (pbs) {
        *pbs = bs;
    }

    return bitmap;
}

void qmp_block_dirty_bitmap_add(const char *node, const char *name,
                                bool has_granularity, uint32_t granularity,
                                bool has_persistent, bool persistent,
                                bool has_disabled, bool disabled,
                                Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    AioContext *aio_context;

    if (!name || name[0] == '\0') {
        error_setg(errp, "Bitmap name cannot be empty");
        return;
    }

    bs = bdrv_lookup_bs(node, node, errp);
    if (!bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (has_granularity) {
        if (granularity < 512 || !is_power_of_2(granularity)) {
            error_setg(errp, "Granularity must be power of 2 "
                             "and at least 512");
            goto out;
        }
    } else {
        /* Default to cluster size, if available: */
        granularity = bdrv_get_default_bitmap_granularity(bs);
    }

    if (!has_persistent) {
        persistent = false;
    }

    if (!has_disabled) {
        disabled = false;
    }

    if (persistent &&
        !bdrv_can_store_new_dirty_bitmap(bs, name, granularity, errp))
    {
        goto out;
    }

    bitmap = bdrv_create_dirty_bitmap(bs, granularity, name, errp);
    if (bitmap == NULL) {
        goto out;
    }

    if (disabled) {
        bdrv_disable_dirty_bitmap(bitmap);
    }

    bdrv_dirty_bitmap_set_persistence(bitmap, persistent);

out:
    aio_context_release(aio_context);
}

BdrvDirtyBitmap *block_dirty_bitmap_remove(const char *node, const char *name,
                                           bool release,
                                           BlockDriverState **bitmap_bs,
                                           Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    AioContext *aio_context;

    GLOBAL_STATE_CODE();

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, errp);
    if (!bitmap || !bs) {
        return NULL;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_BUSY | BDRV_BITMAP_RO,
                                errp)) {
        aio_context_release(aio_context);
        return NULL;
    }

    if (bdrv_dirty_bitmap_get_persistence(bitmap) &&
        bdrv_remove_persistent_dirty_bitmap(bs, name, errp) < 0)
    {
        aio_context_release(aio_context);
        return NULL;
    }

    if (release) {
        bdrv_release_dirty_bitmap(bitmap);
    }

    if (bitmap_bs) {
        *bitmap_bs = bs;
    }

    aio_context_release(aio_context);
    return release ? NULL : bitmap;
}

void qmp_block_dirty_bitmap_remove(const char *node, const char *name,
                                   Error **errp)
{
    block_dirty_bitmap_remove(node, name, true, NULL, errp);
}

/**
 * Completely clear a bitmap, for the purposes of synchronizing a bitmap
 * immediately after a full backup operation.
 */
void qmp_block_dirty_bitmap_clear(const char *node, const char *name,
                                  Error **errp)
{
    BdrvDirtyBitmap *bitmap;
    BlockDriverState *bs;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, errp);
    if (!bitmap || !bs) {
        return;
    }

    if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_DEFAULT, errp)) {
        return;
    }

    bdrv_clear_dirty_bitmap(bitmap, NULL);
}

void qmp_block_dirty_bitmap_enable(const char *node, const char *name,
                                   Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, errp);
    if (!bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_ALLOW_RO, errp)) {
        return;
    }

    bdrv_enable_dirty_bitmap(bitmap);
}

void qmp_block_dirty_bitmap_disable(const char *node, const char *name,
                                    Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, errp);
    if (!bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_ALLOW_RO, errp)) {
        return;
    }

    bdrv_disable_dirty_bitmap(bitmap);
}

BdrvDirtyBitmap *block_dirty_bitmap_merge(const char *node, const char *target,
                                          BlockDirtyBitmapMergeSourceList *bms,
                                          HBitmap **backup, Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *dst, *src, *anon;
    BlockDirtyBitmapMergeSourceList *lst;

    GLOBAL_STATE_CODE();

    dst = block_dirty_bitmap_lookup(node, target, &bs, errp);
    if (!dst) {
        return NULL;
    }

    anon = bdrv_create_dirty_bitmap(bs, bdrv_dirty_bitmap_granularity(dst),
                                    NULL, errp);
    if (!anon) {
        return NULL;
    }

    for (lst = bms; lst; lst = lst->next) {
        switch (lst->value->type) {
            const char *name, *node;
        case QTYPE_QSTRING:
            name = lst->value->u.local;
            src = bdrv_find_dirty_bitmap(bs, name);
            if (!src) {
                error_setg(errp, "Dirty bitmap '%s' not found", name);
                dst = NULL;
                goto out;
            }
            break;
        case QTYPE_QDICT:
            node = lst->value->u.external.node;
            name = lst->value->u.external.name;
            src = block_dirty_bitmap_lookup(node, name, NULL, errp);
            if (!src) {
                dst = NULL;
                goto out;
            }
            break;
        default:
            abort();
        }

        if (!bdrv_merge_dirty_bitmap(anon, src, NULL, errp)) {
            dst = NULL;
            goto out;
        }
    }

    /* Merge into dst; dst is unchanged on failure. */
    bdrv_merge_dirty_bitmap(dst, anon, backup, errp);

 out:
    bdrv_release_dirty_bitmap(anon);
    return dst;
}

void qmp_block_dirty_bitmap_merge(const char *node, const char *target,
                                  BlockDirtyBitmapMergeSourceList *bitmaps,
                                  Error **errp)
{
    block_dirty_bitmap_merge(node, target, bitmaps, NULL, errp);
}
