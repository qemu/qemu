/*
 * QEMU VNC display driver: Websockets support
 *
 * Copyright (C) 2010 Joel Martin
 * Copyright (C) 2012 Tim Hardeck
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, see <http://www.gnu.org/licenses/>.
 */

#include "vnc.h"
#include "qemu/main-loop.h"
#include "crypto/hash.h"

static int vncws_start_tls_handshake(VncState *vs)
{
    Error *err = NULL;

    if (qcrypto_tls_session_handshake(vs->tls, &err) < 0) {
        goto error;
    }

    switch (qcrypto_tls_session_get_handshake_status(vs->tls)) {
    case QCRYPTO_TLS_HANDSHAKE_COMPLETE:
        VNC_DEBUG("Handshake done, checking credentials\n");
        if (qcrypto_tls_session_check_credentials(vs->tls, &err) < 0) {
            goto error;
        }
        VNC_DEBUG("Client verification passed, starting TLS I/O\n");
        qemu_set_fd_handler(vs->csock, vncws_handshake_read, NULL, vs);
        break;

    case QCRYPTO_TLS_HANDSHAKE_RECVING:
        VNC_DEBUG("Handshake interrupted (blocking read)\n");
        qemu_set_fd_handler(vs->csock, vncws_tls_handshake_io, NULL, vs);
        break;

    case QCRYPTO_TLS_HANDSHAKE_SENDING:
        VNC_DEBUG("Handshake interrupted (blocking write)\n");
        qemu_set_fd_handler(vs->csock, NULL, vncws_tls_handshake_io, vs);
        break;
    }

    return 0;

 error:
    VNC_DEBUG("Handshake failed %s\n", error_get_pretty(err));
    error_free(err);
    vnc_client_error(vs);
    return -1;
}

void vncws_tls_handshake_io(void *opaque)
{
    VncState *vs = (VncState *)opaque;
    Error *err = NULL;

    vs->tls = qcrypto_tls_session_new(vs->vd->tlscreds,
                                      NULL,
                                      vs->vd->tlsaclname,
                                      QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
                                      &err);
    if (!vs->tls) {
        VNC_DEBUG("Failed to setup TLS %s\n",
                  error_get_pretty(err));
        error_free(err);
        vnc_client_error(vs);
        return;
    }

    qcrypto_tls_session_set_callbacks(vs->tls,
                                      vnc_tls_push,
                                      vnc_tls_pull,
                                      vs);

    VNC_DEBUG("Start TLS WS handshake process\n");
    vncws_start_tls_handshake(vs);
}

void vncws_handshake_read(void *opaque)
{
    VncState *vs = opaque;
    uint8_t *handshake_end;
    long ret;
    /* Typical HTTP headers from novnc are 512 bytes, so limiting
     * total header size to 4096 is easily enough. */
    size_t want = 4096 - vs->ws_input.offset;
    buffer_reserve(&vs->ws_input, want);
    ret = vnc_client_read_buf(vs, buffer_end(&vs->ws_input), want);

    if (!ret) {
        if (vs->csock == -1) {
            vnc_disconnect_finish(vs);
        }
        return;
    }
    vs->ws_input.offset += ret;

    handshake_end = (uint8_t *)g_strstr_len((char *)vs->ws_input.buffer,
            vs->ws_input.offset, WS_HANDSHAKE_END);
    if (handshake_end) {
        qemu_set_fd_handler(vs->csock, vnc_client_read, NULL, vs);
        vncws_process_handshake(vs, vs->ws_input.buffer, vs->ws_input.offset);
        buffer_advance(&vs->ws_input, handshake_end - vs->ws_input.buffer +
                strlen(WS_HANDSHAKE_END));
    } else if (vs->ws_input.offset >= 4096) {
        VNC_DEBUG("End of headers not found in first 4096 bytes\n");
        vnc_client_error(vs);
    }
}


