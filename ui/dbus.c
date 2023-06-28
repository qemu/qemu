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
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/dbus.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "ui/dbus-module.h"
#ifdef CONFIG_OPENGL
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif
#include "audio/audio.h"
#include "audio/audio_int.h"
#include "qapi/error.h"
#include "trace.h"

#include "dbus.h"

static DBusDisplay *dbus_display;

#ifdef CONFIG_OPENGL
static QEMUGLContext dbus_create_context(DisplayGLCtx *dgc,
                                         QEMUGLParams *params)
{
    eglMakeCurrent(qemu_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   qemu_egl_rn_ctx);
    return qemu_egl_create_context(dgc, params);
}

static bool
dbus_is_compatible_dcl(DisplayGLCtx *dgc,
                       DisplayChangeListener *dcl)
{
    return
        dcl->ops == &dbus_gl_dcl_ops ||
        dcl->ops == &dbus_console_dcl_ops;
}

static void
dbus_create_texture(DisplayGLCtx *ctx, DisplaySurface *surface)
{
    surface_gl_create_texture(ctx->gls, surface);
}

static void
dbus_destroy_texture(DisplayGLCtx *ctx, DisplaySurface *surface)
{
    surface_gl_destroy_texture(ctx->gls, surface);
}

static void
dbus_update_texture(DisplayGLCtx *ctx, DisplaySurface *surface,
                    int x, int y, int w, int h)
{
    surface_gl_update_texture(ctx->gls, surface, x, y, w, h);
}

static const DisplayGLCtxOps dbus_gl_ops = {
    .dpy_gl_ctx_is_compatible_dcl = dbus_is_compatible_dcl,
    .dpy_gl_ctx_create       = dbus_create_context,
    .dpy_gl_ctx_destroy      = qemu_egl_destroy_context,
    .dpy_gl_ctx_make_current = qemu_egl_make_context_current,
    .dpy_gl_ctx_create_texture = dbus_create_texture,
    .dpy_gl_ctx_destroy_texture = dbus_destroy_texture,
    .dpy_gl_ctx_update_texture = dbus_update_texture,
};
#endif

static NotifierList dbus_display_notifiers =
    NOTIFIER_LIST_INITIALIZER(dbus_display_notifiers);

void
dbus_display_notifier_add(Notifier *notifier)
{
    notifier_list_add(&dbus_display_notifiers, notifier);
}

static void
dbus_display_notifier_remove(Notifier *notifier)
{
    notifier_remove(notifier);
}

void
dbus_display_notify(DBusDisplayEvent *event)
{
    notifier_list_notify(&dbus_display_notifiers, event);
}

static void
dbus_display_init(Object *o)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);
    g_autoptr(GDBusObjectSkeleton) vm = NULL;

#ifdef CONFIG_OPENGL
    dd->glctx.ops = &dbus_gl_ops;
    if (display_opengl) {
        dd->glctx.gls = qemu_gl_init_shader();
    }
#endif
    dd->iface = qemu_dbus_display1_vm_skeleton_new();
    dd->consoles = g_ptr_array_new_with_free_func(g_object_unref);

    dd->server = g_dbus_object_manager_server_new(DBUS_DISPLAY1_ROOT);

    vm = g_dbus_object_skeleton_new(DBUS_DISPLAY1_ROOT "/VM");
    g_dbus_object_skeleton_add_interface(
        vm, G_DBUS_INTERFACE_SKELETON(dd->iface));
    g_dbus_object_manager_server_export(dd->server, vm);

    dbus_clipboard_init(dd);
    dbus_chardev_init(dd);
}

static void
dbus_display_finalize(Object *o)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    if (dd->notifier.notify) {
        dbus_display_notifier_remove(&dd->notifier);
    }

    qemu_clipboard_peer_unregister(&dd->clipboard_peer);
    g_clear_object(&dd->clipboard);

    g_clear_object(&dd->server);
    g_clear_pointer(&dd->consoles, g_ptr_array_unref);
    if (dd->add_client_cancellable) {
        g_cancellable_cancel(dd->add_client_cancellable);
    }
    g_clear_object(&dd->add_client_cancellable);
    g_clear_object(&dd->bus);
    g_clear_object(&dd->iface);
    g_free(dd->dbus_addr);
    g_free(dd->audiodev);
#ifdef CONFIG_OPENGL
    g_clear_pointer(&dd->glctx.gls, qemu_gl_fini_shader);
#endif
    dbus_display = NULL;
}

static bool
dbus_display_add_console(DBusDisplay *dd, int idx, Error **errp)
{
    QemuConsole *con;
    DBusDisplayConsole *dbus_console;

    con = qemu_console_lookup_by_index(idx);
    assert(con);

    if (qemu_console_is_graphic(con) &&
        dd->gl_mode != DISPLAYGL_MODE_OFF) {
        qemu_console_set_display_gl_ctx(con, &dd->glctx);
    }

    dbus_console = dbus_display_console_new(dd, con);
    g_ptr_array_insert(dd->consoles, idx, dbus_console);
    g_dbus_object_manager_server_export(dd->server,
                                        G_DBUS_OBJECT_SKELETON(dbus_console));
    return true;
}

