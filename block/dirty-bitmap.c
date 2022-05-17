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
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "qemu/main-loop.h"

struct BdrvDirtyBitmap {
    BlockDriverState *bs;
    HBitmap *bitmap;            /* Dirty bitmap implementation */
    bool busy;                  /* Bitmap is busy, it can't be used via QMP */
    BdrvDirtyBitmap *successor; /* Anonymous child, if any. */
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
    bool inconsistent;          /* bitmap is persistent, but inconsistent.
                                   It cannot be used at all in any way, except
                                   a QMP user can remove it. */
    bool skip_store;            /* We are either migrating or deleting this
                                 * bitmap; it should not be stored on the next
                                 * inactivation. */
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
    bdrv_dirty_bitmaps_lock(bitmap->bs);
}

void bdrv_dirty_bitmap_unlock(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
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

    if (name) {
        if (bdrv_find_dirty_bitmap(bs, name)) {
            error_setg(errp, "Bitmap already exists: %s", name);
            return NULL;
        }
        if (strlen(name) > BDRV_BITMAP_MAX_NAME_SIZE) {
            error_setg(errp, "Bitmap name too long: %s", name);
            return NULL;
        }
    }
    bitmap_size = bdrv_getlength(bs);
    if (bitmap_size < 0) {
        error_setg_errno(errp, -bitmap_size, "could not get length of device");
        errno = -bitmap_size;
        return NULL;
    }
    bitmap = g_new0(BdrvDirtyBitmap, 1);
    bitmap->bs = bs;
    bitmap->bitmap = hbitmap_alloc(bitmap_size, ctz32(granularity));
    bitmap->size = bitmap_size;
    bitmap->name = g_strdup(name);
    bitmap->disabled = false;
    bdrv_dirty_bitmaps_lock(bs);
    QLIST_INSERT_HEAD(&bs->dirty_bitmaps, bitmap, list);
    bdrv_dirty_bitmaps_unlock(bs);
    return bitmap;
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
bool bdrv_dirty_bitmap_has_successor(BdrvDirtyBitmap *bitmap)
{
    return bitmap->successor;
}

static bool bdrv_dirty_bitmap_busy(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->busy;
}

void bdrv_dirty_bitmap_set_busy(BdrvDirtyBitmap *bitmap, bool busy)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bitmap->busy = busy;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

/* Called with BQL taken.  */
bool bdrv_dirty_bitmap_enabled(BdrvDirtyBitmap *bitmap)
{
    return !bitmap->disabled;
}

/* Called with BQL taken.  */
static bool bdrv_dirty_bitmap_recording(BdrvDirtyBitmap *bitmap)
{
    return !bitmap->disabled || (bitmap->successor &&
                                 !bitmap->successor->disabled);
}

int bdrv_dirty_bitmap_check(const BdrvDirtyBitmap *bitmap, uint32_t flags,
                            Error **errp)
{
    if ((flags & BDRV_BITMAP_BUSY) && bdrv_dirty_bitmap_busy(bitmap)) {
        error_setg(errp, "Bitmap '%s' is currently in use by another"
                   " operation and cannot be used", bitmap->name);
        return -1;
    }

    if ((flags & BDRV_BITMAP_RO) && bdrv_dirty_bitmap_readonly(bitmap)) {
        error_setg(errp, "Bitmap '%s' is readonly and cannot be modified",
                   bitmap->name);
        return -1;
    }

    if ((flags & BDRV_BITMAP_INCONSISTENT) &&
        bdrv_dirty_bitmap_inconsistent(bitmap)) {
        error_setg(errp, "Bitmap '%s' is inconsistent and cannot be used",
                   bitmap->name);
        error_append_hint(errp, "Try block-dirty-bitmap-remove to delete"
                          " this bitmap from disk\n");
        return -1;
    }

    return 0;
}

/**
 * Create a successor bitmap destined to replace this bitmap after an operation.
 * Requires that the bitmap is not marked busy and has no successor.
 * The successor will be enabled if the parent bitmap was.
 * Called with BQL taken.
 */
int bdrv_dirty_bitmap_create_successor(BdrvDirtyBitmap *bitmap, Error **errp)
{
    uint64_t granularity;
    BdrvDirtyBitmap *child;

    if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_BUSY, errp)) {
        return -1;
    }
    if (bdrv_dirty_bitmap_has_successor(bitmap)) {
        error_setg(errp, "Cannot create a successor for a bitmap that already "
                   "has one");
        return -1;
    }

    /* Create an anonymous successor */
    granularity = bdrv_dirty_bitmap_granularity(bitmap);
    child = bdrv_create_dirty_bitmap(bitmap->bs, granularity, NULL, errp);
    if (!child) {
        return -1;
    }

    /* Successor will be on or off based on our current state. */
    child->disabled = bitmap->disabled;
    bitmap->disabled = true;

    /* Install the successor and mark the parent as busy */
    bitmap->successor = child;
    bitmap->busy = true;
    return 0;
}

