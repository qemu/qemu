/*
 * Blockdev HMP commands
 *
 *  Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
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
#include "hw/boards.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qapi-commands-block-export.h"
#include "qobject/qdict.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "system/system.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "block/nbd.h"
#include "block/qapi.h"
#include "block/block_int.h"
#include "block/block-hmp-cmds.h"
#include "qemu-io.h"

static void hmp_drive_add_node(Monitor *mon, const char *optstr)
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
        qobject_unref(qdict);
        error_report("'node-name' needs to be specified");
        goto out;
    }

    BlockDriverState *bs = bds_tree_init(qdict, &local_err);
    if (!bs) {
        error_report_err(local_err);
        goto out;
    }

    bdrv_set_monitor_owned(bs);
out:
    qemu_opts_del(opts);
}

void hmp_drive_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    DriveInfo *dinfo;
    QemuOpts *opts;
    MachineClass *mc;
    const char *optstr = qdict_get_str(qdict, "opts");
    bool node = qdict_get_try_bool(qdict, "node", false);

    if (node) {
        hmp_drive_add_node(mon, optstr);
        return;
    }

    opts = qemu_opts_parse_noisily(qemu_find_opts("drive"), optstr, false);
    if (!opts)
        return;

    mc = MACHINE_GET_CLASS(current_machine);
    dinfo = drive_new(opts, mc->block_default_type, &err);
    if (err) {
        error_report_err(err);
        qemu_opts_del(opts);
        goto err;
    }

    if (!dinfo) {
        return;
    }

    switch (dinfo->type) {
    case IF_NONE:
        monitor_printf(mon, "OK\n");
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", dinfo->type);
        goto err;
    }
    return;

err:
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

void hmp_drive_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    BlockBackend *blk;
    BlockDriverState *bs;
    Error *local_err = NULL;

    GLOBAL_STATE_CODE();
    bdrv_graph_rdlock_main_loop();

    bs = bdrv_find_node(id);
    if (bs) {
        qmp_blockdev_del(id, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
        goto unlock;
    }

    blk = blk_by_name(id);
    if (!blk) {
        error_report("Device '%s' not found", id);
        goto unlock;
    }

    if (!blk_legacy_dinfo(blk)) {
        error_report("Deleting device added with blockdev-add"
                     " is not supported");
        goto unlock;
    }

    bs = blk_bs(blk);
    if (bs) {
        if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_DRIVE_DEL, &local_err)) {
            error_report_err(local_err);
            goto unlock;
        }

        bdrv_graph_rdunlock_main_loop();
        blk_remove_bs(blk);
        bdrv_graph_rdlock_main_loop();
    }

    /* Make the BlockBackend and the attached BlockDriverState anonymous */
    monitor_remove_blk(blk);

    /*
     * If this BlockBackend has a device attached to it, its refcount will be
     * decremented when the device is removed; otherwise we have to do so here.
     */
    if (blk_get_attached_dev(blk)) {
        /* Further I/O must not pause the guest */
        blk_set_on_error(blk, BLOCKDEV_ON_ERROR_REPORT,
                         BLOCKDEV_ON_ERROR_REPORT);
    } else {
        blk_unref(blk);
    }

unlock:
    bdrv_graph_rdunlock_main_loop();
}

void hmp_commit(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    BlockBackend *blk;
    int ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!strcmp(device, "all")) {
        ret = blk_commit_all();
    } else {
        BlockDriverState *bs;

        blk = blk_by_name(device);
        if (!blk) {
            error_report("Device '%s' not found", device);
            return;
        }

        bs = bdrv_skip_implicit_filters(blk_bs(blk));

        if (!blk_is_available(blk)) {
            error_report("Device '%s' has no medium", device);
            return;
        }

        ret = bdrv_commit(bs);
    }
    if (ret < 0) {
        error_report("'commit' error for '%s': %s", device, strerror(-ret));
    }
}

void hmp_drive_mirror(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    Error *err = NULL;
    DriveMirror mirror = {
        .device = (char *)qdict_get_str(qdict, "device"),
        .target = (char *)filename,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .unmap = true,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        goto end;
    }
    qmp_drive_mirror(&mirror, &err);
end:
    hmp_handle_error(mon, err);
}

