/*
 * QEMU Block driver for CLOOP images
 *
 * Copyright (c) 2004 Johannes E. Schindelin
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
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include <zlib.h>

/* Maximum compressed block size */
#define MAX_BLOCK_SIZE (64 * 1024 * 1024)

typedef struct BDRVCloopState {
    CoMutex lock;
    uint32_t block_size;
    uint32_t n_blocks;
    uint64_t *offsets;
    uint32_t sectors_per_block;
    uint32_t current_block;
    uint8_t *compressed_block;
    uint8_t *uncompressed_block;
    z_stream zstream;
} BDRVCloopState;

static int cloop_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const char *magic_version_2_0 = "#!/bin/sh\n"
        "#V2.0 Format\n"
        "modprobe cloop file=$0 && mount -r -t iso9660 /dev/cloop $1\n";
    int length = strlen(magic_version_2_0);
    if (length > buf_size) {
        length = buf_size;
    }
    if (!memcmp(magic_version_2_0, buf, length)) {
        return 2;
    }
    return 0;
}

static int cloop_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVCloopState *s = bs->opaque;
    uint32_t offsets_size, max_compressed_block_size = 1, i;
    int ret;

    ret = bdrv_apply_auto_read_only(bs, NULL, errp);
    if (ret < 0) {
        return ret;
    }

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    /* read header */
    ret = bdrv_pread(bs->file, 128, &s->block_size, 4);
    if (ret < 0) {
        return ret;
    }
    s->block_size = be32_to_cpu(s->block_size);
    if (s->block_size % 512) {
        error_setg(errp, "block_size %" PRIu32 " must be a multiple of 512",
                   s->block_size);
        return -EINVAL;
    }
    if (s->block_size == 0) {
        error_setg(errp, "block_size cannot be zero");
        return -EINVAL;
    }

    /* cloop's create_compressed_fs.c warns about block sizes beyond 256 KB but
     * we can accept more.  Prevent ridiculous values like 4 GB - 1 since we
     * need a buffer this big.
     */
    if (s->block_size > MAX_BLOCK_SIZE) {
        error_setg(errp, "block_size %" PRIu32 " must be %u MB or less",
                   s->block_size,
                   MAX_BLOCK_SIZE / (1024 * 1024));
        return -EINVAL;
    }

    ret = bdrv_pread(bs->file, 128 + 4, &s->n_blocks, 4);
    if (ret < 0) {
        return ret;
    }
    s->n_blocks = be32_to_cpu(s->n_blocks);

    /* read offsets */
    if (s->n_blocks > (UINT32_MAX - 1) / sizeof(uint64_t)) {
        /* Prevent integer overflow */
        error_setg(errp, "n_blocks %" PRIu32 " must be %zu or less",
                   s->n_blocks,
                   (UINT32_MAX - 1) / sizeof(uint64_t));
        return -EINVAL;
    }
    offsets_size = (s->n_blocks + 1) * sizeof(uint64_t);
    if (offsets_size > 512 * 1024 * 1024) {
        /* Prevent ridiculous offsets_size which causes memory allocation to
         * fail or overflows bdrv_pread() size.  In practice the 512 MB
         * offsets[] limit supports 16 TB images at 256 KB block size.
         */
        error_setg(errp, "image requires too many offsets, "
                   "try increasing block size");
        return -EINVAL;
    }

    s->offsets = g_try_malloc(offsets_size);
    if (s->offsets == NULL) {
        error_setg(errp, "Could not allocate offsets table");
        return -ENOMEM;
    }

    ret = bdrv_pread(bs->file, 128 + 4 + 4, s->offsets, offsets_size);
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->n_blocks + 1; i++) {
        uint64_t size;

        s->offsets[i] = be64_to_cpu(s->offsets[i]);
        if (i == 0) {
            continue;
        }

        if (s->offsets[i] < s->offsets[i - 1]) {
            error_setg(errp, "offsets not monotonically increasing at "
                       "index %" PRIu32 ", image file is corrupt", i);
            ret = -EINVAL;
            goto fail;
        }

        size = s->offsets[i] - s->offsets[i - 1];

        /* Compressed blocks should be smaller than the uncompressed block size
         * but maybe compression performed poorly so the compressed block is
         * actually bigger.  Clamp down on unrealistic values to prevent
         * ridiculous s->compressed_block allocation.
         */
        if (size > 2 * MAX_BLOCK_SIZE) {
            error_setg(errp, "invalid compressed block size at index %" PRIu32
                       ", image file is corrupt", i);
            ret = -EINVAL;
            goto fail;
        }

        if (size > max_compressed_block_size) {
            max_compressed_block_size = size;
        }
    }

    /* initialize zlib engine */
    s->compressed_block = g_try_malloc(max_compressed_block_size + 1);
    if (s->compressed_block == NULL) {
        error_setg(errp, "Could not allocate compressed_block");
        ret = -ENOMEM;
        goto fail;
    }

    s->uncompressed_block = g_try_malloc(s->block_size);
    if (s->uncompressed_block == NULL) {
        error_setg(errp, "Could not allocate uncompressed_block");
        ret = -ENOMEM;
        goto fail;
    }

    if (inflateInit(&s->zstream) != Z_OK) {
        ret = -EINVAL;
        goto fail;
    }
    s->current_block = s->n_blocks;

    s->sectors_per_block = s->block_size/512;
    bs->total_sectors = s->n_blocks * s->sectors_per_block;
    qemu_co_mutex_init(&s->lock);
    return 0;

