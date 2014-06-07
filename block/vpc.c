/*
 * Block driver for Connectix / Microsoft Virtual PC images
 *
 * Copyright (c) 2005 Alex Beregszaszi
 * Copyright (c) 2009 Kevin Wolf <kwolf@suse.de>
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
#include "migration/migration.h"
#if defined(CONFIG_UUID)
#include <uuid/uuid.h>
#endif

/**************************************************************/

#define HEADER_SIZE 512

//#define CACHE

enum vhd_type {
    VHD_FIXED           = 2,
    VHD_DYNAMIC         = 3,
    VHD_DIFFERENCING    = 4,
};

// Seconds since Jan 1, 2000 0:00:00 (UTC)
#define VHD_TIMESTAMP_BASE 946684800

#define VHD_MAX_SECTORS       (65535LL * 255 * 255)

// always big-endian
typedef struct vhd_footer {
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
} QEMU_PACKED VHDFooter;

typedef struct vhd_dyndisk_header {
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
} QEMU_PACKED VHDDynDiskHeader;

typedef struct BDRVVPCState {
    CoMutex lock;
    uint8_t footer_buf[HEADER_SIZE];
    uint64_t free_data_block_offset;
    int max_table_entries;
    uint32_t *pagetable;
    uint64_t bat_offset;
    uint64_t last_bitmap_offset;

    uint32_t block_size;
    uint32_t bitmap_size;

#ifdef CACHE
    uint8_t *pageentry_u8;
    uint32_t *pageentry_u32;
    uint16_t *pageentry_u16;

    uint64_t last_bitmap;
#endif

    Error *migration_blocker;
} BDRVVPCState;

static uint32_t vpc_checksum(uint8_t* buf, size_t size)
{
    uint32_t res = 0;
    int i;

    for (i = 0; i < size; i++)
        res += buf[i];

    return ~res;
}


static int vpc_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    if (buf_size >= 8 && !strncmp((char *)buf, "conectix", 8))
	return 100;
    return 0;
}

