/*
 * QEMU host block devices
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
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
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "block/blockjob.h"
#include "block/dirty-bitmap.h"
#include "block/qdict.h"
#include "block/throttle-groups.h"
#include "monitor/monitor.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/config-file.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qapi-commands-transaction.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-output-visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/iothread.h"
#include "block/block_int.h"
#include "block/trace.h"
#include "sysemu/runstate.h"
#include "sysemu/replay.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/main-loop.h"
#include "qemu/throttle-options.h"

/* Protected by BQL */
QTAILQ_HEAD(, BlockDriverState) monitor_bdrv_states =
    QTAILQ_HEAD_INITIALIZER(monitor_bdrv_states);

void bdrv_set_monitor_owned(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();
    QTAILQ_INSERT_TAIL(&monitor_bdrv_states, bs, monitor_list);
}

static const char *const if_name[IF_COUNT] = {
    [IF_NONE] = "none",
    [IF_IDE] = "ide",
    [IF_SCSI] = "scsi",
    [IF_FLOPPY] = "floppy",
    [IF_PFLASH] = "pflash",
    [IF_MTD] = "mtd",
    [IF_SD] = "sd",
    [IF_VIRTIO] = "virtio",
    [IF_XEN] = "xen",
};

static int if_max_devs[IF_COUNT] = {
    /*
     * Do not change these numbers!  They govern how drive option
     * index maps to unit and bus.  That mapping is ABI.
     *
     * All controllers used to implement if=T drives need to support
     * if_max_devs[T] units, for any T with if_max_devs[T] != 0.
     * Otherwise, some index values map to "impossible" bus, unit
     * values.
     *
     * For instance, if you change [IF_SCSI] to 255, -drive
     * if=scsi,index=12 no longer means bus=1,unit=5, but
     * bus=0,unit=12.  With an lsi53c895a controller (7 units max),
     * the drive can't be set up.  Regression.
     */
    [IF_IDE] = 2,
    [IF_SCSI] = 7,
};

/**
 * Boards may call this to offer board-by-board overrides
 * of the default, global values.
 */
void override_max_devs(BlockInterfaceType type, int max_devs)
{
    BlockBackend *blk;
    DriveInfo *dinfo;

    GLOBAL_STATE_CODE();

    if (max_devs <= 0) {
        return;
    }

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo->type == type) {
            fprintf(stderr, "Cannot override units-per-bus property of"
                    " the %s interface, because a drive of that type has"
                    " already been added.\n", if_name[type]);
            g_assert_not_reached();
        }
    }

    if_max_devs[type] = max_devs;
}

/*
 * We automatically delete the drive when a device using it gets
 * unplugged.  Questionable feature, but we can't just drop it.
 * Device models call blockdev_mark_auto_del() to schedule the
 * automatic deletion, and generic qdev code calls blockdev_auto_del()
 * when deletion is actually safe.
 */
void blockdev_mark_auto_del(BlockBackend *blk)
{
    DriveInfo *dinfo = blk_legacy_dinfo(blk);
    BlockJob *job;

    GLOBAL_STATE_CODE();

    if (!dinfo) {
        return;
    }

    JOB_LOCK_GUARD();

    do {
        job = block_job_next_locked(NULL);
        while (job && (job->job.cancelled ||
                       job->job.deferred_to_main_loop ||
                       !block_job_has_bdrv(job, blk_bs(blk))))
        {
            job = block_job_next_locked(job);
        }
        if (job) {
            /*
             * This drops the job lock temporarily and polls, so we need to
             * restart processing the list from the start after this.
             */
            job_cancel_locked(&job->job, false);
        }
    } while (job);

    dinfo->auto_del = 1;
}

void blockdev_auto_del(BlockBackend *blk)
{
    DriveInfo *dinfo = blk_legacy_dinfo(blk);
    GLOBAL_STATE_CODE();

    if (dinfo && dinfo->auto_del) {
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

static int drive_index_to_bus_id(BlockInterfaceType type, int index)
{
    int max_devs = if_max_devs[type];
    return max_devs ? index / max_devs : 0;
}

static int drive_index_to_unit_id(BlockInterfaceType type, int index)
{
    int max_devs = if_max_devs[type];
    return max_devs ? index % max_devs : index;
}

QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr)
{
    QemuOpts *opts;

    GLOBAL_STATE_CODE();

    opts = qemu_opts_parse_noisily(qemu_find_opts("drive"), optstr, false);
    if (!opts) {
        return NULL;
    }
    if (type != IF_DEFAULT) {
        qemu_opt_set(opts, "if", if_name[type], &error_abort);
    }
    if (index >= 0) {
        qemu_opt_set_number(opts, "index", index, &error_abort);
    }
    if (file)
        qemu_opt_set(opts, "file", file, &error_abort);
    return opts;
}

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit)
{
    BlockBackend *blk;
    DriveInfo *dinfo;

    GLOBAL_STATE_CODE();

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo && dinfo->type == type
            && dinfo->bus == bus && dinfo->unit == unit) {
            return dinfo;
        }
    }

    return NULL;
}

/*
 * Check board claimed all -drive that are meant to be claimed.
 * Fatal error if any remain unclaimed.
 */
void drive_check_orphaned(void)
{
    BlockBackend *blk;
    DriveInfo *dinfo;
    Location loc;
    bool orphans = false;

    GLOBAL_STATE_CODE();

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        /*
         * Ignore default drives, because we create certain default
         * drives unconditionally, then leave them unclaimed.  Not the
         * users fault.
         * Ignore IF_VIRTIO or IF_XEN, because it gets desugared into
         * -device, so we can leave failing to -device.
         * Ignore IF_NONE, because leaving unclaimed IF_NONE remains
         * available for device_add is a feature.
         */
        if (dinfo->is_default || dinfo->type == IF_VIRTIO
            || dinfo->type == IF_XEN || dinfo->type == IF_NONE) {
            continue;
        }
        if (!blk_get_attached_dev(blk)) {
            loc_push_none(&loc);
            qemu_opts_loc_restore(dinfo->opts);
            error_report("machine type does not support"
                         " if=%s,bus=%d,unit=%d",
                         if_name[dinfo->type], dinfo->bus, dinfo->unit);
            loc_pop(&loc);
            orphans = true;
        }
    }

    if (orphans) {
        exit(1);
    }
}

DriveInfo *drive_get_by_index(BlockInterfaceType type, int index)
{
    GLOBAL_STATE_CODE();
    return drive_get(type,
                     drive_index_to_bus_id(type, index),
                     drive_index_to_unit_id(type, index));
}

int drive_get_max_bus(BlockInterfaceType type)
{
    int max_bus;
    BlockBackend *blk;
    DriveInfo *dinfo;

    GLOBAL_STATE_CODE();

    max_bus = -1;
    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo && dinfo->type == type && dinfo->bus > max_bus) {
            max_bus = dinfo->bus;
        }
    }
    return max_bus;
}

static void bdrv_format_print(void *opaque, const char *name)
{
    qemu_printf(" %s", name);
}

typedef struct {
    QEMUBH *bh;
    BlockDriverState *bs;
} BDRVPutRefBH;

static int parse_block_error_action(const char *buf, bool is_read, Error **errp)
{
    if (!strcmp(buf, "ignore")) {
        return BLOCKDEV_ON_ERROR_IGNORE;
    } else if (!is_read && !strcmp(buf, "enospc")) {
        return BLOCKDEV_ON_ERROR_ENOSPC;
    } else if (!strcmp(buf, "stop")) {
        return BLOCKDEV_ON_ERROR_STOP;
    } else if (!strcmp(buf, "report")) {
        return BLOCKDEV_ON_ERROR_REPORT;
    } else {
        error_setg(errp, "'%s' invalid %s error action",
                   buf, is_read ? "read" : "write");
        return -1;
    }
}

static bool parse_stats_intervals(BlockAcctStats *stats, QList *intervals,
                                  Error **errp)
{
    const QListEntry *entry;
    for (entry = qlist_first(intervals); entry; entry = qlist_next(entry)) {
        switch (qobject_type(entry->value)) {

        case QTYPE_QSTRING: {
            uint64_t length;
            const char *str = qstring_get_str(qobject_to(QString,
                                                         entry->value));
            if (parse_uint_full(str, 10, &length) == 0 &&
                length > 0 && length <= UINT_MAX) {
                block_acct_add_interval(stats, (unsigned) length);
            } else {
                error_setg(errp, "Invalid interval length: %s", str);
                return false;
            }
            break;
        }

        case QTYPE_QNUM: {
            int64_t length = qnum_get_int(qobject_to(QNum, entry->value));

            if (length > 0 && length <= UINT_MAX) {
                block_acct_add_interval(stats, (unsigned) length);
            } else {
                error_setg(errp, "Invalid interval length: %" PRId64, length);
                return false;
            }
            break;
        }

        default:
            error_setg(errp, "The specification of stats-intervals is invalid");
            return false;
        }
    }
    return true;
}

typedef enum { MEDIA_DISK, MEDIA_CDROM } DriveMediaType;

/* All parameters but @opts are optional and may be set to NULL. */
static void extract_common_blockdev_options(QemuOpts *opts, int *bdrv_flags,
    const char **throttling_group, ThrottleConfig *throttle_cfg,
    BlockdevDetectZeroesOptions *detect_zeroes, Error **errp)
{
    Error *local_error = NULL;
    const char *aio;

    if (bdrv_flags) {
        if (qemu_opt_get_bool(opts, "copy-on-read", false)) {
            *bdrv_flags |= BDRV_O_COPY_ON_READ;
        }

        if ((aio = qemu_opt_get(opts, "aio")) != NULL) {
            if (bdrv_parse_aio(aio, bdrv_flags) < 0) {
                error_setg(errp, "invalid aio option");
                return;
            }
        }
    }

    /* disk I/O throttling */
    if (throttling_group) {
        *throttling_group = qemu_opt_get(opts, "throttling.group");
    }

    if (throttle_cfg) {
        throttle_config_init(throttle_cfg);
        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].avg =
            qemu_opt_get_number(opts, "throttling.bps-total", 0);
        throttle_cfg->buckets[THROTTLE_BPS_READ].avg  =
            qemu_opt_get_number(opts, "throttling.bps-read", 0);
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].avg =
            qemu_opt_get_number(opts, "throttling.bps-write", 0);
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].avg =
            qemu_opt_get_number(opts, "throttling.iops-total", 0);
        throttle_cfg->buckets[THROTTLE_OPS_READ].avg =
            qemu_opt_get_number(opts, "throttling.iops-read", 0);
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].avg =
            qemu_opt_get_number(opts, "throttling.iops-write", 0);

        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].max =
            qemu_opt_get_number(opts, "throttling.bps-total-max", 0);
        throttle_cfg->buckets[THROTTLE_BPS_READ].max  =
            qemu_opt_get_number(opts, "throttling.bps-read-max", 0);
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].max =
            qemu_opt_get_number(opts, "throttling.bps-write-max", 0);
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].max =
            qemu_opt_get_number(opts, "throttling.iops-total-max", 0);
        throttle_cfg->buckets[THROTTLE_OPS_READ].max =
            qemu_opt_get_number(opts, "throttling.iops-read-max", 0);
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].max =
            qemu_opt_get_number(opts, "throttling.iops-write-max", 0);

        throttle_cfg->buckets[THROTTLE_BPS_TOTAL].burst_length =
            qemu_opt_get_number(opts, "throttling.bps-total-max-length", 1);
        throttle_cfg->buckets[THROTTLE_BPS_READ].burst_length  =
            qemu_opt_get_number(opts, "throttling.bps-read-max-length", 1);
        throttle_cfg->buckets[THROTTLE_BPS_WRITE].burst_length =
            qemu_opt_get_number(opts, "throttling.bps-write-max-length", 1);
        throttle_cfg->buckets[THROTTLE_OPS_TOTAL].burst_length =
            qemu_opt_get_number(opts, "throttling.iops-total-max-length", 1);
        throttle_cfg->buckets[THROTTLE_OPS_READ].burst_length =
            qemu_opt_get_number(opts, "throttling.iops-read-max-length", 1);
        throttle_cfg->buckets[THROTTLE_OPS_WRITE].burst_length =
            qemu_opt_get_number(opts, "throttling.iops-write-max-length", 1);

        throttle_cfg->op_size =
            qemu_opt_get_number(opts, "throttling.iops-size", 0);

        if (!throttle_is_valid(throttle_cfg, errp)) {
            return;
        }
    }

    if (detect_zeroes) {
        *detect_zeroes =
            qapi_enum_parse(&BlockdevDetectZeroesOptions_lookup,
                            qemu_opt_get(opts, "detect-zeroes"),
                            BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF,
                            &local_error);
        if (local_error) {
            error_propagate(errp, local_error);
            return;
        }
    }
}

