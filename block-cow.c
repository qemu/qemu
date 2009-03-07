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
#ifndef _WIN32
#include "qemu-common.h"
#include "block_int.h"
#include <sys/mman.h>

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
    int fd;
    uint8_t *cow_bitmap; /* if non NULL, COW mappings are used first */
    uint8_t *cow_bitmap_addr; /* mmap address of cow_bitmap */
    int cow_bitmap_size;
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

static int cow_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVCowState *s = bs->opaque;
    int fd;
    struct cow_header_v2 cow_header;
    int64_t size;

    fd = open(filename, O_RDWR | O_BINARY | O_LARGEFILE);
    if (fd < 0) {
        fd = open(filename, O_RDONLY | O_BINARY | O_LARGEFILE);
        if (fd < 0)
            return -1;
    }
    s->fd = fd;
    /* see if it is a cow image */
    if (read(fd, &cow_header, sizeof(cow_header)) != sizeof(cow_header)) {
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

    /* mmap the bitmap */
    s->cow_bitmap_size = ((bs->total_sectors + 7) >> 3) + sizeof(cow_header);
    s->cow_bitmap_addr = mmap(get_mmap_addr(s->cow_bitmap_size),
                              s->cow_bitmap_size,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, s->fd, 0);
    if (s->cow_bitmap_addr == MAP_FAILED)
        goto fail;
    s->cow_bitmap = s->cow_bitmap_addr + sizeof(cow_header);
    s->cow_sectors_offset = (s->cow_bitmap_size + 511) & ~511;
    return 0;
 fail:
    close(fd);
    return -1;
}

static inline void cow_set_bit(uint8_t *bitmap, int64_t bitnum)
{
    bitmap[bitnum / 8] |= (1 << (bitnum%8));
}

static inline int is_bit_set(const uint8_t *bitmap, int64_t bitnum)
{
    return !!(bitmap[bitnum / 8] & (1 << (bitnum%8)));
}


/* Return true if first block has been changed (ie. current version is
 * in COW file).  Set the number of continuous blocks for which that
 * is true. */
static inline int is_changed(uint8_t *bitmap,
                             int64_t sector_num, int nb_sectors,
                             int *num_same)
{
    int changed;

    if (!bitmap || nb_sectors == 0) {
	*num_same = nb_sectors;
	return 0;
    }

    changed = is_bit_set(bitmap, sector_num);
    for (*num_same = 1; *num_same < nb_sectors; (*num_same)++) {
	if (is_bit_set(bitmap, sector_num + *num_same) != changed)
	    break;
    }

    return changed;
}

static int cow_is_allocated(BlockDriverState *bs, int64_t sector_num,
                            int nb_sectors, int *pnum)
{
    BDRVCowState *s = bs->opaque;
    return is_changed(s->cow_bitmap, sector_num, nb_sectors, pnum);
}

static int cow_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVCowState *s = bs->opaque;
    int ret, n;

    while (nb_sectors > 0) {
        if (is_changed(s->cow_bitmap, sector_num, nb_sectors, &n)) {
            lseek(s->fd, s->cow_sectors_offset + sector_num * 512, SEEK_SET);
            ret = read(s->fd, buf, n * 512);
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
    int ret, i;

    lseek(s->fd, s->cow_sectors_offset + sector_num * 512, SEEK_SET);
    ret = write(s->fd, buf, nb_sectors * 512);
    if (ret != nb_sectors * 512)
        return -1;
    for (i = 0; i < nb_sectors; i++)
        cow_set_bit(s->cow_bitmap, sector_num + i);
    return 0;
}

static void cow_close(BlockDriverState *bs)
{
    BDRVCowState *s = bs->opaque;
    munmap(s->cow_bitmap_addr, s->cow_bitmap_size);
    close(s->fd);
}

static int cow_create(const char *filename, int64_t image_sectors,
                      const char *image_filename, int flags)
{
    int fd, cow_fd;
    struct cow_header_v2 cow_header;
    struct stat st;

    if (flags)
        return -ENOTSUP;

    cow_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
              0644);
    if (cow_fd < 0)
        return -1;
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
    write(cow_fd, &cow_header, sizeof(cow_header));
    /* resize to include at least all the bitmap */
    ftruncate(cow_fd, sizeof(cow_header) + ((image_sectors + 7) >> 3));
    close(cow_fd);
    return 0;
}

static void cow_flush(BlockDriverState *bs)
{
    BDRVCowState *s = bs->opaque;
    fsync(s->fd);
}

BlockDriver bdrv_cow = {
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
};
#endif
