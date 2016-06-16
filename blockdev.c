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
#include "block/throttle-groups.h"
#include "monitor/monitor.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/types.h"
#include "qapi-visit.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/util.h"
#include "sysemu/sysemu.h"
#include "block/block_int.h"
#include "qmp-commands.h"
#include "trace.h"
#include "sysemu/arch_init.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"

static QTAILQ_HEAD(, BlockDriverState) monitor_bdrv_states =
    QTAILQ_HEAD_INITIALIZER(monitor_bdrv_states);

static int do_open_tray(const char *device, bool force, Error **errp);

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
    BlockDriverState *bs = blk_bs(blk);
    AioContext *aio_context;

    if (!dinfo) {
        return;
    }

    if (bs) {
        aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);

        if (bs->job) {
            block_job_cancel(bs->job);
        }

        aio_context_release(aio_context);
    }

    dinfo->auto_del = 1;
}

void blockdev_auto_del(BlockBackend *blk)
{
    DriveInfo *dinfo = blk_legacy_dinfo(blk);

    if (dinfo && dinfo->auto_del) {
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

/**
 * Returns the current mapping of how many units per bus
 * a particular interface can support.
 *
 *  A positive integer indicates n units per bus.
 *  0 implies the mapping has not been established.
 * -1 indicates an invalid BlockInterfaceType was given.
 */
int drive_get_max_devs(BlockInterfaceType type)
{
    if (type >= IF_IDE && type < IF_COUNT) {
        return if_max_devs[type];
    }

    return -1;
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

QemuOpts *drive_def(const char *optstr)
{
    return qemu_opts_parse_noisily(qemu_find_opts("drive"), optstr, false);
}

QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr)
{
    QemuOpts *opts;

    opts = drive_def(optstr);
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

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo && dinfo->type == type
            && dinfo->bus == bus && dinfo->unit == unit) {
            return dinfo;
        }
    }

    return NULL;
}

bool drive_check_orphaned(void)
{
    BlockBackend *blk;
    DriveInfo *dinfo;
    bool rs = false;

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        /* If dinfo->bdrv->dev is NULL, it has no device attached. */
        /* Unless this is a default drive, this may be an oversight. */
        if (!blk_get_attached_dev(blk) && !dinfo->is_default &&
            dinfo->type != IF_NONE) {
            fprintf(stderr, "Warning: Orphaned drive without device: "
                    "id=%s,file=%s,if=%s,bus=%d,unit=%d\n",
                    blk_name(blk), blk_bs(blk) ? blk_bs(blk)->filename : "",
                    if_name[dinfo->type], dinfo->bus, dinfo->unit);
            rs = true;
        }
    }

    return rs;
}

DriveInfo *drive_get_by_index(BlockInterfaceType type, int index)
{
    return drive_get(type,
                     drive_index_to_bus_id(type, index),
                     drive_index_to_unit_id(type, index));
}

int drive_get_max_bus(BlockInterfaceType type)
{
    int max_bus;
    BlockBackend *blk;
    DriveInfo *dinfo;

    max_bus = -1;
    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        dinfo = blk_legacy_dinfo(blk);
        if (dinfo && dinfo->type == type && dinfo->bus > max_bus) {
            max_bus = dinfo->bus;
        }
    }
    return max_bus;
}

/* Get a block device.  This should only be used for single-drive devices
   (e.g. SD/Floppy/MTD).  Multi-disk devices (scsi/ide) should use the
   appropriate bus.  */
DriveInfo *drive_get_next(BlockInterfaceType type)
{
    static int next_block_unit[IF_COUNT];

    return drive_get(type, 0, next_block_unit[type]++);
}

static void bdrv_format_print(void *opaque, const char *name)
{
    error_printf(" %s", name);
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
            unsigned long long length;
            const char *str = qstring_get_str(qobject_to_qstring(entry->value));
            if (parse_uint_full(str, &length, 10) == 0 &&
                length > 0 && length <= UINT_MAX) {
                block_acct_add_interval(stats, (unsigned) length);
            } else {
                error_setg(errp, "Invalid interval length: %s", str);
                return false;
            }
            break;
        }

        case QTYPE_QINT: {
            int64_t length = qint_get_int(qobject_to_qint(entry->value));
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
    const char *discard;
    Error *local_error = NULL;
    const char *aio;

    if (bdrv_flags) {
        if (!qemu_opt_get_bool(opts, "read-only", false)) {
            *bdrv_flags |= BDRV_O_RDWR;
        }
        if (qemu_opt_get_bool(opts, "copy-on-read", false)) {
            *bdrv_flags |= BDRV_O_COPY_ON_READ;
        }

        if ((discard = qemu_opt_get(opts, "discard")) != NULL) {
            if (bdrv_parse_discard_flags(discard, bdrv_flags) != 0) {
                error_setg(errp, "Invalid discard option");
                return;
            }
        }

        if ((aio = qemu_opt_get(opts, "aio")) != NULL) {
            if (!strcmp(aio, "native")) {
                *bdrv_flags |= BDRV_O_NATIVE_AIO;
            } else if (!strcmp(aio, "threads")) {
                /* this is the default */
            } else {
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
            qapi_enum_parse(BlockdevDetectZeroesOptions_lookup,
                            qemu_opt_get(opts, "detect-zeroes"),
                            BLOCKDEV_DETECT_ZEROES_OPTIONS__MAX,
                            BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF,
                            &local_error);
        if (local_error) {
            error_propagate(errp, local_error);
            return;
        }

        if (bdrv_flags &&
            *detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP &&
            !(*bdrv_flags & BDRV_O_UNMAP))
        {
            error_setg(errp, "setting detect-zeroes to unmap is not allowed "
                             "without setting discard operation to unmap");
            return;
        }
    }
}

/* Takes the ownership of bs_opts */
static BlockBackend *blockdev_init(const char *file, QDict *bs_opts,
                                   Error **errp)
{
    const char *buf;
    int bdrv_flags = 0;
    int on_read_error, on_write_error;
    bool account_invalid, account_failed;
    bool writethrough;
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
    opts = qemu_opts_create(&qemu_common_drive_opts, id, 1, &error);
    if (error) {
        error_propagate(errp, error);
        goto err_no_opts;
    }

    qemu_opts_absorb_qdict(opts, bs_opts, &error);
    if (error) {
        error_propagate(errp, error);
        goto early_err;
    }

    if (id) {
        qdict_del(bs_opts, "id");
    }

    /* extract parameters */
    snapshot = qemu_opt_get_bool(opts, "snapshot", 0);

    account_invalid = qemu_opt_get_bool(opts, "stats-account-invalid", true);
    account_failed = qemu_opt_get_bool(opts, "stats-account-failed", true);

    writethrough = !qemu_opt_get_bool(opts, BDRV_OPT_CACHE_WB, true);

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
            error_printf("Supported formats:");
            bdrv_iterate_format(bdrv_format_print, NULL);
            error_printf("\n");
            goto early_err;
        }

        if (qdict_haskey(bs_opts, "driver")) {
            error_setg(errp, "Cannot specify both 'driver' and 'format'");
            goto early_err;
        }
        qdict_put(bs_opts, "driver", qstring_from_str(buf));
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

    /* init */
    if ((!file || !*file) && !qdict_size(bs_opts)) {
        BlockBackendRootState *blk_rs;

        blk = blk_new();
        blk_rs = blk_get_root_state(blk);
        blk_rs->open_flags    = bdrv_flags;
        blk_rs->read_only     = !(bdrv_flags & BDRV_O_RDWR);
        blk_rs->detect_zeroes = detect_zeroes;

        QDECREF(bs_opts);
    } else {
        if (file && !*file) {
            file = NULL;
        }

        /* bdrv_open() defaults to the values in bdrv_flags (for compatibility
         * with other callers) rather than what we want as the real defaults.
         * Apply the defaults here instead. */
        qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
        qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");
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

        if (bdrv_key_required(bs)) {
            autostart = 0;
        }

        block_acct_init(blk_get_stats(blk), account_invalid, account_failed);

        if (!parse_stats_intervals(blk_get_stats(blk), interval_list, errp)) {
            blk_unref(blk);
            blk = NULL;
            goto err_no_bs_opts;
        }
    }

    /* disk I/O throttling */
    if (throttle_enabled(&cfg)) {
        if (!throttling_group) {
            throttling_group = blk_name(blk);
        }
        blk_io_limits_enable(blk, throttling_group);
        blk_set_io_limits(blk, &cfg);
    }

    blk_set_enable_write_cache(blk, !writethrough);
    blk_set_on_error(blk, on_read_error, on_write_error);

    if (!monitor_add_blk(blk, qemu_opts_id(opts), errp)) {
        blk_unref(blk);
        blk = NULL;
        goto err_no_bs_opts;
    }

err_no_bs_opts:
    qemu_opts_del(opts);
    QDECREF(interval_dict);
    QDECREF(interval_list);
    return blk;

early_err:
    qemu_opts_del(opts);
    QDECREF(interval_dict);
    QDECREF(interval_list);
err_no_opts:
    QDECREF(bs_opts);
    return NULL;
}

static QemuOptsList qemu_root_bds_opts;

/* Takes the ownership of bs_opts */
static BlockDriverState *bds_tree_init(QDict *bs_opts, Error **errp)
{
    BlockDriverState *bs;
    QemuOpts *opts;
    Error *local_error = NULL;
    BlockdevDetectZeroesOptions detect_zeroes;
    int bdrv_flags = 0;

    opts = qemu_opts_create(&qemu_root_bds_opts, NULL, 1, errp);
    if (!opts) {
        goto fail;
    }

    qemu_opts_absorb_qdict(opts, bs_opts, &local_error);
    if (local_error) {
        error_propagate(errp, local_error);
        goto fail;
    }

    extract_common_blockdev_options(opts, &bdrv_flags, NULL, NULL,
                                    &detect_zeroes, &local_error);
    if (local_error) {
        error_propagate(errp, local_error);
        goto fail;
    }

    /* bdrv_open() defaults to the values in bdrv_flags (for compatibility
     * with other callers) rather than what we want as the real defaults.
     * Apply the defaults here instead. */
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(bs_opts, BDRV_OPT_CACHE_NO_FLUSH, "off");

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        bdrv_flags |= BDRV_O_INACTIVE;
    }

    bs = bdrv_open(NULL, NULL, bs_opts, bdrv_flags, errp);
    if (!bs) {
        goto fail_no_bs_opts;
    }

    bs->detect_zeroes = detect_zeroes;

fail_no_bs_opts:
    qemu_opts_del(opts);
    return bs;

fail:
    qemu_opts_del(opts);
    QDECREF(bs_opts);
    return NULL;
}

void blockdev_close_all_bdrv_states(void)
{
    BlockDriverState *bs, *next_bs;

    QTAILQ_FOREACH_SAFE(bs, &monitor_bdrv_states, monitor_list, next_bs) {
        AioContext *ctx = bdrv_get_aio_context(bs);

        aio_context_acquire(ctx);
        bdrv_unref(bs);
        aio_context_release(ctx);
    }
}

/* Iterates over the list of monitor-owned BlockDriverStates */
BlockDriverState *bdrv_next_monitor_owned(BlockDriverState *bs)
{
    return bs ? QTAILQ_NEXT(bs, monitor_list)
              : QTAILQ_FIRST(&monitor_bdrv_states);
}

