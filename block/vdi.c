/*
 * Block driver for the Virtual Disk Image (VDI) format
 *
 * Copyright (c) 2009, 2012 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) version 3 or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reference:
 * http://forums.virtualbox.org/viewtopic.php?t=8046
 *
 * This driver supports create / read / write operations on VDI images.
 *
 * Todo (see also TODO in code):
 *
 * Some features like snapshots are still missing.
 *
 * Deallocation of zero-filled blocks and shrinking images are missing, too
 * (might be added to common block layer).
 *
 * Allocation of blocks could be optimized (less writes to block map and
 * header).
 *
 * Read and write of adjacent blocks could be done in one operation
 * (current code uses one operation per block (1 MiB).
 *
 * The code is not thread safe (missing locks for changes in header and
 * block table, no problem with current QEMU).
 *
 * Hints:
 *
 * Blocks (VDI documentation) correspond to clusters (QEMU).
 * QEMU's backing files could be implemented using VDI snapshot files (TODO).
 * VDI snapshot files may also contain the complete machine state.
 * Maybe this machine state can be converted to QEMU PC machine snapshot data.
 *
 * The driver keeps a block cache (little endian entries) in memory.
 * For the standard block size (1 MiB), a 1 TiB disk will use 4 MiB RAM,
 * so this seems to be reasonable.
 */

#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "migration/migration.h"

#if defined(CONFIG_UUID)
#include <uuid/uuid.h>
#else
/* TODO: move uuid emulation to some central place in QEMU. */
#include "sysemu/sysemu.h"     /* UUID_FMT */
typedef unsigned char uuid_t[16];
#endif

/* Code configuration options. */

/* Enable debug messages. */
//~ #define CONFIG_VDI_DEBUG

/* Support write operations on VDI images. */
#define CONFIG_VDI_WRITE

/* Support non-standard block (cluster) size. This is untested.
 * Maybe it will be needed for very large images.
 */
//~ #define CONFIG_VDI_BLOCK_SIZE

/* Support static (fixed, pre-allocated) images. */
#define CONFIG_VDI_STATIC_IMAGE

/* Command line option for static images. */
#define BLOCK_OPT_STATIC "static"

#define KiB     1024
#define MiB     (KiB * KiB)

#define SECTOR_SIZE 512
#define DEFAULT_CLUSTER_SIZE (1 * MiB)

#if defined(CONFIG_VDI_DEBUG)
#define logout(fmt, ...) \
                fprintf(stderr, "vdi\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

/* Image signature. */
#define VDI_SIGNATURE 0xbeda107f

/* Image version. */
#define VDI_VERSION_1_1 0x00010001

/* Image type. */
#define VDI_TYPE_DYNAMIC 1
#define VDI_TYPE_STATIC  2

/* Innotek / SUN images use these strings in header.text:
 * "<<< innotek VirtualBox Disk Image >>>\n"
 * "<<< Sun xVM VirtualBox Disk Image >>>\n"
 * "<<< Sun VirtualBox Disk Image >>>\n"
 * The value does not matter, so QEMU created images use a different text.
 */
#define VDI_TEXT "<<< QEMU VM Virtual Disk Image >>>\n"

/* A never-allocated block; semantically arbitrary content. */
#define VDI_UNALLOCATED 0xffffffffU

/* A discarded (no longer allocated) block; semantically zero-filled. */
#define VDI_DISCARDED   0xfffffffeU

#define VDI_IS_ALLOCATED(X) ((X) < VDI_DISCARDED)

/* max blocks in image is (0xffffffff / 4) */
#define VDI_BLOCKS_IN_IMAGE_MAX  0x3fffffff
#define VDI_DISK_SIZE_MAX        ((uint64_t)VDI_BLOCKS_IN_IMAGE_MAX * \
                                  (uint64_t)DEFAULT_CLUSTER_SIZE)

#if !defined(CONFIG_UUID)
static inline void uuid_generate(uuid_t out)
{
    memset(out, 0, sizeof(uuid_t));
}

static inline int uuid_is_null(const uuid_t uu)
{
    uuid_t null_uuid = { 0 };
    return memcmp(uu, null_uuid, sizeof(uuid_t)) == 0;
}

