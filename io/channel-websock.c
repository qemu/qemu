/*
 * QEMU I/O channels driver websockets
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "io/channel-websock.h"
#include "crypto/hash.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/module.h"

/* Max amount to allow in rawinput/encoutput buffers */
#define QIO_CHANNEL_WEBSOCK_MAX_BUFFER 8192

#define QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN 24
#define QIO_CHANNEL_WEBSOCK_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define QIO_CHANNEL_WEBSOCK_GUID_LEN strlen(QIO_CHANNEL_WEBSOCK_GUID)

#define QIO_CHANNEL_WEBSOCK_HEADER_PROTOCOL "sec-websocket-protocol"
#define QIO_CHANNEL_WEBSOCK_HEADER_VERSION "sec-websocket-version"
#define QIO_CHANNEL_WEBSOCK_HEADER_KEY "sec-websocket-key"
#define QIO_CHANNEL_WEBSOCK_HEADER_UPGRADE "upgrade"
#define QIO_CHANNEL_WEBSOCK_HEADER_HOST "host"
#define QIO_CHANNEL_WEBSOCK_HEADER_CONNECTION "connection"

#define QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY "binary"
#define QIO_CHANNEL_WEBSOCK_CONNECTION_UPGRADE "Upgrade"
#define QIO_CHANNEL_WEBSOCK_UPGRADE_WEBSOCKET "websocket"

#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON \
    "Server: QEMU VNC\r\n"                       \
    "Date: %s\r\n"

#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_WITH_PROTO_RES_OK \
    "HTTP/1.1 101 Switching Protocols\r\n"              \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON            \
    "Upgrade: websocket\r\n"                            \
    "Connection: Upgrade\r\n"                           \
    "Sec-WebSocket-Accept: %s\r\n"                      \
    "Sec-WebSocket-Protocol: binary\r\n"                \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_OK    \
    "HTTP/1.1 101 Switching Protocols\r\n"      \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON    \
    "Upgrade: websocket\r\n"                    \
    "Connection: Upgrade\r\n"                   \
    "Sec-WebSocket-Accept: %s\r\n"              \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_NOT_FOUND \
    "HTTP/1.1 404 Not Found\r\n"                    \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON        \
    "Connection: close\r\n"                         \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_BAD_REQUEST \
    "HTTP/1.1 400 Bad Request\r\n"                    \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON          \
    "Connection: close\r\n"                           \
    "Sec-WebSocket-Version: "                         \
    QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION             \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_SERVER_ERR \
    "HTTP/1.1 500 Internal Server Error\r\n"         \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON         \
    "Connection: close\r\n"                          \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_TOO_LARGE  \
    "HTTP/1.1 403 Request Entity Too Large\r\n"      \
    QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_COMMON         \
    "Connection: close\r\n"                          \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_END "\r\n\r\n"
#define QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION "13"
#define QIO_CHANNEL_WEBSOCK_HTTP_METHOD "GET"
#define QIO_CHANNEL_WEBSOCK_HTTP_PATH "/"
#define QIO_CHANNEL_WEBSOCK_HTTP_VERSION "HTTP/1.1"

/* The websockets packet header is variable length
 * depending on the size of the payload... */

/* ...length when using 7-bit payload length */
#define QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT 6
/* ...length when using 16-bit payload length */
#define QIO_CHANNEL_WEBSOCK_HEADER_LEN_16_BIT 8
/* ...length when using 64-bit payload length */
#define QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT 14

/* Length of the optional data mask field in header */
#define QIO_CHANNEL_WEBSOCK_HEADER_LEN_MASK 4

/* Maximum length that can fit in 7-bit payload size */
#define QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_7_BIT 126
/* Maximum length that can fit in 16-bit payload size */
#define QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_16_BIT 65536

/* Magic 7-bit length to indicate use of 16-bit payload length */
#define QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT 126
/* Magic 7-bit length to indicate use of 64-bit payload length */
#define QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_64_BIT 127

/* Bitmasks for accessing header fields */
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_FIN 0x80
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE 0x0f
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_HAS_MASK 0x80
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_PAYLOAD_LEN 0x7f
#define QIO_CHANNEL_WEBSOCK_CONTROL_OPCODE_MASK 0x8

typedef struct QIOChannelWebsockHeader QIOChannelWebsockHeader;

struct QEMU_PACKED QIOChannelWebsockHeader {
    unsigned char b0;
    unsigned char b1;
    union {
        struct QEMU_PACKED {
            uint16_t l16;
            QIOChannelWebsockMask m16;
        } s16;
        struct QEMU_PACKED {
            uint64_t l64;
            QIOChannelWebsockMask m64;
        } s64;
        QIOChannelWebsockMask m;
    } u;
};

typedef struct QIOChannelWebsockHTTPHeader QIOChannelWebsockHTTPHeader;

struct QIOChannelWebsockHTTPHeader {
    char *name;
    char *value;
};

enum {
    QIO_CHANNEL_WEBSOCK_OPCODE_CONTINUATION = 0x0,
    QIO_CHANNEL_WEBSOCK_OPCODE_TEXT_FRAME = 0x1,
    QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME = 0x2,
    QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE = 0x8,
    QIO_CHANNEL_WEBSOCK_OPCODE_PING = 0x9,
    QIO_CHANNEL_WEBSOCK_OPCODE_PONG = 0xA
};