static int vpc_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVVPCState *s = bs->opaque;
    int i;
    VHDFooter *footer;
    VHDDynDiskHeader *dyndisk_header;
    uint8_t buf[HEADER_SIZE];
    uint32_t checksum;
    uint64_t computed_size;
    int disk_type = VHD_DYNAMIC;
    int ret;

    ret = bdrv_pread(bs->file, 0, s->footer_buf, HEADER_SIZE);
    if (ret < 0) {
        goto fail;
    }

    footer = (VHDFooter *) s->footer_buf;
    if (strncmp(footer->creator, "conectix", 8)) {
        int64_t offset = bdrv_getlength(bs->file);
        if (offset < 0) {
            ret = offset;
            goto fail;
        } else if (offset < HEADER_SIZE) {
            ret = -EINVAL;
            goto fail;
        }

        /* If a fixed disk, the footer is found only at the end of the file */
        ret = bdrv_pread(bs->file, offset-HEADER_SIZE, s->footer_buf,
                         HEADER_SIZE);
        if (ret < 0) {
            goto fail;
        }
        if (strncmp(footer->creator, "conectix", 8)) {
            error_setg(errp, "invalid VPC image");
            ret = -EINVAL;
            goto fail;
        }
        disk_type = VHD_FIXED;
    }

    checksum = be32_to_cpu(footer->checksum);
    footer->checksum = 0;
    if (vpc_checksum(s->footer_buf, HEADER_SIZE) != checksum)
        fprintf(stderr, "block-vpc: The header checksum of '%s' is "
            "incorrect.\n", bs->filename);

    /* Write 'checksum' back to footer, or else will leave it with zero. */
    footer->checksum = be32_to_cpu(checksum);

    // The visible size of a image in Virtual PC depends on the geometry
    // rather than on the size stored in the footer (the size in the footer
    // is too large usually)
    bs->total_sectors = (int64_t)
        be16_to_cpu(footer->cyls) * footer->heads * footer->secs_per_cyl;

    /* images created with disk2vhd report a far higher virtual size
     * than expected with the cyls * heads * sectors_per_cyl formula.
     * use the footer->size instead if the image was created with
     * disk2vhd.
     */
    if (!strncmp(footer->creator_app, "d2v", 4)) {
        bs->total_sectors = be64_to_cpu(footer->size) / BDRV_SECTOR_SIZE;
    }

    /* Allow a maximum disk size of approximately 2 TB */
    if (bs->total_sectors >= VHD_MAX_SECTORS) {
        ret = -EFBIG;
        goto fail;
    }

    if (disk_type == VHD_DYNAMIC) {
        ret = bdrv_pread(bs->file, be64_to_cpu(footer->data_offset), buf,
                         HEADER_SIZE);
        if (ret < 0) {
            goto fail;
        }

        dyndisk_header = (VHDDynDiskHeader *) buf;

        if (strncmp(dyndisk_header->magic, "cxsparse", 8)) {
            ret = -EINVAL;
            goto fail;
        }

        s->block_size = be32_to_cpu(dyndisk_header->block_size);
        if (!is_power_of_2(s->block_size) || s->block_size < BDRV_SECTOR_SIZE) {
            error_setg(errp, "Invalid block size %" PRIu32, s->block_size);
            ret = -EINVAL;
            goto fail;
        }
        s->bitmap_size = ((s->block_size / (8 * 512)) + 511) & ~511;

        s->max_table_entries = be32_to_cpu(dyndisk_header->max_table_entries);

        if ((bs->total_sectors * 512) / s->block_size > 0xffffffffU) {
            ret = -EINVAL;
            goto fail;
        }
        if (s->max_table_entries > (VHD_MAX_SECTORS * 512) / s->block_size) {
            ret = -EINVAL;
            goto fail;
        }

        computed_size = (uint64_t) s->max_table_entries * s->block_size;
        if (computed_size < bs->total_sectors * 512) {
            ret = -EINVAL;
            goto fail;
        }

        s->pagetable = qemu_blockalign(bs, s->max_table_entries * 4);

        s->bat_offset = be64_to_cpu(dyndisk_header->table_offset);

        ret = bdrv_pread(bs->file, s->bat_offset, s->pagetable,
                         s->max_table_entries * 4);
        if (ret < 0) {
            goto fail;
        }

        s->free_data_block_offset =
            (s->bat_offset + (s->max_table_entries * 4) + 511) & ~511;

        for (i = 0; i < s->max_table_entries; i++) {
            be32_to_cpus(&s->pagetable[i]);
            if (s->pagetable[i] != 0xFFFFFFFF) {
                int64_t next = (512 * (int64_t) s->pagetable[i]) +
                    s->bitmap_size + s->block_size;

                if (next > s->free_data_block_offset) {
                    s->free_data_block_offset = next;
                }
            }
        }

        if (s->free_data_block_offset > bdrv_getlength(bs->file)) {
            error_setg(errp, "block-vpc: free_data_block_offset points after "
                             "the end of file. The image has been truncated.");
            ret = -EINVAL;
            goto fail;
        }

        s->last_bitmap_offset = (int64_t) -1;

#ifdef CACHE
        s->pageentry_u8 = g_malloc(512);
        s->pageentry_u32 = s->pageentry_u8;
        s->pageentry_u16 = s->pageentry_u8;
        s->last_pagetable = -1;
#endif
    }

    qemu_co_mutex_init(&s->lock);

    /* Disable migration when VHD images are used */
    error_set(&s->migration_blocker,
              QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
              "vpc", bs->device_name, "live migration");
    migrate_add_blocker(s->migration_blocker);

    return 0;

fail:
    qemu_vfree(s->pagetable);
#ifdef CACHE
    g_free(s->pageentry_u8);
#endif
    return ret;
}

static int vpc_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

/*
 * Returns the absolute byte offset of the given sector in the image file.
 * If the sector is not allocated, -1 is returned instead.
 *
 * The parameter write must be 1 if the offset will be used for a write
 * operation (the block bitmaps is updated then), 0 otherwise.
 */