static inline void uuid_unparse(const uuid_t uu, char *out)
{
    snprintf(out, 37, UUID_FMT,
            uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7],
            uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}
#endif

typedef struct {
    char text[0x40];
    uint32_t signature;
    uint32_t version;
    uint32_t header_size;
    uint32_t image_type;
    uint32_t image_flags;
    char description[256];
    uint32_t offset_bmap;
    uint32_t offset_data;
    uint32_t cylinders;         /* disk geometry, unused here */
    uint32_t heads;             /* disk geometry, unused here */
    uint32_t sectors;           /* disk geometry, unused here */
    uint32_t sector_size;
    uint32_t unused1;
    uint64_t disk_size;
    uint32_t block_size;
    uint32_t block_extra;       /* unused here */
    uint32_t blocks_in_image;
    uint32_t blocks_allocated;
    uuid_t uuid_image;
    uuid_t uuid_last_snap;
    uuid_t uuid_link;
    uuid_t uuid_parent;
    uint64_t unused2[7];
} QEMU_PACKED VdiHeader;

typedef struct {
    /* The block map entries are little endian (even in memory). */
    uint32_t *bmap;
    /* Size of block (bytes). */
    uint32_t block_size;
    /* Size of block (sectors). */
    uint32_t block_sectors;
    /* First sector of block map. */
    uint32_t bmap_sector;
    /* VDI header (converted to host endianness). */
    VdiHeader header;

    Error *migration_blocker;
} BDRVVdiState;

/* Change UUID from little endian (IPRT = VirtualBox format) to big endian
 * format (network byte order, standard, see RFC 4122) and vice versa.
 */
static void uuid_convert(uuid_t uuid)
{
    bswap32s((uint32_t *)&uuid[0]);
    bswap16s((uint16_t *)&uuid[4]);
    bswap16s((uint16_t *)&uuid[6]);
}

static void vdi_header_to_cpu(VdiHeader *header)
{
    le32_to_cpus(&header->signature);
    le32_to_cpus(&header->version);
    le32_to_cpus(&header->header_size);
    le32_to_cpus(&header->image_type);
    le32_to_cpus(&header->image_flags);
    le32_to_cpus(&header->offset_bmap);
    le32_to_cpus(&header->offset_data);
    le32_to_cpus(&header->cylinders);
    le32_to_cpus(&header->heads);
    le32_to_cpus(&header->sectors);
    le32_to_cpus(&header->sector_size);
    le64_to_cpus(&header->disk_size);
    le32_to_cpus(&header->block_size);
    le32_to_cpus(&header->block_extra);
    le32_to_cpus(&header->blocks_in_image);
    le32_to_cpus(&header->blocks_allocated);
    uuid_convert(header->uuid_image);
    uuid_convert(header->uuid_last_snap);
    uuid_convert(header->uuid_link);
    uuid_convert(header->uuid_parent);
}

static void vdi_header_to_le(VdiHeader *header)
{
    cpu_to_le32s(&header->signature);
    cpu_to_le32s(&header->version);
    cpu_to_le32s(&header->header_size);
    cpu_to_le32s(&header->image_type);
    cpu_to_le32s(&header->image_flags);
    cpu_to_le32s(&header->offset_bmap);
    cpu_to_le32s(&header->offset_data);
    cpu_to_le32s(&header->cylinders);
    cpu_to_le32s(&header->heads);
    cpu_to_le32s(&header->sectors);
    cpu_to_le32s(&header->sector_size);
    cpu_to_le64s(&header->disk_size);
    cpu_to_le32s(&header->block_size);
    cpu_to_le32s(&header->block_extra);
    cpu_to_le32s(&header->blocks_in_image);
    cpu_to_le32s(&header->blocks_allocated);
    uuid_convert(header->uuid_image);
    uuid_convert(header->uuid_last_snap);
    uuid_convert(header->uuid_link);
    uuid_convert(header->uuid_parent);
}

