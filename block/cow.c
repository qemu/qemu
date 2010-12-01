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
#include "block_int.h"
#include "module.h"

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

static int cow_open(BlockDriverState *bs, int flags)
{
    BDRVCowState *s = bs->opaque;
    struct cow_header_v2 cow_header;
    int bitmap_size;
    int64_t size;

    /* see if it is a cow image */
    if (bdrv_pread(bs->file, 0, &cow_header, sizeof(cow_header)) !=
            sizeof(cow_header)) {
        goto fail;
    }

    if (be32_to_cpu(cow_header.magic) != COW_MAGIC ||
        be32_to_cpu(cow_header.version) != COW_VERSION) {
        goto fail;
    }

    /* cow image found */
    size = be64_to_cpu(cow_header.size);
    bs->total_sectors = size / 512;

    pstrcpy(bs->backing_file, sizeof(bs->backing_file),
            cow_header.backing_file);

    bitmap_size = ((bs->total_sectors + 7) >> 3) + sizeof(cow_header);
    s->cow_sectors_offset = (bitmap_size + 511) & ~511;
    return 0;
 fail:
    return -1;
}

/*
 * XXX(hch): right now these functions are extremly ineffcient.
 * We should just read the whole bitmap we'll need in one go instead.
 */
static inline int cow_set_bit(BlockDriverState *bs, int64_t bitnum)
{
    uint64_t offset = sizeof(struct cow_header_v2) + bitnum / 8;
    uint8_t bitmap;
    int ret;

    ret = bdrv_pread(bs->file, offset, &bitmap, sizeof(bitmap));
    if (ret < 0) {
       return ret;
    }

    bitmap |= (1 << (bitnum % 8));

    ret = bdrv_pwrite_sync(bs->file, offset, &bitmap, sizeof(bitmap));
    if (ret < 0) {
       return ret;
    }
    return 0;
}

static inline int is_bit_set(BlockDriverState *bs, int64_t bitnum)
{
    uint64_t offset = sizeof(struct cow_header_v2) + bitnum / 8;
    uint8_t bitmap;
    int ret;

    ret = bdrv_pread(bs->file, offset, &bitmap, sizeof(bitmap));
    if (ret < 0) {
       return ret;
    }

    return !!(bitmap & (1 << (bitnum % 8)));
}

/* Return true if first block has been changed (ie. current version is
 * in COW file).  Set the number of continuous blocks for which that
 * is true. */
static int cow_is_allocated(BlockDriverState *bs, int64_t sector_num,
        int nb_sectors, int *num_same)
{
    int changed;

    if (nb_sectors == 0) {
	*num_same = nb_sectors;
	return 0;
    }

    changed = is_bit_set(bs, sector_num);
    if (changed < 0) {
        return 0; /* XXX: how to return I/O errors? */
    }

    for (*num_same = 1; *num_same < nb_sectors; (*num_same)++) {
	if (is_bit_set(bs, sector_num + *num_same) != changed)
	    break;
    }

    return changed;
}

static int cow_update_bitmap(BlockDriverState *bs, int64_t sector_num,
        int nb_sectors)
{
    int error = 0;
    int i;

    for (i = 0; i < nb_sectors; i++) {
        error = cow_set_bit(bs, sector_num + i);
        if (error) {
            break;
        }
    }

    return error;
}

static int cow_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVCowState *s = bs->opaque;
    int ret, n;

    while (nb_sectors > 0) {
        if (cow_is_allocated(bs, sector_num, nb_sectors, &n)) {
            ret = bdrv_pread(bs->file,
                        s->cow_sectors_offset + sector_num * 512,
                        buf, n * 512);
            if (ret != n * 512)
                return -1;
        } else {
            if (bs->backing_hd) {
                /* read from the base image */
                ret = bdrv_read(bs->backing_hd, sector_num, buf, n);
                if (ret < 0)
                    return -1;
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

static int cow_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BDRVCowState *s = bs->opaque;
    int ret;

    ret = bdrv_pwrite(bs->file, s->cow_sectors_offset + sector_num * 512,
                      buf, nb_sectors * 512);
    if (ret != nb_sectors * 512)
        return -1;

    return cow_update_bitmap(bs, sector_num, nb_sectors);
}

static void cow_close(BlockDriverState *bs)
{
}

static int cow_create(const char *filename, QEMUOptionParameter *options)
{
    int fd, cow_fd;
    struct cow_header_v2 cow_header;
    struct stat st;
    int64_t image_sectors = 0;
    const char *image_filename = NULL;
    int ret;

    /* Read out options */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            image_sectors = options->value.n / 512;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            image_filename = options->value.s;
        }
        options++;
    }

    cow_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
              0644);
    if (cow_fd < 0)
        return -errno;
    memset(&cow_header, 0, sizeof(cow_header));
    cow_header.magic = cpu_to_be32(COW_MAGIC);
    cow_header.version = cpu_to_be32(COW_VERSION);
    if (image_filename) {
        /* Note: if no file, we put a dummy mtime */
        cow_header.mtime = cpu_to_be32(0);

        fd = open(image_filename, O_RDONLY | O_BINARY);
        if (fd < 0) {
            close(cow_fd);
            goto mtime_fail;
        }
        if (fstat(fd, &st) != 0) {
            close(fd);
            goto mtime_fail;
        }
        close(fd);
        cow_header.mtime = cpu_to_be32(st.st_mtime);
    mtime_fail:
        pstrcpy(cow_header.backing_file, sizeof(cow_header.backing_file),
                image_filename);
    }
    cow_header.sectorsize = cpu_to_be32(512);
    cow_header.size = cpu_to_be64(image_sectors * 512);
    ret = qemu_write_full(cow_fd, &cow_header, sizeof(cow_header));
    if (ret != sizeof(cow_header)) {
        ret = -errno;
        goto exit;
    }

    /* resize to include at least all the bitmap */
    ret = ftruncate(cow_fd, sizeof(cow_header) + ((image_sectors + 7) >> 3));
    if (ret) {
        ret = -errno;
        goto exit;
    }

exit:
    close(cow_fd);
    return ret;
}

static int cow_flush(BlockDriverState *bs)
{
    return bdrv_flush(bs->file);
}

static QEMUOptionParameter cow_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    },
    { NULL }
};

static BlockDriver bdrv_cow = {
    .format_name	= "cow",
    .instance_size	= sizeof(BDRVCowState),
    .bdrv_probe		= cow_probe,
    .bdrv_open		= cow_open,
    .bdrv_read		= cow_read,
    .bdrv_write		= cow_write,
    .bdrv_close		= cow_close,
    .bdrv_create	= cow_create,
    .bdrv_flush		= cow_flush,
    .bdrv_is_allocated	= cow_is_allocated,

    .create_options = cow_create_options,
};

static void bdrv_cow_init(void)
{
    bdrv_register(&bdrv_cow);
}

block_init(bdrv_cow_init);