static OnOffAuto account_get_opt(QemuOpts *opts, const char *name)
{
    if (!qemu_opt_find(opts, name)) {
        return ON_OFF_AUTO_AUTO;
    }
    if (qemu_opt_get_bool(opts, name, true)) {
        return ON_OFF_AUTO_ON;
    }
    return ON_OFF_AUTO_OFF;
}

/* Takes the ownership of bs_opts */
static BlockBackend *blockdev_init(const char *file, QDict *bs_opts,
                                   Error **errp)
{
    const char *buf;
    int bdrv_flags = 0;
    int on_read_error, on_write_error;
    OnOffAuto account_invalid, account_failed;
    bool writethrough, read_only;
    BlockBackend *blk;
    BlockDriverState *bs;
    ThrottleConfig cfg;
    int snapshot = 0;
    Error *error = NULL;
    QemuOpts *opts;
    QDict *interval_dict = NULL;
    QList *interval_list = NULL;
    const char *id;
    BlockdevDetectZeroesOptions detect_zeroes =
        BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF;
    const char *throttling_group = NULL;

    /* Check common options by copying from bs_opts to opts, all other options
     * stay in bs_opts for processing by bdrv_open(). */
    id = qdict_get_try_str(bs_opts, "id");
    opts = qemu_opts_create(&qemu_common_drive_opts, id, 1, errp);
    if (!opts) {
        goto err_no_opts;
    }

    if (!qemu_opts_absorb_qdict(opts, bs_opts, errp)) {
        goto early_err;
    }

    if (id) {
        qdict_del(bs_opts, "id");
    }

    /* extract parameters */
    snapshot = qemu_opt_get_bool(opts, "snapshot", 0);

    account_invalid = account_get_opt(opts, "stats-account-invalid");
    account_failed = account_get_opt(opts, "stats-account-failed");

    writethrough = !qemu_opt_get_bool(opts, BDRV_OPT_CACHE_WB, true);

    id = qemu_opts_id(opts);

    qdict_extract_subqdict(bs_opts, &interval_dict, "stats-intervals.");
    qdict_array_split(interval_dict, &interval_list);

    if (qdict_size(interval_dict) != 0) {
        error_setg(errp, "Invalid option stats-intervals.%s",
                   qdict_first(interval_dict)->key);
        goto early_err;
    }

    extract_common_blockdev_options(opts, &bdrv_flags, &throttling_group, &cfg,
                                    &detect_zeroes, &error);
    if (error) {
        error_propagate(errp, error);
        goto early_err;
    }

    if ((buf = qemu_opt_get(opts, "format")) != NULL) {
        if (is_help_option(buf)) {
            qemu_printf("Supported formats:");
            bdrv_iterate_format(bdrv_format_print, NULL, false);
            qemu_printf("\nSupported formats (read-only):");
            bdrv_iterate_format(bdrv_format_print, NULL, true);
            qemu_printf("\n");
            goto early_err;
        }

        if (qdict_haskey(bs_opts, "driver")) {
            error_setg(errp, "Cannot specify both 'driver' and 'format'");
            goto early_err;
        }
        qdict_put_str(bs_opts, "driver", buf);
    }

    on_write_error = BLOCKDEV_ON_ERROR_ENOSPC;
    if ((buf = qemu_opt_get(opts, "werror")) != NULL) {
        on_write_error = parse_block_error_action(buf, 0, &error);
        if (error) {
            error_propagate(errp, error);
            goto early_err;
        }
    }

    on_read_error = BLOCKDEV_ON_ERROR_REPORT;
    if ((buf = qemu_opt_get(opts, "rerror")) != NULL) {
        on_read_error = parse_block_error_action(buf, 1, &error);
        if (error) {
            error_propagate(errp, error);
            goto early_err;
        }
    }

    if (snapshot) {
        bdrv_flags |= BDRV_O_SNAPSHOT;
    }

    read_only = qemu_opt_get_bool(opts, BDRV_OPT_READ_ONLY, false);

    /* init */
    if ((!file || !*file) && !qdict_size(bs_opts)) {
        BlockBackendRootState *blk_rs;

        blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        blk_rs = blk_get_root_state(blk);
        blk_rs->open_flags    = bdrv_flags | (read_only ? 0 : BDRV_O_RDWR);
        blk_rs->detect_zeroes = detect_zeroes;

        qobject_unref(bs_opts);
    } else {
        if (file && !*file) {
            file = NULL;
        }

        /* bdrv_open() defaults to the values in bdrv_flags (for compatibility
         * with other callers) rather than what we want as the real defaults.
         * Apply the defaults here instead. */
        qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
        qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
        qdict_set_default_str(bs_opts, BDRV_OPT_READ_ONLY,
                              read_only ? "on" : "off");
        qdict_set_default_str(bs_opts, BDRV_OPT_AUTO_READ_ONLY, "on");
        assert((bdrv_flags & BDRV_O_CACHE_MASK) == 0);

        if (runstate_check(RUN_STATE_INMIGRATE)) {
            bdrv_flags |= BDRV_O_INACTIVE;
        }

        blk = blk_new_open(file, NULL, bs_opts, bdrv_flags, errp);
        if (!blk) {
            goto err_no_bs_opts;
        }
        bs = blk_bs(blk);

        bs->detect_zeroes = detect_zeroes;

        block_acct_setup(blk_get_stats(blk), account_invalid, account_failed);

        if (!parse_stats_intervals(blk_get_stats(blk), interval_list, errp)) {
            blk_unref(blk);
            blk = NULL;
            goto err_no_bs_opts;
        }
    }

    /* disk I/O throttling */
    if (throttle_enabled(&cfg)) {
        if (!throttling_group) {
            throttling_group = id;
        }
        blk_io_limits_enable(blk, throttling_group);
        blk_set_io_limits(blk, &cfg);
    }

    blk_set_enable_write_cache(blk, !writethrough);
    blk_set_on_error(blk, on_read_error, on_write_error);

    if (!monitor_add_blk(blk, id, errp)) {
        blk_unref(blk);
        blk = NULL;
        goto err_no_bs_opts;
    }

err_no_bs_opts:
    qemu_opts_del(opts);
    qobject_unref(interval_dict);
    qobject_unref(interval_list);
    return blk;

early_err:
    qemu_opts_del(opts);
    qobject_unref(interval_dict);
    qobject_unref(interval_list);
err_no_opts:
    qobject_unref(bs_opts);
    return NULL;
}

/* Takes the ownership of bs_opts */
BlockDriverState *bds_tree_init(QDict *bs_opts, Error **errp)
{
    int bdrv_flags = 0;

    GLOBAL_STATE_CODE();
    /* bdrv_open() defaults to the values in bdrv_flags (for compatibility
     * with other callers) rather than what we want as the real defaults.
     * Apply the defaults here instead. */
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_READ_ONLY, "off");

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        bdrv_flags |= BDRV_O_INACTIVE;
    }

    return bdrv_open(NULL, NULL, bs_opts, bdrv_flags, errp);
}

void blockdev_close_all_bdrv_states(void)
{
    BlockDriverState *bs, *next_bs;

    GLOBAL_STATE_CODE();
    QTAILQ_FOREACH_SAFE(bs, &monitor_bdrv_states, monitor_list, next_bs) {
        bdrv_unref(bs);
    }
}

/* Iterates over the list of monitor-owned BlockDriverStates */
BlockDriverState *bdrv_next_monitor_owned(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();
    return bs ? QTAILQ_NEXT(bs, monitor_list)
              : QTAILQ_FIRST(&monitor_bdrv_states);
}

static bool qemu_opt_rename(QemuOpts *opts, const char *from, const char *to,
                            Error **errp)
{
    const char *value;

    value = qemu_opt_get(opts, from);
    if (value) {
        if (qemu_opt_find(opts, to)) {
            error_setg(errp, "'%s' and its alias '%s' can't be used at the "
                       "same time", to, from);
            return false;
        }
    }

    /* rename all items in opts */
    while ((value = qemu_opt_get(opts, from))) {
        qemu_opt_set(opts, to, value, &error_abort);
        qemu_opt_unset(opts, from);
    }
    return true;
}

QemuOptsList qemu_legacy_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_legacy_drive_opts.head),
    .desc = {
        {
            .name = "bus",
            .type = QEMU_OPT_NUMBER,
            .help = "bus number",
        },{
            .name = "unit",
            .type = QEMU_OPT_NUMBER,
            .help = "unit number (i.e. lun for scsi)",
        },{
            .name = "index",
            .type = QEMU_OPT_NUMBER,
            .help = "index number",
        },{
            .name = "media",
            .type = QEMU_OPT_STRING,
            .help = "media type (disk, cdrom)",
        },{
            .name = "if",
            .type = QEMU_OPT_STRING,
            .help = "interface (ide, scsi, sd, mtd, floppy, pflash, virtio)",
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "file name",
        },

        /* Options that are passed on, but have special semantics with -drive */
        {
            .name = BDRV_OPT_READ_ONLY,
            .type = QEMU_OPT_BOOL,
            .help = "open drive file as read-only",
        },{
            .name = "rerror",
            .type = QEMU_OPT_STRING,
            .help = "read error action",
        },{
            .name = "werror",
            .type = QEMU_OPT_STRING,
            .help = "write error action",
        },{
            .name = "copy-on-read",
            .type = QEMU_OPT_BOOL,
            .help = "copy read data from backing file into image file",
        },

        { /* end of list */ }
    },
};

