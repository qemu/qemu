/*
 * Block driver for the VMDK format
 *
 * Copyright (c) 2004 Fabrice Bellard
 * Copyright (c) 2005 Filip Navara
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
#include "sysemu/block-backend.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "qemu/memalign.h"
#include "migration/blocker.h"
#include "qemu/cutils.h"
#include <zlib.h>

#define VMDK3_MAGIC (('C' << 24) | ('O' << 16) | ('W' << 8) | 'D')
#define VMDK4_MAGIC (('K' << 24) | ('D' << 16) | ('M' << 8) | 'V')
#define VMDK4_COMPRESSION_DEFLATE 1
#define VMDK4_FLAG_NL_DETECT (1 << 0)
#define VMDK4_FLAG_RGD (1 << 1)
/* Zeroed-grain enable bit */
#define VMDK4_FLAG_ZERO_GRAIN   (1 << 2)
#define VMDK4_FLAG_COMPRESS (1 << 16)
#define VMDK4_FLAG_MARKER (1 << 17)
#define VMDK4_GD_AT_END 0xffffffffffffffffULL

#define VMDK_EXTENT_MAX_SECTORS (1ULL << 32)

#define VMDK_GTE_ZEROED 0x1

/* VMDK internal error codes */
#define VMDK_OK      0
#define VMDK_ERROR   (-1)
/* Cluster not allocated */
#define VMDK_UNALLOC (-2)
#define VMDK_ZEROED  (-3)

#define BLOCK_OPT_ZEROED_GRAIN "zeroed_grain"
#define BLOCK_OPT_TOOLSVERSION "toolsversion"

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t disk_sectors;
    uint32_t granularity;
    uint32_t l1dir_offset;
    uint32_t l1dir_size;
    uint32_t file_sectors;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
} QEMU_PACKED VMDK3Header;

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint64_t capacity;
    uint64_t granularity;
    uint64_t desc_offset;
    uint64_t desc_size;
    /* Number of GrainTableEntries per GrainTable */
    uint32_t num_gtes_per_gt;
    uint64_t rgd_offset;
    uint64_t gd_offset;
    uint64_t grain_offset;
    char filler[1];
    char check_bytes[4];
    uint16_t compressAlgorithm;
} QEMU_PACKED VMDK4Header;

typedef struct VMDKSESparseConstHeader {
    uint64_t magic;
    uint64_t version;
    uint64_t capacity;
    uint64_t grain_size;
    uint64_t grain_table_size;
    uint64_t flags;
    uint64_t reserved1;
    uint64_t reserved2;
    uint64_t reserved3;
    uint64_t reserved4;
    uint64_t volatile_header_offset;
    uint64_t volatile_header_size;
    uint64_t journal_header_offset;
    uint64_t journal_header_size;
    uint64_t journal_offset;
    uint64_t journal_size;
    uint64_t grain_dir_offset;
    uint64_t grain_dir_size;
    uint64_t grain_tables_offset;
    uint64_t grain_tables_size;
    uint64_t free_bitmap_offset;
    uint64_t free_bitmap_size;
    uint64_t backmap_offset;
    uint64_t backmap_size;
    uint64_t grains_offset;
    uint64_t grains_size;
    uint8_t pad[304];
} QEMU_PACKED VMDKSESparseConstHeader;

typedef struct VMDKSESparseVolatileHeader {
    uint64_t magic;
    uint64_t free_gt_number;
    uint64_t next_txn_seq_number;
    uint64_t replay_journal;
    uint8_t pad[480];
} QEMU_PACKED VMDKSESparseVolatileHeader;

#define L2_CACHE_SIZE 16

typedef struct VmdkExtent {
    BdrvChild *file;
    bool flat;
    bool compressed;
    bool has_marker;
    bool has_zero_grain;
    bool sesparse;
    uint64_t sesparse_l2_tables_offset;
    uint64_t sesparse_clusters_offset;
    int32_t entry_size;
    int version;
    int64_t sectors;
    int64_t end_sector;
    int64_t flat_start_offset;
    int64_t l1_table_offset;
    int64_t l1_backup_table_offset;
    void *l1_table;
    uint32_t *l1_backup_table;
    unsigned int l1_size;
    uint32_t l1_entry_sectors;

    unsigned int l2_size;
    void *l2_cache;
    uint32_t l2_cache_offsets[L2_CACHE_SIZE];
    uint32_t l2_cache_counts[L2_CACHE_SIZE];

    int64_t cluster_sectors;
    int64_t next_cluster_sector;
    char *type;
} VmdkExtent;

typedef struct BDRVVmdkState {
    CoMutex lock;
    uint64_t desc_offset;
    bool cid_updated;
    bool cid_checked;
    uint32_t cid;
    uint32_t parent_cid;
    int num_extents;
    /* Extent array with num_extents entries, ascend ordered by address */
    VmdkExtent *extents;
    Error *migration_blocker;
    char *create_type;
} BDRVVmdkState;

typedef struct VmdkMetaData {
    unsigned int l1_index;
    unsigned int l2_index;
    unsigned int l2_offset;
    bool new_allocation;
    uint32_t *l2_cache_entry;
} VmdkMetaData;

typedef struct VmdkGrainMarker {
    uint64_t lba;
    uint32_t size;
    uint8_t  data[];
} QEMU_PACKED VmdkGrainMarker;

enum {
    MARKER_END_OF_STREAM    = 0,
    MARKER_GRAIN_TABLE      = 1,
    MARKER_GRAIN_DIRECTORY  = 2,
    MARKER_FOOTER           = 3,
};

static int vmdk_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    uint32_t magic;

    if (buf_size < 4) {
        return 0;
    }
    magic = be32_to_cpu(*(uint32_t *)buf);
    if (magic == VMDK3_MAGIC ||
        magic == VMDK4_MAGIC) {
        return 100;
    } else {
        const char *p = (const char *)buf;
        const char *end = p + buf_size;
        while (p < end) {
            if (*p == '#') {
                /* skip comment line */
                while (p < end && *p != '\n') {
                    p++;
                }
                p++;
                continue;
            }
            if (*p == ' ') {
                while (p < end && *p == ' ') {
                    p++;
                }
                /* skip '\r' if windows line endings used. */
                if (p < end && *p == '\r') {
                    p++;
                }
                /* only accept blank lines before 'version=' line */
                if (p == end || *p != '\n') {
                    return 0;
                }
                p++;
                continue;
            }
            if (end - p >= strlen("version=X\n")) {
                if (strncmp("version=1\n", p, strlen("version=1\n")) == 0 ||
                    strncmp("version=2\n", p, strlen("version=2\n")) == 0 ||
                    strncmp("version=3\n", p, strlen("version=3\n")) == 0) {
                    return 100;
                }
            }
            if (end - p >= strlen("version=X\r\n")) {
                if (strncmp("version=1\r\n", p, strlen("version=1\r\n")) == 0 ||
                    strncmp("version=2\r\n", p, strlen("version=2\r\n")) == 0 ||
                    strncmp("version=3\r\n", p, strlen("version=3\r\n")) == 0) {
                    return 100;
                }
            }
            return 0;
        }
        return 0;
    }
}

#define SECTOR_SIZE 512
#define DESC_SIZE (20 * SECTOR_SIZE)    /* 20 sectors of 512 bytes each */
#define BUF_SIZE 4096
#define HEADER_SIZE 512                 /* first sector of 512 bytes */

static void vmdk_free_extents(BlockDriverState *bs)
{
    int i;
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *e;

    for (i = 0; i < s->num_extents; i++) {
        e = &s->extents[i];
        g_free(e->l1_table);
        g_free(e->l2_cache);
        g_free(e->l1_backup_table);
        g_free(e->type);
        if (e->file != bs->file) {
            bdrv_unref_child(bs, e->file);
        }
    }
    g_free(s->extents);
}

static void vmdk_free_last_extent(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;

    if (s->num_extents == 0) {
        return;
    }
    s->num_extents--;
    s->extents = g_renew(VmdkExtent, s->extents, s->num_extents);
}

/* Return -ve errno, or 0 on success and write CID into *pcid. */
static int vmdk_read_cid(BlockDriverState *bs, int parent, uint32_t *pcid)
{
    char *desc;
    uint32_t cid;
    const char *p_name, *cid_str;
    size_t cid_str_size;
    BDRVVmdkState *s = bs->opaque;
    int ret;

    desc = g_malloc0(DESC_SIZE);
    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        goto out;
    }

    if (parent) {
        cid_str = "parentCID";
        cid_str_size = sizeof("parentCID");
    } else {
        cid_str = "CID";
        cid_str_size = sizeof("CID");
    }

    desc[DESC_SIZE - 1] = '\0';
    p_name = strstr(desc, cid_str);
    if (p_name == NULL) {
        ret = -EINVAL;
        goto out;
    }
    p_name += cid_str_size;
    if (sscanf(p_name, "%" SCNx32, &cid) != 1) {
        ret = -EINVAL;
        goto out;
    }
    *pcid = cid;
    ret = 0;

out:
    g_free(desc);
    return ret;
}

static int vmdk_write_cid(BlockDriverState *bs, uint32_t cid)
{
    char *desc, *tmp_desc;
    char *p_name, *tmp_str;
    BDRVVmdkState *s = bs->opaque;
    int ret = 0;

    desc = g_malloc0(DESC_SIZE);
    tmp_desc = g_malloc0(DESC_SIZE);
    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        goto out;
    }

    desc[DESC_SIZE - 1] = '\0';
    tmp_str = strstr(desc, "parentCID");
    if (tmp_str == NULL) {
        ret = -EINVAL;
        goto out;
    }

    pstrcpy(tmp_desc, DESC_SIZE, tmp_str);
    p_name = strstr(desc, "CID");
    if (p_name != NULL) {
        p_name += sizeof("CID");
        snprintf(p_name, DESC_SIZE - (p_name - desc), "%" PRIx32 "\n", cid);
        pstrcat(desc, DESC_SIZE, tmp_desc);
    }

    ret = bdrv_pwrite_sync(bs->file, s->desc_offset, desc, DESC_SIZE);

out:
    g_free(desc);
    g_free(tmp_desc);
    return ret;
}

static int vmdk_is_cid_valid(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    uint32_t cur_pcid;

    if (!s->cid_checked && bs->backing) {
        BlockDriverState *p_bs = bs->backing->bs;

        if (strcmp(p_bs->drv->format_name, "vmdk")) {
            /* Backing file is not in vmdk format, so it does not have
             * a CID, which makes the overlay's parent CID invalid */
            return 0;
        }

        if (vmdk_read_cid(p_bs, 0, &cur_pcid) != 0) {
            /* read failure: report as not valid */
            return 0;
        }
        if (s->parent_cid != cur_pcid) {
            /* CID not valid */
            return 0;
        }
    }
    s->cid_checked = true;
    /* CID valid */
    return 1;
}

/* We have nothing to do for VMDK reopen, stubs just return success */
static int vmdk_reopen_prepare(BDRVReopenState *state,
                               BlockReopenQueue *queue, Error **errp)
{
    assert(state != NULL);
    assert(state->bs != NULL);
    return 0;
}

static int vmdk_parent_open(BlockDriverState *bs)
{
    char *p_name;
    char *desc;
    BDRVVmdkState *s = bs->opaque;
    int ret;

    desc = g_malloc0(DESC_SIZE + 1);
    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        goto out;
    }
    ret = 0;

    p_name = strstr(desc, "parentFileNameHint");
    if (p_name != NULL) {
        char *end_name;

        p_name += sizeof("parentFileNameHint") + 1;
        end_name = strchr(p_name, '\"');
        if (end_name == NULL) {
            ret = -EINVAL;
            goto out;
        }
        if ((end_name - p_name) > sizeof(bs->auto_backing_file) - 1) {
            ret = -EINVAL;
            goto out;
        }

        pstrcpy(bs->auto_backing_file, end_name - p_name + 1, p_name);
        pstrcpy(bs->backing_file, sizeof(bs->backing_file),
                bs->auto_backing_file);
        pstrcpy(bs->backing_format, sizeof(bs->backing_format),
                "vmdk");
    }

out:
    g_free(desc);
    return ret;
}

/* Create and append extent to the extent array. Return the added VmdkExtent
 * address. return NULL if allocation failed. */