static void GCC_FMT_ATTR(2, 3)
qio_channel_websock_handshake_send_res(QIOChannelWebsock *ioc,
                                       const char *resmsg,
                                       ...)
{
    va_list vargs;
    char *response;
    size_t responselen;

    va_start(vargs, resmsg);
    response = g_strdup_vprintf(resmsg, vargs);
    responselen = strlen(response);
    buffer_reserve(&ioc->encoutput, responselen);
    buffer_append(&ioc->encoutput, response, responselen);
    g_free(response);
    va_end(vargs);
}

static gchar *qio_channel_websock_date_str(void)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_utc();

    return g_date_time_format(now, "%a, %d %b %Y %H:%M:%S GMT");
}

static void qio_channel_websock_handshake_send_res_err(QIOChannelWebsock *ioc,
                                                       const char *resdata)
{
    char *date = qio_channel_websock_date_str();
    qio_channel_websock_handshake_send_res(ioc, resdata, date);
    g_free(date);
}

enum {
    QIO_CHANNEL_WEBSOCK_STATUS_NORMAL = 1000,
    QIO_CHANNEL_WEBSOCK_STATUS_PROTOCOL_ERR = 1002,
    QIO_CHANNEL_WEBSOCK_STATUS_INVALID_DATA = 1003,
    QIO_CHANNEL_WEBSOCK_STATUS_POLICY = 1008,
    QIO_CHANNEL_WEBSOCK_STATUS_TOO_LARGE = 1009,
    QIO_CHANNEL_WEBSOCK_STATUS_SERVER_ERR = 1011,
};

static size_t
qio_channel_websock_extract_headers(QIOChannelWebsock *ioc,
                                    char *buffer,
                                    QIOChannelWebsockHTTPHeader *hdrs,
                                    size_t nhdrsalloc,
                                    Error **errp)
{
    char *nl, *sep, *tmp;
    size_t nhdrs = 0;

    /*
     * First parse the HTTP protocol greeting of format:
     *
     *   $METHOD $PATH $VERSION
     *
     * e.g.
     *
     *   GET / HTTP/1.1
     */

    nl = strstr(buffer, QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM);
    if (!nl) {
        error_setg(errp, "Missing HTTP header delimiter");
        goto bad_request;
    }
    *nl = '\0';
    trace_qio_channel_websock_http_greeting(ioc, buffer);

    tmp = strchr(buffer, ' ');
    if (!tmp) {
        error_setg(errp, "Missing HTTP path delimiter");
        return 0;
    }
    *tmp = '\0';

    if (!g_str_equal(buffer, QIO_CHANNEL_WEBSOCK_HTTP_METHOD)) {
        error_setg(errp, "Unsupported HTTP method %s", buffer);
        goto bad_request;
    }

    buffer = tmp + 1;
    tmp = strchr(buffer, ' ');
    if (!tmp) {
        error_setg(errp, "Missing HTTP version delimiter");
        goto bad_request;
    }
    *tmp = '\0';

    if (!g_str_equal(buffer, QIO_CHANNEL_WEBSOCK_HTTP_PATH)) {
        qio_channel_websock_handshake_send_res_err(
            ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_NOT_FOUND);
        error_setg(errp, "Unexpected HTTP path %s", buffer);
        return 0;
    }

    buffer = tmp + 1;

    if (!g_str_equal(buffer, QIO_CHANNEL_WEBSOCK_HTTP_VERSION)) {
        error_setg(errp, "Unsupported HTTP version %s", buffer);
        goto bad_request;
    }

    buffer = nl + strlen(QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM);

    /*
     * Now parse all the header fields of format
     *
     *   $NAME: $VALUE
     *
     * e.g.
     *
     *   Cache-control: no-cache
     */
    do {
        QIOChannelWebsockHTTPHeader *hdr;

        nl = strstr(buffer, QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM);
        if (nl) {
            *nl = '\0';
        }

        sep = strchr(buffer, ':');
        if (!sep) {
            error_setg(errp, "Malformed HTTP header");
            goto bad_request;
        }
        *sep = '\0';
        sep++;
        while (*sep == ' ') {
            sep++;
        }

        if (nhdrs >= nhdrsalloc) {
            error_setg(errp, "Too many HTTP headers");
            goto bad_request;
        }

        hdr = &hdrs[nhdrs++];
        hdr->name = buffer;
        hdr->value = sep;

        /* Canonicalize header name for easier identification later */
        for (tmp = hdr->name; *tmp; tmp++) {
            *tmp = g_ascii_tolower(*tmp);
        }

        if (nl) {
            buffer = nl + strlen(QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM);
        }
    } while (nl != NULL);

    return nhdrs;

 bad_request:
    qio_channel_websock_handshake_send_res_err(
        ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_BAD_REQUEST);
    return 0;
}

static const char *
qio_channel_websock_find_header(QIOChannelWebsockHTTPHeader *hdrs,
                                size_t nhdrs,
                                const char *name)
{
    size_t i;

    for (i = 0; i < nhdrs; i++) {
        if (g_str_equal(hdrs[i].name, name)) {
            return hdrs[i].value;
        }
    }

    return NULL;
}


