/*
 * QEMU network listener
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/net-listener.h"
#include "io/dns-resolver.h"
#include "qapi/error.h"
#include "qemu/module.h"

QIONetListener *qio_net_listener_new(void)
{
    return QIO_NET_LISTENER(object_new(TYPE_QIO_NET_LISTENER));
}

void qio_net_listener_set_name(QIONetListener *listener,
                               const char *name)
{
    g_free(listener->name);
    listener->name = g_strdup(name);
}


static gboolean qio_net_listener_channel_func(QIOChannel *ioc,
                                              GIOCondition condition,
                                              gpointer opaque)
{
    QIONetListener *listener = QIO_NET_LISTENER(opaque);
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!sioc) {
        return TRUE;
    }

    if (listener->io_func) {
        listener->io_func(listener, sioc, listener->io_data);
    }

    object_unref(OBJECT(sioc));

    return TRUE;
}


int qio_net_listener_open_sync(QIONetListener *listener,
                               SocketAddress *addr,
                               int num,
                               Error **errp)
{
    QIODNSResolver *resolver = qio_dns_resolver_get_instance();
    SocketAddress **resaddrs;
    size_t nresaddrs;
    size_t i;
    Error *err = NULL;
    bool success = false;

    if (qio_dns_resolver_lookup_sync(resolver,
                                     addr,
                                     &nresaddrs,
                                     &resaddrs,
                                     errp) < 0) {
        return -1;
    }

    for (i = 0; i < nresaddrs; i++) {
        QIOChannelSocket *sioc = qio_channel_socket_new();

        if (qio_channel_socket_listen_sync(sioc, resaddrs[i], num,
                                           err ? NULL : &err) == 0) {
            success = true;

            qio_net_listener_add(listener, sioc);
        }

        qapi_free_SocketAddress(resaddrs[i]);
        object_unref(OBJECT(sioc));
    }
    g_free(resaddrs);

    if (success) {
        error_free(err);
        return 0;
    } else {
        error_propagate(errp, err);
        return -1;
    }
}


void qio_net_listener_add(QIONetListener *listener,
                          QIOChannelSocket *sioc)
{
    if (listener->name) {
        char *name = g_strdup_printf("%s-listen", listener->name);
        qio_channel_set_name(QIO_CHANNEL(sioc), name);
        g_free(name);
    }

    listener->sioc = g_renew(QIOChannelSocket *, listener->sioc,
                             listener->nsioc + 1);
    listener->io_source = g_renew(typeof(listener->io_source[0]),
                                  listener->io_source,
                                  listener->nsioc + 1);
    listener->sioc[listener->nsioc] = sioc;
    listener->io_source[listener->nsioc] = NULL;

    object_ref(OBJECT(sioc));
    listener->connected = true;

    if (listener->io_func != NULL) {
        object_ref(OBJECT(listener));
        listener->io_source[listener->nsioc] = qio_channel_add_watch_source(
            QIO_CHANNEL(listener->sioc[listener->nsioc]), G_IO_IN,
            qio_net_listener_channel_func,
            listener, (GDestroyNotify)object_unref, NULL);
    }

    listener->nsioc++;
}


void qio_net_listener_set_client_func_full(QIONetListener *listener,
                                           QIONetListenerClientFunc func,
                                           gpointer data,
                                           GDestroyNotify notify,
                                           GMainContext *context)
{
    size_t i;

    if (listener->io_notify) {
        listener->io_notify(listener->io_data);
    }
    listener->io_func = func;
    listener->io_data = data;
    listener->io_notify = notify;

    for (i = 0; i < listener->nsioc; i++) {
        if (listener->io_source[i]) {
            g_source_destroy(listener->io_source[i]);
            g_source_unref(listener->io_source[i]);
            listener->io_source[i] = NULL;
        }
    }

    if (listener->io_func != NULL) {
        for (i = 0; i < listener->nsioc; i++) {
            object_ref(OBJECT(listener));
            listener->io_source[i] = qio_channel_add_watch_source(
                QIO_CHANNEL(listener->sioc[i]), G_IO_IN,
                qio_net_listener_channel_func,
                listener, (GDestroyNotify)object_unref, context);
        }
    }
}

void qio_net_listener_set_client_func(QIONetListener *listener,
                                      QIONetListenerClientFunc func,
                                      gpointer data,
                                      GDestroyNotify notify)
{
    qio_net_listener_set_client_func_full(listener, func, data,
                                          notify, NULL);
}

struct QIONetListenerClientWaitData {
    QIOChannelSocket *sioc;
    GMainLoop *loop;
};


static gboolean qio_net_listener_wait_client_func(QIOChannel *ioc,
                                                  GIOCondition condition,
                                                  gpointer opaque)
{
    struct QIONetListenerClientWaitData *data = opaque;
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!sioc) {
        return TRUE;
    }

    if (data->sioc) {
        object_unref(OBJECT(sioc));
    } else {
        data->sioc = sioc;
        g_main_loop_quit(data->loop);
    }

    return TRUE;
}

QIOChannelSocket *qio_net_listener_wait_client(QIONetListener *listener)
{
    GMainContext *ctxt = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(ctxt, TRUE);
    GSource **sources;
    struct QIONetListenerClientWaitData data = {
        .sioc = NULL,
        .loop = loop
    };
    size_t i;

    for (i = 0; i < listener->nsioc; i++) {
        if (listener->io_source[i]) {
            g_source_destroy(listener->io_source[i]);
            g_source_unref(listener->io_source[i]);
            listener->io_source[i] = NULL;
        }
    }

    sources = g_new0(GSource *, listener->nsioc);
    for (i = 0; i < listener->nsioc; i++) {
        sources[i] = qio_channel_create_watch(QIO_CHANNEL(listener->sioc[i]),
                                              G_IO_IN);

        g_source_set_callback(sources[i],
                              (GSourceFunc)qio_net_listener_wait_client_func,
                              &data,
                              NULL);
        g_source_attach(sources[i], ctxt);
    }

    g_main_loop_run(loop);

    for (i = 0; i < listener->nsioc; i++) {
        g_source_unref(sources[i]);
    }
    g_free(sources);
    g_main_loop_unref(loop);
    g_main_context_unref(ctxt);

    if (listener->io_func != NULL) {
        for (i = 0; i < listener->nsioc; i++) {
            object_ref(OBJECT(listener));
            listener->io_source[i] = qio_channel_add_watch_source(
                QIO_CHANNEL(listener->sioc[i]), G_IO_IN,
                qio_net_listener_channel_func,
                listener, (GDestroyNotify)object_unref, NULL);
        }
    }

    return data.sioc;
}

void qio_net_listener_disconnect(QIONetListener *listener)
{
    size_t i;

    if (!listener->connected) {
        return;
    }

    for (i = 0; i < listener->nsioc; i++) {
        if (listener->io_source[i]) {
            g_source_destroy(listener->io_source[i]);
            g_source_unref(listener->io_source[i]);
            listener->io_source[i] = NULL;
        }
        qio_channel_close(QIO_CHANNEL(listener->sioc[i]), NULL);
    }
    listener->connected = false;
}


bool qio_net_listener_is_connected(QIONetListener *listener)
{
    return listener->connected;
}

static void qio_net_listener_finalize(Object *obj)
{
    QIONetListener *listener = QIO_NET_LISTENER(obj);
    size_t i;

    qio_net_listener_disconnect(listener);

    for (i = 0; i < listener->nsioc; i++) {
        object_unref(OBJECT(listener->sioc[i]));
    }
    g_free(listener->io_source);
    g_free(listener->sioc);
    g_free(listener->name);
}

static const TypeInfo qio_net_listener_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_NET_LISTENER,
    .instance_size = sizeof(QIONetListener),
    .instance_finalize = qio_net_listener_finalize,
    .class_size = sizeof(QIONetListenerClass),
};


static void qio_net_listener_register_types(void)
{
    type_register_static(&qio_net_listener_info);
}


type_init(qio_net_listener_register_types);
