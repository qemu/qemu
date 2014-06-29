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

#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "migration/migration.h"
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

#define VMDK_GTE_ZEROED 0x1

/* VMDK internal error codes */
#define VMDK_OK      0
#define VMDK_ERROR   (-1)
/* Cluster not allocated */
#define VMDK_UNALLOC (-2)
#define VMDK_ZEROED  (-3)

#define BLOCK_OPT_ZEROED_GRAIN "zeroed_grain"

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

#define L2_CACHE_SIZE 16

typedef struct VmdkExtent {
    BlockDriverState *file;
    bool flat;
    bool compressed;
    bool has_marker;
    bool has_zero_grain;
    int version;
    int64_t sectors;
    int64_t end_sector;
    int64_t flat_start_offset;
    int64_t l1_table_offset;
    int64_t l1_backup_table_offset;
    uint32_t *l1_table;
    uint32_t *l1_backup_table;
    unsigned int l1_size;
    uint32_t l1_entry_sectors;

    unsigned int l2_size;
    uint32_t *l2_cache;
    uint32_t l2_cache_offsets[L2_CACHE_SIZE];
    uint32_t l2_cache_counts[L2_CACHE_SIZE];

    int64_t cluster_sectors;
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
    uint32_t offset;
    unsigned int l1_index;
    unsigned int l2_index;
    unsigned int l2_offset;
    int valid;
    uint32_t *l2_cache_entry;
} VmdkMetaData;

typedef struct VmdkGrainMarker {
    uint64_t lba;
    uint32_t size;
    uint8_t  data[0];
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
                    strncmp("version=2\n", p, strlen("version=2\n")) == 0) {
                    return 100;
                }
            }
            if (end - p >= strlen("version=X\r\n")) {
                if (strncmp("version=1\r\n", p, strlen("version=1\r\n")) == 0 ||
                    strncmp("version=2\r\n", p, strlen("version=2\r\n")) == 0) {
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
            bdrv_unref(e->file);
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
    s->extents = g_realloc(s->extents, s->num_extents * sizeof(VmdkExtent));
}

static uint32_t vmdk_read_cid(BlockDriverState *bs, int parent)
{
    char desc[DESC_SIZE];
    uint32_t cid = 0xffffffff;
    const char *p_name, *cid_str;
    size_t cid_str_size;
    BDRVVmdkState *s = bs->opaque;
    int ret;

    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        return 0;
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
    if (p_name != NULL) {
        p_name += cid_str_size;
        sscanf(p_name, "%" SCNx32, &cid);
    }

    return cid;
}

static int vmdk_write_cid(BlockDriverState *bs, uint32_t cid)
{
    char desc[DESC_SIZE], tmp_desc[DESC_SIZE];
    char *p_name, *tmp_str;
    BDRVVmdkState *s = bs->opaque;
    int ret;

    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        return ret;
    }

    desc[DESC_SIZE - 1] = '\0';
    tmp_str = strstr(desc, "parentCID");
    if (tmp_str == NULL) {
        return -EINVAL;
    }

    pstrcpy(tmp_desc, sizeof(tmp_desc), tmp_str);
    p_name = strstr(desc, "CID");
    if (p_name != NULL) {
        p_name += sizeof("CID");
        snprintf(p_name, sizeof(desc) - (p_name - desc), "%" PRIx32 "\n", cid);
        pstrcat(desc, sizeof(desc), tmp_desc);
    }

    ret = bdrv_pwrite_sync(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int vmdk_is_cid_valid(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    BlockDriverState *p_bs = bs->backing_hd;
    uint32_t cur_pcid;

    if (!s->cid_checked && p_bs) {
        cur_pcid = vmdk_read_cid(p_bs, 0);
        if (s->parent_cid != cur_pcid) {
            /* CID not valid */
            return 0;
        }
    }
    s->cid_checked = true;
    /* CID valid */
    return 1;
}

/* Queue extents, if any, for reopen() */
static int vmdk_reopen_prepare(BDRVReopenState *state,
                               BlockReopenQueue *queue, Error **errp)
{
    BDRVVmdkState *s;
    int ret = -1;
    int i;
    VmdkExtent *e;

    assert(state != NULL);
    assert(state->bs != NULL);

    if (queue == NULL) {
        error_setg(errp, "No reopen queue for VMDK extents");
        goto exit;
    }

    s = state->bs->opaque;

    assert(s != NULL);

    for (i = 0; i < s->num_extents; i++) {
        e = &s->extents[i];
        if (e->file != state->bs->file) {
            bdrv_reopen_queue(queue, e->file, state->flags);
        }
    }
    ret = 0;

exit:
    return ret;
}

static int vmdk_parent_open(BlockDriverState *bs)
{
    char *p_name;
    char desc[DESC_SIZE + 1];
    BDRVVmdkState *s = bs->opaque;
    int ret;

    desc[DESC_SIZE] = '\0';
    ret = bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE);
    if (ret < 0) {
        return ret;
    }

    p_name = strstr(desc, "parentFileNameHint");
    if (p_name != NULL) {
        char *end_name;

        p_name += sizeof("parentFileNameHint") + 1;
        end_name = strchr(p_name, '\"');
        if (end_name == NULL) {
            return -EINVAL;
        }
        if ((end_name - p_name) > sizeof(bs->backing_file) - 1) {
            return -EINVAL;
        }

        pstrcpy(bs->backing_file, end_name - p_name + 1, p_name);
    }

    return 0;
}

/* Create and append extent to the extent array. Return the added VmdkExtent
 * address. return NULL if allocation failed. */
static int vmdk_add_extent(BlockDriverState *bs,
                           BlockDriverState *file, bool flat, int64_t sectors,
                           int64_t l1_offset, int64_t l1_backup_offset,
                           uint32_t l1_size,
                           int l2_size, uint64_t cluster_sectors,
                           VmdkExtent **new_extent,
                           Error **errp)
{
    VmdkExtent *extent;
    BDRVVmdkState *s = bs->opaque;

    if (cluster_sectors > 0x200000) {
        /* 0x200000 * 512Bytes = 1GB for one cluster is unrealistic */
        error_setg(errp, "Invalid granularity, image may be corrupt");
        return -EFBIG;
    }
    if (l1_size > 512 * 1024 * 1024) {
        /* Although with big capacity and small l1_entry_sectors, we can get a
         * big l1_size, we don't want unbounded value to allocate the table.
         * Limit it to 512M, which is 16PB for default cluster and L2 table
         * size */
        error_setg(errp, "L1 size too big");
        return -EFBIG;
    }

    s->extents = g_realloc(s->extents,
                              (s->num_extents + 1) * sizeof(VmdkExtent));
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
    int l1_size, i;

    /* read the L1 table */
    l1_size = extent->l1_size * sizeof(uint32_t);
    extent->l1_table = g_malloc(l1_size);
    ret = bdrv_pread(extent->file,
                     extent->l1_table_offset,
                     extent->l1_table,
                     l1_size);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Could not read l1 table from extent '%s'",
                         extent->file->filename);
        goto fail_l1;
    }
    for (i = 0; i < extent->l1_size; i++) {
        le32_to_cpus(&extent->l1_table[i]);
    }

    if (extent->l1_backup_table_offset) {
        extent->l1_backup_table = g_malloc(l1_size);
        ret = bdrv_pread(extent->file,
                         extent->l1_backup_table_offset,
                         extent->l1_backup_table,
                         l1_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Could not read l1 backup table from extent '%s'",
                             extent->file->filename);
            goto fail_l1b;
        }
        for (i = 0; i < extent->l1_size; i++) {
            le32_to_cpus(&extent->l1_backup_table[i]);
        }
    }

    extent->l2_cache =
        g_malloc(extent->l2_size * L2_CACHE_SIZE * sizeof(uint32_t));
    return 0;
 fail_l1b:
    g_free(extent->l1_backup_table);
 fail_l1:
    g_free(extent->l1_table);
    return ret;
}

