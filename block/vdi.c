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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "migration/blocker.h"
#include "qemu/coroutine.h"
#include "qemu/cutils.h"
#include "qemu/uuid.h"
#include "qemu/memalign.h"

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

#define SECTOR_SIZE 512
#define DEFAULT_CLUSTER_SIZE 1048576
/* Note: can't use 1 * MiB, because it's passed to stringify() */

#if defined(CONFIG_VDI_DEBUG)
#define VDI_DEBUG 1
#else
#define VDI_DEBUG 0
#endif

#define logout(fmt, ...) \
    do {                                                                \
        if (VDI_DEBUG) {                                                \
            fprintf(stderr, "vdi\t%-24s" fmt, __func__, ##__VA_ARGS__); \
        }                                                               \
    } while (0)

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

/* The bmap will take up VDI_BLOCKS_IN_IMAGE_MAX * sizeof(uint32_t) bytes; since
 * the bmap is read and written in a single operation, its size needs to be
 * limited to INT_MAX; furthermore, when opening an image, the bmap size is
 * rounded up to be aligned on BDRV_SECTOR_SIZE.
 * Therefore this should satisfy the following:
 * VDI_BLOCKS_IN_IMAGE_MAX * sizeof(uint32_t) + BDRV_SECTOR_SIZE == INT_MAX + 1
 * (INT_MAX + 1 is the first value not representable as an int)
 * This guarantees that any value below or equal to the constant will, when
 * multiplied by sizeof(uint32_t) and rounded up to a BDRV_SECTOR_SIZE boundary,
 * still be below or equal to INT_MAX. */
#define VDI_BLOCKS_IN_IMAGE_MAX \
    ((unsigned)((INT_MAX + 1u - BDRV_SECTOR_SIZE) / sizeof(uint32_t)))
#define VDI_DISK_SIZE_MAX        ((uint64_t)VDI_BLOCKS_IN_IMAGE_MAX * \
                                  (uint64_t)DEFAULT_CLUSTER_SIZE)

static QemuOptsList vdi_create_opts;

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
    QemuUUID uuid_image;
    QemuUUID uuid_last_snap;
    QemuUUID uuid_link;
    QemuUUID uuid_parent;
    uint64_t unused2[7];
} QEMU_PACKED VdiHeader;

QEMU_BUILD_BUG_ON(sizeof(VdiHeader) != 512);

typedef struct {
    /* The block map entries are little endian (even in memory). */
    uint32_t *bmap;
    /* Size of block (bytes). */
    uint32_t block_size;
    /* First sector of block map. */
    uint32_t bmap_sector;
    /* VDI header (converted to host endianness). */
    VdiHeader header;

    CoRwlock bmap_lock;

    Error *migration_blocker;
} BDRVVdiState;

static void vdi_header_to_cpu(VdiHeader *header)
{
    header->signature = le32_to_cpu(header->signature);
    header->version = le32_to_cpu(header->version);
    header->header_size = le32_to_cpu(header->header_size);
    header->image_type = le32_to_cpu(header->image_type);
    header->image_flags = le32_to_cpu(header->image_flags);
    header->offset_bmap = le32_to_cpu(header->offset_bmap);
    header->offset_data = le32_to_cpu(header->offset_data);
    header->cylinders = le32_to_cpu(header->cylinders);
    header->heads = le32_to_cpu(header->heads);
    header->sectors = le32_to_cpu(header->sectors);
    header->sector_size = le32_to_cpu(header->sector_size);
    header->disk_size = le64_to_cpu(header->disk_size);
    header->block_size = le32_to_cpu(header->block_size);
    header->block_extra = le32_to_cpu(header->block_extra);
    header->blocks_in_image = le32_to_cpu(header->blocks_in_image);
    header->blocks_allocated = le32_to_cpu(header->blocks_allocated);
    header->uuid_image = qemu_uuid_bswap(header->uuid_image);
    header->uuid_last_snap = qemu_uuid_bswap(header->uuid_last_snap);
    header->uuid_link = qemu_uuid_bswap(header->uuid_link);
    header->uuid_parent = qemu_uuid_bswap(header->uuid_parent);
}

