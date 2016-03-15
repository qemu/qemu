#ifndef BLOCK_DIRTY_BITMAP_H
#define BLOCK_DIRTY_BITMAP_H

#include "qemu-common.h"
#include "qemu/hbitmap.h"

BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs,
                                          uint32_t granularity,
                                          const char *name,
                                          Error **errp);
int bdrv_dirty_bitmap_create_successor(BlockDriverState *bs,
                                       BdrvDirtyBitmap *bitmap,
                                       Error **errp);
BdrvDirtyBitmap *bdrv_dirty_bitmap_abdicate(BlockDriverState *bs,
                                            BdrvDirtyBitmap *bitmap,
                                            Error **errp);
BdrvDirtyBitmap *bdrv_reclaim_dirty_bitmap(BlockDriverState *bs,
                                           BdrvDirtyBitmap *bitmap,
                                           Error **errp);
BdrvDirtyBitmap *bdrv_find_dirty_bitmap(BlockDriverState *bs,
                                        const char *name);
void bdrv_dirty_bitmap_make_anon(BdrvDirtyBitmap *bitmap);
void bdrv_release_dirty_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap);
void bdrv_release_named_dirty_bitmaps(BlockDriverState *bs);
void bdrv_disable_dirty_bitmap(BdrvDirtyBitmap *bitmap);
void bdrv_enable_dirty_bitmap(BdrvDirtyBitmap *bitmap);
BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs);
uint32_t bdrv_get_default_bitmap_granularity(BlockDriverState *bs);
uint32_t bdrv_dirty_bitmap_granularity(BdrvDirtyBitmap *bitmap);
bool bdrv_dirty_bitmap_enabled(BdrvDirtyBitmap *bitmap);
bool bdrv_dirty_bitmap_frozen(BdrvDirtyBitmap *bitmap);
DirtyBitmapStatus bdrv_dirty_bitmap_status(BdrvDirtyBitmap *bitmap);
int bdrv_get_dirty(BlockDriverState *bs, BdrvDirtyBitmap *bitmap,
                   int64_t sector);
void bdrv_set_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                           int64_t cur_sector, int nr_sectors);
void bdrv_reset_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                             int64_t cur_sector, int nr_sectors);
void bdrv_dirty_iter_init(BdrvDirtyBitmap *bitmap, struct HBitmapIter *hbi);
void bdrv_set_dirty_iter(struct HBitmapIter *hbi, int64_t offset);
int64_t bdrv_get_dirty_count(BdrvDirtyBitmap *bitmap);
void bdrv_dirty_bitmap_truncate(BlockDriverState *bs);

#endif
