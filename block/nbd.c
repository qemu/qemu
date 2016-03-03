/*
 * QEMU Block driver for  NBD
 *
 * Copyright (C) 2008 Bull S.A.S.
 *     Author: Laurent Vivier <Laurent.Vivier@bull.net>
 *
 * Some parts:
 *    Copyright (C) 2007 Anthony Liguori <anthony@codemonkey.ws>
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
#include "block/nbd-client.h"
#include "qemu/uri.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qstring.h"


#define EN_OPTSTR ":exportname="

typedef struct BDRVNBDState {
    NbdClientSession client;
} BDRVNBDState;

static int nbd_parse_uri(const char *filename, QDict *options)
{
    URI *uri;
    const char *p;
    QueryParams *qp = NULL;
    int ret = 0;
    bool is_unix;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    /* transport */
    if (!strcmp(uri->scheme, "nbd")) {
        is_unix = false;
    } else if (!strcmp(uri->scheme, "nbd+tcp")) {
        is_unix = false;
    } else if (!strcmp(uri->scheme, "nbd+unix")) {
        is_unix = true;
    } else {
        ret = -EINVAL;
        goto out;
    }

    p = uri->path ? uri->path : "/";
    p += strspn(p, "/");
    if (p[0]) {
        qdict_put(options, "export", qstring_from_str(p));
    }

    qp = query_params_parse(uri->query);
    if (qp->n > 1 || (is_unix && !qp->n) || (!is_unix && qp->n)) {
        ret = -EINVAL;
        goto out;
    }

    if (is_unix) {
        /* nbd+unix:///export?socket=path */
        if (uri->server || uri->port || strcmp(qp->p[0].name, "socket")) {
            ret = -EINVAL;
            goto out;
        }
        qdict_put(options, "path", qstring_from_str(qp->p[0].value));
    } else {
        QString *host;
        /* nbd[+tcp]://host[:port]/export */
        if (!uri->server) {
            ret = -EINVAL;
            goto out;
        }

        /* strip braces from literal IPv6 address */
        if (uri->server[0] == '[') {
            host = qstring_from_substr(uri->server, 1,
                                       strlen(uri->server) - 2);
        } else {
            host = qstring_from_str(uri->server);
        }

        qdict_put(options, "host", host);
        if (uri->port) {
            char* port_str = g_strdup_printf("%d", uri->port);
            qdict_put(options, "port", qstring_from_str(port_str));
            g_free(port_str);
        }
    }

out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
}

static void nbd_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    char *file;
    char *export_name;
    const char *host_spec;
    const char *unixpath;

    if (qdict_haskey(options, "host")
        || qdict_haskey(options, "port")
        || qdict_haskey(options, "path"))
    {
        error_setg(errp, "host/port/path and a file name may not be specified "
                         "at the same time");
        return;
    }

    if (strstr(filename, "://")) {
        int ret = nbd_parse_uri(filename, options);
        if (ret < 0) {
            error_setg(errp, "No valid URL specified");
        }
        return;
    }

    file = g_strdup(filename);

    export_name = strstr(file, EN_OPTSTR);
    if (export_name) {
        if (export_name[strlen(EN_OPTSTR)] == 0) {
            goto out;
        }
        export_name[0] = 0; /* truncate 'file' */
        export_name += strlen(EN_OPTSTR);

        qdict_put(options, "export", qstring_from_str(export_name));
    }

    /* extract the host_spec - fail if it's not nbd:... */
    if (!strstart(file, "nbd:", &host_spec)) {
        error_setg(errp, "File name string for NBD must start with 'nbd:'");
        goto out;
    }

    if (!*host_spec) {
        goto out;
    }

    /* are we a UNIX or TCP socket? */
    if (strstart(host_spec, "unix:", &unixpath)) {
        qdict_put(options, "path", qstring_from_str(unixpath));
    } else {
        InetSocketAddress *addr = NULL;

        addr = inet_parse(host_spec, errp);
        if (!addr) {
            goto out;
        }

        qdict_put(options, "host", qstring_from_str(addr->host));
        qdict_put(options, "port", qstring_from_str(addr->port));
        qapi_free_InetSocketAddress(addr);
    }

out:
    g_free(file);
}

