/*
 * QEMU DBus display
 *
 * Copyright (c) 2021 Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/dbus.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "trace.h"

#include "dbus.h"

#define MIME_TEXT_PLAIN_UTF8 "text/plain;charset=utf-8"

static void
dbus_clipboard_complete_request(
    DBusDisplay *dpy,
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
        dpy->clipboard, invocation,
        MIME_TEXT_PLAIN_UTF8, v_data);
}

static void
dbus_clipboard_update_info(DBusDisplay *dpy, QemuClipboardInfo *info)
{
    bool self_update = info->owner == &dpy->clipboard_peer;
    const char *mime[QEMU_CLIPBOARD_TYPE__COUNT + 1] = { 0, };
    DBusClipboardRequest *req;
    int i = 0;

    if (info->owner == NULL) {
        if (dpy->clipboard_proxy) {
            qemu_dbus_display1_clipboard_call_release(
                dpy->clipboard_proxy,
                info->selection,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
        return;
    }

    if (self_update || !info->has_serial) {
        return;
    }

    req = &dpy->clipboard_request[info->selection];
    if (req->invocation && info->types[req->type].data) {
        dbus_clipboard_complete_request(dpy, req->invocation, info, req->type);
        g_clear_object(&req->invocation);
        g_source_remove(req->timeout_id);
        req->timeout_id = 0;
        return;
    }

    if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
        mime[i++] = MIME_TEXT_PLAIN_UTF8;
    }

    if (i > 0) {
        if (dpy->clipboard_proxy) {
            qemu_dbus_display1_clipboard_call_grab(
                dpy->clipboard_proxy,
                info->selection,
                info->serial,
                mime,
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    }
}

static void
dbus_clipboard_reset_serial(DBusDisplay *dpy)
{
    if (dpy->clipboard_proxy) {
        qemu_dbus_display1_clipboard_call_register(
            dpy->clipboard_proxy,
            G_DBUS_CALL_FLAGS_NONE,
            -1, NULL, NULL, NULL);
    }
}

static void
dbus_clipboard_notify(Notifier *notifier, void *data)
{
    DBusDisplay *dpy =
        container_of(notifier, DBusDisplay, clipboard_peer.notifier);
    QemuClipboardNotify *notify = data;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        dbus_clipboard_update_info(dpy, notify->info);
        return;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        dbus_clipboard_reset_serial(dpy);
        return;
    }
}

static void
dbus_clipboard_qemu_request(QemuClipboardInfo *info,
                            QemuClipboardType type)
{
    DBusDisplay *dpy = container_of(info->owner, DBusDisplay, clipboard_peer);
    g_autofree char *mime = NULL;
    g_autoptr(GVariant) v_data = NULL;
    g_autoptr(GError) err = NULL;
    const char *data = NULL;
    const char *mimes[] = { MIME_TEXT_PLAIN_UTF8, NULL };
    size_t n;

    if (type != QEMU_CLIPBOARD_TYPE_TEXT) {
        /* unsupported atm */
        return;
    }

    if (dpy->clipboard_proxy) {
        if (!qemu_dbus_display1_clipboard_call_request_sync(
                dpy->clipboard_proxy,
                info->selection,
                mimes,
                G_DBUS_CALL_FLAGS_NONE, -1, &mime, &v_data, NULL, &err)) {
            error_report("Failed to request clipboard: %s", err->message);
            return;
        }

        if (g_strcmp0(mime, MIME_TEXT_PLAIN_UTF8)) {
            error_report("Unsupported returned MIME: %s", mime);
            return;
        }

        data = g_variant_get_fixed_array(v_data, &n, 1);
        qemu_clipboard_set_data(&dpy->clipboard_peer, info, type,
                                n, data, true);
    }
}

static void
dbus_clipboard_request_cancelled(DBusClipboardRequest *req)
{
    if (!req->invocation) {
        return;
    }

    g_dbus_method_invocation_return_error(
        req->invocation,
        DBUS_DISPLAY_ERROR,
        DBUS_DISPLAY_ERROR_FAILED,
        "Cancelled clipboard request");

    g_clear_object(&req->invocation);
    g_source_remove(req->timeout_id);
    req->timeout_id = 0;
}

static void
dbus_clipboard_unregister_proxy(DBusDisplay *dpy)
{
    const char *name = NULL;
    int i;

    for (i = 0; i < G_N_ELEMENTS(dpy->clipboard_request); ++i) {
        dbus_clipboard_request_cancelled(&dpy->clipboard_request[i]);
    }

    if (!dpy->clipboard_proxy) {
        return;
    }

    name = g_dbus_proxy_get_name(G_DBUS_PROXY(dpy->clipboard_proxy));
    trace_dbus_clipboard_unregister(name);
    g_clear_object(&dpy->clipboard_proxy);
}

static void
dbus_on_clipboard_proxy_name_owner_changed(
    DBusDisplay *dpy,
    GObject *object,
    GParamSpec *pspec)
{
    dbus_clipboard_unregister_proxy(dpy);
}

