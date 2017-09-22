/*
 * Bitmaps for the QCOW version 2 format
 *
 * Copyright (c) 2014-2017 Vladimir Sementsov-Ogievskiy
 *
 * This file is derived from qcow2-snapshot.c, original copyright:
 * Copyright (c) 2004-2006 Fabrice Bellard
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
#include "qemu/cutils.h"

#include "block/block_int.h"
#include "block/qcow2.h"

/* NOTICE: BME here means Bitmaps Extension and used as a namespace for
 * _internal_ constants. Please do not use this _internal_ abbreviation for
 * other needs and/or outside of this file. */

/* Bitmap directory entry constraints */
#define BME_MAX_TABLE_SIZE 0x8000000
#define BME_MAX_PHYS_SIZE 0x20000000 /* restrict BdrvDirtyBitmap size in RAM */
#define BME_MAX_GRANULARITY_BITS 31
#define BME_MIN_GRANULARITY_BITS 9
#define BME_MAX_NAME_SIZE 1023

#if BME_MAX_TABLE_SIZE * 8ULL > INT_MAX
#error In the code bitmap table physical size assumed to fit into int
#endif

/* Bitmap directory entry flags */
#define BME_RESERVED_FLAGS 0xfffffffcU
#define BME_FLAG_IN_USE (1U << 0)
#define BME_FLAG_AUTO   (1U << 1)

/* bits [1, 8] U [56, 63] are reserved */
#define BME_TABLE_ENTRY_RESERVED_MASK 0xff000000000001feULL
#define BME_TABLE_ENTRY_OFFSET_MASK 0x00fffffffffffe00ULL
#define BME_TABLE_ENTRY_FLAG_ALL_ONES (1ULL << 0)

typedef struct QEMU_PACKED Qcow2BitmapDirEntry {
    /* header is 8 byte aligned */
    uint64_t bitmap_table_offset;

    uint32_t bitmap_table_size;
    uint32_t flags;

    uint8_t type;
    uint8_t granularity_bits;
    uint16_t name_size;
    uint32_t extra_data_size;
    /* extra data follows  */
    /* name follows  */
} Qcow2BitmapDirEntry;

typedef struct Qcow2BitmapTable {
    uint64_t offset;
    uint32_t size; /* number of 64bit entries */
    QSIMPLEQ_ENTRY(Qcow2BitmapTable) entry;
} Qcow2BitmapTable;
typedef QSIMPLEQ_HEAD(Qcow2BitmapTableList, Qcow2BitmapTable)
    Qcow2BitmapTableList;

typedef struct Qcow2Bitmap {
    Qcow2BitmapTable table;
    uint32_t flags;
    uint8_t granularity_bits;
    char *name;

    BdrvDirtyBitmap *dirty_bitmap;

    QSIMPLEQ_ENTRY(Qcow2Bitmap) entry;
} Qcow2Bitmap;
typedef QSIMPLEQ_HEAD(Qcow2BitmapList, Qcow2Bitmap) Qcow2BitmapList;

typedef enum BitmapType {
    BT_DIRTY_TRACKING_BITMAP = 1
} BitmapType;

static inline bool can_write(BlockDriverState *bs)
{
    return !bdrv_is_read_only(bs) && !(bdrv_get_flags(bs) & BDRV_O_INACTIVE);
}

static int update_header_sync(BlockDriverState *bs)
{
    int ret;

    ret = qcow2_update_header(bs);
    if (ret < 0) {
        return ret;
    }

    return bdrv_flush(bs);
}

static inline void bitmap_table_to_be(uint64_t *bitmap_table, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i) {
        cpu_to_be64s(&bitmap_table[i]);
    }
}

static int check_table_entry(uint64_t entry, int cluster_size)
{
    uint64_t offset;

    if (entry & BME_TABLE_ENTRY_RESERVED_MASK) {
        return -EINVAL;
    }

    offset = entry & BME_TABLE_ENTRY_OFFSET_MASK;
    if (offset != 0) {
        /* if offset specified, bit 0 is reserved */
        if (entry & BME_TABLE_ENTRY_FLAG_ALL_ONES) {
            return -EINVAL;
        }

        if (offset % cluster_size != 0) {
            return -EINVAL;
        }
    }

    return 0;
}

static int check_constraints_on_bitmap(BlockDriverState *bs,
                                       const char *name,
                                       uint32_t granularity,
                                       Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    int granularity_bits = ctz32(granularity);
    int64_t len = bdrv_getlength(bs);

    assert(granularity > 0);
    assert((granularity & (granularity - 1)) == 0);

    if (len < 0) {
        error_setg_errno(errp, -len, "Failed to get size of '%s'",
                         bdrv_get_device_or_node_name(bs));
        return len;
    }

    if (granularity_bits > BME_MAX_GRANULARITY_BITS) {
        error_setg(errp, "Granularity exceeds maximum (%llu bytes)",
                   1ULL << BME_MAX_GRANULARITY_BITS);
        return -EINVAL;
    }
    if (granularity_bits < BME_MIN_GRANULARITY_BITS) {
        error_setg(errp, "Granularity is under minimum (%llu bytes)",
                   1ULL << BME_MIN_GRANULARITY_BITS);
        return -EINVAL;
    }

    if ((len > (uint64_t)BME_MAX_PHYS_SIZE << granularity_bits) ||
        (len > (uint64_t)BME_MAX_TABLE_SIZE * s->cluster_size <<
               granularity_bits))
    {
        error_setg(errp, "Too much space will be occupied by the bitmap. "
                   "Use larger granularity");
        return -EINVAL;
    }

    if (strlen(name) > BME_MAX_NAME_SIZE) {
        error_setg(errp, "Name length exceeds maximum (%u characters)",
                   BME_MAX_NAME_SIZE);
        return -EINVAL;
    }

    return 0;
}

