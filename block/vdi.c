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
 * Some features like snapshots are still missing (see TODO in code).
 * Deallocation of zero-filled clusters is missing, too
 * (might be added to common block layer).
 * Asynchronous read / write support could be added, too.
 */

#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

#if defined(HAVE_UUID_H)
#include <uuid/uuid.h>
#endif

/* Enable debug messages. */
#define CONFIG_VDI_DEBUG

/* Support experimental write operations on VDI images. */
#define CONFIG_VDI_WRITE

/* Support snapshot images. */
//~ #define CONFIG_VDI_SNAPSHOT

/* Enable (currently) unsupported features. */
//~ #define CONFIG_VDI_UNSUPPORTED

/* Support non-standard cluster (block) size. */
//~ #define CONFIG_VDI_CLUSTER_SIZE

#define BLOCK_OPT_FIXED_SIZE "fixed_size"

#define KiB     1024
#define MiB     (KiB * KiB)

#if defined(CONFIG_VDI_DEBUG)
#define logout(fmt, ...) \
                fprintf(stderr, "vdi\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

#define SECTOR_SIZE 512

/* Image signature. */
#define VDI_SIGNATURE 0xbeda107f

/* Image version. */
#define VDI_VERSION_1_1 0x00010001

/* Image type. */
#define VDI_TYPE_DYNAMIC 1
#define VDI_TYPE_FIXED  2

/* Innotek / SUN images use these strings in header.text:
 * "<<< innotek VirtualBox Disk Image >>>\n"
 * "<<< Sun xVM VirtualBox Disk Image >>>\n"
 * "<<< Sun VirtualBox Disk Image >>>\n"
 * The value does not matter, so QEMU created images use a different text.
 */
#define VDI_TEXT "<<< QEMU VM Virtual Disk Image >>>\n"

/* Unallocated blocks use this index (no need to convert endianess). */
#define VDI_UNALLOCATED UINT32_MAX

#if !defined(HAVE_UUID_H)
typedef unsigned char uuid_t[16];
#endif

typedef struct {
    BlockDriverAIOCB common;
    int64_t sector_num;
    QEMUIOVector *qiov;
    uint8_t *buf;
    void *orig_buf;
    /* Total number of sectors. */
    int nb_sectors;
    /* Number of sectors for current AIO. */
    int n_sectors;
    uint64_t offset;
    uint8_t *cluster_data;
    BlockDriverAIOCB *hd_aiocb;
    struct iovec hd_iov;
    QEMUIOVector hd_qiov;
    QEMUBH *bh;
    //~ QCowL2Meta l2meta;
} VdiAIOCB;

typedef struct {
    char text[0x40];
    uint32_t signature;
    uint32_t version;
    uint32_t header_size;
    uint32_t image_type;
    uint32_t image_flags;
    char description[256];
    uint32_t offset_blockmap;
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
    BlockDriverState *hd;
    /* The blockmap entries are little endian (even in memory). */
    uint32_t *blockmap;
    /* Size of cluster (bytes). */
    uint32_t cluster_size;
    /* Size of cluster (sectors). */
    uint32_t cluster_sectors;
    VdiHeader header;
} BDRVVdiState;

static void vdi_header_to_cpu(VdiHeader *header)
{
    le32_to_cpus(&header->signature);
    le32_to_cpus(&header->version);
    le32_to_cpus(&header->header_size);
    le32_to_cpus(&header->image_type);
    le32_to_cpus(&header->image_flags);
    le32_to_cpus(&header->offset_blockmap);
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
}

static void vdi_header_to_le(VdiHeader *header)
{
    cpu_to_le32s(&header->signature);
    cpu_to_le32s(&header->version);
    cpu_to_le32s(&header->header_size);
    cpu_to_le32s(&header->image_type);
    cpu_to_le32s(&header->image_flags);
    cpu_to_le32s(&header->offset_blockmap);
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
}

#if defined(CONFIG_VDI_DEBUG)
static void vdi_header_print(VdiHeader *header)
{
    logout("text        %s", header->text);
    logout("signature   0x%04x\n", header->signature);
    logout("header size 0x%04x\n", header->header_size);
    logout("image type  0x%04x\n", header->image_type);
    logout("image flags 0x%04x\n", header->image_flags);
    logout("description %s\n", header->description);
    logout("offset bmap 0x%04x\n", header->offset_blockmap);
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
}
#endif

static int vdi_check(BlockDriverState *bs)
{
    /* TODO: additional checks possible. */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    int n_errors = 0;
    uint32_t blocks_allocated = 0;
    uint32_t block;
    logout("\n");

    /* Check blockmap and value of blocks_allocated. */
    for (block = 0; block < s->header.blocks_in_image; block++) {
        uint32_t blockmap_entry = le32_to_cpu(s->blockmap[block]);
        if (blockmap_entry != VDI_UNALLOCATED) {
            if (blockmap_entry < s->header.blocks_in_image) {
                blocks_allocated++;
            } else {
                fprintf(stderr, "ERROR: block index %" PRIu32
                        " too large, is %" PRIu32 "\n",
                        block, blockmap_entry);
                n_errors++;
            }
        }
    }
    if (blocks_allocated != s->header.blocks_allocated) {
        fprintf(stderr, "ERROR: allocated blocks mismatch, is %" PRIu32
               ", should be %" PRIu32 "\n",
               blocks_allocated, s->header.blocks_allocated);
        n_errors++;
    }

    return n_errors;
}

#if defined(CONFIG_VDI_SNAPSHOT)
static int vdi_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    /* TODO: vdi_get_info would be needed for snapshots.
       vm_state_offset is still missing. */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    logout("\n");
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = -1;
    return -ENOTSUP;
}
#endif

