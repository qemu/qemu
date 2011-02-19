/*
 * Block driver for the Virtual Disk Image (VDI) format
 *
 * Copyright (c) 2009 Stefan Weil
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
 * Read and write of adjacents blocks could be done in one operation
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
#include "block_int.h"
#include "module.h"

#if defined(CONFIG_UUID)
#include <uuid/uuid.h>
#else
/* TODO: move uuid emulation to some central place in QEMU. */
#include "sysemu.h"     /* UUID_FMT */
typedef unsigned char uuid_t[16];
void uuid_generate(uuid_t out);
int uuid_is_null(const uuid_t uu);
void uuid_unparse(const uuid_t uu, char *out);
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

/* Unallocated blocks use this index (no need to convert endianness). */
#define VDI_UNALLOCATED UINT32_MAX

#if !defined(CONFIG_UUID)
void uuid_generate(uuid_t out)
{
    memset(out, 0, sizeof(uuid_t));
}

int uuid_is_null(const uuid_t uu)
{
    uuid_t null_uuid = { 0 };
    return memcmp(uu, null_uuid, sizeof(uuid_t)) == 0;
}

void uuid_unparse(const uuid_t uu, char *out)
{
    snprintf(out, 37, UUID_FMT,
            uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7],
            uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}
#endif

typedef struct {
    BlockDriverAIOCB common;
    int64_t sector_num;
    QEMUIOVector *qiov;
    uint8_t *buf;
    /* Total number of sectors. */
    int nb_sectors;
    /* Number of sectors for current AIO. */
    int n_sectors;
    /* New allocated block map entry. */
    uint32_t bmap_first;
    uint32_t bmap_last;
    /* Buffer for new allocated block. */
    void *block_buffer;
    void *orig_buf;
    int header_modified;
    BlockDriverAIOCB *hd_aiocb;
    struct iovec hd_iov;
    QEMUIOVector hd_qiov;
    QEMUBH *bh;
} VdiAIOCB;

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
} VdiHeader;

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
    logout("signature   0x%04x\n", header->signature);
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

