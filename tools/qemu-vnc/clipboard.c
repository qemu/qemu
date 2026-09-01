/*
 * Standalone VNC server connecting to QEMU via D-Bus display interface.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/error-report.h"
#include "ui/clipboard.h"
#include "ui/dbus-display1.h"
#include "trace.h"
#include "qemu-vnc.h"

#define MIME_TEXT_PLAIN_UTF8 "text/plain;charset=utf-8"

typedef struct {
    GDBusMethodInvocation *invocation;
    QemuClipboardType type;
    guint timeout_id;
} VncDBusClipboardRequest;

static QemuDBusDisplay1Clipboard *clipboard_proxy;
static QemuDBusDisplay1Clipboard *clipboard_skel;
static QemuClipboardPeer clipboard_peer;
static uint32_t clipboard_serial;
static VncDBusClipboardRequest
    clipboard_request[QEMU_CLIPBOARD_SELECTION__COUNT];

static void
vnc_dbus_clipboard_complete_request(
    GDBusMethodInvocation *invocation,
    QemuClipboardInfo *info,
    QemuClipboardType type)
{
    GVariant *v_data = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"),
        info->types[type].data,
        info->types[type].size,
        TRUE,
        (GDestroyNotify)qemu_clipboard_info_unref,
        qemu_clipboard_info_ref(info));

    qemu_dbus_display1_clipboard_complete_request(
        clipboard_skel, invocation,
        MIME_TEXT_PLAIN_UTF8, v_data);
}

static void
vnc_dbus_clipboard_request_cancelled(VncDBusClipboardRequest *req)
{
    if (!req->invocation) {
        return;
    }

    g_dbus_method_invocation_return_error(
        req->invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_FAILED,
        "Cancelled clipboard request");

    g_clear_object(&req->invocation);
    g_clear_handle_id(&req->timeout_id, g_source_remove);
}

static gboolean
vnc_dbus_clipboard_request_timeout(gpointer user_data)
{
    vnc_dbus_clipboard_request_cancelled(user_data);
    return G_SOURCE_REMOVE;
}

static void
vnc_dbus_clipboard_request(QemuClipboardInfo *info,
                           QemuClipboardType type)
{
    g_autofree char *mime = NULL;
    g_autoptr(GVariant) v_data = NULL;
    g_autoptr(GError) err = NULL;
    const char *data = NULL;
    const char *mimes[] = { MIME_TEXT_PLAIN_UTF8, NULL };
    size_t n;

    if (type != QEMU_CLIPBOARD_TYPE_TEXT) {
        return;
    }

    if (!clipboard_proxy) {
        return;
    }

    if (!qemu_dbus_display1_clipboard_call_request_sync(
            clipboard_proxy,
            info->selection,
            mimes,
            G_DBUS_CALL_FLAGS_NONE, -1, &mime, &v_data, NULL, &err)) {
        error_report("Failed to request clipboard: %s", err->message);
        return;
    }

    if (!g_str_equal(mime, MIME_TEXT_PLAIN_UTF8)) {
        error_report("Unsupported returned MIME: %s", mime);
        return;
    }

    data = g_variant_get_fixed_array(v_data, &n, 1);
    qemu_clipboard_set_data(&clipboard_peer, info, type,
                            n, data, true);
}

static void
vnc_dbus_clipboard_update_info(QemuClipboardInfo *info)
{
    bool self_update = info->owner == &clipboard_peer;
    const char *mime[QEMU_CLIPBOARD_TYPE__COUNT + 1] = { 0, };
    VncDBusClipboardRequest *req;
    int i = 0;

    if (info->owner == NULL) {
        if (clipboard_proxy) {
            qemu_dbus_display1_clipboard_call_release(
                clipboard_proxy,
                info->selection,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
        return;
    }

    if (self_update) {
        return;
    }

    req = &clipboard_request[info->selection];
    if (req->invocation && info->types[req->type].data) {
        vnc_dbus_clipboard_complete_request(
            req->invocation, info, req->type);
        g_clear_object(&req->invocation);
        g_clear_handle_id(&req->timeout_id, g_source_remove);
        return;
    }

    if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
        mime[i++] = MIME_TEXT_PLAIN_UTF8;
    }

    if (i > 0 && clipboard_proxy) {
        uint32_t serial = info->has_serial ?
            info->serial : ++clipboard_serial;
        qemu_dbus_display1_clipboard_call_grab(
            clipboard_proxy,
            info->selection,
            serial,
            mime,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static void
vnc_dbus_clipboard_notify(Notifier *notifier, void *data)
{
    QemuClipboardNotify *notify = data;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        vnc_dbus_clipboard_update_info(notify->info);
        return;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        if (clipboard_proxy) {
            qemu_dbus_display1_clipboard_call_register(
                clipboard_proxy,
                G_DBUS_CALL_FLAGS_NONE,
                -1, NULL, NULL, NULL);
        }
        return;
    }
}

static gboolean
on_clipboard_register(QemuDBusDisplay1Clipboard *clipboard,
                      GDBusMethodInvocation *invocation,
                      gpointer user_data)
{
    clipboard_serial = 0;
    qemu_clipboard_reset_serial();

    qemu_dbus_display1_clipboard_complete_register(
        clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
on_clipboard_unregister(QemuDBusDisplay1Clipboard *clipboard,
                        GDBusMethodInvocation *invocation,
                        gpointer user_data)
{
    int i;

    for (i = 0; i < G_N_ELEMENTS(clipboard_request); ++i) {
        vnc_dbus_clipboard_request_cancelled(&clipboard_request[i]);
    }

    qemu_dbus_display1_clipboard_complete_unregister(
        clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
on_clipboard_grab(QemuDBusDisplay1Clipboard *clipboard,
                  GDBusMethodInvocation *invocation,
                  gint arg_selection,
                  guint arg_serial,
                  const gchar *const *arg_mimes,
                  gpointer user_data)
{
    QemuClipboardSelection s = arg_selection;
    g_autoptr(QemuClipboardInfo) info = NULL;

    if (s >= QEMU_CLIPBOARD_SELECTION__COUNT) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED,
            "Invalid clipboard selection: %d", arg_selection);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    trace_qemu_vnc_clipboard_grab(arg_selection, arg_serial);

    info = qemu_clipboard_info_new(&clipboard_peer, s);
    if (g_strv_contains(arg_mimes, MIME_TEXT_PLAIN_UTF8)) {
        info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
    }
    info->serial = arg_serial;
    info->has_serial = true;
    if (qemu_clipboard_check_serial(info, true)) {
        qemu_clipboard_update(info);
    }

    qemu_dbus_display1_clipboard_complete_grab(
        clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
on_clipboard_release(QemuDBusDisplay1Clipboard *clipboard,
                     GDBusMethodInvocation *invocation,
                     gint arg_selection,
                     gpointer user_data)
{
    trace_qemu_vnc_clipboard_release(arg_selection);

    qemu_clipboard_peer_release(&clipboard_peer, arg_selection);

    qemu_dbus_display1_clipboard_complete_release(
        clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
on_clipboard_request(QemuDBusDisplay1Clipboard *clipboard,
                     GDBusMethodInvocation *invocation,
                     gint arg_selection,
                     const gchar *const *arg_mimes,
                     gpointer user_data)
{
    QemuClipboardSelection s = arg_selection;
    QemuClipboardType type = QEMU_CLIPBOARD_TYPE_TEXT;
    QemuClipboardInfo *info = NULL;

    trace_qemu_vnc_clipboard_request(arg_selection);

    if (s >= QEMU_CLIPBOARD_SELECTION__COUNT) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED,
            "Invalid clipboard selection: %d", arg_selection);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (clipboard_request[s].invocation) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED,
            "Pending request");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    info = qemu_clipboard_info(s);
    if (!info || !info->owner || info->owner == &clipboard_peer) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED,
            "Empty clipboard");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (!g_strv_contains(arg_mimes, MIME_TEXT_PLAIN_UTF8) ||
        !info->types[type].available) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_FAILED,
            "Unhandled MIME types requested");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (info->types[type].data) {
        vnc_dbus_clipboard_complete_request(invocation, info, type);
    } else {
        qemu_clipboard_request(info, type);

        clipboard_request[s].invocation = g_object_ref(invocation);
        clipboard_request[s].type = type;
        clipboard_request[s].timeout_id =
            g_timeout_add_seconds(5,
                                  vnc_dbus_clipboard_request_timeout,
                                  &clipboard_request[s]);
    }

    return DBUS_METHOD_INVOCATION_HANDLED;
}

void clipboard_setup(GDBusObjectManager *manager, GDBusConnection *bus)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusInterface) iface = NULL;

    iface = g_dbus_object_manager_get_interface(
        manager, DBUS_DISPLAY1_ROOT "/Clipboard",
        "org.qemu.Display1.Clipboard");
    if (!iface) {
        return;
    }

    clipboard_proxy = g_object_ref(QEMU_DBUS_DISPLAY1_CLIPBOARD(iface));

    clipboard_skel = qemu_dbus_display1_clipboard_skeleton_new();
    g_object_connect(clipboard_skel,
                     "signal::handle-register",
                     on_clipboard_register, NULL,
                     "signal::handle-unregister",
                     on_clipboard_unregister, NULL,
                     "signal::handle-grab",
                     on_clipboard_grab, NULL,
                     "signal::handle-release",
                     on_clipboard_release, NULL,
                     "signal::handle-request",
                     on_clipboard_request, NULL,
                     NULL);

    if (!g_dbus_interface_skeleton_export(
            G_DBUS_INTERFACE_SKELETON(clipboard_skel),
            bus,
            DBUS_DISPLAY1_ROOT "/Clipboard",
            &err)) {
        error_report("Failed to export clipboard: %s", err->message);
        g_clear_object(&clipboard_skel);
        g_clear_object(&clipboard_proxy);
        return;
    }

    clipboard_peer.name = "dbus";
    clipboard_peer.notifier.notify = vnc_dbus_clipboard_notify;
    clipboard_peer.request = vnc_dbus_clipboard_request;
    qemu_clipboard_peer_register(&clipboard_peer);

    qemu_dbus_display1_clipboard_call_register(
        clipboard_proxy,
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, NULL, NULL);
}