static int vmdk_open_vmfs_sparse(BlockDriverState *bs,
                                 BlockDriverState *file,
                                 int flags, Error **errp)
{
    int ret;
    uint32_t magic;
    VMDK3Header header;
    VmdkExtent *extent;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Could not read header from file '%s'",
                         file->filename);
        return ret;
    }
    ret = vmdk_add_extent(bs, file, false,
                          le32_to_cpu(header.disk_sectors),
                          le32_to_cpu(header.l1dir_offset) << 9,
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

static int vmdk_open_desc_file(BlockDriverState *bs, int flags, char *buf,
                               Error **errp);

static char *vmdk_read_desc(BlockDriverState *file, uint64_t desc_offset,
                            Error **errp)
{
    int64_t size;
    char *buf;
    int ret;

    size = bdrv_getlength(file);
    if (size < 0) {
        error_setg_errno(errp, -size, "Could not access file");
        return NULL;
    }

    size = MIN(size, 1 << 20);  /* avoid unbounded allocation */
    buf = g_malloc0(size + 1);

    ret = bdrv_pread(file, desc_offset, buf, size);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read from file");
        g_free(buf);
        return NULL;
    }

    return buf;
}

static int vmdk_open_vmdk4(BlockDriverState *bs,
                           BlockDriverState *file,
                           int flags, Error **errp)
{
    int ret;
    uint32_t magic;
    uint32_t l1_size, l1_entry_sectors;
    VMDK4Header header;
    VmdkExtent *extent;
    BDRVVmdkState *s = bs->opaque;
    int64_t l1_backup_offset = 0;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Could not read header from file '%s'",
                         file->filename);
        return -EINVAL;
    }
    if (header.capacity == 0) {
        uint64_t desc_offset = le64_to_cpu(header.desc_offset);
        if (desc_offset) {
            char *buf = vmdk_read_desc(file, desc_offset << 9, errp);
            if (!buf) {
                return -EINVAL;
            }
            ret = vmdk_open_desc_file(bs, flags, buf, errp);
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
            bs->file->total_sectors * 512 - 1536,
            &footer, sizeof(footer));
        if (ret < 0) {
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
            return -EINVAL;
        }

        header = footer.header;
    }

    if (le32_to_cpu(header.version) > 3) {
        char buf[64];
        snprintf(buf, sizeof(buf), "VMDK version %" PRId32,
                 le32_to_cpu(header.version));
        error_set(errp, QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
                  bs->device_name, "vmdk", buf);
        return -ENOTSUP;
    } else if (le32_to_cpu(header.version) == 3 && (flags & BDRV_O_RDWR)) {
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
        return -EINVAL;
    }
    l1_size = (le64_to_cpu(header.capacity) + l1_entry_sectors - 1)
                / l1_entry_sectors;
    if (le32_to_cpu(header.flags) & VMDK4_FLAG_RGD) {
        l1_backup_offset = le64_to_cpu(header.rgd_offset) << 9;
    }
    if (bdrv_getlength(file) <
            le64_to_cpu(header.grain_offset) * BDRV_SECTOR_SIZE) {
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
static int vmdk_open_sparse(BlockDriverState *bs,
                            BlockDriverState *file, int flags,
                            char *buf, Error **errp)
{
    uint32_t magic;

    magic = ldl_be_p(buf);
    switch (magic) {
        case VMDK3_MAGIC:
            return vmdk_open_vmfs_sparse(bs, file, flags, errp);
            break;
        case VMDK4_MAGIC:
            return vmdk_open_vmdk4(bs, file, flags, errp);
            break;
        default:
            error_setg(errp, "Image not in VMDK format");
            return -EINVAL;
            break;
    }
}

static int vmdk_parse_extents(const char *desc, BlockDriverState *bs,
                              const char *desc_file_path, Error **errp)
{
    int ret;
    char access[11];
    char type[11];
    char fname[512];
    const char *p = desc;
    int64_t sectors = 0;
    int64_t flat_offset;
    char extent_path[PATH_MAX];
    BlockDriverState *extent_file;
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent;

    while (*p) {
        /* parse extent line:
         * RW [size in sectors] FLAT "file-name.vmdk" OFFSET
         * or
         * RW [size in sectors] SPARSE "file-name.vmdk"
         */
        flat_offset = -1;
        ret = sscanf(p, "%10s %" SCNd64 " %10s \"%511[^\n\r\"]\" %" SCNd64,
                access, &sectors, type, fname, &flat_offset);
        if (ret < 4 || strcmp(access, "RW")) {
            goto next_line;
        } else if (!strcmp(type, "FLAT")) {
            if (ret != 5 || flat_offset < 0) {
                error_setg(errp, "Invalid extent lines: \n%s", p);
                return -EINVAL;
            }
        } else if (!strcmp(type, "VMFS")) {
            if (ret == 4) {
                flat_offset = 0;
            } else {
                error_setg(errp, "Invalid extent lines:\n%s", p);
                return -EINVAL;
            }
        } else if (ret != 4) {
            error_setg(errp, "Invalid extent lines:\n%s", p);
            return -EINVAL;
        }

        if (sectors <= 0 ||
            (strcmp(type, "FLAT") && strcmp(type, "SPARSE") &&
             strcmp(type, "VMFS") && strcmp(type, "VMFSSPARSE")) ||
            (strcmp(access, "RW"))) {
            goto next_line;
        }

        path_combine(extent_path, sizeof(extent_path),
                desc_file_path, fname);
        extent_file = NULL;
        ret = bdrv_open(&extent_file, extent_path, NULL, NULL,
                        bs->open_flags | BDRV_O_PROTOCOL, NULL, errp);
        if (ret) {
            return ret;
        }

        /* save to extents array */
        if (!strcmp(type, "FLAT") || !strcmp(type, "VMFS")) {
            /* FLAT extent */

            ret = vmdk_add_extent(bs, extent_file, true, sectors,
                            0, 0, 0, 0, 0, &extent, errp);
            if (ret < 0) {
                return ret;
            }
            extent->flat_start_offset = flat_offset << 9;
        } else if (!strcmp(type, "SPARSE") || !strcmp(type, "VMFSSPARSE")) {
            /* SPARSE extent and VMFSSPARSE extent are both "COWD" sparse file*/
            char *buf = vmdk_read_desc(extent_file, 0, errp);
            if (!buf) {
                ret = -EINVAL;
            } else {
                ret = vmdk_open_sparse(bs, extent_file, bs->open_flags, buf, errp);
            }
            if (ret) {
                g_free(buf);
                bdrv_unref(extent_file);
                return ret;
            }
            extent = &s->extents[s->num_extents - 1];
        } else {
            error_setg(errp, "Unsupported extent type '%s'", type);
            return -ENOTSUP;
        }
        extent->type = g_strdup(type);
next_line:
        /* move to next line */
        while (*p) {
            if (*p == '\n') {
                p++;
                break;
            }
            p++;
        }
    }
    return 0;
}

static int vmdk_open_desc_file(BlockDriverState *bs, int flags, char *buf,
                               Error **errp)
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
        strcmp(ct, "twoGbMaxExtentSparse") &&
        strcmp(ct, "twoGbMaxExtentFlat")) {
        error_setg(errp, "Unsupported image type '%s'", ct);
        ret = -ENOTSUP;
        goto exit;
    }
    s->create_type = g_strdup(ct);
    s->desc_offset = 0;
    ret = vmdk_parse_extents(buf, bs, bs->file->filename, errp);