static inline int64_t get_sector_offset(BlockDriverState *bs,
    int64_t sector_num, int write)
{
    BDRVVPCState *s = bs->opaque;
    uint64_t offset = sector_num * 512;
    uint64_t bitmap_offset, block_offset;
    uint32_t pagetable_index, pageentry_index;

    pagetable_index = offset / s->block_size;
    pageentry_index = (offset % s->block_size) / 512;

    if (pagetable_index >= s->max_table_entries || s->pagetable[pagetable_index] == 0xffffffff)
        return -1; // not allocated

    bitmap_offset = 512 * (uint64_t) s->pagetable[pagetable_index];
    block_offset = bitmap_offset + s->bitmap_size + (512 * pageentry_index);

    // We must ensure that we don't write to any sectors which are marked as
    // unused in the bitmap. We get away with setting all bits in the block
    // bitmap each time we write to a new block. This might cause Virtual PC to
    // miss sparse read optimization, but it's not a problem in terms of
    // correctness.
    if (write && (s->last_bitmap_offset != bitmap_offset)) {
        uint8_t bitmap[s->bitmap_size];

        s->last_bitmap_offset = bitmap_offset;
        memset(bitmap, 0xff, s->bitmap_size);
        bdrv_pwrite_sync(bs->file, bitmap_offset, bitmap, s->bitmap_size);
    }

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

/*
 * Writes the footer to the end of the image file. This is needed when the
 * file grows as it overwrites the old footer
 *
 * Returns 0 on success and < 0 on error
 */
static int rewrite_footer(BlockDriverState* bs)
{
    int ret;
    BDRVVPCState *s = bs->opaque;
    int64_t offset = s->free_data_block_offset;

    ret = bdrv_pwrite_sync(bs->file, offset, s->footer_buf, HEADER_SIZE);
    if (ret < 0)
        return ret;

    return 0;
}

/*
 * Allocates a new block. This involves writing a new footer and updating
 * the Block Allocation Table to use the space at the old end of the image
 * file (overwriting the old footer)
 *
 * Returns the sectors' offset in the image file on success and < 0 on error
 */
static int64_t alloc_block(BlockDriverState* bs, int64_t sector_num)
{
    BDRVVPCState *s = bs->opaque;
    int64_t bat_offset;
    uint32_t index, bat_value;
    int ret;
    uint8_t bitmap[s->bitmap_size];

    // Check if sector_num is valid
    if ((sector_num < 0) || (sector_num > bs->total_sectors))
        return -1;

    // Write entry into in-memory BAT
    index = (sector_num * 512) / s->block_size;
    if (s->pagetable[index] != 0xFFFFFFFF)
        return -1;

    s->pagetable[index] = s->free_data_block_offset / 512;

    // Initialize the block's bitmap
    memset(bitmap, 0xff, s->bitmap_size);
    ret = bdrv_pwrite_sync(bs->file, s->free_data_block_offset, bitmap,
        s->bitmap_size);
    if (ret < 0) {
        return ret;
    }

    // Write new footer (the old one will be overwritten)
    s->free_data_block_offset += s->block_size + s->bitmap_size;
    ret = rewrite_footer(bs);
    if (ret < 0)
        goto fail;

    // Write BAT entry to disk
    bat_offset = s->bat_offset + (4 * index);
    bat_value = be32_to_cpu(s->pagetable[index]);
    ret = bdrv_pwrite_sync(bs->file, bat_offset, &bat_value, 4);
    if (ret < 0)
        goto fail;

    return get_sector_offset(bs, sector_num, 0);

fail:
    s->free_data_block_offset -= (s->block_size + s->bitmap_size);
    return -1;
}

static int vpc_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVVPCState *s = (BDRVVPCState *)bs->opaque;
    VHDFooter *footer = (VHDFooter *) s->footer_buf;

    if (cpu_to_be32(footer->type) != VHD_FIXED) {
        bdi->cluster_size = s->block_size;
    }

    bdi->unallocated_blocks_are_zero = true;
    return 0;
}