DriveInfo *drive_new(QemuOpts *all_opts, BlockInterfaceType block_default_type,
                     Error **errp)
{
    const char *value;
    BlockBackend *blk;
    DriveInfo *dinfo = NULL;
    QDict *bs_opts;
    QemuOpts *legacy_opts;
    DriveMediaType media = MEDIA_DISK;
    BlockInterfaceType type;
    int max_devs, bus_id, unit_id, index;
    const char *werror, *rerror;
    bool read_only = false;
    bool copy_on_read;
    const char *filename;
    int i;

    GLOBAL_STATE_CODE();

    /* Change legacy command line options into QMP ones */
    static const struct {
        const char *from;
        const char *to;
    } opt_renames[] = {
        { "iops",           "throttling.iops-total" },
        { "iops_rd",        "throttling.iops-read" },
        { "iops_wr",        "throttling.iops-write" },

        { "bps",            "throttling.bps-total" },
        { "bps_rd",         "throttling.bps-read" },
        { "bps_wr",         "throttling.bps-write" },

        { "iops_max",       "throttling.iops-total-max" },
        { "iops_rd_max",    "throttling.iops-read-max" },
        { "iops_wr_max",    "throttling.iops-write-max" },

        { "bps_max",        "throttling.bps-total-max" },
        { "bps_rd_max",     "throttling.bps-read-max" },
        { "bps_wr_max",     "throttling.bps-write-max" },

        { "iops_size",      "throttling.iops-size" },

        { "group",          "throttling.group" },

        { "readonly",       BDRV_OPT_READ_ONLY },
    };

    for (i = 0; i < ARRAY_SIZE(opt_renames); i++) {
        if (!qemu_opt_rename(all_opts, opt_renames[i].from,
                             opt_renames[i].to, errp)) {
            return NULL;
        }
    }

    value = qemu_opt_get(all_opts, "cache");
    if (value) {
        int flags = 0;
        bool writethrough;

        if (bdrv_parse_cache_mode(value, &flags, &writethrough) != 0) {
            error_setg(errp, "invalid cache option");
            return NULL;
        }

        /* Specific options take precedence */
        if (!qemu_opt_get(all_opts, BDRV_OPT_CACHE_WB)) {
            qemu_opt_set_bool(all_opts, BDRV_OPT_CACHE_WB,
                              !writethrough, &error_abort);
        }
        if (!qemu_opt_get(all_opts, BDRV_OPT_CACHE_DIRECT)) {
            qemu_opt_set_bool(all_opts, BDRV_OPT_CACHE_DIRECT,
                              !!(flags & BDRV_O_NOCACHE), &error_abort);
        }
        if (!qemu_opt_get(all_opts, BDRV_OPT_CACHE_NO_FLUSH)) {
            qemu_opt_set_bool(all_opts, BDRV_OPT_CACHE_NO_FLUSH,
                              !!(flags & BDRV_O_NO_FLUSH), &error_abort);
        }
        qemu_opt_unset(all_opts, "cache");
    }

    /* Get a QDict for processing the options */
    bs_opts = qdict_new();
    qemu_opts_to_qdict(all_opts, bs_opts);

    legacy_opts = qemu_opts_create(&qemu_legacy_drive_opts, NULL, 0,
                                   &error_abort);
    if (!qemu_opts_absorb_qdict(legacy_opts, bs_opts, errp)) {
        goto fail;
    }

    /* Media type */
    value = qemu_opt_get(legacy_opts, "media");
    if (value) {
        if (!strcmp(value, "disk")) {
            media = MEDIA_DISK;
        } else if (!strcmp(value, "cdrom")) {
            media = MEDIA_CDROM;
            read_only = true;
        } else {
            error_setg(errp, "'%s' invalid media", value);
            goto fail;
        }
    }

    /* copy-on-read is disabled with a warning for read-only devices */
    read_only |= qemu_opt_get_bool(legacy_opts, BDRV_OPT_READ_ONLY, false);
    copy_on_read = qemu_opt_get_bool(legacy_opts, "copy-on-read", false);

    if (read_only && copy_on_read) {
        warn_report("disabling copy-on-read on read-only drive");
        copy_on_read = false;
    }

    qdict_put_str(bs_opts, BDRV_OPT_READ_ONLY, read_only ? "on" : "off");
    qdict_put_str(bs_opts, "copy-on-read", copy_on_read ? "on" : "off");

    /* Controller type */
    value = qemu_opt_get(legacy_opts, "if");
    if (value) {
        for (type = 0;
             type < IF_COUNT && strcmp(value, if_name[type]);
             type++) {
        }
        if (type == IF_COUNT) {
            error_setg(errp, "unsupported bus type '%s'", value);
            goto fail;
        }
    } else {
        type = block_default_type;
    }

    /* Device address specified by bus/unit or index.
     * If none was specified, try to find the first free one. */
    bus_id  = qemu_opt_get_number(legacy_opts, "bus", 0);
    unit_id = qemu_opt_get_number(legacy_opts, "unit", -1);
    index   = qemu_opt_get_number(legacy_opts, "index", -1);

    max_devs = if_max_devs[type];

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            error_setg(errp, "index cannot be used with bus and unit");
            goto fail;
        }
        bus_id = drive_index_to_bus_id(type, index);
        unit_id = drive_index_to_unit_id(type, index);
    }

    if (unit_id == -1) {
       unit_id = 0;
       while (drive_get(type, bus_id, unit_id) != NULL) {
           unit_id++;
           if (max_devs && unit_id >= max_devs) {
               unit_id -= max_devs;
               bus_id++;
           }
       }
    }

    if (max_devs && unit_id >= max_devs) {
        error_setg(errp, "unit %d too big (max is %d)", unit_id, max_devs - 1);
        goto fail;
    }

    if (drive_get(type, bus_id, unit_id) != NULL) {
        error_setg(errp, "drive with bus=%d, unit=%d (index=%d) exists",
                   bus_id, unit_id, index);
        goto fail;
    }

    /* no id supplied -> create one */
    if (qemu_opts_id(all_opts) == NULL) {
        char *new_id;
        const char *mediastr = "";
        if (type == IF_IDE || type == IF_SCSI) {
            mediastr = (media == MEDIA_CDROM) ? "-cd" : "-hd";
        }
        if (max_devs) {
            new_id = g_strdup_printf("%s%i%s%i", if_name[type], bus_id,
                                     mediastr, unit_id);
        } else {
            new_id = g_strdup_printf("%s%s%i", if_name[type],
                                     mediastr, unit_id);
        }
        qdict_put_str(bs_opts, "id", new_id);
        g_free(new_id);
    }

    /* Add virtio block device */
    if (type == IF_VIRTIO) {
        QemuOpts *devopts;
        devopts = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                   &error_abort);
        qemu_opt_set(devopts, "driver", "virtio-blk", &error_abort);
        qemu_opt_set(devopts, "drive", qdict_get_str(bs_opts, "id"),
                     &error_abort);
    } else if (type == IF_XEN) {
        QemuOpts *devopts;
        devopts = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                   &error_abort);
        qemu_opt_set(devopts, "driver",
                     (media == MEDIA_CDROM) ? "xen-cdrom" : "xen-disk",
                     &error_abort);
        qemu_opt_set(devopts, "drive", qdict_get_str(bs_opts, "id"),
                     &error_abort);
    }

    filename = qemu_opt_get(legacy_opts, "file");

    /* Check werror/rerror compatibility with if=... */
    werror = qemu_opt_get(legacy_opts, "werror");
    if (werror != NULL) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO &&
            type != IF_NONE) {
            error_setg(errp, "werror is not supported by this bus type");
            goto fail;
        }
        qdict_put_str(bs_opts, "werror", werror);
    }

    rerror = qemu_opt_get(legacy_opts, "rerror");
    if (rerror != NULL) {
        if (type != IF_IDE && type != IF_VIRTIO && type != IF_SCSI &&
            type != IF_NONE) {
            error_setg(errp, "rerror is not supported by this bus type");
            goto fail;
        }
        qdict_put_str(bs_opts, "rerror", rerror);
    }

    /* Actual block device init: Functionality shared with blockdev-add */
    blk = blockdev_init(filename, bs_opts, errp);
    bs_opts = NULL;
    if (!blk) {
        goto fail;
    }

    /* Create legacy DriveInfo */
    dinfo = g_malloc0(sizeof(*dinfo));
    dinfo->opts = all_opts;

    dinfo->type = type;
    dinfo->bus = bus_id;
    dinfo->unit = unit_id;

    blk_set_legacy_dinfo(blk, dinfo);

    switch(type) {
    case IF_IDE:
    case IF_SCSI:
    case IF_XEN:
    case IF_NONE:
        dinfo->media_cd = media == MEDIA_CDROM;
        break;
    default:
        break;
    }

fail:
    qemu_opts_del(legacy_opts);
    qobject_unref(bs_opts);
    return dinfo;
}

static BlockDriverState *qmp_get_root_bs(const char *name, Error **errp)
{
    BlockDriverState *bs;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs = bdrv_lookup_bs(name, name, errp);
    if (bs == NULL) {
        return NULL;
    }

    if (!bdrv_is_root_node(bs)) {
        error_setg(errp, "Need a root block node");
        return NULL;
    }

    if (!bdrv_is_inserted(bs)) {
        error_setg(errp, "Device has no medium");
        bs = NULL;
    }

    return bs;
}

static void blockdev_do_action(TransactionAction *action, Error **errp)
{
    TransactionActionList list;

    list.value = action;
    list.next = NULL;
    qmp_transaction(&list, NULL, errp);
}

void qmp_blockdev_snapshot_sync(const char *device, const char *node_name,
                                const char *snapshot_file,
                                const char *snapshot_node_name,
                                const char *format,
                                bool has_mode, NewImageMode mode, Error **errp)
{
    BlockdevSnapshotSync snapshot = {
        .device = (char *) device,
        .node_name = (char *) node_name,
        .snapshot_file = (char *) snapshot_file,
        .snapshot_node_name = (char *) snapshot_node_name,
        .format = (char *) format,
        .has_mode = has_mode,
        .mode = mode,
    };
    TransactionAction action = {
        .type = TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC,
        .u.blockdev_snapshot_sync.data = &snapshot,
    };
    blockdev_do_action(&action, errp);
}

void qmp_blockdev_snapshot(const char *node, const char *overlay,
                           Error **errp)
{
    BlockdevSnapshot snapshot_data = {
        .node = (char *) node,
        .overlay = (char *) overlay
    };
    TransactionAction action = {
        .type = TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT,
        .u.blockdev_snapshot.data = &snapshot_data,
    };
    blockdev_do_action(&action, errp);
}

void qmp_blockdev_snapshot_internal_sync(const char *device,
                                         const char *name,
                                         Error **errp)
{
    BlockdevSnapshotInternal snapshot = {
        .device = (char *) device,
        .name = (char *) name
    };
    TransactionAction action = {
        .type = TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_INTERNAL_SYNC,
        .u.blockdev_snapshot_internal_sync.data = &snapshot,
    };
    blockdev_do_action(&action, errp);
}

SnapshotInfo *qmp_blockdev_snapshot_delete_internal_sync(const char *device,
                                                         const char *id,
                                                         const char *name,
                                                         Error **errp)
{
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    Error *local_err = NULL;
    SnapshotInfo *info = NULL;
    int ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs = qmp_get_root_bs(device, errp);
    if (!bs) {
        return NULL;
    }

    if (!id && !name) {
        error_setg(errp, "Name or id must be provided");
        return NULL;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_INTERNAL_SNAPSHOT_DELETE, errp)) {
        return NULL;
    }

    ret = bdrv_snapshot_find_by_id_and_name(bs, id, name, &sn, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }
    if (!ret) {
        error_setg(errp,
                   "Snapshot with id '%s' and name '%s' does not exist on "
                   "device '%s'",
                   STR_OR_NULL(id), STR_OR_NULL(name), device);
        return NULL;
    }

    bdrv_snapshot_delete(bs, id, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    info = g_new0(SnapshotInfo, 1);
    info->id = g_strdup(sn.id_str);
    info->name = g_strdup(sn.name);
    info->date_nsec = sn.date_nsec;
    info->date_sec = sn.date_sec;
    info->vm_state_size = sn.vm_state_size;
    info->vm_clock_nsec = sn.vm_clock_nsec % 1000000000;
    info->vm_clock_sec = sn.vm_clock_nsec / 1000000000;
    if (sn.icount != -1ULL) {
        info->icount = sn.icount;
        info->has_icount = true;
    }

    return info;
}

/* internal snapshot private data */
typedef struct InternalSnapshotState {
    BlockDriverState *bs;
    QEMUSnapshotInfo sn;
    bool created;
} InternalSnapshotState;

static void internal_snapshot_abort(void *opaque);
static void internal_snapshot_clean(void *opaque);
TransactionActionDrv internal_snapshot_drv = {
    .abort = internal_snapshot_abort,
    .clean = internal_snapshot_clean,
};

static void internal_snapshot_action(BlockdevSnapshotInternal *internal,
                                     Transaction *tran, Error **errp)
{
    Error *local_err = NULL;
    const char *device;
    const char *name;
    BlockDriverState *bs;
    QEMUSnapshotInfo old_sn, *sn;
    bool ret;
    int64_t rt;
    InternalSnapshotState *state = g_new0(InternalSnapshotState, 1);
    int ret1;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    tran_add(tran, &internal_snapshot_drv, state);

    device = internal->device;
    name = internal->name;

    bs = qmp_get_root_bs(device, errp);
    if (!bs) {
        return;
    }

    state->bs = bs;

    /* Paired with .clean() */
    bdrv_drained_begin(bs);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_INTERNAL_SNAPSHOT, errp)) {
        return;
    }

    if (bdrv_is_read_only(bs)) {
        error_setg(errp, "Device '%s' is read only", device);
        return;
    }

    if (!bdrv_can_snapshot(bs)) {
        error_setg(errp, "Block format '%s' used by device '%s' "
                   "does not support internal snapshots",
                   bs->drv->format_name, device);
        return;
    }

    if (!strlen(name)) {
        error_setg(errp, "Name is empty");
        return;
    }

    /* check whether a snapshot with name exist */
    ret = bdrv_snapshot_find_by_id_and_name(bs, NULL, name, &old_sn,
                                            &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    } else if (ret) {
        error_setg(errp,
                   "Snapshot with name '%s' already exists on device '%s'",
                   name, device);
        return;
    }

    /* 3. take the snapshot */
    sn = &state->sn;
    pstrcpy(sn->name, sizeof(sn->name), name);
    rt = g_get_real_time();
    sn->date_sec = rt / G_USEC_PER_SEC;
    sn->date_nsec = (rt % G_USEC_PER_SEC) * 1000;
    sn->vm_clock_nsec = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (replay_mode != REPLAY_MODE_NONE) {
        sn->icount = replay_get_current_icount();
    } else {
        sn->icount = -1ULL;
    }

    ret1 = bdrv_snapshot_create(bs, sn);
    if (ret1 < 0) {
        error_setg_errno(errp, -ret1,
                         "Failed to create snapshot '%s' on device '%s'",
                         name, device);
        return;
    }

    /* 4. succeed, mark a snapshot is created */
    state->created = true;
}

static void internal_snapshot_abort(void *opaque)
{
    InternalSnapshotState *state = opaque;
    BlockDriverState *bs = state->bs;
    QEMUSnapshotInfo *sn = &state->sn;
    Error *local_error = NULL;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!state->created) {
        return;
    }

    if (bdrv_snapshot_delete(bs, sn->id_str, sn->name, &local_error) < 0) {
        error_reportf_err(local_error,
                          "Failed to delete snapshot with id '%s' and "
                          "name '%s' on device '%s' in abort: ",
                          sn->id_str, sn->name,
                          bdrv_get_device_name(bs));
    }
}

static void internal_snapshot_clean(void *opaque)
{
    g_autofree InternalSnapshotState *state = opaque;

    if (!state->bs) {
        return;
    }

    bdrv_drained_end(state->bs);
}

/* external snapshot private data */
typedef struct ExternalSnapshotState {
    BlockDriverState *old_bs;
    BlockDriverState *new_bs;
    bool overlay_appended;
} ExternalSnapshotState;

