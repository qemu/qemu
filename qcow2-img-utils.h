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

#endif /* QCOW2_IMG_UTILS_H_ */