void hmp_drive_backup(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    bool compress = qdict_get_try_bool(qdict, "compress", false);
    Error *err = NULL;
    DriveBackup backup = {
        .device = (char *)device,
        .target = (char *)filename,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .has_compress = !!compress,
        .compress = compress,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        goto end;
    }

    qmp_drive_backup(&backup, &err);
end:
    hmp_handle_error(mon, err);
}

void hmp_block_job_set_speed(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    int64_t value = qdict_get_int(qdict, "speed");

    qmp_block_job_set_speed(device, value, &error);

    hmp_handle_error(mon, error);
}

void hmp_block_job_cancel(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    bool force = qdict_get_try_bool(qdict, "force", false);

    qmp_block_job_cancel(device, true, force, &error);

    hmp_handle_error(mon, error);
}

void hmp_block_job_pause(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_pause(device, &error);

    hmp_handle_error(mon, error);
}

void hmp_block_job_resume(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_resume(device, &error);

    hmp_handle_error(mon, error);
}

void hmp_block_job_complete(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_complete(device, &error);

    hmp_handle_error(mon, error);
}

void hmp_snapshot_blkdev(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_try_str(qdict, "snapshot-file");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    enum NewImageMode mode;
    Error *err = NULL;

    if (!filename) {
        /*
         * In the future, if 'snapshot-file' is not specified, the snapshot
         * will be taken internally. Today it's actually required.
         */
        error_setg(&err, QERR_MISSING_PARAMETER, "snapshot-file");
        goto end;
    }

    mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    qmp_blockdev_snapshot_sync(device, NULL, filename, NULL, format,
                               true, mode, &err);
end:
    hmp_handle_error(mon, err);
}

void hmp_snapshot_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    qmp_blockdev_snapshot_internal_sync(device, name, &err);
    hmp_handle_error(mon, err);
}

void hmp_snapshot_delete_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    const char *id = qdict_get_try_str(qdict, "id");
    Error *err = NULL;

    qmp_blockdev_snapshot_delete_internal_sync(device, id, name, &err);
    hmp_handle_error(mon, err);
}

void hmp_nbd_server_start(Monitor *mon, const QDict *qdict)
{
    const char *uri = qdict_get_str(qdict, "uri");
    bool writable = qdict_get_try_bool(qdict, "writable", false);
    bool all = qdict_get_try_bool(qdict, "all", false);
    Error *local_err = NULL;
    BlockInfoList *block_list, *info;
    SocketAddress *addr;
    NbdServerAddOptions export;

    if (writable && !all) {
        error_setg(&local_err, "-w only valid together with -a");
        goto exit;
    }

    /* First check if the address is valid and start the server.  */
    addr = socket_parse(uri, &local_err);
    if (local_err != NULL) {
        goto exit;
    }

    nbd_server_start(addr, NBD_DEFAULT_HANDSHAKE_MAX_SECS, NULL, NULL,
                     NBD_DEFAULT_MAX_CONNECTIONS, &local_err);
    qapi_free_SocketAddress(addr);
    if (local_err != NULL) {
        goto exit;
    }

    if (!all) {
        return;
    }

    /* Then try adding all block devices.  If one fails, close all and
     * exit.
     */
    block_list = qmp_query_block(NULL);

    for (info = block_list; info; info = info->next) {
        if (!info->value->inserted) {
            continue;
        }

        export = (NbdServerAddOptions) {
            .device         = info->value->device,
            .has_writable   = true,
            .writable       = writable,
        };

        qmp_nbd_server_add(&export, &local_err);

        if (local_err != NULL) {
            qmp_nbd_server_stop(NULL);
            break;
        }
    }

    qapi_free_BlockInfoList(block_list);

exit:
    hmp_handle_error(mon, local_err);
}

void hmp_nbd_server_add(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_try_str(qdict, "name");
    bool writable = qdict_get_try_bool(qdict, "writable", false);
    Error *local_err = NULL;

    NbdServerAddOptions export = {
        .device         = (char *) device,
        .name           = (char *) name,
        .has_writable   = true,
        .writable       = writable,
    };

    qmp_nbd_server_add(&export, &local_err);
    hmp_handle_error(mon, local_err);
}

