/*
 * QMP command handlers specific to the system emulators
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

#include "block/block_int.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qmp/qdict.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"

static BlockBackend *qmp_get_blk(const char *blk_name, const char *qdev_id,
                                 Error **errp)
{
    BlockBackend *blk;

    if (!blk_name == !qdev_id) {
        error_setg(errp, "Need exactly one of 'device' and 'id'");
        return NULL;
    }

    if (qdev_id) {
        blk = blk_by_qdev_id(qdev_id, errp);
    } else {
        blk = blk_by_name(blk_name);
        if (blk == NULL) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", blk_name);
        }
    }

    return blk;
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
static int do_open_tray(const char *blk_name, const char *qdev_id,
                        bool force, Error **errp)
{
    BlockBackend *blk;
    const char *device = qdev_id ?: blk_name;
    bool locked;

    blk = qmp_get_blk(blk_name, qdev_id, errp);
    if (!blk) {
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
        blk_dev_change_media_cb(blk, false, &error_abort);
    }

    if (locked && !force) {
        error_setg(errp, "Device '%s' is locked and force was not specified, "
                   "wait for tray to open and try again", device);
        return -EINPROGRESS;
    }

    return 0;
}

void qmp_blockdev_open_tray(const char *device,
                            const char *id,
                            bool has_force, bool force,
                            Error **errp)
{
    Error *local_err = NULL;
    int rc;

    if (!has_force) {
        force = false;
    }
    rc = do_open_tray(device, id, force, &local_err);
    if (rc && rc != -ENOSYS && rc != -EINPROGRESS) {
        error_propagate(errp, local_err);
        return;
    }
    error_free(local_err);
}

void qmp_blockdev_close_tray(const char *device,
                             const char *id,
                             Error **errp)
{
    BlockBackend *blk;
    Error *local_err = NULL;

    blk = qmp_get_blk(device, id, errp);
    if (!blk) {
        return;
    }

    if (!blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device ?: id);
        return;
    }

    if (!blk_dev_has_tray(blk)) {
        /* Ignore this command on tray-less devices */
        return;
    }

    if (!blk_dev_is_tray_open(blk)) {
        return;
    }

    blk_dev_change_media_cb(blk, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void GRAPH_UNLOCKED
blockdev_remove_medium(const char *device, const char *id, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    AioContext *aio_context;
    bool has_attached_device;

    GLOBAL_STATE_CODE();

    blk = qmp_get_blk(device, id, errp);
    if (!blk) {
        return;
    }

    /* For BBs without a device, we can exchange the BDS tree at will */
    has_attached_device = blk_get_attached_dev(blk);

    if (has_attached_device && !blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device '%s' is not removable", device ?: id);
        return;
    }

    if (has_attached_device && blk_dev_has_tray(blk) &&
        !blk_dev_is_tray_open(blk))
    {
        error_setg(errp, "Tray of device '%s' is not open", device ?: id);
        return;
    }

    bs = blk_bs(blk);
    if (!bs) {
        return;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    bdrv_graph_rdlock_main_loop();
    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_EJECT, errp)) {
        bdrv_graph_rdunlock_main_loop();
        goto out;
    }
    bdrv_graph_rdunlock_main_loop();

    blk_remove_bs(blk);

    if (!blk_dev_has_tray(blk)) {
        /* For tray-less devices, blockdev-open-tray is a no-op (or may not be
         * called at all); therefore, the medium needs to be ejected here.
         * Do it after blk_remove_bs() so blk_is_inserted(blk) returns the @load
         * value passed here (i.e. false). */
        blk_dev_change_media_cb(blk, false, &error_abort);
    }

out:
    aio_context_release(aio_context);
}

void qmp_blockdev_remove_medium(const char *id, Error **errp)
{
    blockdev_remove_medium(NULL, id, errp);
}

static void qmp_blockdev_insert_anon_medium(BlockBackend *blk,
                                            BlockDriverState *bs, Error **errp)
{
    Error *local_err = NULL;
    AioContext *ctx;
    bool has_device;
    int ret;

    /* For BBs without a device, we can exchange the BDS tree at will */
    has_device = blk_get_attached_dev(blk);

    if (has_device && !blk_dev_has_removable_media(blk)) {
        error_setg(errp, "Device is not removable");
        return;
    }

    if (has_device && blk_dev_has_tray(blk) && !blk_dev_is_tray_open(blk)) {
        error_setg(errp, "Tray of the device is not open");
        return;
    }

    if (blk_bs(blk)) {
        error_setg(errp, "There already is a medium in the device");
        return;
    }

    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);
    ret = blk_insert_bs(blk, bs, errp);
    aio_context_release(ctx);

    if (ret < 0) {
        return;
    }

    if (!blk_dev_has_tray(blk)) {
        /* For tray-less devices, blockdev-close-tray is a no-op (or may not be
         * called at all); therefore, the medium needs to be pushed into the
         * slot here.
         * Do it after blk_insert_bs() so blk_is_inserted(blk) returns the @load
         * value passed here (i.e. true). */
        blk_dev_change_media_cb(blk, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            blk_remove_bs(blk);
            return;
        }
    }
}

