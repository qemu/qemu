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

#include "qemu/osdep.h"
#include "io/channel-websock.h"
#include "crypto/hash.h"
#include "trace.h"


/* Max amount to allow in rawinput/rawoutput buffers */
#define QIO_CHANNEL_WEBSOCK_MAX_BUFFER 8192

#define QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN 24
#define QIO_CHANNEL_WEBSOCK_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define QIO_CHANNEL_WEBSOCK_GUID_LEN strlen(QIO_CHANNEL_WEBSOCK_GUID)

#define QIO_CHANNEL_WEBSOCK_HEADER_PROTOCOL "Sec-WebSocket-Protocol"
#define QIO_CHANNEL_WEBSOCK_HEADER_VERSION "Sec-WebSocket-Version"
#define QIO_CHANNEL_WEBSOCK_HEADER_KEY "Sec-WebSocket-Key"

#define QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY "binary"

#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_RESPONSE  \
    "HTTP/1.1 101 Switching Protocols\r\n"      \
    "Upgrade: websocket\r\n"                    \
    "Connection: Upgrade\r\n"                   \
    "Sec-WebSocket-Accept: %s\r\n"              \
    "Sec-WebSocket-Protocol: binary\r\n"        \
    "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM "\r\n"
#define QIO_CHANNEL_WEBSOCK_HANDSHAKE_END "\r\n\r\n"
#define QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION "13"

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

/* Bitmasks & shifts for accessing header fields */
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_FIN 0x80
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE 0x0f
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_HAS_MASK 0x80
#define QIO_CHANNEL_WEBSOCK_HEADER_FIELD_PAYLOAD_LEN 0x7f
#define QIO_CHANNEL_WEBSOCK_HEADER_SHIFT_FIN 7
#define QIO_CHANNEL_WEBSOCK_HEADER_SHIFT_HAS_MASK 7

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

enum {
    QIO_CHANNEL_WEBSOCK_OPCODE_CONTINUATION = 0x0,
    QIO_CHANNEL_WEBSOCK_OPCODE_TEXT_FRAME = 0x1,
    QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME = 0x2,
    QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE = 0x8,
    QIO_CHANNEL_WEBSOCK_OPCODE_PING = 0x9,
    QIO_CHANNEL_WEBSOCK_OPCODE_PONG = 0xA
};

static char *qio_channel_websock_handshake_entry(const char *handshake,
                                                 size_t handshake_len,
                                                 const char *name)
{
    char *begin, *end, *ret = NULL;
    char *line = g_strdup_printf("%s%s: ",
                                 QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM,
                                 name);
    begin = g_strstr_len(handshake, handshake_len, line);
    if (begin != NULL) {
        begin += strlen(line);
        end = g_strstr_len(begin, handshake_len - (begin - handshake),
                QIO_CHANNEL_WEBSOCK_HANDSHAKE_DELIM);
        if (end != NULL) {
            ret = g_strndup(begin, end - begin);
        }
    }
    g_free(line);
    return ret;
}


static int qio_channel_websock_handshake_send_response(QIOChannelWebsock *ioc,
                                                       const char *key,
                                                       Error **errp)
{
    char combined_key[QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN +
                      QIO_CHANNEL_WEBSOCK_GUID_LEN + 1];
    char *accept = NULL, *response = NULL;
    size_t responselen;

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
        return -1;
    }

    response = g_strdup_printf(QIO_CHANNEL_WEBSOCK_HANDSHAKE_RESPONSE, accept);
    responselen = strlen(response);
    buffer_reserve(&ioc->encoutput, responselen);
    buffer_append(&ioc->encoutput, response, responselen);

    g_free(accept);
    g_free(response);

    return 0;
}

