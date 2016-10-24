/*
 * Block Dirty Bitmap
 *
 * Copyright (c) 2016 Red Hat. Inc
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"

/**
 * A BdrvDirtyBitmap can be in three possible states:
 * (1) successor is NULL and disabled is false: full r/w mode
 * (2) successor is NULL and disabled is true: read only mode ("disabled")
 * (3) successor is set: frozen mode.
 *     A frozen bitmap cannot be renamed, deleted, anonymized, cleared, set,
 *     or enabled. A frozen bitmap can only abdicate() or reclaim().
 */
struct BdrvDirtyBitmap {
    HBitmap *bitmap;            /* Dirty sector bitmap implementation */
    HBitmap *meta;              /* Meta dirty bitmap */
    BdrvDirtyBitmap *successor; /* Anonymous child; implies frozen status */
    char *name;                 /* Optional non-empty unique ID */
    int64_t size;               /* Size of the bitmap (Number of sectors) */
    bool disabled;              /* Bitmap is read-only */
    int active_iterators;       /* How many iterators are active */
    QLIST_ENTRY(BdrvDirtyBitmap) list;
};

struct BdrvDirtyBitmapIter {
    HBitmapIter hbi;
    BdrvDirtyBitmap *bitmap;
};

BdrvDirtyBitmap *bdrv_find_dirty_bitmap(BlockDriverState *bs, const char *name)
{
    BdrvDirtyBitmap *bm;

    assert(name);
    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        if (bm->name && !strcmp(name, bm->name)) {
            return bm;
        }
    }
    return NULL;
}

void bdrv_dirty_bitmap_make_anon(BdrvDirtyBitmap *bitmap)
{
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    g_free(bitmap->name);
    bitmap->name = NULL;
}

BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs,
                                          uint32_t granularity,
                                          const char *name,
                                          Error **errp)
{
    int64_t bitmap_size;
    BdrvDirtyBitmap *bitmap;
    uint32_t sector_granularity;

    assert((granularity & (granularity - 1)) == 0);

    if (name && bdrv_find_dirty_bitmap(bs, name)) {
        error_setg(errp, "Bitmap already exists: %s", name);
        return NULL;
    }
    sector_granularity = granularity >> BDRV_SECTOR_BITS;
    assert(sector_granularity);
    bitmap_size = bdrv_nb_sectors(bs);
    if (bitmap_size < 0) {
        error_setg_errno(errp, -bitmap_size, "could not get length of device");
        errno = -bitmap_size;
        return NULL;
    }
    bitmap = g_new0(BdrvDirtyBitmap, 1);
    bitmap->bitmap = hbitmap_alloc(bitmap_size, ctz32(sector_granularity));
    bitmap->size = bitmap_size;
    bitmap->name = g_strdup(name);
    bitmap->disabled = false;
    QLIST_INSERT_HEAD(&bs->dirty_bitmaps, bitmap, list);
    return bitmap;
}

/* bdrv_create_meta_dirty_bitmap
 *
 * Create a meta dirty bitmap that tracks the changes of bits in @bitmap. I.e.
 * when a dirty status bit in @bitmap is changed (either from reset to set or
 * the other way around), its respective meta dirty bitmap bit will be marked
 * dirty as well.
 *
 * @bitmap: the block dirty bitmap for which to create a meta dirty bitmap.
 * @chunk_size: how many bytes of bitmap data does each bit in the meta bitmap
 * track.
 */
void bdrv_create_meta_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                                   int chunk_size)
{
    assert(!bitmap->meta);
    bitmap->meta = hbitmap_create_meta(bitmap->bitmap,
                                       chunk_size * BITS_PER_BYTE);
}

void bdrv_release_meta_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    assert(bitmap->meta);
    hbitmap_free_meta(bitmap->bitmap);
    bitmap->meta = NULL;
}

int bdrv_dirty_bitmap_get_meta(BlockDriverState *bs,
                               BdrvDirtyBitmap *bitmap, int64_t sector,
                               int nb_sectors)
{
    uint64_t i;
    int sectors_per_bit = 1 << hbitmap_granularity(bitmap->meta);

    /* To optimize: we can make hbitmap to internally check the range in a
     * coarse level, or at least do it word by word. */
    for (i = sector; i < sector + nb_sectors; i += sectors_per_bit) {
        if (hbitmap_get(bitmap->meta, i)) {
            return true;
        }
    }
    return false;
}