static void blockdev_insert_medium(const char *device, const char *id,
                                   const char *node_name, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    blk = qmp_get_blk(device, id, errp);
    if (!blk) {
        return;
    }

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Node '%s' not found", node_name);
        return;
    }

    if (bdrv_has_blk(bs)) {
        error_setg(errp, "Node '%s' is already in use", node_name);
        return;
    }

    qmp_blockdev_insert_anon_medium(blk, bs, errp);
}

void qmp_blockdev_insert_medium(const char *id, const char *node_name,
                                Error **errp)
{
    blockdev_insert_medium(NULL, id, node_name, errp);
}

void qmp_blockdev_change_medium(const char *device,
                                const char *id,
                                const char *filename,
                                const char *format,
                                bool has_force, bool force,
                                bool has_read_only,
                                BlockdevChangeReadOnlyMode read_only,
                                Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *medium_bs = NULL;
    int bdrv_flags;
    bool detect_zeroes;
    int rc;
    QDict *options = NULL;
    Error *err = NULL;

    blk = qmp_get_blk(device, id, errp);
    if (!blk) {
        goto fail;
    }

    if (blk_bs(blk)) {
        blk_update_root_state(blk);
    }

    bdrv_flags = blk_get_open_flags_from_root_state(blk);
    bdrv_flags &= ~(BDRV_O_TEMPORARY | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING |
        BDRV_O_PROTOCOL | BDRV_O_AUTO_RDONLY);

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

    options = qdict_new();
    detect_zeroes = blk_get_detect_zeroes_from_root_state(blk);
    qdict_put_str(options, "detect-zeroes", detect_zeroes ? "on" : "off");

    if (format) {
        qdict_put_str(options, "driver", format);
    }

    aio_context_acquire(qemu_get_aio_context());
    medium_bs = bdrv_open(filename, NULL, options, bdrv_flags, errp);
    aio_context_release(qemu_get_aio_context());

    if (!medium_bs) {
        goto fail;
    }

    rc = do_open_tray(device, id, force, &err);
    if (rc && rc != -ENOSYS) {
        error_propagate(errp, err);
        goto fail;
    }
    error_free(err);
    err = NULL;

    blockdev_remove_medium(device, id, &err);
    if (err) {
        error_propagate(errp, err);
        goto fail;
    }

    qmp_blockdev_insert_anon_medium(blk, medium_bs, &err);
    if (err) {
        error_propagate(errp, err);
        goto fail;
    }

    qmp_blockdev_close_tray(device, id, errp);

fail:
    /* If the medium has been inserted, the device has its own reference, so
     * ours must be relinquished; and if it has not been inserted successfully,
     * the reference must be relinquished anyway */
    bdrv_unref(medium_bs);
}

void qmp_eject(const char *device, const char *id,
               bool has_force, bool force, Error **errp)
{
    Error *local_err = NULL;
    int rc;

    if (!has_force) {
        force = false;
    }

    rc = do_open_tray(device, id, force, &local_err);
    if (rc && rc != -ENOSYS) {
        error_propagate(errp, local_err);
        return;
    }
    error_free(local_err);

    blockdev_remove_medium(device, id, errp);
}