static void qio_channel_websock_handshake_send_res_ok(QIOChannelWebsock *ioc,
                                                      const char *key,
                                                      const bool use_protocols,
                                                      Error **errp)
{
    char combined_key[QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN +
                      QIO_CHANNEL_WEBSOCK_GUID_LEN + 1];
    char *accept = NULL;
    char *date = NULL;

    g_strlcpy(combined_key, key, QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN + 1);
    g_strlcat(combined_key, QIO_CHANNEL_WEBSOCK_GUID,
              QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN +
              QIO_CHANNEL_WEBSOCK_GUID_LEN + 1);

    /* hash and encode it */
    if (qcrypto_hash_base64(QCRYPTO_HASH_ALG_SHA1,
                            combined_key,
                            QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN +
                            QIO_CHANNEL_WEBSOCK_GUID_LEN,
                            &accept,
                            errp) < 0) {
        qio_channel_websock_handshake_send_res_err(
            ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_SERVER_ERR);
        return;
    }

    date = qio_channel_websock_date_str();
    if (use_protocols) {
            qio_channel_websock_handshake_send_res(
                ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_WITH_PROTO_RES_OK,
                date, accept);
    } else {
            qio_channel_websock_handshake_send_res(
                ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_OK, date, accept);
    }

    g_free(date);
    g_free(accept);
}

static void qio_channel_websock_handshake_process(QIOChannelWebsock *ioc,
                                                  char *buffer,
                                                  Error **errp)
{
    QIOChannelWebsockHTTPHeader hdrs[32];
    size_t nhdrs = G_N_ELEMENTS(hdrs);
    const char *protocols = NULL, *version = NULL, *key = NULL,
        *host = NULL, *connection = NULL, *upgrade = NULL;
    char **connectionv;
    bool upgraded = false;
    size_t i;

    nhdrs = qio_channel_websock_extract_headers(ioc, buffer, hdrs, nhdrs, errp);
    if (!nhdrs) {
        return;
    }

    protocols = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_PROTOCOL);

    version = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_VERSION);
    if (!version) {
        error_setg(errp, "Missing websocket version header data");
        goto bad_request;
    }

    key = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_KEY);
    if (!key) {
        error_setg(errp, "Missing websocket key header data");
        goto bad_request;
    }

    host = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_HOST);
    if (!host) {
        error_setg(errp, "Missing websocket host header data");
        goto bad_request;
    }

    connection = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_CONNECTION);
    if (!connection) {
        error_setg(errp, "Missing websocket connection header data");
        goto bad_request;
    }

    upgrade = qio_channel_websock_find_header(
        hdrs, nhdrs, QIO_CHANNEL_WEBSOCK_HEADER_UPGRADE);
    if (!upgrade) {
        error_setg(errp, "Missing websocket upgrade header data");
        goto bad_request;
    }

    trace_qio_channel_websock_http_request(ioc, protocols, version,
                                           host, connection, upgrade, key);

    if (protocols) {
            if (!g_strrstr(protocols, QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY)) {
                error_setg(errp, "No '%s' protocol is supported by client '%s'",
                           QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY, protocols);
                goto bad_request;
            }
    }

    if (!g_str_equal(version, QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION)) {
        error_setg(errp, "Version '%s' is not supported by client '%s'",
                   QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION, version);
        goto bad_request;
    }

    if (strlen(key) != QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN) {
        error_setg(errp, "Key length '%zu' was not as expected '%d'",
                   strlen(key), QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN);
        goto bad_request;
    }

    connectionv = g_strsplit(connection, ",", 0);
    for (i = 0; connectionv != NULL && connectionv[i] != NULL; i++) {
        g_strstrip(connectionv[i]);
        if (strcasecmp(connectionv[i],
                       QIO_CHANNEL_WEBSOCK_CONNECTION_UPGRADE) == 0) {
            upgraded = true;
        }
    }
    g_strfreev(connectionv);
    if (!upgraded) {
        error_setg(errp, "No connection upgrade requested '%s'", connection);
        goto bad_request;
    }

    if (strcasecmp(upgrade, QIO_CHANNEL_WEBSOCK_UPGRADE_WEBSOCKET) != 0) {
        error_setg(errp, "Incorrect upgrade method '%s'", upgrade);
        goto bad_request;
    }

    qio_channel_websock_handshake_send_res_ok(ioc, key, !!protocols, errp);
    return;

 bad_request:
    qio_channel_websock_handshake_send_res_err(
        ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_BAD_REQUEST);
}

