/*
 * Block layer qmp and info dump related functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/cutils.h"
#include "block/qapi.h"
#include "block/block_int.h"
#include "block/dirty-bitmap.h"
#include "block/throttle-groups.h"
#include "block/write-threshold.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/qemu-print.h"
#include "sysemu/block-backend.h"

BlockDeviceInfo *bdrv_block_device_info(BlockBackend *blk,
                                        BlockDriverState *bs,
                                        bool flat,
                                        Error **errp)
{
    ImageInfo **p_image_info;
    ImageInfo *backing_info;
    BlockDriverState *backing;
    BlockDeviceInfo *info;
    ERRP_GUARD();

    if (!bs->drv) {
        error_setg(errp, "Block device %s is ejected", bs->node_name);
        return NULL;
    }

    bdrv_refresh_filename(bs);

    info = g_malloc0(sizeof(*info));
    info->file                   = g_strdup(bs->filename);
    info->ro                     = bdrv_is_read_only(bs);
    info->drv                    = g_strdup(bs->drv->format_name);
    info->encrypted              = bs->encrypted;

    info->cache = g_new(BlockdevCacheInfo, 1);
    *info->cache = (BlockdevCacheInfo) {
        .writeback      = blk ? blk_enable_write_cache(blk) : true,
        .direct         = !!(bs->open_flags & BDRV_O_NOCACHE),
        .no_flush       = !!(bs->open_flags & BDRV_O_NO_FLUSH),
    };

    if (bs->node_name[0]) {
        info->node_name = g_strdup(bs->node_name);
    }

    backing = bdrv_cow_bs(bs);
    if (backing) {
        info->backing_file = g_strdup(backing->filename);
    }

    if (!QLIST_EMPTY(&bs->dirty_bitmaps)) {
        info->has_dirty_bitmaps = true;
        info->dirty_bitmaps = bdrv_query_dirty_bitmaps(bs);
    }

    info->detect_zeroes = bs->detect_zeroes;

    if (blk && blk_get_public(blk)->throttle_group_member.throttle_state) {
        ThrottleConfig cfg;
        BlockBackendPublic *blkp = blk_get_public(blk);

        throttle_group_get_config(&blkp->throttle_group_member, &cfg);

        info->bps     = cfg.buckets[THROTTLE_BPS_TOTAL].avg;
        info->bps_rd  = cfg.buckets[THROTTLE_BPS_READ].avg;
        info->bps_wr  = cfg.buckets[THROTTLE_BPS_WRITE].avg;

        info->iops    = cfg.buckets[THROTTLE_OPS_TOTAL].avg;
        info->iops_rd = cfg.buckets[THROTTLE_OPS_READ].avg;
        info->iops_wr = cfg.buckets[THROTTLE_OPS_WRITE].avg;

        info->has_bps_max     = cfg.buckets[THROTTLE_BPS_TOTAL].max;
        info->bps_max         = cfg.buckets[THROTTLE_BPS_TOTAL].max;
        info->has_bps_rd_max  = cfg.buckets[THROTTLE_BPS_READ].max;
        info->bps_rd_max      = cfg.buckets[THROTTLE_BPS_READ].max;
        info->has_bps_wr_max  = cfg.buckets[THROTTLE_BPS_WRITE].max;
        info->bps_wr_max      = cfg.buckets[THROTTLE_BPS_WRITE].max;

        info->has_iops_max    = cfg.buckets[THROTTLE_OPS_TOTAL].max;
        info->iops_max        = cfg.buckets[THROTTLE_OPS_TOTAL].max;
        info->has_iops_rd_max = cfg.buckets[THROTTLE_OPS_READ].max;
        info->iops_rd_max     = cfg.buckets[THROTTLE_OPS_READ].max;
        info->has_iops_wr_max = cfg.buckets[THROTTLE_OPS_WRITE].max;
        info->iops_wr_max     = cfg.buckets[THROTTLE_OPS_WRITE].max;

        info->has_bps_max_length     = info->has_bps_max;
        info->bps_max_length         =
            cfg.buckets[THROTTLE_BPS_TOTAL].burst_length;
        info->has_bps_rd_max_length  = info->has_bps_rd_max;
        info->bps_rd_max_length      =
            cfg.buckets[THROTTLE_BPS_READ].burst_length;
        info->has_bps_wr_max_length  = info->has_bps_wr_max;
        info->bps_wr_max_length      =
            cfg.buckets[THROTTLE_BPS_WRITE].burst_length;

        info->has_iops_max_length    = info->has_iops_max;
        info->iops_max_length        =
            cfg.buckets[THROTTLE_OPS_TOTAL].burst_length;
        info->has_iops_rd_max_length = info->has_iops_rd_max;
        info->iops_rd_max_length     =
            cfg.buckets[THROTTLE_OPS_READ].burst_length;
        info->has_iops_wr_max_length = info->has_iops_wr_max;
        info->iops_wr_max_length     =
            cfg.buckets[THROTTLE_OPS_WRITE].burst_length;

        info->has_iops_size = cfg.op_size;
        info->iops_size = cfg.op_size;

        info->group =
            g_strdup(throttle_group_get_name(&blkp->throttle_group_member));
    }

    info->write_threshold = bdrv_write_threshold_get(bs);

    p_image_info = &info->image;
    info->backing_file_depth = 0;

    /*
     * Skip automatically inserted nodes that the user isn't aware of for
     * query-block (blk != NULL), but not for query-named-block-nodes
     */
    bdrv_query_image_info(bs, p_image_info, flat, blk != NULL, errp);
    if (*errp) {
        qapi_free_BlockDeviceInfo(info);
        return NULL;
    }

    backing_info = info->image->backing_image;
    while (backing_info) {
        info->backing_file_depth++;
        backing_info = backing_info->backing_image;
    }

    return info;
}