static void clear_bitmap_table(BlockDriverState *bs, uint64_t *bitmap_table,
                               uint32_t bitmap_table_size)
{
    BDRVQcow2State *s = bs->opaque;
    int i;

    for (i = 0; i < bitmap_table_size; ++i) {
        uint64_t addr = bitmap_table[i] & BME_TABLE_ENTRY_OFFSET_MASK;
        if (!addr) {
            continue;
        }

        qcow2_free_clusters(bs, addr, s->cluster_size, QCOW2_DISCARD_OTHER);
        bitmap_table[i] = 0;
    }
}

static int bitmap_table_load(BlockDriverState *bs, Qcow2BitmapTable *tb,
                             uint64_t **bitmap_table)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    uint32_t i;
    uint64_t *table;

    assert(tb->size != 0);
    table = g_try_new(uint64_t, tb->size);
    if (table == NULL) {
        return -ENOMEM;
    }

    assert(tb->size <= BME_MAX_TABLE_SIZE);
    ret = bdrv_pread(bs->file, tb->offset,
                     table, tb->size * sizeof(uint64_t));
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < tb->size; ++i) {
        be64_to_cpus(&table[i]);
        ret = check_table_entry(table[i], s->cluster_size);
        if (ret < 0) {
            goto fail;
        }
    }

    *bitmap_table = table;
    return 0;

fail:
    g_free(table);

    return ret;
}

static int free_bitmap_clusters(BlockDriverState *bs, Qcow2BitmapTable *tb)
{
    int ret;
    uint64_t *bitmap_table;

    ret = bitmap_table_load(bs, tb, &bitmap_table);
    if (ret < 0) {
        assert(bitmap_table == NULL);
        return ret;
    }

    clear_bitmap_table(bs, bitmap_table, tb->size);
    qcow2_free_clusters(bs, tb->offset, tb->size * sizeof(uint64_t),
                        QCOW2_DISCARD_OTHER);
    g_free(bitmap_table);

    tb->offset = 0;
    tb->size = 0;

    return 0;
}

/* This function returns the number of disk sectors covered by a single qcow2
 * cluster of bitmap data. */
static uint64_t sectors_covered_by_bitmap_cluster(const BDRVQcow2State *s,
                                                  const BdrvDirtyBitmap *bitmap)
{
    uint32_t sector_granularity =
            bdrv_dirty_bitmap_granularity(bitmap) >> BDRV_SECTOR_BITS;

    return (uint64_t)sector_granularity * (s->cluster_size << 3);
}

/* load_bitmap_data
 * @bitmap_table entries must satisfy specification constraints.
 * @bitmap must be cleared */
static int load_bitmap_data(BlockDriverState *bs,
                            const uint64_t *bitmap_table,
                            uint32_t bitmap_table_size,
                            BdrvDirtyBitmap *bitmap)
{
    int ret = 0;
    BDRVQcow2State *s = bs->opaque;
    uint64_t sector, sbc;
    uint64_t bm_size = bdrv_dirty_bitmap_size(bitmap);
    uint8_t *buf = NULL;
    uint64_t i, tab_size =
            size_to_clusters(s,
                bdrv_dirty_bitmap_serialization_size(bitmap, 0, bm_size));

    if (tab_size != bitmap_table_size || tab_size > BME_MAX_TABLE_SIZE) {
        return -EINVAL;
    }

    buf = g_malloc(s->cluster_size);
    sbc = sectors_covered_by_bitmap_cluster(s, bitmap);
    for (i = 0, sector = 0; i < tab_size; ++i, sector += sbc) {
        uint64_t count = MIN(bm_size - sector, sbc);
        uint64_t entry = bitmap_table[i];
        uint64_t offset = entry & BME_TABLE_ENTRY_OFFSET_MASK;

        assert(check_table_entry(entry, s->cluster_size) == 0);

        if (offset == 0) {
            if (entry & BME_TABLE_ENTRY_FLAG_ALL_ONES) {
                bdrv_dirty_bitmap_deserialize_ones(bitmap, sector, count,
                                                   false);
            } else {
                /* No need to deserialize zeros because the dirty bitmap is
                 * already cleared */
            }
        } else {
            ret = bdrv_pread(bs->file, offset, buf, s->cluster_size);
            if (ret < 0) {
                goto finish;
            }
            bdrv_dirty_bitmap_deserialize_part(bitmap, buf, sector, count,
                                               false);
        }
    }
    ret = 0;

    bdrv_dirty_bitmap_deserialize_finish(bitmap);

finish:
    g_free(buf);

    return ret;
}

static BdrvDirtyBitmap *load_bitmap(BlockDriverState *bs,
                                    Qcow2Bitmap *bm, Error **errp)
{
    int ret;
    uint64_t *bitmap_table = NULL;
    uint32_t granularity;
    BdrvDirtyBitmap *bitmap = NULL;

    if (bm->flags & BME_FLAG_IN_USE) {
        error_setg(errp, "Bitmap '%s' is in use", bm->name);
        goto fail;
    }

    ret = bitmap_table_load(bs, &bm->table, &bitmap_table);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Could not read bitmap_table table from image for "
                         "bitmap '%s'", bm->name);
        goto fail;
    }

    granularity = 1U << bm->granularity_bits;
    bitmap = bdrv_create_dirty_bitmap(bs, granularity, bm->name, errp);
    if (bitmap == NULL) {
        goto fail;
    }

    ret = load_bitmap_data(bs, bitmap_table, bm->table.size, bitmap);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read bitmap '%s' from image",
                         bm->name);
        goto fail;
    }

    g_free(bitmap_table);
    return bitmap;

