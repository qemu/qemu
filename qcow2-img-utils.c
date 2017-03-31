#include "qemu/osdep.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi-visit.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qemu/cutils.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/qapi.h"
#include "block/qcow2.h"
#include "crypto/init.h"
#include "trace/control.h"
#include "qcow2-img-utils.h"
#include <getopt.h>

static void set_disk_info(BlockDriverState *bs, Snapshot_cache_t *cache, uint64_t *l1_table_offset, uint32_t *l1_size)
{
    BDRVQcow2State *s = bs->opaque;
    if(cache->snapshot_index != SNAPSHOT_MAX_INDEX){
        QCowSnapshot *snapshot = &s->snapshots[cache->snapshot_index];
        *l1_size = snapshot->l1_size;
        *l1_table_offset = snapshot->l1_table_offset;
    }else{
        *l1_size = s->l1_size;
        *l1_table_offset = s->l1_table_offset;
    }
}

uint64_t get_layer_disk_size(BlockDriverState *bs, int snapshot_index)
{
    BDRVQcow2State *s = bs->opaque;
    if(snapshot_index != SNAPSHOT_MAX_INDEX){
        QCowSnapshot *snapshot = &s->snapshots[snapshot_index];
        return snapshot->disk_size;
    }else{
        return bs->total_sectors<<9;
    }
}

static int get_max_l1_size(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int max_l1_size = s->l1_size;
    int i;
    for(i = 0; i < s->nb_snapshots; i++){
        QCowSnapshot *snapshot = &s->snapshots[i];
        max_l1_size = MAX(max_l1_size, snapshot->l1_size);
    }
    return max_l1_size;
}

uint64_t get_layer_cluster_nb(BlockDriverState *bs, int snapshot_index)
{
    return SIZE_TO_CLUSTER_NB(bs, get_layer_disk_size(bs, snapshot_index));
}

#define SNAPSHOT_MAX_INDEX (0x7fffffff)
int get_snapshot_cluster_l2_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t* ret_offset)
{
    BDRVQcow2State *s = bs->opaque;
    int64_t cluster_nb;
    uint32_t l1_size = 0;
    uint64_t l1_table_offset = 0, disk_size = DISK_SIZE(bs);

    if(!cache || cache->snapshot_index < 0){
        *ret_offset = 0;
        return 0;
    }

    if(cache->snapshot_index >= (int)s->nb_snapshots && SNAPSHOT_MAX_INDEX != cache->snapshot_index){
        error_report("error cache->snapshot_index is %d, totoal is %d", cache->snapshot_index, s->nb_snapshots);
        goto faild;
    }

    if(cache->sn_l1_table_cache.table == NULL){
        set_disk_info(bs, cache, &l1_table_offset, &l1_size);
        int max_l1_size = get_max_l1_size(bs);
        if (l1_size > 0) {
            cache->sn_l1_table_cache.table = g_malloc0(align_offset(max_l1_size * sizeof(uint64_t), 512));
            cache->sn_l1_be_table_cache.table = g_malloc0(align_offset(max_l1_size * sizeof(uint64_t), 512));
            int ret = bdrv_pread(bs->file, l1_table_offset, cache->sn_l1_table_cache.table, l1_size * sizeof(uint64_t));
            if (ret < 0) {
                error_report("bdrv_pread error ret %d, offset %ld size %ld", ret, l1_table_offset, l1_size * sizeof(uint64_t));
                goto faild;
            }
            cache->l1_size = l1_size;
            cache->l1_size_byte = l1_size * sizeof(uint64_t);
            cache->sn_l1_table_cache.cluster_offset = l1_table_offset;
            cache->sn_l1_be_table_cache.cluster_offset = l1_table_offset;
            memcpy(cache->sn_l1_be_table_cache.table, cache->sn_l1_table_cache.table, l1_size * sizeof(uint64_t));
            uint32_t i;
            for(i = 0;i < l1_size; i++) {
                be64_to_cpus(&cache->sn_l1_table_cache.table[i]);
                cache->sn_l1_table_cache.table[i] &= L1E_OFFSET_MASK;
            }
        }
    }

    cluster_nb = disk_size >> s->cluster_bits;
    if(cluster_index >= cluster_nb){
        error_report("cluster_index >= cluster_nb %ld %ld", cluster_index, cluster_nb);
faild:
        return -1;
    }
    uint64_t l1_index = cluster_index >> s->l2_bits;
    uint64_t l2_offset = cache->sn_l1_table_cache.table[l1_index];
    *ret_offset = l2_offset;

    return 0;
}

/**
 * ret < 0, error
 * ret == 0, l2 not allocated, so cluster is not allocated
 * ret > 0, acturely is 1, l2 is allocated, but cluster maybe not allocated
 */