/*
 * Returns 0 on success, with *p_list either set to describe snapshot
 * information, or NULL because there are no snapshots.  Returns -errno on
 * error, with *p_list untouched.
 */
int bdrv_query_snapshot_info_list(BlockDriverState *bs,
                                  SnapshotInfoList **p_list,
                                  Error **errp)
{
    int i, sn_count;
    QEMUSnapshotInfo *sn_tab = NULL;
    SnapshotInfoList *head = NULL, **tail = &head;
    SnapshotInfo *info;

    sn_count = bdrv_snapshot_list(bs, &sn_tab);
    if (sn_count < 0) {
        const char *dev = bdrv_get_device_name(bs);
        switch (sn_count) {
        case -ENOMEDIUM:
            error_setg(errp, "Device '%s' is not inserted", dev);
            break;
        case -ENOTSUP:
            error_setg(errp,
                       "Device '%s' does not support internal snapshots",
                       dev);
            break;
        default:
            error_setg_errno(errp, -sn_count,
                             "Can't list snapshots of device '%s'", dev);
            break;
        }
        return sn_count;
    }

    for (i = 0; i < sn_count; i++) {
        info = g_new0(SnapshotInfo, 1);
        info->id            = g_strdup(sn_tab[i].id_str);
        info->name          = g_strdup(sn_tab[i].name);
        info->vm_state_size = sn_tab[i].vm_state_size;
        info->date_sec      = sn_tab[i].date_sec;
        info->date_nsec     = sn_tab[i].date_nsec;
        info->vm_clock_sec  = sn_tab[i].vm_clock_nsec / 1000000000;
        info->vm_clock_nsec = sn_tab[i].vm_clock_nsec % 1000000000;
        info->icount        = sn_tab[i].icount;
        info->has_icount    = sn_tab[i].icount != -1ULL;

        QAPI_LIST_APPEND(tail, info);
    }

    g_free(sn_tab);
    *p_list = head;
    return 0;
}

/**
 * Helper function for other query info functions.  Store information about @bs
 * in @info, setting @errp on error.
 */