fail:
    g_free(bitmap_table);
    if (bitmap != NULL) {
        bdrv_release_dirty_bitmap(bs, bitmap);
    }

    return NULL;
}

/*
 * Bitmap List
 */

/*
 * Bitmap List private functions
 * Only Bitmap List knows about bitmap directory structure in Qcow2.
 */

static inline void bitmap_dir_entry_to_cpu(Qcow2BitmapDirEntry *entry)
{
    be64_to_cpus(&entry->bitmap_table_offset);
    be32_to_cpus(&entry->bitmap_table_size);
    be32_to_cpus(&entry->flags);
    be16_to_cpus(&entry->name_size);
    be32_to_cpus(&entry->extra_data_size);
}

static inline void bitmap_dir_entry_to_be(Qcow2BitmapDirEntry *entry)
{
    cpu_to_be64s(&entry->bitmap_table_offset);
    cpu_to_be32s(&entry->bitmap_table_size);
    cpu_to_be32s(&entry->flags);
    cpu_to_be16s(&entry->name_size);
    cpu_to_be32s(&entry->extra_data_size);
}

static inline int calc_dir_entry_size(size_t name_size, size_t extra_data_size)
{
    return align_offset(sizeof(Qcow2BitmapDirEntry) +
                        name_size + extra_data_size, 8);
}

static inline int dir_entry_size(Qcow2BitmapDirEntry *entry)
{
    return calc_dir_entry_size(entry->name_size, entry->extra_data_size);
}

static inline const char *dir_entry_name_field(Qcow2BitmapDirEntry *entry)
{
    return (const char *)(entry + 1) + entry->extra_data_size;
}

static inline char *dir_entry_copy_name(Qcow2BitmapDirEntry *entry)
{
    const char *name_field = dir_entry_name_field(entry);
    return g_strndup(name_field, entry->name_size);
}

static inline Qcow2BitmapDirEntry *next_dir_entry(Qcow2BitmapDirEntry *entry)
{
    return (Qcow2BitmapDirEntry *)((uint8_t *)entry + dir_entry_size(entry));
}

static int check_dir_entry(BlockDriverState *bs, Qcow2BitmapDirEntry *entry)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t phys_bitmap_bytes;
    int64_t len;

    bool fail = (entry->bitmap_table_size == 0) ||
                (entry->bitmap_table_offset == 0) ||
                (entry->bitmap_table_offset % s->cluster_size) ||
                (entry->bitmap_table_size > BME_MAX_TABLE_SIZE) ||
                (entry->granularity_bits > BME_MAX_GRANULARITY_BITS) ||
                (entry->granularity_bits < BME_MIN_GRANULARITY_BITS) ||
                (entry->flags & BME_RESERVED_FLAGS) ||
                (entry->name_size > BME_MAX_NAME_SIZE) ||
                (entry->type != BT_DIRTY_TRACKING_BITMAP);

    if (fail) {
        return -EINVAL;
    }

    phys_bitmap_bytes = (uint64_t)entry->bitmap_table_size * s->cluster_size;
    len = bdrv_getlength(bs);

    if (len < 0) {
        return len;
    }

    fail = (phys_bitmap_bytes > BME_MAX_PHYS_SIZE) ||
           (len > ((phys_bitmap_bytes * 8) << entry->granularity_bits));

    return fail ? -EINVAL : 0;
}

static inline void bitmap_directory_to_be(uint8_t *dir, size_t size)
{
    uint8_t *end = dir + size;
    while (dir < end) {
        Qcow2BitmapDirEntry *e = (Qcow2BitmapDirEntry *)dir;
        dir += dir_entry_size(e);

        bitmap_dir_entry_to_be(e);
    }
}

/*
 * Bitmap List public functions
 */

static void bitmap_free(Qcow2Bitmap *bm)
{
    if (bm == NULL) {
        return;
    }

    g_free(bm->name);
    g_free(bm);
}