#if defined(CONFIG_VDI_DEBUG)
static void vdi_header_print(VdiHeader *header)
{
    char uuid[37];
    logout("text        %s", header->text);
    logout("signature   0x%08x\n", header->signature);
    logout("header size 0x%04x\n", header->header_size);
    logout("image type  0x%04x\n", header->image_type);
    logout("image flags 0x%04x\n", header->image_flags);
    logout("description %s\n", header->description);
    logout("offset bmap 0x%04x\n", header->offset_bmap);
    logout("offset data 0x%04x\n", header->offset_data);
    logout("cylinders   0x%04x\n", header->cylinders);
    logout("heads       0x%04x\n", header->heads);
    logout("sectors     0x%04x\n", header->sectors);
    logout("sector size 0x%04x\n", header->sector_size);
    logout("image size  0x%" PRIx64 " B (%" PRIu64 " MiB)\n",
           header->disk_size, header->disk_size / MiB);
    logout("block size  0x%04x\n", header->block_size);
    logout("block extra 0x%04x\n", header->block_extra);
    logout("blocks tot. 0x%04x\n", header->blocks_in_image);
    logout("blocks all. 0x%04x\n", header->blocks_allocated);
    uuid_unparse(header->uuid_image, uuid);
    logout("uuid image  %s\n", uuid);
    uuid_unparse(header->uuid_last_snap, uuid);
    logout("uuid snap   %s\n", uuid);
    uuid_unparse(header->uuid_link, uuid);
    logout("uuid link   %s\n", uuid);
    uuid_unparse(header->uuid_parent, uuid);
    logout("uuid parent %s\n", uuid);
}
#endif

static int vdi_check(BlockDriverState *bs, BdrvCheckResult *res,
                     BdrvCheckMode fix)
{
    /* TODO: additional checks possible. */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    uint32_t blocks_allocated = 0;
    uint32_t block;
    uint32_t *bmap;
    logout("\n");

    if (fix) {
        return -ENOTSUP;
    }

    bmap = g_try_new(uint32_t, s->header.blocks_in_image);
    if (s->header.blocks_in_image && bmap == NULL) {
        res->check_errors++;
        return -ENOMEM;
    }

    memset(bmap, 0xff, s->header.blocks_in_image * sizeof(uint32_t));

    /* Check block map and value of blocks_allocated. */
    for (block = 0; block < s->header.blocks_in_image; block++) {
        uint32_t bmap_entry = le32_to_cpu(s->bmap[block]);
        if (VDI_IS_ALLOCATED(bmap_entry)) {
            if (bmap_entry < s->header.blocks_in_image) {
                blocks_allocated++;
                if (!VDI_IS_ALLOCATED(bmap[bmap_entry])) {
                    bmap[bmap_entry] = bmap_entry;
                } else {
                    fprintf(stderr, "ERROR: block index %" PRIu32
                            " also used by %" PRIu32 "\n", bmap[bmap_entry], bmap_entry);
                    res->corruptions++;
                }
            } else {
                fprintf(stderr, "ERROR: block index %" PRIu32
                        " too large, is %" PRIu32 "\n", block, bmap_entry);
                res->corruptions++;
            }
        }
    }
    if (blocks_allocated != s->header.blocks_allocated) {
        fprintf(stderr, "ERROR: allocated blocks mismatch, is %" PRIu32
               ", should be %" PRIu32 "\n",
               blocks_allocated, s->header.blocks_allocated);
        res->corruptions++;
    }

    g_free(bmap);

    return 0;
}

static int vdi_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    /* TODO: vdi_get_info would be needed for machine snapshots.
       vm_state_offset is still missing. */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    logout("\n");
    bdi->cluster_size = s->block_size;
    bdi->vm_state_offset = 0;
    bdi->unallocated_blocks_are_zero = true;
    return 0;
}

static int vdi_make_empty(BlockDriverState *bs)
{
    /* TODO: missing code. */
    logout("\n");
    /* The return value for missing code must be 0, see block.c. */
    return 0;
}

static int vdi_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const VdiHeader *header = (const VdiHeader *)buf;
    int ret = 0;

    logout("\n");

    if (buf_size < sizeof(*header)) {
        /* Header too small, no VDI. */
    } else if (le32_to_cpu(header->signature) == VDI_SIGNATURE) {
        ret = 100;
    }

    if (ret == 0) {
        logout("no vdi image\n");
    } else {
        logout("%s", header->text);
    }

    return ret;
}