static void GRAPH_RDLOCK
bdrv_do_query_node_info(BlockDriverState *bs, BlockNodeInfo *info, Error **errp)
{
    int64_t size;
    const char *backing_filename;
    BlockDriverInfo bdi;
    int ret;
    Error *err = NULL;

    aio_context_acquire(bdrv_get_aio_context(bs));

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "Can't get image size '%s'",
                         bs->exact_filename);
        goto out;
    }

    bdrv_refresh_filename(bs);

    info->filename        = g_strdup(bs->filename);
    info->format          = g_strdup(bdrv_get_format_name(bs));
    info->virtual_size    = size;
    info->actual_size     = bdrv_get_allocated_file_size(bs);
    info->has_actual_size = info->actual_size >= 0;
    if (bs->encrypted) {
        info->encrypted = true;
        info->has_encrypted = true;
    }
    if (bdrv_get_info(bs, &bdi) >= 0) {
        if (bdi.cluster_size != 0) {
            info->cluster_size = bdi.cluster_size;
            info->has_cluster_size = true;
        }
        info->dirty_flag = bdi.is_dirty;
        info->has_dirty_flag = true;
    }
    info->format_specific = bdrv_get_specific_info(bs, &err);
    if (err) {
        error_propagate(errp, err);
        goto out;
    }
    backing_filename = bs->backing_file;
    if (backing_filename[0] != '\0') {
        char *backing_filename2;

        info->backing_filename = g_strdup(backing_filename);
        backing_filename2 = bdrv_get_full_backing_filename(bs, NULL);

        /* Always report the full_backing_filename if present, even if it's the
         * same as backing_filename. That they are same is useful info. */
        if (backing_filename2) {
            info->full_backing_filename = g_strdup(backing_filename2);
        }

        if (bs->backing_format[0]) {
            info->backing_filename_format = g_strdup(bs->backing_format);
        }
        g_free(backing_filename2);
    }

    ret = bdrv_query_snapshot_info_list(bs, &info->snapshots, &err);
    switch (ret) {
    case 0:
        if (info->snapshots) {
            info->has_snapshots = true;
        }
        break;
    /* recoverable error */
    case -ENOMEDIUM:
    case -ENOTSUP:
        error_free(err);
        break;
    default:
        error_propagate(errp, err);
        goto out;
    }

out:
    aio_context_release(bdrv_get_aio_context(bs));
}

/**
 * bdrv_query_image_info:
 * @bs: block node to examine
 * @p_info: location to store image information
 * @flat: skip backing node information
 * @skip_implicit_filters: skip implicit filters in the backing chain
 * @errp: location to store error information
 *
 * Store image information in @p_info, potentially recursively covering the
 * backing chain.
 *
 * If @flat is true, do not query backing image information, i.e.
 * (*p_info)->has_backing_image will be set to false and
 * (*p_info)->backing_image to NULL even when the image does in fact have a
 * backing image.
 *
 * If @skip_implicit_filters is true, implicit filter nodes in the backing chain
 * will be skipped when querying backing image information.
 * (@skip_implicit_filters is ignored when @flat is true.)
 *
 * @p_info will be set only on success. On error, store error in @errp.
 */
void bdrv_query_image_info(BlockDriverState *bs,
                           ImageInfo **p_info,
                           bool flat,
                           bool skip_implicit_filters,
                           Error **errp)
{
    ImageInfo *info;
    ERRP_GUARD();

    info = g_new0(ImageInfo, 1);
    bdrv_do_query_node_info(bs, qapi_ImageInfo_base(info), errp);
    if (*errp) {
        goto fail;
    }

    if (!flat) {
        BlockDriverState *backing;

        /*
         * Use any filtered child here (for backwards compatibility to when
         * we always took bs->backing, which might be any filtered child).
         */
        backing = bdrv_filter_or_cow_bs(bs);
        if (skip_implicit_filters) {
            backing = bdrv_skip_implicit_filters(backing);
        }

        if (backing) {
            bdrv_query_image_info(backing, &info->backing_image, false,
                                  skip_implicit_filters, errp);
            if (*errp) {
                goto fail;
            }
        }
    }

    *p_info = info;
    return;

fail:
    assert(*errp);
    qapi_free_ImageInfo(info);
}

/**
 * bdrv_query_block_graph_info:
 * @bs: root node to start from
 * @p_info: location to store image information
 * @errp: location to store error information
 *
 * Store image information about the graph starting from @bs in @p_info.
 *
 * @p_info will be set only on success. On error, store error in @errp.
 */
void bdrv_query_block_graph_info(BlockDriverState *bs,
                                 BlockGraphInfo **p_info,
                                 Error **errp)
{
    BlockGraphInfo *info;
    BlockChildInfoList **children_list_tail;
    BdrvChild *c;
    ERRP_GUARD();

    info = g_new0(BlockGraphInfo, 1);
    bdrv_do_query_node_info(bs, qapi_BlockGraphInfo_base(info), errp);
    if (*errp) {
        goto fail;
    }

    children_list_tail = &info->children;

    QLIST_FOREACH(c, &bs->children, next) {
        BlockChildInfo *c_info;

        c_info = g_new0(BlockChildInfo, 1);
        QAPI_LIST_APPEND(children_list_tail, c_info);

        c_info->name = g_strdup(c->name);
        bdrv_query_block_graph_info(c->bs, &c_info->info, errp);
        if (*errp) {
            goto fail;
        }
    }

    *p_info = info;
    return;

fail:
    assert(*errp != NULL);
    qapi_free_BlockGraphInfo(info);
}