exit:
    return ret;
}

static int vmdk_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    char *buf = NULL;
    int ret;
    BDRVVmdkState *s = bs->opaque;
    uint32_t magic;

    buf = vmdk_read_desc(bs->file, 0, errp);
    if (!buf) {
        return -EINVAL;
    }

    magic = ldl_be_p(buf);
    switch (magic) {
        case VMDK3_MAGIC:
        case VMDK4_MAGIC:
            ret = vmdk_open_sparse(bs, bs->file, flags, buf, errp);
            s->desc_offset = 0x200;
            break;
        default:
            ret = vmdk_open_desc_file(bs, flags, buf, errp);
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
    s->cid = vmdk_read_cid(bs, 0);
    s->parent_cid = vmdk_read_cid(bs, 1);
    qemu_co_mutex_init(&s->lock);

    /* Disable migration when VMDK images are used */
    error_set(&s->migration_blocker,
              QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
              "vmdk", bs->device_name, "live migration");
    migrate_add_blocker(s->migration_blocker);
    g_free(buf);
    return 0;

fail:
    g_free(buf);
    g_free(s->create_type);
    s->create_type = NULL;
    vmdk_free_extents(bs);
    return ret;
}


static int vmdk_refresh_limits(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_extents; i++) {
        if (!s->extents[i].flat) {
            bs->bl.write_zeroes_alignment =
                MAX(bs->bl.write_zeroes_alignment,
                    s->extents[i].cluster_sectors);
        }
    }

    return 0;
}

static int get_whole_cluster(BlockDriverState *bs,
                VmdkExtent *extent,
                uint64_t cluster_offset,
                uint64_t offset,
                bool allocate)
{
    int ret = VMDK_OK;
    uint8_t *whole_grain = NULL;

    /* we will be here if it's first write on non-exist grain(cluster).
     * try to read from parent image, if exist */
    if (bs->backing_hd) {
        whole_grain =
            qemu_blockalign(bs, extent->cluster_sectors << BDRV_SECTOR_BITS);
        if (!vmdk_is_cid_valid(bs)) {
            ret = VMDK_ERROR;
            goto exit;
        }

        /* floor offset to cluster */
        offset -= offset % (extent->cluster_sectors * 512);
        ret = bdrv_read(bs->backing_hd, offset >> 9, whole_grain,
                extent->cluster_sectors);
        if (ret < 0) {
            ret = VMDK_ERROR;
            goto exit;
        }

        /* Write grain only into the active image */
        ret = bdrv_write(extent->file, cluster_offset, whole_grain,
                extent->cluster_sectors);
        if (ret < 0) {
            ret = VMDK_ERROR;
            goto exit;
        }
    }
exit:
    qemu_vfree(whole_grain);
    return ret;
}