void hmp_nbd_server_remove(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    bool force = qdict_get_try_bool(qdict, "force", false);
    Error *err = NULL;

    /* Rely on BLOCK_EXPORT_REMOVE_MODE_SAFE being the default */
    qmp_nbd_server_remove(name, force, BLOCK_EXPORT_REMOVE_MODE_HARD, &err);
    hmp_handle_error(mon, err);
}

void hmp_nbd_server_stop(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_nbd_server_stop(&err);
    hmp_handle_error(mon, err);
}

void coroutine_fn hmp_block_resize(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    int64_t size = qdict_get_int(qdict, "size");
    Error *err = NULL;

    qmp_block_resize(device, NULL, size, &err);
    hmp_handle_error(mon, err);
}

void hmp_block_stream(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    const char *base = qdict_get_try_str(qdict, "base");
    int64_t speed = qdict_get_try_int(qdict, "speed", 0);

    qmp_block_stream(device, device, base, NULL, NULL, false, false, NULL,
                     qdict_haskey(qdict, "speed"), speed,
                     true, BLOCKDEV_ON_ERROR_REPORT, NULL,
                     false, false, false, false, &error);

    hmp_handle_error(mon, error);
}

void hmp_block_set_io_throttle(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    char *device = (char *) qdict_get_str(qdict, "device");
    BlockIOThrottle throttle = {
        .bps = qdict_get_int(qdict, "bps"),
        .bps_rd = qdict_get_int(qdict, "bps_rd"),
        .bps_wr = qdict_get_int(qdict, "bps_wr"),
        .iops = qdict_get_int(qdict, "iops"),
        .iops_rd = qdict_get_int(qdict, "iops_rd"),
        .iops_wr = qdict_get_int(qdict, "iops_wr"),
    };

    /*
     * qmp_block_set_io_throttle has separate parameters for the
     * (deprecated) block device name and the qdev ID but the HMP
     * version has only one, so we must decide which one to pass.
     */
    if (blk_by_name(device)) {
        throttle.device = device;
    } else {
        throttle.id = device;
    }

    qmp_block_set_io_throttle(&throttle, &err);
    hmp_handle_error(mon, err);
}

void hmp_eject(Monitor *mon, const QDict *qdict)
{
    bool force = qdict_get_try_bool(qdict, "force", false);
    const char *device = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(device, NULL, true, force, &err);
    hmp_handle_error(mon, err);
}

void hmp_qemu_io(Monitor *mon, const QDict *qdict)
{
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    BlockBackend *local_blk = NULL;
    bool qdev = qdict_get_try_bool(qdict, "qdev", false);
    const char *device = qdict_get_str(qdict, "device");
    const char *command = qdict_get_str(qdict, "command");
    Error *err = NULL;
    int ret;

    if (qdev) {
        blk = blk_by_qdev_id(device, &err);
        if (!blk) {
            goto fail;
        }
    } else {
        blk = blk_by_name(device);
        if (!blk) {
            bs = bdrv_lookup_bs(NULL, device, &err);
            if (!bs) {
                goto fail;
            }
        }
    }

    if (bs) {
        blk = local_blk = blk_new(bdrv_get_aio_context(bs), 0, BLK_PERM_ALL);
        ret = blk_insert_bs(blk, bs, &err);
        if (ret < 0) {
            goto fail;
        }
    }

    /*
     * Notably absent: Proper permission management. This is sad, but it seems
     * almost impossible to achieve without changing the semantics and thereby
     * limiting the use cases of the qemu-io HMP command.
     *
     * In an ideal world we would unconditionally create a new BlockBackend for
     * qemuio_command(), but we have commands like 'reopen' and want them to
     * take effect on the exact BlockBackend whose name the user passed instead
     * of just on a temporary copy of it.
     *
     * Another problem is that deleting the temporary BlockBackend involves
     * draining all requests on it first, but some qemu-iotests cases want to
     * issue multiple aio_read/write requests and expect them to complete in
     * the background while the monitor has already returned.
     *
     * This is also what prevents us from saving the original permissions and
     * restoring them later: We can't revoke permissions until all requests
     * have completed, and we don't know when that is nor can we really let
     * anything else run before we have revoken them to avoid race conditions.
     *
     * What happens now is that command() in qemu-io-cmds.c can extend the
     * permissions if necessary for the qemu-io command. And they simply stay
     * extended, possibly resulting in a read-only guest device keeping write
     * permissions. Ugly, but it appears to be the lesser evil.
     */
    qemuio_command(blk, command);

fail:
    blk_unref(local_blk);
    hmp_handle_error(mon, err);
}

