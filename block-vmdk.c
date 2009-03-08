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

#define VMDK3_MAGIC (('C' << 24) | ('O' << 16) | ('W' << 8) | 'D')
#define VMDK4_MAGIC (('K' << 24) | ('D' << 16) | ('M' << 8) | 'V')

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
    int64_t rgd_offset;
    int64_t gd_offset;
    int64_t grain_offset;
    char filler[1];
    char check_bytes[4];
} __attribute__((packed)) VMDK4Header;

#define L2_CACHE_SIZE 16

typedef struct BDRVVmdkState {
    BlockDriverState *hd;
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
    uint32_t parent_cid;
    int is_parent;
} BDRVVmdkState;

typedef struct VmdkMetaData {
    uint32_t offset;
    unsigned int l1_index;
    unsigned int l2_index;
    unsigned int l2_offset;
    int valid;
} VmdkMetaData;

typedef struct ActiveBDRVState{
    BlockDriverState *hd;            // active image handler
    uint64_t cluster_offset;         // current write offset
}ActiveBDRVState;

static ActiveBDRVState activeBDRV;


static int vmdk_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    uint32_t magic;

    if (buf_size < 4)
        return 0;
    magic = be32_to_cpu(*(uint32_t *)buf);
    if (magic == VMDK3_MAGIC ||
        magic == VMDK4_MAGIC)
        return 100;
    else
        return 0;
}

#define CHECK_CID 1

#define SECTOR_SIZE 512
#define DESC_SIZE 20*SECTOR_SIZE	// 20 sectors of 512 bytes each
#define HEADER_SIZE 512   			// first sector of 512 bytes

static uint32_t vmdk_read_cid(BlockDriverState *bs, int parent)
{
    BDRVVmdkState *s = bs->opaque;
    char desc[DESC_SIZE];
    uint32_t cid;
    const char *p_name, *cid_str;
    size_t cid_str_size;

    /* the descriptor offset = 0x200 */
    if (bdrv_pread(s->hd, 0x200, desc, DESC_SIZE) != DESC_SIZE)
        return 0;

    if (parent) {
        cid_str = "parentCID";
        cid_str_size = sizeof("parentCID");
    } else {
        cid_str = "CID";
        cid_str_size = sizeof("CID");
    }

    if ((p_name = strstr(desc,cid_str)) != NULL) {
        p_name += cid_str_size;
        sscanf(p_name,"%x",&cid);
    }

    return cid;
}

static int vmdk_write_cid(BlockDriverState *bs, uint32_t cid)
{
    BDRVVmdkState *s = bs->opaque;
    char desc[DESC_SIZE], tmp_desc[DESC_SIZE];
    char *p_name, *tmp_str;

    /* the descriptor offset = 0x200 */
    if (bdrv_pread(s->hd, 0x200, desc, DESC_SIZE) != DESC_SIZE)
        return -1;

    tmp_str = strstr(desc,"parentCID");
    pstrcpy(tmp_desc, sizeof(tmp_desc), tmp_str);
    if ((p_name = strstr(desc,"CID")) != NULL) {
        p_name += sizeof("CID");
        snprintf(p_name, sizeof(desc) - (p_name - desc), "%x\n", cid);
        pstrcat(desc, sizeof(desc), tmp_desc);
    }

    if (bdrv_pwrite(s->hd, 0x200, desc, DESC_SIZE) != DESC_SIZE)
        return -1;
    return 0;
}

static int vmdk_is_cid_valid(BlockDriverState *bs)
{
#ifdef CHECK_CID
    BDRVVmdkState *s = bs->opaque;
    BlockDriverState *p_bs = s->hd->backing_hd;
    uint32_t cur_pcid;

    if (p_bs) {
        cur_pcid = vmdk_read_cid(p_bs,0);
        if (s->parent_cid != cur_pcid)
            // CID not valid
            return 0;
    }
#endif
    // CID valid
    return 1;
}

