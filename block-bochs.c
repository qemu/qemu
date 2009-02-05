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
    int fd;

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

static int bochs_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVBochsState *s = bs->opaque;
    int fd, i;
    struct bochs_header bochs;
    struct bochs_header_v1 header_v1;

    fd = open(filename, O_RDWR | O_BINARY);
    if (fd < 0) {
        fd = open(filename, O_RDONLY | O_BINARY);
        if (fd < 0)
            return -1;
    }

    bs->read_only = 1; // no write support yet

    s->fd = fd;

    if (read(fd, &bochs, sizeof(bochs)) != sizeof(bochs)) {
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

    lseek(s->fd, le32_to_cpu(bochs.header), SEEK_SET);

    s->catalog_size = le32_to_cpu(bochs.extra.redolog.catalog);
    s->catalog_bitmap = qemu_malloc(s->catalog_size * 4);
    if (read(s->fd, s->catalog_bitmap, s->catalog_size * 4) !=
	s->catalog_size * 4)
	goto fail;
    for (i = 0; i < s->catalog_size; i++)
	le32_to_cpus(&s->catalog_bitmap[i]);

    s->data_offset = le32_to_cpu(bochs.header) + (s->catalog_size * 4);

    s->bitmap_blocks = 1 + (le32_to_cpu(bochs.extra.redolog.bitmap) - 1) / 512;
    s->extent_blocks = 1 + (le32_to_cpu(bochs.extra.redolog.extent) - 1) / 512;

    s->extent_size = le32_to_cpu(bochs.extra.redolog.extent);

    return 0;
 fail:
    close(fd);
    return -1;
}

static inline int seek_to_sector(BlockDriverState *bs, int64_t sector_num)
{
    BDRVBochsState *s = bs->opaque;
    int64_t offset = sector_num * 512;
    int64_t extent_index, extent_offset, bitmap_offset, block_offset;
    char bitmap_entry;

    // seek to sector
    extent_index = offset / s->extent_size;
    extent_offset = (offset % s->extent_size) / 512;

    if (s->catalog_bitmap[extent_index] == 0xffffffff)
    {
//	fprintf(stderr, "page not allocated [%x - %x:%x]\n",
//	    sector_num, extent_index, extent_offset);
	return -1; // not allocated
    }

    bitmap_offset = s->data_offset + (512 * s->catalog_bitmap[extent_index] *
	(s->extent_blocks + s->bitmap_blocks));
    block_offset = bitmap_offset + (512 * (s->bitmap_blocks + extent_offset));

//    fprintf(stderr, "sect: %x [ext i: %x o: %x] -> %x bitmap: %x block: %x\n",
//	sector_num, extent_index, extent_offset,
//	le32_to_cpu(s->catalog_bitmap[extent_index]),
//	bitmap_offset, block_offset);

    // read in bitmap for current extent
    lseek(s->fd, bitmap_offset + (extent_offset / 8), SEEK_SET);

    read(s->fd, &bitmap_entry, 1);

    if (!((bitmap_entry >> (extent_offset % 8)) & 1))
    {
//	fprintf(stderr, "sector (%x) in bitmap not allocated\n",
//	    sector_num);
	return -1; // not allocated
    }

    lseek(s->fd, block_offset, SEEK_SET);

    return 0;
}

static int bochs_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVBochsState *s = bs->opaque;
    int ret;

    while (nb_sectors > 0) {
	if (!seek_to_sector(bs, sector_num))
	{
	    ret = read(s->fd, buf, 512);
	    if (ret != 512)
		return -1;
	}
	else
            memset(buf, 0, 512);
        nb_sectors--;
        sector_num++;
        buf += 512;
    }
    return 0;
}

static void bochs_close(BlockDriverState *bs)
{
    BDRVBochsState *s = bs->opaque;
    qemu_free(s->catalog_bitmap);
    close(s->fd);
}

BlockDriver bdrv_bochs = {
    "bochs",
    sizeof(BDRVBochsState),
    bochs_probe,
    bochs_open,
    bochs_read,
    NULL,
    bochs_close,
};