static int qio_channel_websock_handshake_read(QIOChannelWebsock *ioc,
                                              Error **errp)
{
    char *handshake_end;
    ssize_t ret;
    /* Typical HTTP headers from novnc are 512 bytes, so limiting
     * total header size to 4096 is easily enough. */
    size_t want = 4096 - ioc->encinput.offset;
    buffer_reserve(&ioc->encinput, want);
    ret = qio_channel_read(ioc->master,
                           (char *)buffer_end(&ioc->encinput), want, errp);
    if (ret < 0) {
        return -1;
    }
    ioc->encinput.offset += ret;

    handshake_end = g_strstr_len((char *)ioc->encinput.buffer,
                                 ioc->encinput.offset,
                                 QIO_CHANNEL_WEBSOCK_HANDSHAKE_END);
    if (!handshake_end) {
        if (ioc->encinput.offset >= 4096) {
            qio_channel_websock_handshake_send_res_err(
                ioc, QIO_CHANNEL_WEBSOCK_HANDSHAKE_RES_TOO_LARGE);
            error_setg(errp,
                       "End of headers not found in first 4096 bytes");
            return 1;
        } else if (ret == 0) {
            error_setg(errp,
                       "End of headers not found before connection closed");
            return -1;
        }
        return 0;
    }
    *handshake_end = '\0';

    qio_channel_websock_handshake_process(ioc,
                                          (char *)ioc->encinput.buffer,
                                          errp);

    buffer_advance(&ioc->encinput,
                   handshake_end - (char *)ioc->encinput.buffer +
                   strlen(QIO_CHANNEL_WEBSOCK_HANDSHAKE_END));
    return 1;
}

static gboolean qio_channel_websock_handshake_send(QIOChannel *ioc,
                                                   GIOCondition condition,
                                                   gpointer user_data)
{
    QIOTask *task = user_data;
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(
        qio_task_get_source(task));
    Error *err = NULL;
    ssize_t ret;

    ret = qio_channel_write(wioc->master,
                            (char *)wioc->encoutput.buffer,
                            wioc->encoutput.offset,
                            &err);

    if (ret < 0) {
        trace_qio_channel_websock_handshake_fail(ioc, error_get_pretty(err));
        qio_task_set_error(task, err);
        qio_task_complete(task);
        return FALSE;
    }

    buffer_advance(&wioc->encoutput, ret);
    if (wioc->encoutput.offset == 0) {
        if (wioc->io_err) {
            trace_qio_channel_websock_handshake_fail(
                ioc, error_get_pretty(wioc->io_err));
            qio_task_set_error(task, wioc->io_err);
            wioc->io_err = NULL;
            qio_task_complete(task);
        } else {
            trace_qio_channel_websock_handshake_complete(ioc);
            qio_task_complete(task);
        }
        return FALSE;
    }
    trace_qio_channel_websock_handshake_pending(ioc, G_IO_OUT);
    return TRUE;
}

static gboolean qio_channel_websock_handshake_io(QIOChannel *ioc,
                                                 GIOCondition condition,
                                                 gpointer user_data)
{
    QIOTask *task = user_data;
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(
        qio_task_get_source(task));
    Error *err = NULL;
    int ret;

    ret = qio_channel_websock_handshake_read(wioc, &err);
    if (ret < 0) {
        /*
         * We only take this path on a fatal I/O error reading from
         * client connection, as most of the time we have an
         * HTTP 4xx err response to send instead
         */
        trace_qio_channel_websock_handshake_fail(ioc, error_get_pretty(err));
        qio_task_set_error(task, err);
        qio_task_complete(task);
        return FALSE;
    }
    if (ret == 0) {
        trace_qio_channel_websock_handshake_pending(ioc, G_IO_IN);
        /* need more data still */
        return TRUE;
    }

    error_propagate(&wioc->io_err, err);

    trace_qio_channel_websock_handshake_reply(ioc);
    qio_channel_add_watch(
        wioc->master,
        G_IO_OUT,
        qio_channel_websock_handshake_send,
        task,
        NULL);
    return FALSE;
}


static void qio_channel_websock_encode(QIOChannelWebsock *ioc,
                                       uint8_t opcode,
                                       const struct iovec *iov,
                                       size_t niov,
                                       size_t size)
{
    size_t header_size;
    size_t i;
    union {
        char buf[QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT];
        QIOChannelWebsockHeader ws;
    } header;

    assert(size <= iov_size(iov, niov));

    header.ws.b0 = QIO_CHANNEL_WEBSOCK_HEADER_FIELD_FIN |
        (opcode & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE);
    if (size < QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_7_BIT) {
        header.ws.b1 = (uint8_t)size;
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT;
    } else if (size < QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_16_BIT) {
        header.ws.b1 = QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT;
        header.ws.u.s16.l16 = cpu_to_be16((uint16_t)size);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_16_BIT;
    } else {
        header.ws.b1 = QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_64_BIT;
        header.ws.u.s64.l64 = cpu_to_be64(size);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT;
    }
    header_size -= QIO_CHANNEL_WEBSOCK_HEADER_LEN_MASK;

    trace_qio_channel_websock_encode(ioc, opcode, header_size, size);
    buffer_reserve(&ioc->encoutput, header_size + size);
    buffer_append(&ioc->encoutput, header.buf, header_size);
    for (i = 0; i < niov && size != 0; i++) {
        size_t want = iov[i].iov_len;
        if (want > size) {
            want = size;
        }
        buffer_append(&ioc->encoutput, iov[i].iov_base, want);
        size -= want;
    }
}


static ssize_t qio_channel_websock_write_wire(QIOChannelWebsock *, Error **);


