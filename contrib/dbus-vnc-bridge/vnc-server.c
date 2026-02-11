/*
 * Minimal RFB (VNC) server for D-Bus VNC bridge
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vnc-server.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>

/* RFB 3.8 */
#define RFB_VERSION "RFB 003.008\n"
#define RFB_SECURITY_TYPE_NONE 1
#define RFB_SECURITY_RESULT_OK 0
#define RFB_ENCODING_RAW 0
#define RFB_SERVER_MSG_FRAMEBUFFER_UPDATE 0
#define RFB_CLIENT_MSG_SET_PIXEL_FORMAT 0
#define RFB_CLIENT_MSG_SET_ENCODINGS 2
#define RFB_CLIENT_MSG_FRAMEBUFFER_UPDATE_REQUEST 3
#define RFB_CLIENT_MSG_KEY_EVENT 4
#define RFB_CLIENT_MSG_POINTER_EVENT 5

struct VncClient {
    GSocketConnection *conn;
    GInputStream *istream;
    GOutputStream *ostream;
    GIOChannel *channel;
    guint width;
    guint height;
    uint8_t bits_per_pixel;
    uint8_t depth;
    uint8_t big_endian;
    uint8_t true_colour;
    uint16_t red_max, green_max, blue_max;
    uint8_t red_shift, green_shift, blue_shift;
    bool handshake_done;
    uint8_t read_buf[256];
    size_t read_len;
    VncServer *server;
};

struct VncServer {
    GSocketListener *listener;
    struct VncServerCallbacks callbacks;
    char *address;
};

static bool read_all(GInputStream *in, void *buf, size_t len, GError **err)
{
    size_t got = 0;
    while (got < len) {
        gsize n = 0;
        if (!g_input_stream_read_all(in, (char *)buf + got, len - got,
                                     &n, NULL, err)) {
            return false;
        }
        got += n;
    }
    return true;
}

static bool write_all(GOutputStream *out, const void *buf, size_t len, GError **err)
{
    size_t done = 0;
    while (done < len) {
        gsize n = 0;
        if (!g_output_stream_write_all(out, (const char *)buf + done, len - done,
                                       &n, NULL, err)) {
            return false;
        }
        done += n;
    }
    return true;
}

static uint16_t be16(uint16_t x)
{
    return GUINT16_TO_BE(x);
}

static uint32_t be32(uint32_t x)
{
    return GUINT32_TO_BE(x);
}

static bool send_server_init(VncClient *client, guint width, guint height,
                             const char *name, GError **err)
{
    uint8_t pf[16];
    uint32_t namelen;
    size_t namelen_val;

    client->width = width;
    client->height = height;
    /* Pixel format: 32 bpp, 24 depth, little-endian, true colour */
    client->bits_per_pixel = 32;
    client->depth = 24;
    client->big_endian = 0;
    client->true_colour = 1;
    client->red_max = 255;
    client->green_max = 255;
    client->blue_max = 255;
    client->red_shift = 16;
    client->green_shift = 8;
    client->blue_shift = 0;

    memset(pf, 0, sizeof(pf));
    pf[0] = 32;
    pf[1] = 24;
    pf[2] = 0; /* little endian */
    pf[3] = 1; /* true colour */
    pf[4] = 255; pf[5] = 0; /* red max */
    pf[6] = 255; pf[7] = 0; /* green max */
    pf[8] = 255; pf[9] = 0; /* blue max */
    pf[10] = 16; /* red shift */
    pf[12] = 8;
    pf[14] = 0;
    pf[15] = 0; /* padding */

    namelen_val = name ? strlen(name) : 0;
    namelen = (uint32_t)namelen_val;

    {
        uint16_t w16 = be16((uint16_t)width);
        uint16_t h16 = be16((uint16_t)height);
        uint32_t n32 = be32(namelen);
        if (!write_all(client->ostream, &w16, 2, err) ||
            !write_all(client->ostream, &h16, 2, err) ||
            !write_all(client->ostream, pf, 16, err) ||
            !write_all(client->ostream, &n32, 4, err)) {
            return false;
        }
    }
    if (namelen_val && !write_all(client->ostream, name, namelen_val, err)) {
        return false;
    }
    return true;
}

