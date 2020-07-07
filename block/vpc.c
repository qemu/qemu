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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "migration/blocker.h"
#include "qemu/bswap.h"
#include "qemu/uuid.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"

/**************************************************************/

#define HEADER_SIZE 512

//#define CACHE

enum vhd_type {
    VHD_FIXED           = 2,
    VHD_DYNAMIC         = 3,
    VHD_DIFFERENCING    = 4,
};

/* Seconds since Jan 1, 2000 0:00:00 (UTC) */
#define VHD_TIMESTAMP_BASE 946684800

#define VHD_CHS_MAX_C   65535LL
#define VHD_CHS_MAX_H   16
#define VHD_CHS_MAX_S   255

#define VHD_MAX_SECTORS       0xff000000    /* 2040 GiB max image size */
#define VHD_MAX_GEOMETRY      (VHD_CHS_MAX_C * VHD_CHS_MAX_H * VHD_CHS_MAX_S)

#define VPC_OPT_FORCE_SIZE "force_size"

/* always big-endian */
typedef struct vhd_footer {
    char        creator[8]; /* "conectix" */
    uint32_t    features;
    uint32_t    version;

    /* Offset of next header structure, 0xFFFFFFFF if none */
    uint64_t    data_offset;

    /* Seconds since Jan 1, 2000 0:00:00 (UTC) */
    uint32_t    timestamp;

    char        creator_app[4]; /*  e.g., "vpc " */
    uint16_t    major;
    uint16_t    minor;
    char        creator_os[4]; /* "Wi2k" */

    uint64_t    orig_size;
    uint64_t    current_size;

    uint16_t    cyls;
    uint8_t     heads;
    uint8_t     secs_per_cyl;

    uint32_t    type;

    /* Checksum of the Hard Disk Footer ("one's complement of the sum of all
       the bytes in the footer without the checksum field") */
    uint32_t    checksum;

    /* UUID used to identify a parent hard disk (backing file) */
    QemuUUID    uuid;

    uint8_t     in_saved_state;
} QEMU_PACKED VHDFooter;

typedef struct vhd_dyndisk_header {
    char        magic[8]; /* "cxsparse" */

    /* Offset of next header structure, 0xFFFFFFFF if none */
    uint64_t    data_offset;

    /* Offset of the Block Allocation Table (BAT) */
    uint64_t    table_offset;

    uint32_t    version;
    uint32_t    max_table_entries; /* 32bit/entry */

    /* 2 MB by default, must be a power of two */
    uint32_t    block_size;

    uint32_t    checksum;
    uint8_t     parent_uuid[16];
    uint32_t    parent_timestamp;
    uint32_t    reserved;

    /* Backing file name (in UTF-16) */
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
    bool force_use_chs;
    bool force_use_sz;

#ifdef CACHE
    uint8_t *pageentry_u8;
    uint32_t *pageentry_u32;
    uint16_t *pageentry_u16;

    uint64_t last_bitmap;
#endif

    Error *migration_blocker;
} BDRVVPCState;

#define VPC_OPT_SIZE_CALC "force_size_calc"
static QemuOptsList vpc_runtime_opts = {
    .name = "vpc-runtime-opts",
    .head = QTAILQ_HEAD_INITIALIZER(vpc_runtime_opts.head),
    .desc = {
        {
            .name = VPC_OPT_SIZE_CALC,
            .type = QEMU_OPT_STRING,
            .help = "Force disk size calculation to use either CHS geometry, "
                    "or use the disk current_size specified in the VHD footer. "
                    "{chs, current_size}"
        },
        { /* end of list */ }
    }
};

static QemuOptsList vpc_create_opts;

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

static void vpc_parse_options(BlockDriverState *bs, QemuOpts *opts,
                              Error **errp)
{
    BDRVVPCState *s = bs->opaque;
    const char *size_calc;

    size_calc = qemu_opt_get(opts, VPC_OPT_SIZE_CALC);

    if (!size_calc) {
       /* no override, use autodetect only */
    } else if (!strcmp(size_calc, "current_size")) {
        s->force_use_sz = true;
    } else if (!strcmp(size_calc, "chs")) {
        s->force_use_chs = true;
    } else {
        error_setg(errp, "Invalid size calculation mode: '%s'", size_calc);
    }
}