/* @p_info will be set only on success. */
static void GRAPH_RDLOCK
bdrv_query_info(BlockBackend *blk, BlockInfo **p_info, Error **errp)
{
    BlockInfo *info = g_malloc0(sizeof(*info));
    BlockDriverState *bs = blk_bs(blk);
    char *qdev;

    /* Skip automatically inserted nodes that the user isn't aware of */
    bs = bdrv_skip_implicit_filters(bs);

    info->device = g_strdup(blk_name(blk));
    info->type = g_strdup("unknown");
    info->locked = blk_dev_is_medium_locked(blk);
    info->removable = blk_dev_has_removable_media(blk);

    qdev = blk_get_attached_dev_id(blk);
    if (qdev && *qdev) {
        info->qdev = qdev;
    } else {
        g_free(qdev);
    }

    if (blk_dev_has_tray(blk)) {
        info->has_tray_open = true;
        info->tray_open = blk_dev_is_tray_open(blk);
    }

    if (blk_iostatus_is_enabled(blk)) {
        info->has_io_status = true;
        info->io_status = blk_iostatus(blk);
    }

    if (bs && bs->drv) {
        info->inserted = bdrv_block_device_info(blk, bs, false, errp);
        if (info->inserted == NULL) {
            goto err;
        }
    }

    *p_info = info;
    return;

 err:
    qapi_free_BlockInfo(info);
}

static uint64List *uint64_list(uint64_t *list, int size)
{
    int i;
    uint64List *out_list = NULL;
    uint64List **tail = &out_list;

    for (i = 0; i < size; i++) {
        QAPI_LIST_APPEND(tail, list[i]);
    }

    return out_list;
}

static BlockLatencyHistogramInfo *
bdrv_latency_histogram_stats(BlockLatencyHistogram *hist)
{
    BlockLatencyHistogramInfo *info;

    if (!hist->bins) {
        return NULL;
    }

    info = g_new0(BlockLatencyHistogramInfo, 1);
    info->boundaries = uint64_list(hist->boundaries, hist->nbins - 1);
    info->bins = uint64_list(hist->bins, hist->nbins);
    return info;
}

