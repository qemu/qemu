/*
 * Block Dirty Bitmap
 *
 * Copyright (c) 2016-2017 Red Hat. Inc
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
    QemuMutex *mutex;
    HBitmap *bitmap;            /* Dirty bitmap implementation */
    HBitmap *meta;              /* Meta dirty bitmap */
    bool qmp_locked;            /* Bitmap is locked, it can't be modified
                                   through QMP */
    BdrvDirtyBitmap *successor; /* Anonymous child; implies frozen status */
    char *name;                 /* Optional non-empty unique ID */
    int64_t size;               /* Size of the bitmap, in bytes */
    bool disabled;              /* Bitmap is disabled. It ignores all writes to
                                   the device */
    int active_iterators;       /* How many iterators are active */
    bool readonly;              /* Bitmap is read-only. This field also
                                   prevents the respective image from being
                                   modified (i.e. blocks writes and discards).
                                   Such operations must fail and both the image
                                   and this bitmap must remain unchanged while
                                   this flag is set. */
    bool persistent;            /* bitmap must be saved to owner disk image */
    bool migration;             /* Bitmap is selected for migration, it should
                                   not be stored on the next inactivation
                                   (persistent flag doesn't matter until next
                                   invalidation).*/
    QLIST_ENTRY(BdrvDirtyBitmap) list;
};

struct BdrvDirtyBitmapIter {
    HBitmapIter hbi;
    BdrvDirtyBitmap *bitmap;
};

static inline void bdrv_dirty_bitmaps_lock(BlockDriverState *bs)
{
    qemu_mutex_lock(&bs->dirty_bitmap_mutex);
}

static inline void bdrv_dirty_bitmaps_unlock(BlockDriverState *bs)
{
    qemu_mutex_unlock(&bs->dirty_bitmap_mutex);
}

void bdrv_dirty_bitmap_lock(BdrvDirtyBitmap *bitmap)
{
    qemu_mutex_lock(bitmap->mutex);
}

void bdrv_dirty_bitmap_unlock(BdrvDirtyBitmap *bitmap)
{
    qemu_mutex_unlock(bitmap->mutex);
}

/* Called with BQL or dirty_bitmap lock taken.  */
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

/* Called with BQL taken.  */
BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs,
                                          uint32_t granularity,
                                          const char *name,
                                          Error **errp)
{
    int64_t bitmap_size;
    BdrvDirtyBitmap *bitmap;

    assert(is_power_of_2(granularity) && granularity >= BDRV_SECTOR_SIZE);

    if (name && bdrv_find_dirty_bitmap(bs, name)) {
        error_setg(errp, "Bitmap already exists: %s", name);
        return NULL;
    }
    bitmap_size = bdrv_getlength(bs);
    if (bitmap_size < 0) {
        error_setg_errno(errp, -bitmap_size, "could not get length of device");
        errno = -bitmap_size;
        return NULL;
    }
    bitmap = g_new0(BdrvDirtyBitmap, 1);
    bitmap->mutex = &bs->dirty_bitmap_mutex;
    bitmap->bitmap = hbitmap_alloc(bitmap_size, ctz32(granularity));
    bitmap->size = bitmap_size;
    bitmap->name = g_strdup(name);
    bitmap->disabled = false;
    bdrv_dirty_bitmaps_lock(bs);
    QLIST_INSERT_HEAD(&bs->dirty_bitmaps, bitmap, list);
    bdrv_dirty_bitmaps_unlock(bs);
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
    qemu_mutex_lock(bitmap->mutex);
    bitmap->meta = hbitmap_create_meta(bitmap->bitmap,
                                       chunk_size * BITS_PER_BYTE);
    qemu_mutex_unlock(bitmap->mutex);
}

void bdrv_release_meta_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    assert(bitmap->meta);
    qemu_mutex_lock(bitmap->mutex);
    hbitmap_free_meta(bitmap->bitmap);
    bitmap->meta = NULL;
    qemu_mutex_unlock(bitmap->mutex);
}

int64_t bdrv_dirty_bitmap_size(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->size;
}

const char *bdrv_dirty_bitmap_name(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->name;
}

/* Called with BQL taken.  */
bool bdrv_dirty_bitmap_frozen(BdrvDirtyBitmap *bitmap)
{
    return bitmap->successor;
}