void bdrv_enable_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap)
{
    bitmap->disabled = false;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_enable_successor(BdrvDirtyBitmap *bitmap)
{
    assert(bitmap->bs == bitmap->successor->bs);
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bdrv_enable_dirty_bitmap_locked(bitmap->successor);
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

/* Called within bdrv_dirty_bitmap_lock..unlock and with BQL taken.  */
static void bdrv_release_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap)
{
    assert(!bitmap->active_iterators);
    assert(!bdrv_dirty_bitmap_busy(bitmap));
    assert(!bdrv_dirty_bitmap_has_successor(bitmap));
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
BdrvDirtyBitmap *bdrv_dirty_bitmap_abdicate(BdrvDirtyBitmap *bitmap,
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
    bitmap->busy = false;
    bdrv_release_dirty_bitmap(bitmap);

    return successor;
}

/**
 * In cases of failure where we can no longer safely delete the parent,
 * we may wish to re-join the parent and child/successor.
 * The merged parent will be marked as not busy.
 * The marged parent will be enabled if and only if the successor was enabled.
 * Called within bdrv_dirty_bitmap_lock..unlock and with BQL taken.
 */
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap_locked(BdrvDirtyBitmap *parent,
                                                  Error **errp)
{
    BdrvDirtyBitmap *successor = parent->successor;

    if (!successor) {
        error_setg(errp, "Cannot reclaim a successor when none is present");
        return NULL;
    }

    hbitmap_merge(parent->bitmap, successor->bitmap, parent->bitmap);

    parent->disabled = successor->disabled;
    parent->busy = false;
    bdrv_release_dirty_bitmap_locked(successor);
    parent->successor = NULL;

    return parent;
}

/* Called with BQL taken. */
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap(BdrvDirtyBitmap *parent,
                                           Error **errp)
{
    BdrvDirtyBitmap *ret;

    bdrv_dirty_bitmaps_lock(parent->bs);
    ret = bdrv_reclaim_dirty_bitmap_locked(parent, errp);
    bdrv_dirty_bitmaps_unlock(parent->bs);

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
        assert(!bdrv_dirty_bitmap_busy(bitmap));
        assert(!bdrv_dirty_bitmap_has_successor(bitmap));
        assert(!bitmap->active_iterators);
        hbitmap_truncate(bitmap->bitmap, bytes);
        bitmap->size = bytes;
    }
    bdrv_dirty_bitmaps_unlock(bs);
}

/* Called with BQL taken.  */
void bdrv_release_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    BlockDriverState *bs = bitmap->bs;

    bdrv_dirty_bitmaps_lock(bs);
    bdrv_release_dirty_bitmap_locked(bitmap);
    bdrv_dirty_bitmaps_unlock(bs);
}

