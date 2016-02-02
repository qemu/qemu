/*
 * QEMU crypto TLS session support
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

#ifndef QCRYPTO_TLS_SESSION_H__
#define QCRYPTO_TLS_SESSION_H__

#include "crypto/tlscreds.h"

/**
 * QCryptoTLSSession:
 *
 * The QCryptoTLSSession object encapsulates the
 * logic to integrate with a TLS providing library such
 * as GNUTLS, to setup and run TLS sessions.
 *
 * The API is designed such that it has no assumption about
 * the type of transport it is running over. It may be a
 * traditional TCP socket, or something else entirely. The
 * only requirement is a full-duplex stream of some kind.
 *
 * <example>
 *   <title>Using TLS session objects</title>
 *   <programlisting>
 * static ssize_t mysock_send(const char *buf, size_t len,
 *                            void *opaque)
 * {
 *    int fd = GPOINTER_TO_INT(opaque);
 *
 *    return write(*fd, buf, len);
 * }
 *
 * static ssize_t mysock_recv(const char *buf, size_t len,
 *                            void *opaque)
 * {
 *    int fd = GPOINTER_TO_INT(opaque);
 *
 *    return read(*fd, buf, len);
 * }
 *
 * static int mysock_run_tls(int sockfd,
 *                           QCryptoTLSCreds *creds,
 *                           Error *errp)
 * {
 *    QCryptoTLSSession *sess;
 *
 *    sess = qcrypto_tls_session_new(creds,
 *                                   "vnc.example.com",
 *                                   NULL,
 *                                   QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT,
 *                                   errp);
 *    if (sess == NULL) {
 *       return -1;
 *    }
 *
 *    qcrypto_tls_session_set_callbacks(sess,
 *                                      mysock_send,
 *                                      mysock_recv
 *                                      GINT_TO_POINTER(fd));
 *
 *    while (1) {
 *       if (qcrypto_tls_session_handshake(sess, errp) < 0) {
 *           qcrypto_tls_session_free(sess);
 *           return -1;
 *       }
 *
 *       switch(qcrypto_tls_session_get_handshake_status(sess)) {
 *       case QCRYPTO_TLS_HANDSHAKE_COMPLETE:
 *           if (qcrypto_tls_session_check_credentials(sess, errp) < )) {
 *               qcrypto_tls_session_free(sess);
 *               return -1;
 *           }
 *           goto done;
 *       case QCRYPTO_TLS_HANDSHAKE_RECVING:
 *           ...wait for GIO_IN event on fd...
 *           break;
 *       case QCRYPTO_TLS_HANDSHAKE_SENDING:
 *           ...wait for GIO_OUT event on fd...
 *           break;
 *       }
 *    }
 *   done:
 *
 *    ....send/recv payload data on sess...
 *
 *    qcrypto_tls_session_free(sess):
 * }
 *   </programlisting>
 * </example>
 */

typedef struct QCryptoTLSSession QCryptoTLSSession;


/**
 * qcrypto_tls_session_new:
 * @creds: pointer to a TLS credentials object
 * @hostname: optional hostname to validate
 * @aclname: optional ACL to validate peer credentials against
 * @endpoint: role of the TLS session, client or server
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a new TLS session object that will be used to
 * negotiate a TLS session over an arbitrary data channel.
 * The session object can operate as either the server or
 * client, according to the value of the @endpoint argument.
 *
 * For clients, the @hostname parameter should hold the full
 * unmodified hostname as requested by the user. This will
 * be used to verify the against the hostname reported in
 * the server's credentials (aka x509 certificate).
 *
 * The @aclname parameter (optionally) specifies the name
 * of an access control list that will be used to validate
 * the peer's credentials. For x509 credentials, the ACL
 * will be matched against the CommonName shown in the peer's
 * certificate. If the session is acting as a server, setting
 * an ACL will require that the client provide a validate
 * x509 client certificate.
 *
 * After creating the session object, the I/O callbacks
 * must be set using the qcrypto_tls_session_set_callbacks()
 * method. A TLS handshake sequence must then be completed
 * using qcrypto_tls_session_handshake(), before payload
 * data is permitted to be sent/received.
 *
 * The session object must be released by calling
 * qcrypto_tls_session_free() when no longer required
 *
 * Returns: a TLS session object, or NULL on error.
 */
QCryptoTLSSession *qcrypto_tls_session_new(QCryptoTLSCreds *creds,
                                           const char *hostname,
                                           const char *aclname,
                                           QCryptoTLSCredsEndpoint endpoint,
                                           Error **errp);

/**
 * qcrypto_tls_session_free:
 * @sess: the TLS session object
 *
 * Release all memory associated with the TLS session
 * object previously allocated by qcrypto_tls_session_new()
 */
void qcrypto_tls_session_free(QCryptoTLSSession *sess);

/**
 * qcrypto_tls_session_check_credentials:
 * @sess: the TLS session object
 * @errp: pointer to a NULL-initialized error object
 *
 * Validate the peer's credentials after a successful
 * TLS handshake. It is an error to call this before
 * qcrypto_tls_session_get_handshake_status() returns
 * QCRYPTO_TLS_HANDSHAKE_COMPLETE
 *
 * Returns 0 if the credentials validated, -1 on error
 */