fail:
    g_free(s->offsets);
    g_free(s->compressed_block);
    g_free(s->uncompressed_block);
    return ret;
}

static void cloop_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.request_alignment = BDRV_SECTOR_SIZE; /* No sub-sector I/O */
}

static inline int cloop_read_block(BlockDriverState *bs, int block_num)
{
    BDRVCloopState *s = bs->opaque;

    if (s->current_block != block_num) {
        int ret;
        uint32_t bytes = s->offsets[block_num + 1] - s->offsets[block_num];

        ret = bdrv_pread(bs->file, s->offsets[block_num],
                         s->compressed_block, bytes);
        if (ret != bytes) {
            return -1;
        }

        s->zstream.next_in = s->compressed_block;
        s->zstream.avail_in = bytes;
        s->zstream.next_out = s->uncompressed_block;
        s->zstream.avail_out = s->block_size;
        ret = inflateReset(&s->zstream);
        if (ret != Z_OK) {
            return -1;
        }
        ret = inflate(&s->zstream, Z_FINISH);
        if (ret != Z_STREAM_END || s->zstream.total_out != s->block_size) {
            return -1;
        }

        s->current_block = block_num;
    }
    return 0;
}

static int coroutine_fn
cloop_co_preadv(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
                QEMUIOVector *qiov, int flags)
{
    BDRVCloopState *s = bs->opaque;
    uint64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    int ret, i;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    qemu_co_mutex_lock(&s->lock);

    for (i = 0; i < nb_sectors; i++) {
        void *data;
        uint32_t sector_offset_in_block =
            ((sector_num + i) % s->sectors_per_block),
            block_num = (sector_num + i) / s->sectors_per_block;
        if (cloop_read_block(bs, block_num) != 0) {
            ret = -EIO;
            goto fail;
        }

        data = s->uncompressed_block + sector_offset_in_block * 512;
        qemu_iovec_from_buf(qiov, i * 512, data, 512);
    }

    ret = 0;
fail:
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static void cloop_close(BlockDriverState *bs)
{
    BDRVCloopState *s = bs->opaque;
    g_free(s->offsets);
    g_free(s->compressed_block);
    g_free(s->uncompressed_block);
    inflateEnd(&s->zstream);
}

static BlockDriver bdrv_cloop = {
    .format_name    = "cloop",
    .instance_size  = sizeof(BDRVCloopState),
    .bdrv_probe     = cloop_probe,
    .bdrv_open      = cloop_open,
    .bdrv_child_perm     = bdrv_format_default_perms,
    .bdrv_refresh_limits = cloop_refresh_limits,
    .bdrv_co_preadv = cloop_co_preadv,
    .bdrv_close     = cloop_close,
};

static void bdrv_cloop_init(void)
{
    bdrv_register(&bdrv_cloop);
}

block_init(bdrv_cloop_init);