void bdrv_dirty_bitmap_reset_meta(BlockDriverState *bs,
                                  BdrvDirtyBitmap *bitmap, int64_t sector,
                                  int nb_sectors)
{
    hbitmap_reset(bitmap->meta, sector, nb_sectors);
}

int64_t bdrv_dirty_bitmap_size(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->size;
}

const char *bdrv_dirty_bitmap_name(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->name;
}

bool bdrv_dirty_bitmap_frozen(BdrvDirtyBitmap *bitmap)
{
    return bitmap->successor;
}

bool bdrv_dirty_bitmap_enabled(BdrvDirtyBitmap *bitmap)
{
    return !(bitmap->disabled || bitmap->successor);
}

DirtyBitmapStatus bdrv_dirty_bitmap_status(BdrvDirtyBitmap *bitmap)
{
    if (bdrv_dirty_bitmap_frozen(bitmap)) {
        return DIRTY_BITMAP_STATUS_FROZEN;
    } else if (!bdrv_dirty_bitmap_enabled(bitmap)) {
        return DIRTY_BITMAP_STATUS_DISABLED;
    } else {
        return DIRTY_BITMAP_STATUS_ACTIVE;
    }
}

/**
 * Create a successor bitmap destined to replace this bitmap after an operation.
 * Requires that the bitmap is not frozen and has no successor.
 */
int bdrv_dirty_bitmap_create_successor(BlockDriverState *bs,
                                       BdrvDirtyBitmap *bitmap, Error **errp)
{
    uint64_t granularity;
    BdrvDirtyBitmap *child;

    if (bdrv_dirty_bitmap_frozen(bitmap)) {
        error_setg(errp, "Cannot create a successor for a bitmap that is "
                   "currently frozen");
        return -1;
    }
    assert(!bitmap->successor);

    /* Create an anonymous successor */
    granularity = bdrv_dirty_bitmap_granularity(bitmap);
    child = bdrv_create_dirty_bitmap(bs, granularity, NULL, errp);
    if (!child) {
        return -1;
    }

    /* Successor will be on or off based on our current state. */
    child->disabled = bitmap->disabled;

    /* Install the successor and freeze the parent */
    bitmap->successor = child;
    return 0;
}

/**
 * For a bitmap with a successor, yield our name to the successor,
 * delete the old bitmap, and return a handle to the new bitmap.
 */
BdrvDirtyBitmap *bdrv_dirty_bitmap_abdicate(BlockDriverState *bs,
                                            BdrvDirtyBitmap *bitmap,
                                            Error **errp)
{
    char *name;
    BdrvDirtyBitmap *successor = bitmap->successor;

    if (successor == NULL) {
        error_setg(errp, "Cannot relinquish control if "
                   "there's no successor present");
        return NULL;
    }

    name = bitmap->name;
    bitmap->name = NULL;
    successor->name = name;
    bitmap->successor = NULL;
    bdrv_release_dirty_bitmap(bs, bitmap);

    return successor;
}

/**
 * In cases of failure where we can no longer safely delete the parent,
 * we may wish to re-join the parent and child/successor.
 * The merged parent will be un-frozen, but not explicitly re-enabled.
 */
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap(BlockDriverState *bs,
                                           BdrvDirtyBitmap *parent,
                                           Error **errp)
{
    BdrvDirtyBitmap *successor = parent->successor;

    if (!successor) {
        error_setg(errp, "Cannot reclaim a successor when none is present");
        return NULL;
    }

    if (!hbitmap_merge(parent->bitmap, successor->bitmap)) {
        error_setg(errp, "Merging of parent and successor bitmap failed");
        return NULL;
    }
    bdrv_release_dirty_bitmap(bs, successor);
    parent->successor = NULL;

    return parent;
}

/**
 * Truncates _all_ bitmaps attached to a BDS.
 */
