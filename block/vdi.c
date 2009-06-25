/*
 * Block driver for the VDI format
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
 */

#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

/* Support experimental write operations on VDI images. */
#define CONFIG_VDI_WRITE

/* Support snapshot images. */
//~ #define CONFIG_VDI_SNAPSHOT

/* Enable (currently) unsupported features. */
//~ #define CONFIG_VDI_UNSUPPORTED

/* Support non-standard cluster (block) size. */
//~ #define CONFIG_VDI_CLUSTER_SIZE

#define KiB     1024
#define MiB     (KiB * KiB)

int n = 0;
#define RAISE() assert(n)

#define logout(fmt, ...) \
    fprintf(stderr, "vdi\t%-24s" fmt, __func__, ##__VA_ARGS__)

#define SECTOR_SIZE 512

#define VDI_VERSION_1_1 0x00010001

typedef char uuid_t[16];

typedef struct {
    char text[0x40];
    uint32_t signature;         /* unused here */
    uint32_t version;
    uint32_t header_size;
    uint32_t image_type;
    uint32_t image_flags;
    char image_description[256];
    uint32_t offset_blockmap;
    uint32_t offset_data;
    uint32_t cylinders;         /* unused here */
    uint32_t heads;             /* unused here */
    uint32_t sectors;           /* unused here */
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

typedef struct BDRVVdiState {
    BlockDriverState *hd;
    uint32_t *blockmap;
    uint32_t sectors_per_block;
    unsigned cluster_size;
    VdiHeader header;
} BDRVVdiState;

static const char magic_sun_vdi[] =  "<<< Sun xVM VirtualBox Disk Image >>>\n";
static const char magic_inno_vdi[] = "<<< innotek VirtualBox Disk Image >>>\n";
static const char magic_qemu_vdi[] = "<<< QEMU VM VirtualBox Disk Image >>>\n";

static int vdi_check(BlockDriverState *bs)
{
    logout("\n");
    RAISE();
    return -ENOTSUP;
}

static int vdi_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVVdiState *s = (BDRVVdiState *)bs->opaque;
    logout("\n");
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = -1;
    RAISE();
    return -ENOTSUP;
}

static int vdi_make_empty(BlockDriverState *bs)
{
    logout("\n");
    RAISE();
    return -ENOTSUP;
}

static int vdi_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const VdiHeader *header = (const VdiHeader *)buf;
    const size_t length = sizeof(header->text);
    int result = 0;

    logout("\n");

    if (buf_size < sizeof(*header)) {
    } else if (strncmp(header->text, magic_qemu_vdi, length) == 0) {
        result = 100;
    } else if (strncmp(header->text, magic_inno_vdi, length) == 0) {
        result = 100;
    } else if (strncmp(header->text, magic_sun_vdi, length) == 0) {
        result = 100;
    }

    return result;
}

#if defined(CONFIG_VDI_SNAPSHOT)
static int vdi_snapshot_create(const char *filename, const char *backing_file)
{
    RAISE();
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
     * TODO: This was copied from qcow2.c, maybe it is true for vdi, too.
     */
    if ((flags & BDRV_O_CACHE_DEF)) {
        flags |= BDRV_O_CACHE_WB;
        flags &= ~BDRV_O_CACHE_DEF;
    }

    ret = bdrv_file_open(&s->hd, filename, flags);
    if (ret < 0) {
        return ret;
    }

    if (bdrv_pread(s->hd, 0, &header, sizeof(header)) != sizeof(header)) {
        goto fail;
    }

    le32_to_cpus(&header.signature);
    le32_to_cpus(&header.version);
    le32_to_cpus(&header.header_size);
    le32_to_cpus(&header.image_type);
    le32_to_cpus(&header.image_flags);
    le32_to_cpus(&header.offset_blockmap);
    le32_to_cpus(&header.offset_data);
    le32_to_cpus(&header.cylinders);
    le32_to_cpus(&header.heads);
    le32_to_cpus(&header.sectors);
    le32_to_cpus(&header.sector_size);
    le64_to_cpus(&header.disk_size);
    le32_to_cpus(&header.block_size);
    le32_to_cpus(&header.block_extra);
    le32_to_cpus(&header.blocks_in_image);
    le32_to_cpus(&header.blocks_allocated);

    logout("%s", header.text);
    logout("image type  0x%04x\n", header.image_type);
    logout("image flags 0x%04x\n", header.image_flags);
    logout("image size  0x%" PRIx64 " B (%" PRIu64 " MiB)\n",
           header.disk_size, header.disk_size / MiB);
    logout("header size 0x%04x\n", header.header_size);
    logout("blocks tot. 0x%04x\n", header.blocks_in_image);
    logout("blocks all. 0x%04x\n", header.blocks_allocated);

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
    if (bdrv_pread(s->hd, header.offset_blockmap, s->blockmap, blockmap_size) != blockmap_size) {
        goto fail_free_blockmap;
    }

    s->sectors_per_block = (header.block_size / SECTOR_SIZE);
    s->header = header;

    return 0;

 fail_free_blockmap:
    qemu_free(s->blockmap);

 fail:
    bdrv_delete(s->hd);
    return -1;
}

