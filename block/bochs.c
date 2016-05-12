/*
 * Block driver for the various disk image formats used by Bochs
 * Currently only for "growing" type in read-only mode
 *
 * Copyright (c) 2005 Alex Beregszaszi
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
#include "block/block_int.h"
#include "qemu/module.h"

/**************************************************************/

#define HEADER_MAGIC "Bochs Virtual HD Image"
#define HEADER_VERSION 0x00020000
#define HEADER_V1 0x00010000
#define HEADER_SIZE 512

#define REDOLOG_TYPE "Redolog"
#define GROWING_TYPE "Growing"

// not allocated: 0xffffffff

// always little-endian
struct bochs_header {
    char magic[32];     /* "Bochs Virtual HD Image" */
    char type[16];      /* "Redolog" */
    char subtype[16];   /* "Undoable" / "Volatile" / "Growing" */
    uint32_t version;
    uint32_t header;    /* size of header */

    uint32_t catalog;   /* num of entries */
    uint32_t bitmap;    /* bitmap size */
    uint32_t extent;    /* extent size */

    union {
        struct {
            uint32_t reserved;  /* for ??? */
            uint64_t disk;      /* disk size */
            char padding[HEADER_SIZE - 64 - 20 - 12];
        } QEMU_PACKED redolog;
        struct {
            uint64_t disk;      /* disk size */
            char padding[HEADER_SIZE - 64 - 20 - 8];
        } QEMU_PACKED redolog_v1;
        char padding[HEADER_SIZE - 64 - 20];
    } extra;
} QEMU_PACKED;

typedef struct BDRVBochsState {
    CoMutex lock;
    uint32_t *catalog_bitmap;
    uint32_t catalog_size;

    uint32_t data_offset;

    uint32_t bitmap_blocks;
    uint32_t extent_blocks;
    uint32_t extent_size;
} BDRVBochsState;

static int bochs_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const struct bochs_header *bochs = (const void *)buf;

    if (buf_size < HEADER_SIZE)
	return 0;

    if (!strcmp(bochs->magic, HEADER_MAGIC) &&
	!strcmp(bochs->type, REDOLOG_TYPE) &&
	!strcmp(bochs->subtype, GROWING_TYPE) &&
	((le32_to_cpu(bochs->version) == HEADER_VERSION) ||
	(le32_to_cpu(bochs->version) == HEADER_V1)))
	return 100;

    return 0;
}

static int bochs_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVBochsState *s = bs->opaque;
    uint32_t i;
    struct bochs_header bochs;
    int ret;

    bs->read_only = 1; // no write support yet
    bs->request_alignment = BDRV_SECTOR_SIZE; /* No sub-sector I/O supported */

    ret = bdrv_pread(bs->file->bs, 0, &bochs, sizeof(bochs));
    if (ret < 0) {
        return ret;
    }

    if (strcmp(bochs.magic, HEADER_MAGIC) ||
        strcmp(bochs.type, REDOLOG_TYPE) ||
        strcmp(bochs.subtype, GROWING_TYPE) ||
	((le32_to_cpu(bochs.version) != HEADER_VERSION) &&
	(le32_to_cpu(bochs.version) != HEADER_V1))) {
        error_setg(errp, "Image not in Bochs format");
        return -EINVAL;
    }

    if (le32_to_cpu(bochs.version) == HEADER_V1) {
        bs->total_sectors = le64_to_cpu(bochs.extra.redolog_v1.disk) / 512;
    } else {
        bs->total_sectors = le64_to_cpu(bochs.extra.redolog.disk) / 512;
    }

    /* Limit to 1M entries to avoid unbounded allocation. This is what is
     * needed for the largest image that bximage can create (~8 TB). */
    s->catalog_size = le32_to_cpu(bochs.catalog);
    if (s->catalog_size > 0x100000) {
        error_setg(errp, "Catalog size is too large");
        return -EFBIG;
    }

    s->catalog_bitmap = g_try_new(uint32_t, s->catalog_size);
    if (s->catalog_size && s->catalog_bitmap == NULL) {
        error_setg(errp, "Could not allocate memory for catalog");
        return -ENOMEM;
    }

    ret = bdrv_pread(bs->file->bs, le32_to_cpu(bochs.header), s->catalog_bitmap,
                     s->catalog_size * 4);
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->catalog_size; i++)
	le32_to_cpus(&s->catalog_bitmap[i]);

    s->data_offset = le32_to_cpu(bochs.header) + (s->catalog_size * 4);

    s->bitmap_blocks = 1 + (le32_to_cpu(bochs.bitmap) - 1) / 512;
    s->extent_blocks = 1 + (le32_to_cpu(bochs.extent) - 1) / 512;

    s->extent_size = le32_to_cpu(bochs.extent);
    if (s->extent_size < BDRV_SECTOR_SIZE) {
        /* bximage actually never creates extents smaller than 4k */
        error_setg(errp, "Extent size must be at least 512");
        ret = -EINVAL;
        goto fail;
    } else if (!is_power_of_2(s->extent_size)) {
        error_setg(errp, "Extent size %" PRIu32 " is not a power of two",
                   s->extent_size);
        ret = -EINVAL;
        goto fail;
    } else if (s->extent_size > 0x800000) {
        error_setg(errp, "Extent size %" PRIu32 " is too large",
                   s->extent_size);
        ret = -EINVAL;
        goto fail;
    }

    if (s->catalog_size < DIV_ROUND_UP(bs->total_sectors,
                                       s->extent_size / BDRV_SECTOR_SIZE))
    {
        error_setg(errp, "Catalog size is too small for this disk size");
        ret = -EINVAL;
        goto fail;
    }

    qemu_co_mutex_init(&s->lock);
    return 0;