static void qemu_opt_rename(QemuOpts *opts, const char *from, const char *to,
                            Error **errp)
{
    const char *value;

    value = qemu_opt_get(opts, from);
    if (value) {
        if (qemu_opt_find(opts, to)) {
            error_setg(errp, "'%s' and its alias '%s' can't be used at the "
                       "same time", to, from);
            return;
        }
    }

    /* rename all items in opts */
    while ((value = qemu_opt_get(opts, from))) {
        qemu_opt_set(opts, to, value, &error_abort);
        qemu_opt_unset(opts, from);
    }
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
            .name = "cyls",
            .type = QEMU_OPT_NUMBER,
            .help = "number of cylinders (ide disk geometry)",
        },{
            .name = "heads",
            .type = QEMU_OPT_NUMBER,
            .help = "number of heads (ide disk geometry)",
        },{
            .name = "secs",
            .type = QEMU_OPT_NUMBER,
            .help = "number of sectors (ide disk geometry)",
        },{
            .name = "trans",
            .type = QEMU_OPT_STRING,
            .help = "chs translation (auto, lba, none)",
        },{
            .name = "boot",
            .type = QEMU_OPT_BOOL,
            .help = "(deprecated, ignored)",
        },{
            .name = "addr",
            .type = QEMU_OPT_STRING,
            .help = "pci address (virtio only)",
        },{
            .name = "serial",
            .type = QEMU_OPT_STRING,
            .help = "disk serial number",
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "file name",
        },

        /* Options that are passed on, but have special semantics with -drive */
        {
            .name = "read-only",
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

DriveInfo *drive_new(QemuOpts *all_opts, BlockInterfaceType block_default_type)
{
    const char *value;
    BlockBackend *blk;
    DriveInfo *dinfo = NULL;
    QDict *bs_opts;
    QemuOpts *legacy_opts;
    DriveMediaType media = MEDIA_DISK;
    BlockInterfaceType type;
    int cyls, heads, secs, translation;
    int max_devs, bus_id, unit_id, index;
    const char *devaddr;
    const char *werror, *rerror;
    bool read_only = false;
    bool copy_on_read;
    const char *serial;
    const char *filename;
    Error *local_err = NULL;
    int i;

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

        { "readonly",       "read-only" },
    };

    for (i = 0; i < ARRAY_SIZE(opt_renames); i++) {
        qemu_opt_rename(all_opts, opt_renames[i].from, opt_renames[i].to,
                        &local_err);
        if (local_err) {
            error_report_err(local_err);
            return NULL;
        }
    }

    value = qemu_opt_get(all_opts, "cache");
    if (value) {
        int flags = 0;
        bool writethrough;

        if (bdrv_parse_cache_mode(value, &flags, &writethrough) != 0) {
            error_report("invalid cache option");
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
    qemu_opts_absorb_qdict(legacy_opts, bs_opts, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto fail;
    }

    /* Deprecated option boot=[on|off] */
    if (qemu_opt_get(legacy_opts, "boot") != NULL) {
        fprintf(stderr, "qemu-kvm: boot=on|off is deprecated and will be "
                "ignored. Future versions will reject this parameter. Please "
                "update your scripts.\n");
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
            error_report("'%s' invalid media", value);
            goto fail;
        }
    }

    /* copy-on-read is disabled with a warning for read-only devices */
    read_only |= qemu_opt_get_bool(legacy_opts, "read-only", false);
    copy_on_read = qemu_opt_get_bool(legacy_opts, "copy-on-read", false);

    if (read_only && copy_on_read) {
        error_report("warning: disabling copy-on-read on read-only drive");
        copy_on_read = false;
    }

    qdict_put(bs_opts, "read-only",
              qstring_from_str(read_only ? "on" : "off"));
    qdict_put(bs_opts, "copy-on-read",
              qstring_from_str(copy_on_read ? "on" :"off"));

    /* Controller type */
    value = qemu_opt_get(legacy_opts, "if");
    if (value) {
        for (type = 0;
             type < IF_COUNT && strcmp(value, if_name[type]);
             type++) {
        }
        if (type == IF_COUNT) {
            error_report("unsupported bus type '%s'", value);
            goto fail;
        }
    } else {
        type = block_default_type;
    }

    /* Geometry */
    cyls  = qemu_opt_get_number(legacy_opts, "cyls", 0);
    heads = qemu_opt_get_number(legacy_opts, "heads", 0);
    secs  = qemu_opt_get_number(legacy_opts, "secs", 0);

    if (cyls || heads || secs) {
        if (cyls < 1) {
            error_report("invalid physical cyls number");
            goto fail;
        }
        if (heads < 1) {
            error_report("invalid physical heads number");
            goto fail;
        }
        if (secs < 1) {
            error_report("invalid physical secs number");
            goto fail;
        }
    }

    translation = BIOS_ATA_TRANSLATION_AUTO;
    value = qemu_opt_get(legacy_opts, "trans");
    if (value != NULL) {
        if (!cyls) {
            error_report("'%s' trans must be used with cyls, heads and secs",
                         value);
            goto fail;
        }
        if (!strcmp(value, "none")) {
            translation = BIOS_ATA_TRANSLATION_NONE;
        } else if (!strcmp(value, "lba")) {
            translation = BIOS_ATA_TRANSLATION_LBA;
        } else if (!strcmp(value, "large")) {
            translation = BIOS_ATA_TRANSLATION_LARGE;
        } else if (!strcmp(value, "rechs")) {
            translation = BIOS_ATA_TRANSLATION_RECHS;
        } else if (!strcmp(value, "auto")) {
            translation = BIOS_ATA_TRANSLATION_AUTO;
        } else {
            error_report("'%s' invalid translation type", value);
            goto fail;
        }
    }

    if (media == MEDIA_CDROM) {
        if (cyls || secs || heads) {
            error_report("CHS can't be set with media=cdrom");
            goto fail;
        }
    }

    /* Device address specified by bus/unit or index.
     * If none was specified, try to find the first free one. */
    bus_id  = qemu_opt_get_number(legacy_opts, "bus", 0);
    unit_id = qemu_opt_get_number(legacy_opts, "unit", -1);
    index   = qemu_opt_get_number(legacy_opts, "index", -1);

    max_devs = if_max_devs[type];

    if (index != -1) {
        if (bus_id != 0 || unit_id != -1) {
            error_report("index cannot be used with bus and unit");
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
        error_report("unit %d too big (max is %d)", unit_id, max_devs - 1);
        goto fail;
    }

    if (drive_get(type, bus_id, unit_id) != NULL) {
        error_report("drive with bus=%d, unit=%d (index=%d) exists",
                     bus_id, unit_id, index);
        goto fail;
    }

    /* Serial number */
    serial = qemu_opt_get(legacy_opts, "serial");

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
        qdict_put(bs_opts, "id", qstring_from_str(new_id));
        g_free(new_id);
    }

    /* Add virtio block device */
    devaddr = qemu_opt_get(legacy_opts, "addr");
    if (devaddr && type != IF_VIRTIO) {
        error_report("addr is not supported by this bus type");
        goto fail;
    }

    if (type == IF_VIRTIO) {
        QemuOpts *devopts;
        devopts = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                   &error_abort);
        if (arch_type == QEMU_ARCH_S390X) {
            qemu_opt_set(devopts, "driver", "virtio-blk-ccw", &error_abort);
        } else {
            qemu_opt_set(devopts, "driver", "virtio-blk-pci", &error_abort);
        }
        qemu_opt_set(devopts, "drive", qdict_get_str(bs_opts, "id"),
                     &error_abort);
        if (devaddr) {
            qemu_opt_set(devopts, "addr", devaddr, &error_abort);
        }
    }

    filename = qemu_opt_get(legacy_opts, "file");

    /* Check werror/rerror compatibility with if=... */
    werror = qemu_opt_get(legacy_opts, "werror");
    if (werror != NULL) {
        if (type != IF_IDE && type != IF_SCSI && type != IF_VIRTIO &&
            type != IF_NONE) {
            error_report("werror is not supported by this bus type");
            goto fail;
        }
        qdict_put(bs_opts, "werror", qstring_from_str(werror));
    }

    rerror = qemu_opt_get(legacy_opts, "rerror");
    if (rerror != NULL) {
        if (type != IF_IDE && type != IF_VIRTIO && type != IF_SCSI &&
            type != IF_NONE) {
            error_report("rerror is not supported by this bus type");
            goto fail;
        }
        qdict_put(bs_opts, "rerror", qstring_from_str(rerror));
    }

    /* Actual block device init: Functionality shared with blockdev-add */
    blk = blockdev_init(filename, bs_opts, &local_err);
    bs_opts = NULL;
    if (!blk) {
        if (local_err) {
            error_report_err(local_err);
        }
        goto fail;
    } else {
        assert(!local_err);
    }

    /* Create legacy DriveInfo */
    dinfo = g_malloc0(sizeof(*dinfo));
    dinfo->opts = all_opts;

    dinfo->cyls = cyls;
    dinfo->heads = heads;
    dinfo->secs = secs;
    dinfo->trans = translation;

    dinfo->type = type;
    dinfo->bus = bus_id;
    dinfo->unit = unit_id;
    dinfo->devaddr = devaddr;
    dinfo->serial = g_strdup(serial);

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
    QDECREF(bs_opts);
    return dinfo;
}

void hmp_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockBackend *blk;
    int ret;

    if (!strcmp(device, "all")) {
        ret = blk_commit_all();
    } else {
        BlockDriverState *bs;
        AioContext *aio_context;

        blk = blk_by_name(device);
        if (!blk) {
            monitor_printf(mon, "Device '%s' not found\n", device);
            return;
        }
        if (!blk_is_available(blk)) {
            monitor_printf(mon, "Device '%s' has no medium\n", device);
            return;
        }

        bs = blk_bs(blk);
        aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);

        ret = bdrv_commit(bs);

        aio_context_release(aio_context);
    }
    if (ret < 0) {
        monitor_printf(mon, "'commit' error for '%s': %s\n", device,
                       strerror(-ret));
    }
}

static void blockdev_do_action(TransactionAction *action, Error **errp)
{
    TransactionActionList list;

    list.value = action;
    list.next = NULL;
    qmp_transaction(&list, false, NULL, errp);
}

void qmp_blockdev_snapshot_sync(bool has_device, const char *device,
                                bool has_node_name, const char *node_name,
                                const char *snapshot_file,
                                bool has_snapshot_node_name,
                                const char *snapshot_node_name,
                                bool has_format, const char *format,
                                bool has_mode, NewImageMode mode, Error **errp)
{
    BlockdevSnapshotSync snapshot = {
        .has_device = has_device,
        .device = (char *) device,
        .has_node_name = has_node_name,
        .node_name = (char *) node_name,
        .snapshot_file = (char *) snapshot_file,
        .has_snapshot_node_name = has_snapshot_node_name,
        .snapshot_node_name = (char *) snapshot_node_name,
        .has_format = has_format,
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
                                                         bool has_id,
                                                         const char *id,
                                                         bool has_name,
                                                         const char *name,
                                                         Error **errp)
{
    BlockDriverState *bs;
    BlockBackend *blk;
    AioContext *aio_context;
    QEMUSnapshotInfo sn;
    Error *local_err = NULL;
    SnapshotInfo *info = NULL;
    int ret;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return NULL;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!has_id) {
        id = NULL;
    }

    if (!has_name) {
        name = NULL;
    }

    if (!id && !name) {
        error_setg(errp, "Name or id must be provided");
        goto out_aio_context;
    }

    if (!blk_is_available(blk)) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out_aio_context;
    }
    bs = blk_bs(blk);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_INTERNAL_SNAPSHOT_DELETE, errp)) {
        goto out_aio_context;
    }

    ret = bdrv_snapshot_find_by_id_and_name(bs, id, name, &sn, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_aio_context;
    }
    if (!ret) {
        error_setg(errp,
                   "Snapshot with id '%s' and name '%s' does not exist on "
                   "device '%s'",
                   STR_OR_NULL(id), STR_OR_NULL(name), device);
        goto out_aio_context;
    }

    bdrv_snapshot_delete(bs, id, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_aio_context;
    }

    aio_context_release(aio_context);

    info = g_new0(SnapshotInfo, 1);
    info->id = g_strdup(sn.id_str);
    info->name = g_strdup(sn.name);
    info->date_nsec = sn.date_nsec;
    info->date_sec = sn.date_sec;
    info->vm_state_size = sn.vm_state_size;
    info->vm_clock_nsec = sn.vm_clock_nsec % 1000000000;
    info->vm_clock_sec = sn.vm_clock_nsec / 1000000000;

    return info;

out_aio_context:
    aio_context_release(aio_context);
    return NULL;
}

/**
 * block_dirty_bitmap_lookup:
 * Return a dirty bitmap (if present), after validating
 * the node reference and bitmap names.
 *
 * @node: The name of the BDS node to search for bitmaps
 * @name: The name of the bitmap to search for
 * @pbs: Output pointer for BDS lookup, if desired. Can be NULL.
 * @paio: Output pointer for aio_context acquisition, if desired. Can be NULL.
 * @errp: Output pointer for error information. Can be NULL.
 *
 * @return: A bitmap object on success, or NULL on failure.
 */
