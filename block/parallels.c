/*
 * Block driver for Parallels disk image format
 *
 * Copyright (c) 2007 Alex Beregszaszi
 * Copyright (c) 2015 Denis V. Lunev <den@openvz.org>
 *
 * This code was originally based on comparing different disk images created
 * by Parallels. Currently it is based on opened OpenVZ sources
 * available at
 *     http://git.openvz.org/?p=ploop;a=summary
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

#define DEFAULT_CLUSTER_SIZE 1048576        /* 1 MiB */


// always little-endian
typedef struct ParallelsHeader {
    char magic[16]; // "WithoutFreeSpace"
    uint32_t version;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t bat_entries;
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

    uint32_t *bat_bitmap;
    unsigned int bat_size;

    unsigned int tracks;

    unsigned int off_multiplier;

    bool has_truncate;
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

    s->bat_size = le32_to_cpu(ph.bat_entries);
    if (s->bat_size > INT_MAX / sizeof(uint32_t)) {
        error_setg(errp, "Catalog too large");
        ret = -EFBIG;
        goto fail;
    }
    s->bat_bitmap = g_try_new(uint32_t, s->bat_size);
    if (s->bat_size && s->bat_bitmap == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, sizeof(ParallelsHeader),
                     s->bat_bitmap, s->bat_size * sizeof(uint32_t));
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < s->bat_size; i++) {
        le32_to_cpus(&s->bat_bitmap[i]);
    }

    s->has_truncate = bdrv_has_zero_init(bs->file) &&
                      bdrv_truncate(bs->file, bdrv_getlength(bs->file)) == 0;

    qemu_co_mutex_init(&s->lock);
    return 0;

fail_format:
    error_setg(errp, "Image not in Parallels format");
    ret = -EINVAL;
fail:
    g_free(s->bat_bitmap);
    return ret;
}

static int64_t seek_to_sector(BDRVParallelsState *s, int64_t sector_num)
{
    uint32_t index, offset;

    index = sector_num / s->tracks;
    offset = sector_num % s->tracks;

    /* not allocated */
    if ((index >= s->bat_size) || (s->bat_bitmap[index] == 0)) {
        return -1;
    }
    return (uint64_t)s->bat_bitmap[index] * s->off_multiplier + offset;
}

static int cluster_remainder(BDRVParallelsState *s, int64_t sector_num,
        int nb_sectors)
{
    int ret = s->tracks - sector_num % s->tracks;
    return MIN(nb_sectors, ret);
}

static int64_t allocate_cluster(BlockDriverState *bs, int64_t sector_num)
{
    BDRVParallelsState *s = bs->opaque;
    uint32_t idx, offset, tmp;
    int64_t pos;
    int ret;

    idx = sector_num / s->tracks;
    offset = sector_num % s->tracks;

    if (idx >= s->bat_size) {
        return -EINVAL;
    }
    if (s->bat_bitmap[idx] != 0) {
        return (uint64_t)s->bat_bitmap[idx] * s->off_multiplier + offset;
    }

    pos = bdrv_getlength(bs->file) >> BDRV_SECTOR_BITS;
    if (s->has_truncate) {
        ret = bdrv_truncate(bs->file, (pos + s->tracks) << BDRV_SECTOR_BITS);
    } else {
        ret = bdrv_write_zeroes(bs->file, pos, s->tracks, 0);
    }
    if (ret < 0) {
        return ret;
    }

    s->bat_bitmap[idx] = pos / s->off_multiplier;

    tmp = cpu_to_le32(s->bat_bitmap[idx]);

    ret = bdrv_pwrite(bs->file,
            sizeof(ParallelsHeader) + idx * sizeof(tmp), &tmp, sizeof(tmp));
    if (ret < 0) {
        s->bat_bitmap[idx] = 0;
        return ret;
    }
    return (uint64_t)s->bat_bitmap[idx] * s->off_multiplier + offset;
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

static coroutine_fn int parallels_co_writev(BlockDriverState *bs,
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
        position = allocate_cluster(bs, sector_num);
        qemu_co_mutex_unlock(&s->lock);
        if (position < 0) {
            ret = (int)position;
            break;
        }

        n = cluster_remainder(s, sector_num, nb_sectors);
        nbytes = n << BDRV_SECTOR_BITS;

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, nbytes);

        ret = bdrv_co_writev(bs->file, position, n, &hd_qiov);
        if (ret < 0) {
            break;
        }

        nb_sectors -= n;
        sector_num += n;
        bytes_done += nbytes;
    }

    qemu_iovec_destroy(&hd_qiov);
    return ret;
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