static void
dbus_display_complete(UserCreatable *uc, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(uc);
    g_autoptr(GError) err = NULL;
    g_autofree char *uuid = qemu_uuid_unparse_strdup(&qemu_uuid);
    g_autoptr(GArray) consoles = NULL;
    GVariant *console_ids;
    int idx;

    if (!object_resolve_path_type("", TYPE_DBUS_DISPLAY, NULL)) {
        error_setg(errp, "There is already an instance of %s",
                   TYPE_DBUS_DISPLAY);
        return;
    }

    if (dd->p2p) {
        /* wait for dbus_display_add_client() */
        dbus_display = dd;
    } else if (dd->dbus_addr && *dd->dbus_addr) {
        dd->bus = g_dbus_connection_new_for_address_sync(dd->dbus_addr,
                        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                        NULL, NULL, &err);
    } else {
        dd->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    }
    if (err) {
        error_setg(errp, "failed to connect to DBus: %s", err->message);
        return;
    }

    if (dd->audiodev && *dd->audiodev) {
        AudioState *audio_state = audio_state_by_name(dd->audiodev);
        if (!audio_state) {
            error_setg(errp, "Audiodev '%s' not found", dd->audiodev);
            return;
        }
        if (!g_str_equal(audio_state->drv->name, "dbus")) {
            error_setg(errp, "Audiodev '%s' is not compatible with DBus",
                       dd->audiodev);
            return;
        }
        audio_state->drv->set_dbus_server(audio_state, dd->server, dd->p2p);
    }

    consoles = g_array_new(FALSE, FALSE, sizeof(guint32));
    for (idx = 0;; idx++) {
        if (!qemu_console_lookup_by_index(idx)) {
            break;
        }
        if (!dbus_display_add_console(dd, idx, errp)) {
            return;
        }
        g_array_append_val(consoles, idx);
    }

    console_ids = g_variant_new_from_data(
        G_VARIANT_TYPE("au"),
        consoles->data, consoles->len * sizeof(guint32), TRUE,
        (GDestroyNotify)g_array_unref, consoles);
    g_steal_pointer(&consoles);
    g_object_set(dd->iface,
                 "name", qemu_name ?: "QEMU " QEMU_VERSION,
                 "uuid", uuid,
                 "console-ids", console_ids,
                 NULL);

    if (dd->bus) {
        g_dbus_object_manager_server_set_connection(dd->server, dd->bus);
        g_bus_own_name_on_connection(dd->bus, "org.qemu",
                                     G_BUS_NAME_OWNER_FLAGS_NONE,
                                     NULL, NULL, NULL, NULL);
    }
}

static void
dbus_display_add_client_ready(GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusConnection) conn = NULL;

    g_clear_object(&dbus_display->add_client_cancellable);

    conn = g_dbus_connection_new_finish(res, &err);
    if (!conn) {
        error_printf("Failed to accept D-Bus client: %s", err->message);
    }

    g_dbus_object_manager_server_set_connection(dbus_display->server, conn);
    g_dbus_connection_start_message_processing(conn);
}


static bool
dbus_display_add_client(int csock, Error **errp)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketConnection) conn = NULL;
    g_autofree char *guid = g_dbus_generate_guid();

    if (!dbus_display) {
        error_setg(errp, "p2p connections not accepted in bus mode");
        return false;
    }

    if (dbus_display->add_client_cancellable) {
        g_cancellable_cancel(dbus_display->add_client_cancellable);
    }

#ifdef WIN32
    socket = g_socket_new_from_fd(_get_osfhandle(csock), &err);
#else
    socket = g_socket_new_from_fd(csock, &err);
#endif
    if (!socket) {
        error_setg(errp, "Failed to setup D-Bus socket: %s", err->message);
        close(csock);
        return false;
    }
#ifdef WIN32
    /* socket owns the SOCKET handle now, so release our osf handle */
    qemu_close_socket_osfhandle(csock);
#endif

    conn = g_socket_connection_factory_create_connection(socket);

    dbus_display->add_client_cancellable = g_cancellable_new();

    g_dbus_connection_new(G_IO_STREAM(conn),
                          guid,
                          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER |
                          G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
                          NULL,
                          dbus_display->add_client_cancellable,
                          dbus_display_add_client_ready,
                          NULL);

    return true;
}

static bool
get_dbus_p2p(Object *o, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    return dd->p2p;
}

static void
set_dbus_p2p(Object *o, bool p2p, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    dd->p2p = p2p;
}

static char *
get_dbus_addr(Object *o, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    return g_strdup(dd->dbus_addr);
}