static int vmdk_add_extent(BlockDriverState *bs,
                           BdrvChild *file, bool flat, int64_t sectors,
                           int64_t l1_offset, int64_t l1_backup_offset,
                           uint32_t l1_size,
                           int l2_size, uint64_t cluster_sectors,
                           VmdkExtent **new_extent,
                           Error **errp)
{
    VmdkExtent *extent;
    BDRVVmdkState *s = bs->opaque;
    int64_t nb_sectors;

    if (cluster_sectors > 0x200000) {
        /* 0x200000 * 512Bytes = 1GB for one cluster is unrealistic */
        error_setg(errp, "Invalid granularity, image may be corrupt");
        return -EFBIG;
    }
    if (l1_size > 32 * 1024 * 1024) {
        /*
         * Although with big capacity and small l1_entry_sectors, we can get a
         * big l1_size, we don't want unbounded value to allocate the table.
         * Limit it to 32M, which is enough to store:
         *     8TB  - for both VMDK3 & VMDK4 with
         *            minimal cluster size: 512B
         *            minimal L2 table size: 512 entries
         *            8 TB is still more than the maximal value supported for
         *            VMDK3 & VMDK4 which is 2TB.
         *     64TB - for "ESXi seSparse Extent"
         *            minimal cluster size: 512B (default is 4KB)
         *            L2 table size: 4096 entries (const).
         *            64TB is more than the maximal value supported for
         *            seSparse VMDKs (which is slightly less than 64TB)
         */
        error_setg(errp, "L1 size too big");
        return -EFBIG;
    }

    nb_sectors = bdrv_nb_sectors(file->bs);
    if (nb_sectors < 0) {
        return nb_sectors;
    }

    s->extents = g_renew(VmdkExtent, s->extents, s->num_extents + 1);
    extent = &s->extents[s->num_extents];
    s->num_extents++;

    memset(extent, 0, sizeof(VmdkExtent));
    extent->file = file;
    extent->flat = flat;
    extent->sectors = sectors;
    extent->l1_table_offset = l1_offset;
    extent->l1_backup_table_offset = l1_backup_offset;
    extent->l1_size = l1_size;
    extent->l1_entry_sectors = l2_size * cluster_sectors;
    extent->l2_size = l2_size;
    extent->cluster_sectors = flat ? sectors : cluster_sectors;
    extent->next_cluster_sector = ROUND_UP(nb_sectors, cluster_sectors);
    extent->entry_size = sizeof(uint32_t);

    if (s->num_extents > 1) {
        extent->end_sector = (*(extent - 1)).end_sector + extent->sectors;
    } else {
        extent->end_sector = extent->sectors;
    }
    bs->total_sectors = extent->end_sector;
    if (new_extent) {
        *new_extent = extent;
    }
    return 0;
}

static int vmdk_init_tables(BlockDriverState *bs, VmdkExtent *extent,
                            Error **errp)
{
    int ret;
    size_t l1_size;
    int i;

    /* read the L1 table */
    l1_size = extent->l1_size * extent->entry_size;
    extent->l1_table = g_try_malloc(l1_size);
    if (l1_size && extent->l1_table == NULL) {
        return -ENOMEM;
    }

    ret = bdrv_pread(extent->file,
                     extent->l1_table_offset,
                     extent->l1_table,
                     l1_size);
    if (ret < 0) {
        bdrv_refresh_filename(extent->file->bs);
        error_setg_errno(errp, -ret,
                         "Could not read l1 table from extent '%s'",
                         extent->file->bs->filename);
        goto fail_l1;
    }
    for (i = 0; i < extent->l1_size; i++) {
        if (extent->entry_size == sizeof(uint64_t)) {
            le64_to_cpus((uint64_t *)extent->l1_table + i);
        } else {
            assert(extent->entry_size == sizeof(uint32_t));
            le32_to_cpus((uint32_t *)extent->l1_table + i);
        }
    }

    if (extent->l1_backup_table_offset) {
        assert(!extent->sesparse);
        extent->l1_backup_table = g_try_malloc(l1_size);
        if (l1_size && extent->l1_backup_table == NULL) {
            ret = -ENOMEM;
            goto fail_l1;
        }
        ret = bdrv_pread(extent->file,
                         extent->l1_backup_table_offset,
                         extent->l1_backup_table,
                         l1_size);
        if (ret < 0) {
            bdrv_refresh_filename(extent->file->bs);
            error_setg_errno(errp, -ret,
                             "Could not read l1 backup table from extent '%s'",
                             extent->file->bs->filename);
            goto fail_l1b;
        }
        for (i = 0; i < extent->l1_size; i++) {
            le32_to_cpus(&extent->l1_backup_table[i]);
        }
    }

    extent->l2_cache =
        g_malloc(extent->entry_size * extent->l2_size * L2_CACHE_SIZE);
    return 0;
 fail_l1b:
    g_free(extent->l1_backup_table);
 fail_l1:
    g_free(extent->l1_table);
    return ret;
}

static int vmdk_open_vmfs_sparse(BlockDriverState *bs,
                                 BdrvChild *file,
                                 int flags, Error **errp)
{
    int ret;
    uint32_t magic;
    VMDK3Header header;
    VmdkExtent *extent = NULL;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        bdrv_refresh_filename(file->bs);
        error_setg_errno(errp, -ret,
                         "Could not read header from file '%s'",
                         file->bs->filename);
        return ret;
    }
    ret = vmdk_add_extent(bs, file, false,
                          le32_to_cpu(header.disk_sectors),
                          (int64_t)le32_to_cpu(header.l1dir_offset) << 9,
                          0,
                          le32_to_cpu(header.l1dir_size),
                          4096,
                          le32_to_cpu(header.granularity),
                          &extent,
                          errp);
    if (ret < 0) {
        return ret;
    }
    ret = vmdk_init_tables(bs, extent, errp);
    if (ret) {
        /* free extent allocated by vmdk_add_extent */
        vmdk_free_last_extent(bs);
    }
    return ret;
}

#define SESPARSE_CONST_HEADER_MAGIC UINT64_C(0x00000000cafebabe)
#define SESPARSE_VOLATILE_HEADER_MAGIC UINT64_C(0x00000000cafecafe)

/* Strict checks - format not officially documented */
static int check_se_sparse_const_header(VMDKSESparseConstHeader *header,
                                        Error **errp)
{
    header->magic = le64_to_cpu(header->magic);
    header->version = le64_to_cpu(header->version);
    header->grain_size = le64_to_cpu(header->grain_size);
    header->grain_table_size = le64_to_cpu(header->grain_table_size);
    header->flags = le64_to_cpu(header->flags);
    header->reserved1 = le64_to_cpu(header->reserved1);
    header->reserved2 = le64_to_cpu(header->reserved2);
    header->reserved3 = le64_to_cpu(header->reserved3);
    header->reserved4 = le64_to_cpu(header->reserved4);

    header->volatile_header_offset =
        le64_to_cpu(header->volatile_header_offset);
    header->volatile_header_size = le64_to_cpu(header->volatile_header_size);

    header->journal_header_offset = le64_to_cpu(header->journal_header_offset);
    header->journal_header_size = le64_to_cpu(header->journal_header_size);

    header->journal_offset = le64_to_cpu(header->journal_offset);
    header->journal_size = le64_to_cpu(header->journal_size);

    header->grain_dir_offset = le64_to_cpu(header->grain_dir_offset);
    header->grain_dir_size = le64_to_cpu(header->grain_dir_size);

    header->grain_tables_offset = le64_to_cpu(header->grain_tables_offset);
    header->grain_tables_size = le64_to_cpu(header->grain_tables_size);

    header->free_bitmap_offset = le64_to_cpu(header->free_bitmap_offset);
    header->free_bitmap_size = le64_to_cpu(header->free_bitmap_size);

    header->backmap_offset = le64_to_cpu(header->backmap_offset);
    header->backmap_size = le64_to_cpu(header->backmap_size);

    header->grains_offset = le64_to_cpu(header->grains_offset);
    header->grains_size = le64_to_cpu(header->grains_size);

    if (header->magic != SESPARSE_CONST_HEADER_MAGIC) {
        error_setg(errp, "Bad const header magic: 0x%016" PRIx64,
                   header->magic);
        return -EINVAL;
    }

    if (header->version != 0x0000000200000001) {
        error_setg(errp, "Unsupported version: 0x%016" PRIx64,
                   header->version);
        return -ENOTSUP;
    }

    if (header->grain_size != 8) {
        error_setg(errp, "Unsupported grain size: %" PRIu64,
                   header->grain_size);
        return -ENOTSUP;
    }

    if (header->grain_table_size != 64) {
        error_setg(errp, "Unsupported grain table size: %" PRIu64,
                   header->grain_table_size);
        return -ENOTSUP;
    }

    if (header->flags != 0) {
        error_setg(errp, "Unsupported flags: 0x%016" PRIx64,
                   header->flags);
        return -ENOTSUP;
    }

    if (header->reserved1 != 0 || header->reserved2 != 0 ||
        header->reserved3 != 0 || header->reserved4 != 0) {
        error_setg(errp, "Unsupported reserved bits:"
                   " 0x%016" PRIx64 " 0x%016" PRIx64
                   " 0x%016" PRIx64 " 0x%016" PRIx64,
                   header->reserved1, header->reserved2,
                   header->reserved3, header->reserved4);
        return -ENOTSUP;
    }

    /* check that padding is 0 */
    if (!buffer_is_zero(header->pad, sizeof(header->pad))) {
        error_setg(errp, "Unsupported non-zero const header padding");
        return -ENOTSUP;
    }

    return 0;
}

static int check_se_sparse_volatile_header(VMDKSESparseVolatileHeader *header,
                                           Error **errp)
{
    header->magic = le64_to_cpu(header->magic);
    header->free_gt_number = le64_to_cpu(header->free_gt_number);
    header->next_txn_seq_number = le64_to_cpu(header->next_txn_seq_number);
    header->replay_journal = le64_to_cpu(header->replay_journal);

    if (header->magic != SESPARSE_VOLATILE_HEADER_MAGIC) {
        error_setg(errp, "Bad volatile header magic: 0x%016" PRIx64,
                   header->magic);
        return -EINVAL;
    }

    if (header->replay_journal) {
        error_setg(errp, "Image is dirty, Replaying journal not supported");
        return -ENOTSUP;
    }

    /* check that padding is 0 */
    if (!buffer_is_zero(header->pad, sizeof(header->pad))) {
        error_setg(errp, "Unsupported non-zero volatile header padding");
        return -ENOTSUP;
    }

    return 0;
}

static int vmdk_open_se_sparse(BlockDriverState *bs,
                               BdrvChild *file,
                               int flags, Error **errp)
{
    int ret;
    VMDKSESparseConstHeader const_header;
    VMDKSESparseVolatileHeader volatile_header;
    VmdkExtent *extent = NULL;

    ret = bdrv_apply_auto_read_only(bs,
            "No write support for seSparse images available", errp);
    if (ret < 0) {
        return ret;
    }

    assert(sizeof(const_header) == SECTOR_SIZE);

    ret = bdrv_pread(file, 0, &const_header, sizeof(const_header));
    if (ret < 0) {
        bdrv_refresh_filename(file->bs);
        error_setg_errno(errp, -ret,
                         "Could not read const header from file '%s'",
                         file->bs->filename);
        return ret;
    }

    /* check const header */
    ret = check_se_sparse_const_header(&const_header, errp);
    if (ret < 0) {
        return ret;
    }

    assert(sizeof(volatile_header) == SECTOR_SIZE);

    ret = bdrv_pread(file,
                     const_header.volatile_header_offset * SECTOR_SIZE,
                     &volatile_header, sizeof(volatile_header));
    if (ret < 0) {
        bdrv_refresh_filename(file->bs);
        error_setg_errno(errp, -ret,
                         "Could not read volatile header from file '%s'",
                         file->bs->filename);
        return ret;
    }

    /* check volatile header */
    ret = check_se_sparse_volatile_header(&volatile_header, errp);
    if (ret < 0) {
        return ret;
    }

    ret = vmdk_add_extent(bs, file, false,
                          const_header.capacity,
                          const_header.grain_dir_offset * SECTOR_SIZE,
                          0,
                          const_header.grain_dir_size *
                          SECTOR_SIZE / sizeof(uint64_t),
                          const_header.grain_table_size *
                          SECTOR_SIZE / sizeof(uint64_t),
                          const_header.grain_size,
                          &extent,
                          errp);
    if (ret < 0) {
        return ret;
    }

    extent->sesparse = true;
    extent->sesparse_l2_tables_offset = const_header.grain_tables_offset;
    extent->sesparse_clusters_offset = const_header.grains_offset;
    extent->entry_size = sizeof(uint64_t);

    ret = vmdk_init_tables(bs, extent, errp);
    if (ret) {
        /* free extent allocated by vmdk_add_extent */
        vmdk_free_last_extent(bs);
    }

    return ret;
}