static int vdi_make_empty(BlockDriverState *bs)
{
    /* TODO: missing code. */
    logout("\n");
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

#if defined(CONFIG_VDI_SNAPSHOT)
static int vdi_snapshot_create(const char *filename, const char *backing_file)
{
    /* TODO: missing code. */
    logout("\n");
    return -1;
}
#endif

static int vdi_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVVdiState *s = bs->opaque;
    VdiHeader header;
    size_t blockmap_size;
    int ret;

    logout("\n");

    /* Performance is terrible right now with cache=writethrough due mainly
     * to reference count updates.  If the user does not explicitly specify
     * a caching type, force to writeback caching.
     * TODO: This is a copy from qcow2.c. Remove it when it is no longer needed.
     */
    if ((flags & BDRV_O_CACHE_DEF)) {
        flags |= BDRV_O_CACHE_WB;
        flags &= ~BDRV_O_CACHE_DEF;
    }

    ret = bdrv_file_open(&s->hd, filename, flags);
    if (ret < 0) {
        return ret;
    }

    if (bdrv_read(s->hd, 0, (uint8_t *)&header, 1) < 0) {
        goto fail;
    }

    vdi_header_to_cpu(&header);
#if defined(CONFIG_VDI_DEBUG)
    vdi_header_print(&header);
#endif

    if (header.version != VDI_VERSION_1_1) {
        logout("unsupported version %u.%u\n",
               header.version >> 16, header.version & 0xffff);
        goto fail;
    } else if (header.offset_blockmap % SECTOR_SIZE != 0) {
        /* We only support blockmaps which start on a sector boundary. */
        logout("unsupported blockmap offset 0x%x B\n", header.offset_blockmap);
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
    } else if (header.disk_size !=
               (uint64_t)header.blocks_in_image * header.block_size) {
        logout("unexpected block number %u B\n", header.blocks_in_image);
        goto fail;
    }

    bs->total_sectors = header.disk_size / SECTOR_SIZE;

    blockmap_size = header.blocks_in_image * sizeof(uint32_t);
    s->blockmap = qemu_malloc(blockmap_size);
    if (bdrv_read(s->hd, header.offset_blockmap / SECTOR_SIZE,
                  (uint8_t *)s->blockmap, blockmap_size / SECTOR_SIZE) < 0) {
        goto fail_free_blockmap;
    }

    /* Blocks (VDI documentation) correspond to clusters (QEMU). */
    s->cluster_size = header.block_size;
    s->cluster_sectors = (header.block_size / SECTOR_SIZE);
    s->header = header;

    return 0;

 fail_free_blockmap:
    qemu_free(s->blockmap);

 fail:
    bdrv_delete(s->hd);
    return -1;
}