static BdrvDirtyBitmap *block_dirty_bitmap_lookup(const char *node,
                                                  const char *name,
                                                  BlockDriverState **pbs,
                                                  AioContext **paio,
                                                  Error **errp)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    AioContext *aio_context;

    if (!node) {
        error_setg(errp, "Node cannot be NULL");
        return NULL;
    }
    if (!name) {
        error_setg(errp, "Bitmap name cannot be NULL");
        return NULL;
    }
    bs = bdrv_lookup_bs(node, node, NULL);
    if (!bs) {
        error_setg(errp, "Node '%s' not found", node);
        return NULL;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    bitmap = bdrv_find_dirty_bitmap(bs, name);
    if (!bitmap) {
        error_setg(errp, "Dirty bitmap '%s' not found", name);
        goto fail;
    }

    if (pbs) {
        *pbs = bs;
    }
    if (paio) {
        *paio = aio_context;
    } else {
        aio_context_release(aio_context);
    }

    return bitmap;

 fail:
    aio_context_release(aio_context);
    return NULL;
}

/* New and old BlockDriverState structs for atomic group operations */

typedef struct BlkActionState BlkActionState;

/**
 * BlkActionOps:
 * Table of operations that define an Action.
 *
 * @instance_size: Size of state struct, in bytes.
 * @prepare: Prepare the work, must NOT be NULL.
 * @commit: Commit the changes, can be NULL.
 * @abort: Abort the changes on fail, can be NULL.
 * @clean: Clean up resources after all transaction actions have called
 *         commit() or abort(). Can be NULL.
 *
 * Only prepare() may fail. In a single transaction, only one of commit() or
 * abort() will be called. clean() will always be called if it is present.
 */
typedef struct BlkActionOps {
    size_t instance_size;
    void (*prepare)(BlkActionState *common, Error **errp);
    void (*commit)(BlkActionState *common);
    void (*abort)(BlkActionState *common);
    void (*clean)(BlkActionState *common);
} BlkActionOps;

/**
 * BlkActionState:
 * Describes one Action's state within a Transaction.
 *
 * @action: QAPI-defined enum identifying which Action to perform.
 * @ops: Table of ActionOps this Action can perform.
 * @block_job_txn: Transaction which this action belongs to.
 * @entry: List membership for all Actions in this Transaction.
 *
 * This structure must be arranged as first member in a subclassed type,
 * assuming that the compiler will also arrange it to the same offsets as the
 * base class.
 */
struct BlkActionState {
    TransactionAction *action;
    const BlkActionOps *ops;
    BlockJobTxn *block_job_txn;
    TransactionProperties *txn_props;
    QSIMPLEQ_ENTRY(BlkActionState) entry;
};

/* internal snapshot private data */
typedef struct InternalSnapshotState {
    BlkActionState common;
    BlockDriverState *bs;
    AioContext *aio_context;
    QEMUSnapshotInfo sn;
    bool created;
} InternalSnapshotState;


static int action_check_completion_mode(BlkActionState *s, Error **errp)
{
    if (s->txn_props->completion_mode != ACTION_COMPLETION_MODE_INDIVIDUAL) {
        error_setg(errp,
                   "Action '%s' does not support Transaction property "
                   "completion-mode = %s",
                   TransactionActionKind_lookup[s->action->type],
                   ActionCompletionMode_lookup[s->txn_props->completion_mode]);
        return -1;
    }
    return 0;
}

static void internal_snapshot_prepare(BlkActionState *common,
                                      Error **errp)
{
    Error *local_err = NULL;
    const char *device;
    const char *name;
    BlockBackend *blk;
    BlockDriverState *bs;
    QEMUSnapshotInfo old_sn, *sn;
    bool ret;
    qemu_timeval tv;
    BlockdevSnapshotInternal *internal;
    InternalSnapshotState *state;
    int ret1;

    g_assert(common->action->type ==
             TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_INTERNAL_SYNC);
    internal = common->action->u.blockdev_snapshot_internal_sync.data;
    state = DO_UPCAST(InternalSnapshotState, common, common);

    /* 1. parse input */
    device = internal->device;
    name = internal->name;

    /* 2. check for validation */
    if (action_check_completion_mode(common, errp) < 0) {
        return;
    }

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    /* AioContext is released in .clean() */
    state->aio_context = blk_get_aio_context(blk);
    aio_context_acquire(state->aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        return;
    }
    bs = blk_bs(blk);

    state->bs = bs;
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
    qemu_gettimeofday(&tv);
    sn->date_sec = tv.tv_sec;
    sn->date_nsec = tv.tv_usec * 1000;
    sn->vm_clock_nsec = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

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

static void internal_snapshot_abort(BlkActionState *common)
{
    InternalSnapshotState *state =
                             DO_UPCAST(InternalSnapshotState, common, common);
    BlockDriverState *bs = state->bs;
    QEMUSnapshotInfo *sn = &state->sn;
    Error *local_error = NULL;

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

static void internal_snapshot_clean(BlkActionState *common)
{
    InternalSnapshotState *state = DO_UPCAST(InternalSnapshotState,
                                             common, common);

    if (state->aio_context) {
        if (state->bs) {
            bdrv_drained_end(state->bs);
        }
        aio_context_release(state->aio_context);
    }
}

/* external snapshot private data */
typedef struct ExternalSnapshotState {
    BlkActionState common;
    BlockDriverState *old_bs;
    BlockDriverState *new_bs;
    AioContext *aio_context;
} ExternalSnapshotState;

static void external_snapshot_prepare(BlkActionState *common,
                                      Error **errp)
{
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
    ExternalSnapshotState *state =
                             DO_UPCAST(ExternalSnapshotState, common, common);
    TransactionAction *action = common->action;

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
            device = s->has_device ? s->device : NULL;
            node_name = s->has_node_name ? s->node_name : NULL;
            new_image_file = s->snapshot_file;
            snapshot_ref = NULL;
        }
        break;
    default:
        g_assert_not_reached();
    }

    /* start processing */
    if (action_check_completion_mode(common, errp) < 0) {
        return;
    }

    state->old_bs = bdrv_lookup_bs(device, node_name, errp);
    if (!state->old_bs) {
        return;
    }

    /* Acquire AioContext now so any threads operating on old_bs stop */
    state->aio_context = bdrv_get_aio_context(state->old_bs);
    aio_context_acquire(state->aio_context);
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

    if (!bdrv_is_first_non_filter(state->old_bs)) {
        error_setg(errp, QERR_FEATURE_DISABLED, "snapshot");
        return;
    }

    if (action->type == TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC) {
        BlockdevSnapshotSync *s = action->u.blockdev_snapshot_sync.data;
        const char *format = s->has_format ? s->format : "qcow2";
        enum NewImageMode mode;
        const char *snapshot_node_name =
            s->has_snapshot_node_name ? s->snapshot_node_name : NULL;

        if (node_name && !snapshot_node_name) {
            error_setg(errp, "New snapshot node name missing");
            return;
        }

        if (snapshot_node_name &&
            bdrv_lookup_bs(snapshot_node_name, snapshot_node_name, NULL)) {
            error_setg(errp, "New snapshot node name already in use");
            return;
        }

        flags = state->old_bs->open_flags;
        flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_COPY_ON_READ);

        /* create new image w/backing file */
        mode = s->has_mode ? s->mode : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
        if (mode != NEW_IMAGE_MODE_EXISTING) {
            int64_t size = bdrv_getlength(state->old_bs);
            if (size < 0) {
                error_setg_errno(errp, -size, "bdrv_getlength failed");
                return;
            }
            bdrv_img_create(new_image_file, format,
                            state->old_bs->filename,
                            state->old_bs->drv->format_name,
                            NULL, size, flags, &local_err, false);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
        }

        options = qdict_new();
        if (s->has_snapshot_node_name) {
            qdict_put(options, "node-name",
                      qstring_from_str(snapshot_node_name));
        }
        qdict_put(options, "driver", qstring_from_str(format));

        flags |= BDRV_O_NO_BACKING;
    }

    state->new_bs = bdrv_open(new_image_file, snapshot_ref, options, flags,
                              errp);
    /* We will manually add the backing_hd field to the bs later */
    if (!state->new_bs) {
        return;
    }

    if (bdrv_has_blk(state->new_bs)) {
        error_setg(errp, "The snapshot is already in use by %s",
                   bdrv_get_parent_name(state->new_bs));
        return;
    }

    if (bdrv_op_is_blocked(state->new_bs, BLOCK_OP_TYPE_EXTERNAL_SNAPSHOT,
                           errp)) {
        return;
    }

    if (state->new_bs->backing != NULL) {
        error_setg(errp, "The snapshot already has a backing image");
        return;
    }

    if (!state->new_bs->drv->supports_backing) {
        error_setg(errp, "The snapshot does not support backing images");
    }
}

static void external_snapshot_commit(BlkActionState *common)
{
    ExternalSnapshotState *state =
                             DO_UPCAST(ExternalSnapshotState, common, common);

    bdrv_set_aio_context(state->new_bs, state->aio_context);

    /* This removes our old bs and adds the new bs */
    bdrv_append(state->new_bs, state->old_bs);
    /* We don't need (or want) to use the transactional
     * bdrv_reopen_multiple() across all the entries at once, because we
     * don't want to abort all of them if one of them fails the reopen */
    if (!state->old_bs->copy_on_read) {
        bdrv_reopen(state->old_bs, state->old_bs->open_flags & ~BDRV_O_RDWR,
                    NULL);
    }
}

static void external_snapshot_abort(BlkActionState *common)
{
    ExternalSnapshotState *state =
                             DO_UPCAST(ExternalSnapshotState, common, common);
    if (state->new_bs) {
        bdrv_unref(state->new_bs);
    }
}

static void external_snapshot_clean(BlkActionState *common)
{
    ExternalSnapshotState *state =
                             DO_UPCAST(ExternalSnapshotState, common, common);
    if (state->aio_context) {
        bdrv_drained_end(state->old_bs);
        aio_context_release(state->aio_context);
    }
}

typedef struct DriveBackupState {
    BlkActionState common;
    BlockDriverState *bs;
    AioContext *aio_context;
    BlockJob *job;
} DriveBackupState;

static void do_drive_backup(const char *device, const char *target,
                            bool has_format, const char *format,
                            enum MirrorSyncMode sync,
                            bool has_mode, enum NewImageMode mode,
                            bool has_speed, int64_t speed,
                            bool has_bitmap, const char *bitmap,
                            bool has_on_source_error,
                            BlockdevOnError on_source_error,
                            bool has_on_target_error,
                            BlockdevOnError on_target_error,
                            BlockJobTxn *txn, Error **errp);