static int vdi_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVVdiState *s = bs->opaque;
    VdiHeader header;
    size_t bmap_size;
    int ret;

    logout("\n");

    ret = bdrv_read(bs->file, 0, (uint8_t *)&header, 1);
    if (ret < 0) {
        goto fail;
    }

    vdi_header_to_cpu(&header);
#if defined(CONFIG_VDI_DEBUG)
    vdi_header_print(&header);
#endif

    if (header.disk_size > VDI_DISK_SIZE_MAX) {
        error_setg(errp, "Unsupported VDI image size (size is 0x%" PRIx64
                          ", max supported is 0x%" PRIx64 ")",
                          header.disk_size, VDI_DISK_SIZE_MAX);
        ret = -ENOTSUP;
        goto fail;
    }

    if (header.disk_size % SECTOR_SIZE != 0) {
        /* 'VBoxManage convertfromraw' can create images with odd disk sizes.
           We accept them but round the disk size to the next multiple of
           SECTOR_SIZE. */
        logout("odd disk size %" PRIu64 " B, round up\n", header.disk_size);
        header.disk_size += SECTOR_SIZE - 1;
        header.disk_size &= ~(SECTOR_SIZE - 1);
    }

    if (header.signature != VDI_SIGNATURE) {
        error_setg(errp, "Image not in VDI format (bad signature %08" PRIx32
                   ")", header.signature);
        ret = -EINVAL;
        goto fail;
    } else if (header.version != VDI_VERSION_1_1) {
        error_setg(errp, "unsupported VDI image (version %" PRIu32 ".%" PRIu32
                   ")", header.version >> 16, header.version & 0xffff);
        ret = -ENOTSUP;
        goto fail;
    } else if (header.offset_bmap % SECTOR_SIZE != 0) {
        /* We only support block maps which start on a sector boundary. */
        error_setg(errp, "unsupported VDI image (unaligned block map offset "
                   "0x%" PRIx32 ")", header.offset_bmap);
        ret = -ENOTSUP;
        goto fail;
    } else if (header.offset_data % SECTOR_SIZE != 0) {
        /* We only support data blocks which start on a sector boundary. */
        error_setg(errp, "unsupported VDI image (unaligned data offset 0x%"
                   PRIx32 ")", header.offset_data);
        ret = -ENOTSUP;
        goto fail;
    } else if (header.sector_size != SECTOR_SIZE) {
        error_setg(errp, "unsupported VDI image (sector size %" PRIu32
                   " is not %u)", header.sector_size, SECTOR_SIZE);
        ret = -ENOTSUP;
        goto fail;
    } else if (header.block_size != DEFAULT_CLUSTER_SIZE) {
        error_setg(errp, "unsupported VDI image (block size %" PRIu32
                   " is not %u)", header.block_size, DEFAULT_CLUSTER_SIZE);
        ret = -ENOTSUP;
        goto fail;
    } else if (header.disk_size >
               (uint64_t)header.blocks_in_image * header.block_size) {
        error_setg(errp, "unsupported VDI image (disk size %" PRIu64 ", "
                   "image bitmap has room for %" PRIu64 ")",
                   header.disk_size,
                   (uint64_t)header.blocks_in_image * header.block_size);
        ret = -ENOTSUP;
        goto fail;
    } else if (!uuid_is_null(header.uuid_link)) {
        error_setg(errp, "unsupported VDI image (non-NULL link UUID)");
        ret = -ENOTSUP;
        goto fail;
    } else if (!uuid_is_null(header.uuid_parent)) {
        error_setg(errp, "unsupported VDI image (non-NULL parent UUID)");
        ret = -ENOTSUP;
        goto fail;
    } else if (header.blocks_in_image > VDI_BLOCKS_IN_IMAGE_MAX) {
        error_setg(errp, "unsupported VDI image "
                         "(too many blocks %u, max is %u)",
                          header.blocks_in_image, VDI_BLOCKS_IN_IMAGE_MAX);
        ret = -ENOTSUP;
        goto fail;
    }

    bs->total_sectors = header.disk_size / SECTOR_SIZE;

    s->block_size = header.block_size;
    s->block_sectors = header.block_size / SECTOR_SIZE;
    s->bmap_sector = header.offset_bmap / SECTOR_SIZE;
    s->header = header;

    bmap_size = header.blocks_in_image * sizeof(uint32_t);
    bmap_size = (bmap_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    s->bmap = qemu_try_blockalign(bs->file, bmap_size * SECTOR_SIZE);
    if (s->bmap == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_read(bs->file, s->bmap_sector, (uint8_t *)s->bmap, bmap_size);
    if (ret < 0) {
        goto fail_free_bmap;
    }

    /* Disable migration when vdi images are used */
    error_set(&s->migration_blocker,
              QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
              "vdi", bs->device_name, "live migration");
    migrate_add_blocker(s->migration_blocker);

    return 0;

 fail_free_bmap:
    qemu_vfree(s->bmap);

 fail:
    return ret;
}

static int vdi_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static int64_t coroutine_fn vdi_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum)
{
    /* TODO: Check for too large sector_num (in bdrv_is_allocated or here). */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    size_t bmap_index = sector_num / s->block_sectors;
    size_t sector_in_block = sector_num % s->block_sectors;
    int n_sectors = s->block_sectors - sector_in_block;
    uint32_t bmap_entry = le32_to_cpu(s->bmap[bmap_index]);
    uint64_t offset;
    int result;

    logout("%p, %" PRId64 ", %d, %p\n", bs, sector_num, nb_sectors, pnum);
    if (n_sectors > nb_sectors) {
        n_sectors = nb_sectors;
    }
    *pnum = n_sectors;
    result = VDI_IS_ALLOCATED(bmap_entry);
    if (!result) {
        return 0;
    }

    offset = s->header.offset_data +
                              (uint64_t)bmap_entry * s->block_size +
                              sector_in_block * SECTOR_SIZE;
    return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | offset;
}