static void bitmap_list_free(Qcow2BitmapList *bm_list)
{
    Qcow2Bitmap *bm;

    if (bm_list == NULL) {
        return;
    }

    while ((bm = QSIMPLEQ_FIRST(bm_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(bm_list, entry);
        bitmap_free(bm);
    }

    g_free(bm_list);
}

static Qcow2BitmapList *bitmap_list_new(void)
{
    Qcow2BitmapList *bm_list = g_new(Qcow2BitmapList, 1);
    QSIMPLEQ_INIT(bm_list);

    return bm_list;
}

static uint32_t bitmap_list_count(Qcow2BitmapList *bm_list)
{
    Qcow2Bitmap *bm;
    uint32_t nb_bitmaps = 0;

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        nb_bitmaps++;
    }

    return nb_bitmaps;
}

/* bitmap_list_load
 * Get bitmap list from qcow2 image. Actually reads bitmap directory,
 * checks it and convert to bitmap list.
 */
static Qcow2BitmapList *bitmap_list_load(BlockDriverState *bs, uint64_t offset,
                                         uint64_t size, Error **errp)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    uint8_t *dir, *dir_end;
    Qcow2BitmapDirEntry *e;
    uint32_t nb_dir_entries = 0;
    Qcow2BitmapList *bm_list = NULL;

    if (size == 0) {
        error_setg(errp, "Requested bitmap directory size is zero");
        return NULL;
    }

    if (size > QCOW2_MAX_BITMAP_DIRECTORY_SIZE) {
        error_setg(errp, "Requested bitmap directory size is too big");
        return NULL;
    }

    dir = g_try_malloc(size);
    if (dir == NULL) {
        error_setg(errp, "Failed to allocate space for bitmap directory");
        return NULL;
    }
    dir_end = dir + size;

    ret = bdrv_pread(bs->file, offset, dir, size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to read bitmap directory");
        goto fail;
    }

    bm_list = bitmap_list_new();
    for (e = (Qcow2BitmapDirEntry *)dir;
         e < (Qcow2BitmapDirEntry *)dir_end;
         e = next_dir_entry(e))
    {
        Qcow2Bitmap *bm;

        if ((uint8_t *)(e + 1) > dir_end) {
            goto broken_dir;
        }

        if (++nb_dir_entries > s->nb_bitmaps) {
            error_setg(errp, "More bitmaps found than specified in header"
                       " extension");
            goto fail;
        }
        bitmap_dir_entry_to_cpu(e);

        if ((uint8_t *)next_dir_entry(e) > dir_end) {
            goto broken_dir;
        }

        if (e->extra_data_size != 0) {
            error_setg(errp, "Bitmap extra data is not supported");
            goto fail;
        }

        ret = check_dir_entry(bs, e);
        if (ret < 0) {
            error_setg(errp, "Bitmap '%.*s' doesn't satisfy the constraints",
                       e->name_size, dir_entry_name_field(e));
            goto fail;
        }

        bm = g_new0(Qcow2Bitmap, 1);
        bm->table.offset = e->bitmap_table_offset;
        bm->table.size = e->bitmap_table_size;
        bm->flags = e->flags;
        bm->granularity_bits = e->granularity_bits;
        bm->name = dir_entry_copy_name(e);
        QSIMPLEQ_INSERT_TAIL(bm_list, bm, entry);
    }

    if (nb_dir_entries != s->nb_bitmaps) {
        error_setg(errp, "Less bitmaps found than specified in header"
                         " extension");
        goto fail;
    }

    if ((uint8_t *)e != dir_end) {
        goto broken_dir;
    }

    g_free(dir);
    return bm_list;

broken_dir:
    ret = -EINVAL;
    error_setg(errp, "Broken bitmap directory");

fail:
    g_free(dir);
    bitmap_list_free(bm_list);

    return NULL;
}

int qcow2_check_bitmaps_refcounts(BlockDriverState *bs, BdrvCheckResult *res,
                                  void **refcount_table,
                                  int64_t *refcount_table_size)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    Qcow2BitmapList *bm_list;
    Qcow2Bitmap *bm;

    if (s->nb_bitmaps == 0) {
        return 0;
    }

    ret = qcow2_inc_refcounts_imrt(bs, res, refcount_table, refcount_table_size,
                                   s->bitmap_directory_offset,
                                   s->bitmap_directory_size);
    if (ret < 0) {
        return ret;
    }

    bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                               s->bitmap_directory_size, NULL);
    if (bm_list == NULL) {
        res->corruptions++;
        return -EINVAL;
    }

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        uint64_t *bitmap_table = NULL;
        int i;

        ret = qcow2_inc_refcounts_imrt(bs, res,
                                       refcount_table, refcount_table_size,
                                       bm->table.offset,
                                       bm->table.size * sizeof(uint64_t));
        if (ret < 0) {
            goto out;
        }

        ret = bitmap_table_load(bs, &bm->table, &bitmap_table);
        if (ret < 0) {
            res->corruptions++;
            goto out;
        }

        for (i = 0; i < bm->table.size; ++i) {
            uint64_t entry = bitmap_table[i];
            uint64_t offset = entry & BME_TABLE_ENTRY_OFFSET_MASK;

            if (check_table_entry(entry, s->cluster_size) < 0) {
                res->corruptions++;
                continue;
            }

            if (offset == 0) {
                continue;
            }

            ret = qcow2_inc_refcounts_imrt(bs, res,
                                           refcount_table, refcount_table_size,
                                           offset, s->cluster_size);
            if (ret < 0) {
                g_free(bitmap_table);
                goto out;
            }
        }

        g_free(bitmap_table);
    }

out:
    bitmap_list_free(bm_list);

    return ret;
}

/* bitmap_list_store
 * Store bitmap list to qcow2 image as a bitmap directory.
 * Everything is checked.
 */
