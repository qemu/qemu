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
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "sysemu/sysemu.h"
#include "block/nbd.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"

typedef struct NBDServerData {
    QIONetListener *listener;
    QCryptoTLSCreds *tlscreds;
} NBDServerData;

static NBDServerData *nbd_server;

static void nbd_blockdev_client_closed(NBDClient *client, bool ignored)
{
    nbd_client_put(client);
}

static void nbd_accept(QIONetListener *listener, QIOChannelSocket *cioc,
                       gpointer opaque)
{
    qio_channel_set_name(QIO_CHANNEL(cioc), "nbd-server");
    nbd_client_new(cioc, nbd_server->tlscreds, NULL,
                   nbd_blockdev_client_closed);
}


static void nbd_server_free(NBDServerData *server)
{
    if (!server) {
        return;
    }

    qio_net_listener_disconnect(server->listener);
    object_unref(OBJECT(server->listener));
    if (server->tlscreds) {
        object_unref(OBJECT(server->tlscreds));
    }

    g_free(server);
}

static QCryptoTLSCreds *nbd_get_tls_creds(const char *id, Error **errp)
{
    Object *obj;
    QCryptoTLSCreds *creds;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        error_setg(errp, "No TLS credentials with id '%s'",
                   id);
        return NULL;
    }
    creds = (QCryptoTLSCreds *)
        object_dynamic_cast(obj, TYPE_QCRYPTO_TLS_CREDS);
    if (!creds) {
        error_setg(errp, "Object with id '%s' is not TLS credentials",
                   id);
        return NULL;
    }

    if (creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        error_setg(errp,
                   "Expecting TLS credentials with a server endpoint");
        return NULL;
    }
    object_ref(obj);
    return creds;
}


void nbd_server_start(SocketAddress *addr, const char *tls_creds,
                      Error **errp)
{
    if (nbd_server) {
        error_setg(errp, "NBD server already running");
        return;
    }

    nbd_server = g_new0(NBDServerData, 1);
    nbd_server->listener = qio_net_listener_new();

    qio_net_listener_set_name(nbd_server->listener,
                              "nbd-listener");

    if (qio_net_listener_open_sync(nbd_server->listener, addr, errp) < 0) {
        goto error;
    }

    if (tls_creds) {
        nbd_server->tlscreds = nbd_get_tls_creds(tls_creds, errp);
        if (!nbd_server->tlscreds) {
            goto error;
        }

        /* TODO SOCKET_ADDRESS_TYPE_FD where fd has AF_INET or AF_INET6 */
        if (addr->type != SOCKET_ADDRESS_TYPE_INET) {
            error_setg(errp, "TLS is only supported with IPv4/IPv6");
            goto error;
        }
    }

    qio_net_listener_set_client_func(nbd_server->listener,
                                     nbd_accept,
                                     NULL,
                                     NULL);

    return;

 error:
    nbd_server_free(nbd_server);
    nbd_server = NULL;
}

void qmp_nbd_server_start(SocketAddressLegacy *addr,
                          bool has_tls_creds, const char *tls_creds,
                          Error **errp)
{
    SocketAddress *addr_flat = socket_address_flatten(addr);

    nbd_server_start(addr_flat, tls_creds, errp);
    qapi_free_SocketAddress(addr_flat);
}

void qmp_nbd_server_add(const char *device, bool has_name, const char *name,
                        bool has_writable, bool writable, Error **errp)
{
    BlockDriverState *bs = NULL;
    BlockBackend *on_eject_blk;
    NBDExport *exp;

    if (!nbd_server) {
        error_setg(errp, "NBD server not running");
        return;
    }

    if (!has_name) {
        name = device;
    }

    if (nbd_export_find(name)) {
        error_setg(errp, "NBD server already has export named '%s'", name);
        return;
    }

    on_eject_blk = blk_by_name(device);

    bs = bdrv_lookup_bs(device, device, errp);
    if (!bs) {
        return;
    }

    if (!has_writable) {
        writable = false;
    }
    if (bdrv_is_read_only(bs)) {
        writable = false;
    }

    exp = nbd_export_new(bs, 0, -1, writable ? 0 : NBD_FLAG_READ_ONLY,
                         NULL, false, on_eject_blk, errp);
    if (!exp) {
        return;
    }

    nbd_export_set_name(exp, name);

    /* The list of named exports has a strong reference to this export now and
     * our only way of accessing it is through nbd_export_find(), so we can drop
     * the strong reference that is @exp. */
    nbd_export_put(exp);
}

void qmp_nbd_server_remove(const char *name,
                           bool has_mode, NbdServerRemoveMode mode,
                           Error **errp)
{
    NBDExport *exp;

    if (!nbd_server) {
        error_setg(errp, "NBD server not running");
        return;
    }

    exp = nbd_export_find(name);
    if (exp == NULL) {
        error_setg(errp, "Export '%s' is not found", name);
        return;
    }

    if (!has_mode) {
        mode = NBD_SERVER_REMOVE_MODE_SAFE;
    }

    nbd_export_remove(exp, mode, errp);
}

void qmp_nbd_server_stop(Error **errp)
{
    nbd_export_close_all();

    nbd_server_free(nbd_server);
    nbd_server = NULL;
}

void qmp_x_nbd_server_add_bitmap(const char *name, const char *bitmap,
                                 bool has_bitmap_export_name,
                                 const char *bitmap_export_name,
                                 Error **errp)
{
    NBDExport *exp;

    if (!nbd_server) {
        error_setg(errp, "NBD server not running");
        return;
    }

    exp = nbd_export_find(name);
    if (exp == NULL) {
        error_setg(errp, "Export '%s' is not found", name);
        return;
    }

    nbd_export_bitmap(exp, bitmap,
                      has_bitmap_export_name ? bitmap_export_name : bitmap,
                      errp);
}