static int vpc_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVVPCState *s = bs->opaque;
    int i;
    VHDFooter *footer;
    VHDDynDiskHeader *dyndisk_header;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    bool use_chs;
    uint8_t buf[HEADER_SIZE];
    uint32_t checksum;
    uint64_t computed_size;
    uint64_t pagetable_size;
    int disk_type = VHD_DYNAMIC;
    int ret;
    int64_t bs_size;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_IMAGE, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    opts = qemu_opts_create(&vpc_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    vpc_parse_options(bs, opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    ret = bdrv_pread(bs->file, 0, s->footer_buf, HEADER_SIZE);
    if (ret < 0) {
        error_setg(errp, "Unable to read VHD header");
        goto fail;
    }

    footer = (VHDFooter *) s->footer_buf;
    if (strncmp(footer->creator, "conectix", 8)) {
        int64_t offset = bdrv_getlength(bs->file->bs);
        if (offset < 0) {
            ret = offset;
            error_setg(errp, "Invalid file size");
            goto fail;
        } else if (offset < HEADER_SIZE) {
            ret = -EINVAL;
            error_setg(errp, "File too small for a VHD header");
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
    if (vpc_checksum(s->footer_buf, HEADER_SIZE) != checksum) {
        error_setg(errp, "Incorrect header checksum");
        ret = -EINVAL;
        goto fail;
    }

    /* Write 'checksum' back to footer, or else will leave it with zero. */
    footer->checksum = cpu_to_be32(checksum);

    /* The visible size of a image in Virtual PC depends on the geometry
       rather than on the size stored in the footer (the size in the footer
       is too large usually) */
    bs->total_sectors = (int64_t)
        be16_to_cpu(footer->cyls) * footer->heads * footer->secs_per_cyl;

    /* Microsoft Virtual PC and Microsoft Hyper-V produce and read
     * VHD image sizes differently.  VPC will rely on CHS geometry,
     * while Hyper-V and disk2vhd use the size specified in the footer.
     *
     * We use a couple of approaches to try and determine the correct method:
     * look at the Creator App field, and look for images that have CHS
     * geometry that is the maximum value.
     *
     * If the CHS geometry is the maximum CHS geometry, then we assume that
     * the size is the footer->current_size to avoid truncation.  Otherwise,
     * we follow the table based on footer->creator_app:
     *
     *  Known creator apps:
     *      'vpc '  :  CHS              Virtual PC (uses disk geometry)
     *      'qemu'  :  CHS              QEMU (uses disk geometry)
     *      'qem2'  :  current_size     QEMU (uses current_size)
     *      'win '  :  current_size     Hyper-V
     *      'd2v '  :  current_size     Disk2vhd
     *      'tap\0' :  current_size     XenServer
     *      'CTXS'  :  current_size     XenConverter
     *
     *  The user can override the table values via drive options, however
     *  even with an override we will still use current_size for images
     *  that have CHS geometry of the maximum size.
     */
    use_chs = (!!strncmp(footer->creator_app, "win ", 4) &&
               !!strncmp(footer->creator_app, "qem2", 4) &&
               !!strncmp(footer->creator_app, "d2v ", 4) &&
               !!strncmp(footer->creator_app, "CTXS", 4) &&
               !!memcmp(footer->creator_app, "tap", 4)) || s->force_use_chs;

    if (!use_chs || bs->total_sectors == VHD_MAX_GEOMETRY || s->force_use_sz) {
        bs->total_sectors = be64_to_cpu(footer->current_size) /
                                        BDRV_SECTOR_SIZE;
    }

    /* Allow a maximum disk size of 2040 GiB */
    if (bs->total_sectors > VHD_MAX_SECTORS) {
        ret = -EFBIG;
        goto fail;
    }

    if (disk_type == VHD_DYNAMIC) {
        ret = bdrv_pread(bs->file, be64_to_cpu(footer->data_offset), buf,
                         HEADER_SIZE);
        if (ret < 0) {
            error_setg(errp, "Error reading dynamic VHD header");
            goto fail;
        }

        dyndisk_header = (VHDDynDiskHeader *) buf;

        if (strncmp(dyndisk_header->magic, "cxsparse", 8)) {
            error_setg(errp, "Invalid header magic");
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
            error_setg(errp, "Too many blocks");
            ret = -EINVAL;
            goto fail;
        }

        computed_size = (uint64_t) s->max_table_entries * s->block_size;
        if (computed_size < bs->total_sectors * 512) {
            error_setg(errp, "Page table too small");
            ret = -EINVAL;
            goto fail;
        }

        if (s->max_table_entries > SIZE_MAX / 4 ||
            s->max_table_entries > (int) INT_MAX / 4) {
            error_setg(errp, "Max Table Entries too large (%" PRId32 ")",
                        s->max_table_entries);
            ret = -EINVAL;
            goto fail;
        }

        pagetable_size = (uint64_t) s->max_table_entries * 4;

        s->pagetable = qemu_try_blockalign(bs->file->bs, pagetable_size);
        if (s->pagetable == NULL) {
            error_setg(errp, "Unable to allocate memory for page table");
            ret = -ENOMEM;
            goto fail;
        }

        s->bat_offset = be64_to_cpu(dyndisk_header->table_offset);

        ret = bdrv_pread(bs->file, s->bat_offset, s->pagetable,
                         pagetable_size);
        if (ret < 0) {
            error_setg(errp, "Error reading pagetable");
            goto fail;
        }

        s->free_data_block_offset =
            ROUND_UP(s->bat_offset + pagetable_size, 512);

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

        bs_size = bdrv_getlength(bs->file->bs);
        if (bs_size < 0) {
            error_setg_errno(errp, -bs_size, "Unable to learn image size");
            ret = bs_size;
            goto fail;
        }
        if (s->free_data_block_offset > bs_size) {
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

    /* Disable migration when VHD images are used */
    error_setg(&s->migration_blocker, "The vpc format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker(s->migration_blocker, errp);
    if (ret < 0) {
        error_free(s->migration_blocker);
        goto fail;
    }

    qemu_co_mutex_init(&s->lock);
    qemu_opts_del(opts);

    return 0;

fail:
    qemu_opts_del(opts);
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
 * If an error occurred trying to write an updated block bitmap back to
 * the file, -2 is returned, and the error value is written to *err.
 * This can only happen for a write operation.
 *
 * The parameter write must be 1 if the offset will be used for a write
 * operation (the block bitmaps is updated then), 0 otherwise.
 * If write is true then err must not be NULL.
 */
static inline int64_t get_image_offset(BlockDriverState *bs, uint64_t offset,
                                       bool write, int *err)
{
    BDRVVPCState *s = bs->opaque;
    uint64_t bitmap_offset, block_offset;
    uint32_t pagetable_index, offset_in_block;

    assert(!(write && err == NULL));

    pagetable_index = offset / s->block_size;
    offset_in_block = offset % s->block_size;

    if (pagetable_index >= s->max_table_entries || s->pagetable[pagetable_index] == 0xffffffff)
        return -1; /* not allocated */

    bitmap_offset = 512 * (uint64_t) s->pagetable[pagetable_index];
    block_offset = bitmap_offset + s->bitmap_size + offset_in_block;

    /* We must ensure that we don't write to any sectors which are marked as
       unused in the bitmap. We get away with setting all bits in the block
       bitmap each time we write to a new block. This might cause Virtual PC to
       miss sparse read optimization, but it's not a problem in terms of
       correctness. */
    if (write && (s->last_bitmap_offset != bitmap_offset)) {
        uint8_t bitmap[s->bitmap_size];
        int r;

        s->last_bitmap_offset = bitmap_offset;
        memset(bitmap, 0xff, s->bitmap_size);
        r = bdrv_pwrite_sync(bs->file, bitmap_offset, bitmap, s->bitmap_size);
        if (r < 0) {
            *err = r;
            return -2;
        }
    }

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
static int64_t alloc_block(BlockDriverState* bs, int64_t offset)
{
    BDRVVPCState *s = bs->opaque;
    int64_t bat_offset;
    uint32_t index, bat_value;
    int ret;
    uint8_t bitmap[s->bitmap_size];

    /* Check if sector_num is valid */
    if ((offset < 0) || (offset > bs->total_sectors * BDRV_SECTOR_SIZE)) {
        return -EINVAL;
    }

    /* Write entry into in-memory BAT */
    index = offset / s->block_size;
    assert(s->pagetable[index] == 0xFFFFFFFF);
    s->pagetable[index] = s->free_data_block_offset / 512;

    /* Initialize the block's bitmap */
    memset(bitmap, 0xff, s->bitmap_size);
    ret = bdrv_pwrite_sync(bs->file, s->free_data_block_offset, bitmap,
        s->bitmap_size);
    if (ret < 0) {
        return ret;
    }

    /* Write new footer (the old one will be overwritten) */
    s->free_data_block_offset += s->block_size + s->bitmap_size;
    ret = rewrite_footer(bs);
    if (ret < 0)
        goto fail;

    /* Write BAT entry to disk */
    bat_offset = s->bat_offset + (4 * index);
    bat_value = cpu_to_be32(s->pagetable[index]);
    ret = bdrv_pwrite_sync(bs->file, bat_offset, &bat_value, 4);
    if (ret < 0)
        goto fail;

    return get_image_offset(bs, offset, false, NULL);

fail:
    s->free_data_block_offset -= (s->block_size + s->bitmap_size);
    return ret;
}

static int vpc_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVVPCState *s = (BDRVVPCState *)bs->opaque;
    VHDFooter *footer = (VHDFooter *) s->footer_buf;

    if (be32_to_cpu(footer->type) != VHD_FIXED) {
        bdi->cluster_size = s->block_size;
    }

    return 0;
}

static int coroutine_fn
vpc_co_preadv(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
              QEMUIOVector *qiov, int flags)
{
    BDRVVPCState *s = bs->opaque;
    int ret;
    int64_t image_offset;
    int64_t n_bytes;
    int64_t bytes_done = 0;
    VHDFooter *footer = (VHDFooter *) s->footer_buf;
    QEMUIOVector local_qiov;

    if (be32_to_cpu(footer->type) == VHD_FIXED) {
        return bdrv_co_preadv(bs->file, offset, bytes, qiov, 0);
    }

    qemu_co_mutex_lock(&s->lock);
    qemu_iovec_init(&local_qiov, qiov->niov);

    while (bytes > 0) {
        image_offset = get_image_offset(bs, offset, false, NULL);
        n_bytes = MIN(bytes, s->block_size - (offset % s->block_size));

        if (image_offset == -1) {
            qemu_iovec_memset(qiov, bytes_done, 0, n_bytes);
        } else {
            qemu_iovec_reset(&local_qiov);
            qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

            qemu_co_mutex_unlock(&s->lock);
            ret = bdrv_co_preadv(bs->file, image_offset, n_bytes,
                                 &local_qiov, 0);
            qemu_co_mutex_lock(&s->lock);
            if (ret < 0) {
                goto fail;
            }
        }

        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;
    }

    ret = 0;
fail:
    qemu_iovec_destroy(&local_qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static int coroutine_fn
vpc_co_pwritev(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
               QEMUIOVector *qiov, int flags)
{
    BDRVVPCState *s = bs->opaque;
    int64_t image_offset;
    int64_t n_bytes;
    int64_t bytes_done = 0;
    int ret = 0;
    VHDFooter *footer =  (VHDFooter *) s->footer_buf;
    QEMUIOVector local_qiov;

    if (be32_to_cpu(footer->type) == VHD_FIXED) {
        return bdrv_co_pwritev(bs->file, offset, bytes, qiov, 0);
    }

    qemu_co_mutex_lock(&s->lock);
    qemu_iovec_init(&local_qiov, qiov->niov);

    while (bytes > 0) {
        image_offset = get_image_offset(bs, offset, true, &ret);
        if (image_offset == -2) {
            /* Failed to write block bitmap: can't proceed with write */
            goto fail;
        }
        n_bytes = MIN(bytes, s->block_size - (offset % s->block_size));

        if (image_offset == -1) {
            image_offset = alloc_block(bs, offset);
            if (image_offset < 0) {
                ret = image_offset;
                goto fail;
            }
        }

        qemu_iovec_reset(&local_qiov);
        qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

        qemu_co_mutex_unlock(&s->lock);
        ret = bdrv_co_pwritev(bs->file, image_offset, n_bytes,
                              &local_qiov, 0);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            goto fail;
        }

        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;
    }

    ret = 0;
fail:
    qemu_iovec_destroy(&local_qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static int coroutine_fn vpc_co_block_status(BlockDriverState *bs,
                                            bool want_zero,
                                            int64_t offset, int64_t bytes,
                                            int64_t *pnum, int64_t *map,
                                            BlockDriverState **file)
{
    BDRVVPCState *s = bs->opaque;
    VHDFooter *footer = (VHDFooter*) s->footer_buf;
    int64_t image_offset;
    bool allocated;
    int ret;
    int64_t n;

    if (be32_to_cpu(footer->type) == VHD_FIXED) {
        *pnum = bytes;
        *map = offset;
        *file = bs->file->bs;
        return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | BDRV_BLOCK_RECURSE;
    }

    qemu_co_mutex_lock(&s->lock);

    image_offset = get_image_offset(bs, offset, false, NULL);
    allocated = (image_offset != -1);
    *pnum = 0;
    ret = BDRV_BLOCK_ZERO;

    do {
        /* All sectors in a block are contiguous (without using the bitmap) */
        n = ROUND_UP(offset + 1, s->block_size) - offset;
        n = MIN(n, bytes);

        *pnum += n;
        offset += n;
        bytes -= n;
        /* *pnum can't be greater than one block for allocated
         * sectors since there is always a bitmap in between. */
        if (allocated) {
            *file = bs->file->bs;
            *map = image_offset;
            ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
            break;
        }
        if (bytes == 0) {
            break;
        }
        image_offset = get_image_offset(bs, offset, false, NULL);
    } while (image_offset == -1);

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
 * Returns 0 on success, -EFBIG if the size is larger than 2040 GiB. Override
 * the hardware EIDE and ATA-2 limit of 16 heads (max disk size of 127 GB)
 * and instead allow up to 255 heads.
 */
static int calculate_geometry(int64_t total_sectors, uint16_t* cyls,
    uint8_t* heads, uint8_t* secs_per_cyl)
{
    uint32_t cyls_times_heads;

    total_sectors = MIN(total_sectors, VHD_MAX_GEOMETRY);

    if (total_sectors >= 65535LL * 16 * 63) {
        *secs_per_cyl = 255;
        *heads = 16;
        cyls_times_heads = total_sectors / *secs_per_cyl;
    } else {
        *secs_per_cyl = 17;
        cyls_times_heads = total_sectors / *secs_per_cyl;
        *heads = DIV_ROUND_UP(cyls_times_heads, 1024);

        if (*heads < 4) {
            *heads = 4;
        }

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

static int create_dynamic_disk(BlockBackend *blk, uint8_t *buf,
                               int64_t total_sectors)
{
    VHDDynDiskHeader *dyndisk_header =
        (VHDDynDiskHeader *) buf;
    size_t block_size, num_bat_entries;
    int i;
    int ret;
    int64_t offset = 0;

    /* Write the footer (twice: at the beginning and at the end) */
    block_size = 0x200000;
    num_bat_entries = DIV_ROUND_UP(total_sectors, block_size / 512);

    ret = blk_pwrite(blk, offset, buf, HEADER_SIZE, 0);
    if (ret < 0) {
        goto fail;
    }

    offset = 1536 + ((num_bat_entries * 4 + 511) & ~511);
    ret = blk_pwrite(blk, offset, buf, HEADER_SIZE, 0);
    if (ret < 0) {
        goto fail;
    }

    /* Write the initial BAT */
    offset = 3 * 512;

    memset(buf, 0xFF, 512);
    for (i = 0; i < DIV_ROUND_UP(num_bat_entries * 4, 512); i++) {
        ret = blk_pwrite(blk, offset, buf, 512, 0);
        if (ret < 0) {
            goto fail;
        }
        offset += 512;
    }

    /* Prepare the Dynamic Disk Header */
    memset(buf, 0, 1024);

    memcpy(dyndisk_header->magic, "cxsparse", 8);

    /*
     * Note: The spec is actually wrong here for data_offset, it says
     * 0xFFFFFFFF, but MS tools expect all 64 bits to be set.
     */
    dyndisk_header->data_offset = cpu_to_be64(0xFFFFFFFFFFFFFFFFULL);
    dyndisk_header->table_offset = cpu_to_be64(3 * 512);
    dyndisk_header->version = cpu_to_be32(0x00010000);
    dyndisk_header->block_size = cpu_to_be32(block_size);
    dyndisk_header->max_table_entries = cpu_to_be32(num_bat_entries);

    dyndisk_header->checksum = cpu_to_be32(vpc_checksum(buf, 1024));

    /* Write the header */
    offset = 512;

    ret = blk_pwrite(blk, offset, buf, 1024, 0);
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
 fail:
    return ret;
}

static int create_fixed_disk(BlockBackend *blk, uint8_t *buf,
                             int64_t total_size, Error **errp)
{
    int ret;

    /* Add footer to total size */
    total_size += HEADER_SIZE;

    ret = blk_truncate(blk, total_size, false, PREALLOC_MODE_OFF, 0, errp);
    if (ret < 0) {
        return ret;
    }

    ret = blk_pwrite(blk, total_size - HEADER_SIZE, buf, HEADER_SIZE, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Unable to write VHD header");
        return ret;
    }

    return 0;
}

static int calculate_rounded_image_size(BlockdevCreateOptionsVpc *vpc_opts,
                                        uint16_t *out_cyls,
                                        uint8_t *out_heads,
                                        uint8_t *out_secs_per_cyl,
                                        int64_t *out_total_sectors,
                                        Error **errp)
{
    int64_t total_size = vpc_opts->size;
    uint16_t cyls = 0;
    uint8_t heads = 0;
    uint8_t secs_per_cyl = 0;
    int64_t total_sectors;
    int i;

    /*
     * Calculate matching total_size and geometry. Increase the number of
     * sectors requested until we get enough (or fail). This ensures that
     * qemu-img convert doesn't truncate images, but rather rounds up.
     *
     * If the image size can't be represented by a spec conformant CHS geometry,
     * we set the geometry to 65535 x 16 x 255 (CxHxS) sectors and use
     * the image size from the VHD footer to calculate total_sectors.
     */
    if (vpc_opts->force_size) {
        /* This will force the use of total_size for sector count, below */
        cyls         = VHD_CHS_MAX_C;
        heads        = VHD_CHS_MAX_H;
        secs_per_cyl = VHD_CHS_MAX_S;
    } else {
        total_sectors = MIN(VHD_MAX_GEOMETRY, total_size / BDRV_SECTOR_SIZE);
        for (i = 0; total_sectors > (int64_t)cyls * heads * secs_per_cyl; i++) {
            calculate_geometry(total_sectors + i, &cyls, &heads, &secs_per_cyl);
        }
    }

    if ((int64_t)cyls * heads * secs_per_cyl == VHD_MAX_GEOMETRY) {
        total_sectors = total_size / BDRV_SECTOR_SIZE;
        /* Allow a maximum disk size of 2040 GiB */
        if (total_sectors > VHD_MAX_SECTORS) {
            error_setg(errp, "Disk size is too large, max size is 2040 GiB");
            return -EFBIG;
        }
    } else {
        total_sectors = (int64_t) cyls * heads * secs_per_cyl;
    }

    *out_total_sectors = total_sectors;
    if (out_cyls) {
        *out_cyls = cyls;
        *out_heads = heads;
        *out_secs_per_cyl = secs_per_cyl;
    }

    return 0;
}

static int coroutine_fn vpc_co_create(BlockdevCreateOptions *opts,
                                      Error **errp)
{
    BlockdevCreateOptionsVpc *vpc_opts;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;

    uint8_t buf[1024];
    VHDFooter *footer = (VHDFooter *) buf;
    uint16_t cyls = 0;
    uint8_t heads = 0;
    uint8_t secs_per_cyl = 0;
    int64_t total_sectors;
    int64_t total_size;
    int disk_type;
    int ret = -EIO;
    QemuUUID uuid;

    assert(opts->driver == BLOCKDEV_DRIVER_VPC);
    vpc_opts = &opts->u.vpc;

    /* Validate options and set default values */
    total_size = vpc_opts->size;

    if (!vpc_opts->has_subformat) {
        vpc_opts->subformat = BLOCKDEV_VPC_SUBFORMAT_DYNAMIC;
    }
    switch (vpc_opts->subformat) {
    case BLOCKDEV_VPC_SUBFORMAT_DYNAMIC:
        disk_type = VHD_DYNAMIC;
        break;
    case BLOCKDEV_VPC_SUBFORMAT_FIXED:
        disk_type = VHD_FIXED;
        break;
    default:
        g_assert_not_reached();
    }

    /* Create BlockBackend to write to the image */
    bs = bdrv_open_blockdev_ref(vpc_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                          errp);
    if (!blk) {
        ret = -EPERM;
        goto out;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    /* Get geometry and check that it matches the image size*/
    ret = calculate_rounded_image_size(vpc_opts, &cyls, &heads, &secs_per_cyl,
                                       &total_sectors, errp);
    if (ret < 0) {
        goto out;
    }

    if (total_size != total_sectors * BDRV_SECTOR_SIZE) {
        error_setg(errp, "The requested image size cannot be represented in "
                         "CHS geometry");
        error_append_hint(errp, "Try size=%llu or force-size=on (the "
                                "latter makes the image incompatible with "
                                "Virtual PC)",
                          total_sectors * BDRV_SECTOR_SIZE);
        ret = -EINVAL;
        goto out;
    }

    /* Prepare the Hard Disk Footer */
    memset(buf, 0, 1024);

    memcpy(footer->creator, "conectix", 8);
    if (vpc_opts->force_size) {
        memcpy(footer->creator_app, "qem2", 4);
    } else {
        memcpy(footer->creator_app, "qemu", 4);
    }
    memcpy(footer->creator_os, "Wi2k", 4);

    footer->features = cpu_to_be32(0x02);
    footer->version = cpu_to_be32(0x00010000);
    if (disk_type == VHD_DYNAMIC) {
        footer->data_offset = cpu_to_be64(HEADER_SIZE);
    } else {
        footer->data_offset = cpu_to_be64(0xFFFFFFFFFFFFFFFFULL);
    }
    footer->timestamp = cpu_to_be32(time(NULL) - VHD_TIMESTAMP_BASE);

    /* Version of Virtual PC 2007 */
    footer->major = cpu_to_be16(0x0005);
    footer->minor = cpu_to_be16(0x0003);
    footer->orig_size = cpu_to_be64(total_size);
    footer->current_size = cpu_to_be64(total_size);
    footer->cyls = cpu_to_be16(cyls);
    footer->heads = heads;
    footer->secs_per_cyl = secs_per_cyl;

    footer->type = cpu_to_be32(disk_type);

    qemu_uuid_generate(&uuid);
    footer->uuid = uuid;

    footer->checksum = cpu_to_be32(vpc_checksum(buf, HEADER_SIZE));

    if (disk_type == VHD_DYNAMIC) {
        ret = create_dynamic_disk(blk, buf, total_sectors);
        if (ret < 0) {
            error_setg(errp, "Unable to create or write VHD header");
        }
    } else {
        ret = create_fixed_disk(blk, buf, total_size, errp);
    }

out:
    blk_unref(blk);
    bdrv_unref(bs);
    return ret;
}

static int coroutine_fn vpc_co_create_opts(BlockDriver *drv,
                                           const char *filename,
                                           QemuOpts *opts,
                                           Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    QDict *qdict;
    Visitor *v;
    BlockDriverState *bs = NULL;
    int ret;

    static const QDictRenames opt_renames[] = {
        { VPC_OPT_FORCE_SIZE,           "force-size" },
        { NULL, NULL },
    };

    /* Parse options and convert legacy syntax */
    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &vpc_create_opts, true);

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    /* Create and open the file (protocol layer) */
    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        goto fail;
    }

    bs = bdrv_open(filename, NULL, NULL,
                   BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    /* Now get the QAPI type BlockdevCreateOptions */
    qdict_put_str(qdict, "driver", "vpc");
    qdict_put_str(qdict, "file", bs->node_name);

    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto fail;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto fail;
    }

    /* Silently round up size */
    assert(create_options->driver == BLOCKDEV_DRIVER_VPC);
    create_options->u.vpc.size =
        ROUND_UP(create_options->u.vpc.size, BDRV_SECTOR_SIZE);

    if (!create_options->u.vpc.force_size) {
        int64_t total_sectors;
        ret = calculate_rounded_image_size(&create_options->u.vpc, NULL, NULL,
                                           NULL, &total_sectors, errp);
        if (ret < 0) {
            goto fail;
        }

        create_options->u.vpc.size = total_sectors * BDRV_SECTOR_SIZE;
    }


    /* Create the vpc image (format layer) */
    ret = vpc_co_create(create_options, errp);

fail:
    qobject_unref(qdict);
    bdrv_unref(bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}


static int vpc_has_zero_init(BlockDriverState *bs)
{
    BDRVVPCState *s = bs->opaque;
    VHDFooter *footer =  (VHDFooter *) s->footer_buf;

    if (be32_to_cpu(footer->type) == VHD_FIXED) {
        return bdrv_has_zero_init(bs->file->bs);
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

static QemuOptsList vpc_create_opts = {
    .name = "vpc-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(vpc_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_SUBFMT,
            .type = QEMU_OPT_STRING,
            .help =
                "Type of virtual hard disk format. Supported formats are "
                "{dynamic (default) | fixed} "
        },
        {
            .name = VPC_OPT_FORCE_SIZE,
            .type = QEMU_OPT_BOOL,
            .help = "Force disk size calculation to use the actual size "
                    "specified, rather than using the nearest CHS-based "
                    "calculation"
        },
        { /* end of list */ }
    }
};

static const char *const vpc_strong_runtime_opts[] = {
    VPC_OPT_SIZE_CALC,

    NULL
};

static BlockDriver bdrv_vpc = {
    .format_name    = "vpc",
    .instance_size  = sizeof(BDRVVPCState),

    .bdrv_probe             = vpc_probe,
    .bdrv_open              = vpc_open,
    .bdrv_close             = vpc_close,
    .bdrv_reopen_prepare    = vpc_reopen_prepare,
    .bdrv_child_perm        = bdrv_default_perms,
    .bdrv_co_create         = vpc_co_create,
    .bdrv_co_create_opts    = vpc_co_create_opts,

    .bdrv_co_preadv             = vpc_co_preadv,
    .bdrv_co_pwritev            = vpc_co_pwritev,
    .bdrv_co_block_status       = vpc_co_block_status,

    .bdrv_get_info          = vpc_get_info,

    .is_format              = true,
    .create_opts            = &vpc_create_opts,
    .bdrv_has_zero_init     = vpc_has_zero_init,
    .strong_runtime_opts    = vpc_strong_runtime_opts,
};

static void bdrv_vpc_init(void)
{
    bdrv_register(&bdrv_vpc);
}

block_init(bdrv_vpc_init);