static int bitmap_list_store(BlockDriverState *bs, Qcow2BitmapList *bm_list,
                             uint64_t *offset, uint64_t *size, bool in_place)
{
    int ret;
    uint8_t *dir;
    int64_t dir_offset = 0;
    uint64_t dir_size = 0;
    Qcow2Bitmap *bm;
    Qcow2BitmapDirEntry *e;

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        dir_size += calc_dir_entry_size(strlen(bm->name), 0);
    }

    if (dir_size == 0 || dir_size > QCOW2_MAX_BITMAP_DIRECTORY_SIZE) {
        return -EINVAL;
    }

    if (in_place) {
        if (*size != dir_size || *offset == 0) {
            return -EINVAL;
        }

        dir_offset = *offset;
    }

    dir = g_try_malloc(dir_size);
    if (dir == NULL) {
        return -ENOMEM;
    }

    e = (Qcow2BitmapDirEntry *)dir;
    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        e->bitmap_table_offset = bm->table.offset;
        e->bitmap_table_size = bm->table.size;
        e->flags = bm->flags;
        e->type = BT_DIRTY_TRACKING_BITMAP;
        e->granularity_bits = bm->granularity_bits;
        e->name_size = strlen(bm->name);
        e->extra_data_size = 0;
        memcpy(e + 1, bm->name, e->name_size);

        if (check_dir_entry(bs, e) < 0) {
            ret = -EINVAL;
            goto fail;
        }

        e = next_dir_entry(e);
    }

    bitmap_directory_to_be(dir, dir_size);

    if (!in_place) {
        dir_offset = qcow2_alloc_clusters(bs, dir_size);
        if (dir_offset < 0) {
            ret = dir_offset;
            goto fail;
        }
    }

    ret = qcow2_pre_write_overlap_check(bs, 0, dir_offset, dir_size);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_pwrite(bs->file, dir_offset, dir, dir_size);
    if (ret < 0) {
        goto fail;
    }

    g_free(dir);

    if (!in_place) {
        *size = dir_size;
        *offset = dir_offset;
    }

    return 0;

fail:
    g_free(dir);

    if (!in_place && dir_offset > 0) {
        qcow2_free_clusters(bs, dir_offset, dir_size, QCOW2_DISCARD_OTHER);
    }

    return ret;
}

/*
 * Bitmap List end
 */

static int update_ext_header_and_dir_in_place(BlockDriverState *bs,
                                              Qcow2BitmapList *bm_list)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    if (!(s->autoclear_features & QCOW2_AUTOCLEAR_BITMAPS) ||
        bm_list == NULL || QSIMPLEQ_EMPTY(bm_list) ||
        bitmap_list_count(bm_list) != s->nb_bitmaps)
    {
        return -EINVAL;
    }

    s->autoclear_features &= ~(uint64_t)QCOW2_AUTOCLEAR_BITMAPS;
    ret = update_header_sync(bs);
    if (ret < 0) {
        /* Two variants are possible here:
         * 1. Autoclear flag is dropped, all bitmaps will be lost.
         * 2. Autoclear flag is not dropped, old state is left.
         */
        return ret;
    }

    /* autoclear bit is not set, so we can safely update bitmap directory */

    ret = bitmap_list_store(bs, bm_list, &s->bitmap_directory_offset,
                            &s->bitmap_directory_size, true);
    if (ret < 0) {
        /* autoclear bit is cleared, so all leaked clusters would be removed on
         * qemu-img check */
        return ret;
    }

    ret = update_header_sync(bs);
    if (ret < 0) {
        /* autoclear bit is cleared, so all leaked clusters would be removed on
         * qemu-img check */
        return ret;
    }

    s->autoclear_features |= QCOW2_AUTOCLEAR_BITMAPS;
    return update_header_sync(bs);
    /* If final update_header_sync() fails, two variants are possible:
     * 1. Autoclear flag is not set, all bitmaps will be lost.
     * 2. Autoclear flag is set, header and directory are successfully updated.
     */
}

static int update_ext_header_and_dir(BlockDriverState *bs,
                                     Qcow2BitmapList *bm_list)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;
    uint64_t new_offset = 0;
    uint64_t new_size = 0;
    uint32_t new_nb_bitmaps = 0;
    uint64_t old_offset = s->bitmap_directory_offset;
    uint64_t old_size = s->bitmap_directory_size;
    uint32_t old_nb_bitmaps = s->nb_bitmaps;
    uint64_t old_autocl = s->autoclear_features;

    if (bm_list != NULL && !QSIMPLEQ_EMPTY(bm_list)) {
        new_nb_bitmaps = bitmap_list_count(bm_list);

        if (new_nb_bitmaps > QCOW2_MAX_BITMAPS) {
            return -EINVAL;
        }

        ret = bitmap_list_store(bs, bm_list, &new_offset, &new_size, false);
        if (ret < 0) {
            return ret;
        }

        ret = bdrv_flush(bs->file->bs);
        if (ret < 0) {
            goto fail;
        }

        s->autoclear_features |= QCOW2_AUTOCLEAR_BITMAPS;
    } else {
        s->autoclear_features &= ~(uint64_t)QCOW2_AUTOCLEAR_BITMAPS;
    }

    s->bitmap_directory_offset = new_offset;
    s->bitmap_directory_size = new_size;
    s->nb_bitmaps = new_nb_bitmaps;

    ret = update_header_sync(bs);
    if (ret < 0) {
        goto fail;
    }

    if (old_size > 0) {
        qcow2_free_clusters(bs, old_offset, old_size, QCOW2_DISCARD_OTHER);
    }

    return 0;

fail:
    if (new_offset > 0) {
        qcow2_free_clusters(bs, new_offset, new_size, QCOW2_DISCARD_OTHER);
    }

    s->bitmap_directory_offset = old_offset;
    s->bitmap_directory_size = old_size;
    s->nb_bitmaps = old_nb_bitmaps;
    s->autoclear_features = old_autocl;

    return ret;
}

/* for g_slist_foreach for GSList of BdrvDirtyBitmap* elements */
static void release_dirty_bitmap_helper(gpointer bitmap,
                                        gpointer bs)
{
    bdrv_release_dirty_bitmap(bs, bitmap);
}

/* for g_slist_foreach for GSList of BdrvDirtyBitmap* elements */
static void set_readonly_helper(gpointer bitmap, gpointer value)
{
    bdrv_dirty_bitmap_set_readonly(bitmap, (bool)value);
}

