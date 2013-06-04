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

#include "block/qapi.h"
#include "block/block_int.h"
#include "qmp-commands.h"

void bdrv_collect_snapshots(BlockDriverState *bs , ImageInfo *info)
{
    int i, sn_count;
    QEMUSnapshotInfo *sn_tab = NULL;
    SnapshotInfoList *info_list, *cur_item = NULL;
    sn_count = bdrv_snapshot_list(bs, &sn_tab);

    for (i = 0; i < sn_count; i++) {
        info->has_snapshots = true;
        info_list = g_new0(SnapshotInfoList, 1);

        info_list->value                = g_new0(SnapshotInfo, 1);
        info_list->value->id            = g_strdup(sn_tab[i].id_str);
        info_list->value->name          = g_strdup(sn_tab[i].name);
        info_list->value->vm_state_size = sn_tab[i].vm_state_size;
        info_list->value->date_sec      = sn_tab[i].date_sec;
        info_list->value->date_nsec     = sn_tab[i].date_nsec;
        info_list->value->vm_clock_sec  = sn_tab[i].vm_clock_nsec / 1000000000;
        info_list->value->vm_clock_nsec = sn_tab[i].vm_clock_nsec % 1000000000;

        /* XXX: waiting for the qapi to support qemu-queue.h types */
        if (!cur_item) {
            info->snapshots = cur_item = info_list;
        } else {
            cur_item->next = info_list;
            cur_item = info_list;
        }

    }

    g_free(sn_tab);
}

void bdrv_collect_image_info(BlockDriverState *bs,
                             ImageInfo *info,
                             const char *filename)
{
    uint64_t total_sectors;
    char backing_filename[1024];
    char backing_filename2[1024];
    BlockDriverInfo bdi;

    bdrv_get_geometry(bs, &total_sectors);

    info->filename        = g_strdup(filename);
    info->format          = g_strdup(bdrv_get_format_name(bs));
    info->virtual_size    = total_sectors * 512;
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
    bdrv_get_backing_filename(bs, backing_filename, sizeof(backing_filename));
    if (backing_filename[0] != '\0') {
        info->backing_filename = g_strdup(backing_filename);
        info->has_backing_filename = true;
        bdrv_get_full_backing_filename(bs, backing_filename2,
                                       sizeof(backing_filename2));

        if (strcmp(backing_filename, backing_filename2) != 0) {
            info->full_backing_filename =
                        g_strdup(backing_filename2);
            info->has_full_backing_filename = true;
        }

        if (bs->backing_format[0]) {
            info->backing_filename_format = g_strdup(bs->backing_format);
            info->has_backing_filename_format = true;
        }
    }
}

BlockInfo *bdrv_query_info(BlockDriverState *bs)
{
    BlockInfo *info = g_malloc0(sizeof(*info));
    info->device = g_strdup(bs->device_name);
    info->type = g_strdup("unknown");
    info->locked = bdrv_dev_is_medium_locked(bs);
    info->removable = bdrv_dev_has_removable_media(bs);

    if (bdrv_dev_has_removable_media(bs)) {
        info->has_tray_open = true;
        info->tray_open = bdrv_dev_is_tray_open(bs);
    }

    if (bdrv_iostatus_is_enabled(bs)) {
        info->has_io_status = true;
        info->io_status = bs->iostatus;
    }

    if (bs->dirty_bitmap) {
        info->has_dirty = true;
        info->dirty = g_malloc0(sizeof(*info->dirty));
        info->dirty->count = bdrv_get_dirty_count(bs) * BDRV_SECTOR_SIZE;
        info->dirty->granularity =
         ((int64_t) BDRV_SECTOR_SIZE << hbitmap_granularity(bs->dirty_bitmap));
    }

    if (bs->drv) {
        info->has_inserted = true;
        info->inserted = g_malloc0(sizeof(*info->inserted));
        info->inserted->file = g_strdup(bs->filename);
        info->inserted->ro = bs->read_only;
        info->inserted->drv = g_strdup(bs->drv->format_name);
        info->inserted->encrypted = bs->encrypted;
        info->inserted->encryption_key_missing = bdrv_key_required(bs);

        if (bs->backing_file[0]) {
            info->inserted->has_backing_file = true;
            info->inserted->backing_file = g_strdup(bs->backing_file);
        }

        info->inserted->backing_file_depth = bdrv_get_backing_file_depth(bs);

        if (bs->io_limits_enabled) {
            info->inserted->bps =
                           bs->io_limits.bps[BLOCK_IO_LIMIT_TOTAL];
            info->inserted->bps_rd =
                           bs->io_limits.bps[BLOCK_IO_LIMIT_READ];
            info->inserted->bps_wr =
                           bs->io_limits.bps[BLOCK_IO_LIMIT_WRITE];
            info->inserted->iops =
                           bs->io_limits.iops[BLOCK_IO_LIMIT_TOTAL];
            info->inserted->iops_rd =
                           bs->io_limits.iops[BLOCK_IO_LIMIT_READ];
            info->inserted->iops_wr =
                           bs->io_limits.iops[BLOCK_IO_LIMIT_WRITE];
        }
    }
    return info;
}

BlockStats *bdrv_query_stats(const BlockDriverState *bs)
{
    BlockStats *s;

    s = g_malloc0(sizeof(*s));

    if (bs->device_name[0]) {
        s->has_device = true;
        s->device = g_strdup(bs->device_name);
    }

    s->stats = g_malloc0(sizeof(*s->stats));
    s->stats->rd_bytes = bs->nr_bytes[BDRV_ACCT_READ];
    s->stats->wr_bytes = bs->nr_bytes[BDRV_ACCT_WRITE];
    s->stats->rd_operations = bs->nr_ops[BDRV_ACCT_READ];
    s->stats->wr_operations = bs->nr_ops[BDRV_ACCT_WRITE];
    s->stats->wr_highest_offset = bs->wr_highest_sector * BDRV_SECTOR_SIZE;
    s->stats->flush_operations = bs->nr_ops[BDRV_ACCT_FLUSH];
    s->stats->wr_total_time_ns = bs->total_time_ns[BDRV_ACCT_WRITE];
    s->stats->rd_total_time_ns = bs->total_time_ns[BDRV_ACCT_READ];
    s->stats->flush_total_time_ns = bs->total_time_ns[BDRV_ACCT_FLUSH];

    if (bs->file) {
        s->has_parent = true;
        s->parent = bdrv_query_stats(bs->file);
    }

    return s;
}

BlockInfoList *qmp_query_block(Error **errp)
{
    BlockInfoList *head = NULL, **p_next = &head;
    BlockDriverState *bs = NULL;

     while ((bs = bdrv_next(bs))) {
        BlockInfoList *info = g_malloc0(sizeof(*info));
        info->value = bdrv_query_info(bs);

        *p_next = info;
        p_next = &info->next;
    }

    return head;
}

BlockStatsList *qmp_query_blockstats(Error **errp)
{
    BlockStatsList *head = NULL, **p_next = &head;
    BlockDriverState *bs = NULL;

     while ((bs = bdrv_next(bs))) {
        BlockStatsList *info = g_malloc0(sizeof(*info));
        info->value = bdrv_query_stats(bs);

        *p_next = info;
        p_next = &info->next;
    }

    return head;
}

#define NB_SUFFIXES 4

static char *get_human_readable_size(char *buf, int buf_size, int64_t size)
{
    static const char suffixes[NB_SUFFIXES] = "KMGT";
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
        if (info->has_full_backing_filename) {
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
}
