/*
 * Block driver for the VMDK format
 * 
 * Copyright (c) 2004 Fabrice Bellard
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
#include "vl.h"
#include "block_int.h"

/* XXX: this code is untested */
/* XXX: add write support */

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
} VMDK4Header;

#define L2_CACHE_SIZE 16

typedef struct BDRVVmdkState {
    int fd;
    int64_t l1_table_offset;
    uint32_t *l1_table;
    unsigned int l1_size;
    uint32_t l1_entry_sectors;

    unsigned int l2_size;
    uint32_t *l2_cache;
    uint32_t l2_cache_offsets[L2_CACHE_SIZE];
    uint32_t l2_cache_counts[L2_CACHE_SIZE];

    unsigned int cluster_sectors;
} BDRVVmdkState;

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

static int vmdk_open(BlockDriverState *bs, const char *filename)
{
    BDRVVmdkState *s = bs->opaque;
    int fd, i;
    uint32_t magic;
    int l1_size;

    fd = open(filename, O_RDONLY | O_BINARY | O_LARGEFILE);
    if (fd < 0)
        return -1;
    if (read(fd, &magic, sizeof(magic)) != sizeof(magic))
        goto fail;
    magic = be32_to_cpu(magic);
    if (magic == VMDK3_MAGIC) {
        VMDK3Header header;
        if (read(fd, &header, sizeof(header)) != 
            sizeof(header))
            goto fail;
        s->cluster_sectors = le32_to_cpu(header.granularity);
        s->l2_size = 1 << 9;
        s->l1_size = 1 << 6;
        bs->total_sectors = le32_to_cpu(header.disk_sectors);
        s->l1_table_offset = le32_to_cpu(header.l1dir_offset) * 512;
        s->l1_entry_sectors = s->l2_size * s->cluster_sectors;
    } else if (magic == VMDK4_MAGIC) {
        VMDK4Header header;
        
        if (read(fd, &header, sizeof(header)) != sizeof(header))
            goto fail;
        bs->total_sectors = le32_to_cpu(header.capacity);
        s->cluster_sectors = le32_to_cpu(header.granularity);
        s->l2_size = le32_to_cpu(header.num_gtes_per_gte);
        s->l1_entry_sectors = s->l2_size * s->cluster_sectors;
        if (s->l1_entry_sectors <= 0)
            goto fail;
        s->l1_size = (bs->total_sectors + s->l1_entry_sectors - 1) 
            / s->l1_entry_sectors;
        s->l1_table_offset = le64_to_cpu(header.rgd_offset) * 512;
    } else {
        goto fail;
    }
    /* read the L1 table */
    l1_size = s->l1_size * sizeof(uint32_t);
    s->l1_table = qemu_malloc(l1_size);
    if (!s->l1_table)
        goto fail;
    if (lseek(fd, s->l1_table_offset, SEEK_SET) == -1)
        goto fail;
    if (read(fd, s->l1_table, l1_size) != l1_size)
        goto fail;
    for(i = 0; i < s->l1_size; i++) {
        le32_to_cpus(&s->l1_table[i]);
    }

    s->l2_cache = qemu_malloc(s->l2_size * L2_CACHE_SIZE * sizeof(uint32_t));
    if (!s->l2_cache)
        goto fail;
    s->fd = fd;
    /* XXX: currently only read only */
    bs->read_only = 1;
    return 0;
 fail:
    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    close(fd);
    return -1;
}

static uint64_t get_cluster_offset(BlockDriverState *bs,
                                   uint64_t offset)
{
    BDRVVmdkState *s = bs->opaque;
    unsigned int l1_index, l2_offset, l2_index;
    int min_index, i, j;
    uint32_t min_count, *l2_table;
    uint64_t cluster_offset;
    
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
    lseek(s->fd, (int64_t)l2_offset * 512, SEEK_SET);
    if (read(s->fd, l2_table, s->l2_size * sizeof(uint32_t)) != 
        s->l2_size * sizeof(uint32_t))
        return 0;
    s->l2_cache_offsets[min_index] = l2_offset;
    s->l2_cache_counts[min_index] = 1;
 found:
    l2_index = ((offset >> 9) / s->cluster_sectors) % s->l2_size;
    cluster_offset = le32_to_cpu(l2_table[l2_index]);
    cluster_offset <<= 9;
    return cluster_offset;
}

static int vmdk_is_allocated(BlockDriverState *bs, int64_t sector_num, 
                             int nb_sectors, int *pnum)
{
    BDRVVmdkState *s = bs->opaque;
    int index_in_cluster, n;
    uint64_t cluster_offset;

    cluster_offset = get_cluster_offset(bs, sector_num << 9);
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
    int ret, index_in_cluster, n;
    uint64_t cluster_offset;
    
    while (nb_sectors > 0) {
        cluster_offset = get_cluster_offset(bs, sector_num << 9);
        index_in_cluster = sector_num % s->cluster_sectors;
        n = s->cluster_sectors - index_in_cluster;
        if (n > nb_sectors)
            n = nb_sectors;
        if (!cluster_offset) {
            memset(buf, 0, 512 * n);
        } else {
            lseek(s->fd, cluster_offset + index_in_cluster * 512, SEEK_SET);
            ret = read(s->fd, buf, n * 512);
            if (ret != n * 512) 
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
    return -1;
}

static void vmdk_close(BlockDriverState *bs)
{
    BDRVVmdkState *s = bs->opaque;
    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    close(s->fd);
}

BlockDriver bdrv_vmdk = {
    "vmdk",
    sizeof(BDRVVmdkState),
    vmdk_probe,
    vmdk_open,
    vmdk_read,
    vmdk_write,
    vmdk_close,
    NULL, /* no create yet */
    vmdk_is_allocated,
};
