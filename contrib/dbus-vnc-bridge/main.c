/*
 * QEMU D-Bus VNC bridge - standalone VNC server coupled to QEMU via D-Bus
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Usage:
 *   Start QEMU with: -display dbus
 *   Run this bridge: qemu-dbus-vnc-bridge [--address 127.0.0.1] [--port 5900] [--dbus-address ADDR]
 *   Connect a VNC viewer to the bridge's address:port.
 */
#include "dbus-bridge.h"
#include "vnc-server.h"
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DbusBridge *g_bridge;
static VncClient *g_vnc_client;
static GMainLoop *g_loop;

static void on_framebuffer_ready(DbusBridge *bridge, gpointer user_data)
{
    (void)user_data;
    if (!g_vnc_client) {
        return;
    }
    guint width, height, stride;
    const uint8_t *data;
    GError *err = NULL;
    if (!dbus_bridge_get_framebuffer(bridge, &width, &height, &stride, &data)) {
        return;
    }
    vnc_client_send_framebuffer(g_vnc_client, 0, 0, (int)width, (int)height, data, (int)stride, &err);
    if (err) {
        /* Don't log broken pipe / connection reset - client may have disconnected */
        if (err->domain != G_IO_ERROR || (err->code != G_IO_ERROR_BROKEN_PIPE && err->code != G_IO_ERROR_CONNECTION_CLOSED)) {
            g_printerr("VNC send error: %s\n", err->message);
        }
        g_error_free(err);
    } else if (g_getenv("QEMU_DBUS_VNC_BRIDGE_DEBUG")) {
        g_print("VNC: sent framebuffer %u×%u (%u bytes)\n", width, height, width * height * 4);
    }
}

static void on_vnc_framebuffer_update_request(VncClient *client, int x, int y, int w, int h,
    gboolean incremental, gpointer user_data)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)incremental;
    on_framebuffer_ready(g_bridge, user_data);
}

static void on_vnc_key_event(VncClient *client, guint32 key, gboolean down, gpointer user_data)
{
    GError *err = NULL;
    if (!dbus_bridge_key_event(g_bridge, key, down, &err)) {
        g_printerr("D-Bus key event error: %s\n", err ? err->message : "unknown");
        if (err) {
            g_error_free(err);
        }
    }
}

static void on_vnc_pointer_event(VncClient *client, int x, int y, guint8 button_mask, gpointer user_data)
{
    GError *err = NULL;
    if (!dbus_bridge_pointer_event(g_bridge, x, y, button_mask, &err)) {
        g_printerr("D-Bus pointer error: %s\n", err ? err->message : "unknown");
        if (err) {
            g_error_free(err);
        }
    }
}

static void on_vnc_client_closed(VncClient *client, gpointer user_data)
{
    g_main_loop_quit(g_loop);
}

static guint g_vnc_watch_id;  /* 0 after source removed in callback */

static gboolean vnc_channel_io(GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
    VncClient *client = user_data;
    GError *err = NULL;
    if (cond & G_IO_IN) {
        if (!vnc_client_handle_read(client, &err)) {
            if (err) {
                g_printerr("VNC read error: %s\n", err->message);
                g_error_free(err);
            }
            on_vnc_client_closed(client, NULL);
            g_vnc_watch_id = 0;
            return G_SOURCE_REMOVE;
        }
    }
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        on_vnc_client_closed(client, NULL);
        g_vnc_watch_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void run_client_loop(DbusBridge *bridge, VncServer *vnc_server)
{
    GError *err = NULL;
    GIOChannel *channel;
    guint id;

    g_vnc_client = vnc_server_accept(vnc_server, &err);
    if (!g_vnc_client) {
        g_printerr("VNC accept failed: %s\n", err ? err->message : "unknown");
        if (err) {
            g_error_free(err);
        }
        return;
    }

    if (!dbus_bridge_register_listener(bridge, on_framebuffer_ready, NULL, &err)) {
        g_printerr("D-Bus register listener failed: %s\n", err->message);
        g_error_free(err);
        vnc_client_free(g_vnc_client);
        g_vnc_client = NULL;
        return;
    }

    channel = vnc_client_get_channel(g_vnc_client);
    g_io_channel_set_encoding(channel, NULL, NULL);
    g_io_channel_set_buffered(channel, FALSE);
    id = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, vnc_channel_io, g_vnc_client);
    g_vnc_watch_id = id;

    g_main_loop_run(g_loop);

    /* Only remove if not already removed by callback (client disconnect). */
    if (g_vnc_watch_id != 0) {
        g_source_remove(g_vnc_watch_id);
        g_vnc_watch_id = 0;
    }
    dbus_bridge_unregister_listener(bridge);
    vnc_client_close(g_vnc_client);
    vnc_client_free(g_vnc_client);
    g_vnc_client = NULL;
}

static void usage(const char *prog)
{
    g_printerr("Usage: %s [OPTIONS]\n"
               "  --address ADDR    Bind address (default: 127.0.0.1)\n"
               "  --port PORT        Bind port (default: 5900)\n"
               "  --dbus-address ADDR  D-Bus address (default: session bus)\n"
               "\n"
               "Start QEMU with: -display dbus\n"
               "Then run this bridge and connect a VNC viewer.\n",
               prog);
}

int main(int argc, char **argv)
{
    const char *address = "127.0.0.1";
    const char *dbus_address = NULL;
    uint16_t port = 5900;
    DbusBridge *bridge = NULL;
    VncServer *vnc_server = NULL;
    struct VncServerCallbacks vnc_cb;
    GError *err = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--address") == 0 && i + 1 < argc) {
            address = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dbus-address") == 0 && i + 1 < argc) {
            dbus_address = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    bridge = dbus_bridge_new(dbus_address, &err);
    if (!bridge) {
        g_printerr("Failed to connect to D-Bus: %s\n", err->message);
        g_error_free(err);
        return 1;
    }
    g_bridge = bridge;

    vnc_cb.framebuffer_update_request = on_vnc_framebuffer_update_request;
    vnc_cb.key_event = on_vnc_key_event;
    vnc_cb.pointer_event = on_vnc_pointer_event;
    vnc_cb.client_closed = on_vnc_client_closed;
    vnc_cb.user_data = NULL;

    vnc_server = vnc_server_new(address, port, &vnc_cb);
    if (!vnc_server) {
        g_printerr("Failed to create VNC server\n");
        dbus_bridge_free(bridge);
        return 1;
    }

    g_loop = g_main_loop_new(NULL, FALSE);
    g_print("D-Bus VNC bridge listening on %s:%u\n", address, port);
    for (;;) {
        run_client_loop(bridge, vnc_server);
    }

    g_main_loop_unref(g_loop);
    vnc_server_free(vnc_server);
    dbus_bridge_free(bridge);
    return 0;
}