static int vdi_check(BlockDriverState *bs, BdrvCheckResult *res)
{
    /* TODO: additional checks possible. */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    uint32_t blocks_allocated = 0;
    uint32_t block;
    uint32_t *bmap;
    logout("\n");

    bmap = qemu_malloc(s->header.blocks_in_image * sizeof(uint32_t));
    memset(bmap, 0xff, s->header.blocks_in_image * sizeof(uint32_t));

    /* Check block map and value of blocks_allocated. */
    for (block = 0; block < s->header.blocks_in_image; block++) {
        uint32_t bmap_entry = le32_to_cpu(s->bmap[block]);
        if (bmap_entry != VDI_UNALLOCATED) {
            if (bmap_entry < s->header.blocks_in_image) {
                blocks_allocated++;
                if (bmap[bmap_entry] == VDI_UNALLOCATED) {
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

    qemu_free(bmap);

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
    int result = 0;

    logout("\n");

    if (buf_size < sizeof(*header)) {
        /* Header too small, no VDI. */
    } else if (le32_to_cpu(header->signature) == VDI_SIGNATURE) {
        result = 100;
    }

    if (result == 0) {
        logout("no vdi image\n");
    } else {
        logout("%s", header->text);
    }

    return result;
}

static int vdi_open(BlockDriverState *bs, int flags)
{
    BDRVVdiState *s = bs->opaque;
    VdiHeader header;
    size_t bmap_size;

    logout("\n");

    if (bdrv_read(bs->file, 0, (uint8_t *)&header, 1) < 0) {
        goto fail;
    }

    vdi_header_to_cpu(&header);
#if defined(CONFIG_VDI_DEBUG)
    vdi_header_print(&header);
#endif

    if (header.disk_size % SECTOR_SIZE != 0) {
        /* 'VBoxManage convertfromraw' can create images with odd disk sizes.
           We accept them but round the disk size to the next multiple of
           SECTOR_SIZE. */
        logout("odd disk size %" PRIu64 " B, round up\n", header.disk_size);
        header.disk_size += SECTOR_SIZE - 1;
        header.disk_size &= ~(SECTOR_SIZE - 1);
    }

    if (header.version != VDI_VERSION_1_1) {
        logout("unsupported version %u.%u\n",
               header.version >> 16, header.version & 0xffff);
        goto fail;
    } else if (header.offset_bmap % SECTOR_SIZE != 0) {
        /* We only support block maps which start on a sector boundary. */
        logout("unsupported block map offset 0x%x B\n", header.offset_bmap);
        goto fail;
    } else if (header.offset_data % SECTOR_SIZE != 0) {
        /* We only support data blocks which start on a sector boundary. */
        logout("unsupported data offset 0x%x B\n", header.offset_data);
        goto fail;
    } else if (header.sector_size != SECTOR_SIZE) {
        logout("unsupported sector size %u B\n", header.sector_size);
        goto fail;
    } else if (header.block_size != 1 * MiB) {
        logout("unsupported block size %u B\n", header.block_size);
        goto fail;
    } else if (header.disk_size >
               (uint64_t)header.blocks_in_image * header.block_size) {
        logout("unsupported disk size %" PRIu64 " B\n", header.disk_size);
        goto fail;
    } else if (!uuid_is_null(header.uuid_link)) {
        logout("link uuid != 0, unsupported\n");
        goto fail;
    } else if (!uuid_is_null(header.uuid_parent)) {
        logout("parent uuid != 0, unsupported\n");
        goto fail;
    }

    bs->total_sectors = header.disk_size / SECTOR_SIZE;

    s->block_size = header.block_size;
    s->block_sectors = header.block_size / SECTOR_SIZE;
    s->bmap_sector = header.offset_bmap / SECTOR_SIZE;
    s->header = header;

    bmap_size = header.blocks_in_image * sizeof(uint32_t);
    bmap_size = (bmap_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (bmap_size > 0) {
        s->bmap = qemu_malloc(bmap_size * SECTOR_SIZE);
    }
    if (bdrv_read(bs->file, s->bmap_sector, (uint8_t *)s->bmap, bmap_size) < 0) {
        goto fail_free_bmap;
    }

    return 0;

 fail_free_bmap:
    qemu_free(s->bmap);

 fail:
    return -1;
}

static int vdi_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
    /* TODO: Check for too large sector_num (in bdrv_is_allocated or here). */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    size_t bmap_index = sector_num / s->block_sectors;
    size_t sector_in_block = sector_num % s->block_sectors;
    int n_sectors = s->block_sectors - sector_in_block;
    uint32_t bmap_entry = le32_to_cpu(s->bmap[bmap_index]);
    logout("%p, %" PRId64 ", %d, %p\n", bs, sector_num, nb_sectors, pnum);
    if (n_sectors > nb_sectors) {
        n_sectors = nb_sectors;
    }
    *pnum = n_sectors;
    return bmap_entry != VDI_UNALLOCATED;
}

static void vdi_aio_cancel(BlockDriverAIOCB *blockacb)
{
    /* TODO: This code is untested. How can I get it executed? */
    VdiAIOCB *acb = container_of(blockacb, VdiAIOCB, common);
    logout("\n");
    if (acb->hd_aiocb) {
        bdrv_aio_cancel(acb->hd_aiocb);
    }
    qemu_aio_release(acb);
}

static AIOPool vdi_aio_pool = {
    .aiocb_size = sizeof(VdiAIOCB),
    .cancel = vdi_aio_cancel,
};

static VdiAIOCB *vdi_aio_setup(BlockDriverState *bs, int64_t sector_num,
        QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int is_write)
{
    VdiAIOCB *acb;

    logout("%p, %" PRId64 ", %p, %d, %p, %p, %d\n",
           bs, sector_num, qiov, nb_sectors, cb, opaque, is_write);

    acb = qemu_aio_get(&vdi_aio_pool, bs, cb, opaque);
    if (acb) {
        acb->hd_aiocb = NULL;
        acb->sector_num = sector_num;
        acb->qiov = qiov;
        if (qiov->niov > 1) {
            acb->buf = qemu_blockalign(bs, qiov->size);
            acb->orig_buf = acb->buf;
            if (is_write) {
                qemu_iovec_to_buffer(qiov, acb->buf);
            }
        } else {
            acb->buf = (uint8_t *)qiov->iov->iov_base;
        }
        acb->nb_sectors = nb_sectors;
        acb->n_sectors = 0;
        acb->bmap_first = VDI_UNALLOCATED;
        acb->bmap_last = VDI_UNALLOCATED;
        acb->block_buffer = NULL;
        acb->header_modified = 0;
    }
    return acb;
}

static int vdi_schedule_bh(QEMUBHFunc *cb, VdiAIOCB *acb)
{
    logout("\n");

    if (acb->bh) {
        return -EIO;
    }

    acb->bh = qemu_bh_new(cb, acb);
    if (!acb->bh) {
        return -EIO;
    }

    qemu_bh_schedule(acb->bh);

    return 0;
}

static void vdi_aio_read_cb(void *opaque, int ret);

static void vdi_aio_read_bh(void *opaque)
{
    VdiAIOCB *acb = opaque;
    logout("\n");
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    vdi_aio_read_cb(opaque, 0);
}

static void vdi_aio_read_cb(void *opaque, int ret)
{
    VdiAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVVdiState *s = bs->opaque;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t sector_in_block;
    uint32_t n_sectors;

    logout("%u sectors read\n", acb->n_sectors);

    acb->hd_aiocb = NULL;

    if (ret < 0) {
        goto done;
    }

    acb->nb_sectors -= acb->n_sectors;

    if (acb->nb_sectors == 0) {
        /* request completed */
        ret = 0;
        goto done;
    }

    acb->sector_num += acb->n_sectors;
    acb->buf += acb->n_sectors * SECTOR_SIZE;

    block_index = acb->sector_num / s->block_sectors;
    sector_in_block = acb->sector_num % s->block_sectors;
    n_sectors = s->block_sectors - sector_in_block;
    if (n_sectors > acb->nb_sectors) {
        n_sectors = acb->nb_sectors;
    }

    logout("will read %u sectors starting at sector %" PRIu64 "\n",
           n_sectors, acb->sector_num);

    /* prepare next AIO request */
    acb->n_sectors = n_sectors;
    bmap_entry = le32_to_cpu(s->bmap[block_index]);
    if (bmap_entry == VDI_UNALLOCATED) {
        /* Block not allocated, return zeros, no need to wait. */
        memset(acb->buf, 0, n_sectors * SECTOR_SIZE);
        ret = vdi_schedule_bh(vdi_aio_read_bh, acb);
        if (ret < 0) {
            goto done;
        }
    } else {
        uint64_t offset = s->header.offset_data / SECTOR_SIZE +
                          (uint64_t)bmap_entry * s->block_sectors +
                          sector_in_block;
        acb->hd_iov.iov_base = (void *)acb->buf;
        acb->hd_iov.iov_len = n_sectors * SECTOR_SIZE;
        qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
        acb->hd_aiocb = bdrv_aio_readv(bs->file, offset, &acb->hd_qiov,
                                       n_sectors, vdi_aio_read_cb, acb);
        if (acb->hd_aiocb == NULL) {
            ret = -EIO;
            goto done;
        }
    }
    return;
done:
    if (acb->qiov->niov > 1) {
        qemu_iovec_from_buffer(acb->qiov, acb->orig_buf, acb->qiov->size);
        qemu_vfree(acb->orig_buf);
    }
    acb->common.cb(acb->common.opaque, ret);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *vdi_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    VdiAIOCB *acb;
    logout("\n");
    acb = vdi_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, 0);
    if (!acb) {
        return NULL;
    }
    vdi_aio_read_cb(acb, 0);
    return &acb->common;
}

static void vdi_aio_write_cb(void *opaque, int ret)
{
    VdiAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVVdiState *s = bs->opaque;
    uint32_t bmap_entry;
    uint32_t block_index;
    uint32_t sector_in_block;
    uint32_t n_sectors;

    acb->hd_aiocb = NULL;

    if (ret < 0) {
        goto done;
    }

    acb->nb_sectors -= acb->n_sectors;
    acb->sector_num += acb->n_sectors;
    acb->buf += acb->n_sectors * SECTOR_SIZE;

    if (acb->nb_sectors == 0) {
        logout("finished data write\n");
        acb->n_sectors = 0;
        if (acb->header_modified) {
            VdiHeader *header = acb->block_buffer;
            logout("now writing modified header\n");
            assert(acb->bmap_first != VDI_UNALLOCATED);
            *header = s->header;
            vdi_header_to_le(header);
            acb->header_modified = 0;
            acb->hd_iov.iov_base = acb->block_buffer;
            acb->hd_iov.iov_len = SECTOR_SIZE;
            qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
            acb->hd_aiocb = bdrv_aio_writev(bs->file, 0, &acb->hd_qiov, 1,
                                            vdi_aio_write_cb, acb);
            if (acb->hd_aiocb == NULL) {
                ret = -EIO;
                goto done;
            }
            return;
        } else if (acb->bmap_first != VDI_UNALLOCATED) {
            /* One or more new blocks were allocated. */
            uint64_t offset;
            uint32_t bmap_first;
            uint32_t bmap_last;
            qemu_free(acb->block_buffer);
            acb->block_buffer = NULL;
            bmap_first = acb->bmap_first;
            bmap_last = acb->bmap_last;
            logout("now writing modified block map entry %u...%u\n",
                   bmap_first, bmap_last);
            /* Write modified sectors from block map. */
            bmap_first /= (SECTOR_SIZE / sizeof(uint32_t));
            bmap_last /= (SECTOR_SIZE / sizeof(uint32_t));
            n_sectors = bmap_last - bmap_first + 1;
            offset = s->bmap_sector + bmap_first;
            acb->bmap_first = VDI_UNALLOCATED;
            acb->hd_iov.iov_base = (void *)((uint8_t *)&s->bmap[0] +
                                            bmap_first * SECTOR_SIZE);
            acb->hd_iov.iov_len = n_sectors * SECTOR_SIZE;
            qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
            logout("will write %u block map sectors starting from entry %u\n",
                   n_sectors, bmap_first);
            acb->hd_aiocb = bdrv_aio_writev(bs->file, offset, &acb->hd_qiov,
                                            n_sectors, vdi_aio_write_cb, acb);
            if (acb->hd_aiocb == NULL) {
                ret = -EIO;
                goto done;
            }
            return;
        }
        ret = 0;
        goto done;
    }

    logout("%u sectors written\n", acb->n_sectors);

    block_index = acb->sector_num / s->block_sectors;
    sector_in_block = acb->sector_num % s->block_sectors;
    n_sectors = s->block_sectors - sector_in_block;
    if (n_sectors > acb->nb_sectors) {
        n_sectors = acb->nb_sectors;
    }

    logout("will write %u sectors starting at sector %" PRIu64 "\n",
           n_sectors, acb->sector_num);

    /* prepare next AIO request */
    acb->n_sectors = n_sectors;
    bmap_entry = le32_to_cpu(s->bmap[block_index]);
    if (bmap_entry == VDI_UNALLOCATED) {
        /* Allocate new block and write to it. */
        uint64_t offset;
        uint8_t *block;
        bmap_entry = s->header.blocks_allocated;
        s->bmap[block_index] = cpu_to_le32(bmap_entry);
        s->header.blocks_allocated++;
        offset = s->header.offset_data / SECTOR_SIZE +
                 (uint64_t)bmap_entry * s->block_sectors;
        block = acb->block_buffer;
        if (block == NULL) {
            block = qemu_mallocz(s->block_size);
            acb->block_buffer = block;
            acb->bmap_first = block_index;
            assert(!acb->header_modified);
            acb->header_modified = 1;
        }
        acb->bmap_last = block_index;
        memcpy(block + sector_in_block * SECTOR_SIZE,
               acb->buf, n_sectors * SECTOR_SIZE);
        acb->hd_iov.iov_base = (void *)block;
        acb->hd_iov.iov_len = s->block_size;
        qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
        acb->hd_aiocb = bdrv_aio_writev(bs->file, offset,
                                        &acb->hd_qiov, s->block_sectors,
                                        vdi_aio_write_cb, acb);
        if (acb->hd_aiocb == NULL) {
            ret = -EIO;
            goto done;
        }
    } else {
        uint64_t offset = s->header.offset_data / SECTOR_SIZE +
                          (uint64_t)bmap_entry * s->block_sectors +
                          sector_in_block;
        acb->hd_iov.iov_base = (void *)acb->buf;
        acb->hd_iov.iov_len = n_sectors * SECTOR_SIZE;
        qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
        acb->hd_aiocb = bdrv_aio_writev(bs->file, offset, &acb->hd_qiov,
                                        n_sectors, vdi_aio_write_cb, acb);
        if (acb->hd_aiocb == NULL) {
            ret = -EIO;
            goto done;
        }
    }

    return;

done:
    if (acb->qiov->niov > 1) {
        qemu_vfree(acb->orig_buf);
    }
    acb->common.cb(acb->common.opaque, ret);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *vdi_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    VdiAIOCB *acb;
    logout("\n");
    acb = vdi_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, 1);
    if (!acb) {
        return NULL;
    }
    vdi_aio_write_cb(acb, 0);
    return &acb->common;
}

static int vdi_create(const char *filename, QEMUOptionParameter *options)
{
    int fd;
    int result = 0;
    uint64_t bytes = 0;
    uint32_t blocks;
    size_t block_size = 1 * MiB;
    uint32_t image_type = VDI_TYPE_DYNAMIC;
    VdiHeader header;
    size_t i;
    size_t bmap_size;
    uint32_t *bmap;

    logout("\n");

    /* Read out options. */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            bytes = options->value.n;
#if defined(CONFIG_VDI_BLOCK_SIZE)
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                /* TODO: Additional checks (SECTOR_SIZE * 2^n, ...). */
                block_size = options->value.n;
            }
#endif
#if defined(CONFIG_VDI_STATIC_IMAGE)
        } else if (!strcmp(options->name, BLOCK_OPT_STATIC)) {
            if (options->value.n) {
                image_type = VDI_TYPE_STATIC;
            }
#endif
        }
        options++;
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
              0644);
    if (fd < 0) {
        return -errno;
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
    if (write(fd, &header, sizeof(header)) < 0) {
        result = -errno;
    }

    bmap = NULL;
    if (bmap_size > 0) {
        bmap = (uint32_t *)qemu_mallocz(bmap_size);
    }
    for (i = 0; i < blocks; i++) {
        if (image_type == VDI_TYPE_STATIC) {
            bmap[i] = i;
        } else {
            bmap[i] = VDI_UNALLOCATED;
        }
    }
    if (write(fd, bmap, bmap_size) < 0) {
        result = -errno;
    }
    qemu_free(bmap);
    if (image_type == VDI_TYPE_STATIC) {
        if (ftruncate(fd, sizeof(header) + bmap_size + blocks * block_size)) {
            result = -errno;
        }
    }

    if (close(fd) < 0) {
        result = -errno;
    }

    return result;
}