static bool process_client_message(VncClient *client, uint8_t type, GError **err)
{
    uint8_t buf[256];
    uint16_t u16;

    switch (type) {
    case RFB_CLIENT_MSG_SET_PIXEL_FORMAT:
        if (!read_all(client->istream, buf, 19, err)) {
            return false;
        }
        /* We only support one format; ignore */
        break;

    case RFB_CLIENT_MSG_SET_ENCODINGS:
        if (!read_all(client->istream, buf, 1, err)) {
            return false;
        }
        if (!read_all(client->istream, &u16, 2, err)) {
            return false;
        }
        {
            uint16_t n = GUINT16_FROM_BE(u16);
            if (n > 64) {
                n = 64;
            }
            if (!read_all(client->istream, buf, n * 4, err)) {
                return false;
            }
        }
        break;

    case RFB_CLIENT_MSG_FRAMEBUFFER_UPDATE_REQUEST:
        if (!read_all(client->istream, buf, 9, err)) {
            return false;
        }
        {
            int x = (int)(buf[0] << 8 | buf[1]);
            int y = (int)(buf[2] << 8 | buf[3]);
            int w = (int)(buf[4] << 8 | buf[5]);
            int h = (int)(buf[6] << 8 | buf[7]);
            bool incremental = (buf[8] != 0);
            client->server->callbacks.framebuffer_update_request(
                client, x, y, w, h, incremental, client->server->callbacks.user_data);
        }
        break;

    case RFB_CLIENT_MSG_KEY_EVENT:
        if (!read_all(client->istream, buf, 8, err)) {
            return false;
        }
        {
            bool down = (buf[0] != 0);
            uint32_t key = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
            client->server->callbacks.key_event(client, key, down,
                client->server->callbacks.user_data);
        }
        break;

    case RFB_CLIENT_MSG_POINTER_EVENT:
        if (!read_all(client->istream, buf, 5, err)) {
            return false;
        }
        {
            int x = (int)(buf[0] << 8 | buf[1]);
            int y = (int)(buf[2] << 8 | buf[3]);
            uint8_t buttons = buf[4];
            client->server->callbacks.pointer_event(client, x, y, buttons,
                client->server->callbacks.user_data);
        }
        break;

    default:
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Unknown RFB client message type %u", type);
        return false;
    }
    return true;
}

bool vnc_client_handle_read(VncClient *client, GError **err)
{
    if (!client->handshake_done) {
        return true; /* not used before handshake */
    }
    if (client->read_len < 1) {
        gsize n = 0;
        if (!g_input_stream_read_all(client->istream, client->read_buf, 1,
                                     &n, NULL, err)) {
            return false;
        }
        if (n == 0) {
            return false; /* EOF */
        }
        client->read_len = n;
    }
    return process_client_message(client, client->read_buf[0], err);
}

VncServer *vnc_server_new(const char *address, uint16_t port,
                          const struct VncServerCallbacks *callbacks)
{
    VncServer *srv = g_new0(VncServer, 1);
    GSocketAddress *addr;
    GInetAddress *iaddr;
    GError *err = NULL;

    srv->callbacks = *callbacks;
    srv->address = g_strdup(address);

    iaddr = g_inet_address_new_from_string(address);
    if (!iaddr) {
        g_free(srv->address);
        g_free(srv);
        return NULL;
    }
    addr = g_inet_socket_address_new(iaddr, port);
    g_object_unref(iaddr);

    srv->listener = g_socket_listener_new();
    if (!g_socket_listener_add_address(srv->listener, addr, G_SOCKET_TYPE_STREAM,
                                       G_SOCKET_PROTOCOL_TCP, NULL, NULL, &err)) {
        g_object_unref(addr);
        g_object_unref(srv->listener);
        g_free(srv->address);
        g_free(srv);
        return NULL;
    }
    g_object_unref(addr);
    return srv;
}

void vnc_server_free(VncServer *srv)
{
    if (!srv) {
        return;
    }
    g_object_unref(srv->listener);
    g_free(srv->address);
    g_free(srv);
}

