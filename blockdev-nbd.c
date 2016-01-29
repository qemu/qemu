/*
 * Serving QEMU block devices via NBD
 *
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "hw/block/block.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "trace.h"
#include "block/nbd.h"
#include "qemu/sockets.h"

static int server_fd = -1;

static void nbd_accept(void *opaque)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    int fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd >= 0) {
        nbd_client_new(NULL, fd, nbd_client_put);
    }
}

void qmp_nbd_server_start(SocketAddress *addr, Error **errp)
{
    if (server_fd != -1) {
        error_setg(errp, "NBD server already running");
        return;
    }

    server_fd = socket_listen(addr, errp);
    if (server_fd != -1) {
        qemu_set_fd_handler(server_fd, nbd_accept, NULL, NULL);
    }
}

void qmp_nbd_server_add(const char *device, bool has_writable, bool writable,
                        Error **errp)
{
    BlockBackend *blk;
    NBDExport *exp;

    if (server_fd == -1) {
        error_setg(errp, "NBD server not running");
        return;
    }

    if (nbd_export_find(device)) {
        error_setg(errp, "NBD server already exporting device '%s'", device);
        return;
    }

    blk = blk_by_name(device);
    if (!blk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device);
        return;
    }
    if (!blk_is_inserted(blk)) {
        error_setg(errp, QERR_DEVICE_HAS_NO_MEDIUM, device);
        return;
    }

    if (!has_writable) {
        writable = false;
    }
    if (blk_is_read_only(blk)) {
        writable = false;
    }

    exp = nbd_export_new(blk, 0, -1, writable ? 0 : NBD_FLAG_READ_ONLY, NULL,
                         errp);
    if (!exp) {
        return;
    }

    nbd_export_set_name(exp, device);

    /* The list of named exports has a strong reference to this export now and
     * our only way of accessing it is through nbd_export_find(), so we can drop
     * the strong reference that is @exp. */
    nbd_export_put(exp);
}

void qmp_nbd_server_stop(Error **errp)
{
    nbd_export_close_all();

    if (server_fd != -1) {
        qemu_set_fd_handler(server_fd, NULL, NULL, NULL);
        close(server_fd);
        server_fd = -1;
    }
}
