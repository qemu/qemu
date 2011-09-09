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
#include "block_int.h"
#include "module.h"
#include "zlib.h"

#define VMDK3_MAGIC (('C' << 24) | ('O' << 16) | ('W' << 8) | 'D')
#define VMDK4_MAGIC (('K' << 24) | ('D' << 16) | ('M' << 8) | 'V')
#define VMDK4_COMPRESSION_DEFLATE 1
#define VMDK4_FLAG_RGD (1 << 1)
#define VMDK4_FLAG_COMPRESS (1 << 16)
#define VMDK4_FLAG_MARKER (1 << 17)

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
} VMDK3Header;

typedef struct {
    uint32_t version;
    uint32_t flags;
    int64_t capacity;
    int64_t granularity;
    int64_t desc_offset;
    int64_t desc_size;
    int32_t num_gtes_per_gte;
    int64_t gd_offset;
    int64_t rgd_offset;
    int64_t grain_offset;
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

    unsigned int cluster_sectors;
} VmdkExtent;

typedef struct BDRVVmdkState {
    int desc_offset;
    bool cid_updated;
    uint32_t parent_cid;
    int num_extents;
    /* Extent array with num_extents entries, ascend ordered by address */
    VmdkExtent *extents;
} BDRVVmdkState;

typedef struct VmdkMetaData {
    uint32_t offset;
    unsigned int l1_index;
    unsigned int l2_index;
    unsigned int l2_offset;
    int valid;
} VmdkMetaData;

typedef struct VmdkGrainMarker {
    uint64_t lba;
    uint32_t size;
    uint8_t  data[0];
} VmdkGrainMarker;

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

#define CHECK_CID 1

#define SECTOR_SIZE 512
#define DESC_SIZE (20 * SECTOR_SIZE)    /* 20 sectors of 512 bytes each */
#define BUF_SIZE 4096
#define HEADER_SIZE 512                 /* first sector of 512 bytes */

static void vmdk_free_extents(BlockDriverState *bs)
{
    int i;
    BDRVVmdkState *s = bs->opaque;

    for (i = 0; i < s->num_extents; i++) {
        g_free(s->extents[i].l1_table);
        g_free(s->extents[i].l2_cache);
        g_free(s->extents[i].l1_backup_table);
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
    uint32_t cid;
    const char *p_name, *cid_str;
    size_t cid_str_size;
    BDRVVmdkState *s = bs->opaque;

    if (bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE) != DESC_SIZE) {
        return 0;
    }

    if (parent) {
        cid_str = "parentCID";
        cid_str_size = sizeof("parentCID");
    } else {
        cid_str = "CID";
        cid_str_size = sizeof("CID");
    }

    p_name = strstr(desc, cid_str);
    if (p_name != NULL) {
        p_name += cid_str_size;
        sscanf(p_name, "%x", &cid);
    }

    return cid;
}

static int vmdk_write_cid(BlockDriverState *bs, uint32_t cid)
{
    char desc[DESC_SIZE], tmp_desc[DESC_SIZE];
    char *p_name, *tmp_str;
    BDRVVmdkState *s = bs->opaque;

    memset(desc, 0, sizeof(desc));
    if (bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE) != DESC_SIZE) {
        return -EIO;
    }

    tmp_str = strstr(desc, "parentCID");
    pstrcpy(tmp_desc, sizeof(tmp_desc), tmp_str);
    p_name = strstr(desc, "CID");
    if (p_name != NULL) {
        p_name += sizeof("CID");
        snprintf(p_name, sizeof(desc) - (p_name - desc), "%x\n", cid);
        pstrcat(desc, sizeof(desc), tmp_desc);
    }

    if (bdrv_pwrite_sync(bs->file, s->desc_offset, desc, DESC_SIZE) < 0) {
        return -EIO;
    }
    return 0;
}

