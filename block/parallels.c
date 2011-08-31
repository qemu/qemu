/*
 * Block driver for Parallels disk image format
 *
 * Copyright (c) 2007 Alex Beregszaszi
 *
 * This code is based on comparing different disk images created by Parallels.
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

#define HEADER_MAGIC "WithoutFreeSpace"
#define HEADER_VERSION 2
#define HEADER_SIZE 64

// always little-endian
struct parallels_header {
    char magic[16]; // "WithoutFreeSpace"
    uint32_t version;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t catalog_entries;
    uint32_t nb_sectors;
    char padding[24];
} QEMU_PACKED;

typedef struct BDRVParallelsState {

    uint32_t *catalog_bitmap;
    int catalog_size;

    int tracks;
} BDRVParallelsState;

static int parallels_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const struct parallels_header *ph = (const void *)buf;

    if (buf_size < HEADER_SIZE)
	return 0;

    if (!memcmp(ph->magic, HEADER_MAGIC, 16) &&
	(le32_to_cpu(ph->version) == HEADER_VERSION))
	return 100;

    return 0;
}

static int parallels_open(BlockDriverState *bs, int flags)
{
    BDRVParallelsState *s = bs->opaque;
    int i;
    struct parallels_header ph;

    bs->read_only = 1; // no write support yet

    if (bdrv_pread(bs->file, 0, &ph, sizeof(ph)) != sizeof(ph))
        goto fail;

    if (memcmp(ph.magic, HEADER_MAGIC, 16) ||
	(le32_to_cpu(ph.version) != HEADER_VERSION)) {
        goto fail;
    }

    bs->total_sectors = le32_to_cpu(ph.nb_sectors);

    s->tracks = le32_to_cpu(ph.tracks);

    s->catalog_size = le32_to_cpu(ph.catalog_entries);
    s->catalog_bitmap = g_malloc(s->catalog_size * 4);
    if (bdrv_pread(bs->file, 64, s->catalog_bitmap, s->catalog_size * 4) !=
	s->catalog_size * 4)
	goto fail;
    for (i = 0; i < s->catalog_size; i++)
	le32_to_cpus(&s->catalog_bitmap[i]);

    return 0;
fail:
    if (s->catalog_bitmap)
	g_free(s->catalog_bitmap);
    return -1;
}

static int64_t seek_to_sector(BlockDriverState *bs, int64_t sector_num)
{
    BDRVParallelsState *s = bs->opaque;
    uint32_t index, offset;

    index = sector_num / s->tracks;
    offset = sector_num % s->tracks;

    /* not allocated */
    if ((index > s->catalog_size) || (s->catalog_bitmap[index] == 0))
	return -1;
    return (uint64_t)(s->catalog_bitmap[index] + offset) * 512;
}

static int parallels_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    while (nb_sectors > 0) {
        int64_t position = seek_to_sector(bs, sector_num);
        if (position >= 0) {
            if (bdrv_pread(bs->file, position, buf, 512) != 512)
                return -1;
        } else {
            memset(buf, 0, 512);
        }
        nb_sectors--;
        sector_num++;
        buf += 512;
    }
    return 0;
}

static void parallels_close(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    g_free(s->catalog_bitmap);
}

static BlockDriver bdrv_parallels = {
    .format_name	= "parallels",
    .instance_size	= sizeof(BDRVParallelsState),
    .bdrv_probe		= parallels_probe,
    .bdrv_open		= parallels_open,
    .bdrv_read		= parallels_read,
    .bdrv_close		= parallels_close,
};

static void bdrv_parallels_init(void)
{
    bdrv_register(&bdrv_parallels);
}

block_init(bdrv_parallels_init);