static void external_snapshot_commit(void *opaque);
static void external_snapshot_abort(void *opaque);
static void external_snapshot_clean(void *opaque);
TransactionActionDrv external_snapshot_drv = {
    .commit = external_snapshot_commit,
    .abort = external_snapshot_abort,
    .clean = external_snapshot_clean,
};

static void external_snapshot_action(TransactionAction *action,
                                     Transaction *tran, Error **errp)
{
    int ret;
    int flags = 0;
    QDict *options = NULL;
    Error *local_err = NULL;
    /* Device and node name of the image to generate the snapshot from */
    const char *device;
    const char *node_name;
    /* Reference to the new image (for 'blockdev-snapshot') */
    const char *snapshot_ref;
    /* File name of the new image (for 'blockdev-snapshot-sync') */
    const char *new_image_file;
    ExternalSnapshotState *state = g_new0(ExternalSnapshotState, 1);
    uint64_t perm, shared;

    /* TODO We'll eventually have to take a writer lock in this function */
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    tran_add(tran, &external_snapshot_drv, state);

    /* 'blockdev-snapshot' and 'blockdev-snapshot-sync' have similar
     * purpose but a different set of parameters */
    switch (action->type) {
    case TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT:
        {
            BlockdevSnapshot *s = action->u.blockdev_snapshot.data;
            device = s->node;
            node_name = s->node;
            new_image_file = NULL;
            snapshot_ref = s->overlay;
        }
        break;
    case TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC:
        {
            BlockdevSnapshotSync *s = action->u.blockdev_snapshot_sync.data;
            device = s->device;
            node_name = s->node_name;
            new_image_file = s->snapshot_file;
            snapshot_ref = NULL;
        }
        break;
    default:
        g_assert_not_reached();
    }

    /* start processing */

    state->old_bs = bdrv_lookup_bs(device, node_name, errp);
    if (!state->old_bs) {
        return;
    }

    /* Paired with .clean() */
    bdrv_drained_begin(state->old_bs);

    if (!bdrv_is_inserted(state->old_bs)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        return;
    }

    if (bdrv_op_is_blocked(state->old_bs,
                           BLOCK_OP_TYPE_EXTERNAL_SNAPSHOT, errp)) {
        return;
    }

    if (!bdrv_is_read_only(state->old_bs)) {
        if (bdrv_flush(state->old_bs)) {
            error_setg(errp, QERR_IO_ERROR);
            return;
        }
    }

    if (action->type == TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC) {
        BlockdevSnapshotSync *s = action->u.blockdev_snapshot_sync.data;
        const char *format = s->format ?: "qcow2";
        enum NewImageMode mode;
        const char *snapshot_node_name = s->snapshot_node_name;

        if (node_name && !snapshot_node_name) {
            error_setg(errp, "New overlay node-name missing");
            return;
        }

        if (snapshot_node_name &&
            bdrv_lookup_bs(snapshot_node_name, snapshot_node_name, NULL)) {
            error_setg(errp, "New overlay node-name already in use");
            return;
        }

        flags = state->old_bs->open_flags;
        flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_COPY_ON_READ);
        flags |= BDRV_O_NO_BACKING;

        /* create new image w/backing file */
        mode = s->has_mode ? s->mode : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
        if (mode != NEW_IMAGE_MODE_EXISTING) {
            int64_t size = bdrv_getlength(state->old_bs);
            if (size < 0) {
                error_setg_errno(errp, -size, "bdrv_getlength failed");
                return;
            }
            bdrv_refresh_filename(state->old_bs);

            bdrv_img_create(new_image_file, format,
                            state->old_bs->filename,
                            state->old_bs->drv->format_name,
                            NULL, size, flags, false, &local_err);

            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
        }

        options = qdict_new();
        if (snapshot_node_name) {
            qdict_put_str(options, "node-name", snapshot_node_name);
        }
        qdict_put_str(options, "driver", format);
    }

    state->new_bs = bdrv_open(new_image_file, snapshot_ref, options, flags,
                              errp);

    /* We will manually add the backing_hd field to the bs later */
    if (!state->new_bs) {
        return;
    }

    /*
     * Allow attaching a backing file to an overlay that's already in use only
     * if the parents don't assume that they are already seeing a valid image.
     * (Specifically, allow it as a mirror target, which is write-only access.)
     */
    bdrv_get_cumulative_perm(state->new_bs, &perm, &shared);
    if (perm & BLK_PERM_CONSISTENT_READ) {
        error_setg(errp, "The overlay is already in use");
        return;
    }

    if (state->new_bs->drv->is_filter) {
        error_setg(errp, "Filters cannot be used as overlays");
        return;
    }

    if (bdrv_cow_child(state->new_bs)) {
        error_setg(errp, "The overlay already has a backing image");
        return;
    }

    if (!state->new_bs->drv->supports_backing) {
        error_setg(errp, "The overlay does not support backing images");
        return;
    }

    ret = bdrv_append(state->new_bs, state->old_bs, errp);
    if (ret < 0) {
        return;
    }
    state->overlay_appended = true;
}

static void external_snapshot_commit(void *opaque)
{
    ExternalSnapshotState *state = opaque;

    /* We don't need (or want) to use the transactional
     * bdrv_reopen_multiple() across all the entries at once, because we
     * don't want to abort all of them if one of them fails the reopen */
    if (!qatomic_read(&state->old_bs->copy_on_read)) {
        bdrv_reopen_set_read_only(state->old_bs, true, NULL);
    }
}

static void external_snapshot_abort(void *opaque)
{
    ExternalSnapshotState *state = opaque;
    if (state->new_bs) {
        if (state->overlay_appended) {
            AioContext *aio_context;
            AioContext *tmp_context;
            int ret;

            aio_context = bdrv_get_aio_context(state->old_bs);

            bdrv_ref(state->old_bs);   /* we can't let bdrv_set_backind_hd()
                                          close state->old_bs; we need it */
            bdrv_set_backing_hd(state->new_bs, NULL, &error_abort);

            /*
             * The call to bdrv_set_backing_hd() above returns state->old_bs to
             * the main AioContext. As we're still going to be using it, return
             * it to the AioContext it was before.
             */
            tmp_context = bdrv_get_aio_context(state->old_bs);
            if (aio_context != tmp_context) {
                ret = bdrv_try_change_aio_context(state->old_bs,
                                                  aio_context, NULL, NULL);
                assert(ret == 0);
            }

            bdrv_drained_begin(state->new_bs);
            bdrv_graph_wrlock();
            bdrv_replace_node(state->new_bs, state->old_bs, &error_abort);
            bdrv_graph_wrunlock();
            bdrv_drained_end(state->new_bs);

            bdrv_unref(state->old_bs); /* bdrv_replace_node() ref'ed old_bs */
        }
    }
}

static void external_snapshot_clean(void *opaque)
{
    g_autofree ExternalSnapshotState *state = opaque;

    if (!state->old_bs) {
        return;
    }

    bdrv_drained_end(state->old_bs);
    bdrv_unref(state->new_bs);
}

typedef struct DriveBackupState {
    BlockDriverState *bs;
    BlockJob *job;
} DriveBackupState;

static BlockJob *do_backup_common(BackupCommon *backup,
                                  BlockDriverState *bs,
                                  BlockDriverState *target_bs,
                                  AioContext *aio_context,
                                  JobTxn *txn, Error **errp);

static void drive_backup_commit(void *opaque);
static void drive_backup_abort(void *opaque);
static void drive_backup_clean(void *opaque);
TransactionActionDrv drive_backup_drv = {
    .commit = drive_backup_commit,
    .abort = drive_backup_abort,
    .clean = drive_backup_clean,
};

static void drive_backup_action(DriveBackup *backup,
                                JobTxn *block_job_txn,
                                Transaction *tran, Error **errp)
{
    DriveBackupState *state = g_new0(DriveBackupState, 1);
    BlockDriverState *bs;
    BlockDriverState *target_bs;
    BlockDriverState *source = NULL;
    AioContext *aio_context;
    const char *format;
    QDict *options;
    Error *local_err = NULL;
    int flags;
    int64_t size;
    bool set_backing_hd = false;
    int ret;

    GLOBAL_STATE_CODE();

    tran_add(tran, &drive_backup_drv, state);

    if (!backup->has_mode) {
        backup->mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    }

    bs = bdrv_lookup_bs(backup->device, backup->device, errp);
    if (!bs) {
        return;
    }

    if (!bs->drv) {
        error_setg(errp, "Device has no medium");
        return;
    }

    aio_context = bdrv_get_aio_context(bs);

    state->bs = bs;
    /* Paired with .clean() */
    bdrv_drained_begin(bs);

    format = backup->format;
    if (!format && backup->mode != NEW_IMAGE_MODE_EXISTING) {
        format = bs->drv->format_name;
    }

    /* Early check to avoid creating target */
    bdrv_graph_rdlock_main_loop();
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BACKUP_SOURCE, errp)) {
        bdrv_graph_rdunlock_main_loop();
        return;
    }

    flags = bs->open_flags | BDRV_O_RDWR;

    /*
     * See if we have a backing HD we can use to create our new image
     * on top of.
     */
    if (backup->sync == MIRROR_SYNC_MODE_TOP) {
        /*
         * Backup will not replace the source by the target, so none
         * of the filters skipped here will be removed (in contrast to
         * mirror).  Therefore, we can skip all of them when looking
         * for the first COW relationship.
         */
        source = bdrv_cow_bs(bdrv_skip_filters(bs));
        if (!source) {
            backup->sync = MIRROR_SYNC_MODE_FULL;
        }
    }
    if (backup->sync == MIRROR_SYNC_MODE_NONE) {
        source = bs;
        flags |= BDRV_O_NO_BACKING;
        set_backing_hd = true;
    }
    bdrv_graph_rdunlock_main_loop();

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "bdrv_getlength failed");
        return;
    }

    if (backup->mode != NEW_IMAGE_MODE_EXISTING) {
        assert(format);
        if (source) {
            /* Implicit filters should not appear in the filename */
            BlockDriverState *explicit_backing;

            bdrv_graph_rdlock_main_loop();
            explicit_backing = bdrv_skip_implicit_filters(source);
            bdrv_refresh_filename(explicit_backing);
            bdrv_graph_rdunlock_main_loop();

            bdrv_img_create(backup->target, format,
                            explicit_backing->filename,
                            explicit_backing->drv->format_name, NULL,
                            size, flags, false, &local_err);
        } else {
            bdrv_img_create(backup->target, format, NULL, NULL, NULL,
                            size, flags, false, &local_err);
        }
    }

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    options = qdict_new();
    qdict_put_str(options, "discard", "unmap");
    qdict_put_str(options, "detect-zeroes", "unmap");
    if (format) {
        qdict_put_str(options, "driver", format);
    }

    target_bs = bdrv_open(backup->target, NULL, options, flags, errp);
    if (!target_bs) {
        return;
    }

    ret = bdrv_try_change_aio_context(target_bs, aio_context, NULL, errp);
    if (ret < 0) {
        bdrv_unref(target_bs);
        return;
    }

    if (set_backing_hd) {
        if (bdrv_set_backing_hd(target_bs, source, errp) < 0) {
            goto unref;
        }
    }

    state->job = do_backup_common(qapi_DriveBackup_base(backup),
                                  bs, target_bs, aio_context,
                                  block_job_txn, errp);

unref:
    bdrv_unref(target_bs);
}

static void drive_backup_commit(void *opaque)
{
    DriveBackupState *state = opaque;

    assert(state->job);
    job_start(&state->job->job);
}

static void drive_backup_abort(void *opaque)
{
    DriveBackupState *state = opaque;

    if (state->job) {
        job_cancel_sync(&state->job->job, true);
    }
}

static void drive_backup_clean(void *opaque)
{
    g_autofree DriveBackupState *state = opaque;

    if (!state->bs) {
        return;
    }

    bdrv_drained_end(state->bs);
}

typedef struct BlockdevBackupState {
    BlockDriverState *bs;
    BlockJob *job;
} BlockdevBackupState;

static void blockdev_backup_commit(void *opaque);
static void blockdev_backup_abort(void *opaque);
static void blockdev_backup_clean(void *opaque);
TransactionActionDrv blockdev_backup_drv = {
    .commit = blockdev_backup_commit,
    .abort = blockdev_backup_abort,
    .clean = blockdev_backup_clean,
};