fail:
    g_free(s->catalog_bitmap);
    return ret;
}

static int64_t seek_to_sector(BlockDriverState *bs, int64_t sector_num)
{
    BDRVBochsState *s = bs->opaque;
    uint64_t offset = sector_num * 512;
    uint64_t extent_index, extent_offset, bitmap_offset;
    char bitmap_entry;
    int ret;

    // seek to sector
    extent_index = offset / s->extent_size;
    extent_offset = (offset % s->extent_size) / 512;

    if (s->catalog_bitmap[extent_index] == 0xffffffff) {
	return 0; /* not allocated */
    }

    bitmap_offset = s->data_offset +
        (512 * (uint64_t) s->catalog_bitmap[extent_index] *
        (s->extent_blocks + s->bitmap_blocks));

    /* read in bitmap for current extent */
    ret = bdrv_pread(bs->file->bs, bitmap_offset + (extent_offset / 8),
                     &bitmap_entry, 1);
    if (ret < 0) {
        return ret;
    }

    if (!((bitmap_entry >> (extent_offset % 8)) & 1)) {
	return 0; /* not allocated */
    }

    return bitmap_offset + (512 * (s->bitmap_blocks + extent_offset));
}

static int coroutine_fn
bochs_co_preadv(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                QEMUIOVector *qiov, int flags)
{
    BDRVBochsState *s = bs->opaque;
    uint64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    uint64_t bytes_done = 0;
    QEMUIOVector local_qiov;
    int ret;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    qemu_iovec_init(&local_qiov, qiov->niov);
    qemu_co_mutex_lock(&s->lock);

    while (nb_sectors > 0) {
        int64_t block_offset = seek_to_sector(bs, sector_num);
        if (block_offset < 0) {
            ret = block_offset;
            goto fail;
        }

        qemu_iovec_reset(&local_qiov);
        qemu_iovec_concat(&local_qiov, qiov, bytes_done, 512);

        if (block_offset > 0) {
            ret = bdrv_co_preadv(bs->file->bs, block_offset, 512,
                                 &local_qiov, 0);
            if (ret < 0) {
                goto fail;
            }
        } else {
            qemu_iovec_memset(&local_qiov, 0, 0, 512);
        }
        nb_sectors--;
        sector_num++;
        bytes_done += 512;
    }

    ret = 0;
fail:
    qemu_co_mutex_unlock(&s->lock);
    qemu_iovec_destroy(&local_qiov);

    return ret;
}

static void bochs_close(BlockDriverState *bs)
{
    BDRVBochsState *s = bs->opaque;
    g_free(s->catalog_bitmap);
}

static BlockDriver bdrv_bochs = {
    .format_name	= "bochs",
    .instance_size	= sizeof(BDRVBochsState),
    .bdrv_probe		= bochs_probe,
    .bdrv_open		= bochs_open,
    .bdrv_co_preadv = bochs_co_preadv,
    .bdrv_close		= bochs_close,
};

static void bdrv_bochs_init(void)
{
    bdrv_register(&bdrv_bochs);
}

block_init(bdrv_bochs_init);