/* Both conditions disallow user-modification via QMP. */
bool bdrv_dirty_bitmap_user_locked(BdrvDirtyBitmap *bitmap) {
    return bdrv_dirty_bitmap_frozen(bitmap) ||
           bdrv_dirty_bitmap_qmp_locked(bitmap);
}

void bdrv_dirty_bitmap_set_qmp_locked(BdrvDirtyBitmap *bitmap, bool qmp_locked)
{
    qemu_mutex_lock(bitmap->mutex);
    bitmap->qmp_locked = qmp_locked;
    qemu_mutex_unlock(bitmap->mutex);
}

bool bdrv_dirty_bitmap_qmp_locked(BdrvDirtyBitmap *bitmap)
{
    return bitmap->qmp_locked;
}

/* Called with BQL taken.  */
bool bdrv_dirty_bitmap_enabled(BdrvDirtyBitmap *bitmap)
{
    return !(bitmap->disabled || bitmap->successor);
}

/* Called with BQL taken.  */
DirtyBitmapStatus bdrv_dirty_bitmap_status(BdrvDirtyBitmap *bitmap)
{
    if (bdrv_dirty_bitmap_frozen(bitmap)) {
        return DIRTY_BITMAP_STATUS_FROZEN;
    } else if (bdrv_dirty_bitmap_qmp_locked(bitmap)) {
        return DIRTY_BITMAP_STATUS_LOCKED;
    } else if (!bdrv_dirty_bitmap_enabled(bitmap)) {
        return DIRTY_BITMAP_STATUS_DISABLED;
    } else {
        return DIRTY_BITMAP_STATUS_ACTIVE;
    }
}

/**
 * Create a successor bitmap destined to replace this bitmap after an operation.
 * Requires that the bitmap is not frozen and has no successor.
 * Called with BQL taken.
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

void bdrv_enable_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap)
{
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    bitmap->disabled = false;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_enable_successor(BdrvDirtyBitmap *bitmap)
{
    assert(bitmap->mutex == bitmap->successor->mutex);
    qemu_mutex_lock(bitmap->mutex);
    bdrv_enable_dirty_bitmap_locked(bitmap->successor);
    qemu_mutex_unlock(bitmap->mutex);
}

/* Called within bdrv_dirty_bitmap_lock..unlock and with BQL taken.  */
static void bdrv_release_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap)
{
    assert(!bitmap->active_iterators);
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    assert(!bitmap->meta);
    QLIST_REMOVE(bitmap, list);
    hbitmap_free(bitmap->bitmap);
    g_free(bitmap->name);
    g_free(bitmap);
}

/**
 * For a bitmap with a successor, yield our name to the successor,
 * delete the old bitmap, and return a handle to the new bitmap.
 * Called with BQL taken.
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
    successor->persistent = bitmap->persistent;
    bitmap->persistent = false;
    bdrv_release_dirty_bitmap(bs, bitmap);

    return successor;
}

/**
 * In cases of failure where we can no longer safely delete the parent,
 * we may wish to re-join the parent and child/successor.
 * The merged parent will be un-frozen, but not explicitly re-enabled.
 * Called within bdrv_dirty_bitmap_lock..unlock and with BQL taken.
 */
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap_locked(BlockDriverState *bs,
                                                  BdrvDirtyBitmap *parent,
                                                  Error **errp)
{
    BdrvDirtyBitmap *successor = parent->successor;

    if (!successor) {
        error_setg(errp, "Cannot reclaim a successor when none is present");
        return NULL;
    }

    if (!hbitmap_merge(parent->bitmap, successor->bitmap, parent->bitmap)) {
        error_setg(errp, "Merging of parent and successor bitmap failed");
        return NULL;
    }
    bdrv_release_dirty_bitmap_locked(successor);
    parent->successor = NULL;

    return parent;
}

/* Called with BQL taken. */
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap(BlockDriverState *bs,
                                           BdrvDirtyBitmap *parent,
                                           Error **errp)
{
    BdrvDirtyBitmap *ret;

    qemu_mutex_lock(parent->mutex);
    ret = bdrv_reclaim_dirty_bitmap_locked(bs, parent, errp);
    qemu_mutex_unlock(parent->mutex);

    return ret;
}

/**
 * Truncates _all_ bitmaps attached to a BDS.
 * Called with BQL taken.
 */
