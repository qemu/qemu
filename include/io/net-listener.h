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

#ifndef QIO_NET_LISTENER_H
#define QIO_NET_LISTENER_H

#include "io/channel-socket.h"

#define TYPE_QIO_NET_LISTENER "qio-net-listener"
#define QIO_NET_LISTENER(obj)                                    \
    OBJECT_CHECK(QIONetListener, (obj), TYPE_QIO_NET_LISTENER)
#define QIO_NET_LISTENER_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(QIONetListenerClass, klass, TYPE_QIO_NET_LISTENER)
#define QIO_NET_LISTENER_GET_CLASS(obj)                                  \
    OBJECT_GET_CLASS(QIONetListenerClass, obj, TYPE_QIO_NET_LISTENER)

typedef struct QIONetListener QIONetListener;
typedef struct QIONetListenerClass QIONetListenerClass;

typedef void (*QIONetListenerClientFunc)(QIONetListener *listener,
                                         QIOChannelSocket *sioc,
                                         gpointer data);

/**
 * QIONetListener:
 *
 * The QIONetListener object encapsulates the management of a
 * listening socket. It is able to listen on multiple sockets
 * concurrently, to deal with the scenario where IPv4 / IPv6
 * needs separate sockets, or there is a need to listen on a
 * subset of interface IP addresses, instead of the wildcard
 * address.
 */
struct QIONetListener {
    Object parent;

    char *name;
    QIOChannelSocket **sioc;
    GSource **io_source;
    size_t nsioc;

    bool connected;

    QIONetListenerClientFunc io_func;
    gpointer io_data;
    GDestroyNotify io_notify;
};

struct QIONetListenerClass {
    ObjectClass parent;
};


/**
 * qio_net_listener_new:
 *
 * Create a new network listener service, which is not
 * listening on any sockets initially.
 *
 * Returns: the new listener
 */
QIONetListener *qio_net_listener_new(void);


/**
 * qio_net_listener_set_name:
 * @listener: the network listener object
 * @name: the listener name
 *
 * Set the name of the listener. This is used as a debugging
 * aid, to set names on any GSource instances associated
 * with the listener
 */
void qio_net_listener_set_name(QIONetListener *listener,
                               const char *name);

/**
 * qio_net_listener_open_sync:
 * @listener: the network listener object
 * @addr: the address to listen on
 * @num: the amount of expected connections
 * @errp: pointer to a NULL initialized error object
 *
 * Synchronously open a listening connection on all
 * addresses associated with @addr. This method may
 * also be invoked multiple times, in order to have a
 * single listener on multiple distinct addresses.
 */
int qio_net_listener_open_sync(QIONetListener *listener,
                               SocketAddress *addr,
                               int num,
                               Error **errp);

/**
 * qio_net_listener_add:
 * @listener: the network listener object
 * @sioc: the socket I/O channel
 *
 * Associate a listening socket I/O channel with the
 * listener. The listener will acquire a new reference
 * on @sioc, so the caller should release its own reference
 * if it no longer requires the object.
 */
void qio_net_listener_add(QIONetListener *listener,
                          QIOChannelSocket *sioc);

/**
 * qio_net_listener_set_client_func_full:
 * @listener: the network listener object
 * @func: the callback function
 * @data: opaque data to pass to @func
 * @notify: callback to free @data
 * @context: the context that the sources will be bound to.  If %NULL,
 *           the default context will be used.
 *
 * Register @func to be invoked whenever a new client
 * connects to the listener. @func will be invoked
 * passing in the QIOChannelSocket instance for the
 * client.
 */
void qio_net_listener_set_client_func_full(QIONetListener *listener,
                                           QIONetListenerClientFunc func,
                                           gpointer data,
                                           GDestroyNotify notify,
                                           GMainContext *context);

/**
 * qio_net_listener_set_client_func:
 * @listener: the network listener object
 * @func: the callback function
 * @data: opaque data to pass to @func
 * @notify: callback to free @data
 *
 * Wrapper of qio_net_listener_set_client_func_full(), only that the
 * sources will always be bound to default main context.
 */
void qio_net_listener_set_client_func(QIONetListener *listener,
                                      QIONetListenerClientFunc func,
                                      gpointer data,
                                      GDestroyNotify notify);

/**
 * qio_net_listener_wait_client:
 * @listener: the network listener object
 *
 * Block execution of the caller until a new client arrives
 * on one of the listening sockets. If there was previously
 * a callback registered with qio_net_listener_set_client_func
 * it will be temporarily disabled, and re-enabled afterwards.
 *
 * Returns: the new client socket
 */
QIOChannelSocket *qio_net_listener_wait_client(QIONetListener *listener);


/**
 * qio_net_listener_disconnect:
 * @listener: the network listener object
 *
 * Disconnect the listener, removing all I/O callback
 * watches and closing the socket channels.
 */
void qio_net_listener_disconnect(QIONetListener *listener);


/**
 * qio_net_listener_is_connected:
 * @listener: the network listener object
 *
 * Determine if the listener is connected to any socket
 * channels
 *
 * Returns: true if connected, false otherwise
 */
bool qio_net_listener_is_connected(QIONetListener *listener);

#endif /* QIO_NET_LISTENER_H */