/* qcow2_load_autoloading_dirty_bitmaps()
 * Return value is a hint for caller: true means that the Qcow2 header was
 * updated. (false doesn't mean that the header should be updated by the
 * caller, it just means that updating was not needed or the image cannot be
 * written to).
 * On failure the function returns false.
 */
bool qcow2_load_autoloading_dirty_bitmaps(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2BitmapList *bm_list;
    Qcow2Bitmap *bm;
    GSList *created_dirty_bitmaps = NULL;
    bool header_updated = false;

    if (s->nb_bitmaps == 0) {
        /* No bitmaps - nothing to do */
        return false;
    }

    bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                               s->bitmap_directory_size, errp);
    if (bm_list == NULL) {
        return false;
    }

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        if ((bm->flags & BME_FLAG_AUTO) && !(bm->flags & BME_FLAG_IN_USE)) {
            BdrvDirtyBitmap *bitmap = load_bitmap(bs, bm, errp);
            if (bitmap == NULL) {
                goto fail;
            }

            bdrv_dirty_bitmap_set_persistance(bitmap, true);
            bdrv_dirty_bitmap_set_autoload(bitmap, true);
            bm->flags |= BME_FLAG_IN_USE;
            created_dirty_bitmaps =
                    g_slist_append(created_dirty_bitmaps, bitmap);
        }
    }

    if (created_dirty_bitmaps != NULL) {
        if (can_write(bs)) {
            /* in_use flags must be updated */
            int ret = update_ext_header_and_dir_in_place(bs, bm_list);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Can't update bitmap directory");
                goto fail;
            }
            header_updated = true;
        } else {
            g_slist_foreach(created_dirty_bitmaps, set_readonly_helper,
                            (gpointer)true);
        }
    }

    g_slist_free(created_dirty_bitmaps);
    bitmap_list_free(bm_list);

    return header_updated;

fail:
    g_slist_foreach(created_dirty_bitmaps, release_dirty_bitmap_helper, bs);
    g_slist_free(created_dirty_bitmaps);
    bitmap_list_free(bm_list);

    return false;
}

int qcow2_reopen_bitmaps_rw(BlockDriverState *bs, Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    Qcow2BitmapList *bm_list;
    Qcow2Bitmap *bm;
    GSList *ro_dirty_bitmaps = NULL;
    int ret = 0;

    if (s->nb_bitmaps == 0) {
        /* No bitmaps - nothing to do */
        return 0;
    }

    if (!can_write(bs)) {
        error_setg(errp, "Can't write to the image on reopening bitmaps rw");
        return -EINVAL;
    }

    bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                               s->bitmap_directory_size, errp);
    if (bm_list == NULL) {
        return -EINVAL;
    }

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        if (!(bm->flags & BME_FLAG_IN_USE)) {
            BdrvDirtyBitmap *bitmap = bdrv_find_dirty_bitmap(bs, bm->name);
            if (bitmap == NULL) {
                continue;
            }

            if (!bdrv_dirty_bitmap_readonly(bitmap)) {
                error_setg(errp, "Bitmap %s is not readonly but not marked"
                                 "'IN_USE' in the image. Something went wrong,"
                                 "all the bitmaps may be corrupted", bm->name);
                ret = -EINVAL;
                goto out;
            }

            bm->flags |= BME_FLAG_IN_USE;
            ro_dirty_bitmaps = g_slist_append(ro_dirty_bitmaps, bitmap);
        }
    }

    if (ro_dirty_bitmaps != NULL) {
        /* in_use flags must be updated */
        ret = update_ext_header_and_dir_in_place(bs, bm_list);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Can't update bitmap directory");
            goto out;
        }
        g_slist_foreach(ro_dirty_bitmaps, set_readonly_helper, false);
    }

out:
    g_slist_free(ro_dirty_bitmaps);
    bitmap_list_free(bm_list);

    return ret;
}

/* store_bitmap_data()
 * Store bitmap to image, filling bitmap table accordingly.
 */
static uint64_t *store_bitmap_data(BlockDriverState *bs,
                                   BdrvDirtyBitmap *bitmap,
                                   uint32_t *bitmap_table_size, Error **errp)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    int64_t sector;
    uint64_t sbc;
    uint64_t bm_size = bdrv_dirty_bitmap_size(bitmap);
    const char *bm_name = bdrv_dirty_bitmap_name(bitmap);
    uint8_t *buf = NULL;
    BdrvDirtyBitmapIter *dbi;
    uint64_t *tb;
    uint64_t tb_size =
            size_to_clusters(s,
                bdrv_dirty_bitmap_serialization_size(bitmap, 0, bm_size));

    if (tb_size > BME_MAX_TABLE_SIZE ||
        tb_size * s->cluster_size > BME_MAX_PHYS_SIZE)
    {
        error_setg(errp, "Bitmap '%s' is too big", bm_name);
        return NULL;
    }

    tb = g_try_new0(uint64_t, tb_size);
    if (tb == NULL) {
        error_setg(errp, "No memory");
        return NULL;
    }

    dbi = bdrv_dirty_iter_new(bitmap, 0);
    buf = g_malloc(s->cluster_size);
    sbc = sectors_covered_by_bitmap_cluster(s, bitmap);
    assert(DIV_ROUND_UP(bm_size, sbc) == tb_size);

    while ((sector = bdrv_dirty_iter_next(dbi)) != -1) {
        uint64_t cluster = sector / sbc;
        uint64_t end, write_size;
        int64_t off;

        sector = cluster * sbc;
        end = MIN(bm_size, sector + sbc);
        write_size =
            bdrv_dirty_bitmap_serialization_size(bitmap, sector, end - sector);
        assert(write_size <= s->cluster_size);

        off = qcow2_alloc_clusters(bs, s->cluster_size);
        if (off < 0) {
            error_setg_errno(errp, -off,
                             "Failed to allocate clusters for bitmap '%s'",
                             bm_name);
            goto fail;
        }
        tb[cluster] = off;

        bdrv_dirty_bitmap_serialize_part(bitmap, buf, sector, end - sector);
        if (write_size < s->cluster_size) {
            memset(buf + write_size, 0, s->cluster_size - write_size);
        }

        ret = qcow2_pre_write_overlap_check(bs, 0, off, s->cluster_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Qcow2 overlap check failed");
            goto fail;
        }

        ret = bdrv_pwrite(bs->file, off, buf, s->cluster_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to write bitmap '%s' to file",
                             bm_name);
            goto fail;
        }

        if (end >= bm_size) {
            break;
        }

        bdrv_set_dirty_iter(dbi, end);
    }

    *bitmap_table_size = tb_size;
    g_free(buf);
    bdrv_dirty_iter_free(dbi);

    return tb;