static int vmdk_L2update(VmdkExtent *extent, VmdkMetaData *m_data)
{
    uint32_t offset;
    QEMU_BUILD_BUG_ON(sizeof(offset) != sizeof(m_data->offset));
    offset = cpu_to_le32(m_data->offset);
    /* update L2 table */
    if (bdrv_pwrite_sync(
                extent->file,
                ((int64_t)m_data->l2_offset * 512)
                    + (m_data->l2_index * sizeof(m_data->offset)),
                &offset, sizeof(offset)) < 0) {
        return VMDK_ERROR;
    }
    /* update backup L2 table */
    if (extent->l1_backup_table_offset != 0) {
        m_data->l2_offset = extent->l1_backup_table[m_data->l1_index];
        if (bdrv_pwrite_sync(
                    extent->file,
                    ((int64_t)m_data->l2_offset * 512)
                        + (m_data->l2_index * sizeof(m_data->offset)),
                    &offset, sizeof(offset)) < 0) {
            return VMDK_ERROR;
        }
    }
    if (m_data->l2_cache_entry) {
        *m_data->l2_cache_entry = offset;
    }

    return VMDK_OK;
}

static int get_cluster_offset(BlockDriverState *bs,
                                    VmdkExtent *extent,
                                    VmdkMetaData *m_data,
                                    uint64_t offset,
                                    int allocate,
                                    uint64_t *cluster_offset)
{
    unsigned int l1_index, l2_offset, l2_index;
    int min_index, i, j;
    uint32_t min_count, *l2_table;
    bool zeroed = false;

    if (m_data) {
        m_data->valid = 0;
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
    l2_offset = extent->l1_table[l1_index];
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
            l2_table = extent->l2_cache + (i * extent->l2_size);
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
    l2_table = extent->l2_cache + (min_index * extent->l2_size);
    if (bdrv_pread(
                extent->file,
                (int64_t)l2_offset * 512,
                l2_table,
                extent->l2_size * sizeof(uint32_t)
            ) != extent->l2_size * sizeof(uint32_t)) {
        return VMDK_ERROR;
    }

    extent->l2_cache_offsets[min_index] = l2_offset;
    extent->l2_cache_counts[min_index] = 1;
 found:
    l2_index = ((offset >> 9) / extent->cluster_sectors) % extent->l2_size;
    *cluster_offset = le32_to_cpu(l2_table[l2_index]);

    if (m_data) {
        m_data->valid = 1;
        m_data->l1_index = l1_index;
        m_data->l2_index = l2_index;
        m_data->offset = *cluster_offset;
        m_data->l2_offset = l2_offset;
        m_data->l2_cache_entry = &l2_table[l2_index];
    }
    if (extent->has_zero_grain && *cluster_offset == VMDK_GTE_ZEROED) {
        zeroed = true;
    }

    if (!*cluster_offset || zeroed) {
        if (!allocate) {
            return zeroed ? VMDK_ZEROED : VMDK_UNALLOC;
        }

        /* Avoid the L2 tables update for the images that have snapshots. */
        *cluster_offset = bdrv_getlength(extent->file);
        if (!extent->compressed) {
            bdrv_truncate(
                extent->file,
                *cluster_offset + (extent->cluster_sectors << 9)
            );
        }

        *cluster_offset >>= 9;
        l2_table[l2_index] = cpu_to_le32(*cluster_offset);

        /* First of all we write grain itself, to avoid race condition
         * that may to corrupt the image.
         * This problem may occur because of insufficient space on host disk
         * or inappropriate VM shutdown.
         */
        if (get_whole_cluster(
                bs, extent, *cluster_offset, offset, allocate) == -1) {
            return VMDK_ERROR;
        }

        if (m_data) {
            m_data->offset = *cluster_offset;
        }
    }
    *cluster_offset <<= 9;
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

static int64_t coroutine_fn vmdk_co_get_block_status(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, int *pnum)
{
    BDRVVmdkState *s = bs->opaque;
    int64_t index_in_cluster, n, ret;
    uint64_t offset;
    VmdkExtent *extent;

    extent = find_extent(s, sector_num, NULL);
    if (!extent) {
        return 0;
    }
    qemu_co_mutex_lock(&s->lock);
    ret = get_cluster_offset(bs, extent, NULL,
                            sector_num * 512, 0, &offset);
    qemu_co_mutex_unlock(&s->lock);

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
        if (extent->file == bs->file && !extent->compressed) {
            ret |= BDRV_BLOCK_OFFSET_VALID | offset;
        }

        break;
    }

    index_in_cluster = sector_num % extent->cluster_sectors;
    n = extent->cluster_sectors - index_in_cluster;
    if (n > nb_sectors) {
        n = nb_sectors;
    }
    *pnum = n;
    return ret;
}

static int vmdk_write_extent(VmdkExtent *extent, int64_t cluster_offset,
                            int64_t offset_in_cluster, const uint8_t *buf,
                            int nb_sectors, int64_t sector_num)
{
    int ret;
    VmdkGrainMarker *data = NULL;
    uLongf buf_len;
    const uint8_t *write_buf = buf;
    int write_len = nb_sectors * 512;

    if (extent->compressed) {
        if (!extent->has_marker) {
            ret = -EINVAL;
            goto out;
        }
        buf_len = (extent->cluster_sectors << 9) * 2;
        data = g_malloc(buf_len + sizeof(VmdkGrainMarker));
        if (compress(data->data, &buf_len, buf, nb_sectors << 9) != Z_OK ||
                buf_len == 0) {
            ret = -EINVAL;
            goto out;
        }
        data->lba = sector_num;
        data->size = buf_len;
        write_buf = (uint8_t *)data;
        write_len = buf_len + sizeof(VmdkGrainMarker);
    }
    ret = bdrv_pwrite(extent->file,
                        cluster_offset + offset_in_cluster,
                        write_buf,
                        write_len);
    if (ret != write_len) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }
    ret = 0;
 out:
    g_free(data);
    return ret;
}