static int vdi_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
    /* TODO: Check for too large sector_num (in bdrv_is_allocated or here). */
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    size_t blockmap_index = sector_num / s->cluster_sectors;
    size_t sector_in_cluster = sector_num % s->cluster_sectors;
    int n_sectors = s->cluster_sectors - sector_in_cluster;
    uint32_t blockmap_entry = le32_to_cpu(s->blockmap[blockmap_index]);
    logout("%p, %" PRId64 ", %d, %p\n", bs, sector_num, nb_sectors, pnum);
    if (n_sectors > nb_sectors) {
        n_sectors = nb_sectors;
    }
    *pnum = n_sectors;
    return blockmap_entry != VDI_UNALLOCATED;
}

#if defined(CONFIG_AIO)

#if 0
static void vdi_aio_remove(VdiAIOCB *acb)
{
    logout("\n");
#if 0
    VdiAIOCB **pacb;

    /* remove the callback from the queue */
    pacb = &posix_aio_state->first_aio;
    for(;;) {
        if (*pacb == NULL) {
            fprintf(stderr, "vdi_aio_remove: aio request not found!\n");
            break;
        } else if (*pacb == acb) {
            *pacb = acb->next;
            qemu_aio_release(acb);
            break;
        }
        pacb = &(*pacb)->next;
    }
#endif
}
#endif

static void vdi_aio_cancel(BlockDriverAIOCB *blockacb)
{
    logout("\n");

#if 0
    int ret;
    VdiAIOCB *acb = (VdiAIOCB *)blockacb;

    ret = qemu_paio_cancel(acb->aiocb.aio_fildes, &acb->aiocb);
    if (ret == QEMU_PAIO_NOTCANCELED) {
        /* fail safe: if the aio could not be canceled, we wait for
           it */
        while (qemu_paio_error(&acb->aiocb) == EINPROGRESS);
    }

    vdi_aio_remove(acb);
#endif
}

static AIOPool vdi_aio_pool = {
    .aiocb_size         = sizeof(VdiAIOCB),
    .cancel             = vdi_aio_cancel,
};

static VdiAIOCB *vdi_aio_setup(BlockDriverState *bs, int64_t sector_num,
        QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int is_write)
{
    //~ BDRVVdiState *s = bs->opaque;
    VdiAIOCB *acb;

    logout("\n");

    acb = qemu_aio_get(&vdi_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }

    acb->hd_aiocb = NULL;
    acb->sector_num = sector_num;
    acb->qiov = qiov;
    if (qiov->niov > 1) {
        acb->buf = acb->orig_buf = qemu_blockalign(bs, qiov->size);
        if (is_write) {
            qemu_iovec_to_buffer(qiov, acb->buf);
        }
    } else {
        acb->buf = (uint8_t *)qiov->iov->iov_base;
    }
    acb->nb_sectors = nb_sectors;
    acb->n_sectors = 0;
    acb->offset = 0;
    return acb;
}

static int vdi_schedule_bh(QEMUBHFunc *cb, VdiAIOCB *acb)
{
    logout("\n");

    if (acb->bh) {
        return -EIO;
    }

    acb->bh = qemu_bh_new(cb, acb);
    if (!acb->bh)
        return -EIO;

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

#if 0
    while (nb_sectors > 0 && sector_num < bs->total_sectors) {
        size_t n_bytes;
        uint32_t blockmap_entry;
        size_t block_index = sector_num / s->cluster_sectors;
        size_t sector_in_cluster = sector_num % s->cluster_sectors;
        size_t n_sectors = s->cluster_sectors - sector_in_cluster;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }
        n_bytes = n_sectors * SECTOR_SIZE;
        blockmap_entry = le32_to_cpu(s->blockmap[block_index]);
        if (blockmap_entry == VDI_UNALLOCATED) {
            /* Cluster not allocated, return zeros. */
            memset(buf, 0, n_bytes);
        } else {
            uint64_t offset = ((uint64_t)s->header.offset_data / SECTOR_SIZE +
                (uint64_t)blockmap_entry * s->cluster_size) / SECTOR_SIZE +
                sector_in_cluster;
            if (bdrv_read(s->hd, offset, buf, n_sectors) < 0) {
                logout("read error\n");
                return -1;
            }
        }
        buf += n_bytes;
        sector_num += n_sectors;
        nb_sectors -= n_sectors;
    }
#endif