static void vdi_header_to_le(VdiHeader *header)
{
    header->signature = cpu_to_le32(header->signature);
    header->version = cpu_to_le32(header->version);
    header->header_size = cpu_to_le32(header->header_size);
    header->image_type = cpu_to_le32(header->image_type);
    header->image_flags = cpu_to_le32(header->image_flags);
    header->offset_bmap = cpu_to_le32(header->offset_bmap);
    header->offset_data = cpu_to_le32(header->offset_data);
    header->cylinders = cpu_to_le32(header->cylinders);
    header->heads = cpu_to_le32(header->heads);
    header->sectors = cpu_to_le32(header->sectors);
    header->sector_size = cpu_to_le32(header->sector_size);
    header->disk_size = cpu_to_le64(header->disk_size);
    header->block_size = cpu_to_le32(header->block_size);
    header->block_extra = cpu_to_le32(header->block_extra);
    header->blocks_in_image = cpu_to_le32(header->blocks_in_image);
    header->blocks_allocated = cpu_to_le32(header->blocks_allocated);
    header->uuid_image = qemu_uuid_bswap(header->uuid_image);
    header->uuid_last_snap = qemu_uuid_bswap(header->uuid_last_snap);
    header->uuid_link = qemu_uuid_bswap(header->uuid_link);
    header->uuid_parent = qemu_uuid_bswap(header->uuid_parent);
}

static void vdi_header_print(VdiHeader *header)
{
    char uuidstr[37];
    QemuUUID uuid;
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
    uuid = header->uuid_image;
    qemu_uuid_unparse(&uuid, uuidstr);
    logout("uuid image  %s\n", uuidstr);
    uuid = header->uuid_last_snap;
    qemu_uuid_unparse(&uuid, uuidstr);
    logout("uuid snap   %s\n", uuidstr);
    uuid = header->uuid_link;
    qemu_uuid_unparse(&uuid, uuidstr);
    logout("uuid link   %s\n", uuidstr);
    uuid = header->uuid_parent;
    qemu_uuid_unparse(&uuid, uuidstr);
    logout("uuid parent %s\n", uuidstr);
}