static int parallels_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int64_t total_size, cl_size;
    uint8_t tmp[BDRV_SECTOR_SIZE];
    Error *local_err = NULL;
    BlockDriverState *file;
    uint32_t bat_entries, bat_sectors;
    ParallelsHeader header;
    int ret;

    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);
    cl_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_CLUSTER_SIZE,
                          DEFAULT_CLUSTER_SIZE), BDRV_SECTOR_SIZE);

    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }

    file = NULL;
    ret = bdrv_open(&file, filename, NULL, NULL,
                    BDRV_O_RDWR | BDRV_O_PROTOCOL, NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        return ret;
    }
    ret = bdrv_truncate(file, 0);
    if (ret < 0) {
        goto exit;
    }

    bat_entries = DIV_ROUND_UP(total_size, cl_size);
    bat_sectors = DIV_ROUND_UP(bat_entries * sizeof(uint32_t) +
                               sizeof(ParallelsHeader), cl_size);
    bat_sectors = (bat_sectors *  cl_size) >> BDRV_SECTOR_BITS;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, HEADER_MAGIC2, sizeof(header.magic));
    header.version = cpu_to_le32(HEADER_VERSION);
    /* don't care much about geometry, it is not used on image level */
    header.heads = cpu_to_le32(16);
    header.cylinders = cpu_to_le32(total_size / BDRV_SECTOR_SIZE / 16 / 32);
    header.tracks = cpu_to_le32(cl_size >> BDRV_SECTOR_BITS);
    header.bat_entries = cpu_to_le32(bat_entries);
    header.nb_sectors = cpu_to_le64(DIV_ROUND_UP(total_size, BDRV_SECTOR_SIZE));
    header.data_off = cpu_to_le32(bat_sectors);

    /* write all the data */
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, &header, sizeof(header));

    ret = bdrv_pwrite(file, 0, tmp, BDRV_SECTOR_SIZE);
    if (ret < 0) {
        goto exit;
    }
    ret = bdrv_write_zeroes(file, 1, bat_sectors - 1, 0);
    if (ret < 0) {
        goto exit;
    }
    ret = 0;

done:
    bdrv_unref(file);
    return ret;

exit:
    error_setg_errno(errp, -ret, "Failed to create Parallels image");
    goto done;
}

static void parallels_close(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    g_free(s->bat_bitmap);
}

static QemuOptsList parallels_create_opts = {
    .name = "parallels-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(parallels_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size",
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Parallels image cluster size",
            .def_value_str = stringify(DEFAULT_CLUSTER_SIZE),
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_parallels = {
    .format_name	= "parallels",
    .instance_size	= sizeof(BDRVParallelsState),
    .bdrv_probe		= parallels_probe,
    .bdrv_open		= parallels_open,
    .bdrv_close		= parallels_close,
    .bdrv_co_get_block_status = parallels_co_get_block_status,
    .bdrv_has_zero_init       = bdrv_has_zero_init_1,
    .bdrv_co_readv  = parallels_co_readv,
    .bdrv_co_writev = parallels_co_writev,

    .bdrv_create    = parallels_create,
    .create_opts    = &parallels_create_opts,
};

static void bdrv_parallels_init(void)
{
    bdrv_register(&bdrv_parallels);
}

block_init(bdrv_parallels_init);
