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
#include "block/qapi.h"
#include "block/block_int.h"
#include "block/throttle-groups.h"
#include "block/write-threshold.h"
#include "qmp-commands.h"
#include "qapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp/types.h"
#include "sysemu/block-backend.h"
#include "qemu/cutils.h"

BlockDeviceInfo *bdrv_block_device_info(BlockBackend *blk,
                                        BlockDriverState *bs, Error **errp)
{
    ImageInfo **p_image_info;
    BlockDriverState *bs0;
    BlockDeviceInfo *info = g_malloc0(sizeof(*info));

    info->file                   = g_strdup(bs->filename);
    info->ro                     = bs->read_only;
    info->drv                    = g_strdup(bs->drv->format_name);
    info->encrypted              = bs->encrypted;
    info->encryption_key_missing = bdrv_key_required(bs);

    info->cache = g_new(BlockdevCacheInfo, 1);
    *info->cache = (BlockdevCacheInfo) {
        .writeback      = blk ? blk_enable_write_cache(blk) : true,
        .direct         = !!(bs->open_flags & BDRV_O_NOCACHE),
        .no_flush       = !!(bs->open_flags & BDRV_O_NO_FLUSH),
    };

    if (bs->node_name[0]) {
        info->has_node_name = true;
        info->node_name = g_strdup(bs->node_name);
    }

    if (bs->backing_file[0]) {
        info->has_backing_file = true;
        info->backing_file = g_strdup(bs->backing_file);
    }

    info->backing_file_depth = bdrv_get_backing_file_depth(bs);
    info->detect_zeroes = bs->detect_zeroes;

    if (blk && blk_get_public(blk)->throttle_state) {
        ThrottleConfig cfg;

        throttle_group_get_config(blk, &cfg);

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

        info->has_group = true;
        info->group = g_strdup(throttle_group_get_name(blk));
    }

    info->write_threshold = bdrv_write_threshold_get(bs);

    bs0 = bs;
    p_image_info = &info->image;
    while (1) {
        Error *local_err = NULL;
        bdrv_query_image_info(bs0, p_image_info, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_BlockDeviceInfo(info);
            return NULL;
        }
        if (bs0->drv && bs0->backing) {
            bs0 = bs0->backing->bs;
            (*p_image_info)->has_backing_image = true;
            p_image_info = &((*p_image_info)->backing_image);
        } else {
            break;
        }
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
    SnapshotInfoList *info_list, *cur_item = NULL, *head = NULL;
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

        info_list = g_new0(SnapshotInfoList, 1);
        info_list->value = info;

        /* XXX: waiting for the qapi to support qemu-queue.h types */
        if (!cur_item) {
            head = cur_item = info_list;
        } else {
            cur_item->next = info_list;
            cur_item = info_list;
        }

    }

    g_free(sn_tab);
    *p_list = head;
    return 0;
}

/**
 * bdrv_query_image_info:
 * @bs: block device to examine
 * @p_info: location to store image information
 * @errp: location to store error information
 *
 * Store "flat" image information in @p_info.
 *
 * "Flat" means it does *not* query backing image information,
 * i.e. (*pinfo)->has_backing_image will be set to false and
 * (*pinfo)->backing_image to NULL even when the image does in fact have
 * a backing image.
 *
 * @p_info will be set only on success. On error, store error in @errp.
 */
