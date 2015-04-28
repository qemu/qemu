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
#include "block/block_int.h"
#include "qemu/module.h"

/**************************************************************/

#define HEADER_MAGIC "WithoutFreeSpace"
#define HEADER_MAGIC2 "WithouFreSpacExt"
#define HEADER_VERSION 2

// always little-endian
typedef struct ParallelsHeader {
    char magic[16]; // "WithoutFreeSpace"
    uint32_t version;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t catalog_entries;
    uint64_t nb_sectors;
    uint32_t inuse;
    uint32_t data_off;
    char padding[12];
} QEMU_PACKED ParallelsHeader;

typedef struct BDRVParallelsState {
    /** Locking is conservative, the lock protects
     *   - image file extending (truncate, fallocate)
     *   - any access to block allocation table
     */
    CoMutex lock;

    uint32_t *catalog_bitmap;
    unsigned int catalog_size;

    unsigned int tracks;

    unsigned int off_multiplier;
} BDRVParallelsState;

static int parallels_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const ParallelsHeader *ph = (const void *)buf;

    if (buf_size < sizeof(ParallelsHeader))
        return 0;

    if ((!memcmp(ph->magic, HEADER_MAGIC, 16) ||
        !memcmp(ph->magic, HEADER_MAGIC2, 16)) &&
        (le32_to_cpu(ph->version) == HEADER_VERSION))
        return 100;

    return 0;
}

static int parallels_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    int i;
    ParallelsHeader ph;
    int ret;

    bs->read_only = 1; // no write support yet

    ret = bdrv_pread(bs->file, 0, &ph, sizeof(ph));
    if (ret < 0) {
        goto fail;
    }

    bs->total_sectors = le64_to_cpu(ph.nb_sectors);

    if (le32_to_cpu(ph.version) != HEADER_VERSION) {
        goto fail_format;
    }
    if (!memcmp(ph.magic, HEADER_MAGIC, 16)) {
        s->off_multiplier = 1;
        bs->total_sectors = 0xffffffff & bs->total_sectors;
    } else if (!memcmp(ph.magic, HEADER_MAGIC2, 16)) {
        s->off_multiplier = le32_to_cpu(ph.tracks);
    } else {
        goto fail_format;
    }

    s->tracks = le32_to_cpu(ph.tracks);
    if (s->tracks == 0) {
        error_setg(errp, "Invalid image: Zero sectors per track");
        ret = -EINVAL;
        goto fail;
    }
    if (s->tracks > INT32_MAX/513) {
        error_setg(errp, "Invalid image: Too big cluster");
        ret = -EFBIG;
        goto fail;
    }

    s->catalog_size = le32_to_cpu(ph.catalog_entries);
    if (s->catalog_size > INT_MAX / sizeof(uint32_t)) {
        error_setg(errp, "Catalog too large");
        ret = -EFBIG;
        goto fail;
    }
    s->catalog_bitmap = g_try_new(uint32_t, s->catalog_size);
    if (s->catalog_size && s->catalog_bitmap == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, sizeof(ParallelsHeader),
                     s->catalog_bitmap, s->catalog_size * sizeof(uint32_t));
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->catalog_size; i++)
        le32_to_cpus(&s->catalog_bitmap[i]);

    qemu_co_mutex_init(&s->lock);
    return 0;

fail_format:
    error_setg(errp, "Image not in Parallels format");
    ret = -EINVAL;
fail:
    g_free(s->catalog_bitmap);
    return ret;
}

static int64_t seek_to_sector(BDRVParallelsState *s, int64_t sector_num)
{
    uint32_t index, offset;

    index = sector_num / s->tracks;
    offset = sector_num % s->tracks;

    /* not allocated */
    if ((index >= s->catalog_size) || (s->catalog_bitmap[index] == 0))
        return -1;
    return (uint64_t)s->catalog_bitmap[index] * s->off_multiplier + offset;
}

static int cluster_remainder(BDRVParallelsState *s, int64_t sector_num,
        int nb_sectors)
{
    int ret = s->tracks - sector_num % s->tracks;
    return MIN(nb_sectors, ret);
}

static int64_t coroutine_fn parallels_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t offset;

    qemu_co_mutex_lock(&s->lock);
    offset = seek_to_sector(s, sector_num);
    qemu_co_mutex_unlock(&s->lock);

    *pnum = cluster_remainder(s, sector_num, nb_sectors);

    if (offset < 0) {
        return 0;
    }

    return (offset << BDRV_SECTOR_BITS) |
        BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
}

static coroutine_fn int parallels_co_readv(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    BDRVParallelsState *s = bs->opaque;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;
    int ret = 0;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    while (nb_sectors > 0) {
        int64_t position;
        int n, nbytes;

        qemu_co_mutex_lock(&s->lock);
        position = seek_to_sector(s, sector_num);
        qemu_co_mutex_unlock(&s->lock);

        n = cluster_remainder(s, sector_num, nb_sectors);
        nbytes = n << BDRV_SECTOR_BITS;

        if (position < 0) {
            qemu_iovec_memset(qiov, bytes_done, 0, nbytes);
        } else {
            qemu_iovec_reset(&hd_qiov);
            qemu_iovec_concat(&hd_qiov, qiov, bytes_done, nbytes);

            ret = bdrv_co_readv(bs->file, position, n, &hd_qiov);
            if (ret < 0) {
                break;
            }
        }

        nb_sectors -= n;
        sector_num += n;
        bytes_done += nbytes;
    }

    qemu_iovec_destroy(&hd_qiov);
    return ret;
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
    .bdrv_close		= parallels_close,
    .bdrv_co_get_block_status = parallels_co_get_block_status,
    .bdrv_has_zero_init       = bdrv_has_zero_init_1,
    .bdrv_co_readv  = parallels_co_readv,
};

static void bdrv_parallels_init(void)
{
    bdrv_register(&bdrv_parallels);
}

block_init(bdrv_parallels_init);