static void bdrv_query_blk_stats(BlockDeviceStats *ds, BlockBackend *blk)
{
    BlockAcctStats *stats = blk_get_stats(blk);
    BlockAcctTimedStats *ts = NULL;
    BlockLatencyHistogram *hgram;

    ds->rd_bytes = stats->nr_bytes[BLOCK_ACCT_READ];
    ds->wr_bytes = stats->nr_bytes[BLOCK_ACCT_WRITE];
    ds->zone_append_bytes = stats->nr_bytes[BLOCK_ACCT_ZONE_APPEND];
    ds->unmap_bytes = stats->nr_bytes[BLOCK_ACCT_UNMAP];
    ds->rd_operations = stats->nr_ops[BLOCK_ACCT_READ];
    ds->wr_operations = stats->nr_ops[BLOCK_ACCT_WRITE];
    ds->zone_append_operations = stats->nr_ops[BLOCK_ACCT_ZONE_APPEND];
    ds->unmap_operations = stats->nr_ops[BLOCK_ACCT_UNMAP];

    ds->failed_rd_operations = stats->failed_ops[BLOCK_ACCT_READ];
    ds->failed_wr_operations = stats->failed_ops[BLOCK_ACCT_WRITE];
    ds->failed_zone_append_operations =
        stats->failed_ops[BLOCK_ACCT_ZONE_APPEND];
    ds->failed_flush_operations = stats->failed_ops[BLOCK_ACCT_FLUSH];
    ds->failed_unmap_operations = stats->failed_ops[BLOCK_ACCT_UNMAP];

    ds->invalid_rd_operations = stats->invalid_ops[BLOCK_ACCT_READ];
    ds->invalid_wr_operations = stats->invalid_ops[BLOCK_ACCT_WRITE];
    ds->invalid_zone_append_operations =
        stats->invalid_ops[BLOCK_ACCT_ZONE_APPEND];
    ds->invalid_flush_operations =
        stats->invalid_ops[BLOCK_ACCT_FLUSH];
    ds->invalid_unmap_operations = stats->invalid_ops[BLOCK_ACCT_UNMAP];

    ds->rd_merged = stats->merged[BLOCK_ACCT_READ];
    ds->wr_merged = stats->merged[BLOCK_ACCT_WRITE];
    ds->zone_append_merged = stats->merged[BLOCK_ACCT_ZONE_APPEND];
    ds->unmap_merged = stats->merged[BLOCK_ACCT_UNMAP];
    ds->flush_operations = stats->nr_ops[BLOCK_ACCT_FLUSH];
    ds->wr_total_time_ns = stats->total_time_ns[BLOCK_ACCT_WRITE];
    ds->zone_append_total_time_ns =
        stats->total_time_ns[BLOCK_ACCT_ZONE_APPEND];
    ds->rd_total_time_ns = stats->total_time_ns[BLOCK_ACCT_READ];
    ds->flush_total_time_ns = stats->total_time_ns[BLOCK_ACCT_FLUSH];
    ds->unmap_total_time_ns = stats->total_time_ns[BLOCK_ACCT_UNMAP];

    ds->has_idle_time_ns = stats->last_access_time_ns > 0;
    if (ds->has_idle_time_ns) {
        ds->idle_time_ns = block_acct_idle_time_ns(stats);
    }

    ds->account_invalid = stats->account_invalid;
    ds->account_failed = stats->account_failed;

    while ((ts = block_acct_interval_next(stats, ts))) {
        BlockDeviceTimedStats *dev_stats = g_malloc0(sizeof(*dev_stats));

        TimedAverage *rd = &ts->latency[BLOCK_ACCT_READ];
        TimedAverage *wr = &ts->latency[BLOCK_ACCT_WRITE];
        TimedAverage *zap = &ts->latency[BLOCK_ACCT_ZONE_APPEND];
        TimedAverage *fl = &ts->latency[BLOCK_ACCT_FLUSH];

        dev_stats->interval_length = ts->interval_length;

        dev_stats->min_rd_latency_ns = timed_average_min(rd);
        dev_stats->max_rd_latency_ns = timed_average_max(rd);
        dev_stats->avg_rd_latency_ns = timed_average_avg(rd);

        dev_stats->min_wr_latency_ns = timed_average_min(wr);
        dev_stats->max_wr_latency_ns = timed_average_max(wr);
        dev_stats->avg_wr_latency_ns = timed_average_avg(wr);

        dev_stats->min_zone_append_latency_ns = timed_average_min(zap);
        dev_stats->max_zone_append_latency_ns = timed_average_max(zap);
        dev_stats->avg_zone_append_latency_ns = timed_average_avg(zap);

        dev_stats->min_flush_latency_ns = timed_average_min(fl);
        dev_stats->max_flush_latency_ns = timed_average_max(fl);
        dev_stats->avg_flush_latency_ns = timed_average_avg(fl);

        dev_stats->avg_rd_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_READ);
        dev_stats->avg_wr_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_WRITE);
        dev_stats->avg_zone_append_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_ZONE_APPEND);

        QAPI_LIST_PREPEND(ds->timed_stats, dev_stats);
    }

    hgram = stats->latency_histogram;
    ds->rd_latency_histogram
        = bdrv_latency_histogram_stats(&hgram[BLOCK_ACCT_READ]);
    ds->wr_latency_histogram
        = bdrv_latency_histogram_stats(&hgram[BLOCK_ACCT_WRITE]);
    ds->zone_append_latency_histogram
        = bdrv_latency_histogram_stats(&hgram[BLOCK_ACCT_ZONE_APPEND]);
    ds->flush_latency_histogram
        = bdrv_latency_histogram_stats(&hgram[BLOCK_ACCT_FLUSH]);
}