static SocketAddress *nbd_config(BDRVNBDState *s, QDict *options, char **export,
                                 Error **errp)
{
    SocketAddress *saddr;

    if (qdict_haskey(options, "path") == qdict_haskey(options, "host")) {
        if (qdict_haskey(options, "path")) {
            error_setg(errp, "path and host may not be used at the same time.");
        } else {
            error_setg(errp, "one of path and host must be specified.");
        }
        return NULL;
    }

    saddr = g_new0(SocketAddress, 1);

    if (qdict_haskey(options, "path")) {
        UnixSocketAddress *q_unix;
        saddr->type = SOCKET_ADDRESS_KIND_UNIX;
        q_unix = saddr->u.q_unix = g_new0(UnixSocketAddress, 1);
        q_unix->path = g_strdup(qdict_get_str(options, "path"));
        qdict_del(options, "path");
    } else {
        InetSocketAddress *inet;
        saddr->type = SOCKET_ADDRESS_KIND_INET;
        inet = saddr->u.inet = g_new0(InetSocketAddress, 1);
        inet->host = g_strdup(qdict_get_str(options, "host"));
        if (!qdict_get_try_str(options, "port")) {
            inet->port = g_strdup_printf("%d", NBD_DEFAULT_PORT);
        } else {
            inet->port = g_strdup(qdict_get_str(options, "port"));
        }
        qdict_del(options, "host");
        qdict_del(options, "port");
    }

    s->client.is_unix = saddr->type == SOCKET_ADDRESS_KIND_UNIX;

    *export = g_strdup(qdict_get_try_str(options, "export"));
    if (*export) {
        qdict_del(options, "export");
    }

    return saddr;
}

NbdClientSession *nbd_get_client_session(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    return &s->client;
}

static QIOChannelSocket *nbd_establish_connection(SocketAddress *saddr,
                                                  Error **errp)
{
    QIOChannelSocket *sioc;
    Error *local_err = NULL;

    sioc = qio_channel_socket_new();

    qio_channel_socket_connect_sync(sioc,
                                    saddr,
                                    &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    qio_channel_set_delay(QIO_CHANNEL(sioc), false);

    return sioc;
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

    if (creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
        error_setg(errp,
                   "Expecting TLS credentials with a client endpoint");
        return NULL;
    }
    object_ref(obj);
    return creds;
}


static int nbd_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVNBDState *s = bs->opaque;
    char *export = NULL;
    QIOChannelSocket *sioc = NULL;
    SocketAddress *saddr;
    const char *tlscredsid;
    QCryptoTLSCreds *tlscreds = NULL;
    const char *hostname = NULL;
    int ret = -EINVAL;

    /* Pop the config into our state object. Exit if invalid. */
    saddr = nbd_config(s, options, &export, errp);
    if (!saddr) {
        goto error;
    }

    tlscredsid = g_strdup(qdict_get_try_str(options, "tls-creds"));
    if (tlscredsid) {
        qdict_del(options, "tls-creds");
        tlscreds = nbd_get_tls_creds(tlscredsid, errp);
        if (!tlscreds) {
            goto error;
        }

        if (saddr->type != SOCKET_ADDRESS_KIND_INET) {
            error_setg(errp, "TLS only supported over IP sockets");
            goto error;
        }
        hostname = saddr->u.inet->host;
    }

    /* establish TCP connection, return error if it fails
     * TODO: Configurable retry-until-timeout behaviour.
     */
    sioc = nbd_establish_connection(saddr, errp);
    if (!sioc) {
        ret = -ECONNREFUSED;
        goto error;
    }

    /* NBD handshake */
    ret = nbd_client_init(bs, sioc, export,
                          tlscreds, hostname, errp);
 error:
    if (sioc) {
        object_unref(OBJECT(sioc));
    }
    if (tlscreds) {
        object_unref(OBJECT(tlscreds));
    }
    qapi_free_SocketAddress(saddr);
    g_free(export);
    return ret;
}

static int nbd_co_readv(BlockDriverState *bs, int64_t sector_num,
                        int nb_sectors, QEMUIOVector *qiov)
{
    return nbd_client_co_readv(bs, sector_num, nb_sectors, qiov);
}