static int vmdk_snapshot_create(const char *filename, const char *backing_file)
{
    int snp_fd, p_fd;
    uint32_t p_cid;
    char *p_name, *gd_buf, *rgd_buf;
    const char *real_filename, *temp_str;
    VMDK4Header header;
    uint32_t gde_entries, gd_size;
    int64_t gd_offset, rgd_offset, capacity, gt_size;
    char p_desc[DESC_SIZE], s_desc[DESC_SIZE], hdr[HEADER_SIZE];
    static const char desc_template[] =
    "# Disk DescriptorFile\n"
    "version=1\n"
    "CID=%x\n"
    "parentCID=%x\n"
    "createType=\"monolithicSparse\"\n"
    "parentFileNameHint=\"%s\"\n"
    "\n"
    "# Extent description\n"
    "RW %u SPARSE \"%s\"\n"
    "\n"
    "# The Disk Data Base \n"
    "#DDB\n"
    "\n";

    snp_fd = open(filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE, 0644);
    if (snp_fd < 0)
        return -1;
    p_fd = open(backing_file, O_RDONLY | O_BINARY | O_LARGEFILE);
    if (p_fd < 0) {
        close(snp_fd);
        return -1;
    }

    /* read the header */
    if (lseek(p_fd, 0x0, SEEK_SET) == -1)
        goto fail;
    if (read(p_fd, hdr, HEADER_SIZE) != HEADER_SIZE)
        goto fail;

    /* write the header */
    if (lseek(snp_fd, 0x0, SEEK_SET) == -1)
        goto fail;
    if (write(snp_fd, hdr, HEADER_SIZE) == -1)
        goto fail;

    memset(&header, 0, sizeof(header));
    memcpy(&header,&hdr[4], sizeof(header)); // skip the VMDK4_MAGIC

    ftruncate(snp_fd, header.grain_offset << 9);
    /* the descriptor offset = 0x200 */
    if (lseek(p_fd, 0x200, SEEK_SET) == -1)
        goto fail;
    if (read(p_fd, p_desc, DESC_SIZE) != DESC_SIZE)
        goto fail;

    if ((p_name = strstr(p_desc,"CID")) != NULL) {
        p_name += sizeof("CID");
        sscanf(p_name,"%x",&p_cid);
    }

    real_filename = filename;
    if ((temp_str = strrchr(real_filename, '\\')) != NULL)
        real_filename = temp_str + 1;
    if ((temp_str = strrchr(real_filename, '/')) != NULL)
        real_filename = temp_str + 1;
    if ((temp_str = strrchr(real_filename, ':')) != NULL)
        real_filename = temp_str + 1;

    snprintf(s_desc, sizeof(s_desc), desc_template, p_cid, p_cid, backing_file,
             (uint32_t)header.capacity, real_filename);

    /* write the descriptor */
    if (lseek(snp_fd, 0x200, SEEK_SET) == -1)
        goto fail;
    if (write(snp_fd, s_desc, strlen(s_desc)) == -1)
        goto fail;

    gd_offset = header.gd_offset * SECTOR_SIZE;     // offset of GD table
    rgd_offset = header.rgd_offset * SECTOR_SIZE;   // offset of RGD table
    capacity = header.capacity * SECTOR_SIZE;       // Extent size
    /*
     * Each GDE span 32M disk, means:
     * 512 GTE per GT, each GTE points to grain
     */
    gt_size = (int64_t)header.num_gtes_per_gte * header.granularity * SECTOR_SIZE;
    if (!gt_size)
        goto fail;
    gde_entries = (uint32_t)(capacity / gt_size);  // number of gde/rgde
    gd_size = gde_entries * sizeof(uint32_t);

    /* write RGD */
    rgd_buf = qemu_malloc(gd_size);
    if (lseek(p_fd, rgd_offset, SEEK_SET) == -1)
        goto fail_rgd;
    if (read(p_fd, rgd_buf, gd_size) != gd_size)
        goto fail_rgd;
    if (lseek(snp_fd, rgd_offset, SEEK_SET) == -1)
        goto fail_rgd;
    if (write(snp_fd, rgd_buf, gd_size) == -1)
        goto fail_rgd;
    qemu_free(rgd_buf);

    /* write GD */
    gd_buf = qemu_malloc(gd_size);
    if (lseek(p_fd, gd_offset, SEEK_SET) == -1)
        goto fail_gd;
    if (read(p_fd, gd_buf, gd_size) != gd_size)
        goto fail_gd;
    if (lseek(snp_fd, gd_offset, SEEK_SET) == -1)
        goto fail_gd;
    if (write(snp_fd, gd_buf, gd_size) == -1)
        goto fail_gd;
    qemu_free(gd_buf);

    close(p_fd);
    close(snp_fd);
    return 0;

    fail_gd:
    qemu_free(gd_buf);
    fail_rgd:
    qemu_free(rgd_buf);
    fail:
    close(p_fd);
    close(snp_fd);
    return -1;
}