static BlockStats * GRAPH_RDLOCK
bdrv_query_bds_stats(BlockDriverState *bs, bool blk_level)
{
    BdrvChild *parent_child;
    BlockDriverState *filter_or_cow_bs;
    BlockStats *s = NULL;

    s = g_malloc0(sizeof(*s));
    s->stats = g_malloc0(sizeof(*s->stats));

    if (!bs) {
        return s;
    }

    /* Skip automatically inserted nodes that the user isn't aware of in
     * a BlockBackend-level command. Stay at the exact node for a node-level
     * command. */
    if (blk_level) {
        bs = bdrv_skip_implicit_filters(bs);
    }

    if (bdrv_get_node_name(bs)[0]) {
        s->node_name = g_strdup(bdrv_get_node_name(bs));
    }

    s->stats->wr_highest_offset = stat64_get(&bs->wr_highest_offset);

    s->driver_specific = bdrv_get_specific_stats(bs);

    parent_child = bdrv_primary_child(bs);
    if (!parent_child ||
        !(parent_child->role & (BDRV_CHILD_DATA | BDRV_CHILD_FILTERED)))
    {
        BdrvChild *c;

        /*
         * Look for a unique data-storing child.  We do not need to look for
         * filtered children, as there would be only one and it would have been
         * the primary child.
         */
        parent_child = NULL;
        QLIST_FOREACH(c, &bs->children, next) {
            if (c->role & BDRV_CHILD_DATA) {
                if (parent_child) {
                    /*
                     * There are multiple data-storing children and we cannot
                     * choose between them.
                     */
                    parent_child = NULL;
                    break;
                }
                parent_child = c;
            }
        }
    }
    if (parent_child) {
        s->parent = bdrv_query_bds_stats(parent_child->bs, blk_level);
    }

    filter_or_cow_bs = bdrv_filter_or_cow_bs(bs);
    if (blk_level && filter_or_cow_bs) {
        /*
         * Put any filtered or COW child here (for backwards
         * compatibility to when we put bs0->backing here, which might
         * be either)
         */
        s->backing = bdrv_query_bds_stats(filter_or_cow_bs, blk_level);
    }

    return s;
}

BlockInfoList *qmp_query_block(Error **errp)
{
    BlockInfoList *head = NULL, **p_next = &head;
    BlockBackend *blk;
    Error *local_err = NULL;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    for (blk = blk_all_next(NULL); blk; blk = blk_all_next(blk)) {
        BlockInfoList *info;

        if (!*blk_name(blk) && !blk_get_attached_dev(blk)) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        bdrv_query_info(blk, &info->value, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            g_free(info);
            qapi_free_BlockInfoList(head);
            return NULL;
        }

        *p_next = info;
        p_next = &info->next;
    }

    return head;
}

BlockStatsList *qmp_query_blockstats(bool has_query_nodes,
                                     bool query_nodes,
                                     Error **errp)
{
    BlockStatsList *head = NULL, **tail = &head;
    BlockBackend *blk;
    BlockDriverState *bs;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    /* Just to be safe if query_nodes is not always initialized */
    if (has_query_nodes && query_nodes) {
        for (bs = bdrv_next_node(NULL); bs; bs = bdrv_next_node(bs)) {
            AioContext *ctx = bdrv_get_aio_context(bs);

            aio_context_acquire(ctx);
            QAPI_LIST_APPEND(tail, bdrv_query_bds_stats(bs, false));
            aio_context_release(ctx);
        }
    } else {
        for (blk = blk_all_next(NULL); blk; blk = blk_all_next(blk)) {
            AioContext *ctx = blk_get_aio_context(blk);
            BlockStats *s;
            char *qdev;

            if (!*blk_name(blk) && !blk_get_attached_dev(blk)) {
                continue;
            }

            aio_context_acquire(ctx);
            s = bdrv_query_bds_stats(blk_bs(blk), true);
            s->device = g_strdup(blk_name(blk));

            qdev = blk_get_attached_dev_id(blk);
            if (qdev && *qdev) {
                s->qdev = qdev;
            } else {
                g_free(qdev);
            }

            bdrv_query_blk_stats(s->stats, blk);
            aio_context_release(ctx);

            QAPI_LIST_APPEND(tail, s);
        }
    }

    return head;
}

void bdrv_snapshot_dump(QEMUSnapshotInfo *sn)
{
    char clock_buf[128];
    char icount_buf[128] = {0};
    int64_t secs;
    char *sizing = NULL;

    if (!sn) {
        qemu_printf("%-10s%-17s%8s%20s%13s%11s",
                    "ID", "TAG", "VM SIZE", "DATE", "VM CLOCK", "ICOUNT");
    } else {
        g_autoptr(GDateTime) date = g_date_time_new_from_unix_local(sn->date_sec);
        g_autofree char *date_buf = g_date_time_format(date, "%Y-%m-%d %H:%M:%S");

        secs = sn->vm_clock_nsec / 1000000000;
        snprintf(clock_buf, sizeof(clock_buf),
                 "%02d:%02d:%02d.%03d",
                 (int)(secs / 3600),
                 (int)((secs / 60) % 60),
                 (int)(secs % 60),
                 (int)((sn->vm_clock_nsec / 1000000) % 1000));
        sizing = size_to_str(sn->vm_state_size);
        if (sn->icount != -1ULL) {
            snprintf(icount_buf, sizeof(icount_buf),
                "%"PRId64, sn->icount);
        }
        qemu_printf("%-9s %-16s %8s%20s%13s%11s",
                    sn->id_str, sn->name,
                    sizing,
                    date_buf,
                    clock_buf,
                    icount_buf);
    }
    g_free(sizing);
}