static void qio_channel_websock_write_close(QIOChannelWebsock *ioc,
                                            uint16_t code, const char *reason)
{
    struct iovec iov[2] = {
        { .iov_base = &code, .iov_len = sizeof(code) },
    };
    size_t niov = 1;
    size_t size = iov[0].iov_len;

    cpu_to_be16s(&code);

    if (reason) {
        iov[1].iov_base = (void *)reason;
        iov[1].iov_len = strlen(reason);
        size += iov[1].iov_len;
        niov++;
    }
    qio_channel_websock_encode(ioc, QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE,
                               iov, niov, size);
    qio_channel_websock_write_wire(ioc, NULL);
    qio_channel_shutdown(ioc->master, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}


static int qio_channel_websock_decode_header(QIOChannelWebsock *ioc,
                                             Error **errp)
{
    unsigned char opcode, fin, has_mask;
    size_t header_size;
    size_t payload_len;
    QIOChannelWebsockHeader *header =
        (QIOChannelWebsockHeader *)ioc->encinput.buffer;

    if (ioc->payload_remain) {
        error_setg(errp,
                   "Decoding header but %zu bytes of payload remain",
                   ioc->payload_remain);
        qio_channel_websock_write_close(
            ioc, QIO_CHANNEL_WEBSOCK_STATUS_SERVER_ERR,
            "internal server error");
        return -1;
    }
    if (ioc->encinput.offset < QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT) {
        /* header not complete */
        return QIO_CHANNEL_ERR_BLOCK;
    }

    fin = header->b0 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_FIN;
    opcode = header->b0 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE;
    has_mask = header->b1 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_HAS_MASK;
    payload_len = header->b1 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_PAYLOAD_LEN;

    /* Save or restore opcode. */
    if (opcode) {
        ioc->opcode = opcode;
    } else {
        opcode = ioc->opcode;
    }

    trace_qio_channel_websock_header_partial_decode(ioc, payload_len,
                                                    fin, opcode, (int)has_mask);

    if (opcode == QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE) {
        /* disconnect */
        return 0;
    }

    /* Websocket frame sanity check:
     * * Fragmentation is only supported for binary frames.
     * * All frames sent by a client MUST be masked.
     * * Only binary and ping/pong encoding is supported.
     */
    if (!fin) {
        if (opcode != QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME) {
            error_setg(errp, "only binary websocket frames may be fragmented");
            qio_channel_websock_write_close(
                ioc, QIO_CHANNEL_WEBSOCK_STATUS_POLICY ,
                "only binary frames may be fragmented");
            return -1;
        }
    } else {
        if (opcode != QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME &&
            opcode != QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE &&
            opcode != QIO_CHANNEL_WEBSOCK_OPCODE_PING &&
            opcode != QIO_CHANNEL_WEBSOCK_OPCODE_PONG) {
            error_setg(errp, "unsupported opcode: 0x%04x; only binary, close, "
                       "ping, and pong websocket frames are supported", opcode);
            qio_channel_websock_write_close(
                ioc, QIO_CHANNEL_WEBSOCK_STATUS_INVALID_DATA ,
                "only binary, close, ping, and pong frames are supported");
            return -1;
        }
    }
    if (!has_mask) {
        error_setg(errp, "client websocket frames must be masked");
        qio_channel_websock_write_close(
            ioc, QIO_CHANNEL_WEBSOCK_STATUS_PROTOCOL_ERR,
            "client frames must be masked");
        return -1;
    }

    if (payload_len < QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT) {
        ioc->payload_remain = payload_len;
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT;
        ioc->mask = header->u.m;
    } else if (opcode & QIO_CHANNEL_WEBSOCK_CONTROL_OPCODE_MASK) {
        error_setg(errp, "websocket control frame is too large");
        qio_channel_websock_write_close(
            ioc, QIO_CHANNEL_WEBSOCK_STATUS_PROTOCOL_ERR,
            "control frame is too large");
        return -1;
    } else if (payload_len == QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT &&
               ioc->encinput.offset >= QIO_CHANNEL_WEBSOCK_HEADER_LEN_16_BIT) {
        ioc->payload_remain = be16_to_cpu(header->u.s16.l16);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_16_BIT;
        ioc->mask = header->u.s16.m16;
    } else if (payload_len == QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_64_BIT &&
               ioc->encinput.offset >= QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT) {
        ioc->payload_remain = be64_to_cpu(header->u.s64.l64);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT;
        ioc->mask = header->u.s64.m64;
    } else {
        /* header not complete */
        return QIO_CHANNEL_ERR_BLOCK;
    }

    trace_qio_channel_websock_header_full_decode(
        ioc, header_size, ioc->payload_remain, ioc->mask.u);
    buffer_advance(&ioc->encinput, header_size);
    return 0;
}


static int qio_channel_websock_decode_payload(QIOChannelWebsock *ioc,
                                              Error **errp)
{
    size_t i;
    size_t payload_len = 0;
    uint32_t *payload32;

    if (ioc->payload_remain) {
        /* If we aren't at the end of the payload, then drop
         * off the last bytes, so we're always multiple of 4
         * for purpose of unmasking, except at end of payload
         */
        if (ioc->encinput.offset < ioc->payload_remain) {
            /* Wait for the entire payload before processing control frames
             * because the payload will most likely be echoed back. */
            if (ioc->opcode & QIO_CHANNEL_WEBSOCK_CONTROL_OPCODE_MASK) {
                return QIO_CHANNEL_ERR_BLOCK;
            }
            payload_len = ioc->encinput.offset - (ioc->encinput.offset % 4);
        } else {
            payload_len = ioc->payload_remain;
        }
        if (payload_len == 0) {
            return QIO_CHANNEL_ERR_BLOCK;
        }

        ioc->payload_remain -= payload_len;

        /* unmask frame */
        /* process 1 frame (32 bit op) */
        payload32 = (uint32_t *)ioc->encinput.buffer;
        for (i = 0; i < payload_len / 4; i++) {
            payload32[i] ^= ioc->mask.u;
        }
        /* process the remaining bytes (if any) */
        for (i *= 4; i < payload_len; i++) {
            ioc->encinput.buffer[i] ^= ioc->mask.c[i % 4];
        }
    }

    trace_qio_channel_websock_payload_decode(
        ioc, ioc->opcode, ioc->payload_remain);

    if (ioc->opcode == QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME) {
        if (payload_len) {
            /* binary frames are passed on */
            buffer_reserve(&ioc->rawinput, payload_len);
            buffer_append(&ioc->rawinput, ioc->encinput.buffer, payload_len);
        }
    } else if (ioc->opcode == QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE) {
        /* close frames are echoed back */
        error_setg(errp, "websocket closed by peer");
        if (payload_len) {
            /* echo client status */
            struct iovec iov = { .iov_base = ioc->encinput.buffer,
                                 .iov_len = ioc->encinput.offset };
            qio_channel_websock_encode(ioc, QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE,
                                       &iov, 1, iov.iov_len);
            qio_channel_websock_write_wire(ioc, NULL);
            qio_channel_shutdown(ioc->master, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        } else {
            /* send our own status */
            qio_channel_websock_write_close(
                ioc, QIO_CHANNEL_WEBSOCK_STATUS_NORMAL, "peer requested close");
        }
        return -1;
    } else if (ioc->opcode == QIO_CHANNEL_WEBSOCK_OPCODE_PING) {
        /* ping frames produce an immediate reply, as long as we've not still
         * got a previous pong queued, in which case we drop the new pong */
        if (ioc->pong_remain == 0) {
            struct iovec iov = { .iov_base = ioc->encinput.buffer,
                                 .iov_len = ioc->encinput.offset };
            qio_channel_websock_encode(ioc, QIO_CHANNEL_WEBSOCK_OPCODE_PONG,
                                       &iov, 1, iov.iov_len);
            ioc->pong_remain = ioc->encoutput.offset;
        }
    }   /* pong frames are ignored */

    if (payload_len) {
        buffer_advance(&ioc->encinput, payload_len);
    }
    return 0;
}


QIOChannelWebsock *
qio_channel_websock_new_server(QIOChannel *master)
{
    QIOChannelWebsock *wioc;
    QIOChannel *ioc;

    wioc = QIO_CHANNEL_WEBSOCK(object_new(TYPE_QIO_CHANNEL_WEBSOCK));
    ioc = QIO_CHANNEL(wioc);

    wioc->master = master;
    if (qio_channel_has_feature(master, QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        qio_channel_set_feature(ioc, QIO_CHANNEL_FEATURE_SHUTDOWN);
    }
    object_ref(OBJECT(master));

    trace_qio_channel_websock_new_server(wioc, master);
    return wioc;
}

void qio_channel_websock_handshake(QIOChannelWebsock *ioc,
                                   QIOTaskFunc func,
                                   gpointer opaque,
                                   GDestroyNotify destroy)
{
    QIOTask *task;

    task = qio_task_new(OBJECT(ioc),
                        func,
                        opaque,
                        destroy);

    trace_qio_channel_websock_handshake_start(ioc);
    trace_qio_channel_websock_handshake_pending(ioc, G_IO_IN);
    qio_channel_add_watch(ioc->master,
                          G_IO_IN,
                          qio_channel_websock_handshake_io,
                          task,
                          NULL);
}


static void qio_channel_websock_finalize(Object *obj)
{
    QIOChannelWebsock *ioc = QIO_CHANNEL_WEBSOCK(obj);

    buffer_free(&ioc->encinput);
    buffer_free(&ioc->encoutput);
    buffer_free(&ioc->rawinput);
    object_unref(OBJECT(ioc->master));
    if (ioc->io_tag) {
        g_source_remove(ioc->io_tag);
    }
    if (ioc->io_err) {
        error_free(ioc->io_err);
    }
}


static ssize_t qio_channel_websock_read_wire(QIOChannelWebsock *ioc,
                                             Error **errp)
{
    ssize_t ret;

    if (ioc->encinput.offset < 4096) {
        size_t want = 4096 - ioc->encinput.offset;

        buffer_reserve(&ioc->encinput, want);
        ret = qio_channel_read(ioc->master,
                               (char *)ioc->encinput.buffer +
                               ioc->encinput.offset,
                               want,
                               errp);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0 && ioc->encinput.offset == 0) {
            ioc->io_eof = TRUE;
            return 0;
        }
        ioc->encinput.offset += ret;
    }

    while (ioc->encinput.offset != 0) {
        if (ioc->payload_remain == 0) {
            ret = qio_channel_websock_decode_header(ioc, errp);
            if (ret < 0) {
                return ret;
            }
        }

        ret = qio_channel_websock_decode_payload(ioc, errp);
        if (ret < 0) {
            return ret;
        }
    }
    return 1;
}


static ssize_t qio_channel_websock_write_wire(QIOChannelWebsock *ioc,
                                              Error **errp)
{
    ssize_t ret;
    ssize_t done = 0;

    while (ioc->encoutput.offset > 0) {
        ret = qio_channel_write(ioc->master,
                                (char *)ioc->encoutput.buffer,
                                ioc->encoutput.offset,
                                errp);
        if (ret < 0) {
            if (ret == QIO_CHANNEL_ERR_BLOCK &&
                done > 0) {
                return done;
            } else {
                return ret;
            }
        }
        buffer_advance(&ioc->encoutput, ret);
        done += ret;
        if (ioc->pong_remain < ret) {
            ioc->pong_remain = 0;
        } else {
            ioc->pong_remain -= ret;
        }
    }
    return done;
}


static void qio_channel_websock_flush_free(gpointer user_data)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(user_data);
    object_unref(OBJECT(wioc));
}

static void qio_channel_websock_set_watch(QIOChannelWebsock *ioc);

static gboolean qio_channel_websock_flush(QIOChannel *ioc,
                                          GIOCondition condition,
                                          gpointer user_data)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(user_data);
    ssize_t ret;

    if (condition & G_IO_OUT) {
        ret = qio_channel_websock_write_wire(wioc, &wioc->io_err);
        if (ret < 0) {
            goto cleanup;
        }
    }

    if (condition & G_IO_IN) {
        ret = qio_channel_websock_read_wire(wioc, &wioc->io_err);
        if (ret < 0) {
            goto cleanup;
        }
    }

 cleanup:
    qio_channel_websock_set_watch(wioc);
    return FALSE;
}