void bdrv_dirty_bitmap_truncate(BlockDriverState *bs, int64_t bytes)
{
    BdrvDirtyBitmap *bitmap;

    bdrv_dirty_bitmaps_lock(bs);
    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        assert(!bdrv_dirty_bitmap_frozen(bitmap));
        assert(!bitmap->active_iterators);
        hbitmap_truncate(bitmap->bitmap, bytes);
        bitmap->size = bytes;
    }
    bdrv_dirty_bitmaps_unlock(bs);
}

/* Called with BQL taken.  */
void bdrv_release_dirty_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmaps_lock(bs);
    bdrv_release_dirty_bitmap_locked(bitmap);
    bdrv_dirty_bitmaps_unlock(bs);
}

/**
 * Release all named dirty bitmaps attached to a BDS (for use in bdrv_close()).
 * There must not be any frozen bitmaps attached.
 * This function does not remove persistent bitmaps from the storage.
 * Called with BQL taken.
 */
void bdrv_release_named_dirty_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm, *next;

    bdrv_dirty_bitmaps_lock(bs);
    QLIST_FOREACH_SAFE(bm, &bs->dirty_bitmaps, list, next) {
        if (bdrv_dirty_bitmap_name(bm)) {
            bdrv_release_dirty_bitmap_locked(bm);
        }
    }
    bdrv_dirty_bitmaps_unlock(bs);
}

/**
 * Remove persistent dirty bitmap from the storage if it exists.
 * Absence of bitmap is not an error, because we have the following scenario:
 * BdrvDirtyBitmap can have .persistent = true but not yet saved and have no
 * stored version. For such bitmap bdrv_remove_persistent_dirty_bitmap() should
 * not fail.
 * This function doesn't release corresponding BdrvDirtyBitmap.
 */
void bdrv_remove_persistent_dirty_bitmap(BlockDriverState *bs,
                                         const char *name,
                                         Error **errp)
{
    if (bs->drv && bs->drv->bdrv_remove_persistent_dirty_bitmap) {
        bs->drv->bdrv_remove_persistent_dirty_bitmap(bs, name, errp);
    }
}

void bdrv_disable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmap_lock(bitmap);
    assert(!bdrv_dirty_bitmap_frozen(bitmap));
    bitmap->disabled = true;
    bdrv_dirty_bitmap_unlock(bitmap);
}

void bdrv_enable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmap_lock(bitmap);
    bdrv_enable_dirty_bitmap_locked(bitmap);
    bdrv_dirty_bitmap_unlock(bitmap);
}

BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    BlockDirtyInfoList *list = NULL;
    BlockDirtyInfoList **plist = &list;

    bdrv_dirty_bitmaps_lock(bs);
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
    bdrv_dirty_bitmaps_unlock(bs);

    return list;
}

/* Called within bdrv_dirty_bitmap_lock..unlock */
bool bdrv_get_dirty_locked(BlockDriverState *bs, BdrvDirtyBitmap *bitmap,
                           int64_t offset)
{
    if (bitmap) {
        return hbitmap_get(bitmap->bitmap, offset);
    } else {
        return false;
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

uint32_t bdrv_dirty_bitmap_granularity(const BdrvDirtyBitmap *bitmap)
{
    return 1U << hbitmap_granularity(bitmap->bitmap);
}

BdrvDirtyBitmapIter *bdrv_dirty_iter_new(BdrvDirtyBitmap *bitmap)
{
    BdrvDirtyBitmapIter *iter = g_new(BdrvDirtyBitmapIter, 1);
    hbitmap_iter_init(&iter->hbi, bitmap->bitmap, 0);
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
    return hbitmap_iter_next(&iter->hbi, true);
}

/**
 * Return the next consecutively dirty area in the dirty bitmap
 * belonging to the given iterator @iter.
 *
 * @max_offset: Maximum value that may be returned for
 *              *offset + *bytes
 * @offset:     Will contain the start offset of the next dirty area
 * @bytes:      Will contain the length of the next dirty area
 *
 * Returns: True if a dirty area could be found before max_offset
 *          (which means that *offset and *bytes then contain valid
 *          values), false otherwise.
 *
 * Note that @iter is never advanced if false is returned.  If an area
 * is found (which means that true is returned), it will be advanced
 * past that area.
 */
bool bdrv_dirty_iter_next_area(BdrvDirtyBitmapIter *iter, uint64_t max_offset,
                               uint64_t *offset, int *bytes)
{
    uint32_t granularity = bdrv_dirty_bitmap_granularity(iter->bitmap);
    uint64_t gran_max_offset;
    int64_t ret;
    int size;

    if (max_offset == iter->bitmap->size) {
        /* If max_offset points to the image end, round it up by the
         * bitmap granularity */
        gran_max_offset = ROUND_UP(max_offset, granularity);
    } else {
        gran_max_offset = max_offset;
    }

    ret = hbitmap_iter_next(&iter->hbi, false);
    if (ret < 0 || ret + granularity > gran_max_offset) {
        return false;
    }

    *offset = ret;
    size = 0;

    assert(granularity <= INT_MAX);

    do {
        /* Advance iterator */
        ret = hbitmap_iter_next(&iter->hbi, true);
        size += granularity;
    } while (ret + granularity <= gran_max_offset &&
             hbitmap_iter_next(&iter->hbi, false) == ret + granularity &&
             size <= INT_MAX - granularity);

    *bytes = MIN(size, max_offset - *offset);
    return true;
}

/* Called within bdrv_dirty_bitmap_lock..unlock */
void bdrv_set_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap,
                                  int64_t offset, int64_t bytes)
{
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    hbitmap_set(bitmap->bitmap, offset, bytes);
}

void bdrv_set_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                           int64_t offset, int64_t bytes)
{
    bdrv_dirty_bitmap_lock(bitmap);
    bdrv_set_dirty_bitmap_locked(bitmap, offset, bytes);
    bdrv_dirty_bitmap_unlock(bitmap);
}