static int vmdk_open_desc_file(BlockDriverState *bs, int flags, char *buf,
                               QDict *options, Error **errp);

static char *vmdk_read_desc(BdrvChild *file, uint64_t desc_offset, Error **errp)
{
    int64_t size;
    char *buf;
    int ret;

    size = bdrv_getlength(file->bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "Could not access file");
        return NULL;
    }

    if (size < 4) {
        /* Both descriptor file and sparse image must be much larger than 4
         * bytes, also callers of vmdk_read_desc want to compare the first 4
         * bytes with VMDK4_MAGIC, let's error out if less is read. */
        error_setg(errp, "File is too small, not a valid image");
        return NULL;
    }

    size = MIN(size, (1 << 20) - 1);  /* avoid unbounded allocation */
    buf = g_malloc(size + 1);

    ret = bdrv_pread(file, desc_offset, buf, size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read from file");
        g_free(buf);
        return NULL;
    }
    buf[ret] = 0;

    return buf;
}

static int vmdk_open_vmdk4(BlockDriverState *bs,
                           BdrvChild *file,
                           int flags, QDict *options, Error **errp)
{
    int ret;
    uint32_t magic;
    uint32_t l1_size, l1_entry_sectors;
    VMDK4Header header;
    VmdkExtent *extent = NULL;
    BDRVVmdkState *s = bs->opaque;
    int64_t l1_backup_offset = 0;
    bool compressed;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        bdrv_refresh_filename(file->bs);
        error_setg_errno(errp, -ret,
                         "Could not read header from file '%s'",
                         file->bs->filename);
        return -EINVAL;
    }
    if (header.capacity == 0) {
        uint64_t desc_offset = le64_to_cpu(header.desc_offset);
        if (desc_offset) {
            char *buf = vmdk_read_desc(file, desc_offset << 9, errp);
            if (!buf) {
                return -EINVAL;
            }
            ret = vmdk_open_desc_file(bs, flags, buf, options, errp);
            g_free(buf);
            return ret;
        }
    }

    if (!s->create_type) {
        s->create_type = g_strdup("monolithicSparse");
    }

    if (le64_to_cpu(header.gd_offset) == VMDK4_GD_AT_END) {
        /*
         * The footer takes precedence over the header, so read it in. The
         * footer starts at offset -1024 from the end: One sector for the
         * footer, and another one for the end-of-stream marker.
         */
        struct {
            struct {
                uint64_t val;
                uint32_t size;
                uint32_t type;
                uint8_t pad[512 - 16];
            } QEMU_PACKED footer_marker;

            uint32_t magic;
            VMDK4Header header;
            uint8_t pad[512 - 4 - sizeof(VMDK4Header)];

            struct {
                uint64_t val;
                uint32_t size;
                uint32_t type;
                uint8_t pad[512 - 16];
            } QEMU_PACKED eos_marker;
        } QEMU_PACKED footer;

        ret = bdrv_pread(file,
            bs->file->bs->total_sectors * 512 - 1536,
            &footer, sizeof(footer));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to read footer");
            return ret;
        }

        /* Some sanity checks for the footer */
        if (be32_to_cpu(footer.magic) != VMDK4_MAGIC ||
            le32_to_cpu(footer.footer_marker.size) != 0  ||
            le32_to_cpu(footer.footer_marker.type) != MARKER_FOOTER ||
            le64_to_cpu(footer.eos_marker.val) != 0  ||
            le32_to_cpu(footer.eos_marker.size) != 0  ||
            le32_to_cpu(footer.eos_marker.type) != MARKER_END_OF_STREAM)
        {
            error_setg(errp, "Invalid footer");
            return -EINVAL;
        }

        header = footer.header;
    }

    compressed =
        le16_to_cpu(header.compressAlgorithm) == VMDK4_COMPRESSION_DEFLATE;
    if (le32_to_cpu(header.version) > 3) {
        error_setg(errp, "Unsupported VMDK version %" PRIu32,
                   le32_to_cpu(header.version));
        return -ENOTSUP;
    } else if (le32_to_cpu(header.version) == 3 && (flags & BDRV_O_RDWR) &&
               !compressed) {
        /* VMware KB 2064959 explains that version 3 added support for
         * persistent changed block tracking (CBT), and backup software can
         * read it as version=1 if it doesn't care about the changed area
         * information. So we are safe to enable read only. */
        error_setg(errp, "VMDK version 3 must be read only");
        return -EINVAL;
    }

    if (le32_to_cpu(header.num_gtes_per_gt) > 512) {
        error_setg(errp, "L2 table size too big");
        return -EINVAL;
    }

    l1_entry_sectors = le32_to_cpu(header.num_gtes_per_gt)
                        * le64_to_cpu(header.granularity);
    if (l1_entry_sectors == 0) {
        error_setg(errp, "L1 entry size is invalid");
        return -EINVAL;
    }
    l1_size = (le64_to_cpu(header.capacity) + l1_entry_sectors - 1)
                / l1_entry_sectors;
    if (le32_to_cpu(header.flags) & VMDK4_FLAG_RGD) {
        l1_backup_offset = le64_to_cpu(header.rgd_offset) << 9;
    }
    if (bdrv_nb_sectors(file->bs) < le64_to_cpu(header.grain_offset)) {
        error_setg(errp, "File truncated, expecting at least %" PRId64 " bytes",
                   (int64_t)(le64_to_cpu(header.grain_offset)
                             * BDRV_SECTOR_SIZE));
        return -EINVAL;
    }

    ret = vmdk_add_extent(bs, file, false,
                          le64_to_cpu(header.capacity),
                          le64_to_cpu(header.gd_offset) << 9,
                          l1_backup_offset,
                          l1_size,
                          le32_to_cpu(header.num_gtes_per_gt),
                          le64_to_cpu(header.granularity),
                          &extent,
                          errp);
    if (ret < 0) {
        return ret;
    }
    extent->compressed =
        le16_to_cpu(header.compressAlgorithm) == VMDK4_COMPRESSION_DEFLATE;
    if (extent->compressed) {
        g_free(s->create_type);
        s->create_type = g_strdup("streamOptimized");
    }
    extent->has_marker = le32_to_cpu(header.flags) & VMDK4_FLAG_MARKER;
    extent->version = le32_to_cpu(header.version);
    extent->has_zero_grain = le32_to_cpu(header.flags) & VMDK4_FLAG_ZERO_GRAIN;
    ret = vmdk_init_tables(bs, extent, errp);
    if (ret) {
        /* free extent allocated by vmdk_add_extent */
        vmdk_free_last_extent(bs);
    }
    return ret;
}

/* find an option value out of descriptor file */
static int vmdk_parse_description(const char *desc, const char *opt_name,
        char *buf, int buf_size)
{
    char *opt_pos, *opt_end;
    const char *end = desc + strlen(desc);

    opt_pos = strstr(desc, opt_name);
    if (!opt_pos) {
        return VMDK_ERROR;
    }
    /* Skip "=\"" following opt_name */
    opt_pos += strlen(opt_name) + 2;
    if (opt_pos >= end) {
        return VMDK_ERROR;
    }
    opt_end = opt_pos;
    while (opt_end < end && *opt_end != '"') {
        opt_end++;
    }
    if (opt_end == end || buf_size < opt_end - opt_pos + 1) {
        return VMDK_ERROR;
    }
    pstrcpy(buf, opt_end - opt_pos + 1, opt_pos);
    return VMDK_OK;
}

/* Open an extent file and append to bs array */
static int vmdk_open_sparse(BlockDriverState *bs, BdrvChild *file, int flags,
                            char *buf, QDict *options, Error **errp)
{
    uint32_t magic;

    magic = ldl_be_p(buf);
    switch (magic) {
        case VMDK3_MAGIC:
            return vmdk_open_vmfs_sparse(bs, file, flags, errp);
        case VMDK4_MAGIC:
            return vmdk_open_vmdk4(bs, file, flags, options, errp);
        default:
            error_setg(errp, "Image not in VMDK format");
            return -EINVAL;
    }
}

static const char *next_line(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            return s + 1;
        }
        s++;
    }
    return s;
}

static int vmdk_parse_extents(const char *desc, BlockDriverState *bs,
                              QDict *options, Error **errp)
{
    int ret;
    int matches;
    char access[11];
    char type[11];
    char fname[512];
    const char *p, *np;
    int64_t sectors = 0;
    int64_t flat_offset;
    char *desc_file_dir = NULL;
    char *extent_path;
    BdrvChild *extent_file;
    BdrvChildRole extent_role;
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    char extent_opt_prefix[32];
    Error *local_err = NULL;

    for (p = desc; *p; p = next_line(p)) {
        /* parse extent line in one of below formats:
         *
         * RW [size in sectors] FLAT "file-name.vmdk" OFFSET
         * RW [size in sectors] SPARSE "file-name.vmdk"
         * RW [size in sectors] VMFS "file-name.vmdk"
         * RW [size in sectors] VMFSSPARSE "file-name.vmdk"
         * RW [size in sectors] SESPARSE "file-name.vmdk"
         */
        flat_offset = -1;
        matches = sscanf(p, "%10s %" SCNd64 " %10s \"%511[^\n\r\"]\" %" SCNd64,
                         access, &sectors, type, fname, &flat_offset);
        if (matches < 4 || strcmp(access, "RW")) {
            continue;
        } else if (!strcmp(type, "FLAT")) {
            if (matches != 5 || flat_offset < 0) {
                goto invalid;
            }
        } else if (!strcmp(type, "VMFS")) {
            if (matches == 4) {
                flat_offset = 0;
            } else {
                goto invalid;
            }
        } else if (matches != 4) {
            goto invalid;
        }

        if (sectors <= 0 ||
            (strcmp(type, "FLAT") && strcmp(type, "SPARSE") &&
             strcmp(type, "VMFS") && strcmp(type, "VMFSSPARSE") &&
             strcmp(type, "SESPARSE")) ||
            (strcmp(access, "RW"))) {
            continue;
        }

        if (path_is_absolute(fname)) {
            extent_path = g_strdup(fname);
        } else {
            if (!desc_file_dir) {
                desc_file_dir = bdrv_dirname(bs->file->bs, errp);
                if (!desc_file_dir) {
                    bdrv_refresh_filename(bs->file->bs);
                    error_prepend(errp, "Cannot use relative paths with VMDK "
                                  "descriptor file '%s': ",
                                  bs->file->bs->filename);
                    ret = -EINVAL;
                    goto out;
                }
            }

            extent_path = g_strconcat(desc_file_dir, fname, NULL);
        }

        ret = snprintf(extent_opt_prefix, 32, "extents.%d", s->num_extents);
        assert(ret < 32);

        extent_role = BDRV_CHILD_DATA;
        if (strcmp(type, "FLAT") != 0 && strcmp(type, "VMFS") != 0) {
            /* non-flat extents have metadata */
            extent_role |= BDRV_CHILD_METADATA;
        }

        extent_file = bdrv_open_child(extent_path, options, extent_opt_prefix,
                                      bs, &child_of_bds, extent_role, false,
                                      &local_err);
        g_free(extent_path);
        if (local_err) {
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto out;
        }

        /* save to extents array */
        if (!strcmp(type, "FLAT") || !strcmp(type, "VMFS")) {
            /* FLAT extent */

            ret = vmdk_add_extent(bs, extent_file, true, sectors,
                            0, 0, 0, 0, 0, &extent, errp);
            if (ret < 0) {
                bdrv_unref_child(bs, extent_file);
                goto out;
            }
            extent->flat_start_offset = flat_offset << 9;
        } else if (!strcmp(type, "SPARSE") || !strcmp(type, "VMFSSPARSE")) {
            /* SPARSE extent and VMFSSPARSE extent are both "COWD" sparse file*/
            char *buf = vmdk_read_desc(extent_file, 0, errp);
            if (!buf) {
                ret = -EINVAL;
            } else {
                ret = vmdk_open_sparse(bs, extent_file, bs->open_flags, buf,
                                       options, errp);
            }
            g_free(buf);
            if (ret) {
                bdrv_unref_child(bs, extent_file);
                goto out;
            }
            extent = &s->extents[s->num_extents - 1];
        } else if (!strcmp(type, "SESPARSE")) {
            ret = vmdk_open_se_sparse(bs, extent_file, bs->open_flags, errp);
            if (ret) {
                bdrv_unref_child(bs, extent_file);
                goto out;
            }
            extent = &s->extents[s->num_extents - 1];
        } else {
            error_setg(errp, "Unsupported extent type '%s'", type);
            bdrv_unref_child(bs, extent_file);
            ret = -ENOTSUP;
            goto out;
        }
        extent->type = g_strdup(type);
    }

    ret = 0;
    goto out;

invalid:
    np = next_line(p);
    assert(np != p);
    if (np[-1] == '\n') {
        np--;
    }
    error_setg(errp, "Invalid extent line: %.*s", (int)(np - p), p);
    ret = -EINVAL;

out:
    g_free(desc_file_dir);
    return ret;
}