static void drive_backup_prepare(BlkActionState *common, Error **errp)
{
    DriveBackupState *state = DO_UPCAST(DriveBackupState, common, common);
    BlockBackend *blk;
    DriveBackup *backup;
    Error *local_err = NULL;

    assert(common->action->type == TRANSACTION_ACTION_KIND_DRIVE_BACKUP);
    backup = common->action->u.drive_backup.data;

    blk = blk_by_name(backup->device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", backup->device);
        return;
    }

    if (!blk_is_available(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, backup->device);
        return;
    }

    /* AioContext is released in .clean() */
    state->aio_context = blk_get_aio_context(blk);
    aio_context_acquire(state->aio_context);
    bdrv_drained_begin(blk_bs(blk));
    state->bs = blk_bs(blk);

    do_drive_backup(backup->device, backup->target,
                    backup->has_format, backup->format,
                    backup->sync,
                    backup->has_mode, backup->mode,
                    backup->has_speed, backup->speed,
                    backup->has_bitmap, backup->bitmap,
                    backup->has_on_source_error, backup->on_source_error,
                    backup->has_on_target_error, backup->on_target_error,
                    common->block_job_txn, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    state->job = state->bs->job;
}

static void drive_backup_abort(BlkActionState *common)
{
    DriveBackupState *state = DO_UPCAST(DriveBackupState, common, common);
    BlockDriverState *bs = state->bs;

    /* Only cancel if it's the job we started */
    if (bs && bs->job && bs->job == state->job) {
        block_job_cancel_sync(bs->job);
    }
}

static void drive_backup_clean(BlkActionState *common)
{
    DriveBackupState *state = DO_UPCAST(DriveBackupState, common, common);

    if (state->aio_context) {
        bdrv_drained_end(state->bs);
        aio_context_release(state->aio_context);
    }
}

typedef struct BlockdevBackupState {
    BlkActionState common;
    BlockDriverState *bs;
    BlockJob *job;
    AioContext *aio_context;
} BlockdevBackupState;

static void do_blockdev_backup(const char *device, const char *target,
                               enum MirrorSyncMode sync,
                               bool has_speed, int64_t speed,
                               bool has_on_source_error,
                               BlockdevOnError on_source_error,
                               bool has_on_target_error,
                               BlockdevOnError on_target_error,
                               BlockJobTxn *txn, Error **errp);

static void blockdev_backup_prepare(BlkActionState *common, Error **errp)
{
    BlockdevBackupState *state = DO_UPCAST(BlockdevBackupState, common, common);
    BlockdevBackup *backup;
    BlockBackend *blk, *target;
    Error *local_err = NULL;

    assert(common->action->type == TRANSACTION_ACTION_KIND_BLOCKDEV_BACKUP);
    backup = common->action->u.blockdev_backup.data;

    blk = blk_by_name(backup->device);
    if (!blk) {
        error_setg(errp, "Device '%s' not found", backup->device);
        return;
    }

    if (!blk_is_available(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, backup->device);
        return;
    }

    target = blk_by_name(backup->target);
    if (!target) {
        error_setg(errp, "Device '%s' not found", backup->target);
        return;
    }

    /* AioContext is released in .clean() */
    state->aio_context = blk_get_aio_context(blk);
    if (state->aio_context != blk_get_aio_context(target)) {
        state->aio_context = NULL;
        error_setg(errp, "Backup between two IO threads is not implemented");
        return;
    }
    aio_context_acquire(state->aio_context);
    state->bs = blk_bs(blk);
    bdrv_drained_begin(state->bs);

    do_blockdev_backup(backup->device, backup->target,
                       backup->sync,
                       backup->has_speed, backup->speed,
                       backup->has_on_source_error, backup->on_source_error,
                       backup->has_on_target_error, backup->on_target_error,
                       common->block_job_txn, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    state->job = state->bs->job;
}

static void blockdev_backup_abort(BlkActionState *common)
{
    BlockdevBackupState *state = DO_UPCAST(BlockdevBackupState, common, common);
    BlockDriverState *bs = state->bs;

    /* Only cancel if it's the job we started */
    if (bs && bs->job && bs->job == state->job) {
        block_job_cancel_sync(bs->job);
    }
}

static void blockdev_backup_clean(BlkActionState *common)
{
    BlockdevBackupState *state = DO_UPCAST(BlockdevBackupState, common, common);

    if (state->aio_context) {
        bdrv_drained_end(state->bs);
        aio_context_release(state->aio_context);
    }
}

typedef struct BlockDirtyBitmapState {
    BlkActionState common;
    BdrvDirtyBitmap *bitmap;
    BlockDriverState *bs;
    AioContext *aio_context;
    HBitmap *backup;
    bool prepared;
} BlockDirtyBitmapState;

static void block_dirty_bitmap_add_prepare(BlkActionState *common,
                                           Error **errp)
{
    Error *local_err = NULL;
    BlockDirtyBitmapAdd *action;
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);

    if (action_check_completion_mode(common, errp) < 0) {
        return;
    }

    action = common->action->u.block_dirty_bitmap_add.data;
    /* AIO context taken and released within qmp_block_dirty_bitmap_add */
    qmp_block_dirty_bitmap_add(action->node, action->name,
                               action->has_granularity, action->granularity,
                               &local_err);

    if (!local_err) {
        state->prepared = true;
    } else {
        error_propagate(errp, local_err);
    }
}

static void block_dirty_bitmap_add_abort(BlkActionState *common)
{
    BlockDirtyBitmapAdd *action;
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);

    action = common->action->u.block_dirty_bitmap_add.data;
    /* Should not be able to fail: IF the bitmap was added via .prepare(),
     * then the node reference and bitmap name must have been valid.
     */
    if (state->prepared) {
        qmp_block_dirty_bitmap_remove(action->node, action->name, &error_abort);
    }
}

static void block_dirty_bitmap_clear_prepare(BlkActionState *common,
                                             Error **errp)
{
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);
    BlockDirtyBitmap *action;

    if (action_check_completion_mode(common, errp) < 0) {
        return;
    }

    action = common->action->u.block_dirty_bitmap_clear.data;
    state->bitmap = block_dirty_bitmap_lookup(action->node,
                                              action->name,
                                              &state->bs,
                                              &state->aio_context,
                                              errp);
    if (!state->bitmap) {
        return;
    }

    if (bdrv_dirty_bitmap_frozen(state->bitmap)) {
        error_setg(errp, "Cannot modify a frozen bitmap");
        return;
    } else if (!bdrv_dirty_bitmap_enabled(state->bitmap)) {
        error_setg(errp, "Cannot clear a disabled bitmap");
        return;
    }

    bdrv_clear_dirty_bitmap(state->bitmap, &state->backup);
    /* AioContext is released in .clean() */
}

static void block_dirty_bitmap_clear_abort(BlkActionState *common)
{
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);

    bdrv_undo_clear_dirty_bitmap(state->bitmap, state->backup);
}

static void block_dirty_bitmap_clear_commit(BlkActionState *common)
{
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);

    hbitmap_free(state->backup);
}

static void block_dirty_bitmap_clear_clean(BlkActionState *common)
{
    BlockDirtyBitmapState *state = DO_UPCAST(BlockDirtyBitmapState,
                                             common, common);

    if (state->aio_context) {
        aio_context_release(state->aio_context);
    }
}

static void abort_prepare(BlkActionState *common, Error **errp)
{
    error_setg(errp, "Transaction aborted using Abort action");
}

static void abort_commit(BlkActionState *common)
{
    g_assert_not_reached(); /* this action never succeeds */
}

static const BlkActionOps actions[] = {
    [TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT] = {
        .instance_size = sizeof(ExternalSnapshotState),
        .prepare  = external_snapshot_prepare,
        .commit   = external_snapshot_commit,
        .abort = external_snapshot_abort,
        .clean = external_snapshot_clean,
    },
    [TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC] = {
        .instance_size = sizeof(ExternalSnapshotState),
        .prepare  = external_snapshot_prepare,
        .commit   = external_snapshot_commit,
        .abort = external_snapshot_abort,
        .clean = external_snapshot_clean,
    },
    [TRANSACTION_ACTION_KIND_DRIVE_BACKUP] = {
        .instance_size = sizeof(DriveBackupState),
        .prepare = drive_backup_prepare,
        .abort = drive_backup_abort,
        .clean = drive_backup_clean,
    },
    [TRANSACTION_ACTION_KIND_BLOCKDEV_BACKUP] = {
        .instance_size = sizeof(BlockdevBackupState),
        .prepare = blockdev_backup_prepare,
        .abort = blockdev_backup_abort,
        .clean = blockdev_backup_clean,
    },
    [TRANSACTION_ACTION_KIND_ABORT] = {
        .instance_size = sizeof(BlkActionState),
        .prepare = abort_prepare,
        .commit = abort_commit,
    },
    [TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_INTERNAL_SYNC] = {
        .instance_size = sizeof(InternalSnapshotState),
        .prepare  = internal_snapshot_prepare,
        .abort = internal_snapshot_abort,
        .clean = internal_snapshot_clean,
    },
    [TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_ADD] = {
        .instance_size = sizeof(BlockDirtyBitmapState),
        .prepare = block_dirty_bitmap_add_prepare,
        .abort = block_dirty_bitmap_add_abort,
    },
    [TRANSACTION_ACTION_KIND_BLOCK_DIRTY_BITMAP_CLEAR] = {
        .instance_size = sizeof(BlockDirtyBitmapState),
        .prepare = block_dirty_bitmap_clear_prepare,
        .commit = block_dirty_bitmap_clear_commit,
        .abort = block_dirty_bitmap_clear_abort,
        .clean = block_dirty_bitmap_clear_clean,
    }
};

/**
 * Allocate a TransactionProperties structure if necessary, and fill
 * that structure with desired defaults if they are unset.
 */
static TransactionProperties *get_transaction_properties(
    TransactionProperties *props)
{
    if (!props) {
        props = g_new0(TransactionProperties, 1);
    }

    if (!props->has_completion_mode) {
        props->has_completion_mode = true;
        props->completion_mode = ACTION_COMPLETION_MODE_INDIVIDUAL;
    }

    return props;
}

/*
 * 'Atomic' group operations.  The operations are performed as a set, and if
 * any fail then we roll back all operations in the group.
 */
void qmp_transaction(TransactionActionList *dev_list,
                     bool has_props,
                     struct TransactionProperties *props,
                     Error **errp)
{
    TransactionActionList *dev_entry = dev_list;
    BlockJobTxn *block_job_txn = NULL;
    BlkActionState *state, *next;
    Error *local_err = NULL;

    QSIMPLEQ_HEAD(snap_bdrv_states, BlkActionState) snap_bdrv_states;
    QSIMPLEQ_INIT(&snap_bdrv_states);

    /* Does this transaction get canceled as a group on failure?
     * If not, we don't really need to make a BlockJobTxn.
     */
    props = get_transaction_properties(props);
    if (props->completion_mode != ACTION_COMPLETION_MODE_INDIVIDUAL) {
        block_job_txn = block_job_txn_new();
    }

    /* drain all i/o before any operations */
    bdrv_drain_all();

    /* We don't do anything in this loop that commits us to the operations */
    while (NULL != dev_entry) {
        TransactionAction *dev_info = NULL;
        const BlkActionOps *ops;

        dev_info = dev_entry->value;
        dev_entry = dev_entry->next;

        assert(dev_info->type < ARRAY_SIZE(actions));

        ops = &actions[dev_info->type];
        assert(ops->instance_size > 0);

        state = g_malloc0(ops->instance_size);
        state->ops = ops;
        state->action = dev_info;
        state->block_job_txn = block_job_txn;
        state->txn_props = props;
        QSIMPLEQ_INSERT_TAIL(&snap_bdrv_states, state, entry);

        state->ops->prepare(state, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto delete_and_fail;
        }
    }

    QSIMPLEQ_FOREACH(state, &snap_bdrv_states, entry) {
        if (state->ops->commit) {
            state->ops->commit(state);
        }
    }

    /* success */
    goto exit;

delete_and_fail:
    /* failure, and it is all-or-none; roll back all operations */
    QSIMPLEQ_FOREACH(state, &snap_bdrv_states, entry) {
        if (state->ops->abort) {
            state->ops->abort(state);
        }
    }
exit:
    QSIMPLEQ_FOREACH_SAFE(state, &snap_bdrv_states, entry, next) {
        if (state->ops->clean) {
            state->ops->clean(state);
        }
        g_free(state);
    }
    if (!has_props) {
        qapi_free_TransactionProperties(props);
    }
    block_job_txn_unref(block_job_txn);
}

void qmp_eject(const char *device, bool has_force, bool force, Error **errp)
{
    Error *local_err = NULL;
    int rc;

    if (!has_force) {
        force = false;
    }

    rc = do_open_tray(device, force, &local_err);
    if (rc && rc != -ENOSYS) {
        error_propagate(errp, local_err);
        return;
    }
    error_free(local_err);

    qmp_x_blockdev_remove_medium(device, errp);
}

