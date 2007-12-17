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

// always big-endian
struct vpc_subheader {
    char magic[8]; // "conectix" / "cxsparse"
    union {
	struct {
	    uint32_t unk1[2];
	    uint32_t unk2; // always zero?
	    uint32_t subheader_offset;
	    uint32_t unk3; // some size?
	    char creator[4]; // "vpc "
	    uint16_t major;
	    uint16_t minor;
	    char guest[4]; // "Wi2k"
	    uint32_t unk4[7];
	    uint8_t vnet_id[16]; // virtual network id, purpose unknown
	    // next 16 longs are used, but dunno the purpose
	    // next 6 longs unknown, following 7 long maybe a serial
	    char padding[HEADER_SIZE - 84];
	} main;
	struct {
	    uint32_t unk1[2]; // all bits set
	    uint32_t unk2; // always zero?
	    uint32_t pagetable_offset;
	    uint32_t unk3;
	    uint32_t pagetable_entries; // 32bit/entry
	    uint32_t pageentry_size; // 512*8*512
	    uint32_t nb_sectors;
	    char padding[HEADER_SIZE - 40];
	} sparse;
	char padding[HEADER_SIZE - 8];
    } type;
};

typedef struct BDRVVPCState {
    int fd;

    int pagetable_entries;
    uint32_t *pagetable;

    uint32_t pageentry_size;
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
    int fd, i;
    struct vpc_subheader header;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;

    bs->read_only = 1; // no write support yet

    s->fd = fd;

    if (read(fd, &header, HEADER_SIZE) != HEADER_SIZE)
        goto fail;

    if (strncmp(header.magic, "conectix", 8))
        goto fail;
    lseek(s->fd, be32_to_cpu(header.type.main.subheader_offset), SEEK_SET);

    if (read(fd, &header, HEADER_SIZE) != HEADER_SIZE)
        goto fail;

    if (strncmp(header.magic, "cxsparse", 8))
	goto fail;

    bs->total_sectors = ((uint64_t)be32_to_cpu(header.type.sparse.pagetable_entries) *
			be32_to_cpu(header.type.sparse.pageentry_size)) / 512;

    lseek(s->fd, be32_to_cpu(header.type.sparse.pagetable_offset), SEEK_SET);

    s->pagetable_entries = be32_to_cpu(header.type.sparse.pagetable_entries);
    s->pagetable = qemu_malloc(s->pagetable_entries * 4);
    if (!s->pagetable)
	goto fail;
    if (read(s->fd, s->pagetable, s->pagetable_entries * 4) !=
	s->pagetable_entries * 4)
	goto fail;
    for (i = 0; i < s->pagetable_entries; i++)
	be32_to_cpus(&s->pagetable[i]);

    s->pageentry_size = be32_to_cpu(header.type.sparse.pageentry_size);
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
    close(fd);
    return -1;
}

static inline int seek_to_sector(BlockDriverState *bs, int64_t sector_num)
{
    BDRVVPCState *s = bs->opaque;
    uint64_t offset = sector_num * 512;
    uint64_t bitmap_offset, block_offset;
    uint32_t pagetable_index, pageentry_index;

    pagetable_index = offset / s->pageentry_size;
    pageentry_index = (offset % s->pageentry_size) / 512;

    if (pagetable_index > s->pagetable_entries || s->pagetable[pagetable_index] == 0xffffffff)
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
    lseek(s->fd, block_offset, SEEK_SET);

    return 0;
}

static int vpc_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVPCState *s = bs->opaque;
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

static void vpc_close(BlockDriverState *bs)
{
    BDRVVPCState *s = bs->opaque;
    qemu_free(s->pagetable);
#ifdef CACHE
    qemu_free(s->pageentry_u8);
#endif
    close(s->fd);
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