static int vmdk_open_desc_file(BlockDriverState *bs, int flags, char *buf,
                               QDict *options, Error **errp)
{
    int ret;
    char ct[128];
    BDRVVmdkState *s = bs->opaque;

    if (vmdk_parse_description(buf, "createType", ct, sizeof(ct))) {
        error_setg(errp, "invalid VMDK image descriptor");
        ret = -EINVAL;
        goto exit;
    }
    if (strcmp(ct, "monolithicFlat") &&
        strcmp(ct, "vmfs") &&
        strcmp(ct, "vmfsSparse") &&
        strcmp(ct, "seSparse") &&
        strcmp(ct, "twoGbMaxExtentSparse") &&
        strcmp(ct, "twoGbMaxExtentFlat")) {
        error_setg(errp, "Unsupported image type '%s'", ct);
        ret = -ENOTSUP;
        goto exit;
    }
    s->create_type = g_strdup(ct);
    s->desc_offset = 0;
    ret = vmdk_parse_extents(buf, bs, options, errp);
exit:
    return ret;
}

static int vmdk_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    char *buf;
    int ret;
    BDRVVmdkState *s = bs->opaque;
    uint32_t magic;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_IMAGE, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    buf = vmdk_read_desc(bs->file, 0, errp);
    if (!buf) {
        return -EINVAL;
    }

    magic = ldl_be_p(buf);
    switch (magic) {
        case VMDK3_MAGIC:
        case VMDK4_MAGIC:
            ret = vmdk_open_sparse(bs, bs->file, flags, buf, options,
                                   errp);
            s->desc_offset = 0x200;
            break;
        default:
            /* No data in the descriptor file */
            bs->file->role &= ~BDRV_CHILD_DATA;

            /* Must succeed because we have given up permissions if anything */
            bdrv_child_refresh_perms(bs, bs->file, &error_abort);

            ret = vmdk_open_desc_file(bs, flags, buf, options, errp);
            break;
    }
    if (ret) {
        goto fail;
    }

    /* try to open parent images, if exist */
    ret = vmdk_parent_open(bs);
    if (ret) {
        goto fail;
    }
    ret = vmdk_read_cid(bs, 0, &s->cid);
    if (ret) {
        goto fail;
    }
    ret = vmdk_read_cid(bs, 1, &s->parent_cid);
    if (ret) {
        goto fail;
    }
    qemu_co_mutex_init(&s->lock);

    /* Disable migration when VMDK images are used */
    error_setg(&s->migration_blocker, "The vmdk format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker(s->migration_blocker, errp);
    if (ret < 0) {
        error_free(s->migration_blocker);
        goto fail;
    }

    g_free(buf);
    return 0;

fail:
    g_free(buf);
    g_free(s->create_type);
    s->create_type = NULL;
    vmdk_free_extents(bs);
    return ret;
}


static void vmdk_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVVmdkState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_extents; i++) {
        if (!s->extents[i].flat) {
            bs->bl.pwrite_zeroes_alignment =
                MAX(bs->bl.pwrite_zeroes_alignment,
                    s->extents[i].cluster_sectors << BDRV_SECTOR_BITS);
        }
    }
}

/**
 * get_whole_cluster
 *
 * Copy backing file's cluster that covers @sector_num, otherwise write zero,
 * to the cluster at @cluster_sector_num. If @zeroed is true, we're overwriting
 * a zeroed cluster in the current layer and must not copy data from the
 * backing file.
 *
 * If @skip_start_sector < @skip_end_sector, the relative range
 * [@skip_start_sector, @skip_end_sector) is not copied or written, and leave
 * it for call to write user data in the request.
 */
static int get_whole_cluster(BlockDriverState *bs,
                             VmdkExtent *extent,
                             uint64_t cluster_offset,
                             uint64_t offset,
                             uint64_t skip_start_bytes,
                             uint64_t skip_end_bytes,
                             bool zeroed)
{
    int ret = VMDK_OK;
    int64_t cluster_bytes;
    uint8_t *whole_grain;
    bool copy_from_backing;

    /* For COW, align request sector_num to cluster start */
    cluster_bytes = extent->cluster_sectors << BDRV_SECTOR_BITS;
    offset = QEMU_ALIGN_DOWN(offset, cluster_bytes);
    whole_grain = qemu_blockalign(bs, cluster_bytes);
    copy_from_backing = bs->backing && !zeroed;

    if (!copy_from_backing) {
        memset(whole_grain, 0, skip_start_bytes);
        memset(whole_grain + skip_end_bytes, 0, cluster_bytes - skip_end_bytes);
    }

    assert(skip_end_bytes <= cluster_bytes);
    /* we will be here if it's first write on non-exist grain(cluster).
     * try to read from parent image, if exist */
    if (bs->backing && !vmdk_is_cid_valid(bs)) {
        ret = VMDK_ERROR;
        goto exit;
    }

    /* Read backing data before skip range */
    if (skip_start_bytes > 0) {
        if (copy_from_backing) {
            /* qcow2 emits this on bs->file instead of bs->backing */
            BLKDBG_EVENT(extent->file, BLKDBG_COW_READ);
            ret = bdrv_pread(bs->backing, offset, whole_grain,
                             skip_start_bytes);
            if (ret < 0) {
                ret = VMDK_ERROR;
                goto exit;
            }
        }
        BLKDBG_EVENT(extent->file, BLKDBG_COW_WRITE);
        ret = bdrv_pwrite(extent->file, cluster_offset, whole_grain,
                          skip_start_bytes);
        if (ret < 0) {
            ret = VMDK_ERROR;
            goto exit;
        }
    }
    /* Read backing data after skip range */
    if (skip_end_bytes < cluster_bytes) {
        if (copy_from_backing) {
            /* qcow2 emits this on bs->file instead of bs->backing */
            BLKDBG_EVENT(extent->file, BLKDBG_COW_READ);
            ret = bdrv_pread(bs->backing, offset + skip_end_bytes,
                             whole_grain + skip_end_bytes,
                             cluster_bytes - skip_end_bytes);
            if (ret < 0) {
                ret = VMDK_ERROR;
                goto exit;
            }
        }
        BLKDBG_EVENT(extent->file, BLKDBG_COW_WRITE);
        ret = bdrv_pwrite(extent->file, cluster_offset + skip_end_bytes,
                          whole_grain + skip_end_bytes,
                          cluster_bytes - skip_end_bytes);
        if (ret < 0) {
            ret = VMDK_ERROR;
            goto exit;
        }
    }

    ret = VMDK_OK;
exit:
    qemu_vfree(whole_grain);
    return ret;
}

static int vmdk_L2update(VmdkExtent *extent, VmdkMetaData *m_data,
                         uint32_t offset)
{
    offset = cpu_to_le32(offset);
    /* update L2 table */
    BLKDBG_EVENT(extent->file, BLKDBG_L2_UPDATE);
    if (bdrv_pwrite(extent->file,
                ((int64_t)m_data->l2_offset * 512)
                    + (m_data->l2_index * sizeof(offset)),
                &offset, sizeof(offset)) < 0) {
        return VMDK_ERROR;
    }
    /* update backup L2 table */
    if (extent->l1_backup_table_offset != 0) {
        m_data->l2_offset = extent->l1_backup_table[m_data->l1_index];
        if (bdrv_pwrite(extent->file,
                    ((int64_t)m_data->l2_offset * 512)
                        + (m_data->l2_index * sizeof(offset)),
                    &offset, sizeof(offset)) < 0) {
            return VMDK_ERROR;
        }
    }
    if (bdrv_flush(extent->file->bs) < 0) {
        return VMDK_ERROR;
    }
    if (m_data->l2_cache_entry) {
        *m_data->l2_cache_entry = offset;
    }

    return VMDK_OK;
}

/**
 * get_cluster_offset
 *
 * Look up cluster offset in extent file by sector number, and store in
 * @cluster_offset.
 *
 * For flat extents, the start offset as parsed from the description file is
 * returned.
 *
 * For sparse extents, look up in L1, L2 table. If allocate is true, return an
 * offset for a new cluster and update L2 cache. If there is a backing file,
 * COW is done before returning; otherwise, zeroes are written to the allocated
 * cluster. Both COW and zero writing skips the sector range
 * [@skip_start_sector, @skip_end_sector) passed in by caller, because caller
 * has new data to write there.
 *
 * Returns: VMDK_OK if cluster exists and mapped in the image.
 *          VMDK_UNALLOC if cluster is not mapped and @allocate is false.
 *          VMDK_ERROR if failed.
 */