void qmp_block_passwd(bool has_device, const char *device,
                      bool has_node_name, const char *node_name,
                      const char *password, Error **errp)
{
    Error *local_err = NULL;
    BlockDriverState *bs;
    AioContext *aio_context;

    bs = bdrv_lookup_bs(has_device ? device : NULL,
                        has_node_name ? node_name : NULL,
                        &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    bdrv_add_key(bs, password, errp);

    aio_context_release(aio_context);
}

/*
 * Attempt to open the tray of @device.
 * If @force, ignore its tray lock.
 * Else, if the tray is locked, don't open it, but ask the guest to open it.
 * On error, store an error through @errp and return -errno.
 * If @device does not exist, return -ENODEV.
 * If it has no removable media, return -ENOTSUP.
 * If it has no tray, return -ENOSYS.
 * If the guest was asked to open the tray, return -EINPROGRESS.
 * Else, return 0.
 */
static int do_open_tray(const char *device, bool force, Error **errp)
{
    BlockBackend *blk;
    bool locked;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return -ENODEV;
    }

    if (!blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device);
        return -ENOTSUP;
    }

    if (!blk_dev_has_tray(blk)) {
        error_setg(errp, "Device '%s' does not have a tray", device);
        return -ENOSYS;
    }

    if (blk_dev_is_tray_open(blk)) {
        return 0;
    }

    locked = blk_dev_is_medium_locked(blk);
    if (locked) {
        blk_dev_eject_request(blk, force);
    }

    if (!locked || force) {
        blk_dev_change_media_cb(blk, false);
    }

    if (locked && !force) {
        error_setg(errp, "Device '%s' is locked and force was not specified, "
                   "wait for tray to open and try again", device);
        return -EINPROGRESS;
    }

    return 0;
}

void qmp_blockdev_open_tray(const char *device, bool has_force, bool force,
                            Error **errp)
{
    Error *local_err = NULL;
    int rc;

    if (!has_force) {
        force = false;
    }
    rc = do_open_tray(device, force, &local_err);
    if (rc && rc != -ENOSYS && rc != -EINPROGRESS) {
        error_propagate(errp, local_err);
        return;
    }
    error_free(local_err);
}

void qmp_blockdev_close_tray(const char *device, Error **errp)
{
    BlockBackend *blk;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    if (!blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device);
        return;
    }

    if (!blk_dev_has_tray(blk)) {
        /* Ignore this command on tray-less devices */
        return;
    }

    if (!blk_dev_is_tray_open(blk)) {
        return;
    }

    blk_dev_change_media_cb(blk, true);
}

void qmp_x_blockdev_remove_medium(const char *device, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    AioContext *aio_context;
    bool has_device;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    /* For BBs without a device, we can exchange the BDS tree at will */
    has_device = blk_get_attached_dev(blk);

    if (has_device && !blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device);
        return;
    }

    if (has_device && blk_dev_has_tray(blk) && !blk_dev_is_tray_open(blk)) {
        error_setg(errp, "Tray of device '%s' is not open", device);
        return;
    }

    bs = blk_bs(blk);
    if (!bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_EJECT, errp)) {
        goto out;
    }

    blk_remove_bs(blk);

    if (!blk_dev_has_tray(blk)) {
        /* For tray-less devices, blockdev-open-tray is a no-op (or may not be
         * called at all); therefore, the medium needs to be ejected here.
         * Do it after blk_remove_bs() so blk_is_inserted(blk) returns the @load
         * value passed here (i.e. false). */
        blk_dev_change_media_cb(blk, false);
    }

out:
    aio_context_release(aio_context);
}

static void qmp_blockdev_insert_anon_medium(const char *device,
                                            BlockDriverState *bs, Error **errp)
{
    BlockBackend *blk;
    bool has_device;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    /* For BBs without a device, we can exchange the BDS tree at will */
    has_device = blk_get_attached_dev(blk);

    if (has_device && !blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device);
        return;
    }

    if (has_device && blk_dev_has_tray(blk) && !blk_dev_is_tray_open(blk)) {
        error_setg(errp, "Tray of device '%s' is not open", device);
        return;
    }

    if (blk_bs(blk)) {
        error_setg(errp, "There already is a medium in device '%s'", device);
        return;
    }

    blk_insert_bs(blk, bs);

    if (!blk_dev_has_tray(blk)) {
        /* For tray-less devices, blockdev-close-tray is a no-op (or may not be
         * called at all); therefore, the medium needs to be pushed into the
         * slot here.
         * Do it after blk_insert_bs() so blk_is_inserted(blk) returns the @load
         * value passed here (i.e. true). */
        blk_dev_change_media_cb(blk, true);
    }
}

void qmp_x_blockdev_insert_medium(const char *device, const char *node_name,
                                  Error **errp)
{
    BlockDriverState *bs;

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Node '%s' not found", node_name);
        return;
    }

    if (bdrv_has_blk(bs)) {
        error_setg(errp, "Node '%s' is already in use by '%s'", node_name,
                   bdrv_get_parent_name(bs));
        return;
    }

    qmp_blockdev_insert_anon_medium(device, bs, errp);
}

void qmp_blockdev_change_medium(const char *device, const char *filename,
                                bool has_format, const char *format,
                                bool has_read_only,
                                BlockdevChangeReadOnlyMode read_only,
                                Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *medium_bs = NULL;
    int bdrv_flags;
    int rc;
    QDict *options = NULL;
    Error *err = NULL;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        goto fail;
    }

    if (blk_bs(blk)) {
        blk_update_root_state(blk);
    }

    bdrv_flags = blk_get_open_flags_from_root_state(blk);
    bdrv_flags &= ~(BDRV_O_TEMPORARY | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING |
        BDRV_O_PROTOCOL);

    if (!has_read_only) {
        read_only = BLOCKDEV_CHANGE_READ_ONLY_MODE_RETAIN;
    }

    switch (read_only) {
    case BLOCKDEV_CHANGE_READ_ONLY_MODE_RETAIN:
        break;

    case BLOCKDEV_CHANGE_READ_ONLY_MODE_READ_ONLY:
        bdrv_flags &= ~BDRV_O_RDWR;
        break;

    case BLOCKDEV_CHANGE_READ_ONLY_MODE_READ_WRITE:
        bdrv_flags |= BDRV_O_RDWR;
        break;

    default:
        abort();
    }

    if (has_format) {
        options = qdict_new();
        qdict_put(options, "driver", qstring_from_str(format));
    }

    medium_bs = bdrv_open(filename, NULL, options, bdrv_flags, errp);
    if (!medium_bs) {
        goto fail;
    }

    bdrv_add_key(medium_bs, NULL, &err);
    if (err) {
        error_propagate(errp, err);
        goto fail;
    }

    rc = do_open_tray(device, false, &err);
    if (rc && rc != -ENOSYS) {
        error_propagate(errp, err);
        goto fail;
    }
    error_free(err);
    err = NULL;

    qmp_x_blockdev_remove_medium(device, &err);
    if (err) {
        error_propagate(errp, err);
        goto fail;
    }

    qmp_blockdev_insert_anon_medium(device, medium_bs, &err);
    if (err) {
        error_propagate(errp, err);
        goto fail;
    }

    blk_apply_root_state(blk, medium_bs);

    qmp_blockdev_close_tray(device, errp);

fail:
    /* If the medium has been inserted, the device has its own reference, so
     * ours must be relinquished; and if it has not been inserted successfully,
     * the reference must be relinquished anyway */
    bdrv_unref(medium_bs);
}

/* throttling disk I/O limits */
void qmp_block_set_io_throttle(const char *device, int64_t bps, int64_t bps_rd,
                               int64_t bps_wr,
                               int64_t iops,
                               int64_t iops_rd,
                               int64_t iops_wr,
                               bool has_bps_max,
                               int64_t bps_max,
                               bool has_bps_rd_max,
                               int64_t bps_rd_max,
                               bool has_bps_wr_max,
                               int64_t bps_wr_max,
                               bool has_iops_max,
                               int64_t iops_max,
                               bool has_iops_rd_max,
                               int64_t iops_rd_max,
                               bool has_iops_wr_max,
                               int64_t iops_wr_max,
                               bool has_bps_max_length,
                               int64_t bps_max_length,
                               bool has_bps_rd_max_length,
                               int64_t bps_rd_max_length,
                               bool has_bps_wr_max_length,
                               int64_t bps_wr_max_length,
                               bool has_iops_max_length,
                               int64_t iops_max_length,
                               bool has_iops_rd_max_length,
                               int64_t iops_rd_max_length,
                               bool has_iops_wr_max_length,
                               int64_t iops_wr_max_length,
                               bool has_iops_size,
                               int64_t iops_size,
                               bool has_group,
                               const char *group, Error **errp)
{
    ThrottleConfig cfg;
    BlockDriverState *bs;
    BlockBackend *blk;
    AioContext *aio_context;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    bs = blk_bs(blk);
    if (!bs) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out;
    }

    throttle_config_init(&cfg);
    cfg.buckets[THROTTLE_BPS_TOTAL].avg = bps;
    cfg.buckets[THROTTLE_BPS_READ].avg  = bps_rd;
    cfg.buckets[THROTTLE_BPS_WRITE].avg = bps_wr;

    cfg.buckets[THROTTLE_OPS_TOTAL].avg = iops;
    cfg.buckets[THROTTLE_OPS_READ].avg  = iops_rd;
    cfg.buckets[THROTTLE_OPS_WRITE].avg = iops_wr;

    if (has_bps_max) {
        cfg.buckets[THROTTLE_BPS_TOTAL].max = bps_max;
    }
    if (has_bps_rd_max) {
        cfg.buckets[THROTTLE_BPS_READ].max = bps_rd_max;
    }
    if (has_bps_wr_max) {
        cfg.buckets[THROTTLE_BPS_WRITE].max = bps_wr_max;
    }
    if (has_iops_max) {
        cfg.buckets[THROTTLE_OPS_TOTAL].max = iops_max;
    }
    if (has_iops_rd_max) {
        cfg.buckets[THROTTLE_OPS_READ].max = iops_rd_max;
    }
    if (has_iops_wr_max) {
        cfg.buckets[THROTTLE_OPS_WRITE].max = iops_wr_max;
    }

    if (has_bps_max_length) {
        cfg.buckets[THROTTLE_BPS_TOTAL].burst_length = bps_max_length;
    }
    if (has_bps_rd_max_length) {
        cfg.buckets[THROTTLE_BPS_READ].burst_length = bps_rd_max_length;
    }
    if (has_bps_wr_max_length) {
        cfg.buckets[THROTTLE_BPS_WRITE].burst_length = bps_wr_max_length;
    }
    if (has_iops_max_length) {
        cfg.buckets[THROTTLE_OPS_TOTAL].burst_length = iops_max_length;
    }
    if (has_iops_rd_max_length) {
        cfg.buckets[THROTTLE_OPS_READ].burst_length = iops_rd_max_length;
    }
    if (has_iops_wr_max_length) {
        cfg.buckets[THROTTLE_OPS_WRITE].burst_length = iops_wr_max_length;
    }

    if (has_iops_size) {
        cfg.op_size = iops_size;
    }

    if (!throttle_is_valid(&cfg, errp)) {
        goto out;
    }

    if (throttle_enabled(&cfg)) {
        /* Enable I/O limits if they're not enabled yet, otherwise
         * just update the throttling group. */
        if (!blk_get_public(blk)->throttle_state) {
            blk_io_limits_enable(blk, has_group ? group : device);
        } else if (has_group) {
            blk_io_limits_update_group(blk, group);
        }
        /* Set the new throttling configuration */
        blk_set_io_limits(blk, &cfg);
    } else if (blk_get_public(blk)->throttle_state) {
        /* If all throttling settings are set to 0, disable I/O limits */
        blk_io_limits_disable(blk);
    }

out:
    aio_context_release(aio_context);
}

void qmp_block_dirty_bitmap_add(const char *node, const char *name,
                                bool has_granularity, uint32_t granularity,
                                Error **errp)
{
    AioContext *aio_context;
    BlockDriverState *bs;

    if (!name || name[0] == '\0') {
        error_setg(errp, "Bitmap name cannot be empty");
        return;
    }

    bs = bdrv_lookup_bs(node, node, errp);
    if (!bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (has_granularity) {
        if (granularity < 512 || !is_power_of_2(granularity)) {
            error_setg(errp, "Granularity must be power of 2 "
                             "and at least 512");
            goto out;
        }
    } else {
        /* Default to cluster size, if available: */
        granularity = bdrv_get_default_bitmap_granularity(bs);
    }

    bdrv_create_dirty_bitmap(bs, granularity, name, errp);

 out:
    aio_context_release(aio_context);
}

void qmp_block_dirty_bitmap_remove(const char *node, const char *name,
                                   Error **errp)
{
    AioContext *aio_context;
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, &aio_context, errp);
    if (!bitmap || !bs) {
        return;
    }

    if (bdrv_dirty_bitmap_frozen(bitmap)) {
        error_setg(errp,
                   "Bitmap '%s' is currently frozen and cannot be removed",
                   name);
        goto out;
    }
    bdrv_dirty_bitmap_make_anon(bitmap);
    bdrv_release_dirty_bitmap(bs, bitmap);

 out:
    aio_context_release(aio_context);
}