static void vmdk_parent_close(BlockDriverState *bs)
{
    if (bs->backing_hd)
        bdrv_close(bs->backing_hd);
}

static int parent_open = 0;
static int vmdk_parent_open(BlockDriverState *bs, const char * filename)
{
    BDRVVmdkState *s = bs->opaque;
    char *p_name;
    char desc[DESC_SIZE];
    char parent_img_name[1024];

    /* the descriptor offset = 0x200 */
    if (bdrv_pread(s->hd, 0x200, desc, DESC_SIZE) != DESC_SIZE)
        return -1;

    if ((p_name = strstr(desc,"parentFileNameHint")) != NULL) {
        char *end_name;
        struct stat file_buf;

        p_name += sizeof("parentFileNameHint") + 1;
        if ((end_name = strchr(p_name,'\"')) == NULL)
            return -1;
        if ((end_name - p_name) > sizeof (s->hd->backing_file) - 1)
            return -1;

        pstrcpy(s->hd->backing_file, end_name - p_name + 1, p_name);
        if (stat(s->hd->backing_file, &file_buf) != 0) {
            path_combine(parent_img_name, sizeof(parent_img_name),
                         filename, s->hd->backing_file);
        } else {
            pstrcpy(parent_img_name, sizeof(parent_img_name),
                    s->hd->backing_file);
        }

        s->hd->backing_hd = bdrv_new("");
        if (!s->hd->backing_hd) {
            failure:
            bdrv_close(s->hd);
            return -1;
        }
        parent_open = 1;
        if (bdrv_open(s->hd->backing_hd, parent_img_name, BDRV_O_RDONLY) < 0)
            goto failure;
        parent_open = 0;
    }

    return 0;
}