int get_snapshot_cluster_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset)
{
    BDRVQcow2State *s = bs->opaque;
    uint64_t l2_offset;
    int ret = get_snapshot_cluster_l2_offset(bs, cache, cluster_index, &l2_offset);
    if(ret < 0){
        error_report("%s get_snapshot_cluster_l2_offset ret %d", __func__, ret);
        return ret;
    }

    if (!l2_offset) {
        ret = 0;// no l2 allocated
        *ret_offset = 0;
        goto out;
    }

    if(unlikely(!cache->sn_l2_table_cache.table)){
        cache->sn_l2_table_cache.table = g_malloc0(align_offset(s->cluster_size, 512));
    }

    if( l2_offset != cache->sn_l2_table_cache.cluster_offset){
        // LOG_DEBUG("misss cache offset 0x%x", l2_offset);
        ret = bdrv_pread(bs->file, l2_offset, cache->sn_l2_table_cache.table, s->cluster_size);
        if (ret < 0) {
            return ret;
        }
        cache->sn_l2_table_cache.cluster_offset = l2_offset;
        uint32_t i;
        for(i = 0; i < (s->cluster_size / sizeof(uint64_t)); i++){
            be64_to_cpus(&cache->sn_l2_table_cache.table[i]);
        }
    }

    int l2_index = cluster_index & ((1 << s->l2_bits) - 1);
    uint64_t cluster_offset = cache->sn_l2_table_cache.table[l2_index];
    *ret_offset = cluster_offset;

    ret = 1; // l2 is allocated

out:
    return ret;
}

int get_snapshot_cluster_offset_with_zero_flag(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset)
{
    int ret = get_snapshot_cluster_offset(bs, cache, cluster_index, ret_offset);
    if(ret < 0)
        return ret;
    *ret_offset &= (L2E_OFFSET_MASK | QCOW_OFLAG_ZERO);
    return ret;
}

int get_snapshot_cluster_pure_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, uint64_t *ret_offset)
{
    int ret = get_snapshot_cluster_offset(bs, cache, cluster_index, ret_offset);
    if(ret < 0)
        return ret;
    *ret_offset &= L2E_OFFSET_MASK;
    return ret;
}

static int __is_backing_file_allocated(BlockDriverState *bs, int64_t cluster_index, int32_t cluster_bits)
{
    uint64_t cluster_offset;
    // unsigned int sector_nb = (1<<cluster_bits)>>9;
    unsigned int bytes = (1<<cluster_bits);
    int64_t offset = cluster_index<<cluster_bits;

    if(!bs){
        return 0;
    }

    int ret = qcow2_get_cluster_offset(bs, offset, &bytes, &cluster_offset);
    if(ret < 0){
        error_report("error is_backing_file_allocated ret %d", ret);
        return ret;
    }
    if(cluster_offset == 0){
        return 0;
    }
    return 1;
}

static int is_backing_file_allocated(BlockDriverState *_backing_bs, int64_t cluster_index, int32_t cluster_bits, BlockDriverState **real_data_backing_bs)
{
    BlockDriverState *backing_bs = _backing_bs;
    *real_data_backing_bs = NULL;
    while(backing_bs){
        int ret = __is_backing_file_allocated(backing_bs, cluster_index, cluster_bits);
        if(ret == 1){
            *real_data_backing_bs = backing_bs;
            return ret;
        }
        if(ret < 0){
            return ret;
        }
        backing_bs = backing_bs->backing->bs;
    }
    return 0;
}

// a cluster one time, for full read
int read_snapshot_cluster_get_offset(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, ClusterData_t *data,
                                     uint64_t *out_offset, bool* backing_with_data, ClusterData_t *backing_data)
{
    BlockDriverState *backing_bs = bs->backing->bs;
    BlockDriverState *real_data_backing_bs = NULL;
    BDRVQcow2State *s = bs->opaque;
    uint64_t cluster_offset;

    int isbaking_alloc = is_backing_file_allocated(backing_bs, cluster_index, s->cluster_bits, &real_data_backing_bs);
    if(isbaking_alloc < 0){
        return -1;
    }
    if(out_offset)
        *out_offset = 0;
    if(backing_with_data)
        *backing_with_data = !!isbaking_alloc;

    int ret = get_snapshot_cluster_offset(bs, cache, cluster_index, &cluster_offset);
    if(ret < 0){
        error_report("%s get_snapshot_cluster_offset ret %d", __func__, ret);
        return ret;
    }
    bool zero_flag = false;
    int use_backing = 0;
    ret = qcow2_get_cluster_type(cluster_offset);
    switch(ret){
    case QCOW2_CLUSTER_UNALLOCATED:
        if(cache->read_backingfile){
            use_backing = isbaking_alloc;
        }
        if(use_backing == 1){
            // LOG_DEBUG("read snapshot cluster %ld is in backing file", cluster_index);
            break;
        }
        // use_backing == 0, backing al
        goto unalloc;
        break;
    case QCOW2_CLUSTER_ZERO: // zeros treated equal to normal
    case QCOW2_CLUSTER_NORMAL:
        if(out_offset)
            *out_offset = cluster_offset & (L2E_OFFSET_MASK | QCOW_OFLAG_ZERO);
        zero_flag = !!(cluster_offset & QCOW_OFLAG_ZERO);
        cluster_offset &= L2E_OFFSET_MASK;
        break;
    default:
        error_report("error unknown type ret %d", ret);
        return -1;
        break;
    }
    if(!data){
        goto normal;
    }

    data->cluset_index = cluster_index;

    if(isbaking_alloc && backing_data){
        ret = bdrv_pread(real_data_backing_bs->file, cluster_index<<s->cluster_bits, backing_data->buf, s->cluster_size);
        if(ret < 0){
            return -1;
        }
        backing_data->cluset_index = cluster_index;
    }

    if(use_backing){
        if(backing_data){
            memcpy(data->buf, backing_data->buf, s->cluster_size);
            ret = s->cluster_size;
        }else{
            ret = bdrv_pread(real_data_backing_bs->file, cluster_index<<s->cluster_bits, data->buf, s->cluster_size);
        }
    }else{
        if(!zero_flag){
            ret = bdrv_pread(bs->file, cluster_offset, data->buf, s->cluster_size);
        } else { // cluster_offset is zero, just use memset
            ret = s->cluster_size;
            memset(data->buf, 0, s->cluster_size);
        }
    }
    if(ret < 0){
        return -1;
    }
normal:
    if(zero_flag){
        // LOG_INFO("cluster index %ld is zeros", cluster_index);
        return 2;
    }
    return 1;
unalloc:
    return 0;
}