/**
 * Completely clear a bitmap, for the purposes of synchronizing a bitmap
 * immediately after a full backup operation.
 */
void qmp_block_dirty_bitmap_clear(const char *node, const char *name,
                                  Error **errp)
{
    AioContext *aio_context;
    BdrvDirtyBitmap *bitmap;
    BlockDriverState *bs;

    bitmap = block_dirty_bitmap_lookup(node, name, &bs, &aio_context, errp);
    if (!bitmap || !bs) {
        return;
    }

    if (bdrv_dirty_bitmap_frozen(bitmap)) {
        error_setg(errp,
                   "Bitmap '%s' is currently frozen and cannot be modified",
                   name);
        goto out;
    } else if (!bdrv_dirty_bitmap_enabled(bitmap)) {
        error_setg(errp,
                   "Bitmap '%s' is currently disabled and cannot be cleared",
                   name);
        goto out;
    }

    bdrv_clear_dirty_bitmap(bitmap, NULL);

 out:
    aio_context_release(aio_context);
}

void hmp_drive_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    BlockBackend *blk;
    BlockDriverState *bs;
    AioContext *aio_context;
    Error *local_err = NULL;

    bs = bdrv_find_node(id);
    if (bs) {
        qmp_x_blockdev_del(false, NULL, true, id, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
        return;
    }

    blk = blk_by_name(id);
    if (!blk) {
        error_report("Device '%s' not found", id);
        return;
    }

    if (!blk_legacy_dinfo(blk)) {
        error_report("Deleting device added with blockdev-add"
                     " is not supported");
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    bs = blk_bs(blk);
    if (bs) {
        if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_DRIVE_DEL, &local_err)) {
            error_report_err(local_err);
            aio_context_release(aio_context);
            return;
        }

        blk_remove_bs(blk);
    }

    /* Make the BlockBackend and the attached BlockDriverState anonymous */
    monitor_remove_blk(blk);

    /* If this BlockBackend has a device attached to it, its refcount will be
     * decremented when the device is removed; otherwise we have to do so here.
     */
    if (blk_get_attached_dev(blk)) {
        /* Further I/O must not pause the guest */
        blk_set_on_error(blk, BLOCKDEV_ON_ERROR_REPORT,
                         BLOCKDEV_ON_ERROR_REPORT);
    } else {
        blk_unref(blk);
    }

    aio_context_release(aio_context);
}

void qmp_block_resize(bool has_device, const char *device,
                      bool has_node_name, const char *node_name,
                      int64_t size, Error **errp)
{
    Error *local_err = NULL;
    BlockDriverState *bs;
    AioContext *aio_context;
    int ret;

    bs = bdrv_lookup_bs(has_device ? device : NULL,
                        has_node_name ? node_name : NULL,
                        &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (!bdrv_is_first_non_filter(bs)) {
        error_setg(errp, QERR_FEATURE_DISABLED, "resize");
        goto out;
    }

    if (size < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "size", "a >0 size");
        goto out;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_RESIZE, NULL)) {
        error_setg(errp, QERR_DEVICE_IN_USE, device);
        goto out;
    }

    /* complete all in-flight operations before resizing the device */
    bdrv_drain_all();

    ret = bdrv_truncate(bs, size);
    switch (ret) {
    case 0:
        break;
    case -ENOMEDIUM:
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        break;
    case -ENOTSUP:
        error_setg(errp, QERR_UNSUPPORTED);
        break;
    case -EACCES:
        error_setg(errp, "Device '%s' is read only", device);
        break;
    case -EBUSY:
        error_setg(errp, QERR_DEVICE_IN_USE, device);
        break;
    default:
        error_setg_errno(errp, -ret, "Could not resize");
        break;
    }

out:
    aio_context_release(aio_context);
}

static void block_job_cb(void *opaque, int ret)
{
    /* Note that this function may be executed from another AioContext besides
     * the QEMU main loop.  If you need to access anything that assumes the
     * QEMU global mutex, use a BH or introduce a mutex.
     */

    BlockDriverState *bs = opaque;
    const char *msg = NULL;

    trace_block_job_cb(bs, bs->job, ret);

    assert(bs->job);

    if (ret < 0) {
        msg = strerror(-ret);
    }

    if (block_job_is_cancelled(bs->job)) {
        block_job_event_cancelled(bs->job);
    } else {
        block_job_event_completed(bs->job, msg);
    }
}

void qmp_block_stream(const char *device,
                      bool has_base, const char *base,
                      bool has_backing_file, const char *backing_file,
                      bool has_speed, int64_t speed,
                      bool has_on_error, BlockdevOnError on_error,
                      Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    BlockDriverState *base_bs = NULL;
    AioContext *aio_context;
    Error *local_err = NULL;
    const char *base_name = NULL;

    if (!has_on_error) {
        on_error = BLOCKDEV_ON_ERROR_REPORT;
    }

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out;
    }
    bs = blk_bs(blk);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_STREAM, errp)) {
        goto out;
    }

    if (has_base) {
        base_bs = bdrv_find_backing_image(bs, base);
        if (base_bs == NULL) {
            error_setg(errp, QERR_BASE_NOT_FOUND, base);
            goto out;
        }
        assert(bdrv_get_aio_context(base_bs) == aio_context);
        base_name = base;
    }

    /* if we are streaming the entire chain, the result will have no backing
     * file, and specifying one is therefore an error */
    if (base_bs == NULL && has_backing_file) {
        error_setg(errp, "backing file specified, but streaming the "
                         "entire chain");
        goto out;
    }

    /* backing_file string overrides base bs filename */
    base_name = has_backing_file ? backing_file : base_name;

    stream_start(bs, base_bs, base_name, has_speed ? speed : 0,
                 on_error, block_job_cb, bs, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    trace_qmp_block_stream(bs, bs->job);

out:
    aio_context_release(aio_context);
}

void qmp_block_commit(const char *device,
                      bool has_base, const char *base,
                      bool has_top, const char *top,
                      bool has_backing_file, const char *backing_file,
                      bool has_speed, int64_t speed,
                      Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    BlockDriverState *base_bs, *top_bs;
    AioContext *aio_context;
    Error *local_err = NULL;
    /* This will be part of the QMP command, if/when the
     * BlockdevOnError change for blkmirror makes it in
     */
    BlockdevOnError on_error = BLOCKDEV_ON_ERROR_REPORT;

    if (!has_speed) {
        speed = 0;
    }

    /* Important Note:
     *  libvirt relies on the DeviceNotFound error class in order to probe for
     *  live commit feature versions; for this to work, we must make sure to
     *  perform the device lookup before any generic errors that may occur in a
     *  scenario in which all optional arguments are omitted. */
    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out;
    }
    bs = blk_bs(blk);

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT_SOURCE, errp)) {
        goto out;
    }

    /* default top_bs is the active layer */
    top_bs = bs;

    if (has_top && top) {
        if (strcmp(bs->filename, top) != 0) {
            top_bs = bdrv_find_backing_image(bs, top);
        }
    }

    if (top_bs == NULL) {
        error_setg(errp, "Top image file %s not found", top ? top : "NULL");
        goto out;
    }

    assert(bdrv_get_aio_context(top_bs) == aio_context);

    if (has_base && base) {
        base_bs = bdrv_find_backing_image(top_bs, base);
    } else {
        base_bs = bdrv_find_base(top_bs);
    }

    if (base_bs == NULL) {
        error_setg(errp, QERR_BASE_NOT_FOUND, base ? base : "NULL");
        goto out;
    }

    assert(bdrv_get_aio_context(base_bs) == aio_context);

    if (bdrv_op_is_blocked(base_bs, BLOCK_OP_TYPE_COMMIT_TARGET, errp)) {
        goto out;
    }

    /* Do not allow attempts to commit an image into itself */
    if (top_bs == base_bs) {
        error_setg(errp, "cannot commit an image into itself");
        goto out;
    }

    if (top_bs == bs) {
        if (has_backing_file) {
            error_setg(errp, "'backing-file' specified,"
                             " but 'top' is the active layer");
            goto out;
        }
        commit_active_start(bs, base_bs, speed, on_error, block_job_cb,
                            bs, &local_err);
    } else {
        commit_start(bs, base_bs, top_bs, speed, on_error, block_job_cb, bs,
                     has_backing_file ? backing_file : NULL, &local_err);
    }
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        goto out;
    }

out:
    aio_context_release(aio_context);
}

static void do_drive_backup(const char *device, const char *target,
                            bool has_format, const char *format,
                            enum MirrorSyncMode sync,
                            bool has_mode, enum NewImageMode mode,
                            bool has_speed, int64_t speed,
                            bool has_bitmap, const char *bitmap,
                            bool has_on_source_error,
                            BlockdevOnError on_source_error,
                            bool has_on_target_error,
                            BlockdevOnError on_target_error,
                            BlockJobTxn *txn, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    BlockDriverState *target_bs;
    BlockDriverState *source = NULL;
    BdrvDirtyBitmap *bmap = NULL;
    AioContext *aio_context;
    QDict *options = NULL;
    Error *local_err = NULL;
    int flags;
    int64_t size;

    if (!has_speed) {
        speed = 0;
    }
    if (!has_on_source_error) {
        on_source_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!has_on_target_error) {
        on_target_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!has_mode) {
        mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    }

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    /* Although backup_run has this check too, we need to use bs->drv below, so
     * do an early check redundantly. */
    if (!blk_is_available(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        goto out;
    }
    bs = blk_bs(blk);

    if (!has_format) {
        format = mode == NEW_IMAGE_MODE_EXISTING ? NULL : bs->drv->format_name;
    }

    /* Early check to avoid creating target */
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BACKUP_SOURCE, errp)) {
        goto out;
    }

    flags = bs->open_flags | BDRV_O_RDWR;

    /* See if we have a backing HD we can use to create our new image
     * on top of. */
    if (sync == MIRROR_SYNC_MODE_TOP) {
        source = backing_bs(bs);
        if (!source) {
            sync = MIRROR_SYNC_MODE_FULL;
        }
    }
    if (sync == MIRROR_SYNC_MODE_NONE) {
        source = bs;
    }

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "bdrv_getlength failed");
        goto out;
    }

    if (mode != NEW_IMAGE_MODE_EXISTING) {
        assert(format);
        if (source) {
            bdrv_img_create(target, format, source->filename,
                            source->drv->format_name, NULL,
                            size, flags, &local_err, false);
        } else {
            bdrv_img_create(target, format, NULL, NULL, NULL,
                            size, flags, &local_err, false);
        }
    }

    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    if (format) {
        options = qdict_new();
        qdict_put(options, "driver", qstring_from_str(format));
    }

    target_bs = bdrv_open(target, NULL, options, flags, errp);
    if (!target_bs) {
        goto out;
    }

    bdrv_set_aio_context(target_bs, aio_context);

    if (has_bitmap) {
        bmap = bdrv_find_dirty_bitmap(bs, bitmap);
        if (!bmap) {
            error_setg(errp, "Bitmap '%s' could not be found", bitmap);
            bdrv_unref(target_bs);
            goto out;
        }
    }

    backup_start(bs, target_bs, speed, sync, bmap,
                 on_source_error, on_target_error,
                 block_job_cb, bs, txn, &local_err);
    bdrv_unref(target_bs);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        goto out;
    }

out:
    aio_context_release(aio_context);
}

void qmp_drive_backup(const char *device, const char *target,
                      bool has_format, const char *format,
                      enum MirrorSyncMode sync,
                      bool has_mode, enum NewImageMode mode,
                      bool has_speed, int64_t speed,
                      bool has_bitmap, const char *bitmap,
                      bool has_on_source_error, BlockdevOnError on_source_error,
                      bool has_on_target_error, BlockdevOnError on_target_error,
                      Error **errp)
{
    return do_drive_backup(device, target, has_format, format, sync,
                           has_mode, mode, has_speed, speed,
                           has_bitmap, bitmap,
                           has_on_source_error, on_source_error,
                           has_on_target_error, on_target_error,
                           NULL, errp);
}