static void blockdev_backup_action(BlockdevBackup *backup,
                                   JobTxn *block_job_txn,
                                   Transaction *tran, Error **errp)
{
    BlockdevBackupState *state = g_new0(BlockdevBackupState, 1);
    BlockDriverState *bs;
    BlockDriverState *target_bs;
    AioContext *aio_context;
    int ret;

    tran_add(tran, &blockdev_backup_drv, state);

    bs = bdrv_lookup_bs(backup->device, backup->device, errp);
    if (!bs) {
        return;
    }

    target_bs = bdrv_lookup_bs(backup->target, backup->target, errp);
    if (!target_bs) {
        return;
    }

    /* Honor bdrv_try_change_aio_context() context acquisition requirements. */
    aio_context = bdrv_get_aio_context(bs);

    ret = bdrv_try_change_aio_context(target_bs, aio_context, NULL, errp);
    if (ret < 0) {
        return;
    }

    state->bs = bs;

    /* Paired with .clean() */
    bdrv_drained_begin(state->bs);

    state->job = do_backup_common(qapi_BlockdevBackup_base(backup),
                                  bs, target_bs, aio_context,
                                  block_job_txn, errp);
}

static void blockdev_backup_commit(void *opaque)
{
    BlockdevBackupState *state = opaque;

    assert(state->job);
    job_start(&state->job->job);
}

static void blockdev_backup_abort(void *opaque)
{
    BlockdevBackupState *state = opaque;

    if (state->job) {
        job_cancel_sync(&state->job->job, true);
    }
}

static void blockdev_backup_clean(void *opaque)
{
    g_autofree BlockdevBackupState *state = opaque;

    if (!state->bs) {
        return;
    }

    bdrv_drained_end(state->bs);
}

typedef struct BlockDirtyBitmapState {
    BdrvDirtyBitmap *bitmap;
    BlockDriverState *bs;
    HBitmap *backup;
    bool was_enabled;
} BlockDirtyBitmapState;

static void block_dirty_bitmap_add_abort(void *opaque);
TransactionActionDrv block_dirty_bitmap_add_drv = {
    .abort = block_dirty_bitmap_add_abort,
    .clean = g_free,
};

static void block_dirty_bitmap_add_action(BlockDirtyBitmapAdd *action,
                                          Transaction *tran, Error **errp)
{
    Error *local_err = NULL;
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_add_drv, state);

    /* AIO context taken and released within qmp_block_dirty_bitmap_add */
    qmp_block_dirty_bitmap_add(action->node, action->name,
                               action->has_granularity, action->granularity,
                               action->has_persistent, action->persistent,
                               action->has_disabled, action->disabled,
                               &local_err);

    if (!local_err) {
        state->bitmap = block_dirty_bitmap_lookup(action->node, action->name,
                                                  NULL, &error_abort);
    } else {
        error_propagate(errp, local_err);
    }
}

static void block_dirty_bitmap_add_abort(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    if (state->bitmap) {
        bdrv_release_dirty_bitmap(state->bitmap);
    }
}

static void block_dirty_bitmap_restore(void *opaque);
static void block_dirty_bitmap_free_backup(void *opaque);
TransactionActionDrv block_dirty_bitmap_clear_drv = {
    .abort = block_dirty_bitmap_restore,
    .commit = block_dirty_bitmap_free_backup,
    .clean = g_free,
};

static void block_dirty_bitmap_clear_action(BlockDirtyBitmap *action,
                                            Transaction *tran, Error **errp)
{
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_clear_drv, state);

    state->bitmap = block_dirty_bitmap_lookup(action->node,
                                              action->name,
                                              &state->bs,
                                              errp);
    if (!state->bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_check(state->bitmap, BDRV_BITMAP_DEFAULT, errp)) {
        return;
    }

    bdrv_clear_dirty_bitmap(state->bitmap, &state->backup);
}

static void block_dirty_bitmap_restore(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    if (state->backup) {
        bdrv_restore_dirty_bitmap(state->bitmap, state->backup);
    }
}

static void block_dirty_bitmap_free_backup(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    hbitmap_free(state->backup);
}

static void block_dirty_bitmap_enable_abort(void *opaque);
TransactionActionDrv block_dirty_bitmap_enable_drv = {
    .abort = block_dirty_bitmap_enable_abort,
    .clean = g_free,
};

static void block_dirty_bitmap_enable_action(BlockDirtyBitmap *action,
                                             Transaction *tran, Error **errp)
{
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_enable_drv, state);

    state->bitmap = block_dirty_bitmap_lookup(action->node,
                                              action->name,
                                              NULL,
                                              errp);
    if (!state->bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_check(state->bitmap, BDRV_BITMAP_ALLOW_RO, errp)) {
        return;
    }

    state->was_enabled = bdrv_dirty_bitmap_enabled(state->bitmap);
    bdrv_enable_dirty_bitmap(state->bitmap);
}

static void block_dirty_bitmap_enable_abort(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    if (!state->was_enabled) {
        bdrv_disable_dirty_bitmap(state->bitmap);
    }
}

static void block_dirty_bitmap_disable_abort(void *opaque);
TransactionActionDrv block_dirty_bitmap_disable_drv = {
    .abort = block_dirty_bitmap_disable_abort,
    .clean = g_free,
};

static void block_dirty_bitmap_disable_action(BlockDirtyBitmap *action,
                                              Transaction *tran, Error **errp)
{
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_disable_drv, state);

    state->bitmap = block_dirty_bitmap_lookup(action->node,
                                              action->name,
                                              NULL,
                                              errp);
    if (!state->bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_check(state->bitmap, BDRV_BITMAP_ALLOW_RO, errp)) {
        return;
    }

    state->was_enabled = bdrv_dirty_bitmap_enabled(state->bitmap);
    bdrv_disable_dirty_bitmap(state->bitmap);
}

static void block_dirty_bitmap_disable_abort(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    if (state->was_enabled) {
        bdrv_enable_dirty_bitmap(state->bitmap);
    }
}

TransactionActionDrv block_dirty_bitmap_merge_drv = {
    .commit = block_dirty_bitmap_free_backup,
    .abort = block_dirty_bitmap_restore,
    .clean = g_free,
};

static void block_dirty_bitmap_merge_action(BlockDirtyBitmapMerge *action,
                                            Transaction *tran, Error **errp)
{
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_merge_drv, state);

    state->bitmap = block_dirty_bitmap_merge(action->node, action->target,
                                             action->bitmaps, &state->backup,
                                             errp);
}

static void block_dirty_bitmap_remove_commit(void *opaque);
static void block_dirty_bitmap_remove_abort(void *opaque);
TransactionActionDrv block_dirty_bitmap_remove_drv = {
    .commit = block_dirty_bitmap_remove_commit,
    .abort = block_dirty_bitmap_remove_abort,
    .clean = g_free,
};

static void block_dirty_bitmap_remove_action(BlockDirtyBitmap *action,
                                             Transaction *tran, Error **errp)
{
    BlockDirtyBitmapState *state = g_new0(BlockDirtyBitmapState, 1);

    tran_add(tran, &block_dirty_bitmap_remove_drv, state);


    state->bitmap = block_dirty_bitmap_remove(action->node, action->name,
                                              false, &state->bs, errp);
    if (state->bitmap) {
        bdrv_dirty_bitmap_skip_store(state->bitmap, true);
        bdrv_dirty_bitmap_set_busy(state->bitmap, true);
    }
}

static void block_dirty_bitmap_remove_abort(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    if (state->bitmap) {
        bdrv_dirty_bitmap_skip_store(state->bitmap, false);
        bdrv_dirty_bitmap_set_busy(state->bitmap, false);
    }
}

static void block_dirty_bitmap_remove_commit(void *opaque)
{
    BlockDirtyBitmapState *state = opaque;

    bdrv_dirty_bitmap_set_busy(state->bitmap, false);
    bdrv_release_dirty_bitmap(state->bitmap);
}

static void abort_commit(void *opaque);
TransactionActionDrv abort_drv = {
    .commit = abort_commit,
};

static void abort_action(Transaction *tran, Error **errp)
{
    tran_add(tran, &abort_drv, NULL);
    error_setg(errp, "Transaction aborted using Abort action");
}

static void abort_commit(void *opaque)
{
    g_assert_not_reached(); /* this action never succeeds */
}

