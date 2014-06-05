/*
 * Block driver for the COW format
 *
 * Copyright (c) 2004 Fabrice Bellard
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
/* COW block driver using file system holes */

/* user mode linux compatible COW file */
#define COW_MAGIC 0x4f4f4f4d  /* MOOO */
#define COW_VERSION 2

struct cow_header_v2 {
    uint32_t magic;
    uint32_t version;
    char backing_file[1024];
    int32_t mtime;
    uint64_t size;
    uint32_t sectorsize;
};

typedef struct BDRVCowState {
    CoMutex lock;
    int64_t cow_sectors_offset;
} BDRVCowState;

static int cow_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const struct cow_header_v2 *cow_header = (const void *)buf;

    if (buf_size >= sizeof(struct cow_header_v2) &&
        be32_to_cpu(cow_header->magic) == COW_MAGIC &&
        be32_to_cpu(cow_header->version) == COW_VERSION)
        return 100;
    else
        return 0;
}

static int cow_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVCowState *s = bs->opaque;
    struct cow_header_v2 cow_header;
    int bitmap_size;
    int64_t size;
    int ret;

    /* see if it is a cow image */
    ret = bdrv_pread(bs->file, 0, &cow_header, sizeof(cow_header));
    if (ret < 0) {
        goto fail;
    }

    if (be32_to_cpu(cow_header.magic) != COW_MAGIC) {
        error_setg(errp, "Image not in COW format");
        ret = -EINVAL;
        goto fail;
    }

    if (be32_to_cpu(cow_header.version) != COW_VERSION) {
        char version[64];
        snprintf(version, sizeof(version),
               "COW version %" PRIu32, cow_header.version);
        error_set(errp, QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
            bs->device_name, "cow", version);
        ret = -ENOTSUP;
        goto fail;
    }

    /* cow image found */
    size = be64_to_cpu(cow_header.size);
    bs->total_sectors = size / 512;

    pstrcpy(bs->backing_file, sizeof(bs->backing_file),
            cow_header.backing_file);

    bitmap_size = ((bs->total_sectors + 7) >> 3) + sizeof(cow_header);
    s->cow_sectors_offset = (bitmap_size + 511) & ~511;
    qemu_co_mutex_init(&s->lock);
    return 0;
 fail:
    return ret;
}

static inline void cow_set_bits(uint8_t *bitmap, int start, int64_t nb_sectors)
{
    int64_t bitnum = start, last = start + nb_sectors;
    while (bitnum < last) {
        if ((bitnum & 7) == 0 && bitnum + 8 <= last) {
            bitmap[bitnum / 8] = 0xFF;
            bitnum += 8;
            continue;
        }
        bitmap[bitnum/8] |= (1 << (bitnum % 8));
        bitnum++;
    }
}

#define BITS_PER_BITMAP_SECTOR (512 * 8)

/* Cannot use bitmap.c on big-endian machines.  */
static int cow_test_bit(int64_t bitnum, const uint8_t *bitmap)
{
    return (bitmap[bitnum / 8] & (1 << (bitnum & 7))) != 0;
}

static int cow_find_streak(const uint8_t *bitmap, int value, int start, int nb_sectors)
{
    int streak_value = value ? 0xFF : 0;
    int last = MIN(start + nb_sectors, BITS_PER_BITMAP_SECTOR);
    int bitnum = start;
    while (bitnum < last) {
        if ((bitnum & 7) == 0 && bitmap[bitnum / 8] == streak_value) {
            bitnum += 8;
            continue;
        }
        if (cow_test_bit(bitnum, bitmap) == value) {
            bitnum++;
            continue;
        }
        break;
    }
    return MIN(bitnum, last) - start;
}

/* Return true if first block has been changed (ie. current version is
 * in COW file).  Set the number of continuous blocks for which that
 * is true. */
static int coroutine_fn cow_co_is_allocated(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *num_same)
{
    int64_t bitnum = sector_num + sizeof(struct cow_header_v2) * 8;
    uint64_t offset = (bitnum / 8) & -BDRV_SECTOR_SIZE;
    bool first = true;
    int changed = 0, same = 0;

    do {
        int ret;
        uint8_t bitmap[BDRV_SECTOR_SIZE];

        bitnum &= BITS_PER_BITMAP_SECTOR - 1;
        int sector_bits = MIN(nb_sectors, BITS_PER_BITMAP_SECTOR - bitnum);

        ret = bdrv_pread(bs->file, offset, &bitmap, sizeof(bitmap));
        if (ret < 0) {
            return ret;
        }

        if (first) {
            changed = cow_test_bit(bitnum, bitmap);
            first = false;
        }

        same += cow_find_streak(bitmap, changed, bitnum, nb_sectors);

        bitnum += sector_bits;
        nb_sectors -= sector_bits;
        offset += BDRV_SECTOR_SIZE;
    } while (nb_sectors);

    *num_same = same;
    return changed;
}

static int64_t coroutine_fn cow_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *num_same)
{
    BDRVCowState *s = bs->opaque;
    int ret = cow_co_is_allocated(bs, sector_num, nb_sectors, num_same);
    int64_t offset = s->cow_sectors_offset + (sector_num << BDRV_SECTOR_BITS);
    if (ret < 0) {
        return ret;
    }
    return (ret ? BDRV_BLOCK_DATA : 0) | offset | BDRV_BLOCK_OFFSET_VALID;
}