int qcrypto_tls_session_check_credentials(QCryptoTLSSession *sess,
                                          Error **errp);

typedef ssize_t (*QCryptoTLSSessionWriteFunc)(const char *buf,
                                              size_t len,
                                              void *opaque);
typedef ssize_t (*QCryptoTLSSessionReadFunc)(char *buf,
                                             size_t len,
                                             void *opaque);

/**
 * qcrypto_tls_session_set_callbacks:
 * @sess: the TLS session object
 * @writeFunc: callback for sending data
 * @readFunc: callback to receiving data
 * @opaque: data to pass to callbacks
 *
 * Sets the callback functions that are to be used for sending
 * and receiving data on the underlying data channel. Typically
 * the callbacks to write/read to/from a TCP socket, but there
 * is no assumption made about the type of channel used.
 *
 * The @writeFunc callback will be passed the encrypted
 * data to send to the remote peer.
 *
 * The @readFunc callback will be passed a pointer to fill
 * with encrypted data received from the remote peer
 */
void qcrypto_tls_session_set_callbacks(QCryptoTLSSession *sess,
                                       QCryptoTLSSessionWriteFunc writeFunc,
                                       QCryptoTLSSessionReadFunc readFunc,
                                       void *opaque);

/**
 * qcrypto_tls_session_write:
 * @sess: the TLS session object
 * @buf: the plain text to send
 * @len: the length of @buf
 *
 * Encrypt @len bytes of the data in @buf and send
 * it to the remote peer using the callback previously
 * registered with qcrypto_tls_session_set_callbacks()
 *
 * It is an error to call this before
 * qcrypto_tls_session_get_handshake_status() returns
 * QCRYPTO_TLS_HANDSHAKE_COMPLETE
 *
 * Returns: the number of bytes sent, or -1 on error
 */
ssize_t qcrypto_tls_session_write(QCryptoTLSSession *sess,
                                  const char *buf,
                                  size_t len);

/**
 * qcrypto_tls_session_read:
 * @sess: the TLS session object
 * @buf: to fill with plain text received
 * @len: the length of @buf
 *
 * Receive up to @len bytes of data from the remote peer
 * using the callback previously registered with
 * qcrypto_tls_session_set_callbacks(), decrypt it and
 * store it in @buf.
 *
 * It is an error to call this before
 * qcrypto_tls_session_get_handshake_status() returns
 * QCRYPTO_TLS_HANDSHAKE_COMPLETE
 *
 * Returns: the number of bytes received, or -1 on error
 */
ssize_t qcrypto_tls_session_read(QCryptoTLSSession *sess,
                                 char *buf,
                                 size_t len);

/**
 * qcrypto_tls_session_handshake:
 * @sess: the TLS session object
 * @errp: pointer to a NULL-initialized error object
 *
 * Start, or continue, a TLS handshake sequence. If
 * the underlying data channel is non-blocking, then
 * this method may return control before the handshake
 * is complete. On non-blocking channels the
 * qcrypto_tls_session_get_handshake_status() method
 * should be used to determine whether the handshake
 * has completed, or is waiting to send or receive
 * data. In the latter cases, the caller should setup
 * an event loop watch and call this method again
 * once the underlying data channel is ready to read
 * or write again
 */
int qcrypto_tls_session_handshake(QCryptoTLSSession *sess,
                                  Error **errp);

typedef enum {
    QCRYPTO_TLS_HANDSHAKE_COMPLETE,
    QCRYPTO_TLS_HANDSHAKE_SENDING,
    QCRYPTO_TLS_HANDSHAKE_RECVING,
} QCryptoTLSSessionHandshakeStatus;

/**
 * qcrypto_tls_session_get_handshake_status:
 * @sess: the TLS session object
 *
 * Check the status of the TLS handshake. This
 * is used with non-blocking data channels to
 * determine whether the handshake is waiting
 * to send or receive further data to/from the
 * remote peer.
 *
 * Once this returns QCRYPTO_TLS_HANDSHAKE_COMPLETE
 * it is permitted to send/receive payload data on
 * the channel
 */
QCryptoTLSSessionHandshakeStatus
qcrypto_tls_session_get_handshake_status(QCryptoTLSSession *sess);

/**
 * qcrypto_tls_session_get_key_size:
 * @sess: the TLS session object
 * @errp: pointer to a NULL-initialized error object
 *
 * Check the size of the data channel encryption key
 *
 * Returns: the length in bytes of the encryption key
 * or -1 on error
 */
int qcrypto_tls_session_get_key_size(QCryptoTLSSession *sess,
                                     Error **errp);

/**
 * qcrypto_tls_session_get_peer_name:
 * @sess: the TLS session object
 *
 * Get the identified name of the remote peer. If the
 * TLS session was negotiated using x509 certificate
 * credentials, this will return the CommonName from
 * the peer's certificate. If no identified name is
 * available it will return NULL.
 *
 * The returned data must be released with g_free()
 * when no longer required.
 *
 * Returns: the peer's name or NULL.
 */
char *qcrypto_tls_session_get_peer_name(QCryptoTLSSession *sess);

#endif /* QCRYPTO_TLS_SESSION_H__ */