static int get_cluster_offset(BlockDriverState *bs,
                              VmdkExtent *extent,
                              VmdkMetaData *m_data,
                              uint64_t offset,
                              bool allocate,
                              uint64_t *cluster_offset,
                              uint64_t skip_start_bytes,
                              uint64_t skip_end_bytes)
{
    unsigned int l1_index, l2_offset, l2_index;
    int min_index, i, j;
    uint32_t min_count;
    void *l2_table;
    bool zeroed = false;
    int64_t ret;
    int64_t cluster_sector;
    unsigned int l2_size_bytes = extent->l2_size * extent->entry_size;

    if (m_data) {
        m_data->new_allocation = false;
    }
    if (extent->flat) {
        *cluster_offset = extent->flat_start_offset;
        return VMDK_OK;
    }

    offset -= (extent->end_sector - extent->sectors) * SECTOR_SIZE;
    l1_index = (offset >> 9) / extent->l1_entry_sectors;
    if (l1_index >= extent->l1_size) {
        return VMDK_ERROR;
    }
    if (extent->sesparse) {
        uint64_t l2_offset_u64;

        assert(extent->entry_size == sizeof(uint64_t));

        l2_offset_u64 = ((uint64_t *)extent->l1_table)[l1_index];
        if (l2_offset_u64 == 0) {
            l2_offset = 0;
        } else if ((l2_offset_u64 & 0xffffffff00000000) != 0x1000000000000000) {
            /*
             * Top most nibble is 0x1 if grain table is allocated.
             * strict check - top most 4 bytes must be 0x10000000 since max
             * supported size is 64TB for disk - so no more than 64TB / 16MB
             * grain directories which is smaller than uint32,
             * where 16MB is the only supported default grain table coverage.
             */
            return VMDK_ERROR;
        } else {
            l2_offset_u64 = l2_offset_u64 & 0x00000000ffffffff;
            l2_offset_u64 = extent->sesparse_l2_tables_offset +
                l2_offset_u64 * l2_size_bytes / SECTOR_SIZE;
            if (l2_offset_u64 > 0x00000000ffffffff) {
                return VMDK_ERROR;
            }
            l2_offset = (unsigned int)(l2_offset_u64);
        }
    } else {
        assert(extent->entry_size == sizeof(uint32_t));
        l2_offset = ((uint32_t *)extent->l1_table)[l1_index];
    }
    if (!l2_offset) {
        return VMDK_UNALLOC;
    }
    for (i = 0; i < L2_CACHE_SIZE; i++) {
        if (l2_offset == extent->l2_cache_offsets[i]) {
            /* increment the hit count */
            if (++extent->l2_cache_counts[i] == 0xffffffff) {
                for (j = 0; j < L2_CACHE_SIZE; j++) {
                    extent->l2_cache_counts[j] >>= 1;
                }
            }
            l2_table = (char *)extent->l2_cache + (i * l2_size_bytes);
            goto found;
        }
    }
    /* not found: load a new entry in the least used one */
    min_index = 0;
    min_count = 0xffffffff;
    for (i = 0; i < L2_CACHE_SIZE; i++) {
        if (extent->l2_cache_counts[i] < min_count) {
            min_count = extent->l2_cache_counts[i];
            min_index = i;
        }
    }
    l2_table = (char *)extent->l2_cache + (min_index * l2_size_bytes);
    BLKDBG_EVENT(extent->file, BLKDBG_L2_LOAD);
    if (bdrv_pread(extent->file,
                (int64_t)l2_offset * 512,
                l2_table,
                l2_size_bytes
            ) != l2_size_bytes) {
        return VMDK_ERROR;
    }

    extent->l2_cache_offsets[min_index] = l2_offset;
    extent->l2_cache_counts[min_index] = 1;
 found:
    l2_index = ((offset >> 9) / extent->cluster_sectors) % extent->l2_size;
    if (m_data) {
        m_data->l1_index = l1_index;
        m_data->l2_index = l2_index;
        m_data->l2_offset = l2_offset;
        m_data->l2_cache_entry = ((uint32_t *)l2_table) + l2_index;
    }

    if (extent->sesparse) {
        cluster_sector = le64_to_cpu(((uint64_t *)l2_table)[l2_index]);
        switch (cluster_sector & 0xf000000000000000) {
        case 0x0000000000000000:
            /* unallocated grain */
            if (cluster_sector != 0) {
                return VMDK_ERROR;
            }
            break;
        case 0x1000000000000000:
            /* scsi-unmapped grain - fallthrough */
        case 0x2000000000000000:
            /* zero grain */
            zeroed = true;
            break;
        case 0x3000000000000000:
            /* allocated grain */
            cluster_sector = (((cluster_sector & 0x0fff000000000000) >> 48) |
                              ((cluster_sector & 0x0000ffffffffffff) << 12));
            cluster_sector = extent->sesparse_clusters_offset +
                cluster_sector * extent->cluster_sectors;
            break;
        default:
            return VMDK_ERROR;
        }
    } else {
        cluster_sector = le32_to_cpu(((uint32_t *)l2_table)[l2_index]);

        if (extent->has_zero_grain && cluster_sector == VMDK_GTE_ZEROED) {
            zeroed = true;
        }
    }

    if (!cluster_sector || zeroed) {
        if (!allocate) {
            return zeroed ? VMDK_ZEROED : VMDK_UNALLOC;
        }
        assert(!extent->sesparse);

        if (extent->next_cluster_sector >= VMDK_EXTENT_MAX_SECTORS) {
            return VMDK_ERROR;
        }

        cluster_sector = extent->next_cluster_sector;
        extent->next_cluster_sector += extent->cluster_sectors;

        /* First of all we write grain itself, to avoid race condition
         * that may to corrupt the image.
         * This problem may occur because of insufficient space on host disk
         * or inappropriate VM shutdown.
         */
        ret = get_whole_cluster(bs, extent, cluster_sector * BDRV_SECTOR_SIZE,
                                offset, skip_start_bytes, skip_end_bytes,
                                zeroed);
        if (ret) {
            return ret;
        }
        if (m_data) {
            m_data->new_allocation = true;
        }
    }
    *cluster_offset = cluster_sector << BDRV_SECTOR_BITS;
    return VMDK_OK;
}

static VmdkExtent *find_extent(BDRVVmdkState *s,
                                int64_t sector_num, VmdkExtent *start_hint)
{
    VmdkExtent *extent = start_hint;

    if (!extent) {
        extent = &s->extents[0];
    }
    while (extent < &s->extents[s->num_extents]) {
        if (sector_num < extent->end_sector) {
            return extent;
        }
        extent++;
    }
    return NULL;
}

static inline uint64_t vmdk_find_offset_in_cluster(VmdkExtent *extent,
                                                   int64_t offset)
{
    uint64_t extent_begin_offset, extent_relative_offset;
    uint64_t cluster_size = extent->cluster_sectors * BDRV_SECTOR_SIZE;

    extent_begin_offset =
        (extent->end_sector - extent->sectors) * BDRV_SECTOR_SIZE;
    extent_relative_offset = offset - extent_begin_offset;
    return extent_relative_offset % cluster_size;
}

static int coroutine_fn vmdk_co_block_status(BlockDriverState *bs,
                                             bool want_zero,
                                             int64_t offset, int64_t bytes,
                                             int64_t *pnum, int64_t *map,
                                             BlockDriverState **file)
{
    BDRVVmdkState *s = bs->opaque;
    int64_t index_in_cluster, n, ret;
    uint64_t cluster_offset;
    VmdkExtent *extent;

    extent = find_extent(s, offset >> BDRV_SECTOR_BITS, NULL);
    if (!extent) {
        return -EIO;
    }
    qemu_co_mutex_lock(&s->lock);
    ret = get_cluster_offset(bs, extent, NULL, offset, false, &cluster_offset,
                             0, 0);
    qemu_co_mutex_unlock(&s->lock);

    index_in_cluster = vmdk_find_offset_in_cluster(extent, offset);
    switch (ret) {
    case VMDK_ERROR:
        ret = -EIO;
        break;
    case VMDK_UNALLOC:
        ret = 0;
        break;
    case VMDK_ZEROED:
        ret = BDRV_BLOCK_ZERO;
        break;
    case VMDK_OK:
        ret = BDRV_BLOCK_DATA;
        if (!extent->compressed) {
            ret |= BDRV_BLOCK_OFFSET_VALID;
            *map = cluster_offset + index_in_cluster;
            if (extent->flat) {
                ret |= BDRV_BLOCK_RECURSE;
            }
        }
        *file = extent->file->bs;
        break;
    }

    n = extent->cluster_sectors * BDRV_SECTOR_SIZE - index_in_cluster;
    *pnum = MIN(n, bytes);
    return ret;
}

static int vmdk_write_extent(VmdkExtent *extent, int64_t cluster_offset,
                            int64_t offset_in_cluster, QEMUIOVector *qiov,
                            uint64_t qiov_offset, uint64_t n_bytes,
                            uint64_t offset)
{
    int ret;
    VmdkGrainMarker *data = NULL;
    uLongf buf_len;
    QEMUIOVector local_qiov;
    int64_t write_offset;
    int64_t write_end_sector;

    if (extent->compressed) {
        void *compressed_data;

        /* Only whole clusters */
        if (offset_in_cluster ||
            n_bytes > (extent->cluster_sectors * SECTOR_SIZE) ||
            (n_bytes < (extent->cluster_sectors * SECTOR_SIZE) &&
             offset + n_bytes != extent->end_sector * SECTOR_SIZE))
        {
            ret = -EINVAL;
            goto out;
        }

        if (!extent->has_marker) {
            ret = -EINVAL;
            goto out;
        }
        buf_len = (extent->cluster_sectors << 9) * 2;
        data = g_malloc(buf_len + sizeof(VmdkGrainMarker));

        compressed_data = g_malloc(n_bytes);
        qemu_iovec_to_buf(qiov, qiov_offset, compressed_data, n_bytes);
        ret = compress(data->data, &buf_len, compressed_data, n_bytes);
        g_free(compressed_data);

        if (ret != Z_OK || buf_len == 0) {
            ret = -EINVAL;
            goto out;
        }

        data->lba = cpu_to_le64(offset >> BDRV_SECTOR_BITS);
        data->size = cpu_to_le32(buf_len);

        n_bytes = buf_len + sizeof(VmdkGrainMarker);
        qemu_iovec_init_buf(&local_qiov, data, n_bytes);

        BLKDBG_EVENT(extent->file, BLKDBG_WRITE_COMPRESSED);
    } else {
        qemu_iovec_init(&local_qiov, qiov->niov);
        qemu_iovec_concat(&local_qiov, qiov, qiov_offset, n_bytes);

        BLKDBG_EVENT(extent->file, BLKDBG_WRITE_AIO);
    }

    write_offset = cluster_offset + offset_in_cluster;
    ret = bdrv_co_pwritev(extent->file, write_offset, n_bytes,
                          &local_qiov, 0);

    write_end_sector = DIV_ROUND_UP(write_offset + n_bytes, BDRV_SECTOR_SIZE);

    if (extent->compressed) {
        extent->next_cluster_sector = write_end_sector;
    } else {
        extent->next_cluster_sector = MAX(extent->next_cluster_sector,
                                          write_end_sector);
    }

    if (ret < 0) {
        goto out;
    }
    ret = 0;
 out:
    g_free(data);
    if (!extent->compressed) {
        qemu_iovec_destroy(&local_qiov);
    }
    return ret;
}

static int vmdk_read_extent(VmdkExtent *extent, int64_t cluster_offset,
                            int64_t offset_in_cluster, QEMUIOVector *qiov,
                            int bytes)
{
    int ret;
    int cluster_bytes, buf_bytes;
    uint8_t *cluster_buf, *compressed_data;
    uint8_t *uncomp_buf;
    uint32_t data_len;
    VmdkGrainMarker *marker;
    uLongf buf_len;


    if (!extent->compressed) {
        BLKDBG_EVENT(extent->file, BLKDBG_READ_AIO);
        ret = bdrv_co_preadv(extent->file,
                             cluster_offset + offset_in_cluster, bytes,
                             qiov, 0);
        if (ret < 0) {
            return ret;
        }
        return 0;
    }
    cluster_bytes = extent->cluster_sectors * 512;
    /* Read two clusters in case GrainMarker + compressed data > one cluster */
    buf_bytes = cluster_bytes * 2;
    cluster_buf = g_malloc(buf_bytes);
    uncomp_buf = g_malloc(cluster_bytes);
    BLKDBG_EVENT(extent->file, BLKDBG_READ_COMPRESSED);
    ret = bdrv_pread(extent->file,
                cluster_offset,
                cluster_buf, buf_bytes);
    if (ret < 0) {
        goto out;
    }
    compressed_data = cluster_buf;
    buf_len = cluster_bytes;
    data_len = cluster_bytes;
    if (extent->has_marker) {
        marker = (VmdkGrainMarker *)cluster_buf;
        compressed_data = marker->data;
        data_len = le32_to_cpu(marker->size);
    }
    if (!data_len || data_len > buf_bytes) {
        ret = -EINVAL;
        goto out;
    }
    ret = uncompress(uncomp_buf, &buf_len, compressed_data, data_len);
    if (ret != Z_OK) {
        ret = -EINVAL;
        goto out;

    }
    if (offset_in_cluster < 0 ||
            offset_in_cluster + bytes > buf_len) {
        ret = -EINVAL;
        goto out;
    }
    qemu_iovec_from_buf(qiov, 0, uncomp_buf + offset_in_cluster, bytes);
    ret = 0;

 out:
    g_free(uncomp_buf);
    g_free(cluster_buf);
    return ret;
}

static int coroutine_fn
vmdk_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
               QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVVmdkState *s = bs->opaque;
    int ret;
    uint64_t n_bytes, offset_in_cluster;
    VmdkExtent *extent = NULL;
    QEMUIOVector local_qiov;
    uint64_t cluster_offset;
    uint64_t bytes_done = 0;

    qemu_iovec_init(&local_qiov, qiov->niov);
    qemu_co_mutex_lock(&s->lock);

    while (bytes > 0) {
        extent = find_extent(s, offset >> BDRV_SECTOR_BITS, extent);
        if (!extent) {
            ret = -EIO;
            goto fail;
        }
        ret = get_cluster_offset(bs, extent, NULL,
                                 offset, false, &cluster_offset, 0, 0);
        offset_in_cluster = vmdk_find_offset_in_cluster(extent, offset);

        n_bytes = MIN(bytes, extent->cluster_sectors * BDRV_SECTOR_SIZE
                             - offset_in_cluster);

        if (ret != VMDK_OK) {
            /* if not allocated, try to read from parent image, if exist */
            if (bs->backing && ret != VMDK_ZEROED) {
                if (!vmdk_is_cid_valid(bs)) {
                    ret = -EINVAL;
                    goto fail;
                }

                qemu_iovec_reset(&local_qiov);
                qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

                /* qcow2 emits this on bs->file instead of bs->backing */
                BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                ret = bdrv_co_preadv(bs->backing, offset, n_bytes,
                                     &local_qiov, 0);
                if (ret < 0) {
                    goto fail;
                }
            } else {
                qemu_iovec_memset(qiov, bytes_done, 0, n_bytes);
            }
        } else {
            qemu_iovec_reset(&local_qiov);
            qemu_iovec_concat(&local_qiov, qiov, bytes_done, n_bytes);

            ret = vmdk_read_extent(extent, cluster_offset, offset_in_cluster,
                                   &local_qiov, n_bytes);
            if (ret) {
                goto fail;
            }
        }
        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;
    }

    ret = 0;