long vnc_client_read_ws(VncState *vs)
{
    int ret, err;
    uint8_t *payload;
    size_t payload_size, header_size;
    VNC_DEBUG("Read websocket %p size %zd offset %zd\n", vs->ws_input.buffer,
            vs->ws_input.capacity, vs->ws_input.offset);
    buffer_reserve(&vs->ws_input, 4096);
    ret = vnc_client_read_buf(vs, buffer_end(&vs->ws_input), 4096);
    if (!ret) {
        return 0;
    }
    vs->ws_input.offset += ret;

    ret = 0;
    /* consume as much of ws_input buffer as possible */
    do {
        if (vs->ws_payload_remain == 0) {
            err = vncws_decode_frame_header(&vs->ws_input,
                                            &header_size,
                                            &vs->ws_payload_remain,
                                            &vs->ws_payload_mask);
            if (err <= 0) {
                return err;
            }

            buffer_advance(&vs->ws_input, header_size);
        }
        if (vs->ws_payload_remain != 0) {
            err = vncws_decode_frame_payload(&vs->ws_input,
                                             &vs->ws_payload_remain,
                                             &vs->ws_payload_mask,
                                             &payload,
                                             &payload_size);
            if (err < 0) {
                return err;
            }
            if (err == 0) {
                return ret;
            }
            ret += err;

            buffer_reserve(&vs->input, payload_size);
            buffer_append(&vs->input, payload, payload_size);

            buffer_advance(&vs->ws_input, payload_size);
        }
    } while (vs->ws_input.offset > 0);

    return ret;
}

long vnc_client_write_ws(VncState *vs)
{
    long ret;
    VNC_DEBUG("Write WS: Pending output %p size %zd offset %zd\n",
              vs->output.buffer, vs->output.capacity, vs->output.offset);
    vncws_encode_frame(&vs->ws_output, vs->output.buffer, vs->output.offset);
    buffer_reset(&vs->output);
    ret = vnc_client_write_buf(vs, vs->ws_output.buffer, vs->ws_output.offset);
    if (!ret) {
        return 0;
    }

    buffer_advance(&vs->ws_output, ret);

    if (vs->ws_output.offset == 0) {
        qemu_set_fd_handler(vs->csock, vnc_client_read, NULL, vs);
    }

    return ret;
}

static char *vncws_extract_handshake_entry(const char *handshake,
        size_t handshake_len, const char *name)
{
    char *begin, *end, *ret = NULL;
    char *line = g_strdup_printf("%s%s: ", WS_HANDSHAKE_DELIM, name);
    begin = g_strstr_len(handshake, handshake_len, line);
    if (begin != NULL) {
        begin += strlen(line);
        end = g_strstr_len(begin, handshake_len - (begin - handshake),
                WS_HANDSHAKE_DELIM);
        if (end != NULL) {
            ret = g_strndup(begin, end - begin);
        }
    }
    g_free(line);
    return ret;
}

static void vncws_send_handshake_response(VncState *vs, const char* key)
{
    char combined_key[WS_CLIENT_KEY_LEN + WS_GUID_LEN + 1];
    char *accept = NULL, *response = NULL;
    Error *err = NULL;

    g_strlcpy(combined_key, key, WS_CLIENT_KEY_LEN + 1);
    g_strlcat(combined_key, WS_GUID, WS_CLIENT_KEY_LEN + WS_GUID_LEN + 1);

    /* hash and encode it */
    if (qcrypto_hash_base64(QCRYPTO_HASH_ALG_SHA1,
                            combined_key,
                            WS_CLIENT_KEY_LEN + WS_GUID_LEN,
                            &accept,
                            &err) < 0) {
        VNC_DEBUG("Hashing Websocket combined key failed %s\n",
                  error_get_pretty(err));
        error_free(err);
        vnc_client_error(vs);
        return;
    }

    response = g_strdup_printf(WS_HANDSHAKE, accept);
    vnc_client_write_buf(vs, (const uint8_t *)response, strlen(response));

    g_free(accept);
    g_free(response);

    vs->encode_ws = 1;
    vnc_init_state(vs);
}

void vncws_process_handshake(VncState *vs, uint8_t *line, size_t size)
{
    char *protocols = vncws_extract_handshake_entry((const char *)line, size,
            "Sec-WebSocket-Protocol");
    char *version = vncws_extract_handshake_entry((const char *)line, size,
            "Sec-WebSocket-Version");
    char *key = vncws_extract_handshake_entry((const char *)line, size,
            "Sec-WebSocket-Key");

    if (protocols && version && key
            && g_strrstr(protocols, "binary")
            && !strcmp(version, WS_SUPPORTED_VERSION)
            && strlen(key) == WS_CLIENT_KEY_LEN) {
        vncws_send_handshake_response(vs, key);
    } else {
        VNC_DEBUG("Defective Websockets header or unsupported protocol\n");
        vnc_client_error(vs);
    }

    g_free(protocols);
    g_free(version);
    g_free(key);
}

