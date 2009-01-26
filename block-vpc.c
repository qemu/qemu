/*
 * Block driver for Conectix/Microsoft Virtual PC images
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

#define HEADER_SIZE 512

//#define CACHE

enum vhd_type {
    VHD_FIXED           = 2,
    VHD_DYNAMIC         = 3,
    VHD_DIFFERENCING    = 4,
};

// always big-endian
struct vhd_footer {
    char        creator[8]; // "conectix"
    uint32_t    features;
    uint32_t    version;

    // Offset of next header structure, 0xFFFFFFFF if none
    uint64_t    data_offset;

    // Seconds since Jan 1, 2000 0:00:00 (UTC)
    uint32_t    timestamp;

    char        creator_app[4]; // "vpc "
    uint16_t    major;
    uint16_t    minor;
    char        creator_os[4]; // "Wi2k"

    uint64_t    orig_size;
    uint64_t    size;

    uint16_t    cyls;
    uint8_t     heads;
    uint8_t     secs_per_cyl;

    uint32_t    type;

    // Checksum of the Hard Disk Footer ("one's complement of the sum of all
    // the bytes in the footer without the checksum field")
    uint32_t    checksum;

    // UUID used to identify a parent hard disk (backing file)
    uint8_t     uuid[16];

    uint8_t     in_saved_state;
};

struct vhd_dyndisk_header {
    char        magic[8]; // "cxsparse"

    // Offset of next header structure, 0xFFFFFFFF if none
    uint64_t    data_offset;

    // Offset of the Block Allocation Table (BAT)
    uint64_t    table_offset;

    uint32_t    version;
    uint32_t    max_table_entries; // 32bit/entry

    // 2 MB by default, must be a power of two
    uint32_t    block_size;

    uint32_t    checksum;
    uint8_t     parent_uuid[16];
    uint32_t    parent_timestamp;
    uint32_t    reserved;

    // Backing file name (in UTF-16)
    uint8_t     parent_name[512];

    struct {
        uint32_t    platform;
        uint32_t    data_space;
        uint32_t    data_length;
        uint32_t    reserved;
        uint64_t    data_offset;
    } parent_locator[8];
};

typedef struct BDRVVPCState {
    BlockDriverState *hd;

    int max_table_entries;
    uint32_t *pagetable;

    uint32_t block_size;
#ifdef CACHE
    uint8_t *pageentry_u8;
    uint32_t *pageentry_u32;
    uint16_t *pageentry_u16;

    uint64_t last_bitmap;
#endif
} BDRVVPCState;

static int vpc_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    if (buf_size >= 8 && !strncmp((char *)buf, "conectix", 8))
	return 100;
    return 0;
}

static int vpc_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVVPCState *s = bs->opaque;
    int ret, i;
    struct vhd_footer* footer;
    struct vhd_dyndisk_header* dyndisk_header;
    uint8_t buf[HEADER_SIZE];

    bs->read_only = 1; // no write support yet

    ret = bdrv_file_open(&s->hd, filename, flags);
    if (ret < 0)
        return ret;

    if (bdrv_pread(s->hd, 0, buf, HEADER_SIZE) != HEADER_SIZE)
        goto fail;

    footer = (struct vhd_footer*) buf;
    if (strncmp(footer->creator, "conectix", 8))
        goto fail;

    // The visible size of a image in Virtual PC depends on the geometry
    // rather than on the size stored in the footer (the size in the footer
    // is too large usually)
    bs->total_sectors = (int64_t)
        be16_to_cpu(footer->cyls) * footer->heads * footer->secs_per_cyl;

    if (bdrv_pread(s->hd, be64_to_cpu(footer->data_offset), buf, HEADER_SIZE)
            != HEADER_SIZE)
        goto fail;

    footer = NULL;
    dyndisk_header = (struct vhd_dyndisk_header*) buf;

    if (strncmp(dyndisk_header->magic, "cxsparse", 8))
        goto fail;


    s->max_table_entries = be32_to_cpu(dyndisk_header->max_table_entries);
    s->pagetable = qemu_malloc(s->max_table_entries * 4);
    if (!s->pagetable)
        goto fail;

    if (bdrv_pread(s->hd, be64_to_cpu(dyndisk_header->table_offset),
            s->pagetable, s->max_table_entries * 4) != s->max_table_entries * 4)
	    goto fail;

    for (i = 0; i < s->max_table_entries; i++)
	be32_to_cpus(&s->pagetable[i]);

    s->block_size = be32_to_cpu(dyndisk_header->block_size);
#ifdef CACHE
    s->pageentry_u8 = qemu_malloc(512);
    if (!s->pageentry_u8)
	goto fail;
    s->pageentry_u32 = s->pageentry_u8;
    s->pageentry_u16 = s->pageentry_u8;
    s->last_pagetable = -1;
#endif

    return 0;
 fail:
    bdrv_delete(s->hd);
    return -1;
}

/*
 * Returns the absolute byte offset of the given sector in the image file.
 * If the sector is not allocated, -1 is returned instead.
 */