static int vmdk_is_cid_valid(BlockDriverState *bs)
{
#ifdef CHECK_CID
    BDRVVmdkState *s = bs->opaque;
    BlockDriverState *p_bs = bs->backing_hd;
    uint32_t cur_pcid;

    if (p_bs) {
        cur_pcid = vmdk_read_cid(p_bs, 0);
        if (s->parent_cid != cur_pcid) {
            /* CID not valid */
            return 0;
        }
    }
#endif
    /* CID valid */
    return 1;
}

static int vmdk_parent_open(BlockDriverState *bs)
{
    char *p_name;
    char desc[DESC_SIZE + 1];
    BDRVVmdkState *s = bs->opaque;

    desc[DESC_SIZE] = '\0';
    if (bdrv_pread(bs->file, s->desc_offset, desc, DESC_SIZE) != DESC_SIZE) {
        return -1;
    }

    p_name = strstr(desc, "parentFileNameHint");
    if (p_name != NULL) {
        char *end_name;

        p_name += sizeof("parentFileNameHint") + 1;
        end_name = strchr(p_name, '\"');
        if (end_name == NULL) {
            return -1;
        }
        if ((end_name - p_name) > sizeof(bs->backing_file) - 1) {
            return -1;
        }

        pstrcpy(bs->backing_file, end_name - p_name + 1, p_name);
    }

    return 0;
}

/* Create and append extent to the extent array. Return the added VmdkExtent
 * address. return NULL if allocation failed. */
static VmdkExtent *vmdk_add_extent(BlockDriverState *bs,
                           BlockDriverState *file, bool flat, int64_t sectors,
                           int64_t l1_offset, int64_t l1_backup_offset,
                           uint32_t l1_size,
                           int l2_size, unsigned int cluster_sectors)
{
    VmdkExtent *extent;
    BDRVVmdkState *s = bs->opaque;

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
    extent->cluster_sectors = cluster_sectors;

    if (s->num_extents > 1) {
        extent->end_sector = (*(extent - 1)).end_sector + extent->sectors;
    } else {
        extent->end_sector = extent->sectors;
    }
    bs->total_sectors = extent->end_sector;
    return extent;
}

static int vmdk_init_tables(BlockDriverState *bs, VmdkExtent *extent)
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

static int vmdk_open_vmdk3(BlockDriverState *bs,
                           BlockDriverState *file,
                           int flags)
{
    int ret;
    uint32_t magic;
    VMDK3Header header;
    VmdkExtent *extent;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        return ret;
    }
    extent = vmdk_add_extent(bs,
                             bs->file, false,
                             le32_to_cpu(header.disk_sectors),
                             le32_to_cpu(header.l1dir_offset) << 9,
                             0, 1 << 6, 1 << 9,
                             le32_to_cpu(header.granularity));
    ret = vmdk_init_tables(bs, extent);
    if (ret) {
        /* free extent allocated by vmdk_add_extent */
        vmdk_free_last_extent(bs);
    }
    return ret;
}

static int vmdk_open_desc_file(BlockDriverState *bs, int flags,
                               int64_t desc_offset);