void vncws_encode_frame(Buffer *output, const void *payload,
        const size_t payload_size)
{
    size_t header_size = 0;
    unsigned char opcode = WS_OPCODE_BINARY_FRAME;
    union {
        char buf[WS_HEAD_MAX_LEN];
        WsHeader ws;
    } header;

    if (!payload_size) {
        return;
    }

    header.ws.b0 = 0x80 | (opcode & 0x0f);
    if (payload_size <= 125) {
        header.ws.b1 = (uint8_t)payload_size;
        header_size = 2;
    } else if (payload_size < 65536) {
        header.ws.b1 = 0x7e;
        header.ws.u.s16.l16 = cpu_to_be16((uint16_t)payload_size);
        header_size = 4;
    } else {
        header.ws.b1 = 0x7f;
        header.ws.u.s64.l64 = cpu_to_be64(payload_size);
        header_size = 10;
    }

    buffer_reserve(output, header_size + payload_size);
    buffer_append(output, header.buf, header_size);
    buffer_append(output, payload, payload_size);
}

int vncws_decode_frame_header(Buffer *input,
                              size_t *header_size,
                              size_t *payload_remain,
                              WsMask *payload_mask)
{
    unsigned char opcode = 0, fin = 0, has_mask = 0;
    size_t payload_len;
    WsHeader *header = (WsHeader *)input->buffer;

    if (input->offset < WS_HEAD_MIN_LEN + 4) {
        /* header not complete */
        return 0;
    }

    fin = (header->b0 & 0x80) >> 7;
    opcode = header->b0 & 0x0f;
    has_mask = (header->b1 & 0x80) >> 7;
    payload_len = header->b1 & 0x7f;

    if (opcode == WS_OPCODE_CLOSE) {
        /* disconnect */
        return -1;
    }

    /* Websocket frame sanity check:
     * * Websocket fragmentation is not supported.
     * * All  websockets frames sent by a client have to be masked.
     * * Only binary encoding is supported.
     */
    if (!fin || !has_mask || opcode != WS_OPCODE_BINARY_FRAME) {
        VNC_DEBUG("Received faulty/unsupported Websocket frame\n");
        return -2;
    }

    if (payload_len < 126) {
        *payload_remain = payload_len;
        *header_size = 6;
        *payload_mask = header->u.m;
    } else if (payload_len == 126 && input->offset >= 8) {
        *payload_remain = be16_to_cpu(header->u.s16.l16);
        *header_size = 8;
        *payload_mask = header->u.s16.m16;
    } else if (payload_len == 127 && input->offset >= 14) {
        *payload_remain = be64_to_cpu(header->u.s64.l64);
        *header_size = 14;
        *payload_mask = header->u.s64.m64;
    } else {
        /* header not complete */
        return 0;
    }

    return 1;
}

int vncws_decode_frame_payload(Buffer *input,
                               size_t *payload_remain, WsMask *payload_mask,
                               uint8_t **payload, size_t *payload_size)
{
    size_t i;
    uint32_t *payload32;

    *payload = input->buffer;
    /* If we aren't at the end of the payload, then drop
     * off the last bytes, so we're always multiple of 4
     * for purpose of unmasking, except at end of payload
     */
    if (input->offset < *payload_remain) {
        *payload_size = input->offset - (input->offset % 4);
    } else {
        *payload_size = *payload_remain;
    }
    if (*payload_size == 0) {
        return 0;
    }
    *payload_remain -= *payload_size;

    /* unmask frame */
    /* process 1 frame (32 bit op) */
    payload32 = (uint32_t *)(*payload);
    for (i = 0; i < *payload_size / 4; i++) {
        payload32[i] ^= payload_mask->u;
    }
    /* process the remaining bytes (if any) */
    for (i *= 4; i < *payload_size; i++) {
        (*payload)[i] ^= payload_mask->c[i % 4];
    }

    return 1;
}