/* throttling disk I/O limits */
void qmp_block_set_io_throttle(BlockIOThrottle *arg, Error **errp)
{
    ThrottleConfig cfg;
    BlockDriverState *bs;
    BlockBackend *blk;
    AioContext *aio_context;

    blk = qmp_get_blk(arg->device, arg->id, errp);
    if (!blk) {
        return;
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    bs = blk_bs(blk);
    if (!bs) {
        error_setg(errp, "Device has no medium");
        goto out;
    }

    throttle_config_init(&cfg);
    cfg.buckets[THROTTLE_BPS_TOTAL].avg = arg->bps;
    cfg.buckets[THROTTLE_BPS_READ].avg  = arg->bps_rd;
    cfg.buckets[THROTTLE_BPS_WRITE].avg = arg->bps_wr;

    cfg.buckets[THROTTLE_OPS_TOTAL].avg = arg->iops;
    cfg.buckets[THROTTLE_OPS_READ].avg  = arg->iops_rd;
    cfg.buckets[THROTTLE_OPS_WRITE].avg = arg->iops_wr;

    if (arg->has_bps_max) {
        cfg.buckets[THROTTLE_BPS_TOTAL].max = arg->bps_max;
    }
    if (arg->has_bps_rd_max) {
        cfg.buckets[THROTTLE_BPS_READ].max = arg->bps_rd_max;
    }
    if (arg->has_bps_wr_max) {
        cfg.buckets[THROTTLE_BPS_WRITE].max = arg->bps_wr_max;
    }
    if (arg->has_iops_max) {
        cfg.buckets[THROTTLE_OPS_TOTAL].max = arg->iops_max;
    }
    if (arg->has_iops_rd_max) {
        cfg.buckets[THROTTLE_OPS_READ].max = arg->iops_rd_max;
    }
    if (arg->has_iops_wr_max) {
        cfg.buckets[THROTTLE_OPS_WRITE].max = arg->iops_wr_max;
    }

    if (arg->has_bps_max_length) {
        cfg.buckets[THROTTLE_BPS_TOTAL].burst_length = arg->bps_max_length;
    }
    if (arg->has_bps_rd_max_length) {
        cfg.buckets[THROTTLE_BPS_READ].burst_length = arg->bps_rd_max_length;
    }
    if (arg->has_bps_wr_max_length) {
        cfg.buckets[THROTTLE_BPS_WRITE].burst_length = arg->bps_wr_max_length;
    }
    if (arg->has_iops_max_length) {
        cfg.buckets[THROTTLE_OPS_TOTAL].burst_length = arg->iops_max_length;
    }
    if (arg->has_iops_rd_max_length) {
        cfg.buckets[THROTTLE_OPS_READ].burst_length = arg->iops_rd_max_length;
    }
    if (arg->has_iops_wr_max_length) {
        cfg.buckets[THROTTLE_OPS_WRITE].burst_length = arg->iops_wr_max_length;
    }

    if (arg->has_iops_size) {
        cfg.op_size = arg->iops_size;
    }

    if (!throttle_is_valid(&cfg, errp)) {
        goto out;
    }

    if (throttle_enabled(&cfg)) {
        /* Enable I/O limits if they're not enabled yet, otherwise
         * just update the throttling group. */
        if (!blk_get_public(blk)->throttle_group_member.throttle_state) {
            blk_io_limits_enable(blk, arg->group ?: arg->device ?: arg->id);
        } else if (arg->group) {
            blk_io_limits_update_group(blk, arg->group);
        }
        /* Set the new throttling configuration */
        blk_set_io_limits(blk, &cfg);
    } else if (blk_get_public(blk)->throttle_group_member.throttle_state) {
        /* If all throttling settings are set to 0, disable I/O limits */
        blk_io_limits_disable(blk);
    }

out:
    aio_context_release(aio_context);
}

void qmp_block_latency_histogram_set(
    const char *id,
    bool has_boundaries, uint64List *boundaries,
    bool has_boundaries_read, uint64List *boundaries_read,
    bool has_boundaries_write, uint64List *boundaries_write,
    bool has_boundaries_append, uint64List *boundaries_append,
    bool has_boundaries_flush, uint64List *boundaries_flush,
    Error **errp)
{
    BlockBackend *blk = qmp_get_blk(NULL, id, errp);
    BlockAcctStats *stats;
    int ret;

    if (!blk) {
        return;
    }

    stats = blk_get_stats(blk);

    if (!has_boundaries && !has_boundaries_read && !has_boundaries_write &&
        !has_boundaries_flush)
    {
        block_latency_histograms_clear(stats);
        return;
    }

    if (has_boundaries || has_boundaries_read) {
        ret = block_latency_histogram_set(
            stats, BLOCK_ACCT_READ,
            has_boundaries_read ? boundaries_read : boundaries);
        if (ret) {
            error_setg(errp, "Device '%s' set read boundaries fail", id);
            return;
        }
    }

    if (has_boundaries || has_boundaries_write) {
        ret = block_latency_histogram_set(
            stats, BLOCK_ACCT_WRITE,
            has_boundaries_write ? boundaries_write : boundaries);
        if (ret) {
            error_setg(errp, "Device '%s' set write boundaries fail", id);
            return;
        }
    }

    if (has_boundaries || has_boundaries_append) {
        ret = block_latency_histogram_set(
                stats, BLOCK_ACCT_ZONE_APPEND,
                has_boundaries_append ? boundaries_append : boundaries);
        if (ret) {
            error_setg(errp, "Device '%s' set append write boundaries fail", id);
            return;
        }
    }

    if (has_boundaries || has_boundaries_flush) {
        ret = block_latency_histogram_set(
            stats, BLOCK_ACCT_FLUSH,
            has_boundaries_flush ? boundaries_flush : boundaries);
        if (ret) {
            error_setg(errp, "Device '%s' set flush boundaries fail", id);
            return;
        }
    }
}