static int qio_channel_websock_handshake_process(QIOChannelWebsock *ioc,
                                                 const char *line,
                                                 size_t size,
                                                 Error **errp)
{
    int ret = -1;
    char *protocols = qio_channel_websock_handshake_entry(
        line, size, QIO_CHANNEL_WEBSOCK_HEADER_PROTOCOL);
    char *version = qio_channel_websock_handshake_entry(
        line, size, QIO_CHANNEL_WEBSOCK_HEADER_VERSION);
    char *key = qio_channel_websock_handshake_entry(
        line, size, QIO_CHANNEL_WEBSOCK_HEADER_KEY);

    if (!protocols) {
        error_setg(errp, "Missing websocket protocol header data");
        goto cleanup;
    }

    if (!version) {
        error_setg(errp, "Missing websocket version header data");
        goto cleanup;
    }

    if (!key) {
        error_setg(errp, "Missing websocket key header data");
        goto cleanup;
    }

    if (!g_strrstr(protocols, QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY)) {
        error_setg(errp, "No '%s' protocol is supported by client '%s'",
                   QIO_CHANNEL_WEBSOCK_PROTOCOL_BINARY, protocols);
        goto cleanup;
    }

    if (!g_str_equal(version, QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION)) {
        error_setg(errp, "Version '%s' is not supported by client '%s'",
                   QIO_CHANNEL_WEBSOCK_SUPPORTED_VERSION, version);
        goto cleanup;
    }

    if (strlen(key) != QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN) {
        error_setg(errp, "Key length '%zu' was not as expected '%d'",
                   strlen(key), QIO_CHANNEL_WEBSOCK_CLIENT_KEY_LEN);
        goto cleanup;
    }

    ret = qio_channel_websock_handshake_send_response(ioc, key, errp);

 cleanup:
    g_free(protocols);
    g_free(version);
    g_free(key);
    return ret;
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
            error_setg(errp,
                       "End of headers not found in first 4096 bytes");
            return -1;
        } else {
            return 0;
        }
    }

    if (qio_channel_websock_handshake_process(ioc,
                                              (char *)ioc->encinput.buffer,
                                              ioc->encinput.offset,
                                              errp) < 0) {
        return -1;
    }

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
        trace_qio_channel_websock_handshake_fail(ioc);
        qio_task_abort(task, err);
        error_free(err);
        return FALSE;
    }

    buffer_advance(&wioc->encoutput, ret);
    if (wioc->encoutput.offset == 0) {
        trace_qio_channel_websock_handshake_complete(ioc);
        qio_task_complete(task);
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
        trace_qio_channel_websock_handshake_fail(ioc);
        qio_task_abort(task, err);
        error_free(err);
        return FALSE;
    }
    if (ret == 0) {
        trace_qio_channel_websock_handshake_pending(ioc, G_IO_IN);
        /* need more data still */
        return TRUE;
    }

    object_ref(OBJECT(task));
    trace_qio_channel_websock_handshake_reply(ioc);
    qio_channel_add_watch(
        wioc->master,
        G_IO_OUT,
        qio_channel_websock_handshake_send,
        task,
        (GDestroyNotify)object_unref);
    return FALSE;
}


static void qio_channel_websock_encode(QIOChannelWebsock *ioc)
{
    size_t header_size;
    union {
        char buf[QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT];
        QIOChannelWebsockHeader ws;
    } header;

    if (!ioc->rawoutput.offset) {
        return;
    }

    header.ws.b0 = (1 << QIO_CHANNEL_WEBSOCK_HEADER_SHIFT_FIN) |
        (QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME &
         QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE);
    if (ioc->rawoutput.offset <
        QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_7_BIT) {
        header.ws.b1 = (uint8_t)ioc->rawoutput.offset;
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT;
    } else if (ioc->rawoutput.offset <
               QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_THRESHOLD_16_BIT) {
        header.ws.b1 = QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT;
        header.ws.u.s16.l16 = cpu_to_be16((uint16_t)ioc->rawoutput.offset);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_16_BIT;
    } else {
        header.ws.b1 = QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_64_BIT;
        header.ws.u.s64.l64 = cpu_to_be64(ioc->rawoutput.offset);
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_64_BIT;
    }
    header_size -= QIO_CHANNEL_WEBSOCK_HEADER_LEN_MASK;

    buffer_reserve(&ioc->encoutput, header_size + ioc->rawoutput.offset);
    buffer_append(&ioc->encoutput, header.buf, header_size);
    buffer_append(&ioc->encoutput, ioc->rawoutput.buffer,
                  ioc->rawoutput.offset);
    buffer_reset(&ioc->rawoutput);
}