static void dump_qdict(int indentation, QDict *dict);
static void dump_qlist(int indentation, QList *list);

static void dump_qobject(int comp_indent, QObject *obj)
{
    switch (qobject_type(obj)) {
        case QTYPE_QNUM: {
            QNum *value = qobject_to(QNum, obj);
            char *tmp = qnum_to_string(value);
            qemu_printf("%s", tmp);
            g_free(tmp);
            break;
        }
        case QTYPE_QSTRING: {
            QString *value = qobject_to(QString, obj);
            qemu_printf("%s", qstring_get_str(value));
            break;
        }
        case QTYPE_QDICT: {
            QDict *value = qobject_to(QDict, obj);
            dump_qdict(comp_indent, value);
            break;
        }
        case QTYPE_QLIST: {
            QList *value = qobject_to(QList, obj);
            dump_qlist(comp_indent, value);
            break;
        }
        case QTYPE_QBOOL: {
            QBool *value = qobject_to(QBool, obj);
            qemu_printf("%s", qbool_get_bool(value) ? "true" : "false");
            break;
        }
        default:
            abort();
    }
}

static void dump_qlist(int indentation, QList *list)
{
    const QListEntry *entry;
    int i = 0;

    for (entry = qlist_first(list); entry; entry = qlist_next(entry), i++) {
        QType type = qobject_type(entry->value);
        bool composite = (type == QTYPE_QDICT || type == QTYPE_QLIST);
        qemu_printf("%*s[%i]:%c", indentation * 4, "", i,
                    composite ? '\n' : ' ');
        dump_qobject(indentation + 1, entry->value);
        if (!composite) {
            qemu_printf("\n");
        }
    }
}

static void dump_qdict(int indentation, QDict *dict)
{
    const QDictEntry *entry;

    for (entry = qdict_first(dict); entry; entry = qdict_next(dict, entry)) {
        QType type = qobject_type(entry->value);
        bool composite = (type == QTYPE_QDICT || type == QTYPE_QLIST);
        char *key = g_malloc(strlen(entry->key) + 1);
        int i;

        /* replace dashes with spaces in key (variable) names */
        for (i = 0; entry->key[i]; i++) {
            key[i] = entry->key[i] == '-' ? ' ' : entry->key[i];
        }
        key[i] = 0;
        qemu_printf("%*s%s:%c", indentation * 4, "", key,
                    composite ? '\n' : ' ');
        dump_qobject(indentation + 1, entry->value);
        if (!composite) {
            qemu_printf("\n");
        }
        g_free(key);
    }
}

/*
 * Return whether dumping the given QObject with dump_qobject() would
 * yield an empty dump, i.e. not print anything.
 */
static bool qobject_is_empty_dump(const QObject *obj)
{
    switch (qobject_type(obj)) {
    case QTYPE_QNUM:
    case QTYPE_QSTRING:
    case QTYPE_QBOOL:
        return false;

    case QTYPE_QDICT:
        return qdict_size(qobject_to(QDict, obj)) == 0;

    case QTYPE_QLIST:
        return qlist_empty(qobject_to(QList, obj));

    default:
        abort();
    }
}

/**
 * Dumps the given ImageInfoSpecific object in a human-readable form,
 * prepending an optional prefix if the dump is not empty.
 */
void bdrv_image_info_specific_dump(ImageInfoSpecific *info_spec,
                                   const char *prefix,
                                   int indentation)
{
    QObject *obj, *data;
    Visitor *v = qobject_output_visitor_new(&obj);

    visit_type_ImageInfoSpecific(v, NULL, &info_spec, &error_abort);
    visit_complete(v, &obj);
    data = qdict_get(qobject_to(QDict, obj), "data");
    if (!qobject_is_empty_dump(data)) {
        if (prefix) {
            qemu_printf("%*s%s", indentation * 4, "", prefix);
        }
        dump_qobject(indentation + 1, data);
    }
    qobject_unref(obj);
    visit_free(v);
}