void bdrv_query_image_info(BlockDriverState *bs,
                           ImageInfo **p_info,
                           Error **errp)
{
    int64_t size;
    const char *backing_filename;
    BlockDriverInfo bdi;
    int ret;
    Error *err = NULL;
    ImageInfo *info;

    aio_context_acquire(bdrv_get_aio_context(bs));

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "Can't get size of device '%s'",
                         bdrv_get_device_name(bs));
        goto out;
    }

    info = g_new0(ImageInfo, 1);
    info->filename        = g_strdup(bs->filename);
    info->format          = g_strdup(bdrv_get_format_name(bs));
    info->virtual_size    = size;
    info->actual_size     = bdrv_get_allocated_file_size(bs);
    info->has_actual_size = info->actual_size >= 0;
    if (bdrv_is_encrypted(bs)) {
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
    info->format_specific     = bdrv_get_specific_info(bs);
    info->has_format_specific = info->format_specific != NULL;

    backing_filename = bs->backing_file;
    if (backing_filename[0] != '\0') {
        char *backing_filename2 = g_malloc0(PATH_MAX);
        info->backing_filename = g_strdup(backing_filename);
        info->has_backing_filename = true;
        bdrv_get_full_backing_filename(bs, backing_filename2, PATH_MAX, &err);
        if (err) {
            /* Can't reconstruct the full backing filename, so we must omit
             * this field and apply a Best Effort to this query. */
            g_free(backing_filename2);
            backing_filename2 = NULL;
            error_free(err);
            err = NULL;
        }

        /* Always report the full_backing_filename if present, even if it's the
         * same as backing_filename. That they are same is useful info. */
        if (backing_filename2) {
            info->full_backing_filename = g_strdup(backing_filename2);
            info->has_full_backing_filename = true;
        }

        if (bs->backing_format[0]) {
            info->backing_filename_format = g_strdup(bs->backing_format);
            info->has_backing_filename_format = true;
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
        qapi_free_ImageInfo(info);
        goto out;
    }

    *p_info = info;

out:
    aio_context_release(bdrv_get_aio_context(bs));
}

/* @p_info will be set only on success. */
static void bdrv_query_info(BlockBackend *blk, BlockInfo **p_info,
                            Error **errp)
{
    BlockInfo *info = g_malloc0(sizeof(*info));
    BlockDriverState *bs = blk_bs(blk);
    info->device = g_strdup(blk_name(blk));
    info->type = g_strdup("unknown");
    info->locked = blk_dev_is_medium_locked(blk);
    info->removable = blk_dev_has_removable_media(blk);

    if (blk_dev_has_tray(blk)) {
        info->has_tray_open = true;
        info->tray_open = blk_dev_is_tray_open(blk);
    }

    if (blk_iostatus_is_enabled(blk)) {
        info->has_io_status = true;
        info->io_status = blk_iostatus(blk);
    }

    if (bs && !QLIST_EMPTY(&bs->dirty_bitmaps)) {
        info->has_dirty_bitmaps = true;
        info->dirty_bitmaps = bdrv_query_dirty_bitmaps(bs);
    }

    if (bs && bs->drv) {
        info->has_inserted = true;
        info->inserted = bdrv_block_device_info(blk, bs, errp);
        if (info->inserted == NULL) {
            goto err;
        }
    }

    *p_info = info;
    return;

 err:
    qapi_free_BlockInfo(info);
}

static BlockStats *bdrv_query_stats(BlockBackend *blk,
                                    const BlockDriverState *bs,
                                    bool query_backing);

static void bdrv_query_blk_stats(BlockDeviceStats *ds, BlockBackend *blk)
{
    BlockAcctStats *stats = blk_get_stats(blk);
    BlockAcctTimedStats *ts = NULL;

    ds->rd_bytes = stats->nr_bytes[BLOCK_ACCT_READ];
    ds->wr_bytes = stats->nr_bytes[BLOCK_ACCT_WRITE];
    ds->rd_operations = stats->nr_ops[BLOCK_ACCT_READ];
    ds->wr_operations = stats->nr_ops[BLOCK_ACCT_WRITE];

    ds->failed_rd_operations = stats->failed_ops[BLOCK_ACCT_READ];
    ds->failed_wr_operations = stats->failed_ops[BLOCK_ACCT_WRITE];
    ds->failed_flush_operations = stats->failed_ops[BLOCK_ACCT_FLUSH];

    ds->invalid_rd_operations = stats->invalid_ops[BLOCK_ACCT_READ];
    ds->invalid_wr_operations = stats->invalid_ops[BLOCK_ACCT_WRITE];
    ds->invalid_flush_operations =
        stats->invalid_ops[BLOCK_ACCT_FLUSH];

    ds->rd_merged = stats->merged[BLOCK_ACCT_READ];
    ds->wr_merged = stats->merged[BLOCK_ACCT_WRITE];
    ds->flush_operations = stats->nr_ops[BLOCK_ACCT_FLUSH];
    ds->wr_total_time_ns = stats->total_time_ns[BLOCK_ACCT_WRITE];
    ds->rd_total_time_ns = stats->total_time_ns[BLOCK_ACCT_READ];
    ds->flush_total_time_ns = stats->total_time_ns[BLOCK_ACCT_FLUSH];

    ds->has_idle_time_ns = stats->last_access_time_ns > 0;
    if (ds->has_idle_time_ns) {
        ds->idle_time_ns = block_acct_idle_time_ns(stats);
    }

    ds->account_invalid = stats->account_invalid;
    ds->account_failed = stats->account_failed;

    while ((ts = block_acct_interval_next(stats, ts))) {
        BlockDeviceTimedStatsList *timed_stats =
            g_malloc0(sizeof(*timed_stats));
        BlockDeviceTimedStats *dev_stats = g_malloc0(sizeof(*dev_stats));
        timed_stats->next = ds->timed_stats;
        timed_stats->value = dev_stats;
        ds->timed_stats = timed_stats;

        TimedAverage *rd = &ts->latency[BLOCK_ACCT_READ];
        TimedAverage *wr = &ts->latency[BLOCK_ACCT_WRITE];
        TimedAverage *fl = &ts->latency[BLOCK_ACCT_FLUSH];

        dev_stats->interval_length = ts->interval_length;

        dev_stats->min_rd_latency_ns = timed_average_min(rd);
        dev_stats->max_rd_latency_ns = timed_average_max(rd);
        dev_stats->avg_rd_latency_ns = timed_average_avg(rd);

        dev_stats->min_wr_latency_ns = timed_average_min(wr);
        dev_stats->max_wr_latency_ns = timed_average_max(wr);
        dev_stats->avg_wr_latency_ns = timed_average_avg(wr);

        dev_stats->min_flush_latency_ns = timed_average_min(fl);
        dev_stats->max_flush_latency_ns = timed_average_max(fl);
        dev_stats->avg_flush_latency_ns = timed_average_avg(fl);

        dev_stats->avg_rd_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_READ);
        dev_stats->avg_wr_queue_depth =
            block_acct_queue_depth(ts, BLOCK_ACCT_WRITE);
    }
}

