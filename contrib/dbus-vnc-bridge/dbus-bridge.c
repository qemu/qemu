/*
 * D-Bus bridge for dbus-vnc-bridge
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dbus-bridge.h"
#include "dbus-display1.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PIXMAN_X8R8G8B8 0x20020888

struct DbusBridge {
    GDBusConnection *session_conn;
    GDBusConnection *listener_conn;  /* P2P connection for Listener */
    QemuDBusDisplay1VM *vm_proxy;
    QemuDBusDisplay1Console *console_proxy;
    QemuDBusDisplay1Keyboard *keyboard_proxy;
    QemuDBusDisplay1Mouse *mouse_proxy;
    QemuDBusDisplay1Listener *listener_skeleton;
    GDBusObjectManagerServer *object_manager;

    /* Framebuffer from Listener callbacks */
    guint fb_width;
    guint fb_height;
    guint fb_stride;
    uint8_t *fb_data;
    gsize fb_size;
    gboolean fb_dirty;

    DbusBridgeFramebufferReady framebuffer_ready_cb;
    gpointer framebuffer_ready_user_data;
};

static void ensure_framebuffer(DbusBridge *bridge, guint width, guint height, guint stride)
{
    gsize need = (gsize)stride * (gsize)height;
    if (bridge->fb_data && bridge->fb_size >= need && bridge->fb_width == width && bridge->fb_height == height) {
        return;
    }
    g_free(bridge->fb_data);
    bridge->fb_data = g_malloc(need);
    bridge->fb_size = need;
    bridge->fb_width = width;
    bridge->fb_height = height;
    bridge->fb_stride = stride;
}

static gboolean on_handle_scanout(QemuDBusDisplay1Listener *object,
    GDBusMethodInvocation *invocation,
    guint arg_width,
    guint arg_height,
    guint arg_stride,
    guint arg_pixman_format,
    GVariant *arg_data,
    gpointer user_data)
{
    DbusBridge *bridge = user_data;
    gconstpointer data;
    gsize len;

    if (arg_pixman_format != PIXMAN_X8R8G8B8) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
            "Unsupported pixman format %u", arg_pixman_format);
        return TRUE;
    }
    data = g_variant_get_data(arg_data);
    len = g_variant_get_size(arg_data);
    ensure_framebuffer(bridge, arg_width, arg_height, arg_stride);
    if (len >= bridge->fb_size) {
        memcpy(bridge->fb_data, data, bridge->fb_size);
    } else {
        memcpy(bridge->fb_data, data, len);
    }
    bridge->fb_dirty = TRUE;
    qemu_dbus_display1_listener_complete_scanout(object, invocation);
    if (bridge->framebuffer_ready_cb) {
        bridge->framebuffer_ready_cb(bridge, bridge->framebuffer_ready_user_data);
    }
    return TRUE;
}

static gboolean on_handle_update(QemuDBusDisplay1Listener *object,
    GDBusMethodInvocation *invocation,
    gint arg_x,
    gint arg_y,
    gint arg_width,
    gint arg_height,
    guint arg_stride,
    guint arg_pixman_format,
    GVariant *arg_data,
    gpointer user_data)
{
    DbusBridge *bridge = user_data;
    gconstpointer data;
    int row;

    if (arg_pixman_format != PIXMAN_X8R8G8B8 || !bridge->fb_data) {
        qemu_dbus_display1_listener_complete_update(object, invocation);
        return TRUE;
    }
    data = g_variant_get_data(arg_data);
    for (row = 0; row < arg_height; row++) {
        size_t offset = (size_t)(arg_y + row) * bridge->fb_stride + (size_t)arg_x * 4;
        size_t row_len = (size_t)arg_width * 4;
        if (offset + row_len <= bridge->fb_size) {
            memcpy(bridge->fb_data + offset, (const uint8_t *)data + (size_t)row * (size_t)arg_stride, row_len);
        }
    }
    bridge->fb_dirty = TRUE;
    qemu_dbus_display1_listener_complete_update(object, invocation);
    if (bridge->framebuffer_ready_cb) {
        bridge->framebuffer_ready_cb(bridge, bridge->framebuffer_ready_user_data);
    }
    return TRUE;
}

static gboolean on_handle_disable(QemuDBusDisplay1Listener *object,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    (void)user_data;
    qemu_dbus_display1_listener_complete_disable(object, invocation);
    return TRUE;
}

static gboolean on_handle_mouse_set(QemuDBusDisplay1Listener *object,
    GDBusMethodInvocation *invocation,
    gint arg_x,
    gint arg_y,
    gint arg_on,
    gpointer user_data)
{
    (void)arg_x;
    (void)arg_y;
    (void)arg_on;
    (void)user_data;
    qemu_dbus_display1_listener_complete_mouse_set(object, invocation);
    return TRUE;
}