static int vmdk_read_extent(VmdkExtent *extent, int64_t cluster_offset,
                            int64_t offset_in_cluster, uint8_t *buf,
                            int nb_sectors)
{
    int ret;
    int cluster_bytes, buf_bytes;
    uint8_t *cluster_buf, *compressed_data;
    uint8_t *uncomp_buf;
    uint32_t data_len;
    VmdkGrainMarker *marker;
    uLongf buf_len;


    if (!extent->compressed) {
        ret = bdrv_pread(extent->file,
                          cluster_offset + offset_in_cluster,
                          buf, nb_sectors * 512);
        if (ret == nb_sectors * 512) {
            return 0;
        } else {
            return -EIO;
        }
    }
    cluster_bytes = extent->cluster_sectors * 512;
    /* Read two clusters in case GrainMarker + compressed data > one cluster */
    buf_bytes = cluster_bytes * 2;
    cluster_buf = g_malloc(buf_bytes);
    uncomp_buf = g_malloc(cluster_bytes);
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
            offset_in_cluster + nb_sectors * 512 > buf_len) {
        ret = -EINVAL;
        goto out;
    }
    memcpy(buf, uncomp_buf + offset_in_cluster, nb_sectors * 512);
    ret = 0;

 out:
    g_free(uncomp_buf);
    g_free(cluster_buf);
    return ret;
}

static int vmdk_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVmdkState *s = bs->opaque;
    int ret;
    uint64_t n, index_in_cluster;
    uint64_t extent_begin_sector, extent_relative_sector_num;
    VmdkExtent *extent = NULL;
    uint64_t cluster_offset;

    while (nb_sectors > 0) {
        extent = find_extent(s, sector_num, extent);
        if (!extent) {
            return -EIO;
        }
        ret = get_cluster_offset(
                            bs, extent, NULL,
                            sector_num << 9, 0, &cluster_offset);
        extent_begin_sector = extent->end_sector - extent->sectors;
        extent_relative_sector_num = sector_num - extent_begin_sector;
        index_in_cluster = extent_relative_sector_num % extent->cluster_sectors;
        n = extent->cluster_sectors - index_in_cluster;
        if (n > nb_sectors) {
            n = nb_sectors;
        }
        if (ret != VMDK_OK) {
            /* if not allocated, try to read from parent image, if exist */
            if (bs->backing_hd && ret != VMDK_ZEROED) {
                if (!vmdk_is_cid_valid(bs)) {
                    return -EINVAL;
                }
                ret = bdrv_read(bs->backing_hd, sector_num, buf, n);
                if (ret < 0) {
                    return ret;
                }
            } else {
                memset(buf, 0, 512 * n);
            }
        } else {
            ret = vmdk_read_extent(extent,
                            cluster_offset, index_in_cluster * 512,
                            buf, n);
            if (ret) {
                return ret;
            }
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;
    }
    return 0;
}