static void bdrv_query_bds_stats(BlockStats *s, const BlockDriverState *bs,
                                 bool query_backing)
{
    if (bdrv_get_node_name(bs)[0]) {
        s->has_node_name = true;
        s->node_name = g_strdup(bdrv_get_node_name(bs));
    }

    s->stats->wr_highest_offset = bs->wr_highest_offset;

    if (bs->file) {
        s->has_parent = true;
        s->parent = bdrv_query_stats(NULL, bs->file->bs, query_backing);
    }

    if (query_backing && bs->backing) {
        s->has_backing = true;
        s->backing = bdrv_query_stats(NULL, bs->backing->bs, query_backing);
    }

}

static BlockStats *bdrv_query_stats(BlockBackend *blk,
                                    const BlockDriverState *bs,
                                    bool query_backing)
{
    BlockStats *s;

    s = g_malloc0(sizeof(*s));
    s->stats = g_malloc0(sizeof(*s->stats));

    if (blk) {
        s->has_device = true;
        s->device = g_strdup(blk_name(blk));
        bdrv_query_blk_stats(s->stats, blk);
    }
    if (bs) {
        bdrv_query_bds_stats(s, bs, query_backing);
    }

    return s;
}

BlockInfoList *qmp_query_block(Error **errp)
{
    BlockInfoList *head = NULL, **p_next = &head;
    BlockBackend *blk;
    Error *local_err = NULL;

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        BlockInfoList *info = g_malloc0(sizeof(*info));
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

static bool next_query_bds(BlockBackend **blk, BlockDriverState **bs,
                           bool query_nodes)
{
    if (query_nodes) {
        *bs = bdrv_next_node(*bs);
        return !!*bs;
    }

    *blk = blk_next(*blk);
    *bs = *blk ? blk_bs(*blk) : NULL;

    return !!*blk;
}

BlockStatsList *qmp_query_blockstats(bool has_query_nodes,
                                     bool query_nodes,
                                     Error **errp)
{
    BlockStatsList *head = NULL, **p_next = &head;
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;

    /* Just to be safe if query_nodes is not always initialized */
    query_nodes = has_query_nodes && query_nodes;

    while (next_query_bds(&blk, &bs, query_nodes)) {
        BlockStatsList *info = g_malloc0(sizeof(*info));
        AioContext *ctx = blk ? blk_get_aio_context(blk)
                              : bdrv_get_aio_context(bs);

        aio_context_acquire(ctx);
        info->value = bdrv_query_stats(blk, bs, !query_nodes);
        aio_context_release(ctx);

        *p_next = info;
        p_next = &info->next;
    }

    return head;
}

#define NB_SUFFIXES 4

static char *get_human_readable_size(char *buf, int buf_size, int64_t size)
{
    static const char suffixes[NB_SUFFIXES] = {'K', 'M', 'G', 'T'};
    int64_t base;
    int i;

    if (size <= 999) {
        snprintf(buf, buf_size, "%" PRId64, size);
    } else {
        base = 1024;
        for (i = 0; i < NB_SUFFIXES; i++) {
            if (size < (10 * base)) {
                snprintf(buf, buf_size, "%0.1f%c",
                         (double)size / base,
                         suffixes[i]);
                break;
            } else if (size < (1000 * base) || i == (NB_SUFFIXES - 1)) {
                snprintf(buf, buf_size, "%" PRId64 "%c",
                         ((size + (base >> 1)) / base),
                         suffixes[i]);
                break;
            }
            base = base * 1024;
        }
    }
    return buf;
}

void bdrv_snapshot_dump(fprintf_function func_fprintf, void *f,
                        QEMUSnapshotInfo *sn)
{
    char buf1[128], date_buf[128], clock_buf[128];
    struct tm tm;
    time_t ti;
    int64_t secs;

    if (!sn) {
        func_fprintf(f,
                     "%-10s%-20s%7s%20s%15s",
                     "ID", "TAG", "VM SIZE", "DATE", "VM CLOCK");
    } else {
        ti = sn->date_sec;
        localtime_r(&ti, &tm);
        strftime(date_buf, sizeof(date_buf),
                 "%Y-%m-%d %H:%M:%S", &tm);
        secs = sn->vm_clock_nsec / 1000000000;
        snprintf(clock_buf, sizeof(clock_buf),
                 "%02d:%02d:%02d.%03d",
                 (int)(secs / 3600),
                 (int)((secs / 60) % 60),
                 (int)(secs % 60),
                 (int)((sn->vm_clock_nsec / 1000000) % 1000));
        func_fprintf(f,
                     "%-10s%-20s%7s%20s%15s",
                     sn->id_str, sn->name,
                     get_human_readable_size(buf1, sizeof(buf1),
                                             sn->vm_state_size),
                     date_buf,
                     clock_buf);
    }
}

static void dump_qdict(fprintf_function func_fprintf, void *f, int indentation,
                       QDict *dict);
static void dump_qlist(fprintf_function func_fprintf, void *f, int indentation,
                       QList *list);

static void dump_qobject(fprintf_function func_fprintf, void *f,
                         int comp_indent, QObject *obj)
{
    switch (qobject_type(obj)) {
        case QTYPE_QINT: {
            QInt *value = qobject_to_qint(obj);
            func_fprintf(f, "%" PRId64, qint_get_int(value));
            break;
        }
        case QTYPE_QSTRING: {
            QString *value = qobject_to_qstring(obj);
            func_fprintf(f, "%s", qstring_get_str(value));
            break;
        }
        case QTYPE_QDICT: {
            QDict *value = qobject_to_qdict(obj);
            dump_qdict(func_fprintf, f, comp_indent, value);
            break;
        }
        case QTYPE_QLIST: {
            QList *value = qobject_to_qlist(obj);
            dump_qlist(func_fprintf, f, comp_indent, value);
            break;
        }
        case QTYPE_QFLOAT: {
            QFloat *value = qobject_to_qfloat(obj);
            func_fprintf(f, "%g", qfloat_get_double(value));
            break;
        }
        case QTYPE_QBOOL: {
            QBool *value = qobject_to_qbool(obj);
            func_fprintf(f, "%s", qbool_get_bool(value) ? "true" : "false");
            break;
        }
        default:
            abort();
    }
}

static void dump_qlist(fprintf_function func_fprintf, void *f, int indentation,
                       QList *list)
{
    const QListEntry *entry;
    int i = 0;

    for (entry = qlist_first(list); entry; entry = qlist_next(entry), i++) {
        QType type = qobject_type(entry->value);
        bool composite = (type == QTYPE_QDICT || type == QTYPE_QLIST);
        func_fprintf(f, "%*s[%i]:%c", indentation * 4, "", i,
                     composite ? '\n' : ' ');
        dump_qobject(func_fprintf, f, indentation + 1, entry->value);
        if (!composite) {
            func_fprintf(f, "\n");
        }
    }
}

static void dump_qdict(fprintf_function func_fprintf, void *f, int indentation,
                       QDict *dict)
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
        func_fprintf(f, "%*s%s:%c", indentation * 4, "", key,
                     composite ? '\n' : ' ');
        dump_qobject(func_fprintf, f, indentation + 1, entry->value);
        if (!composite) {
            func_fprintf(f, "\n");
        }
        g_free(key);
    }
}