static gboolean on_handle_cursor_define(QemuDBusDisplay1Listener *object,
    GDBusMethodInvocation *invocation,
    gint arg_width,
    gint arg_height,
    gint arg_hot_x,
    gint arg_hot_y,
    GVariant *arg_data,
    gpointer user_data)
{
    (void)arg_width;
    (void)arg_height;
    (void)arg_hot_x;
    (void)arg_hot_y;
    (void)arg_data;
    (void)user_data;
    qemu_dbus_display1_listener_complete_cursor_define(object, invocation);
    return TRUE;
}

DbusBridge *dbus_bridge_new(const char *dbus_address, GError **err)
{
    DbusBridge *bridge = g_new0(DbusBridge, 1);

    if (dbus_address && dbus_address[0]) {
        bridge->session_conn = g_dbus_connection_new_for_address_sync(
            dbus_address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
            NULL, NULL, err);
    } else {
        bridge->session_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, err);
    }
    if (!bridge->session_conn) {
        g_free(bridge);
        return NULL;
    }

    bridge->vm_proxy = qemu_dbus_display1_vm_proxy_new_sync(bridge->session_conn,
        G_DBUS_PROXY_FLAGS_NONE, "org.qemu", "/org/qemu/Display1/VM", NULL, err);
    if (!bridge->vm_proxy) {
        g_object_unref(bridge->session_conn);
        g_free(bridge);
        return NULL;
    }

    bridge->console_proxy = qemu_dbus_display1_console_proxy_new_sync(bridge->session_conn,
        G_DBUS_PROXY_FLAGS_NONE, "org.qemu", "/org/qemu/Display1/Console_0", NULL, err);
    if (!bridge->console_proxy) {
        g_object_unref(bridge->vm_proxy);
        g_object_unref(bridge->session_conn);
        g_free(bridge);
        return NULL;
    }

    bridge->keyboard_proxy = qemu_dbus_display1_keyboard_proxy_new_sync(bridge->session_conn,
        G_DBUS_PROXY_FLAGS_NONE, "org.qemu", "/org/qemu/Display1/Console_0", NULL, err);
    if (!bridge->keyboard_proxy) {
        g_object_unref(bridge->console_proxy);
        g_object_unref(bridge->vm_proxy);
        g_object_unref(bridge->session_conn);
        g_free(bridge);
        return NULL;
    }

    bridge->mouse_proxy = qemu_dbus_display1_mouse_proxy_new_sync(bridge->session_conn,
        G_DBUS_PROXY_FLAGS_NONE, "org.qemu", "/org/qemu/Display1/Console_0", NULL, err);
    if (!bridge->mouse_proxy) {
        g_object_unref(bridge->keyboard_proxy);
        g_object_unref(bridge->console_proxy);
        g_object_unref(bridge->vm_proxy);
        g_object_unref(bridge->session_conn);
        g_free(bridge);
        return NULL;
    }

    return bridge;
}

void dbus_bridge_free(DbusBridge *bridge)
{
    if (!bridge) {
        return;
    }
    dbus_bridge_unregister_listener(bridge);
    g_free(bridge->fb_data);
    if (bridge->mouse_proxy) {
        g_object_unref(bridge->mouse_proxy);
    }
    if (bridge->keyboard_proxy) {
        g_object_unref(bridge->keyboard_proxy);
    }
    if (bridge->console_proxy) {
        g_object_unref(bridge->console_proxy);
    }
    if (bridge->vm_proxy) {
        g_object_unref(bridge->vm_proxy);
    }
    if (bridge->session_conn) {
        g_object_unref(bridge->session_conn);
    }
    g_free(bridge);
}