static void vdi_aio_read_cb(void *opaque, int ret)
{
    VdiAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVVdiState *s = bs->opaque;
    size_t n_bytes;

    logout("\n");
    acb->hd_aiocb = NULL;
    if (ret < 0) {
        goto done;
    }

    n_bytes = acb->n_sectors * SECTOR_SIZE;
    //~ size_t block_index = acb->sector_num / s->cluster_sectors;
    size_t sector_in_cluster = acb->sector_num % s->cluster_sectors;
    size_t n_sectors = s->cluster_sectors - sector_in_cluster;
    if (n_sectors > acb->nb_sectors) {
        n_sectors = acb->nb_sectors;
    }

    acb->nb_sectors -= acb->n_sectors;
    acb->sector_num += acb->n_sectors;
    acb->buf += n_bytes;

    if (acb->nb_sectors == 0) {
        /* request completed */
        ret = 0;
        goto done;
    }

    /* prepare next AIO request */
    acb->n_sectors = acb->nb_sectors;
    //~ acb->offset =
        //~ vdi_get_cluster_offset(bs, acb->sector_num << 9, &acb->n);



    if (!acb->offset) {
        /* Note: in this case, no need to wait */
        memset(acb->buf, 0, SECTOR_SIZE * acb->n_sectors);
        ret = vdi_schedule_bh(vdi_aio_read_bh, acb);
        if (ret < 0) {
            goto done;
        }
    } else {
        acb->hd_iov.iov_base = (void *)acb->buf;
        acb->hd_iov.iov_len = acb->n_sectors * SECTOR_SIZE;
        qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
        acb->hd_aiocb = bdrv_aio_readv(s->hd,
                                       acb->offset + sector_in_cluster,
                                       &acb->hd_qiov, acb->n_sectors,
                                       vdi_aio_read_cb, acb);
        if (acb->hd_aiocb == NULL)
            goto done;
#if 0
#endif
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
    const uint8_t *src_buf;
    int n_end;

    logout("\n");

    acb->hd_aiocb = NULL;

    if (ret < 0) {
        goto done;
    }

    acb->nb_sectors -= acb->n_sectors;
    acb->sector_num += acb->n_sectors;
    acb->buf += acb->n_sectors * SECTOR_SIZE;

    if (acb->nb_sectors == 0) {
        /* request completed */
        ret = 0;
        goto done;
    }

    size_t sector_in_cluster = acb->sector_num & (s->cluster_sectors - 1);
    n_end = sector_in_cluster + acb->nb_sectors;

    //~ acb->offset = vdi_alloc_cluster_offset(bs, acb->sector_num << 9,
                                          //~ sector_in_cluster,
                                          //~ n_end, &acb->n, &acb->l2meta);
    src_buf = acb->buf;
    acb->hd_iov.iov_base = (void *)src_buf;
    acb->hd_iov.iov_len = acb->n_sectors * SECTOR_SIZE;
    qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
    acb->hd_aiocb = bdrv_aio_writev(s->hd,
                                    acb->offset + sector_in_cluster,
                                    &acb->hd_qiov, acb->n_sectors,
                                    vdi_aio_write_cb, acb);
    if (acb->hd_aiocb == NULL) {
        goto done;
    }

    return;

done:
    if (acb->qiov->niov > 1)
        qemu_vfree(acb->orig_buf);
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

#else /* CONFIG_AIO */

static int vdi_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    logout("%p, %" PRId64 ", %p, %d\n", bs, sector_num, buf, nb_sectors);
    if (sector_num < 0) {
        logout("unsupported sector %" PRId64 "\n", sector_num);
        return -1;
    }
    while (nb_sectors > 0 && sector_num < bs->total_sectors) {
        size_t n_bytes;
        uint32_t blockmap_entry;
        size_t block_index = sector_num / s->cluster_sectors;
        size_t sector_in_cluster = sector_num % s->cluster_sectors;
        size_t n_sectors = s->cluster_sectors - sector_in_cluster;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }
        n_bytes = n_sectors * SECTOR_SIZE;
        blockmap_entry = le32_to_cpu(s->blockmap[block_index]);
        if (blockmap_entry == VDI_UNALLOCATED) {
            /* Cluster not allocated, return zeros. */
            memset(buf, 0, n_bytes);
        } else {
            uint64_t offset = ((uint64_t)s->header.offset_data / SECTOR_SIZE +
                (uint64_t)blockmap_entry * s->cluster_size) / SECTOR_SIZE +
                sector_in_cluster;
            if (bdrv_read(s->hd, offset, buf, n_sectors) < 0) {
                logout("read error\n");
                return -1;
            }
        }
        buf += n_bytes;
        sector_num += n_sectors;
        nb_sectors -= n_sectors;
    }
    return 0;
}

