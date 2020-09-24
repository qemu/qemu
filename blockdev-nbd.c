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
static bool is_qemu_nbd;

static void nbd_update_server_watch(NBDServerData *s);

void nbd_server_is_qemu_nbd(bool value)
{
    is_qemu_nbd = value;
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

    if (creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        error_setg(errp,
                   "Expecting TLS credentials with a server endpoint");
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

    if (qio_net_listener_open_sync(nbd_server->listener, addr, 1, errp) < 0) {
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

int nbd_export_create(BlockExport *exp, BlockExportOptions *exp_args,
                      Error **errp)
{
    BlockExportOptionsNbd *arg = &exp_args->u.nbd;
    BlockDriverState *bs = NULL;
    AioContext *aio_context;
    int ret;

    assert(exp_args->type == BLOCK_EXPORT_TYPE_NBD);

    if (!nbd_server && !is_qemu_nbd) {
        error_setg(errp, "NBD server not running");
        return -EINVAL;
    }

    if (!arg->has_name) {
        arg->name = exp_args->node_name;
    }

    if (strlen(arg->name) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "export name '%s' too long", arg->name);
        return -EINVAL;
    }

    if (arg->description && strlen(arg->description) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "description '%s' too long", arg->description);
        return -EINVAL;
    }

    if (nbd_export_find(arg->name)) {
        error_setg(errp, "NBD server already has export named '%s'", arg->name);
        return -EEXIST;
    }

    bs = bdrv_lookup_bs(NULL, exp_args->node_name, errp);
    if (!bs) {
        return -ENOENT;
    }

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);

    if (!arg->has_writable) {
        arg->writable = false;
    }
    if (bdrv_is_read_only(bs) && arg->writable) {
        ret = -EINVAL;
        error_setg(errp, "Cannot export read-only node as writable");
        goto out;
    }

    if (!exp_args->has_writethrough) {
        exp_args->writethrough = false;
    }

    ret = nbd_export_new(exp, bs, arg->name, arg->description, arg->bitmap,
                         !arg->writable, !arg->writable,
                         exp_args->writethrough, errp);
    if (ret < 0) {
        goto out;
    }

    ret = 0;
 out:
    aio_context_release(aio_context);
    return ret;
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
        arg->name = arg->device;
    }

    export_opts = g_new(BlockExportOptions, 1);
    *export_opts = (BlockExportOptions) {
        .type                   = BLOCK_EXPORT_TYPE_NBD,
        .id                     = g_strdup(arg->name),
        .node_name              = g_strdup(bdrv_get_node_name(bs)),
        .u.nbd = {
            .has_name           = true,
            .name               = g_strdup(arg->name),
            .has_description    = arg->has_description,
            .description        = g_strdup(arg->description),
            .has_writable       = arg->has_writable,
            .writable           = arg->writable,
            .has_bitmap         = arg->has_bitmap,
            .bitmap             = g_strdup(arg->bitmap),
        },
    };

    /*
     * nbd-server-add doesn't complain when a read-only device should be
     * exported as writable, but simply downgrades it. This is an error with
     * block-export-add.
     */
    if (bdrv_is_read_only(bs)) {
        export_opts->u.nbd.has_writable = true;
        export_opts->u.nbd.writable = false;
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
                           bool has_mode, NbdServerRemoveMode mode,
                           Error **errp)
{
    NBDExport *exp;
    AioContext *aio_context;

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

    aio_context = nbd_export_aio_context(exp);
    aio_context_acquire(aio_context);
    nbd_export_remove(exp, mode, errp);
    aio_context_release(aio_context);
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
