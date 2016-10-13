#ifndef BLOCK_DIRTY_BITMAP_H
#define BLOCK_DIRTY_BITMAP_H

#include "qemu-common.h"
#include "qemu/hbitmap.h"

BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs,
                                          uint32_t granularity,
                                          const char *name,
                                          Error **errp);
void bdrv_create_meta_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                                   int chunk_size);
void bdrv_release_meta_dirty_bitmap(BdrvDirtyBitmap *bitmap);
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
uint32_t bdrv_dirty_bitmap_meta_granularity(BdrvDirtyBitmap *bitmap);
bool bdrv_dirty_bitmap_enabled(BdrvDirtyBitmap *bitmap);
bool bdrv_dirty_bitmap_frozen(BdrvDirtyBitmap *bitmap);
const char *bdrv_dirty_bitmap_name(const BdrvDirtyBitmap *bitmap);
int64_t bdrv_dirty_bitmap_size(const BdrvDirtyBitmap *bitmap);
DirtyBitmapStatus bdrv_dirty_bitmap_status(BdrvDirtyBitmap *bitmap);
int bdrv_get_dirty(BlockDriverState *bs, BdrvDirtyBitmap *bitmap,
                   int64_t sector);
void bdrv_set_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                           int64_t cur_sector, int64_t nr_sectors);
void bdrv_reset_dirty_bitmap(BdrvDirtyBitmap *bitmap,
                             int64_t cur_sector, int64_t nr_sectors);
int bdrv_dirty_bitmap_get_meta(BlockDriverState *bs,
                               BdrvDirtyBitmap *bitmap, int64_t sector,
                               int nb_sectors);
void bdrv_dirty_bitmap_reset_meta(BlockDriverState *bs,
                                  BdrvDirtyBitmap *bitmap, int64_t sector,
                                  int nb_sectors);
BdrvDirtyBitmapIter *bdrv_dirty_meta_iter_new(BdrvDirtyBitmap *bitmap);
BdrvDirtyBitmapIter *bdrv_dirty_iter_new(BdrvDirtyBitmap *bitmap,
                                         uint64_t first_sector);
void bdrv_dirty_iter_free(BdrvDirtyBitmapIter *iter);
int64_t bdrv_dirty_iter_next(BdrvDirtyBitmapIter *iter);
void bdrv_set_dirty_iter(BdrvDirtyBitmapIter *hbi, int64_t sector_num);
int64_t bdrv_get_dirty_count(BdrvDirtyBitmap *bitmap);
int64_t bdrv_get_meta_dirty_count(BdrvDirtyBitmap *bitmap);
void bdrv_dirty_bitmap_truncate(BlockDriverState *bs);

uint64_t bdrv_dirty_bitmap_serialization_size(const BdrvDirtyBitmap *bitmap,
                                              uint64_t start, uint64_t count);
uint64_t bdrv_dirty_bitmap_serialization_align(const BdrvDirtyBitmap *bitmap);
void bdrv_dirty_bitmap_serialize_part(const BdrvDirtyBitmap *bitmap,
                                      uint8_t *buf, uint64_t start,
                                      uint64_t count);
void bdrv_dirty_bitmap_deserialize_part(BdrvDirtyBitmap *bitmap,
                                        uint8_t *buf, uint64_t start,
                                        uint64_t count, bool finish);
void bdrv_dirty_bitmap_deserialize_zeroes(BdrvDirtyBitmap *bitmap,
                                          uint64_t start, uint64_t count,
                                          bool finish);
void bdrv_dirty_bitmap_deserialize_finish(BdrvDirtyBitmap *bitmap);

#endif