/**
 * Print the given @info object in human-readable form.  Every field is indented
 * using the given @indentation (four spaces per indentation level).
 *
 * When using this to print a whole block graph, @protocol can be set to true to
 * signify that the given information is associated with a protocol node, i.e.
 * just data storage for an image, such that the data it presents is not really
 * a full VM disk.  If so, several fields change name: For example, "virtual
 * size" is printed as "file length".
 * (Consider a qcow2 image, which is represented by a qcow2 node and a file
 * node.  Printing a "virtual size" for the file node does not make sense,
 * because without the qcow2 node, it is not really a guest disk, so it does not
 * have a "virtual size".  Therefore, we call it "file length" instead.)
 *
 * @protocol is ignored when @indentation is 0, because we take that to mean
 * that the associated node is the root node in the queried block graph, and
 * thus is always to be interpreted as a standalone guest disk.
 */
void bdrv_node_info_dump(BlockNodeInfo *info, int indentation, bool protocol)
{
    char *size_buf, *dsize_buf;
    g_autofree char *ind_s = g_strdup_printf("%*s", indentation * 4, "");

    if (indentation == 0) {
        /* Top level, consider this a normal image */
        protocol = false;
    }

    if (!info->has_actual_size) {
        dsize_buf = g_strdup("unavailable");
    } else {
        dsize_buf = size_to_str(info->actual_size);
    }
    size_buf = size_to_str(info->virtual_size);
    qemu_printf("%s%s: %s\n"
                "%s%s: %s\n"
                "%s%s: %s (%" PRId64 " bytes)\n"
                "%sdisk size: %s\n",
                ind_s, protocol ? "filename" : "image", info->filename,
                ind_s, protocol ? "protocol type" : "file format",
                info->format,
                ind_s, protocol ? "file length" : "virtual size",
                size_buf, info->virtual_size,
                ind_s, dsize_buf);
    g_free(size_buf);
    g_free(dsize_buf);

    if (info->has_encrypted && info->encrypted) {
        qemu_printf("%sencrypted: yes\n", ind_s);
    }

    if (info->has_cluster_size) {
        qemu_printf("%scluster_size: %" PRId64 "\n",
                    ind_s, info->cluster_size);
    }

    if (info->has_dirty_flag && info->dirty_flag) {
        qemu_printf("%scleanly shut down: no\n", ind_s);
    }

    if (info->backing_filename) {
        qemu_printf("%sbacking file: %s", ind_s, info->backing_filename);
        if (!info->full_backing_filename) {
            qemu_printf(" (cannot determine actual path)");
        } else if (strcmp(info->backing_filename,
                          info->full_backing_filename) != 0) {
            qemu_printf(" (actual path: %s)", info->full_backing_filename);
        }
        qemu_printf("\n");
        if (info->backing_filename_format) {
            qemu_printf("%sbacking file format: %s\n",
                        ind_s, info->backing_filename_format);
        }
    }

    if (info->has_snapshots) {
        SnapshotInfoList *elem;

        qemu_printf("%sSnapshot list:\n", ind_s);
        qemu_printf("%s", ind_s);
        bdrv_snapshot_dump(NULL);
        qemu_printf("\n");

        /* Ideally bdrv_snapshot_dump() would operate on SnapshotInfoList but
         * we convert to the block layer's native QEMUSnapshotInfo for now.
         */
        for (elem = info->snapshots; elem; elem = elem->next) {
            QEMUSnapshotInfo sn = {
                .vm_state_size = elem->value->vm_state_size,
                .date_sec = elem->value->date_sec,
                .date_nsec = elem->value->date_nsec,
                .vm_clock_nsec = elem->value->vm_clock_sec * 1000000000ULL +
                                 elem->value->vm_clock_nsec,
                .icount = elem->value->has_icount ?
                          elem->value->icount : -1ULL,
            };

            pstrcpy(sn.id_str, sizeof(sn.id_str), elem->value->id);
            pstrcpy(sn.name, sizeof(sn.name), elem->value->name);
            qemu_printf("%s", ind_s);
            bdrv_snapshot_dump(&sn);
            qemu_printf("\n");
        }
    }

    if (info->format_specific) {
        bdrv_image_info_specific_dump(info->format_specific,
                                      "Format specific information:\n",
                                      indentation);
    }
}