static int coroutine_fn vdi_co_check(BlockDriverState *bs, BdrvCheckResult *res,
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
    QemuUUID uuid_link, uuid_parent;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_IMAGE, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    logout("\n");

    ret = bdrv_pread(bs->file, 0, &header, sizeof(header));
    if (ret < 0) {
        goto fail;
    }

    vdi_header_to_cpu(&header);
    if (VDI_DEBUG) {
        vdi_header_print(&header);
    }

    if (header.disk_size > VDI_DISK_SIZE_MAX) {
        error_setg(errp, "Unsupported VDI image size (size is 0x%" PRIx64
                          ", max supported is 0x%" PRIx64 ")",
                          header.disk_size, VDI_DISK_SIZE_MAX);
        ret = -ENOTSUP;
        goto fail;
    }

    uuid_link = header.uuid_link;
    uuid_parent = header.uuid_parent;

    if (header.disk_size % SECTOR_SIZE != 0) {
        /* 'VBoxManage convertfromraw' can create images with odd disk sizes.
           We accept them but round the disk size to the next multiple of
           SECTOR_SIZE. */
        logout("odd disk size %" PRIu64 " B, round up\n", header.disk_size);
        header.disk_size = ROUND_UP(header.disk_size, SECTOR_SIZE);
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
                         " is not %" PRIu32 ")",
                   header.block_size, DEFAULT_CLUSTER_SIZE);
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
    } else if (!qemu_uuid_is_null(&uuid_link)) {
        error_setg(errp, "unsupported VDI image (non-NULL link UUID)");
        ret = -ENOTSUP;
        goto fail;
    } else if (!qemu_uuid_is_null(&uuid_parent)) {
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
    s->bmap_sector = header.offset_bmap / SECTOR_SIZE;
    s->header = header;

    bmap_size = header.blocks_in_image * sizeof(uint32_t);
    bmap_size = DIV_ROUND_UP(bmap_size, SECTOR_SIZE);
    s->bmap = qemu_try_blockalign(bs->file->bs, bmap_size * SECTOR_SIZE);
    if (s->bmap == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, header.offset_bmap, s->bmap,
                     bmap_size * SECTOR_SIZE);
    if (ret < 0) {
        goto fail_free_bmap;
    }

    /* Disable migration when vdi images are used */
    error_setg(&s->migration_blocker, "The vdi format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker(s->migration_blocker, errp);
    if (ret < 0) {
        error_free(s->migration_blocker);
        goto fail_free_bmap;
    }

    qemu_co_rwlock_init(&s->bmap_lock);

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

static int coroutine_fn vdi_co_block_status(BlockDriverState *bs,
                                            bool want_zero,
                                            int64_t offset, int64_t bytes,
                                            int64_t *pnum, int64_t *map,
                                            BlockDriverState **file)
{
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    size_t bmap_index = offset / s->block_size;
    size_t index_in_block = offset % s->block_size;
    uint32_t bmap_entry = le32_to_cpu(s->bmap[bmap_index]);
    int result;

    logout("%p, %" PRId64 ", %" PRId64 ", %p\n", bs, offset, bytes, pnum);
    *pnum = MIN(s->block_size - index_in_block, bytes);
    result = VDI_IS_ALLOCATED(bmap_entry);
    if (!result) {
        return BDRV_BLOCK_ZERO;
    }

    *map = s->header.offset_data + (uint64_t)bmap_entry * s->block_size +
        index_in_block;
    *file = bs->file->bs;
    return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID |
        (s->header.image_type == VDI_TYPE_STATIC ? BDRV_BLOCK_RECURSE : 0);
}

static int coroutine_fn
vdi_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
              QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVVdiState *s = bs->opaque;
    QEMUIOVector local_qiov;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t offset_in_block;
    uint32_t n_bytes;
    uint64_t bytes_done = 0;
    int ret = 0;

    logout("\n");

    qemu_iovec_init(&local_qiov, qiov->niov);

    while (ret >= 0 && bytes > 0) {
        block_index = offset / s->block_size;
        offset_in_block = offset % s->block_size;
        n_bytes = MIN(bytes, s->block_size - offset_in_block);

        logout("will read %u bytes starting at offset %" PRIu64 "\n",
               n_bytes, offset);

        /* prepare next AIO request */
        qemu_co_rwlock_rdlock(&s->bmap_lock);
        bmap_entry = le32_to_cpu(s->bmap[block_index]);
        qemu_co_rwlock_unlock(&s->bmap_lock);
        if (!VDI_IS_ALLOCATED(bmap_entry)) {
            /* Block not allocated, return zeros, no need to wait. */
            qemu_iovec_memset(qiov, bytes_done, 0, n_bytes);
            ret = 0;
        } else {
            uint64_t data_offset = s->header.offset_data +
                                   (uint64_t)bmap_entry * s->block_size +
                                   offset_in_block;

            qemu_iovec_reset(&local_qiov);
            qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

            ret = bdrv_co_preadv(bs->file, data_offset, n_bytes,
                                 &local_qiov, 0);
        }
        logout("%u bytes read\n", n_bytes);

        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;
    }

    qemu_iovec_destroy(&local_qiov);

    return ret;
}

static int coroutine_fn
vdi_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
               QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVVdiState *s = bs->opaque;
    QEMUIOVector local_qiov;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t offset_in_block;
    uint32_t n_bytes;
    uint64_t data_offset;
    uint32_t bmap_first = VDI_UNALLOCATED;
    uint32_t bmap_last = VDI_UNALLOCATED;
    uint8_t *block = NULL;
    uint64_t bytes_done = 0;
    int ret = 0;

    logout("\n");

    qemu_iovec_init(&local_qiov, qiov->niov);

    while (ret >= 0 && bytes > 0) {
        block_index = offset / s->block_size;
        offset_in_block = offset % s->block_size;
        n_bytes = MIN(bytes, s->block_size - offset_in_block);

        logout("will write %u bytes starting at offset %" PRIu64 "\n",
               n_bytes, offset);

        /* prepare next AIO request */
        qemu_co_rwlock_rdlock(&s->bmap_lock);
        bmap_entry = le32_to_cpu(s->bmap[block_index]);
        if (!VDI_IS_ALLOCATED(bmap_entry)) {
            /* Allocate new block and write to it. */
            uint64_t data_offset;
            qemu_co_rwlock_upgrade(&s->bmap_lock);
            bmap_entry = le32_to_cpu(s->bmap[block_index]);
            if (VDI_IS_ALLOCATED(bmap_entry)) {
                /* A concurrent allocation did the work for us.  */
                qemu_co_rwlock_downgrade(&s->bmap_lock);
                goto nonallocating_write;
            }

            bmap_entry = s->header.blocks_allocated;
            s->bmap[block_index] = cpu_to_le32(bmap_entry);
            s->header.blocks_allocated++;
            data_offset = s->header.offset_data +
                          (uint64_t)bmap_entry * s->block_size;
            if (block == NULL) {
                block = g_malloc(s->block_size);
                bmap_first = block_index;
            }
            bmap_last = block_index;
            /* Copy data to be written to new block and zero unused parts. */
            memset(block, 0, offset_in_block);
            qemu_iovec_to_buf(qiov, bytes_done, block + offset_in_block,
                              n_bytes);
            memset(block + offset_in_block + n_bytes, 0,
                   s->block_size - n_bytes - offset_in_block);

            /* Write the new block under CoRwLock write-side protection,
             * so this full-cluster write does not overlap a partial write
             * of the same cluster, issued from the "else" branch.
             */
            ret = bdrv_pwrite(bs->file, data_offset, block, s->block_size);
            qemu_co_rwlock_unlock(&s->bmap_lock);
        } else {
nonallocating_write:
            data_offset = s->header.offset_data +
                           (uint64_t)bmap_entry * s->block_size +
                           offset_in_block;
            qemu_co_rwlock_unlock(&s->bmap_lock);

            qemu_iovec_reset(&local_qiov);
            qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

            ret = bdrv_co_pwritev(bs->file, data_offset, n_bytes,
                                  &local_qiov, 0);
        }

        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;

        logout("%u bytes written\n", n_bytes);
    }

    qemu_iovec_destroy(&local_qiov);

    logout("finished data write\n");
    if (ret < 0) {
        g_free(block);
        return ret;
    }

    if (block) {
        /* One or more new blocks were allocated. */
        VdiHeader *header;
        uint8_t *base;
        uint64_t offset;
        uint32_t n_sectors;

        g_free(block);
        header = g_malloc(sizeof(*header));

        logout("now writing modified header\n");
        assert(VDI_IS_ALLOCATED(bmap_first));
        *header = s->header;
        vdi_header_to_le(header);
        ret = bdrv_pwrite(bs->file, 0, header, sizeof(*header));
        g_free(header);

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
        ret = bdrv_pwrite(bs->file, offset * SECTOR_SIZE, base,
                          n_sectors * SECTOR_SIZE);
    }

    return ret < 0 ? ret : 0;
}