BlockDeviceInfoList *qmp_query_named_block_nodes(Error **errp)
{
    return bdrv_named_nodes_list(errp);
}

void do_blockdev_backup(const char *device, const char *target,
                         enum MirrorSyncMode sync,
                         bool has_speed, int64_t speed,
                         bool has_on_source_error,
                         BlockdevOnError on_source_error,
                         bool has_on_target_error,
                         BlockdevOnError on_target_error,
                         BlockJobTxn *txn, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    BlockDriverState *target_bs;
    Error *local_err = NULL;
    AioContext *aio_context;

    if (!has_speed) {
        speed = 0;
    }
    if (!has_on_source_error) {
        on_source_error = BLOCKDEV_ON_ERROR_REPORT;
    }
    if (!has_on_target_error) {
        on_target_error = BLOCKDEV_ON_ERROR_REPORT;
    }

    blk = blk_by_name(device);
    if (!blk) {
        error_setg(errp, "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out;
    }
    bs = blk_bs(blk);

    target_bs = bdrv_lookup_bs(target, target, errp);
    if (!target_bs) {
        goto out;
    }

    if (bdrv_get_aio_context(target_bs) != aio_context) {
        if (!bdrv_has_blk(target_bs)) {
            /* The target BDS is not attached, we can safely move it to another
             * AioContext. */
            bdrv_set_aio_context(target_bs, aio_context);
        } else {
            error_setg(errp, "Target is attached to a different thread from "
                             "source.");
            goto out;
        }
    }
    backup_start(bs, target_bs, speed, sync, NULL, on_source_error,
                 on_target_error, block_job_cb, bs, txn, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
    }
out:
    aio_context_release(aio_context);
}

void qmp_blockdev_backup(const char *device, const char *target,
                         enum MirrorSyncMode sync,
                         bool has_speed, int64_t speed,
                         bool has_on_source_error,
                         BlockdevOnError on_source_error,
                         bool has_on_target_error,
                         BlockdevOnError on_target_error,
                         Error **errp)
{
    do_blockdev_backup(device, target, sync, has_speed, speed,
                       has_on_source_error, on_source_error,
                       has_on_target_error, on_target_error,
                       NULL, errp);
}

/* Parameter check and block job starting for drive mirroring.
 * Caller should hold @device and @target's aio context (must be the same).
 **/
static void blockdev_mirror_common(BlockDriverState *bs,
                                   BlockDriverState *target,
                                   bool has_replaces, const char *replaces,
                                   enum MirrorSyncMode sync,
                                   BlockMirrorBackingMode backing_mode,
                                   bool has_speed, int64_t speed,
                                   bool has_granularity, uint32_t granularity,
                                   bool has_buf_size, int64_t buf_size,
                                   bool has_on_source_error,
                                   BlockdevOnError on_source_error,
                                   bool has_on_target_error,
                                   BlockdevOnError on_target_error,
                                   bool has_unmap, bool unmap,
                                   Error **errp)
{

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

    if (granularity != 0 && (granularity < 512 || granularity > 1048576 * 64)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "granularity",
                   "a value in range [512B, 64MB]");
        return;
    }
    if (granularity & (granularity - 1)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "granularity",
                   "power of 2");
        return;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_MIRROR_SOURCE, errp)) {
        return;
    }
    if (bdrv_op_is_blocked(target, BLOCK_OP_TYPE_MIRROR_TARGET, errp)) {
        return;
    }

    if (!bs->backing && sync == MIRROR_SYNC_MODE_TOP) {
        sync = MIRROR_SYNC_MODE_FULL;
    }

    /* pass the node name to replace to mirror start since it's loose coupling
     * and will allow to check whether the node still exist at mirror completion
     */
    mirror_start(bs, target,
                 has_replaces ? replaces : NULL,
                 speed, granularity, buf_size, sync, backing_mode,
                 on_source_error, on_target_error, unmap,
                 block_job_cb, bs, errp);
}

void qmp_drive_mirror(const char *device, const char *target,
                      bool has_format, const char *format,
                      bool has_node_name, const char *node_name,
                      bool has_replaces, const char *replaces,
                      enum MirrorSyncMode sync,
                      bool has_mode, enum NewImageMode mode,
                      bool has_speed, int64_t speed,
                      bool has_granularity, uint32_t granularity,
                      bool has_buf_size, int64_t buf_size,
                      bool has_on_source_error, BlockdevOnError on_source_error,
                      bool has_on_target_error, BlockdevOnError on_target_error,
                      bool has_unmap, bool unmap,
                      Error **errp)
{
    BlockDriverState *bs;
    BlockBackend *blk;
    BlockDriverState *source, *target_bs;
    AioContext *aio_context;
    BlockMirrorBackingMode backing_mode;
    Error *local_err = NULL;
    QDict *options = NULL;
    int flags;
    int64_t size;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        goto out;
    }
    bs = blk_bs(blk);
    if (!has_mode) {
        mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    }

    if (!has_format) {
        format = mode == NEW_IMAGE_MODE_EXISTING ? NULL : bs->drv->format_name;
    }

    flags = bs->open_flags | BDRV_O_RDWR;
    source = backing_bs(bs);
    if (!source && sync == MIRROR_SYNC_MODE_TOP) {
        sync = MIRROR_SYNC_MODE_FULL;
    }
    if (sync == MIRROR_SYNC_MODE_NONE) {
        source = bs;
    }

    size = bdrv_getlength(bs);
    if (size < 0) {
        error_setg_errno(errp, -size, "bdrv_getlength failed");
        goto out;
    }

    if (has_replaces) {
        BlockDriverState *to_replace_bs;
        AioContext *replace_aio_context;
        int64_t replace_size;

        if (!has_node_name) {
            error_setg(errp, "a node-name must be provided when replacing a"
                             " named node of the graph");
            goto out;
        }

        to_replace_bs = check_to_replace_node(bs, replaces, &local_err);

        if (!to_replace_bs) {
            error_propagate(errp, local_err);
            goto out;
        }

        replace_aio_context = bdrv_get_aio_context(to_replace_bs);
        aio_context_acquire(replace_aio_context);
        replace_size = bdrv_getlength(to_replace_bs);
        aio_context_release(replace_aio_context);

        if (size != replace_size) {
            error_setg(errp, "cannot replace image with a mirror image of "
                             "different size");
            goto out;
        }
    }

    if (mode == NEW_IMAGE_MODE_ABSOLUTE_PATHS) {
        backing_mode = MIRROR_SOURCE_BACKING_CHAIN;
    } else {
        backing_mode = MIRROR_OPEN_BACKING_CHAIN;
    }

    if ((sync == MIRROR_SYNC_MODE_FULL || !source)
        && mode != NEW_IMAGE_MODE_EXISTING)
    {
        /* create new image w/o backing file */
        assert(format);
        bdrv_img_create(target, format,
                        NULL, NULL, NULL, size, flags, &local_err, false);
    } else {
        switch (mode) {
        case NEW_IMAGE_MODE_EXISTING:
            break;
        case NEW_IMAGE_MODE_ABSOLUTE_PATHS:
            /* create new image with backing file */
            bdrv_img_create(target, format,
                            source->filename,
                            source->drv->format_name,
                            NULL, size, flags, &local_err, false);
            break;
        default:
            abort();
        }
    }

    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    options = qdict_new();
    if (has_node_name) {
        qdict_put(options, "node-name", qstring_from_str(node_name));
    }
    if (format) {
        qdict_put(options, "driver", qstring_from_str(format));
    }

    /* Mirroring takes care of copy-on-write using the source's backing
     * file.
     */
    target_bs = bdrv_open(target, NULL, options, flags | BDRV_O_NO_BACKING,
                          errp);
    if (!target_bs) {
        goto out;
    }

    bdrv_set_aio_context(target_bs, aio_context);

    blockdev_mirror_common(bs, target_bs,
                           has_replaces, replaces, sync, backing_mode,
                           has_speed, speed,
                           has_granularity, granularity,
                           has_buf_size, buf_size,
                           has_on_source_error, on_source_error,
                           has_on_target_error, on_target_error,
                           has_unmap, unmap,
                           &local_err);
    bdrv_unref(target_bs);
    if (local_err) {
        error_propagate(errp, local_err);
    }
out:
    aio_context_release(aio_context);
}

void qmp_blockdev_mirror(const char *device, const char *target,
                         bool has_replaces, const char *replaces,
                         MirrorSyncMode sync,
                         bool has_speed, int64_t speed,
                         bool has_granularity, uint32_t granularity,
                         bool has_buf_size, int64_t buf_size,
                         bool has_on_source_error,
                         BlockdevOnError on_source_error,
                         bool has_on_target_error,
                         BlockdevOnError on_target_error,
                         Error **errp)
{
    BlockDriverState *bs;
    BlockBackend *blk;
    BlockDriverState *target_bs;
    AioContext *aio_context;
    BlockMirrorBackingMode backing_mode = MIRROR_LEAVE_BACKING_CHAIN;
    Error *local_err = NULL;

    blk = blk_by_name(device);
    if (!blk) {
        error_setg(errp, "Device '%s' not found", device);
        return;
    }
    bs = blk_bs(blk);

    if (!bs) {
        error_setg(errp, "Device '%s' has no media", device);
        return;
    }

    target_bs = bdrv_lookup_bs(target, target, errp);
    if (!target_bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    bdrv_set_aio_context(target_bs, aio_context);

    blockdev_mirror_common(bs, target_bs,
                           has_replaces, replaces, sync, backing_mode,
                           has_speed, speed,
                           has_granularity, granularity,
                           has_buf_size, buf_size,
                           has_on_source_error, on_source_error,
                           has_on_target_error, on_target_error,
                           true, true,
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }

    aio_context_release(aio_context);
}

/* Get the block job for a given device name and acquire its AioContext */
static BlockJob *find_block_job(const char *device, AioContext **aio_context,
                                Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    *aio_context = NULL;

    blk = blk_by_name(device);
    if (!blk) {
        goto notfound;
    }

    *aio_context = blk_get_aio_context(blk);
    aio_context_acquire(*aio_context);

    if (!blk_is_available(blk)) {
        goto notfound;
    }
    bs = blk_bs(blk);

    if (!bs->job) {
        goto notfound;
    }

    return bs->job;

notfound:
    error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
              "No active block job on device '%s'", device);
    if (*aio_context) {
        aio_context_release(*aio_context);
        *aio_context = NULL;
    }
    return NULL;
}

void qmp_block_job_set_speed(const char *device, int64_t speed, Error **errp)
{
    AioContext *aio_context;
    BlockJob *job = find_block_job(device, &aio_context, errp);

    if (!job) {
        return;
    }

    block_job_set_speed(job, speed, errp);
    aio_context_release(aio_context);
}

void qmp_block_job_cancel(const char *device,
                          bool has_force, bool force, Error **errp)
{
    AioContext *aio_context;
    BlockJob *job = find_block_job(device, &aio_context, errp);

    if (!job) {
        return;
    }

    if (!has_force) {
        force = false;
    }

    if (job->user_paused && !force) {
        error_setg(errp, "The block job for device '%s' is currently paused",
                   device);
        goto out;
    }

    trace_qmp_block_job_cancel(job);
    block_job_cancel(job);
out:
    aio_context_release(aio_context);
}

void qmp_block_job_pause(const char *device, Error **errp)
{
    AioContext *aio_context;
    BlockJob *job = find_block_job(device, &aio_context, errp);

    if (!job || job->user_paused) {
        return;
    }

    job->user_paused = true;
    trace_qmp_block_job_pause(job);
    block_job_pause(job);
    aio_context_release(aio_context);
}

void qmp_block_job_resume(const char *device, Error **errp)
{
    AioContext *aio_context;
    BlockJob *job = find_block_job(device, &aio_context, errp);

    if (!job || !job->user_paused) {
        return;
    }

    job->user_paused = false;
    trace_qmp_block_job_resume(job);
    block_job_resume(job);
    aio_context_release(aio_context);
}