static int nbd_co_writev(BlockDriverState *bs, int64_t sector_num,
                         int nb_sectors, QEMUIOVector *qiov)
{
    return nbd_client_co_writev(bs, sector_num, nb_sectors, qiov);
}

static int nbd_co_flush(BlockDriverState *bs)
{
    return nbd_client_co_flush(bs);
}

static void nbd_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.max_discard = UINT32_MAX >> BDRV_SECTOR_BITS;
    bs->bl.max_transfer_length = UINT32_MAX >> BDRV_SECTOR_BITS;
}

static int nbd_co_discard(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors)
{
    return nbd_client_co_discard(bs, sector_num, nb_sectors);
}

static void nbd_close(BlockDriverState *bs)
{
    nbd_client_close(bs);
}

static int64_t nbd_getlength(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;

    return s->client.size;
}

static void nbd_detach_aio_context(BlockDriverState *bs)
{
    nbd_client_detach_aio_context(bs);
}

static void nbd_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    nbd_client_attach_aio_context(bs, new_context);
}

static void nbd_refresh_filename(BlockDriverState *bs, QDict *options)
{
    QDict *opts = qdict_new();
    const char *path   = qdict_get_try_str(options, "path");
    const char *host   = qdict_get_try_str(options, "host");
    const char *port   = qdict_get_try_str(options, "port");
    const char *export = qdict_get_try_str(options, "export");
    const char *tlscreds = qdict_get_try_str(options, "tls-creds");

    qdict_put_obj(opts, "driver", QOBJECT(qstring_from_str("nbd")));

    if (path && export) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd+unix:///%s?socket=%s", export, path);
    } else if (path && !export) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd+unix://?socket=%s", path);
    } else if (!path && export && port) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd://%s:%s/%s", host, port, export);
    } else if (!path && export && !port) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd://%s/%s", host, export);
    } else if (!path && !export && port) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd://%s:%s", host, port);
    } else if (!path && !export && !port) {
        snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                 "nbd://%s", host);
    }

    if (path) {
        qdict_put_obj(opts, "path", QOBJECT(qstring_from_str(path)));
    } else if (port) {
        qdict_put_obj(opts, "host", QOBJECT(qstring_from_str(host)));
        qdict_put_obj(opts, "port", QOBJECT(qstring_from_str(port)));
    } else {
        qdict_put_obj(opts, "host", QOBJECT(qstring_from_str(host)));
    }
    if (export) {
        qdict_put_obj(opts, "export", QOBJECT(qstring_from_str(export)));
    }
    if (tlscreds) {
        qdict_put_obj(opts, "tls-creds", QOBJECT(qstring_from_str(tlscreds)));
    }

    bs->full_open_options = opts;
}

static BlockDriver bdrv_nbd = {
    .format_name                = "nbd",
    .protocol_name              = "nbd",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_file_open             = nbd_open,
    .bdrv_co_readv              = nbd_co_readv,
    .bdrv_co_writev             = nbd_co_writev,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_discard            = nbd_co_discard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_attach_aio_context,
    .bdrv_refresh_filename      = nbd_refresh_filename,
};

static BlockDriver bdrv_nbd_tcp = {
    .format_name                = "nbd",
    .protocol_name              = "nbd+tcp",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_file_open             = nbd_open,
    .bdrv_co_readv              = nbd_co_readv,
    .bdrv_co_writev             = nbd_co_writev,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_discard            = nbd_co_discard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_attach_aio_context,
    .bdrv_refresh_filename      = nbd_refresh_filename,
};

static BlockDriver bdrv_nbd_unix = {
    .format_name                = "nbd",
    .protocol_name              = "nbd+unix",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_file_open             = nbd_open,
    .bdrv_co_readv              = nbd_co_readv,
    .bdrv_co_writev             = nbd_co_writev,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_discard            = nbd_co_discard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_attach_aio_context,
    .bdrv_refresh_filename      = nbd_refresh_filename,
};

static void bdrv_nbd_init(void)
{
    bdrv_register(&bdrv_nbd);
    bdrv_register(&bdrv_nbd_tcp);
    bdrv_register(&bdrv_nbd_unix);
}

block_init(bdrv_nbd_init);