static void print_block_info(Monitor *mon, BlockInfo *info,
                             BlockDeviceInfo *inserted, bool verbose)
{
    ImageInfo *image_info;

    assert(!info || !info->inserted || info->inserted == inserted);

    if (info && *info->device) {
        monitor_puts(mon, info->device);
        if (inserted && inserted->node_name) {
            monitor_printf(mon, " (%s)", inserted->node_name);
        }
    } else {
        assert(info || inserted);
        monitor_puts(mon,
                     inserted && inserted->node_name ? inserted->node_name
                     : info && info->qdev ? info->qdev
                     : "<anonymous>");
    }

    if (inserted) {
        monitor_printf(mon, ": %s (%s%s%s%s)\n",
                       inserted->file,
                       inserted->drv,
                       inserted->ro ? ", read-only" : "",
                       inserted->encrypted ? ", encrypted" : "",
                       inserted->active ? "" : ", inactive");
    } else {
        monitor_printf(mon, ": [not inserted]\n");
    }

    if (info) {
        if (info->qdev) {
            monitor_printf(mon, "    Attached to:      %s\n", info->qdev);
        }
        if (info->has_io_status && info->io_status != BLOCK_DEVICE_IO_STATUS_OK) {
            monitor_printf(mon, "    I/O status:       %s\n",
                           BlockDeviceIoStatus_str(info->io_status));
        }

        if (info->removable) {
            monitor_printf(mon, "    Removable device: %slocked, tray %s\n",
                           info->locked ? "" : "not ",
                           info->tray_open ? "open" : "closed");
        }
    }


    if (!inserted) {
        return;
    }

    monitor_printf(mon, "    Cache mode:       %s%s%s\n",
                   inserted->cache->writeback ? "writeback" : "writethrough",
                   inserted->cache->direct ? ", direct" : "",
                   inserted->cache->no_flush ? ", ignore flushes" : "");

    if (inserted->backing_file) {
        monitor_printf(mon,
                       "    Backing file:     %s "
                       "(chain depth: %" PRId64 ")\n",
                       inserted->backing_file,
                       inserted->backing_file_depth);
    }

    if (inserted->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF) {
        monitor_printf(mon, "    Detect zeroes:    %s\n",
                BlockdevDetectZeroesOptions_str(inserted->detect_zeroes));
    }

    if (inserted->bps  || inserted->bps_rd  || inserted->bps_wr  ||
        inserted->iops || inserted->iops_rd || inserted->iops_wr)
    {
        monitor_printf(mon, "    I/O throttling:   bps=%" PRId64
                        " bps_rd=%" PRId64  " bps_wr=%" PRId64
                        " bps_max=%" PRId64
                        " bps_rd_max=%" PRId64
                        " bps_wr_max=%" PRId64
                        " iops=%" PRId64 " iops_rd=%" PRId64
                        " iops_wr=%" PRId64
                        " iops_max=%" PRId64
                        " iops_rd_max=%" PRId64
                        " iops_wr_max=%" PRId64
                        " iops_size=%" PRId64
                        " group=%s\n",
                        inserted->bps,
                        inserted->bps_rd,
                        inserted->bps_wr,
                        inserted->bps_max,
                        inserted->bps_rd_max,
                        inserted->bps_wr_max,
                        inserted->iops,
                        inserted->iops_rd,
                        inserted->iops_wr,
                        inserted->iops_max,
                        inserted->iops_rd_max,
                        inserted->iops_wr_max,
                        inserted->iops_size,
                        inserted->group);
    }

    if (verbose) {
        monitor_printf(mon, "\nImages:\n");
        image_info = inserted->image;
        while (1) {
            bdrv_node_info_dump(qapi_ImageInfo_base(image_info), 0, false);
            if (image_info->backing_image) {
                image_info = image_info->backing_image;
            } else {
                break;
            }
        }
    }
}