gboolean dbus_bridge_register_listener(DbusBridge *bridge,
    DbusBridgeFramebufferReady framebuffer_ready_cb,
    gpointer user_data,
    GError **err)
{
    GSocket *socket = NULL;
    GSocketConnection *socket_conn;
    GUnixFDList *fd_list;
    gint fd_index;
    GDBusConnectionFlags flags;
    GSocket *client_side = NULL;

    bridge->framebuffer_ready_cb = framebuffer_ready_cb;
    bridge->framebuffer_ready_user_data = user_data;

    {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED, "socketpair failed");
            return FALSE;
        }
        socket = g_socket_new_from_fd(fds[0], err);
        if (!socket) {
            close(fds[1]);
            return FALSE;
        }
        client_side = g_socket_new_from_fd(fds[1], err);
        if (!client_side) {
            g_object_unref(socket);
            return FALSE;
        }
    }

    fd_list = g_unix_fd_list_new();
    fd_index = g_unix_fd_list_append(fd_list, g_socket_get_fd(socket), err);
    if (fd_index < 0) {
        g_object_unref(fd_list);
        g_object_unref(socket);
        g_object_unref(client_side);
        return FALSE;
    }

    if (!qemu_dbus_display1_console_call_register_listener_sync(bridge->console_proxy,
            g_variant_new_handle(fd_index), G_DBUS_CALL_FLAGS_NONE, -1, fd_list, NULL, NULL, err)) {
        g_object_unref(fd_list);
        g_object_unref(socket);
        g_object_unref(client_side);
        return FALSE;
    }
    g_object_unref(fd_list);
    g_object_unref(socket);

    socket_conn = g_socket_connection_factory_create_connection(client_side);
    g_object_unref(client_side);
    if (!socket_conn) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED, "socket connection failed");
        return FALSE;
    }

    /* We are the client end of the P2P connection (QEMU has the server end). */
    flags = G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT;
    bridge->listener_conn = g_dbus_connection_new_sync(G_IO_STREAM(socket_conn),
        NULL, flags, NULL, NULL, err);
    g_object_unref(socket_conn);
    if (!bridge->listener_conn) {
        return FALSE;
    }

    bridge->listener_skeleton = qemu_dbus_display1_listener_skeleton_new();
    g_signal_connect(bridge->listener_skeleton, "handle-scanout",
        G_CALLBACK(on_handle_scanout), bridge);
    g_signal_connect(bridge->listener_skeleton, "handle-update",
        G_CALLBACK(on_handle_update), bridge);
    g_signal_connect(bridge->listener_skeleton, "handle-disable",
        G_CALLBACK(on_handle_disable), bridge);
    g_signal_connect(bridge->listener_skeleton, "handle-mouse-set",
        G_CALLBACK(on_handle_mouse_set), bridge);
    g_signal_connect(bridge->listener_skeleton, "handle-cursor-define",
        G_CALLBACK(on_handle_cursor_define), bridge);

    bridge->object_manager = g_dbus_object_manager_server_new("/org/qemu/Display1");
    {
        GDBusObjectSkeleton *obj = g_dbus_object_skeleton_new("/org/qemu/Display1/Listener");
        g_dbus_object_skeleton_add_interface(obj, G_DBUS_INTERFACE_SKELETON(bridge->listener_skeleton));
        g_dbus_object_manager_server_export(bridge->object_manager, obj);
    }
    g_dbus_object_manager_server_set_connection(bridge->object_manager, bridge->listener_conn);
    return TRUE;
}

void dbus_bridge_unregister_listener(DbusBridge *bridge)
{
    if (bridge->object_manager) {
        g_dbus_object_manager_server_unexport(bridge->object_manager, "/org/qemu/Display1/Listener");
        g_object_unref(bridge->object_manager);
        bridge->object_manager = NULL;
    }
    if (bridge->listener_skeleton) {
        g_object_unref(bridge->listener_skeleton);
        bridge->listener_skeleton = NULL;
    }
    if (bridge->listener_conn) {
        g_object_unref(bridge->listener_conn);
        bridge->listener_conn = NULL;
    }
}

gboolean dbus_bridge_key_event(DbusBridge *bridge, uint32_t keycode, gboolean down, GError **err)
{
    if (down) {
        return qemu_dbus_display1_keyboard_call_press_sync(bridge->keyboard_proxy, keycode,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, err);
    } else {
        return qemu_dbus_display1_keyboard_call_release_sync(bridge->keyboard_proxy, keycode,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, err);
    }
}

gboolean dbus_bridge_pointer_event(DbusBridge *bridge, int x, int y, uint8_t button_mask, GError **err)
{
    static uint8_t prev_mask = 0;
    uint8_t changed;
    int i;

    if (!qemu_dbus_display1_mouse_call_set_abs_position_sync(bridge->mouse_proxy, (guint)x, (guint)y,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, err)) {
        return FALSE;
    }
    changed = button_mask ^ prev_mask;
    for (i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1 << i);
        if (changed & bit) {
            if (button_mask & bit) {
                qemu_dbus_display1_mouse_call_press_sync(bridge->mouse_proxy, (guint)i,
                        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
            } else {
                qemu_dbus_display1_mouse_call_release_sync(bridge->mouse_proxy, (guint)i,
                        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
            }
        }
    }
    prev_mask = button_mask;
    return TRUE;
}

gboolean dbus_bridge_get_framebuffer(DbusBridge *bridge,
    guint *width, guint *height, guint *stride,
    const uint8_t **data)
{
    if (!bridge->fb_data) {
        return FALSE;
    }
    *width = bridge->fb_width;
    *height = bridge->fb_height;
    *stride = bridge->fb_stride;
    *data = bridge->fb_data;
    return TRUE;
}
