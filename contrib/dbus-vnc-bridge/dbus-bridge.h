/*
 * D-Bus bridge state for dbus-vnc-bridge
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DBUS_BRIDGE_H
#define DBUS_BRIDGE_H

#include <glib.h>
#include "vnc-server.h"

typedef struct DbusBridge DbusBridge;

/* Callbacks: run in D-Bus (main) context */
typedef void (*DbusBridgeFramebufferReady)(DbusBridge *bridge, gpointer user_data);

DbusBridge *dbus_bridge_new(const char *dbus_address, GError **err);
void dbus_bridge_free(DbusBridge *bridge);

/* Register as listener for console 0; uses socket pair and exports Listener on P2P. */
gboolean dbus_bridge_register_listener(DbusBridge *bridge,
    DbusBridgeFramebufferReady framebuffer_ready_cb,
    gpointer user_data,
    GError **err);
void dbus_bridge_unregister_listener(DbusBridge *bridge);

/* Input: forward to QEMU via D-Bus */
gboolean dbus_bridge_key_event(DbusBridge *bridge, uint32_t keycode, gboolean down, GError **err);
gboolean dbus_bridge_pointer_event(DbusBridge *bridge, int x, int y, uint8_t button_mask, GError **err);

/* Framebuffer: filled by Listener callbacks; read by VNC send. */
gboolean dbus_bridge_get_framebuffer(DbusBridge *bridge,
    guint *width, guint *height, guint *stride,
    const uint8_t **data);

#endif /* DBUS_BRIDGE_H */
