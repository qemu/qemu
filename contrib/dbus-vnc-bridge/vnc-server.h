/*
 * Minimal RFB (VNC) server for D-Bus VNC bridge
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VNC_SERVER_H
#define VNC_SERVER_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct VncServer VncServer;
typedef struct VncClient VncClient;

typedef void (*VncFramebufferUpdateRequest)(VncClient *client,
    int x, int y, int w, int h, gboolean incremental, gpointer user_data);
typedef void (*VncKeyEvent)(VncClient *client, uint32_t key, gboolean down,
    gpointer user_data);
typedef void (*VncPointerEvent)(VncClient *client, int x, int y,
    uint8_t button_mask, gpointer user_data);
typedef void (*VncClientClosed)(VncClient *client, gpointer user_data);

struct VncServerCallbacks {
    VncFramebufferUpdateRequest framebuffer_update_request;
    VncKeyEvent key_event;
    VncPointerEvent pointer_event;
    VncClientClosed client_closed;
    gpointer user_data;
};

VncServer *vnc_server_new(const char *address, uint16_t port,
                          const struct VncServerCallbacks *callbacks);
void vnc_server_free(VncServer *srv);

/* Run once to accept one client (blocking). Returns client or NULL on error. */
VncClient *vnc_server_accept(VncServer *srv, GError **err);

/* Send raw framebuffer update. Format: 32bpp, 8-8-8 RGB, little-endian (X8R8G8B8). */
bool vnc_client_send_framebuffer(VncClient *client,
    int x, int y, int w, int h,
    const uint8_t *data, int stride,
    GError **err);

void vnc_client_send_desktop_size(VncClient *client, int w, int h, GError **err);
void vnc_client_close(VncClient *client);
void vnc_client_free(VncClient *client);

/* Return the underlying IO channel for the client (for main loop integration). */
GIOChannel *vnc_client_get_channel(VncClient *client);

/* Process incoming data from the client. Returns false on error or disconnect. */
bool vnc_client_handle_read(VncClient *client, GError **err);

#endif /* VNC_SERVER_H */
