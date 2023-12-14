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
#include "trace.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/option.h"

#ifdef G_OS_UNIX
#include <gio/gunixfdlist.h>
#endif

#include "dbus.h"

static char *
dbus_display_chardev_path(DBusChardev *chr)
{
    return g_strdup_printf(DBUS_DISPLAY1_ROOT "/Chardev_%s",
                           CHARDEV(chr)->label);
}

static void
dbus_display_chardev_export(DBusDisplay *dpy, DBusChardev *chr)
{
    g_autoptr(GDBusObjectSkeleton) sk = NULL;
    g_autofree char *path = dbus_display_chardev_path(chr);

    if (chr->exported) {
        return;
    }

    sk = g_dbus_object_skeleton_new(path);
    g_dbus_object_skeleton_add_interface(
        sk, G_DBUS_INTERFACE_SKELETON(chr->iface));
    g_dbus_object_manager_server_export(dpy->server, sk);
    chr->exported = true;
}

static void
dbus_display_chardev_unexport(DBusDisplay *dpy, DBusChardev *chr)
{
    g_autofree char *path = dbus_display_chardev_path(chr);

    if (!chr->exported) {
        return;
    }

    g_dbus_object_manager_server_unexport(dpy->server, path);
    chr->exported = false;
}

static int
dbus_display_chardev_foreach(Object *obj, void *data)
{
    DBusDisplay *dpy = DBUS_DISPLAY(data);

    if (!CHARDEV_IS_DBUS(obj)) {
        return 0;
    }

    dbus_display_chardev_export(dpy, DBUS_CHARDEV(obj));

    return 0;
}

static void
dbus_display_on_notify(Notifier *notifier, void *data)
{
    DBusDisplay *dpy = container_of(notifier, DBusDisplay, notifier);
    DBusDisplayEvent *event = data;

    switch (event->type) {
    case DBUS_DISPLAY_CHARDEV_OPEN:
        dbus_display_chardev_export(dpy, event->chardev);
        break;
    case DBUS_DISPLAY_CHARDEV_CLOSE:
        dbus_display_chardev_unexport(dpy, event->chardev);
        break;
    }
}

void
dbus_chardev_init(DBusDisplay *dpy)
{
    dpy->notifier.notify = dbus_display_on_notify;
    dbus_display_notifier_add(&dpy->notifier);

    object_child_foreach(container_get(object_get_root(), "/chardevs"),
                         dbus_display_chardev_foreach, dpy);
}