static int vmdk_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVVmdkState *s = bs->opaque;
    uint32_t magic;
    int l1_size, i, ret;

    if (parent_open)
        // Parent must be opened as RO.
        flags = BDRV_O_RDONLY;

    ret = bdrv_file_open(&s->hd, filename, flags);
    if (ret < 0)
        return ret;
    if (bdrv_pread(s->hd, 0, &magic, sizeof(magic)) != sizeof(magic))
        goto fail;

    magic = be32_to_cpu(magic);
    if (magic == VMDK3_MAGIC) {
        VMDK3Header header;

        if (bdrv_pread(s->hd, sizeof(magic), &header, sizeof(header)) != sizeof(header))
            goto fail;
        s->cluster_sectors = le32_to_cpu(header.granularity);
        s->l2_size = 1 << 9;
        s->l1_size = 1 << 6;
        bs->total_sectors = le32_to_cpu(header.disk_sectors);
        s->l1_table_offset = le32_to_cpu(header.l1dir_offset) << 9;
        s->l1_backup_table_offset = 0;
        s->l1_entry_sectors = s->l2_size * s->cluster_sectors;
    } else if (magic == VMDK4_MAGIC) {
        VMDK4Header header;

        if (bdrv_pread(s->hd, sizeof(magic), &header, sizeof(header)) != sizeof(header))
            goto fail;
        bs->total_sectors = le64_to_cpu(header.capacity);
        s->cluster_sectors = le64_to_cpu(header.granularity);
        s->l2_size = le32_to_cpu(header.num_gtes_per_gte);
        s->l1_entry_sectors = s->l2_size * s->cluster_sectors;
        if (s->l1_entry_sectors <= 0)
            goto fail;
        s->l1_size = (bs->total_sectors + s->l1_entry_sectors - 1)
            / s->l1_entry_sectors;
        s->l1_table_offset = le64_to_cpu(header.rgd_offset) << 9;
        s->l1_backup_table_offset = le64_to_cpu(header.gd_offset) << 9;

        if (parent_open)
            s->is_parent = 1;
        else
            s->is_parent = 0;

        // try to open parent images, if exist
        if (vmdk_parent_open(bs, filename) != 0)
            goto fail;
        // write the CID once after the image creation
        s->parent_cid = vmdk_read_cid(bs,1);
    } else {
        goto fail;
    }

    /* read the L1 table */
    l1_size = s->l1_size * sizeof(uint32_t);
    s->l1_table = qemu_malloc(l1_size);
    if (bdrv_pread(s->hd, s->l1_table_offset, s->l1_table, l1_size) != l1_size)
        goto fail;
    for(i = 0; i < s->l1_size; i++) {
        le32_to_cpus(&s->l1_table[i]);
    }

    if (s->l1_backup_table_offset) {
        s->l1_backup_table = qemu_malloc(l1_size);
        if (bdrv_pread(s->hd, s->l1_backup_table_offset, s->l1_backup_table, l1_size) != l1_size)
            goto fail;
        for(i = 0; i < s->l1_size; i++) {
            le32_to_cpus(&s->l1_backup_table[i]);
        }
    }

    s->l2_cache = qemu_malloc(s->l2_size * L2_CACHE_SIZE * sizeof(uint32_t));
    return 0;
 fail:
    qemu_free(s->l1_backup_table);
    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    bdrv_delete(s->hd);
    return -1;
}

static uint64_t get_cluster_offset(BlockDriverState *bs, VmdkMetaData *m_data,
                                   uint64_t offset, int allocate);

static int get_whole_cluster(BlockDriverState *bs, uint64_t cluster_offset,
                             uint64_t offset, int allocate)
{
    uint64_t parent_cluster_offset;
    BDRVVmdkState *s = bs->opaque;
    uint8_t  whole_grain[s->cluster_sectors*512];        // 128 sectors * 512 bytes each = grain size 64KB

    // we will be here if it's first write on non-exist grain(cluster).
    // try to read from parent image, if exist
    if (s->hd->backing_hd) {
        BDRVVmdkState *ps = s->hd->backing_hd->opaque;

        if (!vmdk_is_cid_valid(bs))
            return -1;

        parent_cluster_offset = get_cluster_offset(s->hd->backing_hd, NULL, offset, allocate);

        if (parent_cluster_offset) {
            BDRVVmdkState *act_s = activeBDRV.hd->opaque;

            if (bdrv_pread(ps->hd, parent_cluster_offset, whole_grain, ps->cluster_sectors*512) != ps->cluster_sectors*512)
                return -1;

            //Write grain only into the active image
            if (bdrv_pwrite(act_s->hd, activeBDRV.cluster_offset << 9, whole_grain, sizeof(whole_grain)) != sizeof(whole_grain))
                return -1;
        }
    }
    return 0;
}