static void qio_channel_websock_unset_watch(QIOChannelWebsock *ioc)
{
    if (ioc->io_tag) {
        g_source_remove(ioc->io_tag);
        ioc->io_tag = 0;
    }
}

static void qio_channel_websock_set_watch(QIOChannelWebsock *ioc)
{
    GIOCondition cond = 0;

    qio_channel_websock_unset_watch(ioc);

    if (ioc->io_err) {
        return;
    }

    if (ioc->encoutput.offset) {
        cond |= G_IO_OUT;
    }
    if (ioc->encinput.offset < QIO_CHANNEL_WEBSOCK_MAX_BUFFER &&
        !ioc->io_eof) {
        cond |= G_IO_IN;
    }

    if (cond) {
        object_ref(OBJECT(ioc));
        ioc->io_tag =
            qio_channel_add_watch(ioc->master,
                                  cond,
                                  qio_channel_websock_flush,
                                  ioc,
                                  qio_channel_websock_flush_free);
    }
}


static ssize_t qio_channel_websock_readv(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int **fds,
                                         size_t *nfds,
                                         Error **errp)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(ioc);
    size_t i;
    ssize_t got = 0;
    ssize_t ret;

    if (wioc->io_err) {
        error_propagate(errp, error_copy(wioc->io_err));
        return -1;
    }

    if (!wioc->rawinput.offset) {
        ret = qio_channel_websock_read_wire(QIO_CHANNEL_WEBSOCK(ioc), errp);
        if (ret < 0) {
            return ret;
        }
    }

    for (i = 0 ; i < niov ; i++) {
        size_t want = iov[i].iov_len;
        if (want > (wioc->rawinput.offset - got)) {
            want = (wioc->rawinput.offset - got);
        }

        memcpy(iov[i].iov_base,
               wioc->rawinput.buffer + got,
               want);
        got += want;

        if (want < iov[i].iov_len) {
            break;
        }
    }

    buffer_advance(&wioc->rawinput, got);
    qio_channel_websock_set_watch(wioc);
    return got;
}


