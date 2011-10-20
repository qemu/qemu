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
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

/**************************************************************/

#define HEADER_MAGIC "Bochs Virtual HD Image"
#define HEADER_VERSION 0x00020000
#define HEADER_V1 0x00010000
#define HEADER_SIZE 512

#define REDOLOG_TYPE "Redolog"
#define GROWING_TYPE "Growing"

// not allocated: 0xffffffff

// always little-endian
struct bochs_header_v1 {
    char magic[32]; // "Bochs Virtual HD Image"
    char type[16]; // "Redolog"
    char subtype[16]; // "Undoable" / "Volatile" / "Growing"
    uint32_t version;
    uint32_t header; // size of header

    union {
	struct {
	    uint32_t catalog; // num of entries
	    uint32_t bitmap; // bitmap size
	    uint32_t extent; // extent size
	    uint64_t disk; // disk size
	    char padding[HEADER_SIZE - 64 - 8 - 20];
	} redolog;
	char padding[HEADER_SIZE - 64 - 8];
    } extra;
};

// always little-endian
struct bochs_header {
    char magic[32]; // "Bochs Virtual HD Image"
    char type[16]; // "Redolog"
    char subtype[16]; // "Undoable" / "Volatile" / "Growing"
    uint32_t version;
    uint32_t header; // size of header

    union {
	struct {
	    uint32_t catalog; // num of entries
	    uint32_t bitmap; // bitmap size
	    uint32_t extent; // extent size
	    uint32_t reserved; // for ???
	    uint64_t disk; // disk size
	    char padding[HEADER_SIZE - 64 - 8 - 24];
	} redolog;
	char padding[HEADER_SIZE - 64 - 8];
    } extra;
};

typedef struct BDRVBochsState {
    CoMutex lock;
    uint32_t *catalog_bitmap;
    int catalog_size;

    int data_offset;

    int bitmap_blocks;
    int extent_blocks;
    int extent_size;
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

static int bochs_open(BlockDriverState *bs, int flags)
{
    BDRVBochsState *s = bs->opaque;
    int i;
    struct bochs_header bochs;
    struct bochs_header_v1 header_v1;

    bs->read_only = 1; // no write support yet

    if (bdrv_pread(bs->file, 0, &bochs, sizeof(bochs)) != sizeof(bochs)) {
        goto fail;
    }

    if (strcmp(bochs.magic, HEADER_MAGIC) ||
        strcmp(bochs.type, REDOLOG_TYPE) ||
        strcmp(bochs.subtype, GROWING_TYPE) ||
	((le32_to_cpu(bochs.version) != HEADER_VERSION) &&
	(le32_to_cpu(bochs.version) != HEADER_V1))) {
        goto fail;
    }

    if (le32_to_cpu(bochs.version) == HEADER_V1) {
      memcpy(&header_v1, &bochs, sizeof(bochs));
      bs->total_sectors = le64_to_cpu(header_v1.extra.redolog.disk) / 512;
    } else {
      bs->total_sectors = le64_to_cpu(bochs.extra.redolog.disk) / 512;
    }

    s->catalog_size = le32_to_cpu(bochs.extra.redolog.catalog);
    s->catalog_bitmap = g_malloc(s->catalog_size * 4);
    if (bdrv_pread(bs->file, le32_to_cpu(bochs.header), s->catalog_bitmap,
                   s->catalog_size * 4) != s->catalog_size * 4)
	goto fail;
    for (i = 0; i < s->catalog_size; i++)
	le32_to_cpus(&s->catalog_bitmap[i]);

    s->data_offset = le32_to_cpu(bochs.header) + (s->catalog_size * 4);

    s->bitmap_blocks = 1 + (le32_to_cpu(bochs.extra.redolog.bitmap) - 1) / 512;
    s->extent_blocks = 1 + (le32_to_cpu(bochs.extra.redolog.extent) - 1) / 512;

    s->extent_size = le32_to_cpu(bochs.extra.redolog.extent);

    qemu_co_mutex_init(&s->lock);
    return 0;
 fail:
    return -1;
}

static int64_t seek_to_sector(BlockDriverState *bs, int64_t sector_num)
{
    BDRVBochsState *s = bs->opaque;
    int64_t offset = sector_num * 512;
    int64_t extent_index, extent_offset, bitmap_offset;
    char bitmap_entry;

    // seek to sector
    extent_index = offset / s->extent_size;
    extent_offset = (offset % s->extent_size) / 512;

    if (s->catalog_bitmap[extent_index] == 0xffffffff) {
	return -1; /* not allocated */
    }

    bitmap_offset = s->data_offset + (512 * s->catalog_bitmap[extent_index] *
	(s->extent_blocks + s->bitmap_blocks));

    /* read in bitmap for current extent */
    if (bdrv_pread(bs->file, bitmap_offset + (extent_offset / 8),
                   &bitmap_entry, 1) != 1) {
        return -1;
    }

    if (!((bitmap_entry >> (extent_offset % 8)) & 1)) {
	return -1; /* not allocated */
    }

    return bitmap_offset + (512 * (s->bitmap_blocks + extent_offset));
}

static int bochs_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    int ret;

    while (nb_sectors > 0) {
        int64_t block_offset = seek_to_sector(bs, sector_num);
        if (block_offset >= 0) {
            ret = bdrv_pread(bs->file, block_offset, buf, 512);
            if (ret != 512) {
                return -1;
            }
        } else
            memset(buf, 0, 512);
        nb_sectors--;
        sector_num++;
        buf += 512;
    }
    return 0;
}

static coroutine_fn int bochs_co_read(BlockDriverState *bs, int64_t sector_num,
                                      uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVBochsState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = bochs_read(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
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
    .bdrv_read          = bochs_co_read,
    .bdrv_close		= bochs_close,
};

static void bdrv_bochs_init(void)
{
    bdrv_register(&bdrv_bochs);
}

block_init(bdrv_bochs_init);