static int coroutine_fn vdi_co_do_create(BlockdevCreateOptions *create_options,
                                         size_t block_size, Error **errp)
{
    BlockdevCreateOptionsVdi *vdi_opts;
    int ret = 0;
    uint64_t bytes = 0;
    uint32_t blocks;
    uint32_t image_type;
    VdiHeader header;
    size_t i;
    size_t bmap_size;
    int64_t offset = 0;
    BlockDriverState *bs_file = NULL;
    BlockBackend *blk = NULL;
    uint32_t *bmap = NULL;
    QemuUUID uuid;

    assert(create_options->driver == BLOCKDEV_DRIVER_VDI);
    vdi_opts = &create_options->u.vdi;

    logout("\n");

    /* Validate options and set default values */
    bytes = vdi_opts->size;

    if (!vdi_opts->has_preallocation) {
        vdi_opts->preallocation = PREALLOC_MODE_OFF;
    }
    switch (vdi_opts->preallocation) {
    case PREALLOC_MODE_OFF:
        image_type = VDI_TYPE_DYNAMIC;
        break;
    case PREALLOC_MODE_METADATA:
        image_type = VDI_TYPE_STATIC;
        break;
    default:
        error_setg(errp, "Preallocation mode not supported for vdi");
        return -EINVAL;
    }

#ifndef CONFIG_VDI_STATIC_IMAGE
    if (image_type == VDI_TYPE_STATIC) {
        ret = -ENOTSUP;
        error_setg(errp, "Statically allocated images cannot be created in "
                   "this build");
        goto exit;
    }
#endif
#ifndef CONFIG_VDI_BLOCK_SIZE
    if (block_size != DEFAULT_CLUSTER_SIZE) {
        ret = -ENOTSUP;
        error_setg(errp,
                   "A non-default cluster size is not supported in this build");
        goto exit;
    }
#endif

    if (bytes > VDI_DISK_SIZE_MAX) {
        ret = -ENOTSUP;
        error_setg(errp, "Unsupported VDI image size (size is 0x%" PRIx64
                          ", max supported is 0x%" PRIx64 ")",
                          bytes, VDI_DISK_SIZE_MAX);
        goto exit;
    }

    /* Create BlockBackend to write to the image */
    bs_file = bdrv_open_blockdev_ref(vdi_opts->file, errp);
    if (!bs_file) {
        ret = -EIO;
        goto exit;
    }

    blk = blk_new_with_bs(bs_file, BLK_PERM_WRITE | BLK_PERM_RESIZE,
                          BLK_PERM_ALL, errp);
    if (!blk) {
        ret = -EPERM;
        goto exit;
    }

    blk_set_allow_write_beyond_eof(blk, true);

    /* We need enough blocks to store the given disk size,
       so always round up. */
    blocks = DIV_ROUND_UP(bytes, block_size);

    bmap_size = blocks * sizeof(uint32_t);
    bmap_size = ROUND_UP(bmap_size, SECTOR_SIZE);

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
    qemu_uuid_generate(&uuid);
    header.uuid_image = uuid;
    qemu_uuid_generate(&uuid);
    header.uuid_last_snap = uuid;
    /* There is no need to set header.uuid_link or header.uuid_parent here. */
    if (VDI_DEBUG) {
        vdi_header_print(&header);
    }
    vdi_header_to_le(&header);
    ret = blk_pwrite(blk, offset, &header, sizeof(header), 0);
    if (ret < 0) {
        error_setg(errp, "Error writing header");
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
        ret = blk_pwrite(blk, offset, bmap, bmap_size, 0);
        if (ret < 0) {
            error_setg(errp, "Error writing bmap");
            goto exit;
        }
        offset += bmap_size;
    }

    if (image_type == VDI_TYPE_STATIC) {
        ret = blk_truncate(blk, offset + blocks * block_size, false,
                           PREALLOC_MODE_OFF, 0, errp);
        if (ret < 0) {
            error_prepend(errp, "Failed to statically allocate file");
            goto exit;
        }
    }

    ret = 0;
exit:
    blk_unref(blk);
    bdrv_unref(bs_file);
    g_free(bmap);
    return ret;
}