static ssize_t qio_channel_websock_writev(QIOChannel *ioc,
                                          const struct iovec *iov,
                                          size_t niov,
                                          int *fds,
                                          size_t nfds,
                                          Error **errp)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(ioc);
    ssize_t want = iov_size(iov, niov);
    ssize_t avail;
    ssize_t ret;

    if (wioc->io_err) {
        error_propagate(errp, error_copy(wioc->io_err));
        return -1;
    }

    if (wioc->io_eof) {
        error_setg(errp, "%s", "Broken pipe");
        return -1;
    }

    avail = wioc->encoutput.offset >= QIO_CHANNEL_WEBSOCK_MAX_BUFFER ?
        0 : (QIO_CHANNEL_WEBSOCK_MAX_BUFFER - wioc->encoutput.offset);
    if (want > avail) {
        want = avail;
    }

    if (want) {
        qio_channel_websock_encode(wioc,
                                   QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME,
                                   iov, niov, want);
    }

    /* Even if want == 0, we'll try write_wire in case there's
     * pending data we could usefully flush out
     */
    ret = qio_channel_websock_write_wire(wioc, errp);
    if (ret < 0 &&
        ret != QIO_CHANNEL_ERR_BLOCK) {
        qio_channel_websock_unset_watch(wioc);
        return -1;
    }

    qio_channel_websock_set_watch(wioc);

    if (want == 0) {
        return QIO_CHANNEL_ERR_BLOCK;
    }

    return want;
}