fail:
    qemu_co_mutex_unlock(&s->lock);
    qemu_iovec_destroy(&local_qiov);

    return ret;
}

/**
 * vmdk_write:
 * @zeroed:       buf is ignored (data is zero), use zeroed_grain GTE feature
 *                if possible, otherwise return -ENOTSUP.
 * @zero_dry_run: used for zeroed == true only, don't update L2 table, just try
 *                with each cluster. By dry run we can find if the zero write
 *                is possible without modifying image data.
 *
 * Returns: error code with 0 for success.
 */
static int vmdk_pwritev(BlockDriverState *bs, uint64_t offset,
                       uint64_t bytes, QEMUIOVector *qiov,
                       bool zeroed, bool zero_dry_run)
{
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    int ret;
    int64_t offset_in_cluster, n_bytes;
    uint64_t cluster_offset;
    uint64_t bytes_done = 0;
    VmdkMetaData m_data;

    if (DIV_ROUND_UP(offset, BDRV_SECTOR_SIZE) > bs->total_sectors) {
        error_report("Wrong offset: offset=0x%" PRIx64
                     " total_sectors=0x%" PRIx64,
                     offset, bs->total_sectors);
        return -EIO;
    }

    while (bytes > 0) {
        extent = find_extent(s, offset >> BDRV_SECTOR_BITS, extent);
        if (!extent) {
            return -EIO;
        }
        if (extent->sesparse) {
            return -ENOTSUP;
        }
        offset_in_cluster = vmdk_find_offset_in_cluster(extent, offset);
        n_bytes = MIN(bytes, extent->cluster_sectors * BDRV_SECTOR_SIZE
                             - offset_in_cluster);

        ret = get_cluster_offset(bs, extent, &m_data, offset,
                                 !(extent->compressed || zeroed),
                                 &cluster_offset, offset_in_cluster,
                                 offset_in_cluster + n_bytes);
        if (extent->compressed) {
            if (ret == VMDK_OK) {
                /* Refuse write to allocated cluster for streamOptimized */
                error_report("Could not write to allocated cluster"
                              " for streamOptimized");
                return -EIO;
            } else if (!zeroed) {
                /* allocate */
                ret = get_cluster_offset(bs, extent, &m_data, offset,
                                         true, &cluster_offset, 0, 0);
            }
        }
        if (ret == VMDK_ERROR) {
            return -EINVAL;
        }
        if (zeroed) {
            /* Do zeroed write, buf is ignored */
            if (extent->has_zero_grain &&
                    offset_in_cluster == 0 &&
                    n_bytes >= extent->cluster_sectors * BDRV_SECTOR_SIZE) {
                n_bytes = extent->cluster_sectors * BDRV_SECTOR_SIZE;
                if (!zero_dry_run && ret != VMDK_ZEROED) {
                    /* update L2 tables */
                    if (vmdk_L2update(extent, &m_data, VMDK_GTE_ZEROED)
                            != VMDK_OK) {
                        return -EIO;
                    }
                }
            } else {
                return -ENOTSUP;
            }
        } else {
            ret = vmdk_write_extent(extent, cluster_offset, offset_in_cluster,
                                    qiov, bytes_done, n_bytes, offset);
            if (ret) {
                return ret;
            }
            if (m_data.new_allocation) {
                /* update L2 tables */
                if (vmdk_L2update(extent, &m_data,
                                  cluster_offset >> BDRV_SECTOR_BITS)
                        != VMDK_OK) {
                    return -EIO;
                }
            }
        }
        bytes -= n_bytes;
        offset += n_bytes;
        bytes_done += n_bytes;

        /* update CID on the first write every time the virtual disk is
         * opened */
        if (!s->cid_updated) {
            ret = vmdk_write_cid(bs, g_random_int());
            if (ret < 0) {
                return ret;
            }
            s->cid_updated = true;
        }
    }
    return 0;
}

static int coroutine_fn
vmdk_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = vmdk_pwritev(bs, offset, bytes, qiov, false, false);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int coroutine_fn
vmdk_co_pwritev_compressed(BlockDriverState *bs, int64_t offset, int64_t bytes,
                           QEMUIOVector *qiov)
{
    if (bytes == 0) {
        /* The caller will write bytes 0 to signal EOF.
         * When receive it, we align EOF to a sector boundary. */
        BDRVVmdkState *s = bs->opaque;
        int i, ret;
        int64_t length;

        for (i = 0; i < s->num_extents; i++) {
            length = bdrv_getlength(s->extents[i].file->bs);
            if (length < 0) {
                return length;
            }
            length = QEMU_ALIGN_UP(length, BDRV_SECTOR_SIZE);
            ret = bdrv_truncate(s->extents[i].file, length, false,
                                PREALLOC_MODE_OFF, 0, NULL);
            if (ret < 0) {
                return ret;
            }
        }
        return 0;
    }
    return vmdk_co_pwritev(bs, offset, bytes, qiov, 0);
}

static int coroutine_fn vmdk_co_pwrite_zeroes(BlockDriverState *bs,
                                              int64_t offset,
                                              int64_t bytes,
                                              BdrvRequestFlags flags)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;

    qemu_co_mutex_lock(&s->lock);
    /* write zeroes could fail if sectors not aligned to cluster, test it with
     * dry_run == true before really updating image */
    ret = vmdk_pwritev(bs, offset, bytes, NULL, true, true);
    if (!ret) {
        ret = vmdk_pwritev(bs, offset, bytes, NULL, true, false);
    }
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int vmdk_init_extent(BlockBackend *blk,
                            int64_t filesize, bool flat,
                            bool compress, bool zeroed_grain,
                            Error **errp)
{
    int ret, i;
    VMDK4Header header;
    uint32_t tmp, magic, grains, gd_sectors, gt_size, gt_count;
    uint32_t *gd_buf = NULL;
    int gd_buf_size;

    if (flat) {
        ret = blk_truncate(blk, filesize, false, PREALLOC_MODE_OFF, 0, errp);
        goto exit;
    }
    magic = cpu_to_be32(VMDK4_MAGIC);
    memset(&header, 0, sizeof(header));
    if (compress) {
        header.version = 3;
    } else if (zeroed_grain) {
        header.version = 2;
    } else {
        header.version = 1;
    }
    header.flags = VMDK4_FLAG_RGD | VMDK4_FLAG_NL_DETECT
                   | (compress ? VMDK4_FLAG_COMPRESS | VMDK4_FLAG_MARKER : 0)
                   | (zeroed_grain ? VMDK4_FLAG_ZERO_GRAIN : 0);
    header.compressAlgorithm = compress ? VMDK4_COMPRESSION_DEFLATE : 0;
    header.capacity = filesize / BDRV_SECTOR_SIZE;
    header.granularity = 128;
    header.num_gtes_per_gt = BDRV_SECTOR_SIZE;

    grains = DIV_ROUND_UP(filesize / BDRV_SECTOR_SIZE, header.granularity);
    gt_size = DIV_ROUND_UP(header.num_gtes_per_gt * sizeof(uint32_t),
                           BDRV_SECTOR_SIZE);
    gt_count = DIV_ROUND_UP(grains, header.num_gtes_per_gt);
    gd_sectors = DIV_ROUND_UP(gt_count * sizeof(uint32_t), BDRV_SECTOR_SIZE);

    header.desc_offset = 1;
    header.desc_size = 20;
    header.rgd_offset = header.desc_offset + header.desc_size;
    header.gd_offset = header.rgd_offset + gd_sectors + (gt_size * gt_count);
    header.grain_offset =
        ROUND_UP(header.gd_offset + gd_sectors + (gt_size * gt_count),
                 header.granularity);
    /* swap endianness for all header fields */
    header.version = cpu_to_le32(header.version);
    header.flags = cpu_to_le32(header.flags);
    header.capacity = cpu_to_le64(header.capacity);
    header.granularity = cpu_to_le64(header.granularity);
    header.num_gtes_per_gt = cpu_to_le32(header.num_gtes_per_gt);
    header.desc_offset = cpu_to_le64(header.desc_offset);
    header.desc_size = cpu_to_le64(header.desc_size);
    header.rgd_offset = cpu_to_le64(header.rgd_offset);
    header.gd_offset = cpu_to_le64(header.gd_offset);
    header.grain_offset = cpu_to_le64(header.grain_offset);
    header.compressAlgorithm = cpu_to_le16(header.compressAlgorithm);

    header.check_bytes[0] = 0xa;
    header.check_bytes[1] = 0x20;
    header.check_bytes[2] = 0xd;
    header.check_bytes[3] = 0xa;

    /* write all the data */
    ret = blk_pwrite(blk, 0, &magic, sizeof(magic), 0);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
        goto exit;
    }
    ret = blk_pwrite(blk, sizeof(magic), &header, sizeof(header), 0);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
        goto exit;
    }

    ret = blk_truncate(blk, le64_to_cpu(header.grain_offset) << 9, false,
                       PREALLOC_MODE_OFF, 0, errp);
    if (ret < 0) {
        goto exit;
    }

    /* write grain directory */
    gd_buf_size = gd_sectors * BDRV_SECTOR_SIZE;
    gd_buf = g_malloc0(gd_buf_size);
    for (i = 0, tmp = le64_to_cpu(header.rgd_offset) + gd_sectors;
         i < gt_count; i++, tmp += gt_size) {
        gd_buf[i] = cpu_to_le32(tmp);
    }
    ret = blk_pwrite(blk, le64_to_cpu(header.rgd_offset) * BDRV_SECTOR_SIZE,
                     gd_buf, gd_buf_size, 0);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
        goto exit;
    }

    /* write backup grain directory */
    for (i = 0, tmp = le64_to_cpu(header.gd_offset) + gd_sectors;
         i < gt_count; i++, tmp += gt_size) {
        gd_buf[i] = cpu_to_le32(tmp);
    }
    ret = blk_pwrite(blk, le64_to_cpu(header.gd_offset) * BDRV_SECTOR_SIZE,
                     gd_buf, gd_buf_size, 0);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
    }

    ret = 0;
exit:
    g_free(gd_buf);
    return ret;
}

static int vmdk_create_extent(const char *filename, int64_t filesize,
                              bool flat, bool compress, bool zeroed_grain,
                              BlockBackend **pbb,
                              QemuOpts *opts, Error **errp)
{
    int ret;
    BlockBackend *blk = NULL;

    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        goto exit;
    }

    blk = blk_new_open(filename, NULL, NULL,
                       BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL,
                       errp);
    if (blk == NULL) {
        ret = -EIO;
        goto exit;
    }

    blk_set_allow_write_beyond_eof(blk, true);

    ret = vmdk_init_extent(blk, filesize, flat, compress, zeroed_grain, errp);
exit:
    if (blk) {
        if (pbb) {
            *pbb = blk;
        } else {
            blk_unref(blk);
            blk = NULL;
        }
    }
    return ret;
}

static int filename_decompose(const char *filename, char *path, char *prefix,
                              char *postfix, size_t buf_len, Error **errp)
{
    const char *p, *q;

    if (filename == NULL || !strlen(filename)) {
        error_setg(errp, "No filename provided");
        return VMDK_ERROR;
    }
    p = strrchr(filename, '/');
    if (p == NULL) {
        p = strrchr(filename, '\\');
    }
    if (p == NULL) {
        p = strrchr(filename, ':');
    }
    if (p != NULL) {
        p++;
        if (p - filename >= buf_len) {
            return VMDK_ERROR;
        }
        pstrcpy(path, p - filename + 1, filename);
    } else {
        p = filename;
        path[0] = '\0';
    }
    q = strrchr(p, '.');
    if (q == NULL) {
        pstrcpy(prefix, buf_len, p);
        postfix[0] = '\0';
    } else {
        if (q - p >= buf_len) {
            return VMDK_ERROR;
        }
        pstrcpy(prefix, q - p + 1, p);
        pstrcpy(postfix, buf_len, q);
    }
    return VMDK_OK;
}