/**
 * Release all named dirty bitmaps attached to a BDS (for use in bdrv_close()).
 * There must not be any busy bitmaps attached.
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
static int coroutine_fn
bdrv_co_remove_persistent_dirty_bitmap(BlockDriverState *bs, const char *name,
                                       Error **errp)
{
    if (bs->drv && bs->drv->bdrv_co_remove_persistent_dirty_bitmap) {
        return bs->drv->bdrv_co_remove_persistent_dirty_bitmap(bs, name, errp);
    }

    return 0;
}

typedef struct BdrvRemovePersistentDirtyBitmapCo {
    BlockDriverState *bs;
    const char *name;
    Error **errp;
    int ret;
} BdrvRemovePersistentDirtyBitmapCo;

static void coroutine_fn
bdrv_co_remove_persistent_dirty_bitmap_entry(void *opaque)
{
    BdrvRemovePersistentDirtyBitmapCo *s = opaque;

    s->ret = bdrv_co_remove_persistent_dirty_bitmap(s->bs, s->name, s->errp);
    aio_wait_kick();
}

int bdrv_remove_persistent_dirty_bitmap(BlockDriverState *bs, const char *name,
                                        Error **errp)
{
    if (qemu_in_coroutine()) {
        return bdrv_co_remove_persistent_dirty_bitmap(bs, name, errp);
    } else {
        Coroutine *co;
        BdrvRemovePersistentDirtyBitmapCo s = {
            .bs = bs,
            .name = name,
            .errp = errp,
            .ret = -EINPROGRESS,
        };

        co = qemu_coroutine_create(bdrv_co_remove_persistent_dirty_bitmap_entry,
                                   &s);
        bdrv_coroutine_enter(bs, co);
        BDRV_POLL_WHILE(bs, s.ret == -EINPROGRESS);

        return s.ret;
    }
}

bool
bdrv_supports_persistent_dirty_bitmap(BlockDriverState *bs)
{
    if (bs->drv && bs->drv->bdrv_supports_persistent_dirty_bitmap) {
        return bs->drv->bdrv_supports_persistent_dirty_bitmap(bs);
    }
    return false;
}

static bool coroutine_fn
bdrv_co_can_store_new_dirty_bitmap(BlockDriverState *bs, const char *name,
                                   uint32_t granularity, Error **errp)
{
    BlockDriver *drv = bs->drv;

    if (!drv) {
        error_setg_errno(errp, ENOMEDIUM,
                         "Can't store persistent bitmaps to %s",
                         bdrv_get_device_or_node_name(bs));
        return false;
    }

    if (!drv->bdrv_co_can_store_new_dirty_bitmap) {
        error_setg_errno(errp, ENOTSUP,
                         "Can't store persistent bitmaps to %s",
                         bdrv_get_device_or_node_name(bs));
        return false;
    }

    return drv->bdrv_co_can_store_new_dirty_bitmap(bs, name, granularity, errp);
}

typedef struct BdrvCanStoreNewDirtyBitmapCo {
    BlockDriverState *bs;
    const char *name;
    uint32_t granularity;
    Error **errp;
    bool ret;

    bool in_progress;
} BdrvCanStoreNewDirtyBitmapCo;

static void coroutine_fn bdrv_co_can_store_new_dirty_bitmap_entry(void *opaque)
{
    BdrvCanStoreNewDirtyBitmapCo *s = opaque;

    s->ret = bdrv_co_can_store_new_dirty_bitmap(s->bs, s->name, s->granularity,
                                                s->errp);
    s->in_progress = false;
    aio_wait_kick();
}

bool bdrv_can_store_new_dirty_bitmap(BlockDriverState *bs, const char *name,
                                     uint32_t granularity, Error **errp)
{
    IO_CODE();
    if (qemu_in_coroutine()) {
        return bdrv_co_can_store_new_dirty_bitmap(bs, name, granularity, errp);
    } else {
        Coroutine *co;
        BdrvCanStoreNewDirtyBitmapCo s = {
            .bs = bs,
            .name = name,
            .granularity = granularity,
            .errp = errp,
            .in_progress = true,
        };

        co = qemu_coroutine_create(bdrv_co_can_store_new_dirty_bitmap_entry,
                                   &s);
        bdrv_coroutine_enter(bs, co);
        BDRV_POLL_WHILE(bs, s.in_progress);

        return s.ret;
    }
}

void bdrv_disable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bitmap->disabled = true;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

void bdrv_enable_dirty_bitmap(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bdrv_enable_dirty_bitmap_locked(bitmap);
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    BlockDirtyInfoList *list = NULL;
    BlockDirtyInfoList **tail = &list;

    bdrv_dirty_bitmaps_lock(bs);
    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        BlockDirtyInfo *info = g_new0(BlockDirtyInfo, 1);

        info->count = bdrv_get_dirty_count(bm);
        info->granularity = bdrv_dirty_bitmap_granularity(bm);
        info->has_name = !!bm->name;
        info->name = g_strdup(bm->name);
        info->recording = bdrv_dirty_bitmap_recording(bm);
        info->busy = bdrv_dirty_bitmap_busy(bm);
        info->persistent = bm->persistent;
        info->has_inconsistent = bm->inconsistent;
        info->inconsistent = bm->inconsistent;
        QAPI_LIST_APPEND(tail, info);
    }
    bdrv_dirty_bitmaps_unlock(bs);

    return list;
}

/* Called within bdrv_dirty_bitmap_lock..unlock */
bool bdrv_dirty_bitmap_get_locked(BdrvDirtyBitmap *bitmap, int64_t offset)
{
    return hbitmap_get(bitmap->bitmap, offset);
}