/* Called within bdrv_dirty_bitmap_lock..unlock */
void bdrv_reset_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap,
                                    int64_t offset, int64_t bytes)
{
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    hbitmap_reset(bitmap->bitmap, offset, bytes);
}

void bdrv_reset_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                             int64_t offset, int64_t bytes)
{
    bdrv_dirty_bitmap_lock(bitmap);
    bdrv_reset_dirty_bitmap_locked(bitmap, offset, bytes);
    bdrv_dirty_bitmap_unlock(bitmap);
}

void bdrv_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap **out)
{
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    bdrv_dirty_bitmap_lock(bitmap);
    if (!out) {
        hbitmap_reset_all(bitmap->bitmap);
    } else {
        HBitmap *backup = bitmap->bitmap;
        bitmap->bitmap = hbitmap_alloc(bitmap->size,
                                       hbitmap_granularity(backup));
        *out = backup;
    }
    bdrv_dirty_bitmap_unlock(bitmap);
}

void bdrv_restore_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap *backup)
{
    HBitmap *tmp = bitmap->bitmap;
    assert(bdrv_dirty_bitmap_enabled(bitmap));
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    bitmap->bitmap = backup;
    hbitmap_free(tmp);
}

uint64_t bdrv_dirty_bitmap_serialization_size(const BdrvDirtyBitmap *bitmap,
                                              uint64_t offset, uint64_t bytes)
{
    return hbitmap_serialization_size(bitmap->bitmap, offset, bytes);
}

uint64_t bdrv_dirty_bitmap_serialization_align(const BdrvDirtyBitmap *bitmap)
{
    return hbitmap_serialization_align(bitmap->bitmap);
}

void bdrv_dirty_bitmap_serialize_part(const BdrvDirtyBitmap *bitmap,
                                      uint8_t *buf, uint64_t offset,
                                      uint64_t bytes)
{
    hbitmap_serialize_part(bitmap->bitmap, buf, offset, bytes);
}

void bdrv_dirty_bitmap_deserialize_part(BdrvDirtyBitmap *bitmap,
                                        uint8_t *buf, uint64_t offset,
                                        uint64_t bytes, bool finish)
{
    hbitmap_deserialize_part(bitmap->bitmap, buf, offset, bytes, finish);
}

void bdrv_dirty_bitmap_deserialize_zeroes(BdrvDirtyBitmap *bitmap,
                                          uint64_t offset, uint64_t bytes,
                                          bool finish)
{
    hbitmap_deserialize_zeroes(bitmap->bitmap, offset, bytes, finish);
}

void bdrv_dirty_bitmap_deserialize_ones(BdrvDirtyBitmap *bitmap,
                                        uint64_t offset, uint64_t bytes,
                                        bool finish)
{
    hbitmap_deserialize_ones(bitmap->bitmap, offset, bytes, finish);
}

void bdrv_dirty_bitmap_deserialize_finish(BdrvDirtyBitmap *bitmap)
{
    hbitmap_deserialize_finish(bitmap->bitmap);
}

