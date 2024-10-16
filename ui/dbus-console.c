/*
 * QEMU DBus display console
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ui/input.h"
#include "ui/kbd-state.h"
#include "trace.h"

#ifdef G_OS_UNIX
#include <gio/gunixfdlist.h>
#endif

#include "dbus.h"

static struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX];

struct _DBusDisplayConsole {
    GDBusObjectSkeleton parent_instance;
    DisplayChangeListener dcl;

    DBusDisplay *display;
    GPtrArray *listeners;
    QemuDBusDisplay1Console *iface;

    QemuDBusDisplay1Keyboard *iface_kbd;
    QKbdState *kbd;

    QemuDBusDisplay1Mouse *iface_mouse;
    QemuDBusDisplay1MultiTouch *iface_touch;
    gboolean last_set;
    guint last_x;
    guint last_y;
    Notifier mouse_mode_notifier;
};

G_DEFINE_TYPE(DBusDisplayConsole,
              dbus_display_console,
              G_TYPE_DBUS_OBJECT_SKELETON)

static void
dbus_display_console_set_size(DBusDisplayConsole *ddc,
                              uint32_t width, uint32_t height)
{
    g_object_set(ddc->iface,
                 "width", width,
                 "height", height,
                 NULL);
}

static void
dbus_gfx_switch(DisplayChangeListener *dcl,
                struct DisplaySurface *new_surface)
{
    DBusDisplayConsole *ddc = container_of(dcl, DBusDisplayConsole, dcl);

    dbus_display_console_set_size(ddc,
                                  surface_width(new_surface),
                                  surface_height(new_surface));
}

static void
dbus_gfx_update(DisplayChangeListener *dcl,
                int x, int y, int w, int h)
{
}

static void
dbus_gl_scanout_disable(DisplayChangeListener *dcl)
{
}

static void
dbus_gl_scanout_texture(DisplayChangeListener *dcl,
                        uint32_t tex_id,
                        bool backing_y_0_top,
                        uint32_t backing_width,
                        uint32_t backing_height,
                        uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h,
                        void *d3d_tex2d)
{
    DBusDisplayConsole *ddc = container_of(dcl, DBusDisplayConsole, dcl);

    dbus_display_console_set_size(ddc, w, h);
}

static void
dbus_gl_scanout_dmabuf(DisplayChangeListener *dcl,
                       QemuDmaBuf *dmabuf)
{
    uint32_t width, height;

    DBusDisplayConsole *ddc = container_of(dcl, DBusDisplayConsole, dcl);

    width = qemu_dmabuf_get_width(dmabuf);
    height = qemu_dmabuf_get_height(dmabuf);

    dbus_display_console_set_size(ddc, width, height);
}

static void
dbus_gl_scanout_update(DisplayChangeListener *dcl,
                       uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h)
{
}

const DisplayChangeListenerOps dbus_console_dcl_ops = {
    .dpy_name                = "dbus-console",
    .dpy_gfx_switch          = dbus_gfx_switch,
    .dpy_gfx_update          = dbus_gfx_update,
    .dpy_gl_scanout_disable  = dbus_gl_scanout_disable,
    .dpy_gl_scanout_texture  = dbus_gl_scanout_texture,
    .dpy_gl_scanout_dmabuf   = dbus_gl_scanout_dmabuf,
    .dpy_gl_update           = dbus_gl_scanout_update,
};

static void
dbus_display_console_init(DBusDisplayConsole *object)
{
    DBusDisplayConsole *ddc = DBUS_DISPLAY_CONSOLE(object);

    ddc->listeners = g_ptr_array_new_with_free_func(g_object_unref);
    ddc->dcl.ops = &dbus_console_dcl_ops;
}

static void
dbus_display_console_dispose(GObject *object)
{
    DBusDisplayConsole *ddc = DBUS_DISPLAY_CONSOLE(object);

    unregister_displaychangelistener(&ddc->dcl);
    g_clear_object(&ddc->iface_touch);
    g_clear_object(&ddc->iface_mouse);
    g_clear_object(&ddc->iface_kbd);
    g_clear_object(&ddc->iface);
    g_clear_pointer(&ddc->listeners, g_ptr_array_unref);
    g_clear_pointer(&ddc->kbd, qkbd_state_free);

    G_OBJECT_CLASS(dbus_display_console_parent_class)->dispose(object);
}

static void
dbus_display_console_class_init(DBusDisplayConsoleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = dbus_display_console_dispose;
}

static void
listener_vanished_cb(DBusDisplayListener *listener)
{
    DBusDisplayConsole *ddc = dbus_display_listener_get_console(listener);
    const char *name = dbus_display_listener_get_bus_name(listener);

    trace_dbus_listener_vanished(name);

    g_ptr_array_remove_fast(ddc->listeners, listener);
    qkbd_state_lift_all_keys(ddc->kbd);
}

static gboolean
dbus_console_set_ui_info(DBusDisplayConsole *ddc,
                         GDBusMethodInvocation *invocation,
                         guint16 arg_width_mm,
                         guint16 arg_height_mm,
                         gint arg_xoff,
                         gint arg_yoff,
                         guint arg_width,
                         guint arg_height)
{
    QemuUIInfo info = {
        .width_mm = arg_width_mm,
        .height_mm = arg_height_mm,
        .xoff = arg_xoff,
        .yoff = arg_yoff,
        .width = arg_width,
        .height = arg_height,
    };

    if (!dpy_ui_info_supported(ddc->dcl.con)) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_UNSUPPORTED,
                                              "SetUIInfo is not supported");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    dpy_set_ui_info(ddc->dcl.con, &info, false);
    qemu_dbus_display1_console_complete_set_uiinfo(ddc->iface, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

#ifdef G_OS_WIN32
bool
dbus_win32_import_socket(GDBusMethodInvocation *invocation,
                         GVariant *arg_listener, int *socket)
{
    gsize n;
    WSAPROTOCOL_INFOW *info = (void *)g_variant_get_fixed_array(arg_listener, &n, 1);

    if (!info || n != sizeof(*info)) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Failed to get socket infos");
        return false;
    }

    *socket = WSASocketW(FROM_PROTOCOL_INFO,
                         FROM_PROTOCOL_INFO,
                         FROM_PROTOCOL_INFO,
                         info, 0, 0);
    if (*socket == INVALID_SOCKET) {
        g_autofree gchar *emsg = g_win32_error_message(WSAGetLastError());
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Couldn't create socket: %s", emsg);
        return false;
    }

    return true;
}
#endif

static gboolean
dbus_console_register_listener(DBusDisplayConsole *ddc,
                               GDBusMethodInvocation *invocation,
#ifdef G_OS_UNIX
                               GUnixFDList *fd_list,
#endif
                               GVariant *arg_listener)
{
    const char *sender = g_dbus_method_invocation_get_sender(invocation);
    GDBusConnection *listener_conn;
    g_autoptr(GError) err = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketConnection) socket_conn = NULL;
    g_autofree char *guid = g_dbus_generate_guid();
    DBusDisplayListener *listener;
    int fd;

#ifdef G_OS_WIN32
    if (!dbus_win32_import_socket(invocation, arg_listener, &fd)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
#else
    fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(arg_listener), &err);
    if (err) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Couldn't get peer fd: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
#endif

    socket = g_socket_new_from_fd(fd, &err);
    if (err) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Couldn't make a socket: %s", err->message);
#ifdef G_OS_WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    socket_conn = g_socket_connection_factory_create_connection(socket);

    qemu_dbus_display1_console_complete_register_listener(
        ddc->iface, invocation
#ifdef G_OS_UNIX
        , NULL
#endif
    );

    listener_conn = g_dbus_connection_new_sync(
        G_IO_STREAM(socket_conn),
        guid,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER,
        NULL, NULL, &err);
    if (err) {
        error_report("Failed to setup peer connection: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    listener = dbus_display_listener_new(sender, listener_conn, ddc);
    if (!listener) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    g_ptr_array_add(ddc->listeners, listener);
    g_object_connect(listener_conn,
                     "swapped-signal::closed", listener_vanished_cb, listener,
                     NULL);

    trace_dbus_registered_listener(sender);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_kbd_press(DBusDisplayConsole *ddc,
               GDBusMethodInvocation *invocation,
               guint arg_keycode)
{
    QKeyCode qcode = qemu_input_key_number_to_qcode(arg_keycode);

    trace_dbus_kbd_press(arg_keycode);

    qkbd_state_key_event(ddc->kbd, qcode, true);

    qemu_dbus_display1_keyboard_complete_press(ddc->iface_kbd, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_kbd_release(DBusDisplayConsole *ddc,
                 GDBusMethodInvocation *invocation,
                 guint arg_keycode)
{
    QKeyCode qcode = qemu_input_key_number_to_qcode(arg_keycode);

    trace_dbus_kbd_release(arg_keycode);

    qkbd_state_key_event(ddc->kbd, qcode, false);

    qemu_dbus_display1_keyboard_complete_release(ddc->iface_kbd, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dbus_kbd_qemu_leds_updated(void *data, int ledstate)
{
    DBusDisplayConsole *ddc = DBUS_DISPLAY_CONSOLE(data);

    qemu_dbus_display1_keyboard_set_modifiers(ddc->iface_kbd, ledstate);
}

static gboolean
dbus_mouse_rel_motion(DBusDisplayConsole *ddc,
                      GDBusMethodInvocation *invocation,
                      int dx, int dy)
{
    trace_dbus_mouse_rel_motion(dx, dy);

    if (qemu_input_is_absolute(ddc->dcl.con)) {
        g_dbus_method_invocation_return_error(
            invocation, DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_INVALID,
            "Mouse is not relative");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    qemu_input_queue_rel(ddc->dcl.con, INPUT_AXIS_X, dx);
    qemu_input_queue_rel(ddc->dcl.con, INPUT_AXIS_Y, dy);
    qemu_input_event_sync();

    qemu_dbus_display1_mouse_complete_rel_motion(ddc->iface_mouse,
                                                    invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_touch_send_event(DBusDisplayConsole *ddc,
                      GDBusMethodInvocation *invocation,
                      guint kind, uint64_t num_slot,
                      double x, double y)
{
    Error *error = NULL;
    int width, height;
    trace_dbus_touch_send_event(kind, num_slot, x, y);

    if (kind != INPUT_MULTI_TOUCH_TYPE_BEGIN &&
        kind != INPUT_MULTI_TOUCH_TYPE_UPDATE &&
        kind != INPUT_MULTI_TOUCH_TYPE_CANCEL &&
        kind != INPUT_MULTI_TOUCH_TYPE_END)
    {
        g_dbus_method_invocation_return_error(
            invocation, DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_INVALID,
            "Invalid touch event kind");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    width = qemu_console_get_width(ddc->dcl.con, 0);
    height = qemu_console_get_height(ddc->dcl.con, 0);

    console_handle_touch_event(ddc->dcl.con, touch_slots,
                               num_slot, width, height,
                               x, y, kind, &error);
    if (error != NULL) {
        g_dbus_method_invocation_return_error(
            invocation, DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_INVALID,
            error_get_pretty(error), NULL);
        error_free(error);
    } else {
        qemu_dbus_display1_multi_touch_complete_send_event(ddc->iface_touch,
                                                           invocation);
    }
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_mouse_set_pos(DBusDisplayConsole *ddc,
                   GDBusMethodInvocation *invocation,
                   guint x, guint y)
{
    int width, height;

    trace_dbus_mouse_set_pos(x, y);

    if (!qemu_input_is_absolute(ddc->dcl.con)) {
        g_dbus_method_invocation_return_error(
            invocation, DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_INVALID,
            "Mouse is not absolute");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    width = qemu_console_get_width(ddc->dcl.con, 0);
    height = qemu_console_get_height(ddc->dcl.con, 0);
    if (x >= width || y >= height) {
        g_dbus_method_invocation_return_error(
            invocation, DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_INVALID,
            "Invalid mouse position");
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
    qemu_input_queue_abs(ddc->dcl.con, INPUT_AXIS_X, x, 0, width);
    qemu_input_queue_abs(ddc->dcl.con, INPUT_AXIS_Y, y, 0, height);
    qemu_input_event_sync();

    qemu_dbus_display1_mouse_complete_set_abs_position(ddc->iface_mouse,
                                                          invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_mouse_press(DBusDisplayConsole *ddc,
                 GDBusMethodInvocation *invocation,
                 guint button)
{
    trace_dbus_mouse_press(button);

    qemu_input_queue_btn(ddc->dcl.con, button, true);
    qemu_input_event_sync();

    qemu_dbus_display1_mouse_complete_press(ddc->iface_mouse, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_mouse_release(DBusDisplayConsole *ddc,
                   GDBusMethodInvocation *invocation,
                   guint button)
{
    trace_dbus_mouse_release(button);

    qemu_input_queue_btn(ddc->dcl.con, button, false);
    qemu_input_event_sync();

    qemu_dbus_display1_mouse_complete_release(ddc->iface_mouse, invocation);

    return DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dbus_mouse_update_is_absolute(DBusDisplayConsole *ddc)
{
    g_object_set(ddc->iface_mouse,
                 "is-absolute", qemu_input_is_absolute(ddc->dcl.con),
                 NULL);
}

static void
dbus_mouse_mode_change(Notifier *notify, void *data)
{
    DBusDisplayConsole *ddc =
        container_of(notify, DBusDisplayConsole, mouse_mode_notifier);

    dbus_mouse_update_is_absolute(ddc);
}

int dbus_display_console_get_index(DBusDisplayConsole *ddc)
{
    return qemu_console_get_index(ddc->dcl.con);
}

DBusDisplayConsole *
dbus_display_console_new(DBusDisplay *display, QemuConsole *con)
{
    g_autofree char *path = NULL;
    g_autofree char *label = NULL;
    char device_addr[256] = "";
    DBusDisplayConsole *ddc;
    int idx, i;
    const char *interfaces[] = {
        "org.qemu.Display1.Keyboard",
        "org.qemu.Display1.Mouse",
        "org.qemu.Display1.MultiTouch",
        NULL
    };

    assert(display);
    assert(con);

    label = qemu_console_get_label(con);
    idx = qemu_console_get_index(con);
    path = g_strdup_printf(DBUS_DISPLAY1_ROOT "/Console_%d", idx);
    ddc = g_object_new(DBUS_DISPLAY_TYPE_CONSOLE,
                        "g-object-path", path,
                        NULL);
    ddc->display = display;
    ddc->dcl.con = con;
    /* handle errors, and skip non graphics? */
    qemu_console_fill_device_address(
        con, device_addr, sizeof(device_addr), NULL);

    ddc->iface = qemu_dbus_display1_console_skeleton_new();
    g_object_set(ddc->iface,
        "label", label,
        "type", qemu_console_is_graphic(con) ? "Graphic" : "Text",
        "head", qemu_console_get_head(con),
        "width", qemu_console_get_width(con, 0),
        "height", qemu_console_get_height(con, 0),
        "device-address", device_addr,
        "interfaces", interfaces,
        NULL);
    g_object_connect(ddc->iface,
        "swapped-signal::handle-register-listener",
        dbus_console_register_listener, ddc,
        "swapped-signal::handle-set-uiinfo",
        dbus_console_set_ui_info, ddc,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(ddc),
        G_DBUS_INTERFACE_SKELETON(ddc->iface));

    ddc->kbd = qkbd_state_init(con);
    ddc->iface_kbd = qemu_dbus_display1_keyboard_skeleton_new();
    qemu_add_led_event_handler(dbus_kbd_qemu_leds_updated, ddc);
    g_object_connect(ddc->iface_kbd,
        "swapped-signal::handle-press", dbus_kbd_press, ddc,
        "swapped-signal::handle-release", dbus_kbd_release, ddc,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(ddc),
        G_DBUS_INTERFACE_SKELETON(ddc->iface_kbd));

    ddc->iface_mouse = qemu_dbus_display1_mouse_skeleton_new();
    g_object_connect(ddc->iface_mouse,
        "swapped-signal::handle-set-abs-position", dbus_mouse_set_pos, ddc,
        "swapped-signal::handle-rel-motion", dbus_mouse_rel_motion, ddc,
        "swapped-signal::handle-press", dbus_mouse_press, ddc,
        "swapped-signal::handle-release", dbus_mouse_release, ddc,
        NULL);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(ddc),
        G_DBUS_INTERFACE_SKELETON(ddc->iface_mouse));

    ddc->iface_touch = qemu_dbus_display1_multi_touch_skeleton_new();
    g_object_connect(ddc->iface_touch,
        "swapped-signal::handle-send-event", dbus_touch_send_event, ddc,
        NULL);
    qemu_dbus_display1_multi_touch_set_max_slots(ddc->iface_touch,
                                                 INPUT_EVENT_SLOTS_MAX);
    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(ddc),
        G_DBUS_INTERFACE_SKELETON(ddc->iface_touch));

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        struct touch_slot *slot = &touch_slots[i];
        slot->tracking_id = -1;
    }

    register_displaychangelistener(&ddc->dcl);
    ddc->mouse_mode_notifier.notify = dbus_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&ddc->mouse_mode_notifier);
    dbus_mouse_update_is_absolute(ddc);

    return ddc;
}
