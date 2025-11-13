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
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "trace.h"

struct QIONetListenerSource {
    QIOChannelSocket *sioc;
    GSource *io_source;
    QIONetListener *listener;
};

QIONetListener *qio_net_listener_new(void)
{
    QIONetListener *listener;

    listener = QIO_NET_LISTENER(object_new(TYPE_QIO_NET_LISTENER));
    qemu_mutex_init(&listener->lock);
    return listener;
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
    QIONetListenerClientFunc io_func;
    gpointer io_data;
    GMainContext *context;
    AioContext *aio_context;

    sioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(ioc),
                                     NULL);
    if (!sioc) {
        return TRUE;
    }

    WITH_QEMU_LOCK_GUARD(&listener->lock) {
        io_func = listener->io_func;
        io_data = listener->io_data;
        context = listener->context;
        aio_context = listener->aio_context;
    }

    trace_qio_net_listener_callback(listener, io_func, context, aio_context);
    if (io_func) {
        io_func(listener, sioc, io_data);
    }

    object_unref(OBJECT(sioc));

    return TRUE;
}


static void qio_net_listener_aio_func(void *opaque)
{
    QIONetListenerSource *data = opaque;

    assert(data->io_source == NULL);
    assert(data->listener->aio_context != NULL);
    qio_net_listener_channel_func(QIO_CHANNEL(data->sioc), G_IO_IN,
                                  data->listener);
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

/*
 * i == 0 to set watch on entire array, non-zero to only set watch on
 * recent additions when earlier entries are already watched.
 *
 * listener->lock must be held by caller.
 */
static void
qio_net_listener_watch(QIONetListener *listener, size_t i, const char *caller)
{
    if (!listener->io_func) {
        return;
    }

    trace_qio_net_listener_watch(listener, listener->io_func,
                                 listener->context, listener->aio_context,
                                 caller);
    for ( ; i < listener->nsioc; i++) {
        if (!listener->aio_context) {
            /*
             * The user passed a GMainContext with the async callback;
             * they plan on running the default or their own g_main_loop.
             */
            object_ref(OBJECT(listener));
            listener->source[i]->io_source = qio_channel_add_watch_source(
                QIO_CHANNEL(listener->source[i]->sioc), G_IO_IN,
                qio_net_listener_channel_func,
                listener, (GDestroyNotify)object_unref, listener->context);
        } else {
            /*
             * The user passed an AioContext.  At this point,
             * AioContext lacks a clean way to call a notify function
             * to release a final reference after any callback is
             * complete.  But we asserted earlier that the async
             * callback is changed only from the thread associated
             * with aio_context, which means no other thread is in the
             * middle of running the callback when we are changing the
             * refcount on listener here.  Therefore, a single
             * reference here is sufficient to ensure listener is not
             * finalized during the callback.
             */
            assert(listener->context == NULL);
            if (i == 0) {
                object_ref(OBJECT(listener));
            }
            qio_channel_set_aio_fd_handler(
                QIO_CHANNEL(listener->source[i]->sioc),
                listener->aio_context, qio_net_listener_aio_func,
                NULL, NULL, listener->source[i]);
        }
    }
}

/* listener->lock must be held by caller. */
static void
qio_net_listener_unwatch(QIONetListener *listener, const char *caller)
{
    size_t i;

    if (!listener->io_func) {
        return;
    }

    trace_qio_net_listener_unwatch(listener, listener->io_func,
                                   listener->context, listener->aio_context,
                                   caller);
    for (i = 0; i < listener->nsioc; i++) {
        if (!listener->aio_context) {
            if (listener->source[i]->io_source) {
                g_source_destroy(listener->source[i]->io_source);
                g_source_unref(listener->source[i]->io_source);
                listener->source[i]->io_source = NULL;
            }
        } else {
            assert(listener->context == NULL);
            qio_channel_set_aio_fd_handler(
                QIO_CHANNEL(listener->source[i]->sioc),
                listener->aio_context, NULL, NULL, NULL, NULL);
            if (i == listener->nsioc - 1) {
                object_unref(OBJECT(listener));
            }
        }
    }
}

void qio_net_listener_add(QIONetListener *listener,
                          QIOChannelSocket *sioc)
{
    if (listener->name) {
        qio_channel_set_name(QIO_CHANNEL(sioc), listener->name);
    }

    listener->source = g_renew(typeof(listener->source[0]),
                               listener->source,
                               listener->nsioc + 1);
    listener->source[listener->nsioc] = g_new0(QIONetListenerSource, 1);
    listener->source[listener->nsioc]->sioc = sioc;
    listener->source[listener->nsioc]->listener = listener;

    object_ref(OBJECT(sioc));
    listener->connected = true;

    QEMU_LOCK_GUARD(&listener->lock);
    listener->nsioc++;
    qio_net_listener_watch(listener, listener->nsioc - 1, "add");
}


static void
qio_net_listener_set_client_func_internal(QIONetListener *listener,
                                          QIONetListenerClientFunc func,
                                          gpointer data,
                                          GDestroyNotify notify,
                                          GMainContext *context,
                                          AioContext *aio_context)
{
    QEMU_LOCK_GUARD(&listener->lock);
    if (listener->io_func == func && listener->io_data == data &&
        listener->io_notify == notify && listener->context == context &&
        listener->aio_context == aio_context) {
        return;
    }

    qio_net_listener_unwatch(listener, "set_client_func");
    if (listener->io_notify) {
        listener->io_notify(listener->io_data);
    }
    listener->io_func = func;
    listener->io_data = data;
    listener->io_notify = notify;
    listener->context = context;
    listener->aio_context = aio_context;

    qio_net_listener_watch(listener, 0, "set_client_func");
}

void qio_net_listener_set_client_func_full(QIONetListener *listener,
                                           QIONetListenerClientFunc func,
                                           gpointer data,
                                           GDestroyNotify notify,
                                           GMainContext *context)
{
    qio_net_listener_set_client_func_internal(listener, func, data,
                                              notify, context, NULL);
}

void qio_net_listener_set_client_func(QIONetListener *listener,
                                      QIONetListenerClientFunc func,
                                      gpointer data,
                                      GDestroyNotify notify)
{
    qio_net_listener_set_client_func_internal(listener, func, data,
                                              notify, NULL, NULL);
}

void qio_net_listener_set_client_aio_func(QIONetListener *listener,
                                          QIONetListenerClientFunc func,
                                          void *data,
                                          AioContext *context)
{
    if (!context) {
        assert(qemu_in_main_thread());
        context = qemu_get_aio_context();
    } else {
        /*
         * TODO: The API was intentionally designed to allow a caller
         * to pass an alternative AioContext for future expansion;
         * however, actually implementating that is not possible
         * without notify callbacks wired into AioContext similar to
         * how they work in GSource.  So for now, this code hard-codes
         * the knowledge that the only client needing AioContext is
         * the NBD server, which uses the global context and does not
         * suffer from cross-thread safety issues.
         */
        g_assert_not_reached();
    }
    qio_net_listener_set_client_func_internal(listener, func, data,
                                              NULL, NULL, context);
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

    WITH_QEMU_LOCK_GUARD(&listener->lock) {
        qio_net_listener_unwatch(listener, "wait_client");
    }

    sources = g_new0(GSource *, listener->nsioc);
    for (i = 0; i < listener->nsioc; i++) {
        sources[i] = qio_channel_create_watch(
            QIO_CHANNEL(listener->source[i]->sioc), G_IO_IN);

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

    WITH_QEMU_LOCK_GUARD(&listener->lock) {
        qio_net_listener_watch(listener, 0, "wait_client");
    }

    return data.sioc;
}

void qio_net_listener_disconnect(QIONetListener *listener)
{
    size_t i;

    if (!listener->connected) {
        return;
    }

    QEMU_LOCK_GUARD(&listener->lock);
    qio_net_listener_unwatch(listener, "disconnect");
    for (i = 0; i < listener->nsioc; i++) {
        qio_channel_close(QIO_CHANNEL(listener->source[i]->sioc), NULL);
    }
    listener->connected = false;
}


bool qio_net_listener_is_connected(QIONetListener *listener)
{
    return listener->connected;
}

size_t qio_net_listener_nsioc(QIONetListener *listener)
{
    return listener->nsioc;
}

QIOChannelSocket *qio_net_listener_sioc(QIONetListener *listener, size_t n)
{
    if (n >= listener->nsioc) {
        return NULL;
    }
    return listener->source[n]->sioc;
}

SocketAddress *
qio_net_listener_get_local_address(QIONetListener *listener, size_t n,
                                   Error **errp)
{
    QIOChannelSocket *sioc = qio_net_listener_sioc(listener, n);

    if (!sioc) {
        error_setg(errp, "Listener index out of range");
        return NULL;
    }

    return qio_channel_socket_get_local_address(sioc, errp);
}

static void qio_net_listener_finalize(Object *obj)
{
    QIONetListener *listener = QIO_NET_LISTENER(obj);
    size_t i;

    qio_net_listener_disconnect(listener);
    if (listener->io_notify) {
        listener->io_notify(listener->io_data);
    }

    for (i = 0; i < listener->nsioc; i++) {
        object_unref(OBJECT(listener->source[i]->sioc));
        g_free(listener->source[i]);
    }
    g_free(listener->source);
    g_free(listener->name);
    qemu_mutex_destroy(&listener->lock);
}

static const TypeInfo qio_net_listener_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_NET_LISTENER,
    .instance_size = sizeof(QIONetListener),
    .instance_finalize = qio_net_listener_finalize,
};


static void qio_net_listener_register_types(void)
{
    type_register_static(&qio_net_listener_info);
}


type_init(qio_net_listener_register_types);