static gboolean
dbus_chr_register(
    DBusChardev *dc,
    GDBusMethodInvocation *invocation,
#ifdef G_OS_UNIX
    GUnixFDList *fd_list,
#endif
    GVariant *arg_stream,
    QemuDBusDisplay1Chardev *object)
{
    g_autoptr(GError) err = NULL;
    int fd;

#ifdef G_OS_WIN32
    if (!dbus_win32_import_socket(invocation, arg_stream, &fd)) {
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
#else
    fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(arg_stream), &err);
    if (err) {
        g_dbus_method_invocation_return_error(
            invocation,
            DBUS_DISPLAY_ERROR,
            DBUS_DISPLAY_ERROR_FAILED,
            "Couldn't get peer FD: %s", err->message);
        return DBUS_METHOD_INVOCATION_HANDLED;
    }
#endif

    if (qemu_chr_add_client(CHARDEV(dc), fd) < 0) {
        g_dbus_method_invocation_return_error(invocation,
                                              DBUS_DISPLAY_ERROR,
                                              DBUS_DISPLAY_ERROR_FAILED,
                                              "Couldn't register FD!");
#ifdef G_OS_WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return DBUS_METHOD_INVOCATION_HANDLED;
    }

    g_object_set(dc->iface,
                 "owner", g_dbus_method_invocation_get_sender(invocation),
                 NULL);

    qemu_dbus_display1_chardev_complete_register(object, invocation
#ifndef G_OS_WIN32
                                                 , NULL
#endif
        );
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
dbus_chr_send_break(
    DBusChardev *dc,
    GDBusMethodInvocation *invocation,
    QemuDBusDisplay1Chardev *object)
{
    qemu_chr_be_event(CHARDEV(dc), CHR_EVENT_BREAK);

    qemu_dbus_display1_chardev_complete_send_break(object, invocation);
    return DBUS_METHOD_INVOCATION_HANDLED;
}

static void
dbus_chr_open(Chardev *chr, ChardevBackend *backend,
              bool *be_opened, Error **errp)
{
    ERRP_GUARD();

    DBusChardev *dc = DBUS_CHARDEV(chr);
    DBusDisplayEvent event = {
        .type = DBUS_DISPLAY_CHARDEV_OPEN,
        .chardev = dc,
    };
    g_autoptr(ChardevBackend) be = NULL;
    g_autoptr(QemuOpts) opts = NULL;

    dc->iface = qemu_dbus_display1_chardev_skeleton_new();
    g_object_set(dc->iface, "name", backend->u.dbus.data->name, NULL);
    g_object_connect(dc->iface,
                     "swapped-signal::handle-register",
                     dbus_chr_register, dc,
                     "swapped-signal::handle-send-break",
                     dbus_chr_send_break, dc,
                     NULL);

    dbus_display_notify(&event);

    be = g_new0(ChardevBackend, 1);
    opts = qemu_opts_create(qemu_find_opts("chardev"), NULL, 0, &error_abort);
    qemu_opt_set(opts, "server", "on", &error_abort);
    qemu_opt_set(opts, "wait", "off", &error_abort);
    CHARDEV_CLASS(object_class_by_name(TYPE_CHARDEV_SOCKET))->parse(
        opts, be, errp);
    if (*errp) {
        return;
    }
    CHARDEV_CLASS(object_class_by_name(TYPE_CHARDEV_SOCKET))->open(
        chr, be, be_opened, errp);
}

static void
dbus_chr_set_fe_open(Chardev *chr, int fe_open)
{
    DBusChardev *dc = DBUS_CHARDEV(chr);

    g_object_set(dc->iface, "feopened", fe_open, NULL);
}

static void
dbus_chr_set_echo(Chardev *chr, bool echo)
{
    DBusChardev *dc = DBUS_CHARDEV(chr);

    g_object_set(dc->iface, "echo", echo, NULL);
}

static void
dbus_chr_be_event(Chardev *chr, QEMUChrEvent event)
{
    DBusChardev *dc = DBUS_CHARDEV(chr);
    DBusChardevClass *klass = DBUS_CHARDEV_GET_CLASS(chr);

    switch (event) {
    case CHR_EVENT_CLOSED:
        if (dc->iface) {
            /* on finalize, iface is set to NULL */
            g_object_set(dc->iface, "owner", "", NULL);
        }
        break;
    default:
        break;
    };

    klass->parent_chr_be_event(chr, event);
}

static void
dbus_chr_parse(QemuOpts *opts, ChardevBackend *backend,
               Error **errp)
{
    const char *name = qemu_opt_get(opts, "name");
    ChardevDBus *dbus;

    if (name == NULL) {
        error_setg(errp, "chardev: dbus: no name given");
        return;
    }

    backend->type = CHARDEV_BACKEND_KIND_DBUS;
    dbus = backend->u.dbus.data = g_new0(ChardevDBus, 1);
    qemu_chr_parse_common(opts, qapi_ChardevDBus_base(dbus));
    dbus->name = g_strdup(name);
}

static void
char_dbus_class_init(ObjectClass *oc, void *data)
{
    DBusChardevClass *klass = DBUS_CHARDEV_CLASS(oc);
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = dbus_chr_parse;
    cc->open = dbus_chr_open;
    cc->chr_set_fe_open = dbus_chr_set_fe_open;
    cc->chr_set_echo = dbus_chr_set_echo;
    klass->parent_chr_be_event = cc->chr_be_event;
    cc->chr_be_event = dbus_chr_be_event;
}

static void
char_dbus_finalize(Object *obj)
{
    DBusChardev *dc = DBUS_CHARDEV(obj);
    DBusDisplayEvent event = {
        .type = DBUS_DISPLAY_CHARDEV_CLOSE,
        .chardev = dc,
    };

    dbus_display_notify(&event);
    g_clear_object(&dc->iface);
}

static const TypeInfo char_dbus_type_info = {
    .name = TYPE_CHARDEV_DBUS,
    .parent = TYPE_CHARDEV_SOCKET,
    .class_size = sizeof(DBusChardevClass),
    .instance_size = sizeof(DBusChardev),
    .instance_finalize = char_dbus_finalize,
    .class_init = char_dbus_class_init,
};
module_obj(TYPE_CHARDEV_DBUS);

static void
register_types(void)
{
    type_register_static(&char_dbus_type_info);
}

type_init(register_types);