#if 0
static int get_whole_cluster(BlockDriverState *bs, uint64_t cluster_offset,
                             uint64_t offset, int allocate)
{
#if 1
    RAISE();
    return -1;
#else
    uint64_t parent_cluster_offset;
    BDRVVdiState *s = bs->opaque;
    uint8_t  whole_grain[s->cluster_sectors*512];        // 128 sectors * 512 bytes each = grain size 64KB

    // we will be here if it's first write on non-exist grain(cluster).
    // try to read from parent image, if exist
    if (s->hd->backing_hd) {
        BDRVVdiState *ps = s->hd->backing_hd->opaque;

        if (!vdi_is_cid_valid(bs))
            return -1;

        parent_cluster_offset = get_cluster_offset(s->hd->backing_hd, NULL, offset, allocate);

        if (parent_cluster_offset) {
            BDRVVdiState *act_s = activeBDRV.hd->opaque;

            if (bdrv_pread(ps->hd, parent_cluster_offset, whole_grain, ps->cluster_sectors*512) != ps->cluster_sectors*512)
                return -1;

            //Write grain only into the active image
            if (bdrv_pwrite(act_s->hd, activeBDRV.cluster_offset << 9, whole_grain, sizeof(whole_grain)) != sizeof(whole_grain))
                return -1;
        }
    }
    return 0;
#endif
}
#endif

static int vdi_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
#if 1
    logout("\n");
    RAISE();
    return 0;
#else
    BDRVVdiState *s = bs->opaque;
    int index_in_cluster, n;
    uint64_t cluster_offset;

    cluster_offset = get_cluster_offset(bs, NULL, sector_num << 9, 0);
    index_in_cluster = sector_num % s->cluster_sectors;
    n = s->cluster_sectors - index_in_cluster;
    if (n > nb_sectors)
        n = nb_sectors;
    *pnum = n;
    return (cluster_offset != 0);
#endif
}

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
        size_t block_index = sector_num / s->sectors_per_block;
        size_t block_offset = sector_num % s->sectors_per_block;
        size_t n_sectors;
        size_t n_bytes;
        uint32_t blockmap_entry;
        n_sectors = s->sectors_per_block - block_offset;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }
        n_bytes = n_sectors * SECTOR_SIZE;
        blockmap_entry = s->blockmap[block_index];
        if (blockmap_entry == UINT32_MAX) {
            memset(buf, 0, n_bytes);
        } else {
            uint64_t offset = (uint64_t)s->header.offset_data +
                (uint64_t)blockmap_entry * s->header.block_size +
                block_offset * SECTOR_SIZE;
            if (bdrv_pread(s->hd, offset, buf, n_bytes) != n_bytes) {
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
        size_t block_index = sector_num / s->sectors_per_block;
        size_t block_offset = sector_num % s->sectors_per_block;
        size_t n_sectors;
        size_t n_bytes;
        uint32_t blockmap_entry;
        uint64_t offset;
        n_sectors = s->sectors_per_block - block_offset;
        if (n_sectors > nb_sectors) {
            n_sectors = nb_sectors;
        }
        n_bytes = n_sectors * SECTOR_SIZE;
        blockmap_entry = s->blockmap[block_index];
        if (blockmap_entry == UINT32_MAX) {
            /* Allocate new block and write to it. */
            uint8_t *block;
            blockmap_entry =
            s->blockmap[block_index] = s->header.blocks_allocated;
            s->header.blocks_allocated++;
            offset = (uint64_t)s->header.offset_data +
                (uint64_t)blockmap_entry * s->header.block_size;
            block = qemu_mallocz(s->header.block_size);
            memcpy(block + block_offset * SECTOR_SIZE, buf, n_bytes);
            n_bytes = s->header.block_size;
            if (bdrv_pwrite(s->hd, offset, block, n_bytes) != n_bytes) {
                qemu_free(block);
                logout("write error\n");
                return -1;
            }
            qemu_free(block);
            /* Write modified sector from block map. */
            blockmap_entry &= ~(SECTOR_SIZE / sizeof(uint32_t) - 1);
            offset = (s->header.offset_blockmap +
                      blockmap_entry * sizeof(uint32_t));
            if (bdrv_pwrite(s->hd, offset,
                            &s->blockmap[blockmap_entry],
                            SECTOR_SIZE) != SECTOR_SIZE) {
                logout("write error\n");
                return -1;
            }
        } else {
            /* Write to existing block. */
            offset = (uint64_t)s->header.offset_data +
                (uint64_t)blockmap_entry * s->header.block_size +
                block_offset * SECTOR_SIZE;
            if (bdrv_pwrite(s->hd, offset, buf, n_bytes) != n_bytes) {
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
#endif

static int vdi_create(const char *filename, QEMUOptionParameter *options)
{
#if 1
    int fd;
    uint64_t sectors = 0;
    int flags = 0;
    size_t cluster_size = 1 * MiB;
    VdiHeader header;

    logout("\n");

    /* Read out options. */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            sectors = options->value.n / 512;
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                cluster_size = options->value.n;
            }
        }
        options++;
    }

    memset(&header, 0, sizeof(header);

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
              0644);
    if (fd < 0) {
        return -1;
    }

    write(fd, &header, sizeof(header));
    close(fd);
    RAISE();
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
        .help = "vdi cluster size"
    },
#endif
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

#if defined(CONFIG_VDI_UNSUPPORTED)
    .bdrv_aio_readv     = vdi_aio_readv,
    .bdrv_aio_writev    = vdi_aio_writev,
    .bdrv_write_compressed = vdi_write_compressed,
#endif

    .bdrv_read          = vdi_read,
#if defined(CONFIG_VDI_WRITE)
    .bdrv_write         = vdi_write,
#endif

#if defined(CONFIG_VDI_SNAPSHOT)
    .bdrv_snapshot_create   = vdi_snapshot_create,
    .bdrv_snapshot_goto     = vdi_snapshot_goto,
    .bdrv_snapshot_delete   = vdi_snapshot_delete,
    .bdrv_snapshot_list     = vdi_snapshot_list,
#endif
    .bdrv_get_info      = vdi_get_info,

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
