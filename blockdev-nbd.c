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

#include "qemu/osdep.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "hw/block/block.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "trace.h"
#include "block/nbd.h"
#include "io/channel-socket.h"

static QIOChannelSocket *server_ioc;
static int server_watch = -1;

static gboolean nbd_accept(QIOChannel *ioc, GIOCondition condition,
                           gpointer opaque)
{
    QIOChannelSocket *cioc;

    cioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!cioc) {
        return TRUE;
    }

    nbd_client_new(NULL, cioc, nbd_client_put);
    object_unref(OBJECT(cioc));
    return TRUE;
}

void qmp_nbd_server_start(SocketAddress *addr, Error **errp)
{
    if (server_ioc) {
        error_setg(errp, "NBD server already running");
        return;
    }

    server_ioc = qio_channel_socket_new();
    if (qio_channel_socket_listen_sync(server_ioc, addr, errp) < 0) {
        return;
    }

    server_watch = qio_channel_add_watch(QIO_CHANNEL(server_ioc),
                                         G_IO_IN,
                                         nbd_accept,
                                         NULL,
                                         NULL);
}

void qmp_nbd_server_add(const char *device, bool has_writable, bool writable,
                        Error **errp)
{
    BlockBackend *blk;
    NBDExport *exp;

    if (!server_ioc) {
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

    if (server_watch != -1) {
        g_source_remove(server_watch);
        server_watch = -1;
    }
    if (server_ioc) {
        object_unref(OBJECT(server_ioc));
        server_ioc = NULL;
    }
}