static int vdi_co_read(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors)
{
    BDRVVdiState *s = bs->opaque;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t sector_in_block;
    uint32_t n_sectors;
    int ret = 0;

    logout("\n");

    while (ret >= 0 && nb_sectors > 0) {
        block_index = sector_num / s->block_sectors;
        sector_in_block = sector_num % s->block_sectors;
        n_sectors = s->block_sectors - sector_in_block;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }

        logout("will read %u sectors starting at sector %" PRIu64 "\n",
               n_sectors, sector_num);

        /* prepare next AIO request */
        bmap_entry = le32_to_cpu(s->bmap[block_index]);
        if (!VDI_IS_ALLOCATED(bmap_entry)) {
            /* Block not allocated, return zeros, no need to wait. */
            memset(buf, 0, n_sectors * SECTOR_SIZE);
            ret = 0;
        } else {
            uint64_t offset = s->header.offset_data / SECTOR_SIZE +
                              (uint64_t)bmap_entry * s->block_sectors +
                              sector_in_block;
            ret = bdrv_read(bs->file, offset, buf, n_sectors);
        }
        logout("%u sectors read\n", n_sectors);

        nb_sectors -= n_sectors;
        sector_num += n_sectors;
        buf += n_sectors * SECTOR_SIZE;
    }

    return ret;
}