/*
 * idx == 0: get or create the descriptor file (also the image file if in a
 *           non-split format.
 * idx >= 1: get the n-th extent if in a split subformat
 */
typedef BlockBackend *(*vmdk_create_extent_fn)(int64_t size,
                                               int idx,
                                               bool flat,
                                               bool split,
                                               bool compress,
                                               bool zeroed_grain,
                                               void *opaque,
                                               Error **errp);

static void vmdk_desc_add_extent(GString *desc,
                                 const char *extent_line_fmt,
                                 int64_t size, const char *filename)
{
    char *basename = g_path_get_basename(filename);

    g_string_append_printf(desc, extent_line_fmt,
                           DIV_ROUND_UP(size, BDRV_SECTOR_SIZE), basename);
    g_free(basename);
}

static int coroutine_fn vmdk_co_do_create(int64_t size,
                                          BlockdevVmdkSubformat subformat,
                                          BlockdevVmdkAdapterType adapter_type,
                                          const char *backing_file,
                                          const char *hw_version,
                                          const char *toolsversion,
                                          bool compat6,
                                          bool zeroed_grain,
                                          vmdk_create_extent_fn extent_fn,
                                          void *opaque,
                                          Error **errp)
{
    int extent_idx;
    BlockBackend *blk = NULL;
    BlockBackend *extent_blk;
    Error *local_err = NULL;
    char *desc = NULL;
    int ret = 0;
    bool flat, split, compress;
    GString *ext_desc_lines;
    const int64_t split_size = 0x80000000;  /* VMDK has constant split size */
    int64_t extent_size;
    int64_t created_size = 0;
    const char *extent_line_fmt;
    char *parent_desc_line = g_malloc0(BUF_SIZE);
    uint32_t parent_cid = 0xffffffff;
    uint32_t number_heads = 16;
    uint32_t desc_offset = 0, desc_len;
    const char desc_template[] =
        "# Disk DescriptorFile\n"
        "version=1\n"
        "CID=%" PRIx32 "\n"
        "parentCID=%" PRIx32 "\n"
        "createType=\"%s\"\n"
        "%s"
        "\n"
        "# Extent description\n"
        "%s"
        "\n"
        "# The Disk Data Base\n"
        "#DDB\n"
        "\n"
        "ddb.virtualHWVersion = \"%s\"\n"
        "ddb.geometry.cylinders = \"%" PRId64 "\"\n"
        "ddb.geometry.heads = \"%" PRIu32 "\"\n"
        "ddb.geometry.sectors = \"63\"\n"
        "ddb.adapterType = \"%s\"\n"
        "ddb.toolsVersion = \"%s\"\n";

    ext_desc_lines = g_string_new(NULL);

    /* Read out options */
    if (compat6) {
        if (hw_version) {
            error_setg(errp,
                       "compat6 cannot be enabled with hwversion set");
            ret = -EINVAL;
            goto exit;
        }
        hw_version = "6";
    }
    if (!hw_version) {
        hw_version = "4";
    }
    if (!toolsversion) {
        toolsversion = "2147483647";
    }

    if (adapter_type != BLOCKDEV_VMDK_ADAPTER_TYPE_IDE) {
        /* that's the number of heads with which vmware operates when
           creating, exporting, etc. vmdk files with a non-ide adapter type */
        number_heads = 255;
    }
    split = (subformat == BLOCKDEV_VMDK_SUBFORMAT_TWOGBMAXEXTENTFLAT) ||
            (subformat == BLOCKDEV_VMDK_SUBFORMAT_TWOGBMAXEXTENTSPARSE);
    flat = (subformat == BLOCKDEV_VMDK_SUBFORMAT_MONOLITHICFLAT) ||
           (subformat == BLOCKDEV_VMDK_SUBFORMAT_TWOGBMAXEXTENTFLAT);
    compress = subformat == BLOCKDEV_VMDK_SUBFORMAT_STREAMOPTIMIZED;

    if (flat) {
        extent_line_fmt = "RW %" PRId64 " FLAT \"%s\" 0\n";
    } else {
        extent_line_fmt = "RW %" PRId64 " SPARSE \"%s\"\n";
    }
    if (flat && backing_file) {
        error_setg(errp, "Flat image can't have backing file");
        ret = -ENOTSUP;
        goto exit;
    }
    if (flat && zeroed_grain) {
        error_setg(errp, "Flat image can't enable zeroed grain");
        ret = -ENOTSUP;
        goto exit;
    }

    /* Create extents */
    if (split) {
        extent_size = split_size;
    } else {
        extent_size = size;
    }
    if (!split && !flat) {
        created_size = extent_size;
    } else {
        created_size = 0;
    }
    /* Get the descriptor file BDS */
    blk = extent_fn(created_size, 0, flat, split, compress, zeroed_grain,
                    opaque, errp);
    if (!blk) {
        ret = -EIO;
        goto exit;
    }
    if (!split && !flat) {
        vmdk_desc_add_extent(ext_desc_lines, extent_line_fmt, created_size,
                             blk_bs(blk)->filename);
    }

    if (backing_file) {
        BlockBackend *backing;
        char *full_backing =
            bdrv_get_full_backing_filename_from_filename(blk_bs(blk)->filename,
                                                         backing_file,
                                                         &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            ret = -ENOENT;
            goto exit;
        }
        assert(full_backing);

        backing = blk_new_open(full_backing, NULL, NULL,
                               BDRV_O_NO_BACKING, errp);
        g_free(full_backing);
        if (backing == NULL) {
            ret = -EIO;
            goto exit;
        }
        if (strcmp(blk_bs(backing)->drv->format_name, "vmdk")) {
            error_setg(errp, "Invalid backing file format: %s. Must be vmdk",
                       blk_bs(backing)->drv->format_name);
            blk_unref(backing);
            ret = -EINVAL;
            goto exit;
        }
        ret = vmdk_read_cid(blk_bs(backing), 0, &parent_cid);
        blk_unref(backing);
        if (ret) {
            error_setg(errp, "Failed to read parent CID");
            goto exit;
        }
        snprintf(parent_desc_line, BUF_SIZE,
                "parentFileNameHint=\"%s\"", backing_file);
    }
    extent_idx = 1;
    while (created_size < size) {
        int64_t cur_size = MIN(size - created_size, extent_size);
        extent_blk = extent_fn(cur_size, extent_idx, flat, split, compress,
                               zeroed_grain, opaque, errp);
        if (!extent_blk) {
            ret = -EINVAL;
            goto exit;
        }
        vmdk_desc_add_extent(ext_desc_lines, extent_line_fmt, cur_size,
                             blk_bs(extent_blk)->filename);
        created_size += cur_size;
        extent_idx++;
        blk_unref(extent_blk);
    }

    /* Check whether we got excess extents */
    extent_blk = extent_fn(-1, extent_idx, flat, split, compress, zeroed_grain,
                           opaque, NULL);
    if (extent_blk) {
        blk_unref(extent_blk);
        error_setg(errp, "List of extents contains unused extents");
        ret = -EINVAL;
        goto exit;
    }

    /* generate descriptor file */
    desc = g_strdup_printf(desc_template,
                           g_random_int(),
                           parent_cid,
                           BlockdevVmdkSubformat_str(subformat),
                           parent_desc_line,
                           ext_desc_lines->str,
                           hw_version,
                           size /
                               (int64_t)(63 * number_heads * BDRV_SECTOR_SIZE),
                           number_heads,
                           BlockdevVmdkAdapterType_str(adapter_type),
                           toolsversion);
    desc_len = strlen(desc);
    /* the descriptor offset = 0x200 */
    if (!split && !flat) {
        desc_offset = 0x200;
    }

    ret = blk_pwrite(blk, desc_offset, desc, desc_len, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write description");
        goto exit;
    }
    /* bdrv_pwrite write padding zeros to align to sector, we don't need that
     * for description file */
    if (desc_offset == 0) {
        ret = blk_truncate(blk, desc_len, false, PREALLOC_MODE_OFF, 0, errp);
        if (ret < 0) {
            goto exit;
        }
    }
    ret = 0;
exit:
    if (blk) {
        blk_unref(blk);
    }
    g_free(desc);
    g_free(parent_desc_line);
    g_string_free(ext_desc_lines, true);
    return ret;
}

typedef struct {
    char *path;
    char *prefix;
    char *postfix;
    QemuOpts *opts;
} VMDKCreateOptsData;

static BlockBackend *vmdk_co_create_opts_cb(int64_t size, int idx,
                                            bool flat, bool split, bool compress,
                                            bool zeroed_grain, void *opaque,
                                            Error **errp)
{
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    VMDKCreateOptsData *data = opaque;
    char *ext_filename = NULL;
    char *rel_filename = NULL;

    /* We're done, don't create excess extents. */
    if (size == -1) {
        assert(errp == NULL);
        return NULL;
    }

    if (idx == 0) {
        rel_filename = g_strdup_printf("%s%s", data->prefix, data->postfix);
    } else if (split) {
        rel_filename = g_strdup_printf("%s-%c%03d%s",
                                       data->prefix,
                                       flat ? 'f' : 's', idx, data->postfix);
    } else {
        assert(idx == 1);
        rel_filename = g_strdup_printf("%s-flat%s", data->prefix, data->postfix);
    }

    ext_filename = g_strdup_printf("%s%s", data->path, rel_filename);
    g_free(rel_filename);

    if (vmdk_create_extent(ext_filename, size,
                           flat, compress, zeroed_grain, &blk, data->opts,
                           errp)) {
        goto exit;
    }
    bdrv_unref(bs);
exit:
    g_free(ext_filename);
    return blk;
}

static int coroutine_fn vmdk_co_create_opts(BlockDriver *drv,
                                            const char *filename,
                                            QemuOpts *opts,
                                            Error **errp)
{
    Error *local_err = NULL;
    char *desc = NULL;
    int64_t total_size = 0;
    char *adapter_type = NULL;
    BlockdevVmdkAdapterType adapter_type_enum;
    char *backing_file = NULL;
    char *hw_version = NULL;
    char *toolsversion = NULL;
    char *fmt = NULL;
    BlockdevVmdkSubformat subformat;
    int ret = 0;
    char *path = g_malloc0(PATH_MAX);
    char *prefix = g_malloc0(PATH_MAX);
    char *postfix = g_malloc0(PATH_MAX);
    char *desc_line = g_malloc0(BUF_SIZE);
    char *ext_filename = g_malloc0(PATH_MAX);
    char *desc_filename = g_malloc0(PATH_MAX);
    char *parent_desc_line = g_malloc0(BUF_SIZE);
    bool zeroed_grain;
    bool compat6;
    VMDKCreateOptsData data;
    char *backing_fmt = NULL;

    backing_fmt = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FMT);
    if (backing_fmt && strcmp(backing_fmt, "vmdk") != 0) {
        error_setg(errp, "backing_file must be a vmdk image");
        ret = -EINVAL;
        goto exit;
    }

    if (filename_decompose(filename, path, prefix, postfix, PATH_MAX, errp)) {
        ret = -EINVAL;
        goto exit;
    }
    /* Read out options */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);
    adapter_type = qemu_opt_get_del(opts, BLOCK_OPT_ADAPTER_TYPE);
    backing_file = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    hw_version = qemu_opt_get_del(opts, BLOCK_OPT_HWVERSION);
    toolsversion = qemu_opt_get_del(opts, BLOCK_OPT_TOOLSVERSION);
    compat6 = qemu_opt_get_bool_del(opts, BLOCK_OPT_COMPAT6, false);
    if (strcmp(hw_version, "undefined") == 0) {
        g_free(hw_version);
        hw_version = NULL;
    }
    fmt = qemu_opt_get_del(opts, BLOCK_OPT_SUBFMT);
    zeroed_grain = qemu_opt_get_bool_del(opts, BLOCK_OPT_ZEROED_GRAIN, false);

    if (adapter_type) {
        adapter_type_enum = qapi_enum_parse(&BlockdevVmdkAdapterType_lookup,
                                            adapter_type,
                                            BLOCKDEV_VMDK_ADAPTER_TYPE_IDE,
                                            &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto exit;
        }
    } else {
        adapter_type_enum = BLOCKDEV_VMDK_ADAPTER_TYPE_IDE;
    }

    if (!fmt) {
        /* Default format to monolithicSparse */
        subformat = BLOCKDEV_VMDK_SUBFORMAT_MONOLITHICSPARSE;
    } else {
        subformat = qapi_enum_parse(&BlockdevVmdkSubformat_lookup,
                                    fmt,
                                    BLOCKDEV_VMDK_SUBFORMAT_MONOLITHICSPARSE,
                                    &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            ret = -EINVAL;
            goto exit;
        }
    }
    data = (VMDKCreateOptsData){
        .prefix = prefix,
        .postfix = postfix,
        .path = path,
        .opts = opts,
    };
    ret = vmdk_co_do_create(total_size, subformat, adapter_type_enum,
                            backing_file, hw_version, toolsversion, compat6,
                            zeroed_grain, vmdk_co_create_opts_cb, &data, errp);