static ssize_t qio_channel_websock_decode_header(QIOChannelWebsock *ioc,
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
        return -1;
    }
    if (ioc->encinput.offset < QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT) {
        /* header not complete */
        return QIO_CHANNEL_ERR_BLOCK;
    }

    fin = (header->b0 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_FIN) >>
        QIO_CHANNEL_WEBSOCK_HEADER_SHIFT_FIN;
    opcode = header->b0 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_OPCODE;
    has_mask = (header->b1 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_HAS_MASK) >>
        QIO_CHANNEL_WEBSOCK_HEADER_SHIFT_HAS_MASK;
    payload_len = header->b1 & QIO_CHANNEL_WEBSOCK_HEADER_FIELD_PAYLOAD_LEN;

    if (opcode == QIO_CHANNEL_WEBSOCK_OPCODE_CLOSE) {
        /* disconnect */
        return 0;
    }

    /* Websocket frame sanity check:
     * * Websocket fragmentation is not supported.
     * * All  websockets frames sent by a client have to be masked.
     * * Only binary encoding is supported.
     */
    if (!fin) {
        error_setg(errp, "websocket fragmentation is not supported");
        return -1;
    }
    if (!has_mask) {
        error_setg(errp, "websocket frames must be masked");
        return -1;
    }
    if (opcode != QIO_CHANNEL_WEBSOCK_OPCODE_BINARY_FRAME) {
        error_setg(errp, "only binary websocket frames are supported");
        return -1;
    }

    if (payload_len < QIO_CHANNEL_WEBSOCK_PAYLOAD_LEN_MAGIC_16_BIT) {
        ioc->payload_remain = payload_len;
        header_size = QIO_CHANNEL_WEBSOCK_HEADER_LEN_7_BIT;
        ioc->mask = header->u.m;
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

    buffer_advance(&ioc->encinput, header_size);
    return 1;
}


static ssize_t qio_channel_websock_decode_payload(QIOChannelWebsock *ioc,
                                                  Error **errp)
{
    size_t i;
    size_t payload_len;
    uint32_t *payload32;

    if (!ioc->payload_remain) {
        error_setg(errp,
                   "Decoding payload but no bytes of payload remain");
        return -1;
    }

    /* If we aren't at the end of the payload, then drop
     * off the last bytes, so we're always multiple of 4
     * for purpose of unmasking, except at end of payload
     */
    if (ioc->encinput.offset < ioc->payload_remain) {
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

    buffer_reserve(&ioc->rawinput, payload_len);
    buffer_append(&ioc->rawinput, ioc->encinput.buffer, payload_len);
    buffer_advance(&ioc->encinput, payload_len);
    return payload_len;
}


QIOChannelWebsock *
qio_channel_websock_new_server(QIOChannel *master)
{
    QIOChannelWebsock *wioc;
    QIOChannel *ioc;

    wioc = QIO_CHANNEL_WEBSOCK(object_new(TYPE_QIO_CHANNEL_WEBSOCK));
    ioc = QIO_CHANNEL(wioc);

    wioc->master = master;
    if (master->features & (1 << QIO_CHANNEL_FEATURE_SHUTDOWN)) {
        ioc->features |= (1 << QIO_CHANNEL_FEATURE_SHUTDOWN);
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
    buffer_free(&ioc->rawoutput);
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
        if (ret == 0 &&
            ioc->encinput.offset == 0) {
            return 0;
        }
        ioc->encinput.offset += ret;
    }

    if (ioc->payload_remain == 0) {
        ret = qio_channel_websock_decode_header(ioc, errp);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            return 0;
        }
    }

    ret = qio_channel_websock_decode_payload(ioc, errp);
    if (ret < 0) {
        return ret;
    }
    return ret;
}