#if defined(CONFIG_VDI_WRITE)
static int vdi_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    logout("%p, %" PRId64 ", %p, %d\n", bs, sector_num, buf, nb_sectors);
    if (sector_num < 0) {
        logout("unsupported sector %" PRId64 "\n", sector_num);
        return -1;
    }
    while (nb_sectors > 0 && sector_num < bs->total_sectors) {
        size_t n_bytes;
        uint32_t blockmap_entry;
        uint64_t offset;
        size_t block_index = sector_num / s->cluster_sectors;
        size_t sector_in_cluster = sector_num % s->cluster_sectors;
        size_t n_sectors = s->cluster_sectors - sector_in_cluster;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }
        n_bytes = n_sectors * SECTOR_SIZE;
        blockmap_entry = le32_to_cpu(s->blockmap[block_index]);
        if (blockmap_entry == VDI_UNALLOCATED) {
            /* Allocate new cluster and write to it. */
            uint8_t *block;
            blockmap_entry =
            s->blockmap[block_index] = cpu_to_le32(s->header.blocks_allocated);
            s->header.blocks_allocated++;
            offset = s->header.offset_data / SECTOR_SIZE +
                     (uint64_t)blockmap_entry * s->cluster_sectors;
            block = qemu_mallocz(s->cluster_size);
            memcpy(block + sector_in_cluster * SECTOR_SIZE, buf, n_bytes);
            if (bdrv_write(s->hd, offset, block, s->cluster_sectors) < 0) {
                qemu_free(block);
                logout("write error\n");
                return -1;
            }
            qemu_free(block);

            /* Write modified sector from block map. */
            blockmap_entry &= ~(SECTOR_SIZE / sizeof(uint32_t) - 1);
            offset = (s->header.offset_blockmap / SECTOR_SIZE +
                      blockmap_entry / (SECTOR_SIZE / sizeof(uint32_t)));
            if (bdrv_write(s->hd, offset,
                           (const uint8_t *)&s->blockmap[blockmap_entry], 1) < 0) {
                logout("write error\n");
                return -1;
            }

            /* Write modified header (blocks_allocated). */
            vdi_header_to_le(&s->header);
            if (bdrv_write(s->hd, 0, (const uint8_t *)&s->header, 1) < 0) {
                vdi_header_to_cpu(&s->header);
                logout("write error\n");
                return -1;
            }
            vdi_header_to_cpu(&s->header);
        } else {
            /* Write to existing block. */
            offset = s->header.offset_data / SECTOR_SIZE +
                (uint64_t)blockmap_entry * s->cluster_sectors +
                sector_in_cluster;
            if (bdrv_write(s->hd, offset, buf, n_sectors) < 0) {
                logout("write error\n");
                return -1;
            }
        }
        buf += n_bytes;
        sector_num += n_sectors;
        nb_sectors -= n_sectors;
    }
    return 0;
}
#endif /* CONFIG_VDI_WRITE */

#endif /* CONFIG_AIO */

static int vdi_create(const char *filename, QEMUOptionParameter *options)
{
    /* TODO: Support pre-allocated images. */
    int fd;
    uint64_t bytes = 0;
    uint32_t clusters;
    //~ int flags = 0;
    size_t cluster_size = 1 * MiB;
    uint32_t image_type = VDI_TYPE_DYNAMIC;
    VdiHeader header;
    size_t i;
    size_t blockmap_size;
    uint32_t *blockmap;

    logout("\n");

    /* Read out options. */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            bytes = options->value.n;
#if defined(CONFIG_VDI_CLUSTER_SIZE)
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                /* TODO: Additional checks (SECTOR_SIZE * 2^n, ...). */
                cluster_size = options->value.n;
            }
