/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "system/block-backend.h"
#include "block/block_int.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/error.h"
#include "qemu-io.h"

void qmp_x_qemu_io(const char *device, const char *qdev,
                   const char *command, Error **errp)
{
    BlockBackend *blk = NULL;
    BlockBackend *local_blk = NULL;
    BlockDriverState *bs = NULL;
    int ret;

    if (!device && !qdev) {
        error_setg(errp, "Must specify either device or qdev");
        return;
    }
    if (qdev && device) {
        error_setg(errp, "Cannot specify both qdev and device");
        return;
    }

    if (qdev) {
        blk = blk_by_qdev_id(qdev, errp);
        if (!blk) {
            return;
        }
    } else {
        blk = blk_by_name(device);
        if (!blk) {
            bs = bdrv_lookup_bs(NULL, device, errp);
            if (!bs) {
                return;
            }
        }
    }

    if (bs) {
        blk = local_blk = blk_new(bdrv_get_aio_context(bs), 0, BLK_PERM_ALL);
        ret = blk_insert_bs(blk, bs, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    /*
     * Notably absent: Proper permission management. This is sad, but it seems
     * almost impossible to achieve without changing the semantics and thereby
     * limiting the use cases of the qemu-io command.
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
    qemuio_command(blk, command, errp);

fail:
    blk_unref(local_blk);
}
