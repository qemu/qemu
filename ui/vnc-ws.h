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

#ifndef __QEMU_UI_VNC_WS_H
#define __QEMU_UI_VNC_WS_H

#include <gnutls/gnutls.h>

#define B64LEN(__x) (((__x + 2) / 3) * 12 / 3)
#define SHA1_DIGEST_LEN 20

#define WS_ACCEPT_LEN (B64LEN(SHA1_DIGEST_LEN) + 1)
#define WS_CLIENT_KEY_LEN 24
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_GUID_LEN strlen(WS_GUID)

#define WS_HANDSHAKE "HTTP/1.1 101 Switching Protocols\r\n\
Upgrade: websocket\r\n\
Connection: Upgrade\r\n\
Sec-WebSocket-Accept: %s\r\n\
Sec-WebSocket-Protocol: binary\r\n\
\r\n"
#define WS_HANDSHAKE_DELIM "\r\n"
#define WS_HANDSHAKE_END "\r\n\r\n"
#define WS_SUPPORTED_VERSION "13"

#define WS_HEAD_MIN_LEN sizeof(uint16_t)
#define WS_HEAD_MAX_LEN (WS_HEAD_MIN_LEN + sizeof(uint64_t) + sizeof(uint32_t))

typedef union WsMask {
    char c[4];
    uint32_t u;
} WsMask;

typedef struct QEMU_PACKED WsHeader {
    unsigned char b0;
    unsigned char b1;
    union {
        struct QEMU_PACKED {
            uint16_t l16;
            WsMask m16;
        } s16;
        struct QEMU_PACKED {
            uint64_t l64;
            WsMask m64;
        } s64;
        WsMask m;
    } u;
} WsHeader;

enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT_FRAME = 0x1,
    WS_OPCODE_BINARY_FRAME = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
};

#ifdef CONFIG_VNC_TLS
void vncws_tls_handshake_peek(void *opaque);
#endif /* CONFIG_VNC_TLS */
void vncws_handshake_read(void *opaque);
long vnc_client_write_ws(VncState *vs);
long vnc_client_read_ws(VncState *vs);
void vncws_process_handshake(VncState *vs, uint8_t *line, size_t size);
void vncws_encode_frame(Buffer *output, const void *payload,
            const size_t payload_size);
int vncws_decode_frame(Buffer *input, uint8_t **payload,
                               size_t *payload_size, size_t *frame_size);

#endif /* __QEMU_UI_VNC_WS_H */