static coroutine_fn int vmdk_co_read(BlockDriverState *bs, int64_t sector_num,
                                     uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = vmdk_read(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);
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
static int vmdk_write(BlockDriverState *bs, int64_t sector_num,
                      const uint8_t *buf, int nb_sectors,
                      bool zeroed, bool zero_dry_run)
{
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    int ret;
    int64_t index_in_cluster, n;
    uint64_t extent_begin_sector, extent_relative_sector_num;
    uint64_t cluster_offset;
    VmdkMetaData m_data;

    if (sector_num > bs->total_sectors) {
        error_report("Wrong offset: sector_num=0x%" PRIx64
                " total_sectors=0x%" PRIx64 "\n",
                sector_num, bs->total_sectors);
        return -EIO;
    }

    while (nb_sectors > 0) {
        extent = find_extent(s, sector_num, extent);
        if (!extent) {
            return -EIO;
        }
        ret = get_cluster_offset(
                                bs,
                                extent,
                                &m_data,
                                sector_num << 9, !extent->compressed,
                                &cluster_offset);
        if (extent->compressed) {
            if (ret == VMDK_OK) {
                /* Refuse write to allocated cluster for streamOptimized */
                error_report("Could not write to allocated cluster"
                              " for streamOptimized");
                return -EIO;
            } else {
                /* allocate */
                ret = get_cluster_offset(
                                        bs,
                                        extent,
                                        &m_data,
                                        sector_num << 9, 1,
                                        &cluster_offset);
            }
        }
        if (ret == VMDK_ERROR) {
            return -EINVAL;
        }
        extent_begin_sector = extent->end_sector - extent->sectors;
        extent_relative_sector_num = sector_num - extent_begin_sector;
        index_in_cluster = extent_relative_sector_num % extent->cluster_sectors;
        n = extent->cluster_sectors - index_in_cluster;
        if (n > nb_sectors) {
            n = nb_sectors;
        }
        if (zeroed) {
            /* Do zeroed write, buf is ignored */
            if (extent->has_zero_grain &&
                    index_in_cluster == 0 &&
                    n >= extent->cluster_sectors) {
                n = extent->cluster_sectors;
                if (!zero_dry_run) {
                    m_data.offset = VMDK_GTE_ZEROED;
                    /* update L2 tables */
                    if (vmdk_L2update(extent, &m_data) != VMDK_OK) {
                        return -EIO;
                    }
                }
            } else {
                return -ENOTSUP;
            }
        } else {
            ret = vmdk_write_extent(extent,
                            cluster_offset, index_in_cluster * 512,
                            buf, n, sector_num);
            if (ret) {
                return ret;
            }
            if (m_data.valid) {
                /* update L2 tables */
                if (vmdk_L2update(extent, &m_data) != VMDK_OK) {
                    return -EIO;
                }
            }
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;

        /* update CID on the first write every time the virtual disk is
         * opened */
        if (!s->cid_updated) {
            ret = vmdk_write_cid(bs, time(NULL));
            if (ret < 0) {
                return ret;
            }
            s->cid_updated = true;
        }
    }
    return 0;
}

static coroutine_fn int vmdk_co_write(BlockDriverState *bs, int64_t sector_num,
                                      const uint8_t *buf, int nb_sectors)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    ret = vmdk_write(bs, sector_num, buf, nb_sectors, false, false);
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int vmdk_write_compressed(BlockDriverState *bs,
                                 int64_t sector_num,
                                 const uint8_t *buf,
                                 int nb_sectors)
{
    BDRVVmdkState *s = bs->opaque;
    if (s->num_extents == 1 && s->extents[0].compressed) {
        return vmdk_write(bs, sector_num, buf, nb_sectors, false, false);
    } else {
        return -ENOTSUP;
    }
}

static int coroutine_fn vmdk_co_write_zeroes(BlockDriverState *bs,
                                             int64_t sector_num,
                                             int nb_sectors,
                                             BdrvRequestFlags flags)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;
    qemu_co_mutex_lock(&s->lock);
    /* write zeroes could fail if sectors not aligned to cluster, test it with
     * dry_run == true before really updating image */
    ret = vmdk_write(bs, sector_num, NULL, nb_sectors, true, true);
    if (!ret) {
        ret = vmdk_write(bs, sector_num, NULL, nb_sectors, true, false);
    }
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int vmdk_create_extent(const char *filename, int64_t filesize,
                              bool flat, bool compress, bool zeroed_grain,
                              Error **errp)
{
    int ret, i;
    BlockDriverState *bs = NULL;
    VMDK4Header header;
    Error *local_err = NULL;
    uint32_t tmp, magic, grains, gd_sectors, gt_size, gt_count;
    uint32_t *gd_buf = NULL;
    int gd_buf_size;

    ret = bdrv_create_file(filename, NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }

    assert(bs == NULL);
    ret = bdrv_open(&bs, filename, NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                    NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }

    if (flat) {
        ret = bdrv_truncate(bs, filesize);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not truncate file");
        }
        goto exit;
    }
    magic = cpu_to_be32(VMDK4_MAGIC);
    memset(&header, 0, sizeof(header));
    header.version = zeroed_grain ? 2 : 1;
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
    ret = bdrv_pwrite(bs, 0, &magic, sizeof(magic));
    if (ret < 0) {
        error_set(errp, QERR_IO_ERROR);
        goto exit;
    }
    ret = bdrv_pwrite(bs, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        error_set(errp, QERR_IO_ERROR);
        goto exit;
    }

    ret = bdrv_truncate(bs, le64_to_cpu(header.grain_offset) << 9);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not truncate file");
        goto exit;
    }

    /* write grain directory */
    gd_buf_size = gd_sectors * BDRV_SECTOR_SIZE;
    gd_buf = g_malloc0(gd_buf_size);
    for (i = 0, tmp = le64_to_cpu(header.rgd_offset) + gd_sectors;
         i < gt_count; i++, tmp += gt_size) {
        gd_buf[i] = cpu_to_le32(tmp);
    }
    ret = bdrv_pwrite(bs, le64_to_cpu(header.rgd_offset) * BDRV_SECTOR_SIZE,
                      gd_buf, gd_buf_size);
    if (ret < 0) {
        error_set(errp, QERR_IO_ERROR);
        goto exit;
    }

    /* write backup grain directory */
    for (i = 0, tmp = le64_to_cpu(header.gd_offset) + gd_sectors;
         i < gt_count; i++, tmp += gt_size) {
        gd_buf[i] = cpu_to_le32(tmp);
    }
    ret = bdrv_pwrite(bs, le64_to_cpu(header.gd_offset) * BDRV_SECTOR_SIZE,
                      gd_buf, gd_buf_size);
    if (ret < 0) {
        error_set(errp, QERR_IO_ERROR);
        goto exit;
    }

    ret = 0;
exit:
    if (bs) {
        bdrv_unref(bs);
    }
    g_free(gd_buf);
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

static int vmdk_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int idx = 0;
    BlockDriverState *new_bs = NULL;
    Error *local_err = NULL;
    char *desc = NULL;
    int64_t total_size = 0, filesize;
    char *adapter_type = NULL;
    char *backing_file = NULL;
    char *fmt = NULL;
    int flags = 0;
    int ret = 0;
    bool flat, split, compress;
    GString *ext_desc_lines;
    char path[PATH_MAX], prefix[PATH_MAX], postfix[PATH_MAX];
    const int64_t split_size = 0x80000000;  /* VMDK has constant split size */
    const char *desc_extent_line;
    char parent_desc_line[BUF_SIZE] = "";
    uint32_t parent_cid = 0xffffffff;
    uint32_t number_heads = 16;
    bool zeroed_grain = false;
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
        "ddb.virtualHWVersion = \"%d\"\n"
        "ddb.geometry.cylinders = \"%" PRId64 "\"\n"
        "ddb.geometry.heads = \"%" PRIu32 "\"\n"
        "ddb.geometry.sectors = \"63\"\n"
        "ddb.adapterType = \"%s\"\n";

    ext_desc_lines = g_string_new(NULL);

    if (filename_decompose(filename, path, prefix, postfix, PATH_MAX, errp)) {
        ret = -EINVAL;
        goto exit;
    }
    /* Read out options */
    total_size = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);
    adapter_type = qemu_opt_get_del(opts, BLOCK_OPT_ADAPTER_TYPE);
    backing_file = qemu_opt_get_del(opts, BLOCK_OPT_BACKING_FILE);
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_COMPAT6, false)) {
        flags |= BLOCK_FLAG_COMPAT6;
    }
    fmt = qemu_opt_get_del(opts, BLOCK_OPT_SUBFMT);
    if (qemu_opt_get_bool_del(opts, BLOCK_OPT_ZEROED_GRAIN, false)) {
        zeroed_grain = true;
    }

    if (!adapter_type) {
        adapter_type = g_strdup("ide");
    } else if (strcmp(adapter_type, "ide") &&
               strcmp(adapter_type, "buslogic") &&
               strcmp(adapter_type, "lsilogic") &&
               strcmp(adapter_type, "legacyESX")) {
        error_setg(errp, "Unknown adapter type: '%s'", adapter_type);
        ret = -EINVAL;
        goto exit;
    }
    if (strcmp(adapter_type, "ide") != 0) {
        /* that's the number of heads with which vmware operates when
           creating, exporting, etc. vmdk files with a non-ide adapter type */
        number_heads = 255;
    }
    if (!fmt) {
        /* Default format to monolithicSparse */
        fmt = g_strdup("monolithicSparse");
    } else if (strcmp(fmt, "monolithicFlat") &&
               strcmp(fmt, "monolithicSparse") &&
               strcmp(fmt, "twoGbMaxExtentSparse") &&
               strcmp(fmt, "twoGbMaxExtentFlat") &&
               strcmp(fmt, "streamOptimized")) {
        error_setg(errp, "Unknown subformat: '%s'", fmt);
        ret = -EINVAL;
        goto exit;
    }
    split = !(strcmp(fmt, "twoGbMaxExtentFlat") &&
              strcmp(fmt, "twoGbMaxExtentSparse"));
    flat = !(strcmp(fmt, "monolithicFlat") &&
             strcmp(fmt, "twoGbMaxExtentFlat"));
    compress = !strcmp(fmt, "streamOptimized");
    if (flat) {
        desc_extent_line = "RW %" PRId64 " FLAT \"%s\" 0\n";
    } else {
        desc_extent_line = "RW %" PRId64 " SPARSE \"%s\"\n";
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
    if (backing_file) {
        BlockDriverState *bs = NULL;
        ret = bdrv_open(&bs, backing_file, NULL, NULL, BDRV_O_NO_BACKING, NULL,
                        errp);
        if (ret != 0) {
            goto exit;
        }
        if (strcmp(bs->drv->format_name, "vmdk")) {
            bdrv_unref(bs);
            ret = -EINVAL;
            goto exit;
        }
        parent_cid = vmdk_read_cid(bs, 0);
        bdrv_unref(bs);
        snprintf(parent_desc_line, sizeof(parent_desc_line),
                "parentFileNameHint=\"%s\"", backing_file);
    }

    /* Create extents */
    filesize = total_size;
    while (filesize > 0) {
        char desc_line[BUF_SIZE];
        char ext_filename[PATH_MAX];
        char desc_filename[PATH_MAX];
        int64_t size = filesize;

        if (split && size > split_size) {
            size = split_size;
        }
        if (split) {
            snprintf(desc_filename, sizeof(desc_filename), "%s-%c%03d%s",
                    prefix, flat ? 'f' : 's', ++idx, postfix);
        } else if (flat) {
            snprintf(desc_filename, sizeof(desc_filename), "%s-flat%s",
                    prefix, postfix);
        } else {
            snprintf(desc_filename, sizeof(desc_filename), "%s%s",
                    prefix, postfix);
        }
        snprintf(ext_filename, sizeof(ext_filename), "%s%s",
                path, desc_filename);

        if (vmdk_create_extent(ext_filename, size,
                               flat, compress, zeroed_grain, errp)) {
            ret = -EINVAL;
            goto exit;
        }
        filesize -= size;

        /* Format description line */
        snprintf(desc_line, sizeof(desc_line),
                    desc_extent_line, size / BDRV_SECTOR_SIZE, desc_filename);
        g_string_append(ext_desc_lines, desc_line);
    }
    /* generate descriptor file */
    desc = g_strdup_printf(desc_template,
                           (uint32_t)time(NULL),
                           parent_cid,
                           fmt,
                           parent_desc_line,
                           ext_desc_lines->str,
                           (flags & BLOCK_FLAG_COMPAT6 ? 6 : 4),
                           total_size /
                               (int64_t)(63 * number_heads * BDRV_SECTOR_SIZE),
                           number_heads,
                           adapter_type);
    desc_len = strlen(desc);
    /* the descriptor offset = 0x200 */
    if (!split && !flat) {
        desc_offset = 0x200;
    } else {
        ret = bdrv_create_file(filename, opts, &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            goto exit;
        }
    }
    assert(new_bs == NULL);
    ret = bdrv_open(&new_bs, filename, NULL, NULL,
                    BDRV_O_RDWR | BDRV_O_PROTOCOL, NULL, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto exit;
    }
    ret = bdrv_pwrite(new_bs, desc_offset, desc, desc_len);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write description");
        goto exit;
    }
    /* bdrv_pwrite write padding zeros to align to sector, we don't need that
     * for description file */
    if (desc_offset == 0) {
        ret = bdrv_truncate(new_bs, desc_len);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not truncate file");
        }
    }