static void
set_dbus_addr(Object *o, const char *str, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    g_free(dd->dbus_addr);
    dd->dbus_addr = g_strdup(str);
}

static char *
get_audiodev(Object *o, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    return g_strdup(dd->audiodev);
}

static void
set_audiodev(Object *o, const char *str, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    g_free(dd->audiodev);
    dd->audiodev = g_strdup(str);
}


static int
get_gl_mode(Object *o, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    return dd->gl_mode;
}

static void
set_gl_mode(Object *o, int val, Error **errp)
{
    DBusDisplay *dd = DBUS_DISPLAY(o);

    dd->gl_mode = val;
}

static void
dbus_display_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = dbus_display_complete;
    object_class_property_add_bool(oc, "p2p", get_dbus_p2p, set_dbus_p2p);
    object_class_property_add_str(oc, "addr", get_dbus_addr, set_dbus_addr);
    object_class_property_add_str(oc, "audiodev", get_audiodev, set_audiodev);
    object_class_property_add_enum(oc, "gl-mode",
                                   "DisplayGLMode", &DisplayGLMode_lookup,
                                   get_gl_mode, set_gl_mode);
}

#define TYPE_CHARDEV_VC "chardev-vc"

typedef struct DBusVCClass {
    DBusChardevClass parent_class;

    void (*parent_parse)(QemuOpts *opts, ChardevBackend *b, Error **errp);
} DBusVCClass;

DECLARE_CLASS_CHECKERS(DBusVCClass, DBUS_VC,
                       TYPE_CHARDEV_VC)

static void
dbus_vc_parse(QemuOpts *opts, ChardevBackend *backend,
              Error **errp)
{
    DBusVCClass *klass = DBUS_VC_CLASS(object_class_by_name(TYPE_CHARDEV_VC));
    const char *name = qemu_opt_get(opts, "name");
    const char *id = qemu_opts_id(opts);

    if (name == NULL) {
        if (g_str_has_prefix(id, "compat_monitor")) {
            name = "org.qemu.monitor.hmp.0";
        } else if (g_str_has_prefix(id, "serial")) {
            name = "org.qemu.console.serial.0";
        } else {
            name = "";
        }
        if (!qemu_opt_set(opts, "name", name, errp)) {
            return;
        }
    }

    klass->parent_parse(opts, backend, errp);
}

static void
dbus_vc_class_init(ObjectClass *oc, void *data)
{
    DBusVCClass *klass = DBUS_VC_CLASS(oc);
    ChardevClass *cc = CHARDEV_CLASS(oc);

    klass->parent_parse = cc->parse;
    cc->parse = dbus_vc_parse;
}

static const TypeInfo dbus_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV_DBUS,
    .class_size = sizeof(DBusVCClass),
    .class_init = dbus_vc_class_init,
};

static void
early_dbus_init(DisplayOptions *opts)
{
    DisplayGLMode mode = opts->has_gl ? opts->gl : DISPLAYGL_MODE_OFF;

    if (mode != DISPLAYGL_MODE_OFF) {
#ifdef CONFIG_OPENGL
        egl_init(opts->u.dbus.rendernode, mode, &error_fatal);
#else
        error_report("dbus: GL rendering is not supported");
#endif
    }

    type_register(&dbus_vc_type_info);
}

static void
dbus_init(DisplayState *ds, DisplayOptions *opts)
{
    DisplayGLMode mode = opts->has_gl ? opts->gl : DISPLAYGL_MODE_OFF;

    if (opts->u.dbus.addr && opts->u.dbus.p2p) {
        error_report("dbus: can't accept both addr=X and p2p=yes options");
        exit(1);
    }

    using_dbus_display = 1;

    object_new_with_props(TYPE_DBUS_DISPLAY,
                          object_get_objects_root(),
                          "dbus-display", &error_fatal,
                          "addr", opts->u.dbus.addr ?: "",
                          "audiodev", opts->u.dbus.audiodev ?: "",
                          "gl-mode", DisplayGLMode_str(mode),
                          "p2p", yes_no(opts->u.dbus.p2p),
                          NULL);
}

static const TypeInfo dbus_display_info = {
    .name = TYPE_DBUS_DISPLAY,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DBusDisplay),
    .instance_init = dbus_display_init,
    .instance_finalize = dbus_display_finalize,
    .class_init = dbus_display_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static QemuDisplay qemu_display_dbus = {
    .type       = DISPLAY_TYPE_DBUS,
    .early_init = early_dbus_init,
    .init       = dbus_init,
};

static void register_dbus(void)
{
    qemu_dbus_display = (struct QemuDBusDisplayOps) {
        .add_client = dbus_display_add_client,
    };
    type_register_static(&dbus_display_info);
    qemu_display_register(&qemu_display_dbus);
}

type_init(register_dbus);

#ifdef CONFIG_OPENGL
module_dep("ui-opengl");
#endif