static gboolean
dbus_clipboard_register(
    DBusDisplay *dpy,
    GDBusMethodInvocation *invocation)
{
    g_autoptr(GError) err = NULL;
    const char *name = NULL;

    if (dpy->clipboard_proxy) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Clipboard peer already registered!");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    dpy->clipboard_proxy =
        qemu_dbus_display1_clipboard_proxy_new_sync(
            g_dbus_method_invocation_get_connection(invocation),
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            g_dbus_method_invocation_get_sender(invocation),
            "/org/qemu/Display1/Clipboard",
            NULL,
            &err);
    if (!dpy->clipboard_proxy) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Failed to setup proxy: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    name = g_dbus_proxy_get_name(G_DBUS_PROXY(dpy->clipboard_proxy));
    trace_dbus_clipboard_register(name);

    g_object_connect(dpy->clipboard_proxy,
                     "swapped-signal::notify::g-name-owner",
                     dbus_on_clipboard_proxy_name_owner_changed, dpy,
                     NULL);
    qemu_clipboard_reset_serial();

    qemu_dbus_display1_clipboard_complete_register(dpy->clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_clipboard_check_caller(DBusDisplay *dpy, GDBusMethodInvocation *invocation)
{
    if (!dpy->clipboard_proxy ||
        g_strcmp0(g_dbus_proxy_get_name(G_DBUS_PROXY(dpy->clipboard_proxy)),
                  g_dbus_method_invocation_get_sender(invocation))) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Unregistered caller");
        return FALSE;
    }

    return TRUE;
}

static gboolean
dbus_clipboard_unregister(
    DBusDisplay *dpy,
    GDBusMethodInvocation *invocation)
{
    if (!dbus_clipboard_check_caller(dpy, invocation)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    dbus_clipboard_unregister_proxy(dpy);

    qemu_dbus_display1_clipboard_complete_unregister(
        dpy->clipboard, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_clipboard_grab(
    DBusDisplay *dpy,
    GDBusMethodInvocation *invocation,
    gint arg_selection,
    guint arg_serial,
    const gchar *const *arg_mimes)
{
    QemuClipboardSelection s = arg_selection;
    g_autoptr(QemuClipboardInfo) info = NULL;

    if (!dbus_clipboard_check_caller(dpy, invocation)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (s >= QEMU_CLIPBOARD_SELECTION__COUNT) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Invalid clipboard selection: %d", arg_selection);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    info = qemu_clipboard_info_new(&dpy->clipboard_peer, s);
    if (g_strv_contains(arg_mimes, MIME_TEXT_PLAIN_UTF8)) {
        info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
    }
    info->serial = arg_serial;
    info->has_serial = true;
    if (qemu_clipboard_check_serial(info, true)) {
        qemu_clipboard_update(info);
    } else {
        trace_dbus_clipboard_grab_failed();
    }

    qemu_dbus_display1_clipboard_complete_grab(dpy->clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_clipboard_release(
    DBusDisplay *dpy,
    GDBusMethodInvocation *invocation,
    gint arg_selection)
{
    if (!dbus_clipboard_check_caller(dpy, invocation)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    qemu_clipboard_peer_release(&dpy->clipboard_peer, arg_selection);

    qemu_dbus_display1_clipboard_complete_release(dpy->clipboard, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_clipboard_request_timeout(gpointer user_data)
{
    dbus_clipboard_request_cancelled(user_data);
    return G_SOURCE_REMOVE;
}

static gboolean
dbus_clipboard_request(
    DBusDisplay *dpy,
    GDBusMethodInvocation *invocation,
    gint arg_selection,
    const gchar *const *arg_mimes)
{
    QemuClipboardSelection s = arg_selection;
    QemuClipboardType type = QEMU_CLIPBOARD_TYPE_TEXT;
    QemuClipboardInfo *info = NULL;

    if (!dbus_clipboard_check_caller(dpy, invocation)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (s >= QEMU_CLIPBOARD_SELECTION__COUNT) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Invalid clipboard selection: %d", arg_selection);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (dpy->clipboard_request[s].invocation) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Pending request");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    info = qemu_clipboard_info(s);
    if (!info || !info->owner || info->owner == &dpy->clipboard_peer) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Empty clipboard");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (!g_strv_contains(arg_mimes, MIME_TEXT_PLAIN_UTF8) ||
        !info->types[type].available) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Unhandled MIME types requested");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    if (info->types[type].data) {
        dbus_clipboard_complete_request(dpy, invocation, info, type);
    } else {
        qemu_clipboard_request(info, type);

        dpy->clipboard_request[s].invocation = g_object_ref(invocation);
        dpy->clipboard_request[s].type = type;
        dpy->clipboard_request[s].timeout_id =
            g_timeout_add_seconds(5, dbus_clipboard_request_timeout,
                                  &dpy->clipboard_request[s]);
    }

    return DBUS_METHOD_INVOCATION_HANDLED;
}

void
dbus_clipboard_init(DBusDisplay *dpy)
{
    g_autoptr(GDBusObjectSkeleton) clipboard = NULL;

    assert(!dpy->clipboard);

    clipboard = g_dbus_object_skeleton_new(DBUS_DISPLAY1_ROOT "/Clipboard");
    dpy->clipboard = qemu_dbus_display1_clipboard_skeleton_new();
    g_object_connect(dpy->clipboard,
                     "swapped-signal::handle-register",
                     dbus_clipboard_register, dpy,
                     "swapped-signal::handle-unregister",
                     dbus_clipboard_unregister, dpy,
                     "swapped-signal::handle-grab",
                     dbus_clipboard_grab, dpy,
                     "swapped-signal::handle-release",
                     dbus_clipboard_release, dpy,
                     "swapped-signal::handle-request",
                     dbus_clipboard_request, dpy,
                     NULL);

    g_dbus_object_skeleton_add_interface(
        G_DBUS_OBJECT_SKELETON(clipboard),
        G_DBUS_INTERFACE_SKELETON(dpy->clipboard));
    g_dbus_object_manager_server_export(dpy->server, clipboard);
    dpy->clipboard_peer.name = "dbus";
    dpy->clipboard_peer.notifier.notify = dbus_clipboard_notify;
    dpy->clipboard_peer.request = dbus_clipboard_qemu_request;
    qemu_clipboard_peer_register(&dpy->clipboard_peer);
}