static int cow_update_bitmap(BlockDriverState *bs, int64_t sector_num,
        int nb_sectors)
{
    int64_t bitnum = sector_num + sizeof(struct cow_header_v2) * 8;
    uint64_t offset = (bitnum / 8) & -BDRV_SECTOR_SIZE;
    bool first = true;
    int sector_bits;

    for ( ; nb_sectors;
            bitnum += sector_bits,
            nb_sectors -= sector_bits,
            offset += BDRV_SECTOR_SIZE) {
        int ret, set;
        uint8_t bitmap[BDRV_SECTOR_SIZE];

        bitnum &= BITS_PER_BITMAP_SECTOR - 1;
        sector_bits = MIN(nb_sectors, BITS_PER_BITMAP_SECTOR - bitnum);

        ret = bdrv_pread(bs->file, offset, &bitmap, sizeof(bitmap));
        if (ret < 0) {
            return ret;
        }

        /* Skip over any already set bits */
        set = cow_find_streak(bitmap, 1, bitnum, sector_bits);
        bitnum += set;
        sector_bits -= set;
        nb_sectors -= set;
        if (!sector_bits) {
            continue;
        }

        if (first) {
            ret = bdrv_flush(bs->file);
            if (ret < 0) {
                return ret;
            }
            first = false;
        }

        cow_set_bits(bitmap, bitnum, sector_bits);

        ret = bdrv_pwrite(bs->file, offset, &bitmap, sizeof(bitmap));
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int coroutine_fn cow_read(BlockDriverState *bs, int64_t sector_num,
                                 uint8_t *buf, int nb_sectors)
{
    BDRVCowState *s = bs->opaque;
    int ret, n;

    while (nb_sectors > 0) {
        ret = cow_co_is_allocated(bs, sector_num, nb_sectors, &n);
        if (ret < 0) {
            return ret;
        }
        if (ret) {
            ret = bdrv_pread(bs->file,
                        s->cow_sectors_offset + sector_num * 512,
                        buf, n * 512);
            if (ret < 0) {
                return ret;
            }
        } else {
            if (bs->backing_hd) {
                /* read from the base image */
                ret = bdrv_read(bs->backing_hd, sector_num, buf, n);
                if (ret < 0) {
                    return ret;
                }
            } else {
                memset(buf, 0, n * 512);
            }
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;
    }
    return 0;
}

static coroutine_fn int cow_co_read(BlockDriverState *bs, int64_t sector_num,
                                    uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVCowState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = cow_read(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int cow_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BDRVCowState *s = bs->opaque;
    int ret;

    ret = bdrv_pwrite(bs->file, s->cow_sectors_offset + sector_num * 512,
                      buf, nb_sectors * 512);
    if (ret < 0) {
        return ret;
    }

    return cow_update_bitmap(bs, sector_num, nb_sectors);
}

static coroutine_fn int cow_co_write(BlockDriverState *bs, int64_t sector_num,
                                     const uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVCowState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = cow_write(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static void cow_close(BlockDriverState *bs)
{
}

static int cow_create(const char *filename, QemuOpts *opts, Error **errp)
{
    struct cow_header_v2 cow_header;
    struct stat st;
    int64_t image_sectors = 0;
    char *image_filename = NULL;
    Error *local_err = NULL;
    int ret;
    BlockDriverState *cow_bs;

    /* Read out options */
    image_sectors = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0) / 512;
    image_filename = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);

    ret = bdrv_create_file(filename, NULL, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }

    cow_bs = NULL;
    ret = bdrv_open(&cow_bs, filename, NULL, NULL,
                    BDRV_O_RDWR | BDRV_O_PROTOCOL, NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }

    memset(&cow_header, 0, sizeof(cow_header));
    cow_header.magic = cpu_to_be32(COW_MAGIC);
    cow_header.version = cpu_to_be32(COW_VERSION);
    if (image_filename) {
        /* Note: if no file, we put a dummy mtime */
        cow_header.mtime = cpu_to_be32(0);

        if (stat(image_filename, &st) != 0) {
            goto mtime_fail;
        }
        cow_header.mtime = cpu_to_be32(st.st_mtime);
    mtime_fail:
        pstrcpy(cow_header.backing_file, sizeof(cow_header.backing_file),
                image_filename);
    }
    cow_header.sectorsize = cpu_to_be32(512);
    cow_header.size = cpu_to_be64(image_sectors * 512);
    ret = bdrv_pwrite(cow_bs, 0, &cow_header, sizeof(cow_header));
    if (ret < 0) {
        goto exit;
    }

    /* resize to include at least all the bitmap */
    ret = bdrv_truncate(cow_bs,
        sizeof(cow_header) + ((image_sectors + 7) >> 3));
    if (ret < 0) {
        goto exit;
    }

exit:
    g_free(image_filename);
    bdrv_unref(cow_bs);
    return ret;
}

static QemuOptsList cow_create_opts = {
    .name = "cow-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(cow_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_cow = {
    .format_name    = "cow",
    .instance_size  = sizeof(BDRVCowState),

    .bdrv_probe     = cow_probe,
    .bdrv_open      = cow_open,
    .bdrv_close     = cow_close,
    .bdrv_create2   = cow_create,
    .bdrv_has_zero_init     = bdrv_has_zero_init_1,

    .bdrv_read              = cow_co_read,
    .bdrv_write             = cow_co_write,
    .bdrv_co_get_block_status   = cow_co_get_block_status,

    .create_opts    = &cow_create_opts,
};

static void bdrv_cow_init(void)
{
    bdrv_register(&bdrv_cow);
}

block_init(bdrv_cow_init);