void qmp_block_job_complete(const char *device, Error **errp)
{
    AioContext *aio_context;
    BlockJob *job = find_block_job(device, &aio_context, errp);

    if (!job) {
        return;
    }

    trace_qmp_block_job_complete(job);
    block_job_complete(job, errp);
    aio_context_release(aio_context);
}

void qmp_change_backing_file(const char *device,
                             const char *image_node_name,
                             const char *backing_file,
                             Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs = NULL;
    AioContext *aio_context;
    BlockDriverState *image_bs = NULL;
    Error *local_err = NULL;
    bool ro;
    int open_flags;
    int ret;

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    if (!blk_is_available(blk)) {
        error_setg(errp, "Device '%s' has no medium", device);
        goto out;
    }
    bs = blk_bs(blk);

    image_bs = bdrv_lookup_bs(NULL, image_node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    if (!image_bs) {
        error_setg(errp, "image file not found");
        goto out;
    }

    if (bdrv_find_base(image_bs) == image_bs) {
        error_setg(errp, "not allowing backing file change on an image "
                         "without a backing file");
        goto out;
    }

    /* even though we are not necessarily operating on bs, we need it to
     * determine if block ops are currently prohibited on the chain */
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_CHANGE, errp)) {
        goto out;
    }

    /* final sanity check */
    if (!bdrv_chain_contains(bs, image_bs)) {
        error_setg(errp, "'%s' and image file are not in the same chain",
                   device);
        goto out;
    }

    /* if not r/w, reopen to make r/w */
    open_flags = image_bs->open_flags;
    ro = bdrv_is_read_only(image_bs);

    if (ro) {
        bdrv_reopen(image_bs, open_flags | BDRV_O_RDWR, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto out;
        }
    }

    ret = bdrv_change_backing_file(image_bs, backing_file,
                               image_bs->drv ? image_bs->drv->format_name : "");

    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not change backing file to '%s'",
                         backing_file);
        /* don't exit here, so we can try to restore open flags if
         * appropriate */
    }

    if (ro) {
        bdrv_reopen(image_bs, open_flags, &local_err);
        if (local_err) {
            error_propagate(errp, local_err); /* will preserve prior errp */
        }
    }

out:
    aio_context_release(aio_context);
}

void hmp_drive_add_node(Monitor *mon, const char *optstr)
{
    QemuOpts *opts;
    QDict *qdict;
    Error *local_err = NULL;

    opts = qemu_opts_parse_noisily(&qemu_drive_opts, optstr, false);
    if (!opts) {
        return;
    }

    qdict = qemu_opts_to_qdict(opts, NULL);

    if (!qdict_get_try_str(qdict, "node-name")) {
        QDECREF(qdict);
        error_report("'node-name' needs to be specified");
        goto out;
    }

    BlockDriverState *bs = bds_tree_init(qdict, &local_err);
    if (!bs) {
        error_report_err(local_err);
        goto out;
    }

    QTAILQ_INSERT_TAIL(&monitor_bdrv_states, bs, monitor_list);

out:
    qemu_opts_del(opts);
}

void qmp_blockdev_add(BlockdevOptions *options, Error **errp)
{
    QmpOutputVisitor *ov = qmp_output_visitor_new();
    BlockDriverState *bs;
    BlockBackend *blk = NULL;
    QObject *obj;
    QDict *qdict;
    Error *local_err = NULL;

    /* TODO Sort it out in raw-posix and drive_new(): Reject aio=native with
     * cache.direct=false instead of silently switching to aio=threads, except
     * when called from drive_new().
     *
     * For now, simply forbidding the combination for all drivers will do. */
    if (options->has_aio && options->aio == BLOCKDEV_AIO_OPTIONS_NATIVE) {
        bool direct = options->has_cache &&
                      options->cache->has_direct &&
                      options->cache->direct;
        if (!direct) {
            error_setg(errp, "aio=native requires cache.direct=true");
            goto fail;
        }
    }

    visit_type_BlockdevOptions(qmp_output_get_visitor(ov), NULL, &options,
                               &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }

    obj = qmp_output_get_qobject(ov);
    qdict = qobject_to_qdict(obj);

    qdict_flatten(qdict);

    if (options->has_id) {
        blk = blockdev_init(NULL, qdict, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto fail;
        }

        bs = blk_bs(blk);
    } else {
        if (!qdict_get_try_str(qdict, "node-name")) {
            error_setg(errp, "'id' and/or 'node-name' need to be specified for "
                       "the root node");
            goto fail;
        }

        bs = bds_tree_init(qdict, errp);
        if (!bs) {
            goto fail;
        }

        QTAILQ_INSERT_TAIL(&monitor_bdrv_states, bs, monitor_list);
    }

    if (bs && bdrv_key_required(bs)) {
        if (blk) {
            monitor_remove_blk(blk);
            blk_unref(blk);
        } else {
            QTAILQ_REMOVE(&monitor_bdrv_states, bs, monitor_list);
            bdrv_unref(bs);
        }
        error_setg(errp, "blockdev-add doesn't support encrypted devices");
        goto fail;
    }

fail:
    qmp_output_visitor_cleanup(ov);
}

void qmp_x_blockdev_del(bool has_id, const char *id,
                        bool has_node_name, const char *node_name, Error **errp)
{
    AioContext *aio_context;
    BlockBackend *blk;
    BlockDriverState *bs;

    if (has_id && has_node_name) {
        error_setg(errp, "Only one of id and node-name must be specified");
        return;
    } else if (!has_id && !has_node_name) {
        error_setg(errp, "No block device specified");
        return;
    }

    if (has_id) {
        /* blk_by_name() never returns a BB that is not owned by the monitor */
        blk = blk_by_name(id);
        if (!blk) {
            error_setg(errp, "Cannot find block backend %s", id);
            return;
        }
        if (blk_legacy_dinfo(blk)) {
            error_setg(errp, "Deleting block backend added with drive-add"
                       " is not supported");
            return;
        }
        if (blk_get_refcnt(blk) > 1) {
            error_setg(errp, "Block backend %s is in use", id);
            return;
        }
        bs = blk_bs(blk);
        aio_context = blk_get_aio_context(blk);
    } else {
        blk = NULL;
        bs = bdrv_find_node(node_name);
        if (!bs) {
            error_setg(errp, "Cannot find node %s", node_name);
            return;
        }
        if (bdrv_has_blk(bs)) {
            error_setg(errp, "Node %s is in use by %s",
                       node_name, bdrv_get_parent_name(bs));
            return;
        }
        aio_context = bdrv_get_aio_context(bs);
    }

    aio_context_acquire(aio_context);

    if (bs) {
        if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_DRIVE_DEL, errp)) {
            goto out;
        }

        if (!blk && !bs->monitor_list.tqe_prev) {
            error_setg(errp, "Node %s is not owned by the monitor",
                       bs->node_name);
            goto out;
        }

        if (bs->refcnt > 1) {
            error_setg(errp, "Block device %s is in use",
                       bdrv_get_device_or_node_name(bs));
            goto out;
        }
    }

    if (blk) {
        monitor_remove_blk(blk);
        blk_unref(blk);
    } else {
        QTAILQ_REMOVE(&monitor_bdrv_states, bs, monitor_list);
        bdrv_unref(bs);
    }

out:
    aio_context_release(aio_context);
}

static BdrvChild *bdrv_find_child(BlockDriverState *parent_bs,
                                  const char *child_name)
{
    BdrvChild *child;

    QLIST_FOREACH(child, &parent_bs->children, next) {
        if (strcmp(child->name, child_name) == 0) {
            return child;
        }
    }

    return NULL;
}

void qmp_x_blockdev_change(const char *parent, bool has_child,
                           const char *child, bool has_node,
                           const char *node, Error **errp)
{
    BlockDriverState *parent_bs, *new_bs = NULL;
    BdrvChild *p_child;

    parent_bs = bdrv_lookup_bs(parent, parent, errp);
    if (!parent_bs) {
        return;
    }

    if (has_child == has_node) {
        if (has_child) {
            error_setg(errp, "The parameters child and node are in conflict");
        } else {
            error_setg(errp, "Either child or node must be specified");
        }
        return;
    }

    if (has_child) {
        p_child = bdrv_find_child(parent_bs, child);
        if (!p_child) {
            error_setg(errp, "Node '%s' does not have child '%s'",
                       parent, child);
            return;
        }
        bdrv_del_child(parent_bs, p_child, errp);
    }

    if (has_node) {
        new_bs = bdrv_find_node(node);
        if (!new_bs) {
            error_setg(errp, "Node '%s' not found", node);
            return;
        }
        bdrv_add_child(parent_bs, new_bs, errp);
    }
}

BlockJobInfoList *qmp_query_block_jobs(Error **errp)
{
    BlockJobInfoList *head = NULL, **p_next = &head;
    BlockJob *job;

    for (job = block_job_next(NULL); job; job = block_job_next(job)) {
        BlockJobInfoList *elem = g_new0(BlockJobInfoList, 1);
        AioContext *aio_context = blk_get_aio_context(job->blk);

        aio_context_acquire(aio_context);
        elem->value = block_job_query(job);
        aio_context_release(aio_context);

        *p_next = elem;
        p_next = &elem->next;
    }

    return head;
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
            .name = "discard",
            .type = QEMU_OPT_STRING,
            .help = "discard operation (ignore/off, unmap/on)",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
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
            .name = "read-only",
            .type = QEMU_OPT_BOOL,
            .help = "open drive file as read-only",
        },{
            .name = "throttling.iops-total",
            .type = QEMU_OPT_NUMBER,
            .help = "limit total I/O operations per second",
        },{
            .name = "throttling.iops-read",
            .type = QEMU_OPT_NUMBER,
            .help = "limit read operations per second",
        },{
            .name = "throttling.iops-write",
            .type = QEMU_OPT_NUMBER,
            .help = "limit write operations per second",
        },{
            .name = "throttling.bps-total",
            .type = QEMU_OPT_NUMBER,
            .help = "limit total bytes per second",
        },{
            .name = "throttling.bps-read",
            .type = QEMU_OPT_NUMBER,
            .help = "limit read bytes per second",
        },{
            .name = "throttling.bps-write",
            .type = QEMU_OPT_NUMBER,
            .help = "limit write bytes per second",
        },{
            .name = "throttling.iops-total-max",
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations burst",
        },{
            .name = "throttling.iops-read-max",
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations read burst",
        },{
            .name = "throttling.iops-write-max",
            .type = QEMU_OPT_NUMBER,
            .help = "I/O operations write burst",
        },{
            .name = "throttling.bps-total-max",
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes burst",
        },{
            .name = "throttling.bps-read-max",
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes read burst",
        },{
            .name = "throttling.bps-write-max",
            .type = QEMU_OPT_NUMBER,
            .help = "total bytes write burst",
        },{
            .name = "throttling.iops-total-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iops-total-max burst period, in seconds",
        },{
            .name = "throttling.iops-read-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iops-read-max burst period, in seconds",
        },{
            .name = "throttling.iops-write-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the iops-write-max burst period, in seconds",
        },{
            .name = "throttling.bps-total-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bps-total-max burst period, in seconds",
        },{
            .name = "throttling.bps-read-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bps-read-max burst period, in seconds",
        },{
            .name = "throttling.bps-write-max-length",
            .type = QEMU_OPT_NUMBER,
            .help = "length of the bps-write-max burst period, in seconds",
        },{
            .name = "throttling.iops-size",
            .type = QEMU_OPT_NUMBER,
            .help = "when limiting by iops max size of an I/O in bytes",
        },{
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

static QemuOptsList qemu_root_bds_opts = {
    .name = "root-bds",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_root_bds_opts.head),
    .desc = {
        {
            .name = "discard",
            .type = QEMU_OPT_STRING,
            .help = "discard operation (ignore/off, unmap/on)",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
        },{
            .name = "read-only",
            .type = QEMU_OPT_BOOL,
            .help = "open drive file as read-only",
        },{
            .name = "copy-on-read",
            .type = QEMU_OPT_BOOL,
            .help = "copy read data from backing file into image file",
        },{
            .name = "detect-zeroes",
            .type = QEMU_OPT_STRING,
            .help = "try to optimize zero writes (off, on, unmap)",
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
