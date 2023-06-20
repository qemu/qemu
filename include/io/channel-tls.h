/*
 * QEMU I/O channels TLS driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef QIO_CHANNEL_TLS_H
#define QIO_CHANNEL_TLS_H

#include "io/channel.h"
#include "io/task.h"
#include "crypto/tlssession.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_TLS "qio-channel-tls"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelTLS, QIO_CHANNEL_TLS)


/**
 * QIOChannelTLS
 *
 * The QIOChannelTLS class provides a channel wrapper which
 * can transparently run the TLS encryption protocol. It is
 * usually used over a TCP socket, but there is actually no
 * technical restriction on which type of master channel is
 * used as the transport.
 *
 * This channel object is capable of running as either a
 * TLS server or TLS client.
 */

struct QIOChannelTLS {
    QIOChannel parent;
    QIOChannel *master;
    QCryptoTLSSession *session;
    QIOChannelShutdown shutdown;
    guint hs_ioc_tag;
};

/**
 * qio_channel_tls_new_server:
 * @master: the underlying channel object
 * @creds: the credentials to use for TLS handshake
 * @aclname: the access control list for validating clients
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a new TLS channel that runs the server side of
 * a TLS session. The TLS session handshake will use the
 * credentials provided in @creds. If the @aclname parameter
 * is non-NULL, then the client will have to provide
 * credentials (ie a x509 client certificate) which will
 * then be validated against the ACL.
 *
 * After creating the channel, it is mandatory to call
 * the qio_channel_tls_handshake() method before attempting
 * todo any I/O on the channel.
 *
 * Once the handshake has completed, all I/O should be done
 * via the new TLS channel object and not the original
 * master channel
 *
 * Returns: the new TLS channel object, or NULL
 */
QIOChannelTLS *
qio_channel_tls_new_server(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *aclname,
                           Error **errp);

/**
 * qio_channel_tls_new_client:
 * @master: the underlying channel object
 * @creds: the credentials to use for TLS handshake
 * @hostname: the user specified server hostname
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a new TLS channel that runs the client side of
 * a TLS session. The TLS session handshake will use the
 * credentials provided in @creds. The @hostname parameter
 * should provide the user specified hostname of the server
 * and will be validated against the server's credentials
 * (ie CommonName of the x509 certificate)
 *
 * After creating the channel, it is mandatory to call
 * the qio_channel_tls_handshake() method before attempting
 * todo any I/O on the channel.
 *
 * Once the handshake has completed, all I/O should be done
 * via the new TLS channel object and not the original
 * master channel
 *
 * Returns: the new TLS channel object, or NULL
 */
QIOChannelTLS *
qio_channel_tls_new_client(QIOChannel *master,
                           QCryptoTLSCreds *creds,
                           const char *hostname,
                           Error **errp);

/**
 * qio_channel_tls_handshake:
 * @ioc: the TLS channel object
 * @func: the callback to invoke when completed
 * @opaque: opaque data to pass to @func
 * @destroy: optional callback to free @opaque
 * @context: the context that TLS handshake will run with. If %NULL,
 *           the default context will be used
 *
 * Perform the TLS session handshake. This method
 * will return immediately and the handshake will
 * continue in the background, provided the main
 * loop is running. When the handshake is complete,
 * or fails, the @func callback will be invoked.
 */
void qio_channel_tls_handshake(QIOChannelTLS *ioc,
                               QIOTaskFunc func,
                               gpointer opaque,
                               GDestroyNotify destroy,
                               GMainContext *context);

/**
 * qio_channel_tls_get_session:
 * @ioc: the TLS channel object
 *
 * Get the TLS session used by the channel.
 *
 * Returns: the TLS session
 */
QCryptoTLSSession *
qio_channel_tls_get_session(QIOChannelTLS *ioc);

#endif /* QIO_CHANNEL_TLS_H */