static inline int64_t get_sector_offset(BlockDriverState *bs, int64_t sector_num)
{
    BDRVVPCState *s = bs->opaque;
    uint64_t offset = sector_num * 512;
    uint64_t bitmap_offset, block_offset;
    uint32_t pagetable_index, pageentry_index;

    pagetable_index = offset / s->block_size;
    pageentry_index = (offset % s->block_size) / 512;

    if (pagetable_index > s->max_table_entries || s->pagetable[pagetable_index] == 0xffffffff)
	return -1; // not allocated

    bitmap_offset = 512 * s->pagetable[pagetable_index];
    block_offset = bitmap_offset + 512 + (512 * pageentry_index);

//    printf("sector: %" PRIx64 ", index: %x, offset: %x, bioff: %" PRIx64 ", bloff: %" PRIx64 "\n",
//	sector_num, pagetable_index, pageentry_index,
//	bitmap_offset, block_offset);

// disabled by reason
#if 0
#ifdef CACHE
    if (bitmap_offset != s->last_bitmap)
    {
	lseek(s->fd, bitmap_offset, SEEK_SET);

	s->last_bitmap = bitmap_offset;

	// Scary! Bitmap is stored as big endian 32bit entries,
	// while we used to look it up byte by byte
	read(s->fd, s->pageentry_u8, 512);
	for (i = 0; i < 128; i++)
	    be32_to_cpus(&s->pageentry_u32[i]);
    }

    if ((s->pageentry_u8[pageentry_index / 8] >> (pageentry_index % 8)) & 1)
	return -1;
#else
    lseek(s->fd, bitmap_offset + (pageentry_index / 8), SEEK_SET);

    read(s->fd, &bitmap_entry, 1);

    if ((bitmap_entry >> (pageentry_index % 8)) & 1)
	return -1; // not allocated
#endif
#endif

    return block_offset;
}

static int vpc_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVPCState *s = bs->opaque;
    int ret;
    int64_t offset;

    while (nb_sectors > 0) {
        offset = get_sector_offset(bs, sector_num);

        if (offset == -1) {
            memset(buf, 0, 512);
        } else {
            ret = bdrv_pread(s->hd, offset, buf, 512);
            if (ret != 512)
                return -1;
        }

        nb_sectors--;
        sector_num++;
        buf += 512;
    }
    return 0;
}

static void vpc_close(BlockDriverState *bs)
{
    BDRVVPCState *s = bs->opaque;
    qemu_free(s->pagetable);
#ifdef CACHE
    qemu_free(s->pageentry_u8);
#endif
    bdrv_delete(s->hd);
}

BlockDriver bdrv_vpc = {
    "vpc",
    sizeof(BDRVVPCState),
    vpc_probe,
    vpc_open,
    vpc_read,
    NULL,
    vpc_close,
};