void bdrv_dirty_bitmap_truncate(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bitmap;
    uint64_t size = bdrv_nb_sectors(bs);

    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        assert(!bdrv_dirty_bitmap_frozen(bitmap));
        assert(!bitmap->active_iterators);
        hbitmap_truncate(bitmap->bitmap, size);
        bitmap->size = size;
    }
}

static void bdrv_do_release_matching_dirty_bitmap(BlockDriverState *bs,
                                                  BdrvDirtyBitmap *bitmap,
                                                  bool only_named)
{
    BdrvDirtyBitmap *bm, *next;
    QLIST_FOREACH_SAFE(bm, &bs->dirty_bitmaps, list, next) {
        if ((!bitmap || bm == bitmap) && (!only_named || bm->name)) {
            assert(!bm->active_iterators);
            assert(!bdrv_dirty_bitmap_frozen(bm));
            assert(!bm->meta);
            QLIST_REMOVE(bm, list);
            hbitmap_free(bm->bitmap);
            g_free(bm->name);
            g_free(bm);

            if (bitmap) {
                return;
            }
        }
    }
    if (bitmap) {
        abort();
    }
}

void bdrv_release_dirty_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap)
{
    bdrv_do_release_matching_dirty_bitmap(bs, bitmap, false);
}

/**
 * Release all named dirty bitmaps attached to a BDS (for use in bdrv_close()).
 * There must not be any frozen bitmaps attached.
 */
void bdrv_release_named_dirty_bitmaps(BlockDriverState *bs)
{
    bdrv_do_release_matching_dirty_bitmap(bs, NULL, true);
}

void bdrv_disable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    bitmap->disabled = true;
}

void bdrv_enable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    bitmap->disabled = false;
}

BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    BlockDirtyInfoList *list = NULL;
    BlockDirtyInfoList **plist = &list;

    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        BlockDirtyInfo *info = g_new0(BlockDirtyInfo, 1);
        BlockDirtyInfoList *entry = g_new0(BlockDirtyInfoList, 1);
        info->count = bdrv_get_dirty_count(bm);
        info->granularity = bdrv_dirty_bitmap_granularity(bm);
        info->has_name = !!bm->name;
        info->name = g_strdup(bm->name);
        info->status = bdrv_dirty_bitmap_status(bm);
        entry->value = info;
        *plist = entry;
        plist = &entry->next;
    }

    return list;
}

int bdrv_get_dirty(BlockDriverState *bs, BdrvDirtyBitmap *bitmap,
                   int64_t sector)
{
    if (bitmap) {
        return hbitmap_get(bitmap->bitmap, sector);
    } else {
        return 0;
    }
}

/**
 * Chooses a default granularity based on the existing cluster size,
 * but clamped between [4K, 64K]. Defaults to 64K in the case that there
 * is no cluster size information available.
 */
uint32_t bdrv_get_default_bitmap_granularity(BlockDriverState *bs)
{
    BlockDriverInfo bdi;
    uint32_t granularity;

    if (bdrv_get_info(bs, &bdi) >= 0 && bdi.cluster_size > 0) {
        granularity = MAX(4096, bdi.cluster_size);
        granularity = MIN(65536, granularity);
    } else {
        granularity = 65536;
    }

    return granularity;
}

uint32_t bdrv_dirty_bitmap_granularity(BdrvDirtyBitmap *bitmap)
{
    return BDRV_SECTOR_SIZE << hbitmap_granularity(bitmap->bitmap);
}

uint32_t bdrv_dirty_bitmap_meta_granularity(BdrvDirtyBitmap *bitmap)
{
    return BDRV_SECTOR_SIZE << hbitmap_granularity(bitmap->meta);
}

BdrvDirtyBitmapIter *bdrv_dirty_iter_new(BdrvDirtyBitmap *bitmap,
                                         uint64_t first_sector)
{
    BdrvDirtyBitmapIter *iter = g_new(BdrvDirtyBitmapIter, 1);
    hbitmap_iter_init(&iter->hbi, bitmap->bitmap, first_sector);
    iter->bitmap = bitmap;
    bitmap->active_iterators++;
    return iter;
}

