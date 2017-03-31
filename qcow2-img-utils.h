/*
 * qcow2-img-utils.h
 *
 *  Created on: Feb 13, 2017
 *      Author: zhangzm
 */

#ifndef QCOW2_IMG_UTILS_H_
#define QCOW2_IMG_UTILS_H_

struct local_cluster_cache{
    bool cache_dirty;
    uint64_t cluster_offset;
    uint64_t *table;
};

typedef struct snapshot_cache{
    // for read_snapshot_cluster
    int snapshot_index;
    int l1_size_byte; // byte
    int l1_size; // nb
    bool read_backingfile;
    struct local_cluster_cache sn_l1_be_table_cache; // just cache one
    struct local_cluster_cache sn_l1_table_cache; // just cache one
    struct local_cluster_cache sn_l2_table_cache; // just cache one
} Snapshot_cache_t;

typedef struct ClusterData{
    uint64_t cluset_index;
    char buf[0];
}ClusterData_t;

static inline void init_cache(Snapshot_cache_t * cache, int snapshot_index)
{
    memset(cache, 0, sizeof(Snapshot_cache_t));
    cache->snapshot_index = snapshot_index;
}

#define DISK_SIZE(bs) (unsigned long int)(bs->total_sectors * BDRV_SECTOR_SIZE)
#define TOTAL_CLUSTER_NB(bs) (DISK_SIZE(bs) >> ((BDRVQcow2State *)bs->opaque)->cluster_bits)
#define SNAPSHOT_MAX_INDEX (0x7fffffff)
#define SIZE_TO_CLUSTER_NB(bs, size) ((size) >> ((BDRVQcow2State *)bs->opaque)->cluster_bits)

int get_snapshot_cluster_l2_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t* ret_offset);
int get_snapshot_cluster_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset);
int get_snapshot_cluster_offset_with_zero_flag(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset);
int get_snapshot_cluster_pure_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset);
int read_snapshot_cluster_get_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, ClusterData_t *data,
                                     uint64_t *out_offset, bool* backing_with_data, ClusterData_t *backing_data);
int read_snapshot_cluster(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, ClusterData_t *data);
int count_full_image_clusters(BlockDriverState *bs, Snapshot_cache_t *cache, uint64_t *allocated_cluster_count, uint64_t start_cluster);
int read_snapshot_cluster_increment(BlockDriverState *bs, Snapshot_cache_t *self_cache, Snapshot_cache_t *father_cache,
                                    int64_t cluster_index, ClusterData_t *data, bool* is_0_offset);
int count_increment_clusters(BlockDriverState *bs, Snapshot_cache_t *self_cache, Snapshot_cache_t *father_cache,
                             uint64_t *increment_cluster_count, uint64_t start_cluster);
uint64_t get_layer_disk_size(BlockDriverState *bs, int snapshot_index);
uint64_t get_layer_cluster_nb(BlockDriverState *bs, int snapshot_index);

#endif /* QCOW2_IMG_UTILS_H_ */