static int vmdk_L2update(BlockDriverState *bs, VmdkMetaData *m_data)
{
    BDRVVmdkState *s = bs->opaque;

    /* update L2 table */
    if (bdrv_pwrite(s->hd, ((int64_t)m_data->l2_offset * 512) + (m_data->l2_index * sizeof(m_data->offset)),
                    &(m_data->offset), sizeof(m_data->offset)) != sizeof(m_data->offset))
        return -1;
    /* update backup L2 table */
    if (s->l1_backup_table_offset != 0) {
        m_data->l2_offset = s->l1_backup_table[m_data->l1_index];
        if (bdrv_pwrite(s->hd, ((int64_t)m_data->l2_offset * 512) + (m_data->l2_index * sizeof(m_data->offset)),
                        &(m_data->offset), sizeof(m_data->offset)) != sizeof(m_data->offset))
            return -1;
    }

    return 0;
}

static uint64_t get_cluster_offset(BlockDriverState *bs, VmdkMetaData *m_data,
                                   uint64_t offset, int allocate)
{
    BDRVVmdkState *s = bs->opaque;
    unsigned int l1_index, l2_offset, l2_index;
    int min_index, i, j;
    uint32_t min_count, *l2_table, tmp = 0;
    uint64_t cluster_offset;

    if (m_data)
        m_data->valid = 0;

    l1_index = (offset >> 9) / s->l1_entry_sectors;
    if (l1_index >= s->l1_size)
        return 0;
    l2_offset = s->l1_table[l1_index];
    if (!l2_offset)
        return 0;
    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (l2_offset == s->l2_cache_offsets[i]) {
            /* increment the hit count */
            if (++s->l2_cache_counts[i] == 0xffffffff) {
                for(j = 0; j < L2_CACHE_SIZE; j++) {
                    s->l2_cache_counts[j] >>= 1;
                }
            }
            l2_table = s->l2_cache + (i * s->l2_size);
            goto found;
        }
    }
    /* not found: load a new entry in the least used one */
    min_index = 0;
    min_count = 0xffffffff;
    for(i = 0; i < L2_CACHE_SIZE; i++) {
        if (s->l2_cache_counts[i] < min_count) {
            min_count = s->l2_cache_counts[i];
            min_index = i;
        }
    }
    l2_table = s->l2_cache + (min_index * s->l2_size);
    if (bdrv_pread(s->hd, (int64_t)l2_offset * 512, l2_table, s->l2_size * sizeof(uint32_t)) !=
                                                                        s->l2_size * sizeof(uint32_t))
        return 0;

    s->l2_cache_offsets[min_index] = l2_offset;
    s->l2_cache_counts[min_index] = 1;
 found:
    l2_index = ((offset >> 9) / s->cluster_sectors) % s->l2_size;
    cluster_offset = le32_to_cpu(l2_table[l2_index]);

    if (!cluster_offset) {
        if (!allocate)
            return 0;
        // Avoid the L2 tables update for the images that have snapshots.
        if (!s->is_parent) {
            cluster_offset = bdrv_getlength(s->hd);
            bdrv_truncate(s->hd, cluster_offset + (s->cluster_sectors << 9));

            cluster_offset >>= 9;
            tmp = cpu_to_le32(cluster_offset);
            l2_table[l2_index] = tmp;
            // Save the active image state
            activeBDRV.cluster_offset = cluster_offset;
            activeBDRV.hd = bs;
        }
        /* First of all we write grain itself, to avoid race condition
         * that may to corrupt the image.
         * This problem may occur because of insufficient space on host disk
         * or inappropriate VM shutdown.
         */
        if (get_whole_cluster(bs, cluster_offset, offset, allocate) == -1)
            return 0;

        if (m_data) {
            m_data->offset = tmp;
            m_data->l1_index = l1_index;
            m_data->l2_index = l2_index;
            m_data->l2_offset = l2_offset;
            m_data->valid = 1;
        }
    }
    cluster_offset <<= 9;
    return cluster_offset;
}