static int coroutine_fn vdi_co_create(BlockdevCreateOptions *create_options,
                                      Error **errp)
{
    return vdi_co_do_create(create_options, DEFAULT_CLUSTER_SIZE, errp);
}

static int coroutine_fn vdi_co_create_opts(BlockDriver *drv,
                                           const char *filename,
                                           QemuOpts *opts,
                                           Error **errp)
{
    QDict *qdict = NULL;
    BlockdevCreateOptions *create_options = NULL;
    BlockDriverState *bs_file = NULL;
    uint64_t block_size = DEFAULT_CLUSTER_SIZE;
    bool is_static = false;
    Visitor *v;
    int ret;

    /* Parse options and convert legacy syntax.
     *
     * Since CONFIG_VDI_BLOCK_SIZE is disabled by default,
     * cluster-size is not part of the QAPI schema; therefore we have
     * to parse it before creating the QAPI object. */
#if defined(CONFIG_VDI_BLOCK_SIZE)
    block_size = qemu_opt_get_size_del(opts,
                                       BLOCK_OPT_CLUSTER_SIZE,
                                       DEFAULT_CLUSTER_SIZE);
    if (block_size < BDRV_SECTOR_SIZE || block_size > UINT32_MAX ||
        !is_power_of_2(block_size))
    {
        error_setg(errp, "Invalid cluster size");
        ret = -EINVAL;
        goto done;
    }
#endif
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_STATIC, false)) {
        is_static = true;
    }

    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &vdi_create_opts, true);

    /* Create and open the file (protocol layer) */
    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        goto done;
    }

    bs_file = bdrv_open(filename, NULL, NULL,
                        BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (!bs_file) {
        ret = -EIO;
        goto done;
    }

    qdict_put_str(qdict, "driver", "vdi");
    qdict_put_str(qdict, "file", bs_file->node_name);
    if (is_static) {
        qdict_put_str(qdict, "preallocation", "metadata");
    }

    /* Get the QAPI object */
    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto done;
    }
    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
        ret = -EINVAL;
        goto done;
    }

    /* Silently round up size */
    assert(create_options->driver == BLOCKDEV_DRIVER_VDI);
    create_options->u.vdi.size = ROUND_UP(create_options->u.vdi.size,
                                          BDRV_SECTOR_SIZE);

    /* Create the vdi image (format layer) */
    ret = vdi_co_do_create(create_options, block_size, errp);
