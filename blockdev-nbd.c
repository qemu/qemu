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
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-block-export.h"
#include "qapi/qapi-commands-block-export.h"
#include "block/nbd.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"

typedef struct NBDServerData {
    QIONetListener *listener;
    QCryptoTLSCreds *tlscreds;
    char *tlsauthz;
    uint32_t max_connections;
    uint32_t connections;
} NBDServerData;

static NBDServerData *nbd_server;
static int qemu_nbd_connections = -1; /* Non-negative if this is qemu-nbd */

static void nbd_update_server_watch(NBDServerData *s);

void nbd_server_is_qemu_nbd(int max_connections)
{
    qemu_nbd_connections = max_connections;
}

bool nbd_server_is_running(void)
{
    return nbd_server || qemu_nbd_connections >= 0;
}

int nbd_server_max_connections(void)
{
    return nbd_server ? nbd_server->max_connections : qemu_nbd_connections;
}

static void nbd_blockdev_client_closed(NBDClient *client, bool ignored)
{
    nbd_client_put(client);
    assert(nbd_server->connections > 0);
    nbd_server->connections--;
    nbd_update_server_watch(nbd_server);
}

static void nbd_accept(QIONetListener *listener, QIOChannelSocket *cioc,
                       gpointer opaque)
{
    nbd_server->connections++;
    nbd_update_server_watch(nbd_server);

    qio_channel_set_name(QIO_CHANNEL(cioc), "nbd-server");
    nbd_client_new(cioc, nbd_server->tlscreds, nbd_server->tlsauthz,
                   nbd_blockdev_client_closed);
}

static void nbd_update_server_watch(NBDServerData *s)
{
    if (!s->max_connections || s->connections < s->max_connections) {
        qio_net_listener_set_client_func(s->listener, nbd_accept, NULL, NULL);
    } else {
        qio_net_listener_set_client_func(s->listener, NULL, NULL, NULL);
    }
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
    g_free(server->tlsauthz);

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

    if (!qcrypto_tls_creds_check_endpoint(creds,
                                          QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
                                          errp)) {
        return NULL;
    }
    object_ref(obj);
    return creds;
}


void nbd_server_start(SocketAddress *addr, const char *tls_creds,
                      const char *tls_authz, uint32_t max_connections,
                      Error **errp)
{
    if (nbd_server) {
        error_setg(errp, "NBD server already running");
        return;
    }

    nbd_server = g_new0(NBDServerData, 1);
    nbd_server->max_connections = max_connections;
    nbd_server->listener = qio_net_listener_new();

    qio_net_listener_set_name(nbd_server->listener,
                              "nbd-listener");

    /*
     * Because this server is persistent, a backlog of SOMAXCONN is
     * better than trying to size it to max_connections.
     */
    if (qio_net_listener_open_sync(nbd_server->listener, addr, SOMAXCONN,
                                   errp) < 0) {
        goto error;
    }

    if (tls_creds) {
        nbd_server->tlscreds = nbd_get_tls_creds(tls_creds, errp);
        if (!nbd_server->tlscreds) {
            goto error;
        }
    }

    nbd_server->tlsauthz = g_strdup(tls_authz);

    nbd_update_server_watch(nbd_server);

    return;

 error:
    nbd_server_free(nbd_server);
    nbd_server = NULL;
}

void nbd_server_start_options(NbdServerOptions *arg, Error **errp)
{
    nbd_server_start(arg->addr, arg->tls_creds, arg->tls_authz,
                     arg->max_connections, errp);
}

void qmp_nbd_server_start(SocketAddressLegacy *addr,
                          bool has_tls_creds, const char *tls_creds,
                          bool has_tls_authz, const char *tls_authz,
                          bool has_max_connections, uint32_t max_connections,
                          Error **errp)
{
    SocketAddress *addr_flat = socket_address_flatten(addr);

    nbd_server_start(addr_flat, tls_creds, tls_authz, max_connections, errp);
    qapi_free_SocketAddress(addr_flat);
}

void qmp_nbd_server_add(NbdServerAddOptions *arg, Error **errp)
{
    BlockExport *export;
    BlockDriverState *bs;
    BlockBackend *on_eject_blk;
    BlockExportOptions *export_opts;

    bs = bdrv_lookup_bs(arg->device, arg->device, errp);
    if (!bs) {
        return;
    }

    /*
     * block-export-add would default to the node-name, but we may have to use
     * the device name as a default here for compatibility.
     */
    if (!arg->has_name) {
        arg->has_name = true;
        arg->name = g_strdup(arg->device);
    }

    export_opts = g_new(BlockExportOptions, 1);
    *export_opts = (BlockExportOptions) {
        .type                   = BLOCK_EXPORT_TYPE_NBD,
        .id                     = g_strdup(arg->name),
        .node_name              = g_strdup(bdrv_get_node_name(bs)),
        .has_writable           = arg->has_writable,
        .writable               = arg->writable,
    };
    QAPI_CLONE_MEMBERS(BlockExportOptionsNbdBase, &export_opts->u.nbd,
                       qapi_NbdServerAddOptions_base(arg));
    if (arg->has_bitmap) {
        BlockDirtyBitmapOrStr *el = g_new(BlockDirtyBitmapOrStr, 1);

        *el = (BlockDirtyBitmapOrStr) {
            .type = QTYPE_QSTRING,
            .u.local = g_strdup(arg->bitmap),
        };
        export_opts->u.nbd.has_bitmaps = true;
        QAPI_LIST_PREPEND(export_opts->u.nbd.bitmaps, el);
    }

    /*
     * nbd-server-add doesn't complain when a read-only device should be
     * exported as writable, but simply downgrades it. This is an error with
     * block-export-add.
     */
    if (bdrv_is_read_only(bs)) {
        export_opts->has_writable = true;
        export_opts->writable = false;
    }

    export = blk_exp_add(export_opts, errp);
    if (!export) {
        goto fail;
    }

    /*
     * nbd-server-add removes the export when the named BlockBackend used for
     * @device goes away.
     */
    on_eject_blk = blk_by_name(arg->device);
    if (on_eject_blk) {
        nbd_export_set_on_eject_blk(export, on_eject_blk);
    }

fail:
    qapi_free_BlockExportOptions(export_opts);
}

void qmp_nbd_server_remove(const char *name,
                           bool has_mode, BlockExportRemoveMode mode,
                           Error **errp)
{
    BlockExport *exp;

    exp = blk_exp_find(name);
    if (exp && exp->drv->type != BLOCK_EXPORT_TYPE_NBD) {
        error_setg(errp, "Block export '%s' is not an NBD export", name);
        return;
    }

    qmp_block_export_del(name, has_mode, mode, errp);
}

void qmp_nbd_server_stop(Error **errp)
{
    if (!nbd_server) {
        error_setg(errp, "NBD server not running");
        return;
    }

    blk_exp_close_all_type(BLOCK_EXPORT_TYPE_NBD);

    nbd_server_free(nbd_server);
    nbd_server = NULL;
}