void hmp_info_block(Monitor *mon, const QDict *qdict)
{
    BlockInfoList *block_list, *info;
    BlockDeviceInfoList *blockdev_list, *blockdev;
    const char *device = qdict_get_try_str(qdict, "device");
    bool verbose = qdict_get_try_bool(qdict, "verbose", false);
    bool nodes = qdict_get_try_bool(qdict, "nodes", false);
    bool printed = false;

    /* Print BlockBackend information */
    if (!nodes) {
        block_list = qmp_query_block(NULL);
    } else {
        block_list = NULL;
    }

    for (info = block_list; info; info = info->next) {
        if (device && strcmp(device, info->value->device)) {
            continue;
        }

        if (info != block_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, info->value, info->value->inserted,
                         verbose);
        printed = true;
    }

    qapi_free_BlockInfoList(block_list);

    if ((!device && !nodes) || printed) {
        return;
    }

    /* Print node information */
    blockdev_list = qmp_query_named_block_nodes(false, false, NULL);
    for (blockdev = blockdev_list; blockdev; blockdev = blockdev->next) {
        assert(blockdev->value->node_name);
        if (device && strcmp(device, blockdev->value->node_name)) {
            continue;
        }

        if (blockdev != blockdev_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, NULL, blockdev->value, verbose);
    }
    qapi_free_BlockDeviceInfoList(blockdev_list);
}

void hmp_info_blockstats(Monitor *mon, const QDict *qdict)
{
    BlockStatsList *stats_list, *stats;

    stats_list = qmp_query_blockstats(false, false, NULL);

    for (stats = stats_list; stats; stats = stats->next) {
        if (!stats->value->device) {
            continue;
        }

        monitor_printf(mon, "%s:", stats->value->device);
        monitor_printf(mon, " rd_bytes=%" PRId64
                       " wr_bytes=%" PRId64
                       " rd_operations=%" PRId64
                       " wr_operations=%" PRId64
                       " flush_operations=%" PRId64
                       " wr_total_time_ns=%" PRId64
                       " rd_total_time_ns=%" PRId64
                       " flush_total_time_ns=%" PRId64
                       " rd_merged=%" PRId64
                       " wr_merged=%" PRId64
                       " idle_time_ns=%" PRId64
                       "\n",
                       stats->value->stats->rd_bytes,
                       stats->value->stats->wr_bytes,
                       stats->value->stats->rd_operations,
                       stats->value->stats->wr_operations,
                       stats->value->stats->flush_operations,
                       stats->value->stats->wr_total_time_ns,
                       stats->value->stats->rd_total_time_ns,
                       stats->value->stats->flush_total_time_ns,
                       stats->value->stats->rd_merged,
                       stats->value->stats->wr_merged,
                       stats->value->stats->idle_time_ns);
    }

    qapi_free_BlockStatsList(stats_list);
}