static int vdi_co_write(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors)
{
    BDRVVdiState *s = bs->opaque;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t sector_in_block;
    uint32_t n_sectors;
    uint32_t bmap_first = VDI_UNALLOCATED;
    uint32_t bmap_last = VDI_UNALLOCATED;
    uint8_t *block = NULL;
    int ret = 0;

    logout("\n");

    while (ret >= 0 && nb_sectors > 0) {
        block_index = sector_num / s->block_sectors;
        sector_in_block = sector_num % s->block_sectors;
        n_sectors = s->block_sectors - sector_in_block;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }

        logout("will write %u sectors starting at sector %" PRIu64 "\n",
               n_sectors, sector_num);

        /* prepare next AIO request */
        bmap_entry = le32_to_cpu(s->bmap[block_index]);
        if (!VDI_IS_ALLOCATED(bmap_entry)) {
            /* Allocate new block and write to it. */
            uint64_t offset;
            bmap_entry = s->header.blocks_allocated;
            s->bmap[block_index] = cpu_to_le32(bmap_entry);
            s->header.blocks_allocated++;
            offset = s->header.offset_data / SECTOR_SIZE +
                     (uint64_t)bmap_entry * s->block_sectors;
            if (block == NULL) {
                block = g_malloc(s->block_size);
                bmap_first = block_index;
            }
            bmap_last = block_index;
            /* Copy data to be written to new block and zero unused parts. */
            memset(block, 0, sector_in_block * SECTOR_SIZE);
            memcpy(block + sector_in_block * SECTOR_SIZE,
                   buf, n_sectors * SECTOR_SIZE);
            memset(block + (sector_in_block + n_sectors) * SECTOR_SIZE, 0,
                   (s->block_sectors - n_sectors - sector_in_block) * SECTOR_SIZE);
            ret = bdrv_write(bs->file, offset, block, s->block_sectors);
        } else {
            uint64_t offset = s->header.offset_data / SECTOR_SIZE +
                              (uint64_t)bmap_entry * s->block_sectors +
                              sector_in_block;
            ret = bdrv_write(bs->file, offset, buf, n_sectors);
        }

        nb_sectors -= n_sectors;
        sector_num += n_sectors;
        buf += n_sectors * SECTOR_SIZE;

        logout("%u sectors written\n", n_sectors);
    }

    logout("finished data write\n");
    if (ret < 0) {
        return ret;
    }

    if (block) {
        /* One or more new blocks were allocated. */
        VdiHeader *header = (VdiHeader *) block;
        uint8_t *base;
        uint64_t offset;

        logout("now writing modified header\n");
        assert(VDI_IS_ALLOCATED(bmap_first));
        *header = s->header;
        vdi_header_to_le(header);
        ret = bdrv_write(bs->file, 0, block, 1);
        g_free(block);
        block = NULL;

        if (ret < 0) {
            return ret;
        }

        logout("now writing modified block map entry %u...%u\n",
               bmap_first, bmap_last);
        /* Write modified sectors from block map. */
        bmap_first /= (SECTOR_SIZE / sizeof(uint32_t));
        bmap_last /= (SECTOR_SIZE / sizeof(uint32_t));
        n_sectors = bmap_last - bmap_first + 1;
        offset = s->bmap_sector + bmap_first;
        base = ((uint8_t *)&s->bmap[0]) + bmap_first * SECTOR_SIZE;
        logout("will write %u block map sectors starting from entry %u\n",
               n_sectors, bmap_first);
        ret = bdrv_write(bs->file, offset, base, n_sectors);
    }

    return ret;
}

static int vdi_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int ret = 0;
    uint64_t bytes = 0;
    uint32_t blocks;
    size_t block_size = DEFAULT_CLUSTER_SIZE;
    uint32_t image_type = VDI_TYPE_DYNAMIC;
    VdiHeader header;
    size_t i;
    size_t bmap_size;
    int64_t offset = 0;
    Error *local_err = NULL;
    BlockDriverState *bs = NULL;
    uint32_t *bmap = NULL;

    logout("\n");

    /* Read out options. */
    bytes = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);
#if defined(CONFIG_VDI_BLOCK_SIZE)
    /* TODO: Additional checks (SECTOR_SIZE * 2^n, ...). */
    block_size = qemu_opt_get_size_del(opts,
                                       BLOCK_OPT_CLUSTER_SIZE,
                                       DEFAULT_CLUSTER_SIZE);
#endif
#if defined(CONFIG_VDI_STATIC_IMAGE)
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_STATIC, false)) {
        image_type = VDI_TYPE_STATIC;
    }