static int vmdk_open_vmdk4(BlockDriverState *bs,
                           BlockDriverState *file,
                           int flags)
{
    int ret;
    uint32_t magic;
    uint32_t l1_size, l1_entry_sectors;
    VMDK4Header header;
    VmdkExtent *extent;
    int64_t l1_backup_offset = 0;

    ret = bdrv_pread(file, sizeof(magic), &header, sizeof(header));
    if (ret < 0) {
        return ret;
    }
    if (header.capacity == 0 && header.desc_offset) {
        return vmdk_open_desc_file(bs, flags, header.desc_offset << 9);
    }
    l1_entry_sectors = le32_to_cpu(header.num_gtes_per_gte)
                        * le64_to_cpu(header.granularity);
    if (l1_entry_sectors <= 0) {
        return -EINVAL;
    }
    l1_size = (le64_to_cpu(header.capacity) + l1_entry_sectors - 1)
                / l1_entry_sectors;
    if (le32_to_cpu(header.flags) & VMDK4_FLAG_RGD) {
        l1_backup_offset = le64_to_cpu(header.rgd_offset) << 9;
    }
    extent = vmdk_add_extent(bs, file, false,
                          le64_to_cpu(header.capacity),
                          le64_to_cpu(header.gd_offset) << 9,
                          l1_backup_offset,
                          l1_size,
                          le32_to_cpu(header.num_gtes_per_gte),
                          le64_to_cpu(header.granularity));
    extent->compressed =
        le16_to_cpu(header.compressAlgorithm) == VMDK4_COMPRESSION_DEFLATE;
    extent->has_marker = le32_to_cpu(header.flags) & VMDK4_FLAG_MARKER;
    ret = vmdk_init_tables(bs, extent);
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
        return -1;
    }
    /* Skip "=\"" following opt_name */
    opt_pos += strlen(opt_name) + 2;
    if (opt_pos >= end) {
        return -1;
    }
    opt_end = opt_pos;
    while (opt_end < end && *opt_end != '"') {
        opt_end++;
    }
    if (opt_end == end || buf_size < opt_end - opt_pos + 1) {
        return -1;
    }
    pstrcpy(buf, opt_end - opt_pos + 1, opt_pos);
    return 0;
}

/* Open an extent file and append to bs array */
static int vmdk_open_sparse(BlockDriverState *bs,
                            BlockDriverState *file,
                            int flags)
{
    uint32_t magic;

    if (bdrv_pread(file, 0, &magic, sizeof(magic)) != sizeof(magic)) {
        return -EIO;
    }

    magic = be32_to_cpu(magic);
    switch (magic) {
        case VMDK3_MAGIC:
            return vmdk_open_vmdk3(bs, file, flags);
            break;
        case VMDK4_MAGIC:
            return vmdk_open_vmdk4(bs, file, flags);
            break;
        default:
            return -EINVAL;
            break;
    }
}

static int vmdk_parse_extents(const char *desc, BlockDriverState *bs,
        const char *desc_file_path)
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

    while (*p) {
        /* parse extent line:
         * RW [size in sectors] FLAT "file-name.vmdk" OFFSET
         * or
         * RW [size in sectors] SPARSE "file-name.vmdk"
         */
        flat_offset = -1;
        ret = sscanf(p, "%10s %" SCNd64 " %10s %511s %" SCNd64,
                access, &sectors, type, fname, &flat_offset);
        if (ret < 4 || strcmp(access, "RW")) {
            goto next_line;
        } else if (!strcmp(type, "FLAT")) {
            if (ret != 5 || flat_offset < 0) {
                return -EINVAL;
            }
        } else if (ret != 4) {
            return -EINVAL;
        }

        /* trim the quotation marks around */
        if (fname[0] == '"') {
            memmove(fname, fname + 1, strlen(fname));
            if (strlen(fname) <= 1 || fname[strlen(fname) - 1] != '"') {
                return -EINVAL;
            }
            fname[strlen(fname) - 1] = '\0';
        }
        if (sectors <= 0 ||
            (strcmp(type, "FLAT") && strcmp(type, "SPARSE")) ||
            (strcmp(access, "RW"))) {
            goto next_line;
        }

        path_combine(extent_path, sizeof(extent_path),
                desc_file_path, fname);
        ret = bdrv_file_open(&extent_file, extent_path, bs->open_flags);
        if (ret) {
            return ret;
        }

        /* save to extents array */
        if (!strcmp(type, "FLAT")) {
            /* FLAT extent */
            VmdkExtent *extent;

            extent = vmdk_add_extent(bs, extent_file, true, sectors,
                            0, 0, 0, 0, sectors);
            extent->flat_start_offset = flat_offset << 9;
        } else if (!strcmp(type, "SPARSE")) {
            /* SPARSE extent */
            ret = vmdk_open_sparse(bs, extent_file, bs->open_flags);
            if (ret) {
                bdrv_delete(extent_file);
                return ret;
            }
        } else {
            fprintf(stderr,
                "VMDK: Not supported extent type \"%s\""".\n", type);
            return -ENOTSUP;
        }
next_line:
        /* move to next line */
        while (*p && *p != '\n') {
            p++;
        }
        p++;
    }
    return 0;
}