exit:
    if (new_bs) {
        bdrv_unref(new_bs);
    }
    g_free(adapter_type);
    g_free(backing_file);
    g_free(fmt);
    g_free(desc);
    g_string_free(ext_desc_lines, true);
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

static coroutine_fn int vmdk_co_flush(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    int i, err;
    int ret = 0;

    for (i = 0; i < s->num_extents; i++) {
        err = bdrv_co_flush(s->extents[i].file);
        if (err < 0) {
            ret = err;
        }
    }
    return ret;
}

static int64_t vmdk_get_allocated_file_size(BlockDriverState *bs)
{
    int i;
    int64_t ret = 0;
    int64_t r;
    BDRVVmdkState *s = bs->opaque;

    ret = bdrv_get_allocated_file_size(bs->file);
    if (ret < 0) {
        return ret;
    }
    for (i = 0; i < s->num_extents; i++) {
        if (s->extents[i].file == bs->file) {
            continue;
        }
        r = bdrv_get_allocated_file_size(s->extents[i].file);
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
            if (!bdrv_has_zero_init(s->extents[i].file)) {
                return 0;
            }
        }
    }
    return 1;
}

static ImageInfo *vmdk_get_extent_info(VmdkExtent *extent)
{
    ImageInfo *info = g_new0(ImageInfo, 1);

    *info = (ImageInfo){
        .filename         = g_strdup(extent->file->filename),
        .format           = g_strdup(extent->type),
        .virtual_size     = extent->sectors * BDRV_SECTOR_SIZE,
        .compressed       = extent->compressed,
        .has_compressed   = extent->compressed,
        .cluster_size     = extent->cluster_sectors * BDRV_SECTOR_SIZE,
        .has_cluster_size = !extent->flat,
    };

    return info;
}

