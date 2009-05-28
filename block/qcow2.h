/*
 * Block driver for the QCOW version 2 format
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
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

#ifndef BLOCK_QCOW2_H
#define BLOCK_QCOW2_H

#include "aes.h"

#define QCOW_MAGIC (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xfb)
#define QCOW_VERSION 2

#define QCOW_CRYPT_NONE 0
#define QCOW_CRYPT_AES  1

#define QCOW_MAX_CRYPT_CLUSTERS 32

/* indicate that the refcount of the referenced cluster is exactly one. */
#define QCOW_OFLAG_COPIED     (1LL << 63)
/* indicate that the cluster is compressed (they never have the copied flag) */
#define QCOW_OFLAG_COMPRESSED (1LL << 62)

#define REFCOUNT_SHIFT 1 /* refcount size is 2 bytes */

#define MIN_CLUSTER_BITS 9
#define MAX_CLUSTER_BITS 16

#define L2_CACHE_SIZE 16

typedef struct QCowHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t backing_file_offset;
    uint32_t backing_file_size;
    uint32_t cluster_bits;
    uint64_t size; /* in bytes */
    uint32_t crypt_method;
    uint32_t l1_size; /* XXX: save number of clusters instead ? */
    uint64_t l1_table_offset;
    uint64_t refcount_table_offset;
    uint32_t refcount_table_clusters;
    uint32_t nb_snapshots;
    uint64_t snapshots_offset;
} QCowHeader;

typedef struct QCowSnapshot {
    uint64_t l1_table_offset;
    uint32_t l1_size;
    char *id_str;
    char *name;
    uint32_t vm_state_size;
    uint32_t date_sec;
    uint32_t date_nsec;
    uint64_t vm_clock_nsec;
} QCowSnapshot;

typedef struct BDRVQcowState {
    BlockDriverState *hd;
    int cluster_bits;
    int cluster_size;
    int cluster_sectors;
    int l2_bits;
    int l2_size;
    int l1_size;
    int l1_vm_state_index;
    int csize_shift;
    int csize_mask;
    uint64_t cluster_offset_mask;
    uint64_t l1_table_offset;
    uint64_t *l1_table;
    uint64_t *l2_cache;
    uint64_t l2_cache_offsets[L2_CACHE_SIZE];
    uint32_t l2_cache_counts[L2_CACHE_SIZE];
    uint8_t *cluster_cache;
    uint8_t *cluster_data;
    uint64_t cluster_cache_offset;

    uint64_t *refcount_table;
    uint64_t refcount_table_offset;
    uint32_t refcount_table_size;
    uint64_t refcount_block_cache_offset;
    uint16_t *refcount_block_cache;
    int64_t free_cluster_index;
    int64_t free_byte_offset;

    uint32_t crypt_method; /* current crypt method, 0 if no key yet */
    uint32_t crypt_method_header;
    AES_KEY aes_encrypt_key;
    AES_KEY aes_decrypt_key;
    uint64_t snapshots_offset;
    int snapshots_size;
    int nb_snapshots;
    QCowSnapshot *snapshots;
} BDRVQcowState;

/* XXX: use std qcow open function ? */
typedef struct QCowCreateState {
    int cluster_size;
    int cluster_bits;
    uint16_t *refcount_block;
    uint64_t *refcount_table;
    int64_t l1_table_offset;
    int64_t refcount_table_offset;
    int64_t refcount_block_offset;
} QCowCreateState;

/* XXX This could be private for qcow2-cluster.c */
typedef struct QCowL2Meta
{
    uint64_t offset;
    int n_start;
    int nb_available;
    int nb_clusters;
} QCowL2Meta;

static inline int size_to_clusters(BDRVQcowState *s, int64_t size)
{
    return (size + (s->cluster_size - 1)) >> s->cluster_bits;
}

// FIXME Need qcow2_ prefix to global functions

/* qcow2.c functions */
void l2_cache_reset(BlockDriverState *bs);
int backing_read1(BlockDriverState *bs,
                  int64_t sector_num, uint8_t *buf, int nb_sectors);

/* qcow2-refcount.c functions */
int refcount_init(BlockDriverState *bs);
void refcount_close(BlockDriverState *bs);

int64_t alloc_clusters(BlockDriverState *bs, int64_t size);
int64_t alloc_bytes(BlockDriverState *bs, int size);
void free_clusters(BlockDriverState *bs,
    int64_t offset, int64_t size);
void free_any_clusters(BlockDriverState *bs,
    uint64_t cluster_offset, int nb_clusters);

void create_refcount_update(QCowCreateState *s, int64_t offset, int64_t size);
int update_snapshot_refcount(BlockDriverState *bs,
                             int64_t l1_table_offset,
                             int l1_size,
                             int addend);

int check_refcounts(BlockDriverState *bs);

/* qcow2-cluster.c functions */
int grow_l1_table(BlockDriverState *bs, int min_size);
int decompress_cluster(BDRVQcowState *s, uint64_t cluster_offset);
void encrypt_sectors(BDRVQcowState *s, int64_t sector_num,
                     uint8_t *out_buf, const uint8_t *in_buf,
                     int nb_sectors, int enc,
                     const AES_KEY *key);

uint64_t get_cluster_offset(BlockDriverState *bs, uint64_t offset, int *num);
uint64_t alloc_cluster_offset(BlockDriverState *bs,
                              uint64_t offset,
                              int n_start, int n_end,
                              int *num, QCowL2Meta *m);
uint64_t alloc_compressed_cluster_offset(BlockDriverState *bs,
                                         uint64_t offset,
                                         int compressed_size);

int alloc_cluster_link_l2(BlockDriverState *bs, uint64_t cluster_offset,
    QCowL2Meta *m);

#endif