#endif
        } else if (!strcmp(options->name, BLOCK_OPT_FIXED_SIZE)) {
            image_type = VDI_TYPE_FIXED;
        }
        options++;
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
              0644);
    if (fd < 0) {
        return -1;
    }

    clusters = bytes / cluster_size;
    blockmap_size = clusters * sizeof(uint32_t);
    blockmap_size = ((blockmap_size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE -1));

    memset(&header, 0, sizeof(header));
    strcpy(header.text, VDI_TEXT);
    header.signature = VDI_SIGNATURE;
    header.version = VDI_VERSION_1_1;
    header.header_size = 0x180;
    header.image_type = image_type;
    header.offset_blockmap = 0x200;
    header.offset_data = 0x200 + blockmap_size;
    header.sector_size = SECTOR_SIZE;
    header.disk_size = bytes;
    header.block_size = cluster_size;
    header.blocks_in_image = clusters;
#if defined(HAVE_UUID_H)
    uuid_generate(header.uuid_image);
    uuid_generate(header.uuid_last_snap);
#if 0
    uuid_generate(header.uuid_link);
    uuid_generate(header.uuid_parent);
#endif
#endif
#if defined(CONFIG_VDI_DEBUG)
    vdi_header_print(&header);
#endif
    vdi_header_to_le(&header);
    write(fd, &header, sizeof(header));

    blockmap = (uint32_t *)qemu_mallocz(blockmap_size);
    for (i = 0; i < clusters; i++) {
        blockmap[i] = VDI_UNALLOCATED;
    }
    write(fd, blockmap, blockmap_size);
    qemu_free(blockmap);

    close(fd);

    return 0;
}

static void vdi_close(BlockDriverState *bs)
{
    BDRVVdiState *s = bs->opaque;
    logout("\n");
    bdrv_delete(s->hd);
}

static void vdi_flush(BlockDriverState *bs)
{
    BDRVVdiState *s = bs->opaque;
    logout("\n");
    bdrv_flush(s->hd);
}


static QEMUOptionParameter vdi_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
#if defined(CONFIG_VDI_CLUSTER_SIZE)
    {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "VDI cluster size"
    },
#endif
    {
        .name = BLOCK_OPT_FIXED_SIZE,
        .type = OPT_FLAG,
        .help = "VDI fixed size (pre-allocated) image"
    },
    { NULL }
};

static BlockDriver bdrv_vdi = {
    .format_name        = "vdi",
    .instance_size      = sizeof(BDRVVdiState),
    .bdrv_probe         = vdi_probe,
    .bdrv_open          = vdi_open,
    .bdrv_close         = vdi_close,
    .bdrv_create        = vdi_create,
    .bdrv_flush         = vdi_flush,
#if defined(CONFIG_VDI_UNSUPPORTED)
    .bdrv_getlength     = vdi_getlength,
#endif
    .bdrv_is_allocated  = vdi_is_allocated,
#if defined(CONFIG_VDI_UNSUPPORTED)
    .bdrv_set_key       = vdi_set_key,
#endif
    .bdrv_make_empty    = vdi_make_empty,

#ifdef CONFIG_AIO
    .bdrv_aio_readv     = vdi_aio_readv,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_aio_writev    = vdi_aio_writev,
#endif
#else
    .bdrv_read          = vdi_read,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_write         = vdi_write,
#endif
#endif

#if defined(CONFIG_VDI_UNSUPPORTED)
    .bdrv_write_compressed = vdi_write_compressed,
#endif

#if defined(CONFIG_VDI_SNAPSHOT)
    .bdrv_snapshot_create   = vdi_snapshot_create,
    .bdrv_snapshot_goto     = vdi_snapshot_goto,
    .bdrv_snapshot_delete   = vdi_snapshot_delete,
    .bdrv_snapshot_list     = vdi_snapshot_list,
    .bdrv_get_info = vdi_get_info,
#endif

#if defined(CONFIG_VDI_UNSUPPORTED)
    .bdrv_put_buffer    = vdi_put_buffer,
    .bdrv_get_buffer    = vdi_get_buffer,
#endif

    .create_options     = vdi_create_options,
    .bdrv_check         = vdi_check,
};

static void bdrv_vdi_init(void)
{
    logout("\n");
    bdrv_register(&bdrv_vdi);
}

block_init(bdrv_vdi_init);