static int vmdk_check(BlockDriverState *bs, BdrvCheckResult *result,
                      BdrvCheckMode fix)
{
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    int64_t sector_num = 0;
    int64_t total_sectors = bdrv_getlength(bs) / BDRV_SECTOR_SIZE;
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
            break;
        }
        ret = get_cluster_offset(bs, extent, NULL,
                                 sector_num << BDRV_SECTOR_BITS,
                                 0, &cluster_offset);
        if (ret == VMDK_ERROR) {
            fprintf(stderr,
                    "ERROR: could not get cluster_offset for sector %"
                    PRId64 "\n", sector_num);
            break;
        }
        if (ret == VMDK_OK && cluster_offset >= bdrv_getlength(extent->file)) {
            fprintf(stderr,
                    "ERROR: cluster offset for sector %"
                    PRId64 " points after EOF\n", sector_num);
            break;
        }
        sector_num += extent->cluster_sectors;
    }

    result->corruptions++;
    return 0;
}

static ImageInfoSpecific *vmdk_get_specific_info(BlockDriverState *bs)
{
    int i;
    BDRVVmdkState *s = bs->opaque;
    ImageInfoSpecific *spec_info = g_new0(ImageInfoSpecific, 1);
    ImageInfoList **next;

    *spec_info = (ImageInfoSpecific){
        .kind = IMAGE_INFO_SPECIFIC_KIND_VMDK,
        {
            .vmdk = g_new0(ImageInfoSpecificVmdk, 1),
        },
    };

    *spec_info->vmdk = (ImageInfoSpecificVmdk) {
        .create_type = g_strdup(s->create_type),
        .cid = s->cid,
        .parent_cid = s->parent_cid,
    };

    next = &spec_info->vmdk->extents;
    for (i = 0; i < s->num_extents; i++) {
        *next = g_new0(ImageInfoList, 1);
        (*next)->value = vmdk_get_extent_info(&s->extents[i]);
        (*next)->next = NULL;
        next = &(*next)->next;
    }

    return spec_info;
}

static int vmdk_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    int i;
    BDRVVmdkState *s = bs->opaque;
    assert(s->num_extents);
    bdi->needs_compressed_writes = s->extents[0].compressed;
    if (!s->extents[0].flat) {
        bdi->cluster_size = s->extents[0].cluster_sectors << BDRV_SECTOR_BITS;
    }
    /* See if we have multiple extents but they have different cases */
    for (i = 1; i < s->num_extents; i++) {
        if (bdi->needs_compressed_writes != s->extents[i].compressed ||
            (bdi->cluster_size && bdi->cluster_size !=
                s->extents[i].cluster_sectors << BDRV_SECTOR_BITS)) {
            return -ENOTSUP;
        }
    }
    return 0;
}

static void vmdk_detach_aio_context(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_extents; i++) {
        bdrv_detach_aio_context(s->extents[i].file);
    }
}

static void vmdk_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BDRVVmdkState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_extents; i++) {
        bdrv_attach_aio_context(s->extents[i].file, new_context);
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
            .name = BLOCK_OPT_COMPAT6,
            .type = QEMU_OPT_BOOL,
            .help = "VMDK version 6 image",
            .def_value_str = "off"
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
    .bdrv_check                   = vmdk_check,
    .bdrv_reopen_prepare          = vmdk_reopen_prepare,
    .bdrv_read                    = vmdk_co_read,
    .bdrv_write                   = vmdk_co_write,
    .bdrv_write_compressed        = vmdk_write_compressed,
    .bdrv_co_write_zeroes         = vmdk_co_write_zeroes,
    .bdrv_close                   = vmdk_close,
    .bdrv_create                  = vmdk_create,
    .bdrv_co_flush_to_disk        = vmdk_co_flush,
    .bdrv_co_get_block_status     = vmdk_co_get_block_status,
    .bdrv_get_allocated_file_size = vmdk_get_allocated_file_size,
    .bdrv_has_zero_init           = vmdk_has_zero_init,
    .bdrv_get_specific_info       = vmdk_get_specific_info,
    .bdrv_refresh_limits          = vmdk_refresh_limits,
    .bdrv_get_info                = vmdk_get_info,
    .bdrv_detach_aio_context      = vmdk_detach_aio_context,
    .bdrv_attach_aio_context      = vmdk_attach_aio_context,

    .supports_backing             = true,
    .create_opts                  = &vmdk_create_opts,
};

static void bdrv_vmdk_init(void)
{
    bdrv_register(&bdrv_vmdk);
}

block_init(bdrv_vmdk_init);