static bool do_handshake(VncClient *client, guint width, guint height,
                         const char *name, GError **err)
{
    char version[13];
    uint8_t n_sec;
    uint8_t sec_type;
    uint8_t sec_result;

    /* Version */
    if (!read_all(client->istream, version, 12, err)) {
        return false;
    }
    version[12] = '\0';
    if (!write_all(client->ostream, RFB_VERSION, 12, err)) {
        return false;
    }

    /* Security */
    if (!read_all(client->istream, &n_sec, 1, err)) {
        return false;
    }
    if (n_sec == 0) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Server sent no security types");
        return false;
    }
    if (!read_all(client->istream, &sec_type, 1, err)) {
        return false;
    }
    if (n_sec > 1 && !read_all(client->istream, version, (size_t)n_sec - 1, err)) {
        return false;
    }
    sec_result = RFB_SECURITY_RESULT_OK;
    if (!write_all(client->ostream, &sec_result, 1, err)) {
        return false;
    }

    /* ClientInit (shared flag), 1 byte - just read and ignore */
    if (!read_all(client->istream, &sec_result, 1, err)) {
        return false;
    }

    if (!send_server_init(client, width, height, name, err)) {
        return false;
    }
    client->handshake_done = true;
    return true;
}

VncClient *vnc_server_accept(VncServer *srv, GError **err)
{
    GSocket *socket = NULL;
    GSocketConnection *conn;
    VncClient *client;

    conn = g_socket_listener_accept(srv->listener, NULL, NULL, err);
    if (!conn) {
        return NULL;
    }
    socket = g_socket_connection_get_socket(conn);

    client = g_new0(VncClient, 1);
    client->conn = conn;
    client->istream = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    client->ostream = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    client->channel = g_io_channel_unix_new(g_socket_get_fd(socket));
    g_io_channel_set_encoding(client->channel, NULL, NULL);
    g_io_channel_set_buffered(client->channel, FALSE);
    client->server = srv;

    /* Default size; will be updated by first Scanout */
    if (!do_handshake(client, 640, 480, "QEMU (D-Bus VNC bridge)", err)) {
        vnc_client_free(client);
        return NULL;
    }
    return client;
}

bool vnc_client_send_framebuffer(VncClient *client,
    int x, int y, int w, int h,
    const uint8_t *data, int stride,
    GError **err)
{
    uint8_t hdr[12];
    int row;
    uint32_t enc_be;

    if (!client->handshake_done) {
        return true;
    }
    /* Message type 0, padding 0 */
    hdr[0] = RFB_SERVER_MSG_FRAMEBUFFER_UPDATE;
    hdr[1] = 0;
    /* number of rectangles = 1 */
    *(uint16_t *)(hdr + 2) = be16(1);
    *(uint16_t *)(hdr + 4) = be16((uint16_t)x);
    *(uint16_t *)(hdr + 6) = be16((uint16_t)y);
    *(uint16_t *)(hdr + 8) = be16((uint16_t)w);
    *(uint16_t *)(hdr + 10) = be16((uint16_t)h);
    enc_be = be32(RFB_ENCODING_RAW);
    if (!write_all(client->ostream, hdr, 12, err) ||
        !write_all(client->ostream, &enc_be, 4, err)) {
        return false;
    }
    /* Raw pixels: 32bpp = 4 bytes per pixel, row-major */
    for (row = 0; row < h; row++) {
        if (!write_all(client->ostream, data + (size_t)row * (size_t)stride,
                       (size_t)w * 4, err)) {
            return false;
        }
    }
    return true;
}

void vnc_client_send_desktop_size(VncClient *client, int w, int h, GError **err)
{
    /* We don't send DesktopSize pseudo-encoding in this minimal server;
     * the client already got the size in ServerInit. Resize would require
     * ExtendedDesktopSize. Skip for simplicity.
     */
    (void)client;
    (void)w;
    (void)h;
    (void)err;
}

void vnc_client_close(VncClient *client)
{
    if (client && client->conn) {
        g_io_stream_close(G_IO_STREAM(client->conn), NULL, NULL);
    }
}

void vnc_client_free(VncClient *client)
{
    if (!client) {
        return;
    }
    if (client->channel) {
        g_io_channel_unref(client->channel);
    }
    if (client->conn) {
        g_object_unref(client->conn);
    }
    g_free(client);
}

GIOChannel *vnc_client_get_channel(VncClient *client)
{
    return client ? client->channel : NULL;
}