static int qio_channel_websock_set_blocking(QIOChannel *ioc,
                                            bool enabled,
                                            Error **errp)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(ioc);

    qio_channel_set_blocking(wioc->master, enabled, errp);
    return 0;
}

static void qio_channel_websock_set_delay(QIOChannel *ioc,
                                          bool enabled)
{
    QIOChannelWebsock *tioc = QIO_CHANNEL_WEBSOCK(ioc);

    qio_channel_set_delay(tioc->master, enabled);
}

static void qio_channel_websock_set_cork(QIOChannel *ioc,
                                         bool enabled)
{
    QIOChannelWebsock *tioc = QIO_CHANNEL_WEBSOCK(ioc);

    qio_channel_set_cork(tioc->master, enabled);
}

static int qio_channel_websock_shutdown(QIOChannel *ioc,
                                        QIOChannelShutdown how,
                                        Error **errp)
{
    QIOChannelWebsock *tioc = QIO_CHANNEL_WEBSOCK(ioc);

    return qio_channel_shutdown(tioc->master, how, errp);
}

static int qio_channel_websock_close(QIOChannel *ioc,
                                     Error **errp)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(ioc);

    trace_qio_channel_websock_close(ioc);
    return qio_channel_close(wioc->master, errp);
}

typedef struct QIOChannelWebsockSource QIOChannelWebsockSource;
struct QIOChannelWebsockSource {
    GSource parent;
    QIOChannelWebsock *wioc;
    GIOCondition condition;
};

static gboolean
qio_channel_websock_source_check(GSource *source)
{
    QIOChannelWebsockSource *wsource = (QIOChannelWebsockSource *)source;
    GIOCondition cond = 0;

    if (wsource->wioc->rawinput.offset) {
        cond |= G_IO_IN;
    }
    if (wsource->wioc->encoutput.offset < QIO_CHANNEL_WEBSOCK_MAX_BUFFER) {
        cond |= G_IO_OUT;
    }
    if (wsource->wioc->io_eof) {
        cond |= G_IO_HUP;
    }
    if (wsource->wioc->io_err) {
        cond |= G_IO_ERR;
    }

    return cond & wsource->condition;
}

static gboolean
qio_channel_websock_source_prepare(GSource *source,
                                   gint *timeout)
{
    *timeout = -1;
    return qio_channel_websock_source_check(source);
}

static gboolean
qio_channel_websock_source_dispatch(GSource *source,
                                    GSourceFunc callback,
                                    gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelWebsockSource *wsource = (QIOChannelWebsockSource *)source;

    return (*func)(QIO_CHANNEL(wsource->wioc),
                   qio_channel_websock_source_check(source),
                   user_data);
}

static void
qio_channel_websock_source_finalize(GSource *source)
{
    QIOChannelWebsockSource *ssource = (QIOChannelWebsockSource *)source;

    object_unref(OBJECT(ssource->wioc));
}

GSourceFuncs qio_channel_websock_source_funcs = {
    qio_channel_websock_source_prepare,
    qio_channel_websock_source_check,
    qio_channel_websock_source_dispatch,
    qio_channel_websock_source_finalize
};

static GSource *qio_channel_websock_create_watch(QIOChannel *ioc,
                                                 GIOCondition condition)
{
    QIOChannelWebsock *wioc = QIO_CHANNEL_WEBSOCK(ioc);
    QIOChannelWebsockSource *ssource;
    GSource *source;

    source = g_source_new(&qio_channel_websock_source_funcs,
                          sizeof(QIOChannelWebsockSource));
    ssource = (QIOChannelWebsockSource *)source;

    ssource->wioc = wioc;
    object_ref(OBJECT(wioc));

    ssource->condition = condition;

    qio_channel_websock_set_watch(wioc);
    return source;
}

static void qio_channel_websock_class_init(ObjectClass *klass,
                                           void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_websock_writev;
    ioc_klass->io_readv = qio_channel_websock_readv;
    ioc_klass->io_set_blocking = qio_channel_websock_set_blocking;
    ioc_klass->io_set_cork = qio_channel_websock_set_cork;
    ioc_klass->io_set_delay = qio_channel_websock_set_delay;
    ioc_klass->io_close = qio_channel_websock_close;
    ioc_klass->io_shutdown = qio_channel_websock_shutdown;
    ioc_klass->io_create_watch = qio_channel_websock_create_watch;
}

static const TypeInfo qio_channel_websock_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_WEBSOCK,
    .instance_size = sizeof(QIOChannelWebsock),
    .instance_finalize = qio_channel_websock_finalize,
    .class_init = qio_channel_websock_class_init,
};

static void qio_channel_websock_register_types(void)
{
    type_register_static(&qio_channel_websock_info);
}

type_init(qio_channel_websock_register_types);