static void transaction_action(TransactionAction *act, JobTxn *block_job_txn,
                               Transaction *tran, Error **errp)
{
    switch (act->type) {
    case TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT:
    case TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC:
        external_snapshot_action(act, tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_DRIVE_BACKUP:
        drive_backup_action(act->u.drive_backup.data,
                            block_job_txn, tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCKDEV_BACKUP:
        blockdev_backup_action(act->u.blockdev_backup.data,
                               block_job_txn, tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_ABORT:
        abort_action(tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_INTERNAL_SYNC:
        internal_snapshot_action(act->u.blockdev_snapshot_internal_sync.data,
                                 tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_ADD:
        block_dirty_bitmap_add_action(act->u.block_dirty_bitmap_add.data,
                                      tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_CLEAR:
        block_dirty_bitmap_clear_action(act->u.block_dirty_bitmap_clear.data,
                                        tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_ENABLE:
        block_dirty_bitmap_enable_action(act->u.block_dirty_bitmap_enable.data,
                                         tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_DISABLE:
        block_dirty_bitmap_disable_action(
                act->u.block_dirty_bitmap_disable.data, tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_MERGE:
        block_dirty_bitmap_merge_action(act->u.block_dirty_bitmap_merge.data,
                                        tran, errp);
        return;
    case TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_REMOVE:
        block_dirty_bitmap_remove_action(act->u.block_dirty_bitmap_remove.data,
                                         tran, errp);
        return;
    /*
     * Where are transactions for MIRROR, COMMIT and STREAM?
     * Although these blockjobs use transaction callbacks like the backup job,
     * these jobs do not necessarily adhere to transaction semantics.
     * These jobs may not fully undo all of their actions on abort, nor do they
     * necessarily work in transactions with more than one job in them.
     */
    case TRANSACTION_ACTION_KIND__MAX:
    default:
        g_assert_not_reached();
    };
}


/*
 * 'Atomic' group operations.  The operations are performed as a set, and if
 * any fail then we roll back all operations in the group.
 *
 * Always run under BQL.
 */
void qmp_transaction(TransactionActionList *actions,
                     struct TransactionProperties *properties,
                     Error **errp)
{
    TransactionActionList *act;
    JobTxn *block_job_txn = NULL;
    Error *local_err = NULL;
    Transaction *tran;
    ActionCompletionMode comp_mode =
        properties ? properties->completion_mode :
        ACTION_COMPLETION_MODE_INDIVIDUAL;

    GLOBAL_STATE_CODE();

    /* Does this transaction get canceled as a group on failure?
     * If not, we don't really need to make a JobTxn.
     */
    if (comp_mode != ACTION_COMPLETION_MODE_INDIVIDUAL) {
        for (act = actions; act; act = act->next) {
            TransactionActionKind type = act->value->type;

            if (type != TRANSACTION_ACTION_KIND_BLOCKDEV_BACKUP &&
                type != TRANSACTION_ACTION_KIND_DRIVE_BACKUP)
            {
                error_setg(errp,
                           "Action '%s' does not support transaction property "
                           "completion-mode = %s",
                           TransactionActionKind_str(type),
                           ActionCompletionMode_str(comp_mode));
                return;
            }
        }

        block_job_txn = job_txn_new();
    }

    /* drain all i/o before any operations */
    bdrv_drain_all();

    tran = tran_new();

    /* We don't do anything in this loop that commits us to the operations */
    for (act = actions; act; act = act->next) {
        transaction_action(act->value, block_job_txn, tran, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto delete_and_fail;
        }
    }

    tran_commit(tran);

    /* success */
    goto exit;

delete_and_fail:
    /* failure, and it is all-or-none; roll back all operations */
    tran_abort(tran);
exit:
    job_txn_unref(block_job_txn);
}

BlockDirtyBitmapSha256 *qmp_x_debug_block_dirty_bitmap_sha256(const char *node,
                                                              const char *name,
                                                              Error **errp)
{
    BdrvDirtyBitmap *bitmap;
    BlockDriverState *bs;
    BlockDirtyBitmapSha256 *ret = NULL;
    char *sha256;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, errp);
    if (!bitmap || !bs) {
        return NULL;
    }

    sha256 = bdrv_dirty_bitmap_sha256(bitmap, errp);
    if (sha256 == NULL) {
        return NULL;
    }

    ret = g_new(BlockDirtyBitmapSha256, 1);
    ret->sha256 = sha256;

    return ret;
}

void coroutine_fn qmp_block_resize(const char *device, const char *node_name,
                                   int64_t size, Error **errp)
{
    Error *local_err = NULL;
    BlockBackend *blk;
    BlockDriverState *bs;
    AioContext *old_ctx;

    bs = bdrv_lookup_bs(device, node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (size < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "size", "a >0 size");
        return;
    }

    bdrv_graph_co_rdlock();
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_RESIZE, errp)) {
        bdrv_graph_co_rdunlock();
        return;
    }
    bdrv_graph_co_rdunlock();

    blk = blk_co_new_with_bs(bs, BLK_PERM_RESIZE, BLK_PERM_ALL, errp);
    if (!blk) {
        return;
    }

    bdrv_drained_begin(bs);

    old_ctx = bdrv_co_enter(bs);
    blk_co_truncate(blk, size, false, PREALLOC_MODE_OFF, 0, errp);
    bdrv_co_leave(bs, old_ctx);

    bdrv_drained_end(bs);
    blk_co_unref(blk);
}

void qmp_block_stream(const char *job_id, const char *device,
                      const char *base,
                      const char *base_node,
                      const char *backing_file,
                      bool has_backing_mask_protocol,
                      bool backing_mask_protocol,
                      const char *bottom,
                      bool has_speed, int64_t speed,
                      bool has_on_error, BlockdevOnError on_error,
                      const char *filter_node_name,
                      bool has_auto_finalize, bool auto_finalize,
                      bool has_auto_dismiss, bool auto_dismiss,
                      Error **errp)
{
    BlockDriverState *bs, *iter, *iter_end;
    BlockDriverState *base_bs = NULL;
    BlockDriverState *bottom_bs = NULL;
    AioContext *aio_context;
    Error *local_err = NULL;
    int job_flags = JOB_DEFAULT;

    GLOBAL_STATE_CODE();

    if (base && base_node) {
        error_setg(errp, "'base' and 'base-node' cannot be specified "
                   "at the same time");
        return;
    }

    if (base && bottom) {
        error_setg(errp, "'base' and 'bottom' cannot be specified "
                   "at the same time");
        return;
    }

    if (bottom && base_node) {
        error_setg(errp, "'bottom' and 'base-node' cannot be specified "
                   "at the same time");
        return;
    }

    if (!has_backing_mask_protocol) {
        backing_mask_protocol = false;
    }

    if (!has_on_error) {
        on_error = BLOCKDEV_ON_ERROR_REPORT;
    }

    bs = bdrv_lookup_bs(device, device, errp);
    if (!bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);

    bdrv_graph_rdlock_main_loop();
    if (base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (base_bs == NULL) {
            error_setg(errp, "Can't find '%s' in the backing chain", base);
            goto out_rdlock;
        }
        assert(bdrv_get_aio_context(base_bs) == aio_context);
    }

    if (base_node) {
        base_bs = bdrv_lookup_bs(NULL, base_node, errp);
        if (!base_bs) {
            goto out_rdlock;
        }
        if (bs == base_bs || !bdrv_chain_contains(bs, base_bs)) {
            error_setg(errp, "Node '%s' is not a backing image of '%s'",
                       base_node, device);
            goto out_rdlock;
        }
        assert(bdrv_get_aio_context(base_bs) == aio_context);

        bdrv_refresh_filename(base_bs);
    }

    if (bottom) {
        bottom_bs = bdrv_lookup_bs(NULL, bottom, errp);
        if (!bottom_bs) {
            goto out_rdlock;
        }
        if (!bottom_bs->drv) {
            error_setg(errp, "Node '%s' is not open", bottom);
            goto out_rdlock;
        }
        if (bottom_bs->drv->is_filter) {
            error_setg(errp, "Node '%s' is a filter, use a non-filter node "
                       "as 'bottom'", bottom);
            goto out_rdlock;
        }
        if (!bdrv_chain_contains(bs, bottom_bs)) {
            error_setg(errp, "Node '%s' is not in a chain starting from '%s'",
                       bottom, device);
            goto out_rdlock;
        }
        assert(bdrv_get_aio_context(bottom_bs) == aio_context);
    }

    /*
     * Check for op blockers in the whole chain between bs and base (or bottom)
     */
    iter_end = bottom ? bdrv_filter_or_cow_bs(bottom_bs) : base_bs;
    for (iter = bs; iter && iter != iter_end;
         iter = bdrv_filter_or_cow_bs(iter))
    {
        if (bdrv_op_is_blocked(iter, BLOCK_OP_TYPE_STREAM, errp)) {
            goto out_rdlock;
        }
    }
    bdrv_graph_rdunlock_main_loop();

    /* if we are streaming the entire chain, the result will have no backing
     * file, and specifying one is therefore an error */
    if (!base_bs && backing_file) {
        error_setg(errp, "backing file specified, but streaming the "
                         "entire chain");
        return;
    }

    if (has_auto_finalize && !auto_finalize) {
        job_flags |= JOB_MANUAL_FINALIZE;
    }
    if (has_auto_dismiss && !auto_dismiss) {
        job_flags |= JOB_MANUAL_DISMISS;
    }

    stream_start(job_id, bs, base_bs, backing_file,
                 backing_mask_protocol,
                 bottom_bs, job_flags, has_speed ? speed : 0, on_error,
                 filter_node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    trace_qmp_block_stream(bs);
    return;

out_rdlock:
    bdrv_graph_rdunlock_main_loop();
}

void qmp_block_commit(const char *job_id, const char *device,
                      const char *base_node,
                      const char *base,
                      const char *top_node,
                      const char *top,
                      const char *backing_file,
                      bool has_backing_mask_protocol,
                      bool backing_mask_protocol,
                      bool has_speed, int64_t speed,
                      bool has_on_error, BlockdevOnError on_error,
                      const char *filter_node_name,
                      bool has_auto_finalize, bool auto_finalize,
                      bool has_auto_dismiss, bool auto_dismiss,
                      Error **errp)
{
    BlockDriverState *bs;
    BlockDriverState *iter;
    BlockDriverState *base_bs, *top_bs;
    AioContext *aio_context;
    Error *local_err = NULL;
    int job_flags = JOB_DEFAULT;
    uint64_t top_perm, top_shared;

    /* TODO We'll eventually have to take a writer lock in this function */
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!has_speed) {
        speed = 0;
    }
    if (!has_on_error) {
        on_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (has_auto_finalize && !auto_finalize) {
        job_flags |= JOB_MANUAL_FINALIZE;
    }
    if (has_auto_dismiss && !auto_dismiss) {
        job_flags |= JOB_MANUAL_DISMISS;
    }
    if (!has_backing_mask_protocol) {
        backing_mask_protocol = false;
    }

    /* Important Note:
     *  libvirt relies on the DeviceNotFound error class in order to probe for
     *  live commit feature versions; for this to work, we must make sure to
     *  perform the device lookup before any generic errors that may occur in a
     *  scenario in which all optional arguments are omitted. */
    bs = qmp_get_root_bs(device, &local_err);
    if (!bs) {
        bs = bdrv_lookup_bs(device, device, NULL);
        if (!bs) {
            error_free(local_err);
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", device);
        } else {
            error_propagate(errp, local_err);
        }
        return;
    }

    aio_context = bdrv_get_aio_context(bs);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT_SOURCE, errp)) {
        return;
    }

    /* default top_bs is the active layer */
    top_bs = bs;

    if (top_node && top) {
        error_setg(errp, "'top-node' and 'top' are mutually exclusive");
        return;
    } else if (top_node) {
        top_bs = bdrv_lookup_bs(NULL, top_node, errp);
        if (top_bs == NULL) {
            return;
        }
        if (!bdrv_chain_contains(bs, top_bs)) {
            error_setg(errp, "'%s' is not in this backing file chain",
                       top_node);
            return;
        }
    } else if (top) {
        /* This strcmp() is just a shortcut, there is no need to
         * refresh @bs's filename.  If it mismatches,
         * bdrv_find_backing_image() will do the refresh and may still
         * return @bs. */
        if (strcmp(bs->filename, top) != 0) {
            top_bs = bdrv_find_backing_image(bs, top);
        }
    }

    if (top_bs == NULL) {
        error_setg(errp, "Top image file %s not found", top ? top : "NULL");
        return;
    }

    assert(bdrv_get_aio_context(top_bs) == aio_context);

    if (base_node && base) {
        error_setg(errp, "'base-node' and 'base' are mutually exclusive");
        return;
    } else if (base_node) {
        base_bs = bdrv_lookup_bs(NULL, base_node, errp);
        if (base_bs == NULL) {
            return;
        }
        if (!bdrv_chain_contains(top_bs, base_bs)) {
            error_setg(errp, "'%s' is not in this backing file chain",
                       base_node);
            return;
        }
    } else if (base) {
        base_bs = bdrv_find_backing_image(top_bs, base);
        if (base_bs == NULL) {
            error_setg(errp, "Can't find '%s' in the backing chain", base);
            return;
        }
    } else {
        base_bs = bdrv_find_base(top_bs);
        if (base_bs == NULL) {
            error_setg(errp, "There is no backimg image");
            return;
        }
    }

    assert(bdrv_get_aio_context(base_bs) == aio_context);

    for (iter = top_bs; iter != bdrv_filter_or_cow_bs(base_bs);
         iter = bdrv_filter_or_cow_bs(iter))
    {
        if (bdrv_op_is_blocked(iter, BLOCK_OP_TYPE_COMMIT_TARGET, errp)) {
            return;
        }
    }

    /* Do not allow attempts to commit an image into itself */
    if (top_bs == base_bs) {
        error_setg(errp, "cannot commit an image into itself");
        return;
    }

    /*
     * Active commit is required if and only if someone has taken a
     * WRITE permission on the top node.  Historically, we have always
     * used active commit for top nodes, so continue that practice
     * lest we possibly break clients that rely on this behavior, e.g.
     * to later attach this node to a writing parent.
     * (Active commit is never really wrong.)
     */
    bdrv_get_cumulative_perm(top_bs, &top_perm, &top_shared);
    if (top_perm & BLK_PERM_WRITE ||
        bdrv_skip_filters(top_bs) == bdrv_skip_filters(bs))
    {
        if (backing_file) {
            if (bdrv_skip_filters(top_bs) == bdrv_skip_filters(bs)) {
                error_setg(errp, "'backing-file' specified,"
                                 " but 'top' is the active layer");
            } else {
                error_setg(errp, "'backing-file' specified, but 'top' has a "
                                 "writer on it");
            }
            return;
        }
        if (!job_id) {
            /*
             * Emulate here what block_job_create() does, because it
             * is possible that @bs != @top_bs (the block job should
             * be named after @bs, even if @top_bs is the actual
             * source)
             */
            job_id = bdrv_get_device_name(bs);
        }
        commit_active_start(job_id, top_bs, base_bs, job_flags, speed, on_error,
                            filter_node_name, NULL, NULL, false, &local_err);
    } else {
        BlockDriverState *overlay_bs = bdrv_find_overlay(bs, top_bs);
        if (bdrv_op_is_blocked(overlay_bs, BLOCK_OP_TYPE_COMMIT_TARGET, errp)) {
            return;
        }
        commit_start(job_id, bs, base_bs, top_bs, job_flags,
                     speed, on_error, backing_file,
                     backing_mask_protocol,
                     filter_node_name, &local_err);
    }
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

/* Common QMP interface for drive-backup and blockdev-backup */
static BlockJob *do_backup_common(BackupCommon *backup,
                                  BlockDriverState *bs,
                                  BlockDriverState *target_bs,
                                  AioContext *aio_context,
                                  JobTxn *txn, Error **errp)
{
    BlockJob *job = NULL;
    BdrvDirtyBitmap *bmap = NULL;
    BackupPerf perf = { .max_workers = 64 };
    int job_flags = JOB_DEFAULT;

    if (!backup->has_speed) {
        backup->speed = 0;
    }
    if (!backup->has_on_source_error) {
        backup->on_source_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!backup->has_on_target_error) {
        backup->on_target_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!backup->has_auto_finalize) {
        backup->auto_finalize = true;
    }
    if (!backup->has_auto_dismiss) {
        backup->auto_dismiss = true;
    }
    if (!backup->has_compress) {
        backup->compress = false;
    }

    if (backup->x_perf) {
        if (backup->x_perf->has_use_copy_range) {
            perf.use_copy_range = backup->x_perf->use_copy_range;
        }
        if (backup->x_perf->has_max_workers) {
            perf.max_workers = backup->x_perf->max_workers;
        }
        if (backup->x_perf->has_max_chunk) {
            perf.max_chunk = backup->x_perf->max_chunk;
        }
    }

    if ((backup->sync == MIRROR_SYNC_MODE_BITMAP) ||
        (backup->sync == MIRROR_SYNC_MODE_INCREMENTAL)) {
        /* done before desugaring 'incremental' to print the right message */
        if (!backup->bitmap) {
            error_setg(errp, "must provide a valid bitmap name for "
                       "'%s' sync mode", MirrorSyncMode_str(backup->sync));
            return NULL;
        }
    }

    if (backup->sync == MIRROR_SYNC_MODE_INCREMENTAL) {
        if (backup->has_bitmap_mode &&
            backup->bitmap_mode != BITMAP_SYNC_MODE_ON_SUCCESS) {
            error_setg(errp, "Bitmap sync mode must be '%s' "
                       "when using sync mode '%s'",
                       BitmapSyncMode_str(BITMAP_SYNC_MODE_ON_SUCCESS),
                       MirrorSyncMode_str(backup->sync));
            return NULL;
        }
        backup->has_bitmap_mode = true;
        backup->sync = MIRROR_SYNC_MODE_BITMAP;
        backup->bitmap_mode = BITMAP_SYNC_MODE_ON_SUCCESS;
    }

    if (backup->bitmap) {
        bmap = bdrv_find_dirty_bitmap(bs, backup->bitmap);
        if (!bmap) {
            error_setg(errp, "Bitmap '%s' could not be found", backup->bitmap);
            return NULL;
        }
        if (!backup->has_bitmap_mode) {
            error_setg(errp, "Bitmap sync mode must be given "
                       "when providing a bitmap");
            return NULL;
        }
        if (bdrv_dirty_bitmap_check(bmap, BDRV_BITMAP_ALLOW_RO, errp)) {
            return NULL;
        }

        /* This does not produce a useful bitmap artifact: */
        if (backup->sync == MIRROR_SYNC_MODE_NONE) {
            error_setg(errp, "sync mode '%s' does not produce meaningful bitmap"
                       " outputs", MirrorSyncMode_str(backup->sync));
            return NULL;
        }

        /* If the bitmap isn't used for input or output, this is useless: */
        if (backup->bitmap_mode == BITMAP_SYNC_MODE_NEVER &&
            backup->sync != MIRROR_SYNC_MODE_BITMAP) {
            error_setg(errp, "Bitmap sync mode '%s' has no meaningful effect"
                       " when combined with sync mode '%s'",
                       BitmapSyncMode_str(backup->bitmap_mode),
                       MirrorSyncMode_str(backup->sync));
            return NULL;
        }
    }

    if (!backup->bitmap && backup->has_bitmap_mode) {
        error_setg(errp, "Cannot specify bitmap sync mode without a bitmap");
        return NULL;
    }

    if (!backup->auto_finalize) {
        job_flags |= JOB_MANUAL_FINALIZE;
    }
    if (!backup->auto_dismiss) {
        job_flags |= JOB_MANUAL_DISMISS;
    }

    job = backup_job_create(backup->job_id, bs, target_bs, backup->speed,
                            backup->sync, bmap, backup->bitmap_mode,
                            backup->compress,
                            backup->filter_node_name,
                            &perf,
                            backup->on_source_error,
                            backup->on_target_error,
                            job_flags, NULL, NULL, txn, errp);
    return job;
}

void qmp_drive_backup(DriveBackup *backup, Error **errp)
{
    TransactionAction action = {
        .type = TRANSACTION_ACTION_KIND_DRIVE_BACKUP,
        .u.drive_backup.data = backup,
    };
    blockdev_do_action(&action, errp);
}

BlockDeviceInfoList *qmp_query_named_block_nodes(bool has_flat,
                                                 bool flat,
                                                 Error **errp)
{
    bool return_flat = has_flat && flat;

    return bdrv_named_nodes_list(return_flat, errp);
}

XDbgBlockGraph *qmp_x_debug_query_block_graph(Error **errp)
{
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    return bdrv_get_xdbg_block_graph(errp);
}

void qmp_blockdev_backup(BlockdevBackup *backup, Error **errp)
{
    TransactionAction action = {
        .type = TRANSACTION_ACTION_KIND_BLOCKDEV_BACKUP,
        .u.blockdev_backup.data = backup,
    };
    blockdev_do_action(&action, errp);
}

/* Parameter check and block job starting for drive mirroring.
 * Caller should hold @device and @target's aio context (must be the same).
 **/
static void blockdev_mirror_common(const char *job_id, BlockDriverState *bs,
                                   BlockDriverState *target,
                                   const char *replaces,
                                   enum MirrorSyncMode sync,
                                   BlockMirrorBackingMode backing_mode,
                                   bool zero_target,
                                   bool has_speed, int64_t speed,
                                   bool has_granularity, uint32_t granularity,
                                   bool has_buf_size, int64_t buf_size,
                                   bool has_on_source_error,
                                   BlockdevOnError on_source_error,
                                   bool has_on_target_error,
                                   BlockdevOnError on_target_error,
                                   bool has_unmap, bool unmap,
                                   const char *filter_node_name,
                                   bool has_copy_mode, MirrorCopyMode copy_mode,
                                   bool has_auto_finalize, bool auto_finalize,
                                   bool has_auto_dismiss, bool auto_dismiss,
                                   Error **errp)
{
    BlockDriverState *unfiltered_bs;
    int job_flags = JOB_DEFAULT;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!has_speed) {
        speed = 0;
    }
    if (!has_on_source_error) {
        on_source_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!has_on_target_error) {
        on_target_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!has_granularity) {
        granularity = 0;
    }
    if (!has_buf_size) {
        buf_size = 0;
    }
    if (!has_unmap) {
        unmap = true;
    }
    if (!has_copy_mode) {
        copy_mode = MIRROR_COPY_MODE_BACKGROUND;
    }
    if (has_auto_finalize && !auto_finalize) {
        job_flags |= JOB_MANUAL_FINALIZE;
    }
    if (has_auto_dismiss && !auto_dismiss) {
        job_flags |= JOB_MANUAL_DISMISS;
    }

    if (granularity != 0 && (granularity < 512 || granularity > 1048576 * 64)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "granularity",
                   "a value in range [512B, 64MB]");
        return;
    }
    if (granularity & (granularity - 1)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "granularity",
                   "a power of 2");
        return;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_MIRROR_SOURCE, errp)) {
        return;
    }
    if (bdrv_op_is_blocked(target, BLOCK_OP_TYPE_MIRROR_TARGET, errp)) {
        return;
    }

    if (!bdrv_backing_chain_next(bs) && sync == MIRROR_SYNC_MODE_TOP) {
        sync = MIRROR_SYNC_MODE_FULL;
    }

    if (!replaces) {
        /* We want to mirror from @bs, but keep implicit filters on top */
        unfiltered_bs = bdrv_skip_implicit_filters(bs);
        if (unfiltered_bs != bs) {
            replaces = unfiltered_bs->node_name;
        }
    }

    if (replaces) {
        BlockDriverState *to_replace_bs;
        int64_t bs_size, replace_size;

        bs_size = bdrv_getlength(bs);
        if (bs_size < 0) {
            error_setg_errno(errp, -bs_size, "Failed to query device's size");
            return;
        }

        to_replace_bs = check_to_replace_node(bs, replaces, errp);
        if (!to_replace_bs) {
            return;
        }

        replace_size = bdrv_getlength(to_replace_bs);

        if (replace_size < 0) {
            error_setg_errno(errp, -replace_size,
                             "Failed to query the replacement node's size");
            return;
        }
        if (bs_size != replace_size) {
            error_setg(errp, "cannot replace image with a mirror image of "
                             "different size");
            return;
        }
    }

    /* pass the node name to replace to mirror start since it's loose coupling
     * and will allow to check whether the node still exist at mirror completion
     */
    mirror_start(job_id, bs, target,
                 replaces, job_flags,
                 speed, granularity, buf_size, sync, backing_mode, zero_target,
                 on_source_error, on_target_error, unmap, filter_node_name,
                 copy_mode, errp);
}

void qmp_drive_mirror(DriveMirror *arg, Error **errp)
{
    BlockDriverState *bs;
    BlockDriverState *target_backing_bs, *target_bs;
    AioContext *aio_context;
    BlockMirrorBackingMode backing_mode;
    Error *local_err = NULL;
    QDict *options = NULL;
    int flags;
    int64_t size;
    const char *format = arg->format;
    bool zero_target;
    int ret;

    bs = qmp_get_root_bs(arg->device, errp);
    if (!bs) {
        return;
    }

    /* Early check to avoid creating target */
    bdrv_graph_rdlock_main_loop();
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_MIRROR_SOURCE, errp)) {
        bdrv_graph_rdunlock_main_loop();
        return;
    }

    aio_context = bdrv_get_aio_context(bs);

    if (!arg->has_mode) {
        arg->mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    }

    if (!arg->format) {
        format = (arg->mode == NEW_IMAGE_MODE_EXISTING
                  ? NULL : bs->drv->format_name);
    }

    flags = bs->open_flags | BDRV_O_RDWR;
    target_backing_bs = bdrv_cow_bs(bdrv_skip_filters(bs));
    if (!target_backing_bs && arg->sync == MIRROR_SYNC_MODE_TOP) {
        arg->sync = MIRROR_SYNC_MODE_FULL;
    }
    if (arg->sync == MIRROR_SYNC_MODE_NONE) {
        target_backing_bs = bs;
    }
    bdrv_graph_rdunlock_main_loop();

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "bdrv_getlength failed");
        return;
    }

    if (arg->replaces) {
        if (!arg->node_name) {
            error_setg(errp, "a node-name must be provided when replacing a"
                             " named node of the graph");
            return;
        }
    }

    if (arg->mode == NEW_IMAGE_MODE_ABSOLUTE_PATHS) {
        backing_mode = MIRROR_SOURCE_BACKING_CHAIN;
    } else {
        backing_mode = MIRROR_OPEN_BACKING_CHAIN;
    }

    /* Don't open backing image in create() */
    flags |= BDRV_O_NO_BACKING;

    if ((arg->sync == MIRROR_SYNC_MODE_FULL || !target_backing_bs)
        && arg->mode != NEW_IMAGE_MODE_EXISTING)
    {
        /* create new image w/o backing file */
        assert(format);
        bdrv_img_create(arg->target, format,
                        NULL, NULL, NULL, size, flags, false, &local_err);
    } else {
        BlockDriverState *explicit_backing;

        switch (arg->mode) {
        case NEW_IMAGE_MODE_EXISTING:
            break;
        case NEW_IMAGE_MODE_ABSOLUTE_PATHS:
            /*
             * Create new image with backing file.
             * Implicit filters should not appear in the filename.
             */
            bdrv_graph_rdlock_main_loop();
            explicit_backing = bdrv_skip_implicit_filters(target_backing_bs);
            bdrv_refresh_filename(explicit_backing);
            bdrv_graph_rdunlock_main_loop();

            bdrv_img_create(arg->target, format,
                            explicit_backing->filename,
                            explicit_backing->drv->format_name,
                            NULL, size, flags, false, &local_err);
            break;
        default:
            abort();
        }
    }

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    options = qdict_new();
    if (arg->node_name) {
        qdict_put_str(options, "node-name", arg->node_name);
    }
    if (format) {
        qdict_put_str(options, "driver", format);
    }

    /* Mirroring takes care of copy-on-write using the source's backing
     * file.
     */
    target_bs = bdrv_open(arg->target, NULL, options, flags, errp);
    if (!target_bs) {
        return;
    }

    bdrv_graph_rdlock_main_loop();
    zero_target = (arg->sync == MIRROR_SYNC_MODE_FULL &&
                   (arg->mode == NEW_IMAGE_MODE_EXISTING ||
                    !bdrv_has_zero_init(target_bs)));
    bdrv_graph_rdunlock_main_loop();


    ret = bdrv_try_change_aio_context(target_bs, aio_context, NULL, errp);
    if (ret < 0) {
        bdrv_unref(target_bs);
        return;
    }

    blockdev_mirror_common(arg->job_id, bs, target_bs,
                           arg->replaces, arg->sync,
                           backing_mode, zero_target,
                           arg->has_speed, arg->speed,
                           arg->has_granularity, arg->granularity,
                           arg->has_buf_size, arg->buf_size,
                           arg->has_on_source_error, arg->on_source_error,
                           arg->has_on_target_error, arg->on_target_error,
                           arg->has_unmap, arg->unmap,
                           NULL,
                           arg->has_copy_mode, arg->copy_mode,
                           arg->has_auto_finalize, arg->auto_finalize,
                           arg->has_auto_dismiss, arg->auto_dismiss,
                           errp);
    bdrv_unref(target_bs);
}