fail:
    clear_bitmap_table(bs, tb, tb_size);
    g_free(buf);
    bdrv_dirty_iter_free(dbi);
    g_free(tb);

    return NULL;
}

/* store_bitmap()
 * Store bm->dirty_bitmap to qcow2.
 * Set bm->table_offset and bm->table_size accordingly.
 */
static int store_bitmap(BlockDriverState *bs, Qcow2Bitmap *bm, Error **errp)
{
    int ret;
    uint64_t *tb;
    int64_t tb_offset;
    uint32_t tb_size;
    BdrvDirtyBitmap *bitmap = bm->dirty_bitmap;
    const char *bm_name;

    assert(bitmap != NULL);

    bm_name = bdrv_dirty_bitmap_name(bitmap);

    tb = store_bitmap_data(bs, bitmap, &tb_size, errp);
    if (tb == NULL) {
        return -EINVAL;
    }

    assert(tb_size <= BME_MAX_TABLE_SIZE);
    tb_offset = qcow2_alloc_clusters(bs, tb_size * sizeof(tb[0]));
    if (tb_offset < 0) {
        error_setg_errno(errp, -tb_offset,
                         "Failed to allocate clusters for bitmap '%s'",
                         bm_name);
        ret = tb_offset;
        goto fail;
    }

    ret = qcow2_pre_write_overlap_check(bs, 0, tb_offset,
                                        tb_size * sizeof(tb[0]));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Qcow2 overlap check failed");
        goto fail;
    }

    bitmap_table_to_be(tb, tb_size);
    ret = bdrv_pwrite(bs->file, tb_offset, tb, tb_size * sizeof(tb[0]));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to write bitmap '%s' to file",
                         bm_name);
        goto fail;
    }

    g_free(tb);

    bm->table.offset = tb_offset;
    bm->table.size = tb_size;

    return 0;

fail:
    clear_bitmap_table(bs, tb, tb_size);

    if (tb_offset > 0) {
        qcow2_free_clusters(bs, tb_offset, tb_size * sizeof(tb[0]),
                            QCOW2_DISCARD_OTHER);
    }

    g_free(tb);

    return ret;
}

static Qcow2Bitmap *find_bitmap_by_name(Qcow2BitmapList *bm_list,
                                        const char *name)
{
    Qcow2Bitmap *bm;

    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        if (strcmp(name, bm->name) == 0) {
            return bm;
        }
    }

    return NULL;
}

void qcow2_remove_persistent_dirty_bitmap(BlockDriverState *bs,
                                          const char *name,
                                          Error **errp)
{
    int ret;
    BDRVQcow2State *s = bs->opaque;
    Qcow2Bitmap *bm;
    Qcow2BitmapList *bm_list;

    if (s->nb_bitmaps == 0) {
        /* Absence of the bitmap is not an error: see explanation above
         * bdrv_remove_persistent_dirty_bitmap() definition. */
        return;
    }

    bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                               s->bitmap_directory_size, errp);
    if (bm_list == NULL) {
        return;
    }

    bm = find_bitmap_by_name(bm_list, name);
    if (bm == NULL) {
        goto fail;
    }

    QSIMPLEQ_REMOVE(bm_list, bm, Qcow2Bitmap, entry);

    ret = update_ext_header_and_dir(bs, bm_list);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to update bitmap extension");
        goto fail;
    }

    free_bitmap_clusters(bs, &bm->table);

fail:
    bitmap_free(bm);
    bitmap_list_free(bm_list);
}