done:
    qobject_unref(qdict);
    qapi_free_BlockdevCreateOptions(create_options);
    bdrv_unref(bs_file);
    return ret;
}

static void vdi_close(BlockDriverState *bs)
{
    BDRVVdiState *s = bs->opaque;

    qemu_vfree(s->bmap);

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static int vdi_has_zero_init(BlockDriverState *bs)
{
    BDRVVdiState *s = bs->opaque;

    if (s->header.image_type == VDI_TYPE_STATIC) {
        return bdrv_has_zero_init(bs->file->bs);
    } else {
        return 1;
    }
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
    .bdrv_child_perm          = bdrv_default_perms,
    .bdrv_co_create      = vdi_co_create,
    .bdrv_co_create_opts = vdi_co_create_opts,
    .bdrv_has_zero_init  = vdi_has_zero_init,
    .bdrv_co_block_status = vdi_co_block_status,
    .bdrv_make_empty = vdi_make_empty,

    .bdrv_co_preadv     = vdi_co_preadv,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_co_pwritev    = vdi_co_pwritev,
#endif

    .bdrv_get_info = vdi_get_info,

    .is_format = true,
    .create_opts = &vdi_create_opts,
    .bdrv_co_check = vdi_co_check,
};

static void bdrv_vdi_init(void)
{
    logout("\n");
    bdrv_register(&bdrv_vdi);
}

block_init(bdrv_vdi_init);
