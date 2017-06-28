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

/* Bitmap directory entry flags */
#define BME_RESERVED_FLAGS 0xfffffffcU

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

typedef struct Qcow2Bitmap {
    Qcow2BitmapTable table;
    uint32_t flags;
    uint8_t granularity_bits;
    char *name;

    QSIMPLEQ_ENTRY(Qcow2Bitmap) entry;
} Qcow2Bitmap;
typedef QSIMPLEQ_HEAD(Qcow2BitmapList, Qcow2Bitmap) Qcow2BitmapList;

typedef enum BitmapType {
    BT_DIRTY_TRACKING_BITMAP = 1
} BitmapType;

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

/*
 * Bitmap List public functions
 */

static void bitmap_free(Qcow2Bitmap *bm)
{
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

        bm = g_new(Qcow2Bitmap, 1);
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