void qcow2_store_persistent_dirty_bitmaps(BlockDriverState *bs, Error **errp)
{
    BdrvDirtyBitmap *bitmap;
    BDRVQcow2State *s = bs->opaque;
    uint32_t new_nb_bitmaps = s->nb_bitmaps;
    uint64_t new_dir_size = s->bitmap_directory_size;
    int ret;
    Qcow2BitmapList *bm_list;
    Qcow2Bitmap *bm;
    Qcow2BitmapTableList drop_tables;
    Qcow2BitmapTable *tb, *tb_next;

    if (!bdrv_has_changed_persistent_bitmaps(bs)) {
        /* nothing to do */
        return;
    }

    if (!can_write(bs)) {
        error_setg(errp, "No write access");
        return;
    }

    QSIMPLEQ_INIT(&drop_tables);

    if (s->nb_bitmaps == 0) {
        bm_list = bitmap_list_new();
    } else {
        bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                                   s->bitmap_directory_size, errp);
        if (bm_list == NULL) {
            return;
        }
    }

    /* check constraints and names */
    for (bitmap = bdrv_dirty_bitmap_next(bs, NULL); bitmap != NULL;
         bitmap = bdrv_dirty_bitmap_next(bs, bitmap))
    {
        const char *name = bdrv_dirty_bitmap_name(bitmap);
        uint32_t granularity = bdrv_dirty_bitmap_granularity(bitmap);
        Qcow2Bitmap *bm;

        if (!bdrv_dirty_bitmap_get_persistance(bitmap) ||
            bdrv_dirty_bitmap_readonly(bitmap))
        {
            continue;
        }

        if (check_constraints_on_bitmap(bs, name, granularity, errp) < 0) {
            error_prepend(errp, "Bitmap '%s' doesn't satisfy the constraints: ",
                          name);
            goto fail;
        }

        bm = find_bitmap_by_name(bm_list, name);
        if (bm == NULL) {
            if (++new_nb_bitmaps > QCOW2_MAX_BITMAPS) {
                error_setg(errp, "Too many persistent bitmaps");
                goto fail;
            }

            new_dir_size += calc_dir_entry_size(strlen(name), 0);
            if (new_dir_size > QCOW2_MAX_BITMAP_DIRECTORY_SIZE) {
                error_setg(errp, "Bitmap directory is too large");
                goto fail;
            }

            bm = g_new0(Qcow2Bitmap, 1);
            bm->name = g_strdup(name);
            QSIMPLEQ_INSERT_TAIL(bm_list, bm, entry);
        } else {
            if (!(bm->flags & BME_FLAG_IN_USE)) {
                error_setg(errp, "Bitmap '%s' already exists in the image",
                           name);
                goto fail;
            }
            tb = g_memdup(&bm->table, sizeof(bm->table));
            bm->table.offset = 0;
            bm->table.size = 0;
            QSIMPLEQ_INSERT_TAIL(&drop_tables, tb, entry);
        }
        bm->flags = bdrv_dirty_bitmap_get_autoload(bitmap) ? BME_FLAG_AUTO : 0;
        bm->granularity_bits = ctz32(bdrv_dirty_bitmap_granularity(bitmap));
        bm->dirty_bitmap = bitmap;
    }

    /* allocate clusters and store bitmaps */
    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        if (bm->dirty_bitmap == NULL) {
            continue;
        }

        ret = store_bitmap(bs, bm, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = update_ext_header_and_dir(bs, bm_list);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to update bitmap extension");
        goto fail;
    }

    /* Bitmap directory was successfully updated, so, old data can be dropped.
     * TODO it is better to reuse these clusters */
    QSIMPLEQ_FOREACH_SAFE(tb, &drop_tables, entry, tb_next) {
        free_bitmap_clusters(bs, tb);
        g_free(tb);
    }

    bitmap_list_free(bm_list);
    return;

fail:
    QSIMPLEQ_FOREACH(bm, bm_list, entry) {
        if (bm->dirty_bitmap == NULL || bm->table.offset == 0) {
            continue;
        }

        free_bitmap_clusters(bs, &bm->table);
    }

    QSIMPLEQ_FOREACH_SAFE(tb, &drop_tables, entry, tb_next) {
        g_free(tb);
    }

    bitmap_list_free(bm_list);
}

int qcow2_reopen_bitmaps_ro(BlockDriverState *bs, Error **errp)
{
    BdrvDirtyBitmap *bitmap;
    Error *local_err = NULL;

    qcow2_store_persistent_dirty_bitmaps(bs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    for (bitmap = bdrv_dirty_bitmap_next(bs, NULL); bitmap != NULL;
         bitmap = bdrv_dirty_bitmap_next(bs, bitmap))
    {
        if (bdrv_dirty_bitmap_get_persistance(bitmap)) {
            bdrv_dirty_bitmap_set_readonly(bitmap, true);
        }
    }

    return 0;
}

bool qcow2_can_store_new_dirty_bitmap(BlockDriverState *bs,
                                      const char *name,
                                      uint32_t granularity,
                                      Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    bool found;
    Qcow2BitmapList *bm_list;

    if (check_constraints_on_bitmap(bs, name, granularity, errp) != 0) {
        goto fail;
    }

    if (s->nb_bitmaps == 0) {
        return true;
    }

    if (s->nb_bitmaps >= QCOW2_MAX_BITMAPS) {
        error_setg(errp,
                   "Maximum number of persistent bitmaps is already reached");
        goto fail;
    }

    if (s->bitmap_directory_size + calc_dir_entry_size(strlen(name), 0) >
        QCOW2_MAX_BITMAP_DIRECTORY_SIZE)
    {
        error_setg(errp, "Not enough space in the bitmap directory");
        goto fail;
    }

    bm_list = bitmap_list_load(bs, s->bitmap_directory_offset,
                               s->bitmap_directory_size, errp);
    if (bm_list == NULL) {
        goto fail;
    }

    found = find_bitmap_by_name(bm_list, name);
    bitmap_list_free(bm_list);
    if (found) {
        error_setg(errp, "Bitmap with the same name is already stored");
        goto fail;
    }

    return true;

fail:
    error_prepend(errp, "Can't make bitmap '%s' persistent in '%s': ",
                  name, bdrv_get_device_or_node_name(bs));
    return false;
}