int read_snapshot_cluster(BlockDriverState *bs, Snapshot_cache_t *cache, int64_t cluster_index, ClusterData_t *data)
{
    return read_snapshot_cluster_get_offset(bs, cache, cluster_index, data, NULL, NULL, NULL);
}

// not include None allocated clusters
int count_full_image_clusters(BlockDriverState *bs, Snapshot_cache_t *cache, uint64_t *allocated_cluster_count, uint64_t start_cluster)
{
    uint64_t total_cluster_count;
    *allocated_cluster_count = 0;
    total_cluster_count = TOTAL_CLUSTER_NB(bs);
    uint64_t i;
    for(i = start_cluster; i < total_cluster_count; i ++){
        int ret = read_snapshot_cluster(bs, cache, i, NULL);
        if(ret < 0)
            return ret;

        if(ret == 1 || ret == 2){
            *allocated_cluster_count += 1;
        }
    }
    return 0;
}

// FOR INCREMENT READ, 1 means success, 0 not allocated, -1 error
int read_snapshot_cluster_increment(BlockDriverState *bs, Snapshot_cache_t *self_cache, Snapshot_cache_t *father_cache,
                                    int64_t cluster_index, ClusterData_t *data, bool* is_0_offset)
{
    uint64_t self_cluster_offset, pure_self_cluster_offset, father_cluster_offset;
    int ret1, ret2;
    ret1 = get_snapshot_cluster_offset_with_zero_flag(bs, self_cache, cluster_index, &self_cluster_offset);
    ret2 = get_snapshot_cluster_offset_with_zero_flag(bs, father_cache, cluster_index, &father_cluster_offset);
    if(ret1 < 0 || ret2 < 0){
        return -1; // failed
    }
    // if zeros flag set, self_cluster_offset is |1, if both |1, so self_cluster_offset == father_cluster_offset
    if(self_cluster_offset == father_cluster_offset || self_cluster_offset == 0){
        if(is_0_offset){
            *is_0_offset = (self_cluster_offset == 0);
        }
        return 0; // same as father or not allocated
    }
    // allocated or zeros
    pure_self_cluster_offset = self_cluster_offset & L2E_OFFSET_MASK;
    bool zero_flag = !!(self_cluster_offset & QCOW_OFLAG_ZERO);

    if(!data){
        goto out;
    }
    BDRVQcow2State *s = bs->opaque;
    data->cluset_index = cluster_index;
    if(zero_flag){
        memset(data->buf, 0, s->cluster_size);
    }else{
        int ret = bdrv_pread(bs->file, pure_self_cluster_offset, data->buf, s->cluster_size);
        if(ret < 0){
            return ret;
        }
    }
out:
    if(zero_flag){
        // LOG_INFO("cluster index %ld is zeros", cluster_index);
        return 2;
    }
    return 1;
}

int count_increment_clusters(BlockDriverState *bs, Snapshot_cache_t *self_cache, Snapshot_cache_t *father_cache, uint64_t *increment_cluster_count, uint64_t start_cluster)
{
    uint64_t total_cluster_count;
    *increment_cluster_count = 0;
    total_cluster_count = TOTAL_CLUSTER_NB(bs);
    uint64_t i;
    for(i = start_cluster; i < total_cluster_count; i++){
        int ret = read_snapshot_cluster_increment(bs, self_cache, father_cache, i, NULL, NULL);
        if(ret < 0)
            return ret;

        if(ret == 1 || ret == 2){
            (*increment_cluster_count) += 1;
        }
    }
    return 0;
}