exit:
    g_free(backing_fmt);
    g_free(adapter_type);
    g_free(backing_file);
    g_free(hw_version);
    g_free(toolsversion);
    g_free(fmt);
    g_free(desc);
    g_free(path);
    g_free(prefix);
    g_free(postfix);
    g_free(desc_line);
    g_free(ext_filename);
    g_free(desc_filename);
    g_free(parent_desc_line);
    return ret;
}

static BlockBackend *vmdk_co_create_cb(int64_t size, int idx,
                                       bool flat, bool split, bool compress,
                                       bool zeroed_grain, void *opaque,
                                       Error **errp)
{
    int ret;
    BlockDriverState *bs;
    BlockBackend *blk;
    BlockdevCreateOptionsVmdk *opts = opaque;

    if (idx == 0) {
        bs = bdrv_open_blockdev_ref(opts->file, errp);
    } else {
        int i;
        BlockdevRefList *list = opts->extents;
        for (i = 1; i < idx; i++) {
            if (!list || !list->next) {
                error_setg(errp, "Extent [%d] not specified", i);
                return NULL;
            }
            list = list->next;
        }
        if (!list) {
            error_setg(errp, "Extent [%d] not specified", idx - 1);
            return NULL;
        }
        bs = bdrv_open_blockdev_ref(list->value, errp);
    }
    if (!bs) {
        return NULL;
    }
    blk = blk_new_with_bs(bs,
                          BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE | BLK_PERM_RESIZE,
                          BLK_PERM_ALL, errp);
    if (!blk) {
        return NULL;
    }
    blk_set_allow_write_beyond_eof(blk, true);
    bdrv_unref(bs);

    if (size != -1) {
        ret = vmdk_init_extent(blk, size, flat, compress, zeroed_grain, errp);
        if (ret) {
            blk_unref(blk);
            blk = NULL;
        }
    }
    return blk;
}

static int coroutine_fn vmdk_co_create(BlockdevCreateOptions *create_options,
                                       Error **errp)
{
    int ret;
    BlockdevCreateOptionsVmdk *opts;

    opts = &create_options->u.vmdk;

    /* Validate options */
    if (!QEMU_IS_ALIGNED(opts->size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Image size must be a multiple of 512 bytes");
        ret = -EINVAL;
        goto out;
    }

    ret = vmdk_co_do_create(opts->size,
                            opts->subformat,
                            opts->adapter_type,
                            opts->backing_file,
                            opts->hwversion,
                            opts->toolsversion,
                            false,
                            opts->zeroed_grain,
                            vmdk_co_create_cb,
                            opts, errp);
    return ret;

out:
    return ret;
}

static void vmdk_close(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;

    vmdk_free_extents(bs);
    g_free(s->create_type);

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static int64_t vmdk_get_allocated_file_size(BlockDriverState *bs)
{
    int i;
    int64_t ret = 0;
    int64_t r;
    BDRVVmdkState *s = bs->opaque;

    ret = bdrv_get_allocated_file_size(bs->file->bs);
    if (ret < 0) {
        return ret;
    }
    for (i = 0; i < s->num_extents; i++) {
        if (s->extents[i].file == bs->file) {
            continue;
        }
        r = bdrv_get_allocated_file_size(s->extents[i].file->bs);
        if (r < 0) {
            return r;
        }
        ret += r;
    }
    return ret;
}

static int vmdk_has_zero_init(BlockDriverState *bs)
{
    int i;
    BDRVVmdkState *s = bs->opaque;

    /* If has a flat extent and its underlying storage doesn't have zero init,
     * return 0. */
    for (i = 0; i < s->num_extents; i++) {
        if (s->extents[i].flat) {
            if (!bdrv_has_zero_init(s->extents[i].file->bs)) {
                return 0;
            }
        }
    }
    return 1;
}

static ImageInfo *vmdk_get_extent_info(VmdkExtent *extent)
{
    ImageInfo *info = g_new0(ImageInfo, 1);

    bdrv_refresh_filename(extent->file->bs);
    *info = (ImageInfo){
        .filename         = g_strdup(extent->file->bs->filename),
        .format           = g_strdup(extent->type),
        .virtual_size     = extent->sectors * BDRV_SECTOR_SIZE,
        .compressed       = extent->compressed,
        .has_compressed   = extent->compressed,
        .cluster_size     = extent->cluster_sectors * BDRV_SECTOR_SIZE,
        .has_cluster_size = !extent->flat,
    };

    return info;
}

static int coroutine_fn vmdk_co_check(BlockDriverState *bs,
                                      BdrvCheckResult *result,
                                      BdrvCheckMode fix)
{
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    int64_t sector_num = 0;
    int64_t total_sectors = bdrv_nb_sectors(bs);
    int ret;
    uint64_t cluster_offset;

    if (fix) {
        return -ENOTSUP;
    }

    for (;;) {
        if (sector_num >= total_sectors) {
            return 0;
        }
        extent = find_extent(s, sector_num, extent);
        if (!extent) {
            fprintf(stderr,
                    "ERROR: could not find extent for sector %" PRId64 "\n",
                    sector_num);
            ret = -EINVAL;
            break;
        }
        ret = get_cluster_offset(bs, extent, NULL,
                                 sector_num << BDRV_SECTOR_BITS,
                                 false, &cluster_offset, 0, 0);
        if (ret == VMDK_ERROR) {
            fprintf(stderr,
                    "ERROR: could not get cluster_offset for sector %"
                    PRId64 "\n", sector_num);
            break;
        }
        if (ret == VMDK_OK) {
            int64_t extent_len = bdrv_getlength(extent->file->bs);
            if (extent_len < 0) {
                fprintf(stderr,
                        "ERROR: could not get extent file length for sector %"
                        PRId64 "\n", sector_num);
                ret = extent_len;
                break;
            }
            if (cluster_offset >= extent_len) {
                fprintf(stderr,
                        "ERROR: cluster offset for sector %"
                        PRId64 " points after EOF\n", sector_num);
                ret = -EINVAL;
                break;
            }
        }
        sector_num += extent->cluster_sectors;
    }

    result->corruptions++;
    return ret;
}

static ImageInfoSpecific *vmdk_get_specific_info(BlockDriverState *bs,
                                                 Error **errp)
{
    int i;
    BDRVVmdkState *s = bs->opaque;
    ImageInfoSpecific *spec_info = g_new0(ImageInfoSpecific, 1);
    ImageInfoList **tail;

    *spec_info = (ImageInfoSpecific){
        .type = IMAGE_INFO_SPECIFIC_KIND_VMDK,
        .u = {
            .vmdk.data = g_new0(ImageInfoSpecificVmdk, 1),
        },
    };

    *spec_info->u.vmdk.data = (ImageInfoSpecificVmdk) {
        .create_type = g_strdup(s->create_type),
        .cid = s->cid,
        .parent_cid = s->parent_cid,
    };

    tail = &spec_info->u.vmdk.data->extents;
    for (i = 0; i < s->num_extents; i++) {
        QAPI_LIST_APPEND(tail, vmdk_get_extent_info(&s->extents[i]));
    }

    return spec_info;
}

static bool vmdk_extents_type_eq(const VmdkExtent *a, const VmdkExtent *b)
{
    return a->flat == b->flat &&
           a->compressed == b->compressed &&
           (a->flat || a->cluster_sectors == b->cluster_sectors);
}

static int vmdk_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    int i;
    BDRVVmdkState *s = bs->opaque;
    assert(s->num_extents);

    /* See if we have multiple extents but they have different cases */
    for (i = 1; i < s->num_extents; i++) {
        if (!vmdk_extents_type_eq(&s->extents[0], &s->extents[i])) {
            return -ENOTSUP;
        }
    }
    bdi->needs_compressed_writes = s->extents[0].compressed;
    if (!s->extents[0].flat) {
        bdi->cluster_size = s->extents[0].cluster_sectors << BDRV_SECTOR_BITS;
    }
    return 0;
}

static void vmdk_gather_child_options(BlockDriverState *bs, QDict *target,
                                      bool backing_overridden)
{
    /* No children but file and backing can be explicitly specified (TODO) */
    qdict_put(target, "file",
              qobject_ref(bs->file->bs->full_open_options));

    if (backing_overridden) {
        if (bs->backing) {
            qdict_put(target, "backing",
                      qobject_ref(bs->backing->bs->full_open_options));
        } else {
            qdict_put_null(target, "backing");
        }
    }
}

static QemuOptsList vmdk_create_opts = {
    .name = "vmdk-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(vmdk_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_ADAPTER_TYPE,
            .type = QEMU_OPT_STRING,
            .help = "Virtual adapter type, can be one of "
                    "ide (default), lsilogic, buslogic or legacyESX"
        },
        {
            .name = BLOCK_OPT_BACKING_FILE,
            .type = QEMU_OPT_STRING,
            .help = "File name of a base image"
        },
        {
            .name = BLOCK_OPT_BACKING_FMT,
            .type = QEMU_OPT_STRING,
            .help = "Must be 'vmdk' if present",
        },
        {
            .name = BLOCK_OPT_COMPAT6,
            .type = QEMU_OPT_BOOL,
            .help = "VMDK version 6 image",
            .def_value_str = "off"
        },
        {
            .name = BLOCK_OPT_HWVERSION,
            .type = QEMU_OPT_STRING,
            .help = "VMDK hardware version",
            .def_value_str = "undefined"
        },
        {
            .name = BLOCK_OPT_TOOLSVERSION,
            .type = QEMU_OPT_STRING,
            .help = "VMware guest tools version",
        },
        {
            .name = BLOCK_OPT_SUBFMT,
            .type = QEMU_OPT_STRING,
            .help =
                "VMDK flat extent format, can be one of "
                "{monolithicSparse (default) | monolithicFlat | twoGbMaxExtentSparse | twoGbMaxExtentFlat | streamOptimized} "
        },
        {
            .name = BLOCK_OPT_ZEROED_GRAIN,
            .type = QEMU_OPT_BOOL,
            .help = "Enable efficient zero writes "
                    "using the zeroed-grain GTE feature"
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_vmdk = {
    .format_name                  = "vmdk",
    .instance_size                = sizeof(BDRVVmdkState),
    .bdrv_probe                   = vmdk_probe,
    .bdrv_open                    = vmdk_open,
    .bdrv_co_check                = vmdk_co_check,
    .bdrv_reopen_prepare          = vmdk_reopen_prepare,
    .bdrv_child_perm              = bdrv_default_perms,
    .bdrv_co_preadv               = vmdk_co_preadv,
    .bdrv_co_pwritev              = vmdk_co_pwritev,
    .bdrv_co_pwritev_compressed   = vmdk_co_pwritev_compressed,
    .bdrv_co_pwrite_zeroes        = vmdk_co_pwrite_zeroes,
    .bdrv_close                   = vmdk_close,
    .bdrv_co_create_opts          = vmdk_co_create_opts,
    .bdrv_co_create               = vmdk_co_create,
    .bdrv_co_block_status         = vmdk_co_block_status,
    .bdrv_get_allocated_file_size = vmdk_get_allocated_file_size,
    .bdrv_has_zero_init           = vmdk_has_zero_init,
    .bdrv_get_specific_info       = vmdk_get_specific_info,
    .bdrv_refresh_limits          = vmdk_refresh_limits,
    .bdrv_get_info                = vmdk_get_info,
    .bdrv_gather_child_options    = vmdk_gather_child_options,

    .is_format                    = true,
    .supports_backing             = true,
    .create_opts                  = &vmdk_create_opts,
};

static void bdrv_vmdk_init(void)
{
    bdrv_register(&bdrv_vmdk);
}

block_init(bdrv_vmdk_init);