#endif

    if (bytes > VDI_DISK_SIZE_MAX) {
        ret = -ENOTSUP;
        error_setg(errp, "Unsupported VDI image size (size is 0x%" PRIx64
                          ", max supported is 0x%" PRIx64 ")",
                          bytes, VDI_DISK_SIZE_MAX);
        goto exit;
    }

    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }
    ret = bdrv_open(&bs, filename, NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                    NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }

    /* We need enough blocks to store the given disk size,
       so always round up. */
    blocks = (bytes + block_size - 1) / block_size;

    bmap_size = blocks * sizeof(uint32_t);
    bmap_size = ((bmap_size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE -1));

    memset(&header, 0, sizeof(header));
    pstrcpy(header.text, sizeof(header.text), VDI_TEXT);
    header.signature = VDI_SIGNATURE;
    header.version = VDI_VERSION_1_1;
    header.header_size = 0x180;
    header.image_type = image_type;
    header.offset_bmap = 0x200;
    header.offset_data = 0x200 + bmap_size;
    header.sector_size = SECTOR_SIZE;
    header.disk_size = bytes;
    header.block_size = block_size;
    header.blocks_in_image = blocks;
    if (image_type == VDI_TYPE_STATIC) {
        header.blocks_allocated = blocks;
    }
    uuid_generate(header.uuid_image);
    uuid_generate(header.uuid_last_snap);
    /* There is no need to set header.uuid_link or header.uuid_parent here. */
#if defined(CONFIG_VDI_DEBUG)
    vdi_header_print(&header);
#endif
    vdi_header_to_le(&header);
    ret = bdrv_pwrite_sync(bs, offset, &header, sizeof(header));
    if (ret < 0) {
        error_setg(errp, "Error writing header to %s", filename);
        goto exit;
    }
    offset += sizeof(header);

    if (bmap_size > 0) {
        bmap = g_try_malloc0(bmap_size);
        if (bmap == NULL) {
            ret = -ENOMEM;
            error_setg(errp, "Could not allocate bmap");
            goto exit;
        }
        for (i = 0; i < blocks; i++) {
            if (image_type == VDI_TYPE_STATIC) {
                bmap[i] = i;
            } else {
                bmap[i] = VDI_UNALLOCATED;
            }
        }
        ret = bdrv_pwrite_sync(bs, offset, bmap, bmap_size);
        if (ret < 0) {
            error_setg(errp, "Error writing bmap to %s", filename);
            goto exit;
        }
        offset += bmap_size;
    }

    if (image_type == VDI_TYPE_STATIC) {
        ret = bdrv_truncate(bs, offset + blocks * block_size);
        if (ret < 0) {
            error_setg(errp, "Failed to statically allocate %s", filename);
            goto exit;
        }
    }

exit:
    bdrv_unref(bs);
    g_free(bmap);
    return ret;
}

static void vdi_close(BlockDriverState *bs)
{
    BDRVVdiState *s = bs->opaque;

    qemu_vfree(s->bmap);

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static QemuOptsList vdi_create_opts = {
    .name = "vdi-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(vdi_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
#if defined(CONFIG_VDI_BLOCK_SIZE)
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "VDI cluster (block) size",
            .def_value_str = stringify(DEFAULT_CLUSTER_SIZE)
        },
#endif
#if defined(CONFIG_VDI_STATIC_IMAGE)
        {
            .name = BLOCK_OPT_STATIC,
            .type = QEMU_OPT_BOOL,
            .help = "VDI static (pre-allocated) image",
            .def_value_str = "off"
        },
#endif
        {
            .name = BLOCK_OPT_NOCOW,
            .type = QEMU_OPT_BOOL,
            .help = "Turn off copy-on-write (valid only on btrfs)"
        },
        /* TODO: An additional option to set UUID values might be useful. */
        { /* end of list */ }
    }
};

static BlockDriver bdrv_vdi = {
    .format_name = "vdi",
    .instance_size = sizeof(BDRVVdiState),
    .bdrv_probe = vdi_probe,
    .bdrv_open = vdi_open,
    .bdrv_close = vdi_close,
    .bdrv_reopen_prepare = vdi_reopen_prepare,
    .bdrv_create = vdi_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_co_get_block_status = vdi_co_get_block_status,
    .bdrv_make_empty = vdi_make_empty,

    .bdrv_read = vdi_co_read,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_write = vdi_co_write,
#endif

    .bdrv_get_info = vdi_get_info,

    .create_opts = &vdi_create_opts,
    .bdrv_check = vdi_check,
};

static void bdrv_vdi_init(void)
{
    logout("\n");
    bdrv_register(&bdrv_vdi);
}

block_init(bdrv_vdi_init);