bool bdrv_dirty_bitmap_get(BdrvDirtyBitmap *bitmap, int64_t offset)
{
    bool ret;
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    ret = bdrv_dirty_bitmap_get_locked(bitmap, offset);
    bdrv_dirty_bitmaps_unlock(bitmap->bs);

    return ret;
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

/* Called within bdrv_dirty_bitmap_lock..unlock */
void bdrv_set_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap,
                                  int64_t offset, int64_t bytes)
{
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    hbitmap_set(bitmap->bitmap, offset, bytes);
}

void bdrv_set_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                           int64_t offset, int64_t bytes)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bdrv_set_dirty_bitmap_locked(bitmap, offset, bytes);
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

/* Called within bdrv_dirty_bitmap_lock..unlock */
void bdrv_reset_dirty_bitmap_locked(BdrvDirtyBitmap *bitmap,
                                    int64_t offset, int64_t bytes)
{
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    hbitmap_reset(bitmap->bitmap, offset, bytes);
}

void bdrv_reset_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                             int64_t offset, int64_t bytes)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bdrv_reset_dirty_bitmap_locked(bitmap, offset, bytes);
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

void bdrv_clear_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap **out)
{
    IO_CODE();
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    if (!out) {
        hbitmap_reset_all(bitmap->bitmap);
    } else {
        HBitmap *backup = bitmap->bitmap;
        bitmap->bitmap = hbitmap_alloc(bitmap->size,
                                       hbitmap_granularity(backup));
        *out = backup;
    }
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

void bdrv_restore_dirty_bitmap(BdrvDirtyBitmap *bitmap, HBitmap *backup)
{
    HBitmap *tmp = bitmap->bitmap;
    assert(!bdrv_dirty_bitmap_readonly(bitmap));
    GLOBAL_STATE_CODE();
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

/* Return the disk size covered by a chunk of serialized bitmap data. */
uint64_t bdrv_dirty_bitmap_serialization_coverage(int serialized_chunk_size,
                                                  const BdrvDirtyBitmap *bitmap)
{
    uint64_t granularity = bdrv_dirty_bitmap_granularity(bitmap);
    uint64_t limit = granularity * (serialized_chunk_size << 3);

    assert(QEMU_IS_ALIGNED(limit,
                           bdrv_dirty_bitmap_serialization_align(bitmap)));
    return limit;
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
    IO_CODE();

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

bool bdrv_dirty_bitmap_readonly(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->readonly;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_readonly(BdrvDirtyBitmap *bitmap, bool value)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bitmap->readonly = value;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
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

bool bdrv_has_named_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;

    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        if (bdrv_dirty_bitmap_name(bm)) {
            return true;
        }
    }

    return false;
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_persistence(BdrvDirtyBitmap *bitmap, bool persistent)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bitmap->persistent = persistent;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_set_inconsistent(BdrvDirtyBitmap *bitmap)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    assert(bitmap->persistent == true);
    bitmap->inconsistent = true;
    bitmap->disabled = true;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

/* Called with BQL taken. */
void bdrv_dirty_bitmap_skip_store(BdrvDirtyBitmap *bitmap, bool skip)
{
    bdrv_dirty_bitmaps_lock(bitmap->bs);
    bitmap->skip_store = skip;
    bdrv_dirty_bitmaps_unlock(bitmap->bs);
}

bool bdrv_dirty_bitmap_get_persistence(BdrvDirtyBitmap *bitmap)
{
    return bitmap->persistent && !bitmap->skip_store;
}

bool bdrv_dirty_bitmap_inconsistent(const BdrvDirtyBitmap *bitmap)
{
    return bitmap->inconsistent;
}

BdrvDirtyBitmap *bdrv_dirty_bitmap_first(BlockDriverState *bs)
{
    return QLIST_FIRST(&bs->dirty_bitmaps);
}

BdrvDirtyBitmap *bdrv_dirty_bitmap_next(BdrvDirtyBitmap *bitmap)
{
    return QLIST_NEXT(bitmap, list);
}

char *bdrv_dirty_bitmap_sha256(const BdrvDirtyBitmap *bitmap, Error **errp)
{
    return hbitmap_sha256(bitmap->bitmap, errp);
}

int64_t bdrv_dirty_bitmap_next_dirty(BdrvDirtyBitmap *bitmap, int64_t offset,
                                     int64_t bytes)
{
    return hbitmap_next_dirty(bitmap->bitmap, offset, bytes);
}

int64_t bdrv_dirty_bitmap_next_zero(BdrvDirtyBitmap *bitmap, int64_t offset,
                                    int64_t bytes)
{
    return hbitmap_next_zero(bitmap->bitmap, offset, bytes);
}

bool bdrv_dirty_bitmap_next_dirty_area(BdrvDirtyBitmap *bitmap,
        int64_t start, int64_t end, int64_t max_dirty_count,
        int64_t *dirty_start, int64_t *dirty_count)
{
    return hbitmap_next_dirty_area(bitmap->bitmap, start, end, max_dirty_count,
                                   dirty_start, dirty_count);
}

bool bdrv_dirty_bitmap_status(BdrvDirtyBitmap *bitmap, int64_t offset,
                              int64_t bytes, int64_t *count)
{
    return hbitmap_status(bitmap->bitmap, offset, bytes, count);
}

/**
 * bdrv_merge_dirty_bitmap: merge src into dest.
 * Ensures permissions on bitmaps are reasonable; use for public API.
 *
 * @backup: If provided, make a copy of dest here prior to merge.
 *
 * Returns true on success, false on failure. In case of failure bitmaps are
 * untouched.
 */
bool bdrv_merge_dirty_bitmap(BdrvDirtyBitmap *dest, const BdrvDirtyBitmap *src,
                             HBitmap **backup, Error **errp)
{
    bool ret = false;

    bdrv_dirty_bitmaps_lock(dest->bs);
    if (src->bs != dest->bs) {
        bdrv_dirty_bitmaps_lock(src->bs);
    }

    if (bdrv_dirty_bitmap_check(dest, BDRV_BITMAP_DEFAULT, errp)) {
        goto out;
    }

    if (bdrv_dirty_bitmap_check(src, BDRV_BITMAP_ALLOW_RO, errp)) {
        goto out;
    }

    if (bdrv_dirty_bitmap_size(src) != bdrv_dirty_bitmap_size(dest)) {
        error_setg(errp, "Bitmaps are of different sizes (destination size is %"
                   PRId64 ", source size is %" PRId64 ") and can't be merged",
                   bdrv_dirty_bitmap_size(dest), bdrv_dirty_bitmap_size(src));
        goto out;
    }

    bdrv_dirty_bitmap_merge_internal(dest, src, backup, false);
    ret = true;

out:
    bdrv_dirty_bitmaps_unlock(dest->bs);
    if (src->bs != dest->bs) {
        bdrv_dirty_bitmaps_unlock(src->bs);
    }

    return ret;
}

/**
 * bdrv_dirty_bitmap_merge_internal: merge src into dest.
 * Does NOT check bitmap permissions; not suitable for use as public API.
 * @dest, @src and @backup (if not NULL) must have same size.
 *
 * @backup: If provided, make a copy of dest here prior to merge.
 * @lock: If true, lock and unlock bitmaps on the way in/out.
 */
void bdrv_dirty_bitmap_merge_internal(BdrvDirtyBitmap *dest,
                                      const BdrvDirtyBitmap *src,
                                      HBitmap **backup,
                                      bool lock)
{
    IO_CODE();

    assert(!bdrv_dirty_bitmap_readonly(dest));
    assert(!bdrv_dirty_bitmap_inconsistent(dest));
    assert(!bdrv_dirty_bitmap_inconsistent(src));

    if (lock) {
        bdrv_dirty_bitmaps_lock(dest->bs);
        if (src->bs != dest->bs) {
            bdrv_dirty_bitmaps_lock(src->bs);
        }
    }

    if (backup) {
        *backup = dest->bitmap;
        dest->bitmap = hbitmap_alloc(dest->size, hbitmap_granularity(*backup));
        hbitmap_merge(*backup, src->bitmap, dest->bitmap);
    } else {
        hbitmap_merge(dest->bitmap, src->bitmap, dest->bitmap);
    }

    if (lock) {
        bdrv_dirty_bitmaps_unlock(dest->bs);
        if (src->bs != dest->bs) {
            bdrv_dirty_bitmaps_unlock(src->bs);
        }
    }
}