BdrvDirtyBitmapIter *bdrv_dirty_meta_iter_new(BdrvDirtyBitmap *bitmap)
{
    BdrvDirtyBitmapIter *iter = g_new(BdrvDirtyBitmapIter, 1);
    hbitmap_iter_init(&iter->hbi, bitmap->meta, 0);
    iter->bitmap = bitmap;
    bitmap->active_iterators++;
    return iter;
}

void bdrv_dirty_iter_free(BdrvDirtyBitmapIter *iter)
{
    if (!iter) {
        return;
    }
    assert(iter->bitmap->active_iterators > 0);
    iter->bitmap->active_iterators--;
    g_free(iter);
}

int64_t bdrv_dirty_iter_next(BdrvDirtyBitmapIter *iter)
{
    return hbitmap_iter_next(&iter->hbi);
}

void bdrv_set_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                           int64_t cur_sector, int64_t nr_sectors)
{
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    hbitmap_set(bitmap->bitmap, cur_sector, nr_sectors);
}

void bdrv_reset_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                             int64_t cur_sector, int64_t nr_sectors)
{
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    hbitmap_reset(bitmap->bitmap, cur_sector, nr_sectors);
}

void bdrv_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap **out)
{
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    if (!out) {
        hbitmap_reset_all(bitmap->bitmap);
    } else {
        HBitmap *backup = bitmap->bitmap;
        bitmap->bitmap = hbitmap_alloc(bitmap->size,
                                       hbitmap_granularity(backup));
        *out = backup;
    }
}

void bdrv_undo_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap *in)
{
    HBitmap *tmp = bitmap->bitmap;
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    bitmap->bitmap = in;
    hbitmap_free(tmp);
}

uint64_t bdrv_dirty_bitmap_serialization_size(const BdrvDirtyBitmap *bitmap,
                                              uint64_t start, uint64_t count)
{
    return hbitmap_serialization_size(bitmap->bitmap, start, count);
}

uint64_t bdrv_dirty_bitmap_serialization_align(const BdrvDirtyBitmap *bitmap)
{
    return hbitmap_serialization_granularity(bitmap->bitmap);
}

void bdrv_dirty_bitmap_serialize_part(const BdrvDirtyBitmap *bitmap,
                                      uint8_t *buf, uint64_t start,
                                      uint64_t count)
{
    hbitmap_serialize_part(bitmap->bitmap, buf, start, count);
}

void bdrv_dirty_bitmap_deserialize_part(BdrvDirtyBitmap *bitmap,
                                        uint8_t *buf, uint64_t start,
                                        uint64_t count, bool finish)
{
    hbitmap_deserialize_part(bitmap->bitmap, buf, start, count, finish);
}

void bdrv_dirty_bitmap_deserialize_zeroes(BdrvDirtyBitmap *bitmap,
                                          uint64_t start, uint64_t count,
                                          bool finish)
{
    hbitmap_deserialize_zeroes(bitmap->bitmap, start, count, finish);
}

void bdrv_dirty_bitmap_deserialize_finish(BdrvDirtyBitmap *bitmap)
{
    hbitmap_deserialize_finish(bitmap->bitmap);
}

void bdrv_set_dirty(BlockDriverState *bs, int64_t cur_sector,
                    int64_t nr_sectors)
{
    BdrvDirtyBitmap *bitmap;
    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        if (!bdrv_dirty_bitmap_enabled(bitmap)) {
            continue;
        }
        hbitmap_set(bitmap->bitmap, cur_sector, nr_sectors);
    }
}

/**
 * Advance a BdrvDirtyBitmapIter to an arbitrary offset.
 */
void bdrv_set_dirty_iter(BdrvDirtyBitmapIter *iter, int64_t sector_num)
{
    hbitmap_iter_init(&iter->hbi, iter->hbi.hb, sector_num);
}

int64_t bdrv_get_dirty_count(BdrvDirtyBitmap *bitmap)
{
    return hbitmap_count(bitmap->bitmap);
}

int64_t bdrv_get_meta_dirty_count(BdrvDirtyBitmap *bitmap)
{
    return hbitmap_count(bitmap->meta);
}