static int vpc_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVPCState *s = bs->opaque;
    int ret;
    int64_t offset;
    int64_t sectors, sectors_per_block;
    VHDFooter *footer = (VHDFooter *) s->footer_buf;

    if (cpu_to_be32(footer->type) == VHD_FIXED) {
        return bdrv_read(bs->file, sector_num, buf, nb_sectors);
    }
    while (nb_sectors > 0) {
        offset = get_sector_offset(bs, sector_num, 0);

        sectors_per_block = s->block_size >> BDRV_SECTOR_BITS;
        sectors = sectors_per_block - (sector_num % sectors_per_block);
        if (sectors > nb_sectors) {
            sectors = nb_sectors;
        }

        if (offset == -1) {
            memset(buf, 0, sectors * BDRV_SECTOR_SIZE);
        } else {
            ret = bdrv_pread(bs->file, offset, buf,
                sectors * BDRV_SECTOR_SIZE);
            if (ret != sectors * BDRV_SECTOR_SIZE) {
                return -1;
            }
        }

        nb_sectors -= sectors;
        sector_num += sectors;
        buf += sectors * BDRV_SECTOR_SIZE;
    }
    return 0;
}

static coroutine_fn int vpc_co_read(BlockDriverState *bs, int64_t sector_num,
                                    uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVVPCState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = vpc_read(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int vpc_write(BlockDriverState *bs, int64_t sector_num,
    const uint8_t *buf, int nb_sectors)
{
    BDRVVPCState *s = bs->opaque;
    int64_t offset;
    int64_t sectors, sectors_per_block;
    int ret;
    VHDFooter *footer =  (VHDFooter *) s->footer_buf;

    if (cpu_to_be32(footer->type) == VHD_FIXED) {
        return bdrv_write(bs->file, sector_num, buf, nb_sectors);
    }
    while (nb_sectors > 0) {
        offset = get_sector_offset(bs, sector_num, 1);

        sectors_per_block = s->block_size >> BDRV_SECTOR_BITS;
        sectors = sectors_per_block - (sector_num % sectors_per_block);
        if (sectors > nb_sectors) {
            sectors = nb_sectors;
        }

        if (offset == -1) {
            offset = alloc_block(bs, sector_num);
            if (offset < 0)
                return -1;
        }

        ret = bdrv_pwrite(bs->file, offset, buf, sectors * BDRV_SECTOR_SIZE);
        if (ret != sectors * BDRV_SECTOR_SIZE) {
            return -1;
        }

        nb_sectors -= sectors;
        sector_num += sectors;
        buf += sectors * BDRV_SECTOR_SIZE;
    }

    return 0;
}

static coroutine_fn int vpc_co_write(BlockDriverState *bs, int64_t sector_num,
                                     const uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVVPCState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = vpc_write(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

/*
 * Calculates the number of cylinders, heads and sectors per cylinder
 * based on a given number of sectors. This is the algorithm described
 * in the VHD specification.
 *
 * Note that the geometry doesn't always exactly match total_sectors but
 * may round it down.
 *
 * Returns 0 on success, -EFBIG if the size is larger than ~2 TB. Override
 * the hardware EIDE and ATA-2 limit of 16 heads (max disk size of 127 GB)
 * and instead allow up to 255 heads.
 */
static int calculate_geometry(int64_t total_sectors, uint16_t* cyls,
    uint8_t* heads, uint8_t* secs_per_cyl)
{
    uint32_t cyls_times_heads;

    /* Allow a maximum disk size of approximately 2 TB */
    if (total_sectors > 65535LL * 255 * 255) {
        return -EFBIG;
    }

    if (total_sectors > 65535 * 16 * 63) {
        *secs_per_cyl = 255;
        if (total_sectors > 65535 * 16 * 255) {
            *heads = 255;
        } else {
            *heads = 16;
        }
        cyls_times_heads = total_sectors / *secs_per_cyl;
    } else {
        *secs_per_cyl = 17;
        cyls_times_heads = total_sectors / *secs_per_cyl;
        *heads = (cyls_times_heads + 1023) / 1024;

        if (*heads < 4)
            *heads = 4;

        if (cyls_times_heads >= (*heads * 1024) || *heads > 16) {
            *secs_per_cyl = 31;
            *heads = 16;
            cyls_times_heads = total_sectors / *secs_per_cyl;
        }

        if (cyls_times_heads >= (*heads * 1024)) {
            *secs_per_cyl = 63;
            *heads = 16;
            cyls_times_heads = total_sectors / *secs_per_cyl;
        }
    }

    *cyls = cyls_times_heads / *heads;

    return 0;
}

static int create_dynamic_disk(int fd, uint8_t *buf, int64_t total_sectors)
{
    VHDDynDiskHeader *dyndisk_header =
        (VHDDynDiskHeader *) buf;
    size_t block_size, num_bat_entries;
    int i;
    int ret = -EIO;

    // Write the footer (twice: at the beginning and at the end)
    block_size = 0x200000;
    num_bat_entries = (total_sectors + block_size / 512) / (block_size / 512);

    if (write(fd, buf, HEADER_SIZE) != HEADER_SIZE) {
        goto fail;
    }

    if (lseek(fd, 1536 + ((num_bat_entries * 4 + 511) & ~511), SEEK_SET) < 0) {
        goto fail;
    }
    if (write(fd, buf, HEADER_SIZE) != HEADER_SIZE) {
        goto fail;
    }

    // Write the initial BAT
    if (lseek(fd, 3 * 512, SEEK_SET) < 0) {
        goto fail;
    }

    memset(buf, 0xFF, 512);
    for (i = 0; i < (num_bat_entries * 4 + 511) / 512; i++) {
        if (write(fd, buf, 512) != 512) {
            goto fail;
        }
    }

    // Prepare the Dynamic Disk Header
    memset(buf, 0, 1024);

    memcpy(dyndisk_header->magic, "cxsparse", 8);

    /*
     * Note: The spec is actually wrong here for data_offset, it says
     * 0xFFFFFFFF, but MS tools expect all 64 bits to be set.
     */
    dyndisk_header->data_offset = be64_to_cpu(0xFFFFFFFFFFFFFFFFULL);
    dyndisk_header->table_offset = be64_to_cpu(3 * 512);
    dyndisk_header->version = be32_to_cpu(0x00010000);
    dyndisk_header->block_size = be32_to_cpu(block_size);
    dyndisk_header->max_table_entries = be32_to_cpu(num_bat_entries);

    dyndisk_header->checksum = be32_to_cpu(vpc_checksum(buf, 1024));

    // Write the header
    if (lseek(fd, 512, SEEK_SET) < 0) {
        goto fail;
    }

    if (write(fd, buf, 1024) != 1024) {
        goto fail;
    }
    ret = 0;

 fail:
    return ret;
}

static int create_fixed_disk(int fd, uint8_t *buf, int64_t total_size)
{
    int ret = -EIO;

    /* Add footer to total size */
    total_size += 512;
    if (ftruncate(fd, total_size) != 0) {
        ret = -errno;
        goto fail;
    }
    if (lseek(fd, -512, SEEK_END) < 0) {
        goto fail;
    }
    if (write(fd, buf, HEADER_SIZE) != HEADER_SIZE) {
        goto fail;
    }

    ret = 0;

 fail:
    return ret;
}

static int vpc_create(const char *filename, QEMUOptionParameter *options,
                      Error **errp)
{
    uint8_t buf[1024];
    VHDFooter *footer = (VHDFooter *) buf;
    QEMUOptionParameter *disk_type_param;
    int fd, i;
    uint16_t cyls = 0;
    uint8_t heads = 0;
    uint8_t secs_per_cyl = 0;
    int64_t total_sectors;
    int64_t total_size;
    int disk_type;
    int ret = -EIO;

    /* Read out options */
    total_size = get_option_parameter(options, BLOCK_OPT_SIZE)->value.n;

    disk_type_param = get_option_parameter(options, BLOCK_OPT_SUBFMT);
    if (disk_type_param && disk_type_param->value.s) {
        if (!strcmp(disk_type_param->value.s, "dynamic")) {
            disk_type = VHD_DYNAMIC;
        } else if (!strcmp(disk_type_param->value.s, "fixed")) {
            disk_type = VHD_FIXED;
        } else {
            return -EINVAL;
        }
    } else {
        disk_type = VHD_DYNAMIC;
    }

    /* Create the file */
    fd = qemu_open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        return -EIO;
    }

    /*
     * Calculate matching total_size and geometry. Increase the number of
     * sectors requested until we get enough (or fail). This ensures that
     * qemu-img convert doesn't truncate images, but rather rounds up.
     */
    total_sectors = total_size / BDRV_SECTOR_SIZE;
    for (i = 0; total_sectors > (int64_t)cyls * heads * secs_per_cyl; i++) {
        if (calculate_geometry(total_sectors + i, &cyls, &heads,
                               &secs_per_cyl))
        {
            ret = -EFBIG;
            goto fail;
        }
    }

    total_sectors = (int64_t) cyls * heads * secs_per_cyl;

    /* Prepare the Hard Disk Footer */
    memset(buf, 0, 1024);

    memcpy(footer->creator, "conectix", 8);
    /* TODO Check if "qemu" creator_app is ok for VPC */
    memcpy(footer->creator_app, "qemu", 4);
    memcpy(footer->creator_os, "Wi2k", 4);

    footer->features = be32_to_cpu(0x02);
    footer->version = be32_to_cpu(0x00010000);
    if (disk_type == VHD_DYNAMIC) {
        footer->data_offset = be64_to_cpu(HEADER_SIZE);
    } else {
        footer->data_offset = be64_to_cpu(0xFFFFFFFFFFFFFFFFULL);
    }
    footer->timestamp = be32_to_cpu(time(NULL) - VHD_TIMESTAMP_BASE);

    /* Version of Virtual PC 2007 */
    footer->major = be16_to_cpu(0x0005);
    footer->minor = be16_to_cpu(0x0003);
    if (disk_type == VHD_DYNAMIC) {
        footer->orig_size = be64_to_cpu(total_sectors * 512);
        footer->size = be64_to_cpu(total_sectors * 512);
    } else {
        footer->orig_size = be64_to_cpu(total_size);
        footer->size = be64_to_cpu(total_size);
    }
    footer->cyls = be16_to_cpu(cyls);
    footer->heads = heads;
    footer->secs_per_cyl = secs_per_cyl;

    footer->type = be32_to_cpu(disk_type);

#if defined(CONFIG_UUID)
    uuid_generate(footer->uuid);
#endif

    footer->checksum = be32_to_cpu(vpc_checksum(buf, HEADER_SIZE));

    if (disk_type == VHD_DYNAMIC) {
        ret = create_dynamic_disk(fd, buf, total_sectors);
    } else {
        ret = create_fixed_disk(fd, buf, total_size);
    }

 fail:
    qemu_close(fd);
    return ret;
}

static int vpc_has_zero_init(BlockDriverState *bs)
{
    BDRVVPCState *s = bs->opaque;
    VHDFooter *footer =  (VHDFooter *) s->footer_buf;

    if (cpu_to_be32(footer->type) == VHD_FIXED) {
        return bdrv_has_zero_init(bs->file);
    } else {
        return 1;
    }
}

static void vpc_close(BlockDriverState *bs)
{
    BDRVVPCState *s = bs->opaque;
    qemu_vfree(s->pagetable);
#ifdef CACHE
    g_free(s->pageentry_u8);
#endif

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static QEMUOptionParameter vpc_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    {
        .name = BLOCK_OPT_SUBFMT,
        .type = OPT_STRING,
        .help =
            "Type of virtual hard disk format. Supported formats are "
            "{dynamic (default) | fixed} "
    },
    { NULL }
};

static BlockDriver bdrv_vpc = {
    .format_name    = "vpc",
    .instance_size  = sizeof(BDRVVPCState),

    .bdrv_probe             = vpc_probe,
    .bdrv_open              = vpc_open,
    .bdrv_close             = vpc_close,
    .bdrv_reopen_prepare    = vpc_reopen_prepare,
    .bdrv_create            = vpc_create,

    .bdrv_read              = vpc_co_read,
    .bdrv_write             = vpc_co_write,

    .bdrv_get_info          = vpc_get_info,

    .create_options         = vpc_create_options,
    .bdrv_has_zero_init     = vpc_has_zero_init,
};

static void bdrv_vpc_init(void)
{
    bdrv_register(&bdrv_vpc);
}

block_init(bdrv_vpc_init);