static int vmdk_open_desc_file(BlockDriverState *bs, int flags,
                               int64_t desc_offset)
{
    int ret;
    char buf[2048];
    char ct[128];
    BDRVVmdkState *s = bs->opaque;

    ret = bdrv_pread(bs->file, desc_offset, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }
    buf[2047] = '\0';
    if (vmdk_parse_description(buf, "createType", ct, sizeof(ct))) {
        return -EINVAL;
    }
    if (strcmp(ct, "monolithicFlat") &&
        strcmp(ct, "twoGbMaxExtentSparse") &&
        strcmp(ct, "twoGbMaxExtentFlat")) {
        fprintf(stderr,
                "VMDK: Not supported image type \"%s\""".\n", ct);
        return -ENOTSUP;
    }
    s->desc_offset = 0;
    ret = vmdk_parse_extents(buf, bs, bs->file->filename);
    if (ret) {
        return ret;
    }

    /* try to open parent images, if exist */
    if (vmdk_parent_open(bs)) {
        g_free(s->extents);
        return -EINVAL;
    }
    s->parent_cid = vmdk_read_cid(bs, 1);
    return 0;
}

static int vmdk_open(BlockDriverState *bs, int flags)
{
    int ret;
    BDRVVmdkState *s = bs->opaque;

    if (vmdk_open_sparse(bs, bs->file, flags) == 0) {
        s->desc_offset = 0x200;
        /* try to open parent images, if exist */
        ret = vmdk_parent_open(bs);
        if (ret) {
            vmdk_free_extents(bs);
            return ret;
        }
        s->parent_cid = vmdk_read_cid(bs, 1);
        return 0;
    } else {
        return vmdk_open_desc_file(bs, flags, 0);
    }
}

static int get_whole_cluster(BlockDriverState *bs,
                VmdkExtent *extent,
                uint64_t cluster_offset,
                uint64_t offset,
                bool allocate)
{
    /* 128 sectors * 512 bytes each = grain size 64KB */
    uint8_t  whole_grain[extent->cluster_sectors * 512];

    /* we will be here if it's first write on non-exist grain(cluster).
     * try to read from parent image, if exist */
    if (bs->backing_hd) {
        int ret;

        if (!vmdk_is_cid_valid(bs)) {
            return -1;
        }

        /* floor offset to cluster */
        offset -= offset % (extent->cluster_sectors * 512);
        ret = bdrv_read(bs->backing_hd, offset >> 9, whole_grain,
                extent->cluster_sectors);
        if (ret < 0) {
            return -1;
        }

        /* Write grain only into the active image */
        ret = bdrv_write(extent->file, cluster_offset, whole_grain,
                extent->cluster_sectors);
        if (ret < 0) {
            return -1;
        }
    }
    return 0;
}

