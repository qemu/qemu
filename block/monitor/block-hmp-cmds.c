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
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "block/nbd.h"
#include "block/block_int.h"
#include "block/block-hmp-cmds.h"
#include "qemu-io.h"

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

    opts = drive_def(optstr);
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
    AioContext *aio_context;
    Error *local_err = NULL;

    bs = bdrv_find_node(id);
    if (bs) {
        qmp_blockdev_del(id, &local_err);
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

    aio_context_release(aio_context);
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
            error_report("Device '%s' not found", device);
            return;
        }
        if (!blk_is_available(blk)) {
            error_report("Device '%s' has no medium", device);
            return;
        }

        bs = blk_bs(blk);
        aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);

        ret = bdrv_commit(bs);

        aio_context_release(aio_context);
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
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .unmap = true,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, err);
        return;
    }
    qmp_drive_mirror(&mirror, &err);
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
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .has_compress = !!compress,
        .compress = compress,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, err);
        return;
    }

    qmp_drive_backup(&backup, &err);
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
        hmp_handle_error(mon, err);
        return;
    }

    mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    qmp_blockdev_snapshot_sync(true, device, false, NULL,
                               filename, false, NULL,
                               !!format, format,
                               true, mode, &err);
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

    qmp_blockdev_snapshot_delete_internal_sync(device, !!id, id,
                                               true, name, &err);
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
    BlockExportNbd export;

    if (writable && !all) {
        error_setg(&local_err, "-w only valid together with -a");
        goto exit;
    }

    /* First check if the address is valid and start the server.  */
    addr = socket_parse(uri, &local_err);
    if (local_err != NULL) {
        goto exit;
    }

    nbd_server_start(addr, NULL, NULL, &local_err);
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
        if (!info->value->has_inserted) {
            continue;
        }

        export = (BlockExportNbd) {
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

    BlockExportNbd export = {
        .device         = (char *) device,
        .has_name       = !!name,
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

    /* Rely on NBD_SERVER_REMOVE_MODE_SAFE being the default */
    qmp_nbd_server_remove(name, force, NBD_SERVER_REMOVE_MODE_HARD, &err);
    hmp_handle_error(mon, err);
}

void hmp_nbd_server_stop(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_nbd_server_stop(&err);
    hmp_handle_error(mon, err);
}

void hmp_block_resize(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    int64_t size = qdict_get_int(qdict, "size");
    Error *err = NULL;

    qmp_block_resize(true, device, false, NULL, size, &err);
    hmp_handle_error(mon, err);
}

void hmp_block_stream(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    const char *base = qdict_get_try_str(qdict, "base");
    int64_t speed = qdict_get_try_int(qdict, "speed", 0);

    qmp_block_stream(true, device, device, base != NULL, base, false, NULL,
                     false, NULL, qdict_haskey(qdict, "speed"), speed, true,
                     BLOCKDEV_ON_ERROR_REPORT, false, false, false, false,
                     &error);

    hmp_handle_error(mon, error);
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_block_passwd(true, device, false, NULL, password, &err);
    hmp_handle_error(mon, err);
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
        throttle.has_device = true;
        throttle.device = device;
    } else {
        throttle.has_id = true;
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

    qmp_eject(true, device, false, NULL, true, force, &err);
    hmp_handle_error(mon, err);
}

void hmp_qemu_io(Monitor *mon, const QDict *qdict)
{
    BlockBackend *blk;
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
            BlockDriverState *bs = bdrv_lookup_bs(NULL, device, &err);
            if (bs) {
                blk = local_blk = blk_new(bdrv_get_aio_context(bs),
                                          0, BLK_PERM_ALL);
                ret = blk_insert_bs(blk, bs, &err);
                if (ret < 0) {
                    goto fail;
                }
            } else {
                goto fail;
            }
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