void bdrv_set_dirty(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    BdrvDirtyBitmap *bitmap;

    if (QLIST_EMPTY(&bs->dirty_bitmaps)) {
        return;
    }

    bdrv_dirty_bitmaps_lock(bs);
    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        if (!bdrv_dirty_bitmap_enabled(bitmap)) {
            continue;
        }
        assert(!bdrv_dirty_bitmap_readonly(bitmap));
        hbitmap_set(bitmap->bitmap, offset, bytes);
    }
    bdrv_dirty_bitmaps_unlock(bs);
}

/**
 * Advance a BdrvDirtyBitmapIter to an arbitrary offset.
 */
void bdrv_set_dirty_iter(BdrvDirtyBitmapIter *iter, int64_t offset)
{
    hbitmap_iter_init(&iter->hbi, iter->hbi.hb, offset);
}

int64_t bdrv_get_dirty_count(BdrvDirtyBitmap *bitmap)
{
    return hbitmap_count(bitmap->bitmap);
}

int64_t bdrv_get_meta_dirty_count(BdrvDirtyBitmap *bitmap)
{
    return hbitmap_count(bitmap->meta);
}

bool bdrv_dirty_bitmap_readonly(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->readonly;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_readonly(BdrvDirtyBitmap *bitmap, bool value)
{
    qemu_mutex_lock(bitmap->mutex);
    bitmap->readonly = value;
    qemu_mutex_unlock(bitmap->mutex);
}

bool bdrv_has_readonly_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        if (bm->readonly) {
            return true;
        }
    }

    return false;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_persistance(BdrvDirtyBitmap *bitmap, bool persistent)
{
    qemu_mutex_lock(bitmap->mutex);
    bitmap->persistent = persistent;
    qemu_mutex_unlock(bitmap->mutex);
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_migration(BdrvDirtyBitmap *bitmap, bool migration)
{
    qemu_mutex_lock(bitmap->mutex);
    bitmap->migration = migration;
    qemu_mutex_unlock(bitmap->mutex);
}

bool bdrv_dirty_bitmap_get_persistance(BdrvDirtyBitmap *bitmap)
{
    return bitmap->persistent && !bitmap->migration;
}

bool bdrv_has_changed_persistent_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        if (bm->persistent && !bm->readonly && !bm->migration) {
            return true;
        }
    }

    return false;
}

BdrvDirtyBitmap *bdrv_dirty_bitmap_next(BlockDriverState *bs,
                                        BdrvDirtyBitmap *bitmap)
{
    return bitmap == NULL ? QLIST_FIRST(&bs->dirty_bitmaps) :
                            QLIST_NEXT(bitmap, list);
}

char *bdrv_dirty_bitmap_sha256(const BdrvDirtyBitmap *bitmap, Error **errp)
{
    return hbitmap_sha256(bitmap->bitmap, errp);
}

int64_t bdrv_dirty_bitmap_next_zero(BdrvDirtyBitmap *bitmap, uint64_t offset)
{
    return hbitmap_next_zero(bitmap->bitmap, offset);
}

void bdrv_merge_dirty_bitmap(BdrvDirtyBitmap *dest, const BdrvDirtyBitmap *src,
                             HBitmap **backup, Error **errp)
{
    bool ret;

    /* only bitmaps from one bds are supported */
    assert(dest->mutex == src->mutex);

    qemu_mutex_lock(dest->mutex);

    if (bdrv_dirty_bitmap_user_locked(dest)) {
        error_setg(errp, "Bitmap '%s' is currently in use by another"
        " operation and cannot be modified", dest->name);
        goto out;
    }

    if (bdrv_dirty_bitmap_readonly(dest)) {
        error_setg(errp, "Bitmap '%s' is readonly and cannot be modified",
                   dest->name);
        goto out;
    }

    if (!hbitmap_can_merge(dest->bitmap, src->bitmap)) {
        error_setg(errp, "Bitmaps are incompatible and can't be merged");
        goto out;
    }

    if (backup) {
        *backup = dest->bitmap;
        dest->bitmap = hbitmap_alloc(dest->size, hbitmap_granularity(*backup));
        ret = hbitmap_merge(*backup, src->bitmap, dest->bitmap);
    } else {
        ret = hbitmap_merge(dest->bitmap, src->bitmap, dest->bitmap);
    }
    assert(ret);

out:
    qemu_mutex_unlock(dest->mutex);
}