void qmp_blockdev_mirror(const char *job_id,
                         const char *device, const char *target,
                         const char *replaces,
                         MirrorSyncMode sync,
                         bool has_speed, int64_t speed,
                         bool has_granularity, uint32_t granularity,
                         bool has_buf_size, int64_t buf_size,
                         bool has_on_source_error,
                         BlockdevOnError on_source_error,
                         bool has_on_target_error,
                         BlockdevOnError on_target_error,
                         const char *filter_node_name,
                         bool has_copy_mode, MirrorCopyMode copy_mode,
                         bool has_auto_finalize, bool auto_finalize,
                         bool has_auto_dismiss, bool auto_dismiss,
                         Error **errp)
{
    BlockDriverState *bs;
    BlockDriverState *target_bs;
    AioContext *aio_context;
    BlockMirrorBackingMode backing_mode = MIRROR_LEAVE_BACKING_CHAIN;
    bool zero_target;
    int ret;

    bs = qmp_get_root_bs(device, errp);
    if (!bs) {
        return;
    }

    target_bs = bdrv_lookup_bs(target, target, errp);
    if (!target_bs) {
        return;
    }

    zero_target = (sync == MIRROR_SYNC_MODE_FULL);

    aio_context = bdrv_get_aio_context(bs);

    ret = bdrv_try_change_aio_context(target_bs, aio_context, NULL, errp);
    if (ret < 0) {
        return;
    }

    blockdev_mirror_common(job_id, bs, target_bs,
                           replaces, sync, backing_mode,
                           zero_target, has_speed, speed,
                           has_granularity, granularity,
                           has_buf_size, buf_size,
                           has_on_source_error, on_source_error,
                           has_on_target_error, on_target_error,
                           true, true, filter_node_name,
                           has_copy_mode, copy_mode,
                           has_auto_finalize, auto_finalize,
                           has_auto_dismiss, auto_dismiss,
                           errp);
}