void hmp_info_block_jobs(Monitor *mon, const QDict *qdict)
{
    BlockJobInfoList *list;

    list = qmp_query_block_jobs(&error_abort);

    if (!list) {
        monitor_printf(mon, "No active jobs\n");
        return;
    }

    while (list) {
        if (list->value->type == JOB_TYPE_STREAM) {
            monitor_printf(mon, "Streaming device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        } else {
            monitor_printf(mon, "Type %s, device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           JobType_str(list->value->type),
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        }
        list = list->next;
    }

    qapi_free_BlockJobInfoList(list);
}

void hmp_info_snapshots(Monitor *mon, const QDict *qdict)
{
    BlockDriverState *bs, *bs1;
    BdrvNextIterator it1;
    QEMUSnapshotInfo *sn_tab, *sn;
    bool no_snapshot = true;
    int nb_sns, i;
    int total;
    int *global_snapshots;

    typedef struct SnapshotEntry {
        QEMUSnapshotInfo sn;
        QTAILQ_ENTRY(SnapshotEntry) next;
    } SnapshotEntry;

    typedef struct ImageEntry {
        const char *imagename;
        QTAILQ_ENTRY(ImageEntry) next;
        QTAILQ_HEAD(, SnapshotEntry) snapshots;
    } ImageEntry;

    QTAILQ_HEAD(, ImageEntry) image_list =
        QTAILQ_HEAD_INITIALIZER(image_list);

    ImageEntry *image_entry, *next_ie;
    SnapshotEntry *snapshot_entry;
    Error *err = NULL;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs = bdrv_all_find_vmstate_bs(NULL, false, NULL, &err);
    if (!bs) {
        error_report_err(err);
        return;
    }

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);

    if (nb_sns < 0) {
        monitor_printf(mon, "bdrv_snapshot_list: error %d\n", nb_sns);
        return;
    }

    for (bs1 = bdrv_first(&it1); bs1; bs1 = bdrv_next(&it1)) {
        int bs1_nb_sns = 0;
        ImageEntry *ie;
        SnapshotEntry *se;

        if (bdrv_can_snapshot(bs1)) {
            sn = NULL;
            bs1_nb_sns = bdrv_snapshot_list(bs1, &sn);
            if (bs1_nb_sns > 0) {
                no_snapshot = false;
                ie = g_new0(ImageEntry, 1);
                ie->imagename = bdrv_get_device_name(bs1);
                QTAILQ_INIT(&ie->snapshots);
                QTAILQ_INSERT_TAIL(&image_list, ie, next);
                for (i = 0; i < bs1_nb_sns; i++) {
                    se = g_new0(SnapshotEntry, 1);
                    se->sn = sn[i];
                    QTAILQ_INSERT_TAIL(&ie->snapshots, se, next);
                }
            }
            g_free(sn);
        }
    }

    if (no_snapshot) {
        monitor_printf(mon, "There is no snapshot available.\n");
        return;
    }

    global_snapshots = g_new0(int, nb_sns);
    total = 0;
    for (i = 0; i < nb_sns; i++) {
        SnapshotEntry *next_sn;
        if (bdrv_all_has_snapshot(sn_tab[i].name, false, NULL, NULL) == 1) {
            global_snapshots[total] = i;
            total++;
            QTAILQ_FOREACH(image_entry, &image_list, next) {
                QTAILQ_FOREACH_SAFE(snapshot_entry, &image_entry->snapshots,
                                    next, next_sn) {
                    if (!strcmp(sn_tab[i].name, snapshot_entry->sn.name)) {
                        QTAILQ_REMOVE(&image_entry->snapshots, snapshot_entry,
                                      next);
                        g_free(snapshot_entry);
                    }
                }
            }
        }
    }
    monitor_printf(mon, "List of snapshots present on all disks:\n");

    if (total > 0) {
        bdrv_snapshot_dump(NULL);
        monitor_printf(mon, "\n");
        for (i = 0; i < total; i++) {
            sn = &sn_tab[global_snapshots[i]];
            /*
             * The ID is not guaranteed to be the same on all images, so
             * overwrite it.
             */
            pstrcpy(sn->id_str, sizeof(sn->id_str), "--");
            bdrv_snapshot_dump(sn);
            monitor_printf(mon, "\n");
        }
    } else {
        monitor_printf(mon, "None\n");
    }

    QTAILQ_FOREACH(image_entry, &image_list, next) {
        if (QTAILQ_EMPTY(&image_entry->snapshots)) {
            continue;
        }
        monitor_printf(mon,
                       "\nList of partial (non-loadable) snapshots on '%s':\n",
                       image_entry->imagename);
        bdrv_snapshot_dump(NULL);
        monitor_printf(mon, "\n");
        QTAILQ_FOREACH(snapshot_entry, &image_entry->snapshots, next) {
            bdrv_snapshot_dump(&snapshot_entry->sn);
            monitor_printf(mon, "\n");
        }
    }

    QTAILQ_FOREACH_SAFE(image_entry, &image_list, next, next_ie) {
        SnapshotEntry *next_sn;
        QTAILQ_FOREACH_SAFE(snapshot_entry, &image_entry->snapshots, next,
                            next_sn) {
            g_free(snapshot_entry);
        }
        g_free(image_entry);
    }
    g_free(sn_tab);
    g_free(global_snapshots);
}

void hmp_change_medium(Monitor *mon, const char *device, const char *target,
                       const char *arg, const char *read_only, bool force,
                       Error **errp)
{
    ERRP_GUARD();
    BlockdevChangeReadOnlyMode read_only_mode = 0;

    if (read_only) {
        read_only_mode =
            qapi_enum_parse(&BlockdevChangeReadOnlyMode_lookup,
                            read_only,
                            BLOCKDEV_CHANGE_READ_ONLY_MODE_RETAIN, errp);
        if (*errp) {
            return;
        }
    }

    qmp_blockdev_change_medium(device, NULL, target, arg, true, force,
                               !!read_only, read_only_mode, errp);
}