static ssize_t qio_channel_websock_write_wire(QIOChannelWebsock *ioc,
                                              Error **errp)
{
    ssize_t ret;
    ssize_t done = 0;
    qio_channel_websock_encode(ioc);

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
        if (ret == 0) {
            wioc->io_eof = TRUE;
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
        *errp = error_copy(wioc->io_err);
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
    size_t i;
    ssize_t done = 0;
    ssize_t ret;

    if (wioc->io_err) {
        *errp = error_copy(wioc->io_err);
        return -1;
    }

    if (wioc->io_eof) {
        error_setg(errp, "%s", "Broken pipe");
        return -1;
    }

    for (i = 0; i < niov; i++) {
        size_t want = iov[i].iov_len;
        if ((want + wioc->rawoutput.offset) > QIO_CHANNEL_WEBSOCK_MAX_BUFFER) {
            want = (QIO_CHANNEL_WEBSOCK_MAX_BUFFER - wioc->rawoutput.offset);
        }
        if (want == 0) {
            goto done;
        }

        buffer_reserve(&wioc->rawoutput, want);
        buffer_append(&wioc->rawoutput, iov[i].iov_base, want);
        done += want;
        if (want < iov[i].iov_len) {
            break;
        }
    }

 done:
    ret = qio_channel_websock_write_wire(wioc, errp);
    if (ret < 0 &&
        ret != QIO_CHANNEL_ERR_BLOCK) {
        qio_channel_websock_unset_watch(wioc);
        return -1;
    }

    qio_channel_websock_set_watch(wioc);

    if (done == 0) {
        return QIO_CHANNEL_ERR_BLOCK;
    }

    return done;
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

    return qio_channel_close(wioc->master, errp);
}

typedef struct QIOChannelWebsockSource QIOChannelWebsockSource;
struct QIOChannelWebsockSource {
    GSource parent;
    QIOChannelWebsock *wioc;
    GIOCondition condition;
};

static gboolean
qio_channel_websock_source_prepare(GSource *source,
                                   gint *timeout)
{
    QIOChannelWebsockSource *wsource = (QIOChannelWebsockSource *)source;
    GIOCondition cond = 0;
    *timeout = -1;

    if (wsource->wioc->rawinput.offset) {
        cond |= G_IO_IN;
    }
    if (wsource->wioc->rawoutput.offset < QIO_CHANNEL_WEBSOCK_MAX_BUFFER) {
        cond |= G_IO_OUT;
    }

    return cond & wsource->condition;
}

static gboolean
qio_channel_websock_source_check(GSource *source)
{
    QIOChannelWebsockSource *wsource = (QIOChannelWebsockSource *)source;
    GIOCondition cond = 0;

    if (wsource->wioc->rawinput.offset) {
        cond |= G_IO_IN;
    }
    if (wsource->wioc->rawoutput.offset < QIO_CHANNEL_WEBSOCK_MAX_BUFFER) {
        cond |= G_IO_OUT;
    }

    return cond & wsource->condition;
}

static gboolean
qio_channel_websock_source_dispatch(GSource *source,
                                    GSourceFunc callback,
                                    gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelWebsockSource *wsource = (QIOChannelWebsockSource *)source;
    GIOCondition cond = 0;

    if (wsource->wioc->rawinput.offset) {
        cond |= G_IO_IN;
    }
    if (wsource->wioc->rawoutput.offset < QIO_CHANNEL_WEBSOCK_MAX_BUFFER) {
        cond |= G_IO_OUT;
    }

    return (*func)(QIO_CHANNEL(wsource->wioc),
                   (cond & wsource->condition),
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