static int vmdk_L2update(VmdkExtent *extent, VmdkMetaData *m_data)
{
    /* update L2 table */
    if (bdrv_pwrite_sync(
                extent->file,
                ((int64_t)m_data->l2_offset * 512)
                    + (m_data->l2_index * sizeof(m_data->offset)),
                &(m_data->offset),
                sizeof(m_data->offset)
            ) < 0) {
        return -1;
    }
    /* update backup L2 table */
    if (extent->l1_backup_table_offset != 0) {
        m_data->l2_offset = extent->l1_backup_table[m_data->l1_index];
        if (bdrv_pwrite_sync(
                    extent->file,
                    ((int64_t)m_data->l2_offset * 512)
                        + (m_data->l2_index * sizeof(m_data->offset)),
                    &(m_data->offset), sizeof(m_data->offset)
                ) < 0) {
            return -1;
        }
    }

    return 0;
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
    uint32_t min_count, *l2_table, tmp = 0;

    if (m_data) {
        m_data->valid = 0;
    }
    if (extent->flat) {
        *cluster_offset = extent->flat_start_offset;
        return 0;
    }

    offset -= (extent->end_sector - extent->sectors) * SECTOR_SIZE;
    l1_index = (offset >> 9) / extent->l1_entry_sectors;
    if (l1_index >= extent->l1_size) {
        return -1;
    }
    l2_offset = extent->l1_table[l1_index];
    if (!l2_offset) {
        return -1;
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
        return -1;
    }

    extent->l2_cache_offsets[min_index] = l2_offset;
    extent->l2_cache_counts[min_index] = 1;
 found:
    l2_index = ((offset >> 9) / extent->cluster_sectors) % extent->l2_size;
    *cluster_offset = le32_to_cpu(l2_table[l2_index]);

    if (!*cluster_offset) {
        if (!allocate) {
            return -1;
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
        tmp = cpu_to_le32(*cluster_offset);
        l2_table[l2_index] = tmp;

        /* First of all we write grain itself, to avoid race condition
         * that may to corrupt the image.
         * This problem may occur because of insufficient space on host disk
         * or inappropriate VM shutdown.
         */
        if (get_whole_cluster(
                bs, extent, *cluster_offset, offset, allocate) == -1) {
            return -1;
        }

        if (m_data) {
            m_data->offset = tmp;
            m_data->l1_index = l1_index;
            m_data->l2_index = l2_index;
            m_data->l2_offset = l2_offset;
            m_data->valid = 1;
        }
    }
    *cluster_offset <<= 9;
    return 0;
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

static int vmdk_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
    BDRVVmdkState *s = bs->opaque;
    int64_t index_in_cluster, n, ret;
    uint64_t offset;
    VmdkExtent *extent;

    extent = find_extent(s, sector_num, NULL);
    if (!extent) {
        return 0;
    }
    ret = get_cluster_offset(bs, extent, NULL,
                            sector_num * 512, 0, &offset);
    /* get_cluster_offset returning 0 means success */
    ret = !ret;

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
        index_in_cluster = sector_num % extent->cluster_sectors;
        n = extent->cluster_sectors - index_in_cluster;
        if (n > nb_sectors) {
            n = nb_sectors;
        }
        if (ret) {
            /* if not allocated, try to read from parent image, if exist */
            if (bs->backing_hd) {
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

static int vmdk_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BDRVVmdkState *s = bs->opaque;
    VmdkExtent *extent = NULL;
    int n, ret;
    int64_t index_in_cluster;
    uint64_t cluster_offset;
    VmdkMetaData m_data;

    if (sector_num > bs->total_sectors) {
        fprintf(stderr,
                "(VMDK) Wrong offset: sector_num=0x%" PRIx64
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
            if (ret == 0) {
                /* Refuse write to allocated cluster for streamOptimized */
                fprintf(stderr,
                        "VMDK: can't write to allocated cluster"
                        " for streamOptimized\n");
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
        if (ret) {
            return -EINVAL;
        }
        index_in_cluster = sector_num % extent->cluster_sectors;
        n = extent->cluster_sectors - index_in_cluster;
        if (n > nb_sectors) {
            n = nb_sectors;
        }

        ret = vmdk_write_extent(extent,
                        cluster_offset, index_in_cluster * 512,
                        buf, n, sector_num);
        if (ret) {
            return ret;
        }
        if (m_data.valid) {
            /* update L2 tables */
            if (vmdk_L2update(extent, &m_data) == -1) {
                return -EIO;
            }
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;

        /* update CID on the first write every time the virtual disk is
         * opened */
        if (!s->cid_updated) {
            vmdk_write_cid(bs, time(NULL));
            s->cid_updated = true;
        }
    }
    return 0;
}


static int vmdk_create_extent(const char *filename, int64_t filesize,
                              bool flat, bool compress)
{
    int ret, i;
    int fd = 0;
    VMDK4Header header;
    uint32_t tmp, magic, grains, gd_size, gt_size, gt_count;

    fd = open(
        filename,
        O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
        0644);
    if (fd < 0) {
        return -errno;
    }
    if (flat) {
        ret = ftruncate(fd, filesize);
        if (ret < 0) {
            ret = -errno;
        }
        goto exit;
    }
    magic = cpu_to_be32(VMDK4_MAGIC);
    memset(&header, 0, sizeof(header));
    header.version = 1;
    header.flags =
        3 | (compress ? VMDK4_FLAG_COMPRESS | VMDK4_FLAG_MARKER : 0);
    header.compressAlgorithm = compress ? VMDK4_COMPRESSION_DEFLATE : 0;
    header.capacity = filesize / 512;
    header.granularity = 128;
    header.num_gtes_per_gte = 512;

    grains = (filesize / 512 + header.granularity - 1) / header.granularity;
    gt_size = ((header.num_gtes_per_gte * sizeof(uint32_t)) + 511) >> 9;
    gt_count =
        (grains + header.num_gtes_per_gte - 1) / header.num_gtes_per_gte;
    gd_size = (gt_count * sizeof(uint32_t) + 511) >> 9;

    header.desc_offset = 1;
    header.desc_size = 20;
    header.rgd_offset = header.desc_offset + header.desc_size;
    header.gd_offset = header.rgd_offset + gd_size + (gt_size * gt_count);
    header.grain_offset =
       ((header.gd_offset + gd_size + (gt_size * gt_count) +
         header.granularity - 1) / header.granularity) *
        header.granularity;
    /* swap endianness for all header fields */
    header.version = cpu_to_le32(header.version);
    header.flags = cpu_to_le32(header.flags);
    header.capacity = cpu_to_le64(header.capacity);
    header.granularity = cpu_to_le64(header.granularity);
    header.num_gtes_per_gte = cpu_to_le32(header.num_gtes_per_gte);
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
    ret = qemu_write_full(fd, &magic, sizeof(magic));
    if (ret != sizeof(magic)) {
        ret = -errno;
        goto exit;
    }
    ret = qemu_write_full(fd, &header, sizeof(header));
    if (ret != sizeof(header)) {
        ret = -errno;
        goto exit;
    }

    ret = ftruncate(fd, le64_to_cpu(header.grain_offset) << 9);
    if (ret < 0) {
        ret = -errno;
        goto exit;
    }

    /* write grain directory */
    lseek(fd, le64_to_cpu(header.rgd_offset) << 9, SEEK_SET);
    for (i = 0, tmp = le64_to_cpu(header.rgd_offset) + gd_size;
         i < gt_count; i++, tmp += gt_size) {
        ret = qemu_write_full(fd, &tmp, sizeof(tmp));
        if (ret != sizeof(tmp)) {
            ret = -errno;
            goto exit;
        }
    }

    /* write backup grain directory */
    lseek(fd, le64_to_cpu(header.gd_offset) << 9, SEEK_SET);
    for (i = 0, tmp = le64_to_cpu(header.gd_offset) + gd_size;
         i < gt_count; i++, tmp += gt_size) {
        ret = qemu_write_full(fd, &tmp, sizeof(tmp));
        if (ret != sizeof(tmp)) {
            ret = -errno;
            goto exit;
        }
    }

    ret = 0;
 exit:
    close(fd);
    return ret;
}

static int filename_decompose(const char *filename, char *path, char *prefix,
        char *postfix, size_t buf_len)
{
    const char *p, *q;

    if (filename == NULL || !strlen(filename)) {
        fprintf(stderr, "Vmdk: no filename provided.\n");
        return -1;
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
            return -1;
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
            return -1;
        }
        pstrcpy(prefix, q - p + 1, p);
        pstrcpy(postfix, buf_len, q);
    }
    return 0;
}

static int relative_path(char *dest, int dest_size,
        const char *base, const char *target)
{
    int i = 0;
    int n = 0;
    const char *p, *q;
#ifdef _WIN32
    const char *sep = "\\";
#else
    const char *sep = "/";
#endif

    if (!(dest && base && target)) {
        return -1;
    }
    if (path_is_absolute(target)) {
        dest[dest_size - 1] = '\0';
        strncpy(dest, target, dest_size - 1);
        return 0;
    }
    while (base[i] == target[i]) {
        i++;
    }
    p = &base[i];
    q = &target[i];
    while (*p) {
        if (*p == *sep) {
            n++;
        }
        p++;
    }
    dest[0] = '\0';
    for (; n; n--) {
        pstrcat(dest, dest_size, "..");
        pstrcat(dest, dest_size, sep);
    }
    pstrcat(dest, dest_size, q);
    return 0;
}

static int vmdk_create(const char *filename, QEMUOptionParameter *options)
{
    int fd, idx = 0;
    char desc[BUF_SIZE];
    int64_t total_size = 0, filesize;
    const char *backing_file = NULL;
    const char *fmt = NULL;
    int flags = 0;
    int ret = 0;
    bool flat, split, compress;
    char ext_desc_lines[BUF_SIZE] = "";
    char path[PATH_MAX], prefix[PATH_MAX], postfix[PATH_MAX];
    const int64_t split_size = 0x80000000;  /* VMDK has constant split size */
    const char *desc_extent_line;
    char parent_desc_line[BUF_SIZE] = "";
    uint32_t parent_cid = 0xffffffff;
    const char desc_template[] =
        "# Disk DescriptorFile\n"
        "version=1\n"
        "CID=%x\n"
        "parentCID=%x\n"
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
        "ddb.geometry.heads = \"16\"\n"
        "ddb.geometry.sectors = \"63\"\n"
        "ddb.adapterType = \"ide\"\n";

    if (filename_decompose(filename, path, prefix, postfix, PATH_MAX)) {
        return -EINVAL;
    }
    /* Read out options */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            total_size = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_COMPAT6)) {
            flags |= options->value.n ? BLOCK_FLAG_COMPAT6 : 0;
        } else if (!strcmp(options->name, BLOCK_OPT_SUBFMT)) {
            fmt = options->value.s;
        }
        options++;
    }
    if (!fmt) {
        /* Default format to monolithicSparse */
        fmt = "monolithicSparse";
    } else if (strcmp(fmt, "monolithicFlat") &&
               strcmp(fmt, "monolithicSparse") &&
               strcmp(fmt, "twoGbMaxExtentSparse") &&
               strcmp(fmt, "twoGbMaxExtentFlat") &&
               strcmp(fmt, "streamOptimized")) {
        fprintf(stderr, "VMDK: Unknown subformat: %s\n", fmt);
        return -EINVAL;
    }
    split = !(strcmp(fmt, "twoGbMaxExtentFlat") &&
              strcmp(fmt, "twoGbMaxExtentSparse"));
    flat = !(strcmp(fmt, "monolithicFlat") &&
             strcmp(fmt, "twoGbMaxExtentFlat"));
    compress = !strcmp(fmt, "streamOptimized");
    if (flat) {
        desc_extent_line = "RW %lld FLAT \"%s\" 0\n";
    } else {
        desc_extent_line = "RW %lld SPARSE \"%s\"\n";
    }
    if (flat && backing_file) {
        /* not supporting backing file for flat image */
        return -ENOTSUP;
    }
    if (backing_file) {
        char parent_filename[PATH_MAX];
        BlockDriverState *bs = bdrv_new("");
        ret = bdrv_open(bs, backing_file, 0, NULL);
        if (ret != 0) {
            bdrv_delete(bs);
            return ret;
        }
        if (strcmp(bs->drv->format_name, "vmdk")) {
            bdrv_delete(bs);
            return -EINVAL;
        }
        filesize = bdrv_getlength(bs);
        parent_cid = vmdk_read_cid(bs, 0);
        bdrv_delete(bs);
        relative_path(parent_filename, sizeof(parent_filename),
                      filename, backing_file);
        snprintf(parent_desc_line, sizeof(parent_desc_line),
                "parentFileNameHint=\"%s\"", parent_filename);
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

        if (vmdk_create_extent(ext_filename, size, flat, compress)) {
            return -EINVAL;
        }
        filesize -= size;

        /* Format description line */
        snprintf(desc_line, sizeof(desc_line),
                    desc_extent_line, size / 512, desc_filename);
        pstrcat(ext_desc_lines, sizeof(ext_desc_lines), desc_line);
    }
    /* generate descriptor file */
    snprintf(desc, sizeof(desc), desc_template,
            (unsigned int)time(NULL),
            parent_cid,
            fmt,
            parent_desc_line,
            ext_desc_lines,
            (flags & BLOCK_FLAG_COMPAT6 ? 6 : 4),
            total_size / (int64_t)(63 * 16 * 512));
    if (split || flat) {
        fd = open(
                filename,
                O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
                0644);
    } else {
        fd = open(
                filename,
                O_WRONLY | O_BINARY | O_LARGEFILE,
                0644);
    }
    if (fd < 0) {
        return -errno;
    }
    /* the descriptor offset = 0x200 */
    if (!split && !flat && 0x200 != lseek(fd, 0x200, SEEK_SET)) {
        ret = -errno;
        goto exit;
    }
    ret = qemu_write_full(fd, desc, strlen(desc));
    if (ret != strlen(desc)) {
        ret = -errno;
        goto exit;
    }
    ret = 0;
exit:
    close(fd);
    return ret;
}

static void vmdk_close(BlockDriverState *bs)
{
    vmdk_free_extents(bs);
}

static int vmdk_flush(BlockDriverState *bs)
{
    int i, ret, err;
    BDRVVmdkState *s = bs->opaque;

    ret = bdrv_flush(bs->file);
    for (i = 0; i < s->num_extents; i++) {
        err = bdrv_flush(s->extents[i].file);
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

static QEMUOptionParameter vmdk_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    },
    {
        .name = BLOCK_OPT_COMPAT6,
        .type = OPT_FLAG,
        .help = "VMDK version 6 image"
    },
    {
        .name = BLOCK_OPT_SUBFMT,
        .type = OPT_STRING,
        .help =
            "VMDK flat extent format, can be one of "
            "{monolithicSparse (default) | monolithicFlat | twoGbMaxExtentSparse | twoGbMaxExtentFlat | streamOptimized} "
    },
    { NULL }
};

static BlockDriver bdrv_vmdk = {
    .format_name    = "vmdk",
    .instance_size  = sizeof(BDRVVmdkState),
    .bdrv_probe     = vmdk_probe,
    .bdrv_open      = vmdk_open,
    .bdrv_read      = vmdk_read,
    .bdrv_write     = vmdk_write,
    .bdrv_close     = vmdk_close,
    .bdrv_create    = vmdk_create,
    .bdrv_flush     = vmdk_flush,
    .bdrv_is_allocated  = vmdk_is_allocated,
    .bdrv_get_allocated_file_size  = vmdk_get_allocated_file_size,

    .create_options = vmdk_create_options,
};

static void bdrv_vmdk_init(void)
{
    bdrv_register(&bdrv_vmdk);
}

block_init(bdrv_vmdk_init);
