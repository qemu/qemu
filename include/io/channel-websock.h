/*
 * QEMU I/O channels driver websockets
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_CHANNEL_WEBSOCK_H
#define QIO_CHANNEL_WEBSOCK_H

#include "io/channel.h"
#include "qemu/buffer.h"
#include "io/task.h"

#define TYPE_QIO_CHANNEL_WEBSOCK "qio-channel-websock"
#define QIO_CHANNEL_WEBSOCK(obj)                                     \
    OBJECT_CHECK(QIOChannelWebsock, (obj), TYPE_QIO_CHANNEL_WEBSOCK)

typedef struct QIOChannelWebsock QIOChannelWebsock;
typedef union QIOChannelWebsockMask QIOChannelWebsockMask;

union QIOChannelWebsockMask {
    char c[4];
    uint32_t u;
};

/**
 * QIOChannelWebsock
 *
 * The QIOChannelWebsock class provides a channel wrapper which
 * can transparently run the HTTP websockets protocol. This is
 * usually used over a TCP socket, but there is actually no
 * technical restriction on which type of master channel is
 * used as the transport.
 *
 * This channel object is currently only capable of running as
 * a websocket server and is a pretty crude implementation
 * of it, not supporting the full websockets protocol feature
 * set. It is sufficient to use with a simple websockets
 * client for encapsulating VNC for noVNC in-browser client.
 */

struct QIOChannelWebsock {
    QIOChannel parent;
    QIOChannel *master;
    Buffer encinput;
    Buffer encoutput;
    Buffer rawinput;
    size_t payload_remain;
    size_t pong_remain;
    QIOChannelWebsockMask mask;
    guint io_tag;
    Error *io_err;
    gboolean io_eof;
    uint8_t opcode;
};

/**
 * qio_channel_websock_new_server:
 * @master: the underlying channel object
 *
 * Create a new websockets channel that runs the server
 * side of the protocol.
 *
 * After creating the channel, it is mandatory to call
 * the qio_channel_websock_handshake() method before attempting
 * todo any I/O on the channel.
 *
 * Once the handshake has completed, all I/O should be done
 * via the new websocket channel object and not the original
 * master channel
 *
 * Returns: the new websockets channel object
 */
QIOChannelWebsock *
qio_channel_websock_new_server(QIOChannel *master);

/**
 * qio_channel_websock_handshake:
 * @ioc: the websocket channel object
 * @func: the callback to invoke when completed
 * @opaque: opaque data to pass to @func
 * @destroy: optional callback to free @opaque
 *
 * Perform the websocket handshake. This method
 * will return immediately and the handshake will
 * continue in the background, provided the main
 * loop is running. When the handshake is complete,
 * or fails, the @func callback will be invoked.
 */
void qio_channel_websock_handshake(QIOChannelWebsock *ioc,
                                   QIOTaskFunc func,
                                   gpointer opaque,
                                   GDestroyNotify destroy);

#endif /* QIO_CHANNEL_WEBSOCK_H */