static int vmdk_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
    BDRVVmdkState *s = bs->opaque;
    int index_in_cluster, n;
    uint64_t cluster_offset;

    cluster_offset = get_cluster_offset(bs, NULL, sector_num << 9, 0);
    index_in_cluster = sector_num % s->cluster_sectors;
    n = s->cluster_sectors - index_in_cluster;
    if (n > nb_sectors)
        n = nb_sectors;
    *pnum = n;
    return (cluster_offset != 0);
}

static int vmdk_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVmdkState *s = bs->opaque;
    int index_in_cluster, n, ret;
    uint64_t cluster_offset;

    while (nb_sectors > 0) {
        cluster_offset = get_cluster_offset(bs, NULL, sector_num << 9, 0);
        index_in_cluster = sector_num % s->cluster_sectors;
        n = s->cluster_sectors - index_in_cluster;
        if (n > nb_sectors)
            n = nb_sectors;
        if (!cluster_offset) {
            // try to read from parent image, if exist
            if (s->hd->backing_hd) {
                if (!vmdk_is_cid_valid(bs))
                    return -1;
                ret = bdrv_read(s->hd->backing_hd, sector_num, buf, n);
                if (ret < 0)
                    return -1;
            } else {
                memset(buf, 0, 512 * n);
            }
        } else {
            if(bdrv_pread(s->hd, cluster_offset + index_in_cluster * 512, buf, n * 512) != n * 512)
                return -1;
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
    VmdkMetaData m_data;
    int index_in_cluster, n;
    uint64_t cluster_offset;
    static int cid_update = 0;

    if (sector_num > bs->total_sectors) {
        fprintf(stderr,
                "(VMDK) Wrong offset: sector_num=0x%" PRIx64
                " total_sectors=0x%" PRIx64 "\n",
                sector_num, bs->total_sectors);
        return -1;
    }

    while (nb_sectors > 0) {
        index_in_cluster = sector_num & (s->cluster_sectors - 1);
        n = s->cluster_sectors - index_in_cluster;
        if (n > nb_sectors)
            n = nb_sectors;
        cluster_offset = get_cluster_offset(bs, &m_data, sector_num << 9, 1);
        if (!cluster_offset)
            return -1;

        if (bdrv_pwrite(s->hd, cluster_offset + index_in_cluster * 512, buf, n * 512) != n * 512)
            return -1;
        if (m_data.valid) {
            /* update L2 tables */
            if (vmdk_L2update(bs, &m_data) == -1)
                return -1;
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;

        // update CID on the first write every time the virtual disk is opened
        if (!cid_update) {
            vmdk_write_cid(bs, time(NULL));
            cid_update++;
        }
    }
    return 0;
}

static int vmdk_create(const char *filename, int64_t total_size,
                       const char *backing_file, int flags)
{
    int fd, i;
    VMDK4Header header;
    uint32_t tmp, magic, grains, gd_size, gt_size, gt_count;
    static const char desc_template[] =
        "# Disk DescriptorFile\n"
        "version=1\n"
        "CID=%x\n"
        "parentCID=ffffffff\n"
        "createType=\"monolithicSparse\"\n"
        "\n"
        "# Extent description\n"
        "RW %" PRId64 " SPARSE \"%s\"\n"
        "\n"
        "# The Disk Data Base \n"
        "#DDB\n"
        "\n"
        "ddb.virtualHWVersion = \"%d\"\n"
        "ddb.geometry.cylinders = \"%" PRId64 "\"\n"
        "ddb.geometry.heads = \"16\"\n"
        "ddb.geometry.sectors = \"63\"\n"
        "ddb.adapterType = \"ide\"\n";
    char desc[1024];
    const char *real_filename, *temp_str;

    /* XXX: add support for backing file */
    if (backing_file) {
        return vmdk_snapshot_create(filename, backing_file);
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE,
              0644);
    if (fd < 0)
        return -1;
    magic = cpu_to_be32(VMDK4_MAGIC);
    memset(&header, 0, sizeof(header));
    header.version = cpu_to_le32(1);
    header.flags = cpu_to_le32(3); /* ?? */
    header.capacity = cpu_to_le64(total_size);
    header.granularity = cpu_to_le64(128);
    header.num_gtes_per_gte = cpu_to_le32(512);

    grains = (total_size + header.granularity - 1) / header.granularity;
    gt_size = ((header.num_gtes_per_gte * sizeof(uint32_t)) + 511) >> 9;
    gt_count = (grains + header.num_gtes_per_gte - 1) / header.num_gtes_per_gte;
    gd_size = (gt_count * sizeof(uint32_t) + 511) >> 9;

    header.desc_offset = 1;
    header.desc_size = 20;
    header.rgd_offset = header.desc_offset + header.desc_size;
    header.gd_offset = header.rgd_offset + gd_size + (gt_size * gt_count);
    header.grain_offset =
       ((header.gd_offset + gd_size + (gt_size * gt_count) +
         header.granularity - 1) / header.granularity) *
        header.granularity;

    header.desc_offset = cpu_to_le64(header.desc_offset);
    header.desc_size = cpu_to_le64(header.desc_size);
    header.rgd_offset = cpu_to_le64(header.rgd_offset);
    header.gd_offset = cpu_to_le64(header.gd_offset);
    header.grain_offset = cpu_to_le64(header.grain_offset);

    header.check_bytes[0] = 0xa;
    header.check_bytes[1] = 0x20;
    header.check_bytes[2] = 0xd;
    header.check_bytes[3] = 0xa;

    /* write all the data */
    write(fd, &magic, sizeof(magic));
    write(fd, &header, sizeof(header));

    ftruncate(fd, header.grain_offset << 9);

    /* write grain directory */
    lseek(fd, le64_to_cpu(header.rgd_offset) << 9, SEEK_SET);
    for (i = 0, tmp = header.rgd_offset + gd_size;
         i < gt_count; i++, tmp += gt_size)
        write(fd, &tmp, sizeof(tmp));

    /* write backup grain directory */
    lseek(fd, le64_to_cpu(header.gd_offset) << 9, SEEK_SET);
    for (i = 0, tmp = header.gd_offset + gd_size;
         i < gt_count; i++, tmp += gt_size)
        write(fd, &tmp, sizeof(tmp));

    /* compose the descriptor */
    real_filename = filename;
    if ((temp_str = strrchr(real_filename, '\\')) != NULL)
        real_filename = temp_str + 1;
    if ((temp_str = strrchr(real_filename, '/')) != NULL)
        real_filename = temp_str + 1;
    if ((temp_str = strrchr(real_filename, ':')) != NULL)
        real_filename = temp_str + 1;
    snprintf(desc, sizeof(desc), desc_template, (unsigned int)time(NULL),
             total_size, real_filename,
             (flags & BLOCK_FLAG_COMPAT6 ? 6 : 4),
             total_size / (int64_t)(63 * 16));

    /* write the descriptor */
    lseek(fd, le64_to_cpu(header.desc_offset) << 9, SEEK_SET);
    write(fd, desc, strlen(desc));

    close(fd);
    return 0;
}

static void vmdk_close(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;

    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    // try to close parent image, if exist
    vmdk_parent_close(s->hd);
    bdrv_delete(s->hd);
}

static void vmdk_flush(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    bdrv_flush(s->hd);
}

BlockDriver bdrv_vmdk = {
    .format_name	= "vmdk",
    .instance_size	= sizeof(BDRVVmdkState),
    .bdrv_probe		= vmdk_probe,
    .bdrv_open		= vmdk_open,
    .bdrv_read		= vmdk_read,
    .bdrv_write		= vmdk_write,
    .bdrv_close		= vmdk_close,
    .bdrv_create	= vmdk_create,
    .bdrv_flush		= vmdk_flush,
    .bdrv_is_allocated	= vmdk_is_allocated,
};