void bdrv_image_info_specific_dump(fprintf_function func_fprintf, void *f,
                                   ImageInfoSpecific *info_spec)
{
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    QObject *obj, *data;

    visit_type_ImageInfoSpecific(qmp_output_get_visitor(ov), NULL, &info_spec,
                                 &error_abort);
    obj = qmp_output_get_qobject(ov);
    assert(qobject_type(obj) == QTYPE_QDICT);
    data = qdict_get(qobject_to_qdict(obj), "data");
    dump_qobject(func_fprintf, f, 1, data);
    qmp_output_visitor_cleanup(ov);
}

void bdrv_image_info_dump(fprintf_function func_fprintf, void *f,
                          ImageInfo *info)
{
    char size_buf[128], dsize_buf[128];
    if (!info->has_actual_size) {
        snprintf(dsize_buf, sizeof(dsize_buf), "unavailable");
    } else {
        get_human_readable_size(dsize_buf, sizeof(dsize_buf),
                                info->actual_size);
    }
    get_human_readable_size(size_buf, sizeof(size_buf), info->virtual_size);
    func_fprintf(f,
                 "image: %s\n"
                 "file format: %s\n"
                 "virtual size: %s (%" PRId64 " bytes)\n"
                 "disk size: %s\n",
                 info->filename, info->format, size_buf,
                 info->virtual_size,
                 dsize_buf);

    if (info->has_encrypted && info->encrypted) {
        func_fprintf(f, "encrypted: yes\n");
    }

    if (info->has_cluster_size) {
        func_fprintf(f, "cluster_size: %" PRId64 "\n",
                       info->cluster_size);
    }

    if (info->has_dirty_flag && info->dirty_flag) {
        func_fprintf(f, "cleanly shut down: no\n");
    }

    if (info->has_backing_filename) {
        func_fprintf(f, "backing file: %s", info->backing_filename);
        if (!info->has_full_backing_filename) {
            func_fprintf(f, " (cannot determine actual path)");
        } else if (strcmp(info->backing_filename,
                          info->full_backing_filename) != 0) {
            func_fprintf(f, " (actual path: %s)", info->full_backing_filename);
        }
        func_fprintf(f, "\n");
        if (info->has_backing_filename_format) {
            func_fprintf(f, "backing file format: %s\n",
                         info->backing_filename_format);
        }
    }

    if (info->has_snapshots) {
        SnapshotInfoList *elem;

        func_fprintf(f, "Snapshot list:\n");
        bdrv_snapshot_dump(func_fprintf, f, NULL);
        func_fprintf(f, "\n");

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
            };

            pstrcpy(sn.id_str, sizeof(sn.id_str), elem->value->id);
            pstrcpy(sn.name, sizeof(sn.name), elem->value->name);
            bdrv_snapshot_dump(func_fprintf, f, &sn);
            func_fprintf(f, "\n");
        }
    }

    if (info->has_format_specific) {
        func_fprintf(f, "Format specific information:\n");
        bdrv_image_info_specific_dump(func_fprintf, f, info->format_specific);
    }
}