static void vdi_close(BlockDriverState *bs)
{
}

static int vdi_flush(BlockDriverState *bs)
{
    logout("\n");
    return bdrv_flush(bs->file);
}


static QEMUOptionParameter vdi_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
#if defined(CONFIG_VDI_BLOCK_SIZE)
    {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "VDI cluster (block) size"
    },
#endif
#if defined(CONFIG_VDI_STATIC_IMAGE)
    {
        .name = BLOCK_OPT_STATIC,
        .type = OPT_FLAG,
        .help = "VDI static (pre-allocated) image"
    },
#endif
    /* TODO: An additional option to set UUID values might be useful. */
    { NULL }
};

static BlockDriver bdrv_vdi = {
    .format_name = "vdi",
    .instance_size = sizeof(BDRVVdiState),
    .bdrv_probe = vdi_probe,
    .bdrv_open = vdi_open,
    .bdrv_close = vdi_close,
    .bdrv_create = vdi_create,
    .bdrv_flush = vdi_flush,
    .bdrv_is_allocated = vdi_is_allocated,
    .bdrv_make_empty = vdi_make_empty,

    .bdrv_aio_readv = vdi_aio_readv,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_aio_writev = vdi_aio_writev,
#endif

    .bdrv_get_info = vdi_get_info,

    .create_options = vdi_create_options,
    .bdrv_check = vdi_check,
};

static void bdrv_vdi_init(void)
{
    logout("\n");
    bdrv_register(&bdrv_vdi);
}

block_init(bdrv_vdi_init);