/*
 * Get a block job using its ID. Called with job_mutex held.
 */
static BlockJob *find_block_job_locked(const char *id, Error **errp)
{
    BlockJob *job;

    assert(id != NULL);

    job = block_job_get_locked(id);

    if (!job) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
                  "Block job '%s' not found", id);
        return NULL;
    }

    return job;
}

void qmp_block_job_set_speed(const char *device, int64_t speed, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(device, errp);

    if (!job) {
        return;
    }

    block_job_set_speed_locked(job, speed, errp);
}

void qmp_block_job_cancel(const char *device,
                          bool has_force, bool force, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(device, errp);

    if (!job) {
        return;
    }

    if (!has_force) {
        force = false;
    }

    if (job_user_paused_locked(&job->job) && !force) {
        error_setg(errp, "The block job for device '%s' is currently paused",
                   device);
        return;
    }

    trace_qmp_block_job_cancel(job);
    job_user_cancel_locked(&job->job, force, errp);
}

void qmp_block_job_pause(const char *device, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(device, errp);

    if (!job) {
        return;
    }

    trace_qmp_block_job_pause(job);
    job_user_pause_locked(&job->job, errp);
}

void qmp_block_job_resume(const char *device, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(device, errp);

    if (!job) {
        return;
    }

    trace_qmp_block_job_resume(job);
    job_user_resume_locked(&job->job, errp);
}

void qmp_block_job_complete(const char *device, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(device, errp);

    if (!job) {
        return;
    }

    trace_qmp_block_job_complete(job);
    job_complete_locked(&job->job, errp);
}

void qmp_block_job_finalize(const char *id, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_block_job_finalize(job);
    job_ref_locked(&job->job);
    job_finalize_locked(&job->job, errp);

    job_unref_locked(&job->job);
}

void qmp_block_job_dismiss(const char *id, Error **errp)
{
    BlockJob *bjob;
    Job *job;

    JOB_LOCK_GUARD();
    bjob = find_block_job_locked(id, errp);

    if (!bjob) {
        return;
    }

    trace_qmp_block_job_dismiss(bjob);
    job = &bjob->job;
    job_dismiss_locked(&job, errp);
}

void qmp_block_job_change(BlockJobChangeOptions *opts, Error **errp)
{
    BlockJob *job;

    JOB_LOCK_GUARD();
    job = find_block_job_locked(opts->id, errp);

    if (!job) {
        return;
    }

    block_job_change_locked(job, opts, errp);
}

void qmp_change_backing_file(const char *device,
                             const char *image_node_name,
                             const char *backing_file,
                             Error **errp)
{
    BlockDriverState *bs = NULL;
    BlockDriverState *image_bs = NULL;
    Error *local_err = NULL;
    bool ro;
    int ret;

    bs = qmp_get_root_bs(device, errp);
    if (!bs) {
        return;
    }

    bdrv_graph_rdlock_main_loop();

    image_bs = bdrv_lookup_bs(NULL, image_node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_rdlock;
    }

    if (!image_bs) {
        error_setg(errp, "image file not found");
        goto out_rdlock;
    }

    if (bdrv_find_base(image_bs) == image_bs) {
        error_setg(errp, "not allowing backing file change on an image "
                         "without a backing file");
        goto out_rdlock;
    }

    /* even though we are not necessarily operating on bs, we need it to
     * determine if block ops are currently prohibited on the chain */
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_CHANGE, errp)) {
        goto out_rdlock;
    }

    /* final sanity check */
    if (!bdrv_chain_contains(bs, image_bs)) {
        error_setg(errp, "'%s' and image file are not in the same chain",
                   device);
        goto out_rdlock;
    }
    bdrv_graph_rdunlock_main_loop();

    /* if not r/w, reopen to make r/w */
    ro = bdrv_is_read_only(image_bs);

    if (ro) {
        if (bdrv_reopen_set_read_only(image_bs, false, errp) != 0) {
            return;
        }
    }

    ret = bdrv_change_backing_file(image_bs, backing_file,
                                   image_bs->drv ? image_bs->drv->format_name : "",
                                   false);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not change backing file to '%s'",
                         backing_file);
        /* don't exit here, so we can try to restore open flags if
         * appropriate */
    }

    if (ro) {
        bdrv_reopen_set_read_only(image_bs, true, errp);
    }
    return;

out_rdlock:
    bdrv_graph_rdunlock_main_loop();
}

void qmp_blockdev_add(BlockdevOptions *options, Error **errp)
{
    BlockDriverState *bs;
    QObject *obj;
    Visitor *v = qobject_output_visitor_new(&obj);
    QDict *qdict;

    visit_type_BlockdevOptions(v, NULL, &options, &error_abort);
    visit_complete(v, &obj);
    qdict = qobject_to(QDict, obj);

    qdict_flatten(qdict);

    if (!qdict_get_try_str(qdict, "node-name")) {
        error_setg(errp, "'node-name' must be specified for the root node");
        goto fail;
    }

    bs = bds_tree_init(qdict, errp);
    if (!bs) {
        goto fail;
    }

    bdrv_set_monitor_owned(bs);

fail:
    visit_free(v);
}

void qmp_blockdev_reopen(BlockdevOptionsList *reopen_list, Error **errp)
{
    BlockReopenQueue *queue = NULL;

    /* Add each one of the BDS that we want to reopen to the queue */
    for (; reopen_list != NULL; reopen_list = reopen_list->next) {
        BlockdevOptions *options = reopen_list->value;
        BlockDriverState *bs;
        QObject *obj;
        Visitor *v;
        QDict *qdict;

        /* Check for the selected node name */
        if (!options->node_name) {
            error_setg(errp, "node-name not specified");
            goto fail;
        }

        bs = bdrv_find_node(options->node_name);
        if (!bs) {
            error_setg(errp, "Failed to find node with node-name='%s'",
                       options->node_name);
            goto fail;
        }

        /* Put all options in a QDict and flatten it */
        v = qobject_output_visitor_new(&obj);
        visit_type_BlockdevOptions(v, NULL, &options, &error_abort);
        visit_complete(v, &obj);
        visit_free(v);

        qdict = qobject_to(QDict, obj);

        qdict_flatten(qdict);

        queue = bdrv_reopen_queue(queue, bs, qdict, false);
    }

    /* Perform the reopen operation */
    bdrv_reopen_multiple(queue, errp);
    queue = NULL;

fail:
    bdrv_reopen_queue_free(queue);
}

void qmp_blockdev_del(const char *node_name, Error **errp)
{
    BlockDriverState *bs;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Failed to find node with node-name='%s'", node_name);
        return;
    }
    if (bdrv_has_blk(bs)) {
        error_setg(errp, "Node %s is in use", node_name);
        return;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_DRIVE_DEL, errp)) {
        return;
    }

    if (!QTAILQ_IN_USE(bs, monitor_list)) {
        error_setg(errp, "Node %s is not owned by the monitor",
                   bs->node_name);
        return;
    }

    if (bs->refcnt > 1) {
        error_setg(errp, "Block device %s is in use",
                   bdrv_get_device_or_node_name(bs));
        return;
    }

    QTAILQ_REMOVE(&monitor_bdrv_states, bs, monitor_list);
    bdrv_unref(bs);
}

static BdrvChild * GRAPH_RDLOCK
bdrv_find_child(BlockDriverState *parent_bs, const char *child_name)
{
    BdrvChild *child;

    QLIST_FOREACH(child, &parent_bs->children, next) {
        if (strcmp(child->name, child_name) == 0) {
            return child;
        }
    }

    return NULL;
}

void qmp_x_blockdev_change(const char *parent, const char *child,
                           const char *node, Error **errp)
{
    BlockDriverState *parent_bs, *new_bs = NULL;
    BdrvChild *p_child;

    bdrv_graph_wrlock();

    parent_bs = bdrv_lookup_bs(parent, parent, errp);
    if (!parent_bs) {
        goto out;
    }

    if (!child == !node) {
        if (child) {
            error_setg(errp, "The parameters child and node are in conflict");
        } else {
            error_setg(errp, "Either child or node must be specified");
        }
        goto out;
    }

    if (child) {
        p_child = bdrv_find_child(parent_bs, child);
        if (!p_child) {
            error_setg(errp, "Node '%s' does not have child '%s'",
                       parent, child);
            goto out;
        }
        bdrv_del_child(parent_bs, p_child, errp);
    }

    if (node) {
        new_bs = bdrv_find_node(node);
        if (!new_bs) {
            error_setg(errp, "Node '%s' not found", node);
            goto out;
        }
        bdrv_add_child(parent_bs, new_bs, errp);
    }

out:
    bdrv_graph_wrunlock();
}

BlockJobInfoList *qmp_query_block_jobs(Error **errp)
{
    BlockJobInfoList *head = NULL, **tail = &head;
    BlockJob *job;

    JOB_LOCK_GUARD();

    for (job = block_job_next_locked(NULL); job;
         job = block_job_next_locked(job)) {
        BlockJobInfo *value;

        if (block_job_is_internal(job)) {
            continue;
        }
        value = block_job_query_locked(job, errp);
        if (!value) {
            qapi_free_BlockJobInfoList(head);
            return NULL;
        }
        QAPI_LIST_APPEND(tail, value);
    }

    return head;
}

void qmp_x_blockdev_set_iothread(const char *node_name, StrOrNull *iothread,
                                 bool has_force, bool force, Error **errp)
{
    AioContext *new_context;
    BlockDriverState *bs;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Failed to find node with node-name='%s'", node_name);
        return;
    }

    /* Protects against accidents. */
    if (!(has_force && force) && bdrv_has_blk(bs)) {
        error_setg(errp, "Node %s is associated with a BlockBackend and could "
                         "be in use (use force=true to override this check)",
                         node_name);
        return;
    }

    if (iothread->type == QTYPE_QSTRING) {
        IOThread *obj = iothread_by_id(iothread->u.s);
        if (!obj) {
            error_setg(errp, "Cannot find iothread %s", iothread->u.s);
            return;
        }

        new_context = iothread_get_aio_context(obj);
    } else {
        new_context = qemu_get_aio_context();
    }

    bdrv_try_change_aio_context(bs, new_context, NULL, errp);
}

QemuOptsList qemu_common_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_common_drive_opts.head),
    .desc = {
        {
            .name = "snapshot",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable snapshot mode",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native, io_uring)",
        },{
            .name = BDRV_OPT_CACHE_WB,
            .type = QEMU_OPT_BOOL,
            .help = "Enable writeback mode",
        },{
            .name = "format",
            .type = QEMU_OPT_STRING,
            .help = "disk format (raw, qcow2, ...)",
        },{
            .name = "rerror",
            .type = QEMU_OPT_STRING,
            .help = "read error action",
        },{
            .name = "werror",
            .type = QEMU_OPT_STRING,
            .help = "write error action",
        },{
            .name = BDRV_OPT_READ_ONLY,
            .type = QEMU_OPT_BOOL,
            .help = "open drive file as read-only",
        },

        THROTTLE_OPTS,

        {
            .name = "throttling.group",
            .type = QEMU_OPT_STRING,
            .help = "name of the block throttling group",
        },{
            .name = "copy-on-read",
            .type = QEMU_OPT_BOOL,
            .help = "copy read data from backing file into image file",
        },{
            .name = "detect-zeroes",
            .type = QEMU_OPT_STRING,
            .help = "try to optimize zero writes (off, on, unmap)",
        },{
            .name = "stats-account-invalid",
            .type = QEMU_OPT_BOOL,
            .help = "whether to account for invalid I/O operations "
                    "in the statistics",
        },{
            .name = "stats-account-failed",
            .type = QEMU_OPT_BOOL,
            .help = "whether to account for failed I/O operations "
                    "in the statistics",
        },
        { /* end of list */ }
    },
};

QemuOptsList qemu_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_drive_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};
